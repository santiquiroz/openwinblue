// driver/src/avdtp.c
// AVDTP signaling state machine.
#include "avdtp.h"
#include "owb_a2dp.h"
#include "l2cap_stream.h"
#include "../owb_ioctl.h"

VOID AvdtpContextInit(_Out_ POWB_AVDTP_CONTEXT Ctx) {
    RtlZeroMemory(Ctx, sizeof(*Ctx));
    Ctx->State         = AvdtpStateIdle;
    Ctx->LocalSeid     = 0x01;
    Ctx->ActiveCodecId = OWB_CODEC_SBC;
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

    DevExt->Avdtp.State = AvdtpStateDiscovering;
    return AvdtpSendCommand(DevExt, AVDTP_MSG_DISCOVER, NULL, 0u);
}

// Handle DISCOVER response: find first audio sink SEID, send GET_CAPABILITIES.
static VOID HandleDiscoverResponse(
    _In_ POWB_DEVICE_EXTENSION DevExt,
    _In_reads_bytes_opt_(Len) const UCHAR* Data,
    _In_ USHORT Len)
{
    if (!Data || Len == 0u) return;
    for (USHORT i = 0u; i + 1u < Len; i += 2u) {
        UCHAR tsep = (Data[i] >> 1u) & 0x01u;  // bit 1 = TSEP per AVDTP spec §8.6.2
        if (tsep == 0x01u) {   // 1 = SNK (audio sink), 0 = SRC
            DevExt->Avdtp.RemoteSeid = (UCHAR)((Data[i] >> 2u) & 0x3Fu);
            DevExt->Avdtp.State = AvdtpStateConfiguring;
            UCHAR payload = (UCHAR)((DevExt->Avdtp.RemoteSeid << 2u) & 0xFCu);
            AvdtpSendCommand(DevExt, AVDTP_MSG_GET_CAPABILITIES, &payload, 1u);
            return;
        }
    }
    KdPrint(("OpenWinBlue: AVDTP DISCOVER found no audio sink SEID\n"));
}

// Build SBC SET_CONFIGURATION payload (A2DP spec Table 4.25).
// 44.1kHz | Joint Stereo | 16 blocks | 8 subbands | Loudness | bitpool 2-53.
static USHORT BuildSbcSetConfig(
    _Out_writes_bytes_(MaxLen) PUCHAR Buf,
    _In_ UCHAR AcpSeid,
    _In_ UCHAR IntSeid,
    _In_ USHORT MaxLen)
{
    if (MaxLen < 10u) return 0u;
    Buf[0] = (UCHAR)((AcpSeid << 2u) & 0xFCu);  // ACP_SEID
    Buf[1] = (UCHAR)((IntSeid << 2u) & 0xFCu);  // INT_SEID
    Buf[2] = 0x07u;  // Service Category: Media Codec
    Buf[3] = 0x06u;  // LOSC = 6
    Buf[4] = 0x00u;  // Media Type: Audio
    Buf[5] = 0x00u;  // Codec Type: SBC
    Buf[6] = 0x21u;  // 44.1kHz | Joint Stereo
    Buf[7] = 0x15u;  // Blocks=16 | Subbands=8 | Loudness
    Buf[8] = 0x02u;  // min bitpool
    Buf[9] = 0x35u;  // max bitpool = 53
    return 10u;
}

static VOID HandleGetCapabilitiesResponse(_In_ POWB_DEVICE_EXTENSION DevExt) {
    UCHAR payload[10];
    USHORT len = BuildSbcSetConfig(payload,
                                   DevExt->Avdtp.RemoteSeid,
                                   DevExt->Avdtp.LocalSeid,
                                   sizeof(payload));
    if (len == 0u) {
        KdPrint(("OpenWinBlue: BuildSbcSetConfig failed\n"));
        return;
    }
    NTSTATUS st = AvdtpSendCommand(DevExt, AVDTP_MSG_SET_CONFIGURATION, payload, len);
    if (NT_SUCCESS(st)) DevExt->Avdtp.State = AvdtpStateConfigured;
}

static VOID HandleSetConfigurationResponse(_In_ POWB_DEVICE_EXTENSION DevExt) {
    UCHAR seid = (UCHAR)((DevExt->Avdtp.RemoteSeid << 2u) & 0xFCu);
    NTSTATUS st = AvdtpSendCommand(DevExt, AVDTP_MSG_OPEN, &seid, 1u);
    if (NT_SUCCESS(st)) DevExt->Avdtp.State = AvdtpStateOpen;
}

static VOID HandleOpenResponse(_In_ POWB_DEVICE_EXTENSION DevExt) {
    UCHAR seid = (UCHAR)((DevExt->Avdtp.RemoteSeid << 2u) & 0xFCu);
    AvdtpSendCommand(DevExt, AVDTP_MSG_START, &seid, 1u);
    // State remains AvdtpStateOpen until START response confirms streaming.
}

static VOID HandleStartResponse(_In_ POWB_DEVICE_EXTENSION DevExt) {
    DevExt->Avdtp.State = AvdtpStateStreaming;
    KdPrint(("OpenWinBlue: A2DP streaming started (SBC)\n"));
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
            HandleGetCapabilitiesResponse(DevExt);
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
