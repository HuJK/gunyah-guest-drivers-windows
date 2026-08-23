/*
 * viosnd restricted-DMA-pool (rdmapool) support.
 *
 * In a Gunyah protected VM the guest's own pages are lent, not shared: crosvm
 * can only read and write memory inside the restricted DMA pool. Everything
 * the virtio-snd device touches -- the vrings, the control/event messages and
 * the PCM payload staged into VIRTIO_SND_PCM_XFER requests -- therefore has to
 * live in the pool.
 *
 * viosnd makes that cheap. Every device-visible byte already comes out of one
 * function (ViosndAllocateDmaBuffer) and the WaveRT buffer the Windows audio
 * engine writes into is NOT device-visible -- ViosndWritePcm copies each period
 * from it into a DMA buffer before the descriptor is added. So the whole port
 * is: point that one allocator at the pool.
 *
 * Sub-allocation is page-granular over the single contiguous region rdmapool
 * hands back. Unlike the storage miniports (fixed control slots + fixed data
 * chunks, sized by queue depth) viosnd asks for a handful of variable-sized
 * blocks -- vrings at device init, then one IO pool per stream at stream start
 * -- so a bitmap over the region fits better than a SLIST of fixed slots.
 *
 * Absent pool = absent device interface: RdmaClientConnectEx returns
 * STATUS_NOT_FOUND, Active stays FALSE, and the driver keeps the ordinary
 * AllocateCommonBuffer path. That is what runs on QEMU/KVM and on a
 * pseudo-unprotected Gunyah VM, where guest RAM is shared with the host and
 * there is nothing to stage through.
 *
 * Copyright (c) 2026
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef _VIOSNDRDMA_H_
#define _VIOSNDRDMA_H_

extern "C" {
#include "rdmaclient.h"
}

/*
 * Vrings for the four virtio-snd queues (control, event, tx, rx). A 1024-entry
 * split ring is ~28KB (16KB of descriptors plus avail and used), so 16 pages
 * per queue covers the largest size a backend is likely to advertise -- and
 * VirtIOPCIModern halves the queue until the ring fits if it does not.
 *
 * Rings and payload come out of one bitmap over the whole region, so this and
 * VIOSND_RDMA_DATA_PAGES only decide how much pool is reserved in total.
 */
#define VIOSND_RDMA_RING_PAGES 64u

/*
 * Device-visible payload. Per stream viosnd stages VIOSND_RENDER_IO_POOL_SIZE
 * (12) plus VIOSND_CAPTURE_IO_POOL_SIZE (8) period buffers, each
 * sizeof(VIRTIO_SND_PCM_XFER) + one packet; a shared-mode WASAPI packet at
 * 48kHz stereo 16-bit is ~2KB and rarely exceeds 8KB, so a stream costs well
 * under 256KB. 4MB leaves room for every subdevice plus the control and event
 * buffers, without taking a storage-sized bite out of a pool that viostor and
 * NetKVM share.
 */
#define VIOSND_RDMA_DATA_PAGES 1024u

typedef struct _VIOSND_RDMA {
    RDMA_CLIENT Client;
    /* Guards the bitmap. Allocation happens at PASSIVE_LEVEL (device init and
     * stream start); the lock is still taken at DISPATCH so a future caller on
     * the streaming path cannot corrupt the map. */
    KSPIN_LOCK Lock;
    RTL_BITMAP Bitmap;
    PULONG BitmapBuffer;
    ULONG PageCount;
} VIOSND_RDMA, *PVIOSND_RDMA;

/*
 * Connect and carve the region into pages. Returns STATUS_NOT_FOUND when
 * rdmapool is absent, which callers treat as "stay on normal DMA", not as a
 * failure to start.
 */
NTSTATUS ViosndRdmaConnect(_Inout_ PVIOSND_RDMA Rdma, _In_ ULONG RingPages, _In_ ULONG DataPages);

VOID ViosndRdmaDisconnect(_Inout_ PVIOSND_RDMA Rdma);

/* TRUE once the pool is connected and every DMA buffer must come from it. */
__forceinline BOOLEAN ViosndRdmaActive(_In_ PVIOSND_RDMA Rdma)
{
    return Rdma->Client.Active && Rdma->BitmapBuffer != NULL;
}

/*
 * Allocate Size bytes (rounded up to pages) from the pool and report the
 * physical address the device must be given. NULL when the region is full.
 */
_Ret_maybenull_
PVOID ViosndRdmaAlloc(_Inout_ PVIOSND_RDMA Rdma,
                      _In_ SIZE_T Size,
                      _Out_ PPHYSICAL_ADDRESS LogicalAddress);

/* Return a block. Size must be the size passed to ViosndRdmaAlloc. */
VOID ViosndRdmaFree(_Inout_ PVIOSND_RDMA Rdma, _In_opt_ PVOID Va, _In_ SIZE_T Size);

#endif /* _VIOSNDRDMA_H_ */
