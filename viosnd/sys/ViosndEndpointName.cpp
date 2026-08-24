// SPDX-License-Identifier: BSD-3-Clause
#include "precomp.h"

/*
 * One GUID per endpoint slot, fixed at build time. See the header for why they are not derived
 * from the host device.
 */
static const GUID ViosndRenderNameGuids[VIOSND_MAX_ENDPOINTS] = {
    { 0x2CA106AC, 0xDA02, 0x417F, { 0x9C, 0x15, 0x69, 0xD7, 0xBE, 0x74, 0x71, 0x1A } },
    { 0x2134965D, 0x0F65, 0x42A0, { 0xB6, 0xA4, 0xA1, 0xA0, 0x75, 0x57, 0x92, 0x6C } },
    { 0xEECE6A09, 0x6B19, 0x4D0C, { 0xB7, 0x11, 0xC5, 0x1A, 0x89, 0xE5, 0x78, 0xEE } },
    { 0xDCABC571, 0xDE00, 0x4852, { 0x92, 0x0F, 0x1B, 0xD1, 0xE5, 0xE0, 0x6D, 0xC7 } }
};

static const GUID ViosndCaptureNameGuids[VIOSND_MAX_ENDPOINTS] = {
    { 0x09DEDC0F, 0x6890, 0x4829, { 0x97, 0xD8, 0xBC, 0xA1, 0xB3, 0x07, 0xA0, 0x07 } },
    { 0x1EFE9091, 0xB26A, 0x4B99, { 0x8A, 0x2B, 0x9C, 0xF7, 0x83, 0x76, 0x52, 0x42 } },
    { 0xA38F4632, 0x6DEE, 0x423C, { 0x89, 0x73, 0xCC, 0x6A, 0x09, 0xB7, 0xFA, 0xD2 } },
    { 0x079C8899, 0x23FB, 0x49D4, { 0x8C, 0x1E, 0xB8, 0xCD, 0x92, 0x0E, 0xAC, 0x8A } }
};

const GUID *
ViosndEndpointNameGuid(
    _In_ BOOLEAN Capture,
    _In_ ULONG Index)
{
    if (Index >= VIOSND_MAX_ENDPOINTS) {
        return NULL;
    }
    return Capture ? &ViosndCaptureNameGuids[Index] : &ViosndRenderNameGuids[Index];
}

/*
 * What to call an endpoint of this kind. These read the way Windows' own endpoint names do --
 * the device half of the composed name already says whose speaker it is, so this half only has
 * to say which one.
 */
static PCWSTR
ViosndEndpointKindName(
    _In_ BOOLEAN Capture,
    _In_ ULONG Kind)
{
    switch (Kind) {
    case VIOSND_ENDPOINT_KIND_SPEAKER:
        return L"Speakers";
    case VIOSND_ENDPOINT_KIND_HEADPHONES:
        return L"Headphones";
    case VIOSND_ENDPOINT_KIND_HEADSET:
        return Capture ? L"Headset Microphone" : L"Headset Earphone";
    case VIOSND_ENDPOINT_KIND_LINE_OUT:
        return Capture ? L"Line In" : L"Line Out";
    case VIOSND_ENDPOINT_KIND_DIGITAL:
        return Capture ? L"Digital In" : L"Digital Out";
    case VIOSND_ENDPOINT_KIND_MICROPHONE:
        return L"Microphone";
    case VIOSND_ENDPOINT_KIND_TELEPHONY:
        return L"Telephony";
    default:
        /* The host said nothing. Fall back to what the node type would have produced, so an
         * unknown kind reads exactly as it did before endpoints were named at all. */
        return Capture ? L"Microphone" : L"Speakers";
    }
}

NTSTATUS
ViosndPublishEndpointName(
    _In_ BOOLEAN Capture,
    _In_ ULONG Index,
    _In_ ULONG Kind)
{
    const GUID *guid = ViosndEndpointNameGuid(Capture, Index);
    WCHAR path[128];
    WCHAR name[64];
    UNICODE_STRING pathString;
    UNICODE_STRING valueName;
    OBJECT_ATTRIBUTES attributes;
    HANDLE key = NULL;
    NTSTATUS status;

    if (guid == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    status = RtlStringCchPrintfW(
        path,
        SIZEOF_ARRAY(path),
        L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Control\\MediaCategories\\"
        L"{%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
        guid->Data1,
        guid->Data2,
        guid->Data3,
        guid->Data4[0], guid->Data4[1], guid->Data4[2], guid->Data4[3],
        guid->Data4[4], guid->Data4[5], guid->Data4[6], guid->Data4[7]);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    /* The first of a kind keeps the bare name; the rest are numbered from two, matching how the
     * subdevices themselves are named in the INF. */
    if (Index == 0) {
        status = RtlStringCchCopyW(name, SIZEOF_ARRAY(name), ViosndEndpointKindName(Capture, Kind));
    } else {
        status = RtlStringCchPrintfW(name,
                                     SIZEOF_ARRAY(name),
                                     L"%s %u",
                                     ViosndEndpointKindName(Capture, Kind),
                                     Index + 1);
    }
    if (!NT_SUCCESS(status)) {
        return status;
    }

    RtlInitUnicodeString(&pathString, path);
    InitializeObjectAttributes(&attributes,
                               &pathString,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               NULL,
                               NULL);
    status = ZwCreateKey(&key, KEY_SET_VALUE, &attributes, 0, NULL, REG_OPTION_NON_VOLATILE, NULL);
    if (!NT_SUCCESS(status)) {
        VIOSND_LOG(DPFLTR_IHVDRIVER_ID,
                   DPFLTR_ERROR_LEVEL,
                   "viosnd: MediaCategories create failed 0x%08x for %ws\n",
                   status,
                   path);
        return status;
    }

    RtlInitUnicodeString(&valueName, L"Name");
    status = ZwSetValueKey(key,
                           &valueName,
                           0,
                           REG_SZ,
                           name,
                           (ULONG)((wcslen(name) + 1) * sizeof(WCHAR)));
    ZwClose(key);

    if (!NT_SUCCESS(status)) {
        VIOSND_LOG(DPFLTR_IHVDRIVER_ID,
                   DPFLTR_ERROR_LEVEL,
                   "viosnd: MediaCategories write failed 0x%08x for %ws\n",
                   status,
                   path);
    }
    return status;
}
