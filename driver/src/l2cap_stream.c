// driver/src/l2cap_stream.c
// L2CAP channel management — Phase 2c real BRB implementation.
#include "l2cap_stream.h"
#include "avdtp.h"
#include "owb_a2dp.h"

// RTP header (12 bytes packed, RFC 3550).
#pragma pack(push, 1)
typedef struct _OWB_RTP_HEADER {
    UCHAR   vpxcc;      // V=2 P=0 X=0 CC=0 → 0x80
    UCHAR   mpt;        // M=0 PT=96 (SBC) → 0x60
    USHORT  seq_num;    // big-endian sequence number
    ULONG   timestamp;  // big-endian sample clock
    ULONG   ssrc;       // big-endian synchronization source
} OWB_RTP_HEADER;
#pragma pack(pop)

// ── BRB submit helper ─────────────────────────────────────────────────────────
NTSTATUS L2capSubmitBrb(
    _In_ POWB_DEVICE_EXTENSION DevExt,
    _In_ PBRB Brb)
{
    if (!DevExt->BthInterface.BthSubmitBrb)
        return STATUS_DEVICE_NOT_READY;

    WDF_REQUEST_SEND_OPTIONS sendOpts;
    WDF_REQUEST_SEND_OPTIONS_INIT(&sendOpts, WDF_REQUEST_SEND_OPTION_SYNCHRONOUS);
    WDF_REQUEST_SEND_OPTIONS_SET_TIMEOUT(&sendOpts, WDF_REL_TIMEOUT_IN_SEC(10));

    WDFREQUEST request;
    NTSTATUS   status = WdfRequestCreate(
        WDF_NO_OBJECT_ATTRIBUTES, DevExt->BthIoTarget, &request);
    if (!NT_SUCCESS(status)) {
        KdPrint(("OpenWinBlue: WdfRequestCreate failed 0x%x\n", status));
        return status;
    }

    status = DevExt->BthInterface.BthSubmitBrb(
        DevExt->BthIoTarget, request, Brb, &sendOpts);

    WdfObjectDelete(request);
    return status;
}

// ── Signaling receive callback ────────────────────────────────────────────────
_IRQL_requires_max_(DISPATCH_LEVEL)
VOID L2capSignalingReceiveCallback(
    _In_                       PVOID  Context,
    _In_reads_bytes_(DataSize)  PUCHAR Data,
    _In_                       UINT   DataSize)
{
    POWB_DEVICE_EXTENSION devExt = (POWB_DEVICE_EXTENSION)Context;
    if (!devExt || !Data || DataSize == 0 || DataSize > 0xFFFFu) return;
    AvdtpHandleSignalingPacket(devExt, Data, (USHORT)DataSize);
}

// ── L2CAP signaling channel open ─────────────────────────────────────────────
NTSTATUS L2capOpenSignalingChannel(_In_ POWB_DEVICE_EXTENSION DevExt)
{
    if (!DevExt->BthInterface.BthAllocateBrb)
        return STATUS_DEVICE_NOT_READY;
    if (DevExt->RemoteBtAddress == 0)
        return STATUS_DEVICE_NOT_CONNECTED;

    struct _BRB_L2CA_OPEN_CHANNEL* brb =
        (struct _BRB_L2CA_OPEN_CHANNEL*)
        DevExt->BthInterface.BthAllocateBrb(BRB_L2CA_OPEN_CHANNEL, 'OWBO');
    if (!brb) return STATUS_INSUFFICIENT_RESOURCES;

    brb->BtAddress            = DevExt->RemoteBtAddress;
    brb->Psm                  = AVDTP_SIGNALING_PSM;
    brb->ChannelFlags         = CF_LINK_ENCRYPTED | CF_LINK_AUTHENTICATED;
    brb->IncomingMtuRange.Max = L2CAP_DEFAULT_MTU;
    brb->IncomingMtuRange.Min = L2CAP_MIN_MTU;
    brb->OutMTU               = L2CAP_DEFAULT_MTU;
    brb->CallbackRoutine      = L2capSignalingReceiveCallback;
    brb->CallbackContext      = DevExt;

    NTSTATUS status = L2capSubmitBrb(DevExt, (PBRB)brb);
    if (NT_SUCCESS(status)) {
        DevExt->SignalingChannelHandle = brb->ChannelHandle;
        DevExt->Avdtp.State = AvdtpStateConnecting;
        KdPrint(("OpenWinBlue: signaling channel opened, handle=0x%p\n",
                 (PVOID)brb->ChannelHandle));
        // Kick off AVDTP DISCOVER
        status = AvdtpConnect(DevExt);
    } else {
        KdPrint(("OpenWinBlue: BRB_L2CA_OPEN_CHANNEL failed 0x%x\n", status));
    }

    DevExt->BthInterface.BthFreeBrb((PBRB)brb);
    return status;
}

// ── L2CAP media channel open (after AVDTP OPEN) ───────────────────────────────
NTSTATUS L2capOpenMediaChannel(_In_ POWB_DEVICE_EXTENSION DevExt)
{
    if (!DevExt->BthInterface.BthAllocateBrb)
        return STATUS_DEVICE_NOT_READY;

    struct _BRB_L2CA_OPEN_CHANNEL* brb =
        (struct _BRB_L2CA_OPEN_CHANNEL*)
        DevExt->BthInterface.BthAllocateBrb(BRB_L2CA_OPEN_CHANNEL, 'OWBM');
    if (!brb) return STATUS_INSUFFICIENT_RESOURCES;

    brb->BtAddress            = DevExt->RemoteBtAddress;
    brb->Psm                  = AVDTP_SIGNALING_PSM;
    brb->ChannelFlags         = CF_LINK_ENCRYPTED | CF_LINK_AUTHENTICATED;
    brb->IncomingMtuRange.Max = L2CAP_DEFAULT_MTU;
    brb->IncomingMtuRange.Min = L2CAP_MIN_MTU;
    brb->OutMTU               = L2CAP_DEFAULT_MTU;
    brb->CallbackRoutine      = NULL;   // A2DP source: send-only
    brb->CallbackContext      = NULL;

    NTSTATUS status = L2capSubmitBrb(DevExt, (PBRB)brb);
    if (NT_SUCCESS(status)) {
        DevExt->MediaChannelHandle = brb->ChannelHandle;
        KdPrint(("OpenWinBlue: media channel opened, handle=0x%p\n",
                 (PVOID)brb->ChannelHandle));
    } else {
        KdPrint(("OpenWinBlue: media channel open failed 0x%x\n", status));
    }

    DevExt->BthInterface.BthFreeBrb((PBRB)brb);
    return status;
}

// ── L2CAP signaling send ──────────────────────────────────────────────────────
NTSTATUS L2capSendSignaling(
    _In_ POWB_DEVICE_EXTENSION DevExt,
    _In_reads_bytes_(Length) const UCHAR* Data,
    _In_ USHORT Length)
{
    if (!DevExt->BthInterface.BthAllocateBrb)
        return STATUS_DEVICE_NOT_READY;
    if (DevExt->SignalingChannelHandle == 0)
        return STATUS_DEVICE_NOT_CONNECTED;

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

    NTSTATUS status = L2capSubmitBrb(DevExt, (PBRB)brb);
    if (!NT_SUCCESS(status)) {
        KdPrint(("OpenWinBlue: L2capSendSignaling failed 0x%x len=%u\n",
                 status, (ULONG)Length));
    }
    DevExt->BthInterface.BthFreeBrb((PBRB)brb);
    return status;
}

// ── L2CAP media frame send ────────────────────────────────────────────────────
NTSTATUS L2capSendMediaFrame(
    _In_ POWB_DEVICE_EXTENSION DevExt,
    _In_ ULONG  CodecId,
    _In_reads_bytes_(FrameLen) const UCHAR* FrameData,
    _In_ USHORT FrameLen)
{
    if (DevExt->Avdtp.State != AvdtpStateStreaming)
        return STATUS_DEVICE_NOT_CONNECTED;
    if (!DevExt->BthInterface.BthAllocateBrb)
        return STATUS_DEVICE_NOT_READY;

    UNREFERENCED_PARAMETER(CodecId);

    ULONG pkt_len_u = sizeof(OWB_RTP_HEADER) + 1u + (ULONG)FrameLen;
    if (pkt_len_u > 0xFFFFu) return STATUS_INVALID_PARAMETER;
    const USHORT pkt_len = (USHORT)pkt_len_u;

    PUCHAR pkt = (PUCHAR)ExAllocatePool2(POOL_FLAG_NON_PAGED, (SIZE_T)pkt_len, 'RTPM');
    if (!pkt) return STATUS_INSUFFICIENT_RESOURCES;

    OWB_RTP_HEADER* hdr = (OWB_RTP_HEADER*)pkt;
    hdr->vpxcc     = 0x80u;
    hdr->mpt       = 0x60u;
    hdr->seq_num   = RtlUshortByteSwap(DevExt->RtpSeqNum++);
    hdr->timestamp = RtlUlongByteSwap(DevExt->RtpTimestamp);
    hdr->ssrc      = RtlUlongByteSwap(0x00000001UL);
    DevExt->RtpTimestamp += 128u;  // blocks(16) × subbands(8) = 128 PCM samples/frame

    pkt[sizeof(OWB_RTP_HEADER)] = 0x01u;  // SBC: 1 frame, no fragmentation
    RtlCopyMemory(pkt + sizeof(OWB_RTP_HEADER) + 1u, FrameData, FrameLen);

    struct _BRB_L2CA_ACL_TRANSFER* brb =
        (struct _BRB_L2CA_ACL_TRANSFER*)
        DevExt->BthInterface.BthAllocateBrb(BRB_L2CA_ACL_TRANSFER, 'RTPM');
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

    NTSTATUS status = L2capSubmitBrb(DevExt, (PBRB)brb);
    DevExt->BthInterface.BthFreeBrb((PBRB)brb);
    ExFreePoolWithTag(pkt, 'RTPM');
    return status;
}

// ── RSSI query ────────────────────────────────────────────────────────────────
NTSTATUS L2capGetRssi(
    _In_  POWB_DEVICE_EXTENSION DevExt,
    _Out_ LONG*                 RssiDbm)
{
    *RssiDbm = -60L;  // safe default (medium signal strength)
    // TODO(phase3): submit BRB_HCI_GET_LINK_QUALITY for real RSSI.
    UNREFERENCED_PARAMETER(DevExt);
    return STATUS_SUCCESS;
}
