// driver/owb_ioctl.h
// IOCTL interface between owb_a2dp.sys (kernel) and owb-service.exe (user-mode).
// Plain C header — compilable in both kernel and user-mode contexts.
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif

// CTL_CODE is defined in winioctl.h (user) and wdm.h (kernel).
// Both paths provide it; include guards prevent double-inclusion.
#ifndef CTL_CODE
#  include <winioctl.h>
#endif

// offsetof is needed for OWB_SEND_FRAME_INPUT_SIZE.
// In user mode: stddef.h; in kernel mode: ntddk.h provides it as a compiler intrinsic.
#ifndef offsetof
#  include <stddef.h>
#endif

// Custom device type for OpenWinBlue IOCTLs (0x8000–0xFFFF = vendor range)
#define OWB_DEVICE_TYPE  0x8000u

// ─── IOCTL codes ──────────────────────────────────────────────────────────────

// Send one encoded audio frame to transmit over Bluetooth A2DP.
// Input:  OWB_SEND_FRAME_INPUT  (variable-length — use OWB_SEND_FRAME_INPUT_SIZE)
// Output: none
#define OWB_IOCTL_SEND_AUDIO_FRAME \
    CTL_CODE(OWB_DEVICE_TYPE, 0x800, METHOD_BUFFERED, FILE_WRITE_DATA)

// Query RF link quality for adaptive bitrate decisions.
// Input:  none
// Output: OWB_RF_QUALITY
#define OWB_IOCTL_GET_RF_QUALITY \
    CTL_CODE(OWB_DEVICE_TYPE, 0x801, METHOD_BUFFERED, FILE_READ_DATA)

// Push a codec parameter to the driver (triggers AVDTP SET_CONFIGURATION).
// Input:  OWB_CODEC_CONFIG
// Output: none
#define OWB_IOCTL_SET_CODEC_CONFIG \
    CTL_CODE(OWB_DEVICE_TYPE, 0x802, METHOD_BUFFERED, FILE_WRITE_DATA)

// Query the A2DP connection state.
// Input:  none
// Output: OWB_DEVICE_STATE
#define OWB_IOCTL_GET_DEVICE_STATE \
    CTL_CODE(OWB_DEVICE_TYPE, 0x803, METHOD_BUFFERED, FILE_READ_DATA)

// ─── Payload structures ───────────────────────────────────────────────────────

#pragma pack(push, 1)

// Codec IDs — matches owb::ipc::SetCodecPayload.codec_name convention
#define OWB_CODEC_SBC    0u
#define OWB_CODEC_LDAC   1u
#define OWB_CODEC_APTX   2u
#define OWB_CODEC_APTXHD 3u
#define OWB_CODEC_AAC              4u
#define OWB_CODEC_LC3              5u
#define OWB_CODEC_APTX_ADAPTIVE    6u

// Input for OWB_IOCTL_SEND_AUDIO_FRAME.
// data[] holds exactly data_len bytes of encoded audio (SBC frame, LDAC frame, etc.)
typedef struct _OWB_SEND_FRAME_INPUT {
    unsigned long  codec_id;   // OWB_CODEC_*
    unsigned long  data_len;   // byte count of data[] — driver MUST validate: data_len <= (buffer_size - offsetof(data))
    unsigned char  data[1];    // flexible array — actual size = data_len
} OWB_SEND_FRAME_INPUT, *POWB_SEND_FRAME_INPUT;

// Bytes prepended before each encoded frame on the A2DP media channel:
// 12-byte RTP header (RFC 3550) + 1-byte SBC RTP payload header (A2DP spec 4.3.1).
#define OWB_RTP_OVERHEAD 13u

// Compute the allocation size for a given frame length.
// Returns size_t. Kernel driver must validate result fits within the actual
// input buffer length before using it (data_len <= buffer_size - offsetof(data)).
#define OWB_SEND_FRAME_INPUT_SIZE(data_bytes) \
    (offsetof(OWB_SEND_FRAME_INPUT, data) + (data_bytes))

// Output for OWB_IOCTL_GET_RF_QUALITY.
typedef struct _OWB_RF_QUALITY {
    long           rssi_dbm;             // signal strength (negative dBm)
    unsigned long  retransmit_per_mille; // retransmissions per 1000 packets
    unsigned long  link_quality;         // Windows quality score 0–255
} OWB_RF_QUALITY, *POWB_RF_QUALITY;

// Input for OWB_IOCTL_SET_CODEC_CONFIG.
typedef struct _OWB_CODEC_CONFIG {
    unsigned long  codec_id;        // OWB_CODEC_*
    char           param_key[16];   // parameter name, e.g. "bitpool"
    long long      param_value;     // parameter value, e.g. 53
} OWB_CODEC_CONFIG, *POWB_CODEC_CONFIG;

// Output for OWB_IOCTL_GET_DEVICE_STATE.
#define OWB_STATE_DISCONNECTED 0u
#define OWB_STATE_CONNECTING   1u
#define OWB_STATE_CONNECTED    2u
#define OWB_STATE_STREAMING    3u

typedef struct _OWB_DEVICE_STATE {
    unsigned long  state;           // OWB_STATE_*
    unsigned long  active_codec_id; // OWB_CODEC_*
    unsigned char  remote_addr[6];  // Bluetooth address (big-endian, MSB first, per Bluetooth spec)
    unsigned char  _pad[2];
} OWB_DEVICE_STATE, *POWB_DEVICE_STATE;

#pragma pack(pop)
