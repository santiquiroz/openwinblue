// driver/src/l2cap_stream.c
// L2CAP channel management — Phase 2c real BRB implementation.
// Reference: Windows Driver Samples / bluetooth / bthecho (MIT License)
#include "l2cap_stream.h"
#include "avdtp.h"
#include "owb_a2dp.h"
#include "owb_trace.h"
#include "../owb_ioctl.h"   // OWB_CODEC_* ids
#include <bthioctl.h>   // IOCTL_INTERNAL_BTH_SUBMIT_BRB

// RTP header (12 bytes packed, RFC 3550).
#pragma pack(push, 1)
typedef struct _OWB_RTP_HEADER {
    UCHAR   vpxcc;      // V=2 P=0 X=0 CC=0 -> 0x80
    UCHAR   mpt;        // M=0 PT=96 (SBC) -> 0x60
    USHORT  seq_num;    // big-endian sequence number
    ULONG   timestamp;  // big-endian sample clock
    ULONG   ssrc;       // big-endian synchronization source
} OWB_RTP_HEADER;
#pragma pack(pop)

// BRB submit helper
// Submits a BRB to BthPort synchronously.
// MUST be called at PASSIVE_LEVEL only (connection setup, not streaming hot path).
NTSTATUS L2capSubmitBrb(
    _In_ POWB_DEVICE_EXTENSION DevExt,
    _In_ PBRB                  Brb,
    _In_ ULONG                 BrbSize)
{
    WDF_REQUEST_REUSE_PARAMS reuseParams;
    WDF_REQUEST_REUSE_PARAMS_INIT(&reuseParams,
                                   WDF_REQUEST_REUSE_NO_FLAGS,
                                   STATUS_NOT_SUPPORTED);
    NTSTATUS status = WdfRequestReuse(DevExt->BrbRequest, &reuseParams);
    if (!NT_SUCCESS(status)) {
        OwbLog("SubmitBrb: WdfRequestReuse fallo 0x%x", status);
        return status;
    }

    WDF_MEMORY_DESCRIPTOR memDesc;
    WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(&memDesc, Brb, BrbSize);

    return WdfIoTargetSendInternalIoctlOthersSynchronously(
        DevExt->BthIoTarget,
        DevExt->BrbRequest,
        IOCTL_INTERNAL_BTH_SUBMIT_BRB,
        &memDesc,
        NULL,   // OtherArg2
        NULL,   // OtherArg4
        NULL,   // RequestOptions
        NULL    // BytesReturned
    );
}

// Signaling receive callback
// Called by BthPort at up to DISPATCH_LEVEL when signaling data arrives.
// L2CAP indication callback — matches PFNBTHPORT_INDICATION_CALLBACK.
// Called at DISPATCH_LEVEL by BthPort for channel events.
// On IndicationRecvPacket: stores packet length and enqueues work item.
// The work item (PASSIVE_LEVEL) submits a BRB_L2CA_ACL_TRANSFER IN to read the data.
_IRQL_requires_max_(DISPATCH_LEVEL)
VOID L2capSignalingIndicationCallback(
    _In_opt_ PVOID                  Context,
    _In_     INDICATION_CODE        Indication,
    _In_     PINDICATION_PARAMETERS Parameters)
{
    POWB_DEVICE_EXTENSION devExt = (POWB_DEVICE_EXTENSION)Context;
    if (!devExt) return;

    switch (Indication) {
        case IndicationRecvPacket:
            // Store pending length; work item will read the data via BRB IN.
            devExt->PendingRecvLen = Parameters->Parameters.RecvPacket.PacketLength;
            OwbLog("L2CAP ind: RecvPacket %lu bytes -> encolando work item",
                   devExt->PendingRecvLen);
            WdfWorkItemEnqueue(devExt->AvdtpWorkItem);
            break;

        case IndicationRemoteDisconnect:
            // Remote side closed the channel.
            devExt->SignalingChannelHandle = 0;
            devExt->Avdtp.State = AvdtpStateIdle;
            OwbLog("L2CAP ind: canal de signaling DESCONECTADO por remoto");
            break;

        default:
            OwbLog("L2CAP ind: no manejada (%d)", (int)Indication);
            break;
    }
}

// L2CAP signaling channel open
NTSTATUS L2capOpenSignalingChannel(_In_ POWB_DEVICE_EXTENSION DevExt)
{
    if (!DevExt->BthInterface.BthAllocateBrb) {
        OwbLog("OpenSignaling: sin interfaz BthPort (DEVICE_NOT_READY)");
        return STATUS_DEVICE_NOT_READY;
    }
    if (DevExt->RemoteBtAddress == 0) {
        OwbLog("OpenSignaling: sin BT addr remota (DEVICE_NOT_CONNECTED)");
        return STATUS_DEVICE_NOT_CONNECTED;
    }
    OwbLog("OpenSignaling: abriendo PSM 0x%04x hacia %012I64x",
           AVDTP_SIGNALING_PSM, DevExt->RemoteBtAddress);

    struct _BRB_L2CA_OPEN_CHANNEL* brb =
        (struct _BRB_L2CA_OPEN_CHANNEL*)
        DevExt->BthInterface.BthAllocateBrb(BRB_L2CA_OPEN_CHANNEL, 'OWBO');
    if (!brb) return STATUS_INSUFFICIENT_RESOURCES;

    brb->BtAddress            = DevExt->RemoteBtAddress;
    brb->Psm                  = AVDTP_SIGNALING_PSM;
    brb->ChannelFlags         = CF_LINK_ENCRYPTED | CF_LINK_AUTHENTICATED;
    brb->ConfigIn.Mtu.Max     = L2CAP_DEFAULT_MTU;
    brb->ConfigIn.Mtu.Min     = L2CAP_MIN_MTU;
    brb->ConfigOut.Mtu.Max    = L2CAP_DEFAULT_MTU;
    brb->ConfigOut.Mtu.Min    = L2CAP_MIN_MTU;
    brb->Callback             = L2capSignalingIndicationCallback;
    brb->CallbackContext      = DevExt;
    brb->ReferenceObject      = (PVOID)WdfDeviceWdmGetDeviceObject(DevExt->Device);
    brb->CallbackFlags        = CALLBACK_DISCONNECT | CALLBACK_RECV_PACKET;

    NTSTATUS status = L2capSubmitBrb(DevExt, (PBRB)brb, sizeof(*brb));
    if (NT_SUCCESS(status)) {
        DevExt->SignalingChannelHandle = brb->ChannelHandle;
        DevExt->Avdtp.State = AvdtpStateConnecting;
        OwbLog("OpenSignaling: canal abierto handle=0x%p", (PVOID)brb->ChannelHandle);
        status = AvdtpConnect(DevExt);
    } else {
        OwbLog("OpenSignaling: BRB_L2CA_OPEN_CHANNEL fallo 0x%x", status);
    }

    DevExt->BthInterface.BthFreeBrb((PBRB)brb);
    return status;
}

// L2CAP media channel open (after AVDTP OPEN)
NTSTATUS L2capOpenMediaChannel(_In_ POWB_DEVICE_EXTENSION DevExt)
{
    if (!DevExt->BthInterface.BthAllocateBrb) {
        OwbLog("OpenMedia: sin interfaz BthPort");
        return STATUS_DEVICE_NOT_READY;
    }
    if (DevExt->RemoteBtAddress == 0) {
        OwbLog("OpenMedia: sin BT addr remota");
        return STATUS_DEVICE_NOT_CONNECTED;
    }
    OwbLog("OpenMedia: abriendo PSM 0x%04x", AVDTP_MEDIA_PSM);

    struct _BRB_L2CA_OPEN_CHANNEL* brb =
        (struct _BRB_L2CA_OPEN_CHANNEL*)
        DevExt->BthInterface.BthAllocateBrb(BRB_L2CA_OPEN_CHANNEL, 'OWBM');
    if (!brb) return STATUS_INSUFFICIENT_RESOURCES;

    brb->BtAddress            = DevExt->RemoteBtAddress;
    brb->Psm                  = AVDTP_MEDIA_PSM;
    brb->ChannelFlags         = CF_LINK_ENCRYPTED | CF_LINK_AUTHENTICATED;
    brb->ConfigIn.Mtu.Max     = L2CAP_DEFAULT_MTU;
    brb->ConfigIn.Mtu.Min     = L2CAP_MIN_MTU;
    brb->ConfigOut.Mtu.Max    = L2CAP_DEFAULT_MTU;
    brb->ConfigOut.Mtu.Min    = L2CAP_MIN_MTU;
    brb->Callback             = NULL;   // A2DP source: send-only
    brb->CallbackContext      = NULL;
    brb->ReferenceObject      = NULL;
    brb->CallbackFlags        = 0;

    NTSTATUS status = L2capSubmitBrb(DevExt, (PBRB)brb, sizeof(*brb));
    if (NT_SUCCESS(status)) {
        DevExt->MediaChannelHandle = brb->ChannelHandle;
        OwbLog("OpenMedia: canal abierto handle=0x%p", (PVOID)brb->ChannelHandle);
    } else {
        OwbLog("OpenMedia: BRB_L2CA_OPEN_CHANNEL fallo 0x%x", status);
    }

    DevExt->BthInterface.BthFreeBrb((PBRB)brb);
    return status;
}

// L2CAP signaling send
NTSTATUS L2capSendSignaling(
    _In_ POWB_DEVICE_EXTENSION DevExt,
    _In_reads_bytes_(Length) const UCHAR* Data,
    _In_ USHORT Length)
{
    if (!DevExt->BthInterface.BthAllocateBrb) {
        OwbLog("SendSignaling: sin interfaz BthPort");
        return STATUS_DEVICE_NOT_READY;
    }
    if (DevExt->SignalingChannelHandle == 0) {
        OwbLog("SendSignaling: canal de signaling cerrado");
        return STATUS_DEVICE_NOT_CONNECTED;
    }

    struct _BRB_L2CA_ACL_TRANSFER* brb =
        (struct _BRB_L2CA_ACL_TRANSFER*)
        DevExt->BthInterface.BthAllocateBrb(BRB_L2CA_ACL_TRANSFER, 'OWBS');
    if (!brb) return STATUS_INSUFFICIENT_RESOURCES;

    brb->BtAddress     = DevExt->RemoteBtAddress;
    brb->ChannelHandle = DevExt->SignalingChannelHandle;
    brb->TransferFlags = ACL_TRANSFER_DIRECTION_OUT | ACL_SHORT_TRANSFER_OK;
    brb->BufferSize    = Length;
    brb->Buffer        = (PVOID)Data;
    brb->BufferMDL     = NULL;

    NTSTATUS status = L2capSubmitBrb(DevExt, (PBRB)brb, sizeof(*brb));
    if (!NT_SUCCESS(status)) {
        OwbLog("SendSignaling: fallo 0x%x len=%u", status, Length);
    }
    DevExt->BthInterface.BthFreeBrb((PBRB)brb);
    return status;
}

// Per-codec A2DP media framing policy.
// use_rtp:        prepend the 12-byte RTP header (RFC 3550).
// payload_header: prepend the 1-byte codec payload header (SBC/LDAC frame count).
// ts_incr:        RTP timestamp advance (PCM samples represented by one frame).
// aptX streams raw codec bytes with no RTP and no payload header.
static VOID OwbMediaFraming(
    _In_ ULONG CodecId,
    _Out_ BOOLEAN* UseRtp, _Out_ BOOLEAN* PayloadHeader, _Out_ ULONG* TsIncr)
{
    switch (CodecId) {
        case OWB_CODEC_APTX:
        case OWB_CODEC_APTXHD:
        case OWB_CODEC_APTX_ADAPTIVE:
            *UseRtp = FALSE; *PayloadHeader = FALSE; *TsIncr = 0u;   break;
        case OWB_CODEC_AAC:
            *UseRtp = TRUE;  *PayloadHeader = FALSE; *TsIncr = 1024u; break;
        case OWB_CODEC_LC3:
            *UseRtp = TRUE;  *PayloadHeader = FALSE; *TsIncr = 480u;  break;
        case OWB_CODEC_LDAC:
        case OWB_CODEC_SBC:
        default:
            *UseRtp = TRUE;  *PayloadHeader = TRUE;  *TsIncr = 128u;  break;
    }
}

// L2CAP media frame send
NTSTATUS L2capSendMediaFrame(
    _In_ POWB_DEVICE_EXTENSION DevExt,
    _In_ ULONG  CodecId,
    _In_reads_bytes_(FrameLen) const UCHAR* FrameData,
    _In_ USHORT FrameLen)
{
    static ULONG mediaCount     = 0;
    static ULONG notStreamCount = 0;

    if (DevExt->Avdtp.State != AvdtpStateStreaming) {
        notStreamCount++;
        if ((notStreamCount % 1000u) == 1u) {
            OwbLog("SendMedia: descartado #%lu - estado=%s (no Streaming)",
                   notStreamCount, OwbAvdtpStateName(DevExt->Avdtp.State));
        }
        return STATUS_DEVICE_NOT_CONNECTED;
    }
    if (!DevExt->BthInterface.BthAllocateBrb) {
        OwbLog("SendMedia: sin interfaz BthPort");
        return STATUS_DEVICE_NOT_READY;
    }

    BOOLEAN use_rtp = TRUE, payload_hdr = TRUE;
    ULONG   ts_incr = 128u;
    OwbMediaFraming(CodecId, &use_rtp, &payload_hdr, &ts_incr);

    const ULONG rtp_len = use_rtp ? (ULONG)sizeof(OWB_RTP_HEADER) : 0u;
    const ULONG hdr_len = payload_hdr ? 1u : 0u;

    ULONG pkt_len_u = rtp_len + hdr_len + (ULONG)FrameLen;
    if (pkt_len_u > 0xFFFFu) return STATUS_INVALID_PARAMETER;
    const USHORT pkt_len = (USHORT)pkt_len_u;

    PUCHAR pkt = (PUCHAR)ExAllocatePool2(POOL_FLAG_NON_PAGED,
                                          (SIZE_T)pkt_len, 'RTPM');
    if (!pkt) return STATUS_INSUFFICIENT_RESOURCES;

    if (use_rtp) {
        OWB_RTP_HEADER* hdr = (OWB_RTP_HEADER*)pkt;
        hdr->vpxcc     = 0x80u;
        hdr->mpt       = 0x60u;
        hdr->seq_num   = RtlUshortByteSwap(DevExt->RtpSeqNum++);
        hdr->timestamp = RtlUlongByteSwap(DevExt->RtpTimestamp);
        hdr->ssrc      = RtlUlongByteSwap(0x00000001UL);
        DevExt->RtpTimestamp += ts_incr;
    }
    if (payload_hdr)
        pkt[rtp_len] = 0x01u;  // 1 frame, no fragmentation
    RtlCopyMemory(pkt + rtp_len + hdr_len, FrameData, FrameLen);

    struct _BRB_L2CA_ACL_TRANSFER* brb =
        (struct _BRB_L2CA_ACL_TRANSFER*)
        DevExt->BthInterface.BthAllocateBrb(BRB_L2CA_ACL_TRANSFER, 'BRTM');
    if (!brb) {
        ExFreePoolWithTag(pkt, 'RTPM');
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    brb->BtAddress     = DevExt->RemoteBtAddress;
    brb->ChannelHandle = DevExt->MediaChannelHandle;
    brb->TransferFlags = ACL_TRANSFER_DIRECTION_OUT | ACL_SHORT_TRANSFER_OK;
    brb->BufferSize    = pkt_len;
    brb->Buffer        = pkt;
    brb->BufferMDL     = NULL;

    NTSTATUS status = L2capSubmitBrb(DevExt, (PBRB)brb, sizeof(*brb));
    DevExt->BthInterface.BthFreeBrb((PBRB)brb);
    ExFreePoolWithTag(pkt, 'RTPM');

    mediaCount++;
    if (mediaCount == 1u) {
        OwbLog("SendMedia: PRIMER paquete (codec=%lu pkt=%u rtp=%d status=0x%x)",
               CodecId, pkt_len, use_rtp, status);
    } else if (!NT_SUCCESS(status) || (mediaCount % 1000u) == 0u) {
        OwbLog("SendMedia: #%lu codec=%lu pkt=%u status=0x%x",
               mediaCount, CodecId, pkt_len, status);
    }
    return status;
}

// RSSI query
NTSTATUS L2capGetRssi(
    _In_  POWB_DEVICE_EXTENSION DevExt,
    _Out_ LONG*                 RssiDbm)
{
    *RssiDbm = -60L;  // safe default (medium signal strength)
    // TODO(phase3): submit BRB_HCI_GET_LINK_QUALITY for real RSSI.
    UNREFERENCED_PARAMETER(DevExt);
    return STATUS_SUCCESS;
}
