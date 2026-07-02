/*
 * vioscsi restricted-DMA-pool (rdmapool) support: virtio-scsi-specific staging
 * on top of the shared rdmapool client library (rdmapool/rdmaclient.c).
 * See vioscsi_rdma.h for the design overview.
 *
 * Copyright (c) 2026
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include "helper.h" /* brings storport, osdep, vioscsi.h, srbhelper, SRB_* macros */

/* ------------------------------------------------------------------ */
/* rdmapool connect / disconnect                                      */
/* ------------------------------------------------------------------ */

NTSTATUS VioScsiConnectRdmaPool(PVOID DeviceExtension)
{
    PADAPTER_EXTENSION adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
    NTSTATUS status;

    if (adaptExt->dump_mode)
    {
        adaptExt->rdma.Active = FALSE;
        return STATUS_NOT_SUPPORTED;
    }

    /* Meta = one page of event nodes + one control slot per outstanding
     * request (queue_depth, +2 headroom for TMF on the control queue). */
    status = RdmaClientConnect(&adaptExt->rdma,
                               "vioscsi",
                               adaptExt->pageAllocationSize / PAGE_SIZE,
                               1 + (adaptExt->queue_depth + 2) * BOUNCE_CTL_PAGES);
    if (!NT_SUCCESS(status))
    {
        return status;
    }

    /* Redirect the page bump allocator (used by virtio_find_queues) at the pool. */
    adaptExt->pageAllocationVa = adaptExt->rdma.BaseVA;
    adaptExt->pageAllocationSize = (ULONG)adaptExt->rdma.Size;
    adaptExt->pageOffset = 0;
    return STATUS_SUCCESS;
}

VOID VioScsiDisconnectRdmaPool(PVOID DeviceExtension)
{
    PADAPTER_EXTENSION adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
    RdmaClientDisconnect(&adaptExt->rdma);
}

/* ------------------------------------------------------------------ */
/* VA <-> PA                                                          */
/* ------------------------------------------------------------------ */

PHYSICAL_ADDRESS VioScsiRdmaVAtoPA(PVOID DeviceExtension, PVOID va)
{
    PADAPTER_EXTENSION adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
    return RdmaClientVAtoPA(&adaptExt->rdma, va);
}

PVOID VioScsiRdmaPAtoVA(PVOID DeviceExtension, PHYSICAL_ADDRESS pa)
{
    PADAPTER_EXTENSION adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
    return RdmaClientPAtoVA(&adaptExt->rdma, pa);
}

/* ------------------------------------------------------------------ */
/* Bounce init / build / complete (virtio-scsi layout)                */
/* ------------------------------------------------------------------ */

NTSTATUS VioScsiBounceInit(PVOID DeviceExtension)
{
    PADAPTER_EXTENSION adaptExt = (PADAPTER_EXTENSION)DeviceExtension;

    if (!adaptExt->rdma.Active)
    {
        return STATUS_NOT_SUPPORTED;
    }

    return RdmaClientBounceInit(&adaptExt->rdma,
                                (PUCHAR)adaptExt->pageAllocationVa + adaptExt->pageOffset,
                                (adaptExt->queue_depth ? adaptExt->queue_depth : 64) + 2,
                                BOUNCE_CTL_SIZE,
                                BOUNCE_EVENT_COUNT * sizeof(VirtIOSCSIEventNode),
                                BOUNCE_DATA_CHUNK_SIZE);
}

PVOID VioScsiBounceEventNodes(PVOID DeviceExtension)
{
    PADAPTER_EXTENSION adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
    return adaptExt->rdma.EventBaseVA;
}

PVOID VioScsiBounceAllocCtl(PVOID DeviceExtension, PVOID srbExtArg)
{
    PADAPTER_EXTENSION adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
    PSRB_EXTENSION srbExt = (PSRB_EXTENSION)srbExtArg;
    PVOID ctl;

    if (!adaptExt->rdma.Active || !adaptExt->rdma.BounceInitialized)
    {
        return NULL;
    }
    ctl = RdmaClientAllocCtl(&adaptExt->rdma);
    if (ctl == NULL)
    {
        return NULL;
    }
    srbExt->bounceCtl = ctl;
    srbExt->bounceChunkFirst = 0;
    srbExt->bounceChunkCount = 0;
    srbExt->bounceDataIn = FALSE;
    RtlCopyMemory((PUCHAR)ctl + BOUNCE_CTL_REQ_OFFSET, &srbExt->cmd.req, sizeof(srbExt->cmd.req));
    /* Zero the resp area so stale bytes from a previous request can't leak into
     * cmd.resp when the device writes less than the full union (e.g. TMF). */
    RtlZeroMemory((PUCHAR)ctl + BOUNCE_CTL_RESP_OFFSET, sizeof(srbExt->cmd.resp));
    return ctl;
}

BOOLEAN VioScsiBounceBuild(PVOID DeviceExtension, PVOID SrbArg)
{
    PADAPTER_EXTENSION adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
    PSRB_TYPE Srb = (PSRB_TYPE)SrbArg;
    PSRB_EXTENSION srbExt = SRB_EXTENSION(Srb);
    PRDMA_CLIENT c = &adaptExt->rdma;
    PVOID ctl;
    PVOID dataVA = NULL;
    ULONG dataLen = SRB_DATA_TRANSFER_LENGTH(Srb);
    BOOLEAN dataOut = ((SRB_FLAGS(Srb) & SRB_FLAGS_DATA_OUT) == SRB_FLAGS_DATA_OUT);
    ULONG nChunks, sgIdx, i, off;

    ctl = VioScsiBounceAllocCtl(DeviceExtension, srbExt);
    if (ctl == NULL)
    {
        DbgPrint(" bounce: no ctl slot\n");
        return FALSE;
    }

    if (dataLen)
    {
        if (StorPortGetSystemAddress(DeviceExtension, (PSCSI_REQUEST_BLOCK)Srb, &dataVA) != STOR_STATUS_SUCCESS ||
            dataVA == NULL)
        {
            DbgPrint(" bounce: StorPortGetSystemAddress failed\n");
            RdmaClientFreeCtl(c, ctl);
            srbExt->bounceCtl = NULL;
            return FALSE;
        }
    }
    srbExt->srbDataVA = (PUCHAR)dataVA;
    srbExt->srbDataLen = dataLen;

    nChunks = dataLen ? ((dataLen + c->DataChunkSize - 1) / c->DataChunkSize) : 0;

    /* virtio-scsi layout: out = req [+ data-out chunks]; in = resp [+ data-in chunks]. */
    sgIdx = 0;
    srbExt->psgl[sgIdx].physAddr = VioScsiRdmaVAtoPA(DeviceExtension, (PUCHAR)ctl + BOUNCE_CTL_REQ_OFFSET);
    srbExt->psgl[sgIdx].length = sizeof(srbExt->cmd.req.cmd);
    sgIdx++;

    if (nChunks && dataOut)
    {
        srbExt->bounceChunkFirst = sgIdx;
        off = 0;
        for (i = 0; i < nChunks; i++, sgIdx++)
        {
            PVOID chunk = RdmaClientAllocChunk(c);
            ULONG clen;
            if (chunk == NULL)
            {
                goto no_chunk;
            }
            clen = min(c->DataChunkSize, dataLen - off);
            RtlCopyMemory(chunk, (PUCHAR)dataVA + off, clen);
            srbExt->psgl[sgIdx].physAddr = VioScsiRdmaVAtoPA(DeviceExtension, chunk);
            srbExt->psgl[sgIdx].length = clen;
            srbExt->Xfer += clen;
            srbExt->bounceChunkCount++;
            off += clen;
        }
    }
    srbExt->out = sgIdx;

    srbExt->psgl[sgIdx].physAddr = VioScsiRdmaVAtoPA(DeviceExtension, (PUCHAR)ctl + BOUNCE_CTL_RESP_OFFSET);
    srbExt->psgl[sgIdx].length = sizeof(srbExt->cmd.resp.cmd);
    sgIdx++;

    if (nChunks && !dataOut)
    {
        srbExt->bounceChunkFirst = sgIdx;
        srbExt->bounceDataIn = TRUE;
        off = 0;
        for (i = 0; i < nChunks; i++, sgIdx++)
        {
            PVOID chunk = RdmaClientAllocChunk(c);
            ULONG clen;
            if (chunk == NULL)
            {
                goto no_chunk;
            }
            clen = min(c->DataChunkSize, dataLen - off);
            srbExt->psgl[sgIdx].physAddr = VioScsiRdmaVAtoPA(DeviceExtension, chunk);
            srbExt->psgl[sgIdx].length = clen;
            srbExt->Xfer += clen;
            srbExt->bounceChunkCount++;
            off += clen;
        }
    }
    srbExt->in = sgIdx - srbExt->out;
    return TRUE;

no_chunk:
    /* Roll back chunks already taken, then the ctl slot. */
    for (i = 0; i < srbExt->bounceChunkCount; i++)
    {
        RdmaClientFreeChunk(c, VioScsiRdmaPAtoVA(DeviceExtension, srbExt->psgl[srbExt->bounceChunkFirst + i].physAddr));
    }
    RdmaClientFreeCtl(c, ctl);
    srbExt->bounceCtl = NULL;
    srbExt->bounceChunkFirst = 0;
    srbExt->bounceChunkCount = 0;
    srbExt->bounceDataIn = FALSE;
    srbExt->Xfer = 0;
    DbgPrint(" bounce: no data chunk\n");
    return FALSE;
}

VOID VioScsiBounceComplete(PVOID DeviceExtension, PVOID srbExtArg)
{
    PADAPTER_EXTENSION adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
    PSRB_EXTENSION srbExt = (PSRB_EXTENSION)srbExtArg;
    PRDMA_CLIENT c = &adaptExt->rdma;
    PVOID ctl;
    ULONG i, off;

    if (!adaptExt->rdma.Active || srbExt == NULL || srbExt->bounceCtl == NULL)
    {
        return; /* not a bounced request */
    }
    ctl = srbExt->bounceCtl;

    /* Latch the device-written resp (status / sense / TMF response) before any
     * completion logic (HandleResponse, TMF drains) reads cmd.resp. */
    RtlCopyMemory(&srbExt->cmd.resp, (PUCHAR)ctl + BOUNCE_CTL_RESP_OFFSET, sizeof(srbExt->cmd.resp));

    /* Reads: copy data back from the bounce chunks into the original SRB buffer. */
    if (srbExt->bounceDataIn && srbExt->srbDataVA && srbExt->bounceChunkCount)
    {
        off = 0;
        for (i = 0; i < srbExt->bounceChunkCount; i++)
        {
            PVOID chunk = VioScsiRdmaPAtoVA(DeviceExtension, srbExt->psgl[srbExt->bounceChunkFirst + i].physAddr);
            ULONG clen = srbExt->psgl[srbExt->bounceChunkFirst + i].length;
            RtlCopyMemory(srbExt->srbDataVA + off, chunk, clen);
            off += clen;
        }
    }

    /* Free chunks then the control slot. */
    for (i = 0; i < srbExt->bounceChunkCount; i++)
    {
        RdmaClientFreeChunk(c, VioScsiRdmaPAtoVA(DeviceExtension, srbExt->psgl[srbExt->bounceChunkFirst + i].physAddr));
    }
    RdmaClientFreeCtl(c, ctl);
    srbExt->bounceCtl = NULL;
    srbExt->bounceChunkFirst = 0;
    srbExt->bounceChunkCount = 0;
    srbExt->bounceDataIn = FALSE;
}

/* ------------------------------------------------------------------ */
/* Completion poll thread callbacks                                   */
/* ------------------------------------------------------------------ */

static BOOLEAN VioScsiPollBusy(PVOID Context)
{
    PADAPTER_EXTENSION adaptExt = (PADAPTER_EXTENSION)Context;
    ULONG q;
    if (adaptExt->tmf_infly)
    {
        return TRUE;
    }
    for (q = 0; q < adaptExt->num_queues; q++)
    {
        if (adaptExt->processing_srbs[q].srb_cnt != 0)
        {
            return TRUE;
        }
    }
    return FALSE;
}

static VOID VioScsiPollDrain(PVOID Context)
{
    VioScsiPollDrainAll(Context);
}

NTSTATUS VioScsiStartPollThread(PVOID DeviceExtension)
{
    PADAPTER_EXTENSION adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
    return RdmaClientStartPoll(&adaptExt->rdma, VioScsiPollBusy, VioScsiPollDrain, adaptExt, adaptExt->pollIntervalUs);
}

VOID VioScsiStopPollThread(PVOID DeviceExtension)
{
    PADAPTER_EXTENSION adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
    RdmaClientStopPoll(&adaptExt->rdma);
}

VOID VioScsiPollKick(PVOID DeviceExtension)
{
    PADAPTER_EXTENSION adaptExt = (PADAPTER_EXTENSION)DeviceExtension;
    RdmaClientPollKick(&adaptExt->rdma);
}
