// driver/src/l2cap_stream.h
// L2CAP channel management and media frame transmission.
#pragma once
#include <ntddk.h>
#include <wdf.h>

typedef struct _OWB_DEVICE_EXTENSION OWB_DEVICE_EXTENSION, *POWB_DEVICE_EXTENSION;

// Send a packet on the AVDTP signaling L2CAP channel.
NTSTATUS L2capSendSignaling(
    _In_ POWB_DEVICE_EXTENSION DevExt,
    _In_reads_bytes_(Length) const UCHAR* Data,
    _In_ USHORT Length);

// Send one RTP-framed audio packet on the media L2CAP channel.
NTSTATUS L2capSendMediaFrame(
    _In_ POWB_DEVICE_EXTENSION DevExt,
    _In_ ULONG  CodecId,
    _In_reads_bytes_(FrameLen) const UCHAR* FrameData,
    _In_ USHORT FrameLen);

// Open the L2CAP signaling channel to a remote device.
// Stub in Phase 2b — full BRB flow in Phase 2c.
NTSTATUS L2capOpenSignalingChannel(_In_ POWB_DEVICE_EXTENSION DevExt);
