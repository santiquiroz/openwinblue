// driver/src/avdtp.c
// AVDTP signaling state machine.
#include "avdtp.h"
#include "avdtp_packets.h"
#include "owb_a2dp.h"
#include "l2cap_stream.h"
#include "../owb_ioctl.h"

VOID AvdtpContextInit(_Out_ POWB_AVDTP_CONTEXT Ctx) {
    RtlZeroMemory(Ctx, sizeof(*Ctx));
    Ctx->State         = AvdtpStateIdle;
    Ctx->LocalSeid     = 0x01;
    Ctx->ActiveCodecId = OWB_CODEC_SBC;
    Ctx->PendingCodecId = OWB_CODEC_SBC;
}

// Build and send a single-packet AVDTP command.
NTSTATUS AvdtpSendCommand(
    _In_ POWB_DEVICE_EXTENSION DevExt,
    _In_ UCHAR  SignalId,
    _In_reads_bytes_opt_(PayloadLen) const UCHAR* Payload,
    _In_ USHORT PayloadLen)
{
    if (PayloadLen > (USHORT)(0xFFFFu - 2u)) return STATUS_INVALID_PARAMETER;
    const USHORT pkt_len = (USHORT)(2u + PayloadLen);
    PUCHAR buf = (PUCHAR)ExAllocatePool2(POOL_FLAG_NON_PAGED,
                                          (SIZE_T)pkt_len, 'AVDT');
    if (!buf) return STATUS_INSUFFICIENT_RESOURCES;

    DevExt->Avdtp.TransactionId = (UCHAR)((DevExt->Avdtp.TransactionId + 1u) & 0x0Fu);

    buf[0] = (UCHAR)((DevExt->Avdtp.TransactionId << 4u) |
                     (AVDTP_PKT_SINGLE << 2u) |
                     AVDTP_MSG_CMD);
    buf[1] = (UCHAR)(SignalId & 0x3Fu);

    if (Payload && PayloadLen > 0u)
        RtlCopyMemory(buf + 2, Payload, PayloadLen);

    NTSTATUS status = L2capSendSignaling(DevExt, buf, pkt_len);
    ExFreePoolWithTag(buf, 'AVDT');
    return status;
}

NTSTATUS AvdtpConnect(_In_ POWB_DEVICE_EXTENSION DevExt) {
    if (DevExt->Avdtp.State != AvdtpStateIdle)
        return STATUS_INVALID_DEVICE_STATE;

    NTSTATUS st = AvdtpSendCommand(DevExt, AVDTP_MSG_DISCOVER, NULL, 0u);
    if (NT_SUCCESS(st)) DevExt->Avdtp.State = AvdtpStateDiscovering;
    return st;
}

// Handle DISCOVER response: find first free audio sink SEID, send GET_CAPABILITIES.
static VOID HandleDiscoverResponse(
    _In_ POWB_DEVICE_EXTENSION DevExt,
    _In_reads_bytes_opt_(Len) const UCHAR* Data,
    _In_ USHORT Len)
{
    const UCHAR seid = owb_avdtp_find_free_audio_sink_seid(Data, Len);
    if (seid == OWB_AVDTP_SEID_NONE) {
        KdPrint(("OpenWinBlue: AVDTP DISCOVER found no free audio sink SEID\n"));
        return;
    }
    DevExt->Avdtp.RemoteSeid = seid;
    DevExt->Avdtp.State = AvdtpStateConfiguring;
    UCHAR payload = (UCHAR)((seid << 2u) & 0xFCu);
    AvdtpSendCommand(DevExt, AVDTP_MSG_GET_CAPABILITIES, &payload, 1u);
}

// Pick the preferred codec if the sink supports it (SBC otherwise) and send
// SET_CONFIGURATION. LC3 is LE Audio only, so an LC3 preference falls back to SBC.
static VOID HandleGetCapabilitiesResponse(
    _In_ POWB_DEVICE_EXTENSION DevExt,
    _In_reads_bytes_opt_(Len) const UCHAR* Data,
    _In_ USHORT Len)
{
    UCHAR payload[OWB_AVDTP_SET_CONFIGURATION_MAX_LEN];
    const ULONG codec = owb_avdtp_select_codec(DevExt->PreferredCodecId, Data, Len);
    const size_t plen = owb_avdtp_build_set_configuration(
        codec, DevExt->Avdtp.RemoteSeid, DevExt->Avdtp.LocalSeid,
        payload, sizeof(payload));

    if (plen == 0u) {
        KdPrint(("OpenWinBlue: failed to build SET_CONFIGURATION payload\n"));
        return;
    }
    KdPrint(("OpenWinBlue: negotiating codec %lu\n", codec));
    NTSTATUS st = AvdtpSendCommand(DevExt, AVDTP_MSG_SET_CONFIGURATION,
                                   payload, (USHORT)plen);
    if (!NT_SUCCESS(st)) return;
    DevExt->Avdtp.PendingCodecId = codec;
    DevExt->Avdtp.State = AvdtpStateConfigured;
}

static VOID HandleSetConfigurationResponse(_In_ POWB_DEVICE_EXTENSION DevExt) {
    DevExt->Avdtp.ActiveCodecId = DevExt->Avdtp.PendingCodecId;
    UCHAR seid = (UCHAR)((DevExt->Avdtp.RemoteSeid << 2u) & 0xFCu);
    NTSTATUS st = AvdtpSendCommand(DevExt, AVDTP_MSG_OPEN, &seid, 1u);
    if (NT_SUCCESS(st)) DevExt->Avdtp.State = AvdtpStateOpen;
}

static VOID HandleOpenResponse(_In_ POWB_DEVICE_EXTENSION DevExt) {
    // Open the A2DP media L2CAP channel before sending AVDTP START.
    NTSTATUS st = L2capOpenMediaChannel(DevExt);
    if (!NT_SUCCESS(st)) {
        KdPrint(("OpenWinBlue: media channel open failed 0x%x — aborting\n", st));
        DevExt->Avdtp.State = AvdtpStateIdle;
        return;  // Don't send START without a media channel
    }
    UCHAR seid = (UCHAR)((DevExt->Avdtp.RemoteSeid << 2u) & 0xFCu);
    AvdtpSendCommand(DevExt, AVDTP_MSG_START, &seid, 1u);
    // State transitions to AvdtpStateStreaming on successful START response.
}

static VOID HandleStartResponse(_In_ POWB_DEVICE_EXTENSION DevExt) {
    DevExt->Avdtp.State = AvdtpStateStreaming;
    KdPrint(("OpenWinBlue: A2DP streaming started\n"));
}

NTSTATUS AvdtpSetPreferredCodec(
    _In_ POWB_DEVICE_EXTENSION DevExt,
    _In_ ULONG                 NewCodecId)
{
    DevExt->PreferredCodecId = NewCodecId;
    KdPrint(("OpenWinBlue: preferred codec set to %lu\n", NewCodecId));

    // If currently streaming, trigger reconfiguration: SUSPEND → re-negotiate → START.
    if (DevExt->Avdtp.State == AvdtpStateStreaming) {
        UCHAR seid = (UCHAR)((DevExt->Avdtp.RemoteSeid << 2u) & 0xFCu);
        NTSTATUS st = AvdtpSendCommand(DevExt, AVDTP_MSG_SUSPEND, &seid, 1u);
        if (!NT_SUCCESS(st)) return st;
        DevExt->Avdtp.State = AvdtpStateIdle;
        return AvdtpConnect(DevExt);
    }
    return STATUS_SUCCESS;
}

VOID AvdtpHandleSignalingPacket(
    _In_    POWB_DEVICE_EXTENSION DevExt,
    _In_reads_bytes_(Length) const UCHAR* Data,
    _In_    USHORT Length)
{
    if (Length < 2u) return;

    const UCHAR msg_type  = Data[0] & 0x03u;
    const UCHAR signal_id = Data[1] & 0x3Fu;
    const UCHAR* payload  = (Length > 2u) ? Data + 2 : NULL;
    const USHORT pay_len  = (Length > 2u) ? (USHORT)(Length - 2u) : 0u;

    if (msg_type != AVDTP_MSG_RESPONSE_ACCEPT) {
        KdPrint(("OpenWinBlue: AVDTP rejected signal=0x%02x\n", signal_id));
        return;
    }

    switch (signal_id) {
        case AVDTP_MSG_DISCOVER:
            HandleDiscoverResponse(DevExt, payload, pay_len);
            break;
        case AVDTP_MSG_GET_CAPABILITIES:
            HandleGetCapabilitiesResponse(DevExt, payload, pay_len);
            break;
        case AVDTP_MSG_SET_CONFIGURATION:
            HandleSetConfigurationResponse(DevExt);
            break;
        case AVDTP_MSG_OPEN:
            HandleOpenResponse(DevExt);
            break;
        case AVDTP_MSG_START:
            HandleStartResponse(DevExt);
            break;
        default:
            KdPrint(("OpenWinBlue: unknown AVDTP response 0x%02x\n", signal_id));
            break;
    }
}
