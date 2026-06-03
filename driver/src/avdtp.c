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

    NTSTATUS st = AvdtpSendCommand(DevExt, AVDTP_MSG_DISCOVER, NULL, 0u);
    if (NT_SUCCESS(st)) DevExt->Avdtp.State = AvdtpStateDiscovering;
    return st;
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

// Build LDAC SET_CONFIGURATION payload (Sony LDAC, vendor codec type 0xFF).
// Total: 12 bytes. Buf must be at least 12 bytes.
static USHORT BuildLdacSetConfig(
    _Out_writes_bytes_(MaxLen) PUCHAR Buf,
    _In_ UCHAR AcpSeid, _In_ UCHAR IntSeid, _In_ USHORT MaxLen)
{
    if (MaxLen < 12u) return 0u;
    Buf[0]  = (UCHAR)((AcpSeid << 2u) & 0xFCu);  // ACP_SEID
    Buf[1]  = (UCHAR)((IntSeid << 2u) & 0xFCu);  // INT_SEID
    Buf[2]  = 0x07u;   // Service Category: Media Codec
    Buf[3]  = 0x08u;   // LOSC = 8
    Buf[4]  = 0x00u;   // Media Type: Audio
    Buf[5]  = 0xFFu;   // Codec Type: Vendor Specific
    // Sony vendor ID: 0x0000012D (little-endian)
    Buf[6]  = 0x2Du; Buf[7]  = 0x01u; Buf[8]  = 0x00u; Buf[9]  = 0x00u;
    // LDAC Codec ID: 0x00AA (little-endian)
    Buf[10] = 0xAAu; Buf[11] = 0x00u;
    return 12u;
}

// Build aptX Classic (isHD=FALSE) or aptX HD (isHD=TRUE) SET_CONFIGURATION payload.
// Classic: 13 bytes total. HD: 14 bytes total.
static USHORT BuildAptxSetConfig(
    _Out_writes_bytes_(MaxLen) PUCHAR Buf,
    _In_ UCHAR AcpSeid, _In_ UCHAR IntSeid, _In_ USHORT MaxLen, _In_ BOOLEAN isHD)
{
    const USHORT needed = isHD ? 14u : 13u;
    if (MaxLen < needed) return 0u;
    Buf[0] = (UCHAR)((AcpSeid << 2u) & 0xFCu);
    Buf[1] = (UCHAR)((IntSeid << 2u) & 0xFCu);
    Buf[2] = 0x07u;                           // Service Category: Media Codec
    Buf[3] = (UCHAR)(5u + (isHD ? 2u : 1u)); // LOSC
    Buf[4] = 0x00u;                           // Media Type: Audio
    Buf[5] = 0xFFu;                           // Codec Type: Vendor Specific
    if (!isHD) {
        // Qualcomm aptX: 0x0000004F
        Buf[6] = 0x4Fu; Buf[7] = 0x00u; Buf[8] = 0x00u; Buf[9] = 0x00u;
        Buf[10] = 0x01u; Buf[11] = 0x00u;  // Codec ID: 0x0001
        Buf[12] = 0x22u;  // 44.1kHz(0x20) | Stereo(0x02)
        return 13u;
    } else {
        // Qualcomm aptX HD: 0x000000D7
        Buf[6] = 0xD7u; Buf[7] = 0x00u; Buf[8] = 0x00u; Buf[9] = 0x00u;
        Buf[10] = 0x24u; Buf[11] = 0x00u;  // Codec ID: 0x0024
        Buf[12] = 0x22u;  // 44.1kHz | Stereo
        Buf[13] = 0x00u;  // reserved
        return 14u;
    }
}

// Check if the GET_CAPABILITIES response payload contains a vendor codec
// with the given 4-byte vendor ID and 2-byte codec ID.
static BOOLEAN CapabilitiesContainsVendorCodec(
    _In_reads_bytes_opt_(Len) const UCHAR* Data, _In_ USHORT Len,
    _In_ ULONG VendorId, _In_ USHORT CodecId)
{
    if (!Data || Len < 4u) return FALSE;
    USHORT i = 0u;
    while (i + 1u < Len) {
        UCHAR cat  = Data[i];
        UCHAR losc = Data[i + 1u];
        if (cat == 0x07u && losc >= 6u && (USHORT)(i + 2u + losc) <= Len) {
            if (Data[i + 3u] == 0xFFu && (USHORT)(i + 9u) < Len) {
                ULONG vid = (ULONG)Data[i+4u] | ((ULONG)Data[i+5u] << 8u)
                          | ((ULONG)Data[i+6u] << 16u) | ((ULONG)Data[i+7u] << 24u);
                USHORT cid = (USHORT)Data[i+8u] | ((USHORT)Data[i+9u] << 8u);
                if (vid == VendorId && cid == CodecId) return TRUE;
            }
        }
        if (losc == 0u) break;
        i = (USHORT)(i + 2u + losc);
    }
    return FALSE;
}

static VOID HandleGetCapabilitiesResponse(
    _In_ POWB_DEVICE_EXTENSION DevExt,
    _In_reads_bytes_opt_(Len) const UCHAR* Data,
    _In_ USHORT Len)
{
    UCHAR payload[16];
    USHORT plen = 0u;
    ULONG preferred = DevExt->PreferredCodecId;

    if (preferred == OWB_CODEC_LDAC &&
        CapabilitiesContainsVendorCodec(Data, Len, 0x0000012DUL, 0x00AAu)) {
        plen = BuildLdacSetConfig(payload, DevExt->Avdtp.RemoteSeid,
                                  DevExt->Avdtp.LocalSeid, sizeof(payload));
        KdPrint(("OpenWinBlue: negotiating LDAC\n"));
    } else if (preferred == OWB_CODEC_APTXHD &&
               CapabilitiesContainsVendorCodec(Data, Len, 0x000000D7UL, 0x0024u)) {
        plen = BuildAptxSetConfig(payload, DevExt->Avdtp.RemoteSeid,
                                   DevExt->Avdtp.LocalSeid, sizeof(payload), TRUE);
        KdPrint(("OpenWinBlue: negotiating aptX HD\n"));
    } else if (preferred == OWB_CODEC_APTX &&
               CapabilitiesContainsVendorCodec(Data, Len, 0x0000004FUL, 0x0001u)) {
        plen = BuildAptxSetConfig(payload, DevExt->Avdtp.RemoteSeid,
                                   DevExt->Avdtp.LocalSeid, sizeof(payload), FALSE);
        KdPrint(("OpenWinBlue: negotiating aptX Classic\n"));
    } else {
        plen = BuildSbcSetConfig(payload, DevExt->Avdtp.RemoteSeid,
                                  DevExt->Avdtp.LocalSeid, sizeof(payload));
        KdPrint(("OpenWinBlue: negotiating SBC (fallback)\n"));
    }

    if (plen == 0u) {
        KdPrint(("OpenWinBlue: failed to build SET_CONFIGURATION payload\n"));
        return;
    }
    NTSTATUS st = AvdtpSendCommand(DevExt, AVDTP_MSG_SET_CONFIGURATION, payload, plen);
    if (NT_SUCCESS(st)) DevExt->Avdtp.State = AvdtpStateConfigured;
}

static VOID HandleSetConfigurationResponse(_In_ POWB_DEVICE_EXTENSION DevExt) {
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
