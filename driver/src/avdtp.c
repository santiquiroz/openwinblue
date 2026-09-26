// driver/src/avdtp.c
// AVDTP signaling state machine.
#include "avdtp.h"
#include "avdtp_packets.h"
#include "owb_a2dp.h"
#include "owb_trace.h"
#include "l2cap_stream.h"
#include "../owb_ioctl.h"

VOID AvdtpContextInit(_Out_ POWB_AVDTP_CONTEXT Ctx) {
    RtlZeroMemory(Ctx, sizeof(*Ctx));
    Ctx->State         = AvdtpStateIdle;
    Ctx->LocalSeid     = 0x01;
    Ctx->ActiveCodecId = OWB_CODEC_SBC;
    Ctx->PendingCodecId = OWB_CODEC_SBC;
    OwbLog("AVDTP: contexto inicializado (SEID local=0x%02x)", Ctx->LocalSeid);
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
    if (!buf) {
        OwbLog("AVDTP TX: sin memoria para cmd 0x%02x", SignalId);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    DevExt->Avdtp.TransactionId = (UCHAR)((DevExt->Avdtp.TransactionId + 1u) & 0x0Fu);

    buf[0] = (UCHAR)((DevExt->Avdtp.TransactionId << 4u) |
                     (AVDTP_PKT_SINGLE << 2u) |
                     AVDTP_MSG_CMD);
    buf[1] = (UCHAR)(SignalId & 0x3Fu);

    if (Payload && PayloadLen > 0u)
        RtlCopyMemory(buf + 2, Payload, PayloadLen);

    NTSTATUS status = L2capSendSignaling(DevExt, buf, pkt_len);
    OwbLog("AVDTP TX: cmd signal=0x%02x txid=%u len=%u -> 0x%x",
           SignalId, DevExt->Avdtp.TransactionId, pkt_len, status);
    ExFreePoolWithTag(buf, 'AVDT');
    return status;
}

NTSTATUS AvdtpConnect(_In_ POWB_DEVICE_EXTENSION DevExt) {
    // Se invoca desde dos sitios: tras abrir el canal de señalización
    // (State=Connecting) y desde AvdtpSetPreferredCodec (State=Idle). Ambos son
    // válidos para arrancar DISCOVER; cualquier otro estado sí es inválido.
    if (DevExt->Avdtp.State != AvdtpStateIdle &&
        DevExt->Avdtp.State != AvdtpStateConnecting) {
        OwbLog("AvdtpConnect: rechazado, estado=%s",
               OwbAvdtpStateName(DevExt->Avdtp.State));
        return STATUS_INVALID_DEVICE_STATE;
    }

    OwbLog("AvdtpConnect: enviando DISCOVER");
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
    OwbLog("DISCOVER rsp: %u bytes (%u SEPs)", Len, (USHORT)(Len / 2u));
    const UCHAR seid = owb_avdtp_find_free_audio_sink_seid(Data, Len);
    if (seid == OWB_AVDTP_SEID_NONE) {
        OwbLog("DISCOVER rsp: sin SEID de audio sink libre");
        return;
    }
    DevExt->Avdtp.RemoteSeid = seid;
    DevExt->Avdtp.State = AvdtpStateConfiguring;
    OwbLog("DISCOVER rsp: audio sink SEID=0x%02x -> GET_CAPABILITIES", seid);
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
    OwbLog("GET_CAPABILITIES rsp: %u bytes, codec preferido=%lu",
           Len, DevExt->PreferredCodecId);
    const ULONG codec = owb_avdtp_select_codec(DevExt->PreferredCodecId, Data, Len);
    const size_t plen = owb_avdtp_build_set_configuration(
        codec, DevExt->Avdtp.RemoteSeid, DevExt->Avdtp.LocalSeid,
        payload, sizeof(payload));

    if (plen == 0u) {
        OwbLog("SET_CONFIGURATION: no se pudo construir payload");
        return;
    }
    OwbLog("negociando codec %lu", codec);
    NTSTATUS st = AvdtpSendCommand(DevExt, AVDTP_MSG_SET_CONFIGURATION,
                                   payload, (USHORT)plen);
    if (!NT_SUCCESS(st)) {
        OwbLog("SET_CONFIGURATION: envio fallo 0x%x", st);
        return;
    }
    DevExt->Avdtp.PendingCodecId = codec;
    DevExt->Avdtp.State = AvdtpStateConfigured;
}

static VOID HandleSetConfigurationResponse(_In_ POWB_DEVICE_EXTENSION DevExt) {
    DevExt->Avdtp.ActiveCodecId = DevExt->Avdtp.PendingCodecId;
    OwbLog("SET_CONFIGURATION aceptada (codec=%lu) -> enviando OPEN",
           DevExt->Avdtp.ActiveCodecId);
    UCHAR seid = (UCHAR)((DevExt->Avdtp.RemoteSeid << 2u) & 0xFCu);
    NTSTATUS st = AvdtpSendCommand(DevExt, AVDTP_MSG_OPEN, &seid, 1u);
    if (NT_SUCCESS(st)) DevExt->Avdtp.State = AvdtpStateOpen;
    else OwbLog("OPEN: envio fallo 0x%x", st);
}

static VOID HandleOpenResponse(_In_ POWB_DEVICE_EXTENSION DevExt) {
    OwbLog("OPEN aceptada -> abriendo canal de media");
    // Open the A2DP media L2CAP channel before sending AVDTP START.
    NTSTATUS st = L2capOpenMediaChannel(DevExt);
    if (!NT_SUCCESS(st)) {
        OwbLog("canal de media fallo 0x%x - abortando (vuelve a Idle)", st);
        DevExt->Avdtp.State = AvdtpStateIdle;
        return;  // Don't send START without a media channel
    }
    UCHAR seid = (UCHAR)((DevExt->Avdtp.RemoteSeid << 2u) & 0xFCu);
    AvdtpSendCommand(DevExt, AVDTP_MSG_START, &seid, 1u);
    // State transitions to AvdtpStateStreaming on successful START response.
}

static VOID HandleStartResponse(_In_ POWB_DEVICE_EXTENSION DevExt) {
    DevExt->Avdtp.State = AvdtpStateStreaming;
    OwbLog("*** A2DP STREAMING INICIADO (codec=%lu) ***",
           DevExt->Avdtp.ActiveCodecId);
}

NTSTATUS AvdtpSetPreferredCodec(
    _In_ POWB_DEVICE_EXTENSION DevExt,
    _In_ ULONG                 NewCodecId)
{
    DevExt->PreferredCodecId = NewCodecId;
    OwbLog("codec preferido -> %lu (estado=%s)",
           NewCodecId, OwbAvdtpStateName(DevExt->Avdtp.State));

    // If currently streaming, trigger reconfiguration: SUSPEND → re-negotiate → START.
    if (DevExt->Avdtp.State == AvdtpStateStreaming) {
        OwbLog("streaming activo -> SUSPEND + renegociacion");
        UCHAR seid = (UCHAR)((DevExt->Avdtp.RemoteSeid << 2u) & 0xFCu);
        NTSTATUS st = AvdtpSendCommand(DevExt, AVDTP_MSG_SUSPEND, &seid, 1u);
        if (!NT_SUCCESS(st)) {
            OwbLog("SUSPEND fallo 0x%x", st);
            return st;
        }
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
    if (Length < 2u) {
        OwbLog("AVDTP RX: paquete corto (%u bytes) - descartado", Length);
        return;
    }

    const UCHAR msg_type  = Data[0] & 0x03u;
    const UCHAR signal_id = Data[1] & 0x3Fu;
    const UCHAR* payload  = (Length > 2u) ? Data + 2 : NULL;
    const USHORT pay_len  = (Length > 2u) ? (USHORT)(Length - 2u) : 0u;

    OwbLog("AVDTP RX: type=%u signal=0x%02x len=%u estado=%s",
           msg_type, signal_id, Length, OwbAvdtpStateName(DevExt->Avdtp.State));

    if (msg_type != AVDTP_MSG_RESPONSE_ACCEPT) {
        OwbLog("AVDTP RX: RECHAZO/no-accept signal=0x%02x type=%u", signal_id, msg_type);
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
            OwbLog("AVDTP RX: respuesta desconocida 0x%02x", signal_id);
            break;
    }
}
