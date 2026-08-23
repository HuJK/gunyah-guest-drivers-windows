// SPDX-License-Identifier: BSD-3-Clause
#include "precomp.h"

#define VIOSND_RDMA_TAG 'aRSV'

static __forceinline ULONG ViosndRdmaPagesFor(_In_ SIZE_T Size)
{
    return (ULONG)((Size + PAGE_SIZE - 1) / PAGE_SIZE);
}

NTSTATUS ViosndRdmaOpen(_Inout_ PVIOSND_RDMA Rdma)
{
    NTSTATUS status;

    RtlZeroMemory(Rdma, sizeof(*Rdma));
    KeInitializeSpinLock(&Rdma->Lock);

    status = RdmaClientOpen(&Rdma->Client, "viosnd");
    if (!NT_SUCCESS(status))
    {
        return status;
    }

    Rdma->Opened = TRUE;
    DbgPrint("viosnd rdmapool: open, pool=%I64u bytes\n", Rdma->Client.LastPoolTotalSize);
    return STATUS_SUCCESS;
}

/* Takes one more region of at least Pages pages. Caller holds the lock. */
static PVIOSND_RDMA_REGION ViosndRdmaAddRegion(_Inout_ PVIOSND_RDMA Rdma, _In_ ULONG Pages)
{
    PVIOSND_RDMA_REGION region;
    PVOID va = NULL;
    PHYSICAL_ADDRESS pa;
    SIZE_T bitmapBytes;
    PULONG bitmapBuffer;
    ULONG want;
    NTSTATUS status;

    if (Rdma->RegionCount >= VIOSND_RDMA_MAX_REGIONS)
    {
        DbgPrint("viosnd rdmapool: region table full (%u)\n", VIOSND_RDMA_MAX_REGIONS);
        return NULL;
    }

    /* Round up so a run of small allocations shares one region rather than
     * scattering the pool, but never below what was actually asked for. */
    want = Pages;
    if (want < VIOSND_RDMA_REGION_GRAIN_PAGES)
    {
        want = VIOSND_RDMA_REGION_GRAIN_PAGES;
    }

    /*
     * Ask for the rounded-up size, and settle for the exact size if the pool
     * cannot spare it. Both are worth trying: the first keeps the pool tidy,
     * the second is the difference between a working device and none.
     */
    status = RdmaClientAllocRegion(&Rdma->Client, want, &va, &pa);
    if (!NT_SUCCESS(status) && want != Pages)
    {
        DbgPrint("viosnd rdmapool: %u pages refused (0x%x), asking for %u\n", want, status, Pages);
        want = Pages;
        status = RdmaClientAllocRegion(&Rdma->Client, want, &va, &pa);
    }
    if (!NT_SUCCESS(status))
    {
        DbgPrint("viosnd rdmapool: %u pages refused (0x%x)\n", want, status);
        return NULL;
    }

    /* RtlInitializeBitMap wants a ULONG-aligned buffer sized in whole ULONGs. */
    bitmapBytes = ((SIZE_T)((want + 31) / 32)) * sizeof(ULONG);
    bitmapBuffer = (PULONG)ExAllocatePoolUninitialized(NonPagedPoolNx, bitmapBytes, VIOSND_RDMA_TAG);
    if (bitmapBuffer == NULL)
    {
        RdmaClientFreeRegion(&Rdma->Client, va, want);
        return NULL;
    }

    region = &Rdma->Regions[Rdma->RegionCount];
    region->BaseVA = va;
    region->BasePA = pa;
    region->Pages = want;
    region->BitmapBuffer = bitmapBuffer;
    RtlZeroMemory(bitmapBuffer, bitmapBytes);
    RtlInitializeBitMap(&region->Bitmap, bitmapBuffer, want);
    RtlClearAllBits(&region->Bitmap);
    Rdma->RegionCount++;

    DbgPrint("viosnd rdmapool: region %u = %u pages at VA=%p PA=0x%I64x\n",
             Rdma->RegionCount - 1,
             want,
             va,
             pa.QuadPart);
    return region;
}

VOID ViosndRdmaClose(_Inout_ PVIOSND_RDMA Rdma)
{
    for (ULONG i = 0; i < Rdma->RegionCount; ++i)
    {
        PVIOSND_RDMA_REGION region = &Rdma->Regions[i];
        if (region->BitmapBuffer != NULL)
        {
            ExFreePoolWithTag(region->BitmapBuffer, VIOSND_RDMA_TAG);
            region->BitmapBuffer = NULL;
        }
        if (region->BaseVA != NULL)
        {
            RdmaClientFreeRegion(&Rdma->Client, region->BaseVA, region->Pages);
            region->BaseVA = NULL;
        }
        region->Pages = 0;
    }
    Rdma->RegionCount = 0;
    Rdma->Opened = FALSE;
    RdmaClientClose(&Rdma->Client);
}

_Ret_maybenull_ PVOID ViosndRdmaAlloc(_Inout_ PVIOSND_RDMA Rdma,
                                      _In_ SIZE_T Size,
                                      _Out_ PPHYSICAL_ADDRESS LogicalAddress)
{
    KIRQL irql;
    ULONG pages;
    PVOID va = NULL;

    LogicalAddress->QuadPart = 0;
    if (!ViosndRdmaActive(Rdma))
    {
        return NULL;
    }

    pages = ViosndRdmaPagesFor(Size);
    if (pages == 0)
    {
        return NULL;
    }

    KeAcquireSpinLock(&Rdma->Lock, &irql);

    for (ULONG attempt = 0; attempt < 2 && va == NULL; ++attempt)
    {
        for (ULONG i = 0; i < Rdma->RegionCount; ++i)
        {
            PVIOSND_RDMA_REGION region = &Rdma->Regions[i];
            ULONG index;

            if (pages > region->Pages)
            {
                continue;
            }
            index = RtlFindClearBitsAndSet(&region->Bitmap, pages, 0);
            if (index == 0xFFFFFFFF)
            {
                continue;
            }
            va = (PVOID)((PUCHAR)region->BaseVA + ((SIZE_T)index * PAGE_SIZE));
            LogicalAddress->QuadPart = region->BasePA.QuadPart + ((LONGLONG)index * PAGE_SIZE);
            break;
        }

        /* Nothing had a long enough run. One more region, then try again --
         * and only once, so a pool that cannot serve this size fails here
         * rather than looping. */
        if (va == NULL && attempt == 0 && ViosndRdmaAddRegion(Rdma, pages) == NULL)
        {
            break;
        }
    }

    KeReleaseSpinLock(&Rdma->Lock, irql);

    if (va == NULL)
    {
        DbgPrint("viosnd rdmapool: no room for %u pages across %u region(s)\n",
                 pages,
                 Rdma->RegionCount);
        return NULL;
    }

    /* The device reads whatever is here; never hand it the previous stream's
     * audio. */
    RtlZeroMemory(va, (SIZE_T)pages * PAGE_SIZE);
    return va;
}

VOID ViosndRdmaFree(_Inout_ PVIOSND_RDMA Rdma, _In_opt_ PVOID Va, _In_ SIZE_T Size)
{
    KIRQL irql;
    ULONG pages;

    if (Va == NULL || !ViosndRdmaActive(Rdma))
    {
        return;
    }
    pages = ViosndRdmaPagesFor(Size);
    if (pages == 0)
    {
        return;
    }

    KeAcquireSpinLock(&Rdma->Lock, &irql);
    for (ULONG i = 0; i < Rdma->RegionCount; ++i)
    {
        PVIOSND_RDMA_REGION region = &Rdma->Regions[i];
        PUCHAR base = (PUCHAR)region->BaseVA;
        SIZE_T span = (SIZE_T)region->Pages * PAGE_SIZE;

        if ((PUCHAR)Va < base || (PUCHAR)Va >= base + span)
        {
            continue;
        }
        RtlClearBits(&region->Bitmap,
                     (ULONG)(((PUCHAR)Va - base) / PAGE_SIZE),
                     pages);
        break;
    }
    KeReleaseSpinLock(&Rdma->Lock, irql);
    /* Regions are kept once taken: a device that just freed a buffer is very
     * likely about to want one the same size, and handing the pages back only
     * to ask for them again invites another client to take the gap. They go
     * back at ViosndRdmaClose. */
}

ULONG ViosndRdmaHeldPages(_In_ PVIOSND_RDMA Rdma)
{
    ULONG total = 0;

    for (ULONG i = 0; i < Rdma->RegionCount; ++i)
    {
        total += Rdma->Regions[i].Pages;
    }
    return total;
}
