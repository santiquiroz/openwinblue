// driver/src/avdtp.h
// AVDTP signaling state machine for the A2DP profile.
#pragma once
#include <ntddk.h>
#include <wdf.h>

// AVDTP L2CAP PSM (Protocol Service Multiplexer)
#define AVDTP_SIGNALING_PSM    0x0019u
// Media transport L2CAP also uses PSM 0x0019 per AVDTP spec §7.
// The L2CAP stack assigns a distinct CID for each channel.
#define AVDTP_MEDIA_PSM  AVDTP_SIGNALING_PSM

// AVDTP signal identifiers (A2DP spec section 8.5)
#define AVDTP_MSG_DISCOVER          0x01u
#define AVDTP_MSG_GET_CAPABILITIES  0x02u
#define AVDTP_MSG_SET_CONFIGURATION 0x03u
#define AVDTP_MSG_OPEN              0x06u
#define AVDTP_MSG_START             0x07u
#define AVDTP_MSG_CLOSE             0x08u
#define AVDTP_MSG_SUSPEND           0x09u
#define AVDTP_MSG_ABORT             0x0Au

// Packet type bits (2-bit field in AVDTP header byte 0)
#define AVDTP_PKT_SINGLE            0x00u

// Message type bits (2-bit field in AVDTP header byte 0)
#define AVDTP_MSG_CMD               0x00u
#define AVDTP_MSG_RESPONSE_ACCEPT   0x02u
#define AVDTP_MSG_RESPONSE_REJECT   0x03u

// AVDTP connection states
typedef enum _OWB_AVDTP_STATE {
    AvdtpStateIdle        = 0,
    AvdtpStateConnecting  = 1,
    AvdtpStateDiscovering = 2,
    AvdtpStateConfiguring = 3,
    AvdtpStateConfigured  = 4,
    AvdtpStateOpen        = 5,
    AvdtpStateStreaming   = 6,
    AvdtpStateClosing     = 7,
} OWB_AVDTP_STATE;

// Per-device AVDTP context stored in OWB_DEVICE_EXTENSION
typedef struct _OWB_AVDTP_CONTEXT {
    OWB_AVDTP_STATE   State;
    USHORT            SignalingCid;    // L2CAP signaling channel ID
    USHORT            MediaCid;        // L2CAP media channel ID
    UCHAR             RemoteSeid;      // remote Stream End-Point ID
    UCHAR             LocalSeid;       // our SEID (fixed: 0x01)
    UCHAR             TransactionId;   // rolling 0-15 counter
    ULONG             ActiveCodecId;   // OWB_CODEC_* of negotiated codec
} OWB_AVDTP_CONTEXT, *POWB_AVDTP_CONTEXT;

// Forward declaration — full definition in owb_a2dp.h
typedef struct _OWB_DEVICE_EXTENSION OWB_DEVICE_EXTENSION, *POWB_DEVICE_EXTENSION;

// Initialize AVDTP context to idle state.
VOID AvdtpContextInit(_Out_ POWB_AVDTP_CONTEXT Ctx);

// Send an AVDTP command on the signaling channel.
NTSTATUS AvdtpSendCommand(
    _In_ POWB_DEVICE_EXTENSION DevExt,
    _In_ UCHAR  SignalId,
    _In_reads_bytes_opt_(PayloadLen) const UCHAR* Payload,
    _In_ USHORT PayloadLen);

// Start AVDTP discovery — transitions from Idle to Discovering.
NTSTATUS AvdtpConnect(_In_ POWB_DEVICE_EXTENSION DevExt);

// Update the preferred codec and trigger AVDTP reconfiguration (SUSPEND→SET_CONFIG→START).
// Call from PASSIVE_LEVEL (IOCTL dispatch context).
NTSTATUS AvdtpSetPreferredCodec(
    _In_ POWB_DEVICE_EXTENSION DevExt,
    _In_ ULONG                 NewCodecId);

// Process an inbound AVDTP signaling packet.
VOID AvdtpHandleSignalingPacket(
    _In_    POWB_DEVICE_EXTENSION DevExt,
    _In_reads_bytes_(Length) const UCHAR* Data,
    _In_    USHORT Length);
