// driver/src/l2cap_stream.h
// L2CAP channel management and media frame transmission — Phase 2c real BRB.
#pragma once
#include <ntddk.h>
#include <wdf.h>
#include <initguid.h>
#include <bthdef.h>
#include <bthguid.h>
#include <bthioctl.h>
#include <bthddi.h>

// L2CAP MTU constants (defined in bthddi.h; add fallbacks for older WDK)
#ifndef L2CAP_DEFAULT_MTU
#  define L2CAP_DEFAULT_MTU 672u
#endif
#ifndef L2CAP_MIN_MTU
#  define L2CAP_MIN_MTU 48u
#endif

typedef struct _OWB_DEVICE_EXTENSION OWB_DEVICE_EXTENSION, *POWB_DEVICE_EXTENSION;

// Submit a BRB synchronously to BthPort.
// Must be called at PASSIVE_LEVEL only (connection setup, not streaming).
NTSTATUS L2capSubmitBrb(
    _In_ POWB_DEVICE_EXTENSION DevExt,
    _In_ PBRB                  Brb,
    _In_ ULONG                 BrbSize);

// Open the AVDTP signaling L2CAP channel (PSM 0x0019) to the remote device.
// Returns STATUS_SUCCESS on success, STATUS_DEVICE_NOT_READY if BthInterface
// not yet acquired, STATUS_DEVICE_NOT_CONNECTED if BT address unknown.
NTSTATUS L2capOpenSignalingChannel(_In_ POWB_DEVICE_EXTENSION DevExt);

// Open the A2DP media L2CAP channel (called after AVDTP OPEN response).
NTSTATUS L2capOpenMediaChannel(_In_ POWB_DEVICE_EXTENSION DevExt);

// Send a packet on the AVDTP signaling L2CAP channel.
NTSTATUS L2capSendSignaling(
    _In_ POWB_DEVICE_EXTENSION DevExt,
    _In_reads_bytes_(Length) const UCHAR* Data,
    _In_ USHORT Length);

// Send one RTP-framed audio packet on the A2DP media L2CAP channel.
NTSTATUS L2capSendMediaFrame(
    _In_ POWB_DEVICE_EXTENSION DevExt,
    _In_ ULONG  CodecId,
    _In_reads_bytes_(FrameLen) const UCHAR* FrameData,
    _In_ USHORT FrameLen);

// Query current RSSI. Falls back to -60 dBm stub if BRB not yet available.
NTSTATUS L2capGetRssi(
    _In_  POWB_DEVICE_EXTENSION DevExt,
    _Out_ LONG*                 RssiDbm);

// L2CAP indication callback — registered as Callback on BRB_L2CA_OPEN_CHANNEL.
// Called at DISPATCH_LEVEL. Matches PFNBTHPORT_INDICATION_CALLBACK signature.
// On IndicationRecvPacket: stores length and enqueues work item for PASSIVE read.
_IRQL_requires_max_(DISPATCH_LEVEL)
VOID L2capSignalingIndicationCallback(
    _In_opt_ PVOID                   Context,
    _In_     INDICATION_CODE         Indication,
    _In_     PINDICATION_PARAMETERS  Parameters);
