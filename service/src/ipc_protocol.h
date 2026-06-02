// service/src/ipc_protocol.h
//
// Binary IPC protocol for OpenWinBlue named pipe.
// All messages: [MsgHeader][payload bytes]
// All integers: little-endian.
#pragma once
#include <cstdint>

namespace owb::ipc {

// ─── Message types ────────────────────────────────────────────────────────────
enum class MsgType : uint16_t {
    Ping        = 0x0001,   // no payload
    Pong        = 0x0002,   // no payload
    GetStatus   = 0x0010,   // no payload
    StatusReply = 0x0011,   // payload: StatusPayload
    SetCodec    = 0x0020,   // payload: SetCodecPayload
    CodecAck    = 0x0021,   // payload: AckPayload
    Error       = 0x00FF,   // payload: ErrorPayload
};

// ─── Header (precedes every message) ─────────────────────────────────────────
#pragma pack(push, 1)
struct MsgHeader {
    MsgType  type;           // 2 bytes
    uint16_t payload_len;    // bytes that follow this header
};
static_assert(sizeof(MsgHeader) == 4);

// ─── Payloads ─────────────────────────────────────────────────────────────────
struct StatusPayload {
    char     codec_name[16]; // e.g. "SBC\0"
    uint32_t bitrate;        // effective bits/sec
    uint8_t  is_capturing;   // 0 or 1
    uint8_t  hfp_guard_on;   // 0 or 1
    uint8_t  _pad[2];
};
static_assert(sizeof(StatusPayload) == 24);

struct SetCodecPayload {
    char    codec_name[16];  // "SBC", "LDAC", "aptX", ...
    char    param_key[16];   // e.g. "bitpool"
    int64_t param_value;     // e.g. 53
};
static_assert(sizeof(SetCodecPayload) == 40);

struct AckPayload {
    uint8_t success;         // 1 = OK, 0 = rejected
    uint8_t _pad[3];
};
static_assert(sizeof(AckPayload) == 4);

struct ErrorPayload {
    char message[64];
};
static_assert(sizeof(ErrorPayload) == 64);
#pragma pack(pop)

inline constexpr wchar_t kPipeName[] = L"\\\\.\\pipe\\openwinblue";

} // namespace owb::ipc
