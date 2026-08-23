#pragma once

typedef struct _VIOSND_DEVICE VIOSND_DEVICE, *PVIOSND_DEVICE;
typedef struct _VIOSND_PCM_IO VIOSND_PCM_IO, *PVIOSND_PCM_IO;

NTSTATUS
ViosndCreateDevice(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PDEVICE_OBJECT PhysicalDeviceObject,
    _In_ PRESOURCELIST ResourceList,
    _Outptr_ PVIOSND_DEVICE *Device);

VOID
ViosndDestroyDevice(
    _In_opt_ PVIOSND_DEVICE Device);

/* Raise the render pump's in-flight target to the host's hint, if it published one and it is
 * safe (never beyond NotificationCount - 1). Returns DriverChoice unchanged otherwise. */
ULONG
ViosndApplyHostOutstandingHint(
    _In_ PVIOSND_DEVICE Device,
    _In_ ULONG DriverChoice,
    _In_ ULONG NotificationCount);

NTSTATUS
ViosndInitializeDevice(
    _Inout_ PVIOSND_DEVICE Device);

VOID
ViosndPollEvents(
    _Inout_ PVIOSND_DEVICE Device);

VOID
ViosndKickTxQueue(
    _Inout_ PVIOSND_DEVICE Device);

/* Groups the device's PCM streams into the endpoints this driver will expose. */
NTSTATUS
ViosndEnumerateEndpoints(
    _Inout_ PVIOSND_DEVICE Device,
    _Out_ PVIOSND_ENDPOINT_SET Set);

NTSTATUS
ViosndQueryPcmStreams(
    _Inout_ PVIOSND_DEVICE Device,
    _Out_ PVIOSND_STREAM_PAIR Pair);

NTSTATUS
ViosndConfigureDefaultPcm(
    _Inout_ PVIOSND_DEVICE Device,
    _In_ ULONG StreamId);

NTSTATUS
ViosndConfigureFallbackPcm(
    _Inout_ PVIOSND_DEVICE Device,
    _In_ ULONG StreamId);

NTSTATUS
ViosndStartPcm(
    _Inout_ PVIOSND_DEVICE Device,
    _In_ ULONG StreamId);

NTSTATUS
ViosndStopPcm(
    _Inout_ PVIOSND_DEVICE Device,
    _In_ ULONG StreamId);

NTSTATUS
ViosndReleasePcm(
    _Inout_ PVIOSND_DEVICE Device,
    _In_ ULONG StreamId);

NTSTATUS
ViosndWritePcm(
    _Inout_ PVIOSND_DEVICE Device,
    _In_ ULONG StreamId,
    _In_reads_bytes_(Length) const VOID *Buffer,
    _In_ ULONG Length,
    _Out_opt_ PULONG BytesWritten);

NTSTATUS
ViosndSubmitWritePcm(
    _Inout_ PVIOSND_DEVICE Device,
    _In_ ULONG StreamId,
    _In_reads_bytes_(Length) const VOID *Buffer,
    _In_ ULONG Length);

NTSTATUS
ViosndAllocateWritePcmIo(
    _Inout_ PVIOSND_DEVICE Device,
    _In_ ULONG MaxAudioLength,
    _Outptr_ PVIOSND_PCM_IO *Io);

VOID
ViosndFreeWritePcmIo(
    _Inout_ PVIOSND_DEVICE Device,
    _In_opt_ PVIOSND_PCM_IO Io);

NTSTATUS
ViosndAllocateReadPcmIo(
    _Inout_ PVIOSND_DEVICE Device,
    _In_ ULONG MaxAudioLength,
    _Outptr_ PVIOSND_PCM_IO *Io);

VOID
ViosndFreeReadPcmIo(
    _Inout_ PVIOSND_DEVICE Device,
    _In_opt_ PVIOSND_PCM_IO Io);

NTSTATUS
ViosndSubmitPreparedWritePcm(
    _Inout_ PVIOSND_DEVICE Device,
    _In_ ULONG StreamId,
    _In_reads_bytes_(Length) const VOID *Buffer,
    _In_ ULONG Length,
    _In_ ULONG SourceLength,
    _In_ ULONG PacketNumber,
    _Inout_ PVIOSND_PCM_IO Io);

NTSTATUS
ViosndReclaimWritePcm(
    _Inout_ PVIOSND_DEVICE Device,
    _Out_opt_ PULONG BytesWritten);

NTSTATUS
ViosndReclaimPreparedWritePcm(
    _Inout_ PVIOSND_DEVICE Device,
    _Outptr_opt_result_maybenull_ PVIOSND_PCM_IO *Io,
    _Out_opt_ PULONG BytesWritten,
    _Out_opt_ PULONG LatencyBytes);

NTSTATUS
ViosndDetachUnusedWritePcm(
    _Inout_ PVIOSND_DEVICE Device,
    _Outptr_opt_result_maybenull_ PVIOSND_PCM_IO *Io);

NTSTATUS
ViosndSubmitPreparedReadPcm(
    _Inout_ PVIOSND_DEVICE Device,
    _In_ ULONG StreamId,
    _In_ ULONG Length,
    _In_ ULONG PacketNumber,
    _Inout_ PVIOSND_PCM_IO Io);

NTSTATUS
ViosndReclaimPreparedReadPcm(
    _Inout_ PVIOSND_DEVICE Device,
    _Outptr_opt_result_maybenull_ PVIOSND_PCM_IO *Io,
    _Out_opt_ PULONG BytesRead,
    _Out_opt_ PULONG LatencyBytes);

ULONG
ViosndDetachUnusedReadPcm(
    _Inout_ PVIOSND_DEVICE Device);

PVOID
ViosndGetPcmIoAudioBuffer(
    _In_ PVIOSND_PCM_IO Io);

ULONG
ViosndGetPcmIoPacketNumber(
    _In_ PVIOSND_PCM_IO Io);

ULONG
ViosndGetPcmIoSourceLength(
    _In_ PVIOSND_PCM_IO Io);

NTSTATUS
ViosndReadPcm(
    _Inout_ PVIOSND_DEVICE Device,
    _In_ ULONG StreamId,
    _Out_writes_bytes_(Length) VOID *Buffer,
    _In_ ULONG Length,
    _Out_opt_ PULONG BytesRead);
