/*
 * vioscsi restricted-DMA-pool (rdmapool) support: virtio-scsi-specific staging
 * on top of the shared rdmapool client library (rdmapool/rdmaclient.c), which
 * provides the pool connection, the SLIST bounce allocator (control slots +
 * large contiguous data chunks + event area) and the completion poll thread.
 *
 * This layer owns what is virtio-scsi-shaped: the control-slot layout (the
 * VirtIOSCSICmd req/resp unions, incl. the sense buffer, also used for TMF
 * requests on the control queue), how an EXECUTE_SCSI SRB is staged through
 * data chunks (out = req + data-out, in = resp + data-in), the event-node
 * area the device writes hotplug events into, and which queues the poll
 * thread drains.
 *
 * Copyright (c) 2026
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef _VIOSCSI_RDMA_H_
#define _VIOSCSI_RDMA_H_

#include "rdmaclient.h"

/*
 * Control slot layout (one page). Holds the device-visible request metadata
 * that must live in rdmapool: the VirtIOSCSICmd req union (cmd/tmf/an) at
 * offset 0 and the resp union (incl. the 96-byte sense buffer) at offset 64.
 * Indirect descriptors are not used on the rdmapool path, so no indirect
 * table is needed here.
 */
#define BOUNCE_CTL_PAGES         1
#define BOUNCE_CTL_SIZE          (BOUNCE_CTL_PAGES * PAGE_SIZE)
#define BOUNCE_CTL_REQ_OFFSET    0  /* req union: VirtIOSCSICmdReq (51B) is the largest member */
#define BOUNCE_CTL_RESP_OFFSET   64 /* resp union: VirtIOSCSICmdResp (108B) is the largest member */

/* VirtIOSCSIEventNode slots (device writes VirtIOSCSIEvent into them). */
#define BOUNCE_EVENT_COUNT       8

/*
 * Default contiguous data chunk size (== MaximumTransferLength on this path).
 * BounceInit may shrink the chunk to keep one chunk per control slot.
 */
#define BOUNCE_DATA_CHUNK_SIZE   (256 * 1024)

/* Default gentle poll interval; registry PollIntervalUs overrides (0 = tight spin). */
#define VIOSCSI_POLL_INTERVAL_US RDMA_CLIENT_POLL_INTERVAL_US

/*
 * Connect to the rdmapool driver and allocate one contiguous region big enough
 * for the vrings (adaptExt->pageAllocationSize, already computed for the
 * control + event + request queues) plus a bounce area. On success sets
 * adaptExt->rdma.Active and redirects the page bump allocator
 * (pageAllocationVa/Size/Offset) at the pool. PASSIVE_LEVEL only. Returns
 * STATUS_NOT_FOUND when rdmapool is absent — i.e. whenever the ACPI\RDMA0000
 * device does not exist (QEMU/KVM) — and the caller keeps the normal DMA path.
 * Call from VioScsiFindAdapter.
 */
NTSTATUS VioScsiConnectRdmaPool(PVOID DeviceExtension);
VOID VioScsiDisconnectRdmaPool(PVOID DeviceExtension);

/*
 * Carve the event area, control slots and data chunks out of the rdmapool
 * region left after the vrings (i.e. starting at pageAllocationVa + pageOffset).
 * Call right after virtio_find_queues and BEFORE the first KickEvent, i.e.
 * from VioScsiHwInitialize. Idempotent across adapter re-initialization.
 */
NTSTATUS VioScsiBounceInit(PVOID DeviceExtension);

/* The VirtIOSCSIEventNode[BOUNCE_EVENT_COUNT] array inside the pool (device-
 * writable), valid after VioScsiBounceInit. */
PVOID VioScsiBounceEventNodes(PVOID DeviceExtension);

/* VA<->PA within the contiguous rdmapool region. */
PHYSICAL_ADDRESS VioScsiRdmaVAtoPA(PVOID DeviceExtension, PVOID va);
PVOID VioScsiRdmaPAtoVA(PVOID DeviceExtension, PHYSICAL_ADDRESS pa);

/*
 * Stage an EXECUTE_SCSI SRB through bounce buffers. Fills srbExt->psgl/out/in
 * with rdmapool physical addresses (req + [data-out chunks] + resp +
 * [data-in chunks]), copies write data in, and records what completion needs
 * to copy back/free. Returns FALSE if the pool is momentarily exhausted (the
 * caller completes the SRB with SRB_STATUS_BUSY so the class driver retries).
 * Called from VioScsiBuildIo when rdma.Active, after the cmd header is set.
 */
BOOLEAN VioScsiBounceBuild(PVOID DeviceExtension, PVOID Srb);

/*
 * Allocate a control slot, copy srbExt->cmd.req into its req area, zero its
 * resp area, and stash it in srbExt->bounceCtl. Used directly by DeviceReset
 * (TMF on the control queue) which carries no data payload. Returns the slot
 * VA or NULL.
 */
PVOID VioScsiBounceAllocCtl(PVOID DeviceExtension, PVOID srbExt);

/*
 * Finish a bounced request: copy the resp union (status/sense/TMF response)
 * back into srbExt->cmd.resp, copy data-in chunks into the original SRB
 * buffer, and free the bounce resources. No-op when srbExt->bounceCtl is
 * NULL, so call sites may invoke it unconditionally. Call BEFORE any code
 * reads cmd.resp (HandleResponse / the TMF drain loops).
 */
VOID VioScsiBounceComplete(PVOID DeviceExtension, PVOID srbExt);

/* Poll thread lifecycle (PASSIVE_LEVEL). */
NTSTATUS VioScsiStartPollThread(PVOID DeviceExtension);
VOID VioScsiStopPollThread(PVOID DeviceExtension);
/* Wake the poll thread after submitting work (any IRQL). */
VOID VioScsiPollKick(PVOID DeviceExtension);

/*
 * Drain all virtqueues once (implemented in vioscsi.c next to the ISR logic):
 * request queues via ProcessQueue, control/event queues under the interrupt
 * lock (INTx mode only). Called by the poll thread at PASSIVE_LEVEL.
 */
VOID VioScsiPollDrainAll(PVOID DeviceExtension);

#endif /* _VIOSCSI_RDMA_H_ */
