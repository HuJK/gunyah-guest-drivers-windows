#include "precomp.h"

enum {
    VIOSND_TOPO_NODE_NONE = 0
};

class CViosndMiniportTopology;

static NTSTATUS
ViosndTopologyPropertyHandler(_In_ PPCPROPERTY_REQUEST PropertyRequest);

static KSJACK_DESCRIPTION ViosndSpeakerJackDescription = {
    KSAUDIO_SPEAKER_STEREO,
    0xB3C98C,
    eConnTypeUnknown,
    eGeoLocNotApplicable,
    eGenLocPrimaryBox,
    ePortConnIntegratedDevice,
    TRUE
};

static KSJACK_DESCRIPTION ViosndMicJackDescription = {
    KSAUDIO_SPEAKER_MONO,
    0x993399,
    eConnTypeUnknown,
    eGeoLocNotApplicable,
    eGenLocPrimaryBox,
    ePortConnIntegratedDevice,
    TRUE
};

static PKSJACK_DESCRIPTION ViosndRenderJackDescriptions[] = {
    NULL,
    &ViosndSpeakerJackDescription
};

static PKSJACK_DESCRIPTION ViosndCaptureJackDescriptions[] = {
    &ViosndMicJackDescription,
    NULL
};

static PCPROPERTY_ITEM ViosndTopologyProperties[] = {
    {
        &KSPROPSETID_Jack,
        KSPROPERTY_JACK_DESCRIPTION,
        KSPROPERTY_TYPE_GET | KSPROPERTY_TYPE_BASICSUPPORT,
        ViosndTopologyPropertyHandler
    },
    {
        &KSPROPSETID_Jack,
        KSPROPERTY_JACK_DESCRIPTION2,
        KSPROPERTY_TYPE_GET | KSPROPERTY_TYPE_BASICSUPPORT,
        ViosndTopologyPropertyHandler
    }
};

DEFINE_PCAUTOMATION_TABLE_PROP(ViosndTopologyAutomation, ViosndTopologyProperties);

static KSDATARANGE ViosndTopoPinDataRangesBridge[] = {
    {
        sizeof(KSDATARANGE),
        0,
        0,
        0,
        STATICGUIDOF(KSDATAFORMAT_TYPE_AUDIO),
        STATICGUIDOF(KSDATAFORMAT_SUBTYPE_ANALOG),
        STATICGUIDOF(KSDATAFORMAT_SPECIFIER_NONE)
    }
};

static PKSDATARANGE ViosndTopoPinDataRangePointersBridge[] = {
    &ViosndTopoPinDataRangesBridge[0]
};

static PCPIN_DESCRIPTOR ViosndRenderTopoPins[] = {
    {
        0,
        0,
        0,
        NULL,
        {
            0,
            NULL,
            0,
            NULL,
            SIZEOF_ARRAY(ViosndTopoPinDataRangePointersBridge),
            ViosndTopoPinDataRangePointersBridge,
            KSPIN_DATAFLOW_IN,
            KSPIN_COMMUNICATION_NONE,
            &KSCATEGORY_AUDIO,
            NULL,
            0
        }
    },
    {
        0,
        0,
        0,
        NULL,
        {
            0,
            NULL,
            0,
            NULL,
            SIZEOF_ARRAY(ViosndTopoPinDataRangePointersBridge),
            ViosndTopoPinDataRangePointersBridge,
            KSPIN_DATAFLOW_OUT,
            KSPIN_COMMUNICATION_NONE,
            &KSNODETYPE_SPEAKER,
            NULL,
            0
        }
    }
};

static PCCONNECTION_DESCRIPTOR ViosndRenderTopoConnections[] = {
    { PCFILTER_NODE, VIOSND_TOPO_PIN_SOURCE, PCFILTER_NODE, VIOSND_TOPO_PIN_BRIDGE }
};

static PCFILTER_DESCRIPTOR ViosndRenderTopoFilterDescriptor = {
    0,
    &ViosndTopologyAutomation,
    sizeof(PCPIN_DESCRIPTOR),
    SIZEOF_ARRAY(ViosndRenderTopoPins),
    ViosndRenderTopoPins,
    sizeof(PCNODE_DESCRIPTOR),
    0,
    NULL,
    SIZEOF_ARRAY(ViosndRenderTopoConnections),
    ViosndRenderTopoConnections,
    0,
    NULL
};

static PCPIN_DESCRIPTOR ViosndCaptureTopoPins[] = {
    {
        0,
        0,
        0,
        NULL,
        {
            0,
            NULL,
            0,
            NULL,
            SIZEOF_ARRAY(ViosndTopoPinDataRangePointersBridge),
            ViosndTopoPinDataRangePointersBridge,
            KSPIN_DATAFLOW_IN,
            KSPIN_COMMUNICATION_NONE,
            &KSNODETYPE_MICROPHONE,
            NULL,
            0
        }
    },
    {
        0,
        0,
        0,
        NULL,
        {
            0,
            NULL,
            0,
            NULL,
            SIZEOF_ARRAY(ViosndTopoPinDataRangePointersBridge),
            ViosndTopoPinDataRangePointersBridge,
            KSPIN_DATAFLOW_OUT,
            KSPIN_COMMUNICATION_NONE,
            &KSCATEGORY_AUDIO,
            NULL,
            0
        }
    }
};

static PCCONNECTION_DESCRIPTOR ViosndCaptureTopoConnections[] = {
    { PCFILTER_NODE, VIOSND_TOPO_PIN_SOURCE, PCFILTER_NODE, VIOSND_TOPO_PIN_BRIDGE }
};

static PCFILTER_DESCRIPTOR ViosndCaptureTopoFilterDescriptor = {
    0,
    &ViosndTopologyAutomation,
    sizeof(PCPIN_DESCRIPTOR),
    SIZEOF_ARRAY(ViosndCaptureTopoPins),
    ViosndCaptureTopoPins,
    sizeof(PCNODE_DESCRIPTOR),
    0,
    NULL,
    SIZEOF_ARRAY(ViosndCaptureTopoConnections),
    ViosndCaptureTopoConnections,
    0,
    NULL
};

class CViosndMiniportTopology : public IMiniportTopology
{
public:
    CViosndMiniportTopology(_In_ BOOLEAN Capture, _In_ ULONG Index);

    IMP_IMiniportTopology;

    STDMETHODIMP_(NTSTATUS) QueryInterface(_In_ REFIID InterfaceId, _COM_Outptr_ PVOID *Interface);
    STDMETHODIMP_(ULONG) AddRef();
    STDMETHODIMP_(ULONG) Release();

    BOOLEAN IsCapture() const { return m_Capture; }

private:
    LONG m_RefCount;
    BOOLEAN m_Capture;
    ULONG m_Index;
    PPORTTOPOLOGY m_Port;
    /* A per-instance copy of the static description. The bridge pin's name is the one thing that
     * differs between two endpoints of the same direction, and it is what Windows shows. */
    PCPIN_DESCRIPTOR m_Pins[2];
    PCFILTER_DESCRIPTOR m_Filter;
};

CViosndMiniportTopology::CViosndMiniportTopology(_In_ BOOLEAN Capture, _In_ ULONG Index) :
    m_RefCount(1),
    m_Capture(Capture),
    m_Index(Index),
    m_Port(NULL)
{
    const PCFILTER_DESCRIPTOR *tmpl = Capture ? &ViosndCaptureTopoFilterDescriptor
                                              : &ViosndRenderTopoFilterDescriptor;

    RtlCopyMemory(m_Pins, tmpl->Pins, sizeof(m_Pins));
    m_Filter = *tmpl;
    m_Filter.Pins = m_Pins;
    m_Filter.PinCount = SIZEOF_ARRAY(m_Pins);

    /* NULL leaves the pin named after its node type, which is what every endpoint on the card
     * was called before. A slot past the pool gets that same fallback rather than a wrong name. */
    m_Pins[VIOSND_TOPO_PIN_BRIDGE].KsPinDescriptor.Name =
        ViosndEndpointNameGuid(Capture, Index);
}

STDMETHODIMP_(NTSTATUS)
CViosndMiniportTopology::QueryInterface(_In_ REFIID InterfaceId, _COM_Outptr_ PVOID *Interface)
{
    if (Interface == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    *Interface = NULL;
    if (IsEqualGUIDAligned(InterfaceId, IID_IUnknown) ||
        IsEqualGUIDAligned(InterfaceId, IID_IMiniport) ||
        IsEqualGUIDAligned(InterfaceId, IID_IMiniportTopology)) {
        *Interface = (IMiniportTopology *)this;
    }

    if (*Interface == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    AddRef();
    return STATUS_SUCCESS;
}

STDMETHODIMP_(ULONG)
CViosndMiniportTopology::AddRef()
{
    return (ULONG)InterlockedIncrement(&m_RefCount);
}

STDMETHODIMP_(ULONG)
CViosndMiniportTopology::Release()
{
    LONG count = InterlockedDecrement(&m_RefCount);
    if (count == 0) {
        this->~CViosndMiniportTopology();
        ExFreePoolWithTag(this, VIOSND_POOL_TAG);
    }
    return (ULONG)count;
}

STDMETHODIMP_(NTSTATUS)
CViosndMiniportTopology::GetDescription(_Out_ PPCFILTER_DESCRIPTOR *Description)
{
    *Description = &m_Filter;
    return STATUS_SUCCESS;
}

STDMETHODIMP_(NTSTATUS)
CViosndMiniportTopology::DataRangeIntersection(
    _In_ ULONG PinId,
    _In_ PKSDATARANGE DataRange,
    _In_ PKSDATARANGE MatchingDataRange,
    _In_ ULONG OutputBufferLength,
    _Out_writes_bytes_to_opt_(OutputBufferLength, *ResultantFormatLength) PVOID ResultantFormat,
    _Out_ PULONG ResultantFormatLength)
{
    UNREFERENCED_PARAMETER(PinId);
    UNREFERENCED_PARAMETER(DataRange);
    UNREFERENCED_PARAMETER(MatchingDataRange);
    UNREFERENCED_PARAMETER(OutputBufferLength);
    UNREFERENCED_PARAMETER(ResultantFormat);

    *ResultantFormatLength = 0;
    return STATUS_NOT_IMPLEMENTED;
}

STDMETHODIMP_(NTSTATUS)
CViosndMiniportTopology::Init(
    _In_ PUNKNOWN UnknownAdapter,
    _In_ PRESOURCELIST ResourceList,
    _In_ PPORTTOPOLOGY Port)
{
    UNREFERENCED_PARAMETER(UnknownAdapter);
    UNREFERENCED_PARAMETER(ResourceList);

    m_Port = Port;
    return STATUS_SUCCESS;
}

static NTSTATUS
ViosndBasicSupport(
    _In_ PPCPROPERTY_REQUEST PropertyRequest,
    _In_ ULONG AccessFlags)
{
    if (PropertyRequest->ValueSize == 0) {
        PropertyRequest->ValueSize = sizeof(ULONG);
        return STATUS_BUFFER_OVERFLOW;
    }

    if (PropertyRequest->ValueSize < sizeof(ULONG)) {
        return STATUS_BUFFER_TOO_SMALL;
    }

    *(PULONG)PropertyRequest->Value = AccessFlags;
    PropertyRequest->ValueSize = sizeof(ULONG);
    return STATUS_SUCCESS;
}

static NTSTATUS
ViosndGetJackDescription(
    _In_ PPCPROPERTY_REQUEST PropertyRequest,
    _In_reads_(JackCount) PKSJACK_DESCRIPTION *Jacks,
    _In_ ULONG JackCount)
{
    ULONG pinId;
    ULONG needed = sizeof(KSMULTIPLE_ITEM) + sizeof(KSJACK_DESCRIPTION);
    PKSMULTIPLE_ITEM multipleItem;

    if (PropertyRequest->InstanceSize < sizeof(ULONG) ||
        PropertyRequest->Instance == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    pinId = *(PULONG)PropertyRequest->Instance;
    if (pinId >= JackCount || Jacks[pinId] == NULL) {
        return STATUS_INVALID_DEVICE_REQUEST;
    }

    if (PropertyRequest->ValueSize == 0) {
        PropertyRequest->ValueSize = needed;
        return STATUS_BUFFER_OVERFLOW;
    }
    if (PropertyRequest->ValueSize < needed) {
        return STATUS_BUFFER_TOO_SMALL;
    }

    multipleItem = (PKSMULTIPLE_ITEM)PropertyRequest->Value;
    multipleItem->Size = needed;
    multipleItem->Count = 1;
    RtlCopyMemory(multipleItem + 1, Jacks[pinId], sizeof(KSJACK_DESCRIPTION));
    PropertyRequest->ValueSize = needed;
    return STATUS_SUCCESS;
}

static NTSTATUS
ViosndGetJackDescription2(
    _In_ PPCPROPERTY_REQUEST PropertyRequest,
    _In_reads_(JackCount) PKSJACK_DESCRIPTION *Jacks,
    _In_ ULONG JackCount)
{
    ULONG pinId;
    ULONG needed = sizeof(KSMULTIPLE_ITEM) + sizeof(KSJACK_DESCRIPTION2);
    PKSMULTIPLE_ITEM multipleItem;
    PKSJACK_DESCRIPTION2 description;

    if (PropertyRequest->InstanceSize < sizeof(ULONG) ||
        PropertyRequest->Instance == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    pinId = *(PULONG)PropertyRequest->Instance;
    if (pinId >= JackCount || Jacks[pinId] == NULL) {
        return STATUS_INVALID_DEVICE_REQUEST;
    }

    if (PropertyRequest->ValueSize == 0) {
        PropertyRequest->ValueSize = needed;
        return STATUS_BUFFER_OVERFLOW;
    }
    if (PropertyRequest->ValueSize < needed) {
        return STATUS_BUFFER_TOO_SMALL;
    }

    multipleItem = (PKSMULTIPLE_ITEM)PropertyRequest->Value;
    description = (PKSJACK_DESCRIPTION2)(multipleItem + 1);
    multipleItem->Size = needed;
    multipleItem->Count = 1;
    RtlZeroMemory(description, sizeof(*description));
    description->DeviceStateInfo = 0;
    description->JackCapabilities = 0;
    PropertyRequest->ValueSize = needed;
    return STATUS_SUCCESS;
}

static NTSTATUS
ViosndTopologyPropertyHandler(_In_ PPCPROPERTY_REQUEST PropertyRequest)
{
    CViosndMiniportTopology *miniport;
    PKSJACK_DESCRIPTION *jacks;
    ULONG jackCount;

    if (PropertyRequest == NULL || PropertyRequest->MajorTarget == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    miniport = (CViosndMiniportTopology *)PropertyRequest->MajorTarget;
    jacks = miniport->IsCapture() ? ViosndCaptureJackDescriptions :
                                    ViosndRenderJackDescriptions;
    jackCount = miniport->IsCapture() ? SIZEOF_ARRAY(ViosndCaptureJackDescriptions) :
                                        SIZEOF_ARRAY(ViosndRenderJackDescriptions);

    if (PropertyRequest->Verb & KSPROPERTY_TYPE_BASICSUPPORT) {
        return ViosndBasicSupport(PropertyRequest,
                                  KSPROPERTY_TYPE_GET | KSPROPERTY_TYPE_BASICSUPPORT);
    }

    if ((PropertyRequest->Verb & KSPROPERTY_TYPE_GET) == 0) {
        return STATUS_INVALID_DEVICE_REQUEST;
    }

    if (PropertyRequest->PropertyItem->Id == KSPROPERTY_JACK_DESCRIPTION) {
        return ViosndGetJackDescription(PropertyRequest, jacks, jackCount);
    }

    if (PropertyRequest->PropertyItem->Id == KSPROPERTY_JACK_DESCRIPTION2) {
        return ViosndGetJackDescription2(PropertyRequest, jacks, jackCount);
    }

    return STATUS_INVALID_DEVICE_REQUEST;
}

NTSTATUS
ViosndCreateTopologyMiniport(
    _In_ BOOLEAN Capture,
    _In_ ULONG Index,
    _In_ ULONG Kind,
    _Outptr_ PMINIPORT *Miniport)
{
    PVOID memory;

    *Miniport = NULL;

    /* Before the subdevice is registered, because the lookup that turns this GUID into a name
     * runs once, when Windows first builds the endpoint. A failure here costs the endpoint its
     * name, not its audio, so it is logged and carried on from. */
    (VOID)ViosndPublishEndpointName(Capture, Index, Kind);

    memory = ExAllocatePoolUninitialized(NonPagedPoolNx,
                                         sizeof(CViosndMiniportTopology),
                                         VIOSND_POOL_TAG);
    if (memory == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    *Miniport = new(memory) CViosndMiniportTopology(Capture, Index);
    return STATUS_SUCCESS;
}
