// driver/src/l2cap_stream.c
// L2CAP channel management and media frame sending (Phase 2b stubs).
#include "l2cap_stream.h"
#include "avdtp.h"
#include "owb_a2dp.h"

// RTP header for A2DP media packets (12 bytes, RFC 3550).
#pragma pack(push, 1)
typedef struct _OWB_RTP_HEADER {
    UCHAR   vpxcc;     // V=2, P=0, X=0, CC=0 → 0x80
    UCHAR   mpt;       // M=0, PT=96 (0x60) for SBC
    USHORT  seq_num;   // big-endian, incremented per packet
    ULONG   timestamp; // big-endian, sample clock units
    ULONG   ssrc;      // synchronization source (constant)
} OWB_RTP_HEADER;
#pragma pack(pop)

// Phase 2b stubs: signaling channel handle not yet wired to BthPort BRBs.
// The L2CAP BRB connection flow is implemented in Phase 2c.

NTSTATUS L2capSendSignaling(
    _In_ POWB_DEVICE_EXTENSION DevExt,
    _In_reads_bytes_(Length) const UCHAR* Data,
    _In_ USHORT Length)
{
    // Phase 2b stub: log and succeed so AVDTP state machine progresses
    // on simulated responses during unit testing.
    UNREFERENCED_PARAMETER(DevExt);
    UNREFERENCED_PARAMETER(Data);
    UNREFERENCED_PARAMETER(Length);
    KdPrint(("OpenWinBlue: L2capSendSignaling len=%u (stub)\n", (ULONG)Length));
    return STATUS_SUCCESS;
}

NTSTATUS L2capSendMediaFrame(
    _In_ POWB_DEVICE_EXTENSION DevExt,
    _In_ ULONG  CodecId,
    _In_reads_bytes_(FrameLen) const UCHAR* FrameData,
    _In_ USHORT FrameLen)
{
    if (DevExt->Avdtp.State != AvdtpStateStreaming)
        return STATUS_DEVICE_NOT_CONNECTED;

    UNREFERENCED_PARAMETER(CodecId);

    // Build RTP + SBC payload header + frame data into one buffer.
    // Compute in ULONG to avoid USHORT overflow before checking the bound.
    ULONG pkt_len_u = sizeof(OWB_RTP_HEADER) + 1u + (ULONG)FrameLen;
    if (pkt_len_u > 0xFFFFu) return STATUS_INVALID_PARAMETER;
    const USHORT pkt_len = (USHORT)pkt_len_u;
    PUCHAR pkt = (PUCHAR)ExAllocatePool2(POOL_FLAG_NON_PAGED,
                                          (SIZE_T)pkt_len, 'RTPM');
    if (!pkt) return STATUS_INSUFFICIENT_RESOURCES;

    OWB_RTP_HEADER* hdr = (OWB_RTP_HEADER*)pkt;
    hdr->vpxcc    = 0x80u;
    hdr->mpt      = 0x60u;   // PT=96 for SBC
    hdr->seq_num  = RtlUshortByteSwap(DevExt->RtpSeqNum++);
    hdr->timestamp = RtlUlongByteSwap(DevExt->RtpTimestamp);
    hdr->ssrc     = 0x00000001UL;
    DevExt->RtpTimestamp += 128u;  // blocks(16) × subbands(8) = 128 PCM samples/frame

    pkt[sizeof(OWB_RTP_HEADER)] = 0x01u;  // SBC payload hdr: 1 frame, no fragmentation
    RtlCopyMemory(pkt + sizeof(OWB_RTP_HEADER) + 1u, FrameData, FrameLen);

    // Phase 2b: send via signaling path (media channel not yet separately opened).
    NTSTATUS status = L2capSendSignaling(DevExt, pkt, pkt_len);
    ExFreePoolWithTag(pkt, 'RTPM');
    return status;
}

NTSTATUS L2capOpenSignalingChannel(_In_ POWB_DEVICE_EXTENSION DevExt) {
    UNREFERENCED_PARAMETER(DevExt);
    // Phase 2c: open L2CAP channel via BRB_L2CA_OPEN_CHANNEL BRB.
    KdPrint(("OpenWinBlue: L2capOpenSignalingChannel stub (Phase 2c)\n"));
    return STATUS_NOT_SUPPORTED;
}
