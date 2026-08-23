/*
 * viosnd restricted-DMA-pool (rdmapool) support -- see ViosndRdma.h for the
 * design overview.
 *
 * Copyright (c) 2026
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include "precomp.h"
#include "ViosndRdma.h"

#define VIOSND_RDMA_TAG 'RdnS'

static ULONG ViosndRdmaPagesFor(_In_ SIZE_T Size)
{
    SIZE_T rounded = ROUND_TO_PAGES(Size);

    if (rounded == 0)
    {
        return 0;
    }
    return (ULONG)(rounded / PAGE_SIZE);
}

NTSTATUS
ViosndRdmaConnect(_Inout_ PVIOSND_RDMA Rdma, _In_ ULONG RingPages, _In_ ULONG DataPages)
{
    NTSTATUS status;
    ULONG pageCount;
    SIZE_T bitmapBytes;

    RtlZeroMemory(Rdma, sizeof(*Rdma));
    KeInitializeSpinLock(&Rdma->Lock);

    /* MetaPages is 0: viosnd does not use the client library's fixed-slot
     * bounce allocator, it sub-allocates the whole region itself. */
    status = RdmaClientConnectEx(&Rdma->Client, "viosnd", RingPages, 0, DataPages);
    if (!NT_SUCCESS(status))
    {
        return status;
    }

    pageCount = (ULONG)(Rdma->Client.Size / PAGE_SIZE);
    if (pageCount == 0)
    {
        RdmaClientDisconnect(&Rdma->Client);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* RtlInitializeBitMap wants a ULONG-aligned buffer sized in whole ULONGs. */
    bitmapBytes = ((SIZE_T)((pageCount + 31) / 32)) * sizeof(ULONG);
    Rdma->BitmapBuffer = (PULONG)ExAllocatePoolUninitialized(NonPagedPoolNx, bitmapBytes, VIOSND_RDMA_TAG);
    if (Rdma->BitmapBuffer == NULL)
    {
        RdmaClientDisconnect(&Rdma->Client);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(Rdma->BitmapBuffer, bitmapBytes);
    RtlInitializeBitMap(&Rdma->Bitmap, Rdma->BitmapBuffer, pageCount);
    RtlClearAllBits(&Rdma->Bitmap);
    Rdma->PageCount = pageCount;

    DbgPrint("viosnd rdmapool: %u pages (%u KB) at VA=%p PA=0x%I64x\n",
             pageCount,
             pageCount * (PAGE_SIZE / 1024),
             Rdma->Client.BaseVA,
             Rdma->Client.BasePA.QuadPart);
    return STATUS_SUCCESS;
}

VOID ViosndRdmaDisconnect(_Inout_ PVIOSND_RDMA Rdma)
{
    if (Rdma->BitmapBuffer != NULL)
    {
        ExFreePoolWithTag(Rdma->BitmapBuffer, VIOSND_RDMA_TAG);
        Rdma->BitmapBuffer = NULL;
    }
    Rdma->PageCount = 0;
    RdmaClientDisconnect(&Rdma->Client);
}

_Ret_maybenull_ PVOID ViosndRdmaAlloc(_Inout_ PVIOSND_RDMA Rdma,
                                      _In_ SIZE_T Size,
                                      _Out_ PPHYSICAL_ADDRESS LogicalAddress)
{
    KIRQL irql;
    ULONG pages;
    ULONG index;
    PVOID va;

    LogicalAddress->QuadPart = 0;
    if (!ViosndRdmaActive(Rdma))
    {
        return NULL;
    }

    pages = ViosndRdmaPagesFor(Size);
    if (pages == 0 || pages > Rdma->PageCount)
    {
        return NULL;
    }

    KeAcquireSpinLock(&Rdma->Lock, &irql);
    index = RtlFindClearBitsAndSet(&Rdma->Bitmap, pages, 0);
    KeReleaseSpinLock(&Rdma->Lock, irql);

    if (index == 0xFFFFFFFF)
    {
        DbgPrint("viosnd rdmapool: out of pool memory (%u pages of %u)\n", pages, Rdma->PageCount);
        return NULL;
    }

    va = (PVOID)((PUCHAR)Rdma->Client.BaseVA + ((SIZE_T)index * PAGE_SIZE));
    /* The device reads whatever is here; never hand it the previous stream's
     * audio. */
    RtlZeroMemory(va, (SIZE_T)pages * PAGE_SIZE);
    *LogicalAddress = RdmaClientVAtoPA(&Rdma->Client, va);
    return va;
}

VOID ViosndRdmaFree(_Inout_ PVIOSND_RDMA Rdma, _In_opt_ PVOID Va, _In_ SIZE_T Size)
{
    KIRQL irql;
    ULONG pages;
    SIZE_T offset;
    ULONG index;

    if (Va == NULL || !ViosndRdmaActive(Rdma))
    {
        return;
    }
    if (!RdmaClientOwnsVA(&Rdma->Client, Va))
    {
        /* Not ours: the caller mixed a common-buffer block into the pool path. */
        DbgPrint("viosnd rdmapool: free of foreign VA %p ignored\n", Va);
        return;
    }

    pages = ViosndRdmaPagesFor(Size);
    if (pages == 0)
    {
        return;
    }

    offset = (SIZE_T)((PUCHAR)Va - (PUCHAR)Rdma->Client.BaseVA);
    index = (ULONG)(offset / PAGE_SIZE);
    if (index + pages > Rdma->PageCount)
    {
        return;
    }

    KeAcquireSpinLock(&Rdma->Lock, &irql);
    RtlClearBits(&Rdma->Bitmap, index, pages);
    KeReleaseSpinLock(&Rdma->Lock, irql);
}
