#pragma once

#define VIOSND_POOL_TAG 'dnSV'
#define VIOSND_MAX_SUBDEVICES 4

extern "C" DRIVER_ADD_DEVICE XcbVirtioAudioAddDevice;

extern "C" NTSTATUS
XcbVirtioAudioStartDevice(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_ PRESOURCELIST ResourceList);

/* Records a line of driver state under the device's registry key; see the definition. */
VOID
ViosndWriteDeviceDiagString(
    _In_ PDEVICE_OBJECT PhysicalDeviceObject,
    _In_z_ PCWSTR ValueName,
    _In_z_ PCWSTR Value);
