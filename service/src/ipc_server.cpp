// service/src/ipc_server.cpp
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winioctl.h>

#include "ipc_server.h"
#include "ipc_protocol.h"
#include "a2dp_stream.h"
#include "codec_controller.h"
#include "owb_ioctl.h"
#include "../ai/ai_pipeline.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace owb {

namespace {

constexpr std::string_view kAiTarget       = "AI";
constexpr std::string_view kSwitchKey      = "switch";
constexpr std::string_view kBitpoolKey     = "bitpool";
constexpr std::string_view kDefaultCodec   = "SBC";
constexpr int64_t          kMinSbcBitpool  = 2;
constexpr int64_t          kMaxSbcBitpool  = 53;

struct CodecNameId {
    std::string_view name;
    uint32_t         id;
};

constexpr std::array<CodecNameId, 7> kCodecNames{{
    {"SBC",           OWB_CODEC_SBC},
    {"LDAC",          OWB_CODEC_LDAC},
    {"aptX",          OWB_CODEC_APTX},
    {"aptX-HD",       OWB_CODEC_APTXHD},
    {"AAC",           OWB_CODEC_AAC},
    {"LC3",           OWB_CODEC_LC3},
    {"aptX-Adaptive", OWB_CODEC_APTX_ADAPTIVE},
}};

std::optional<uint32_t> codec_id_from_name(std::string_view name) {
    for (const auto& entry : kCodecNames)
        if (entry.name == name) return entry.id;
    return std::nullopt;
}

std::string_view codec_name_from_id(uint32_t id) {
    for (const auto& entry : kCodecNames)
        if (entry.id == id) return entry.name;
    return {};
}

template <size_t N>
std::string_view bounded_string(const char (&field)[N]) {
    return std::string_view(field, strnlen(field, N));
}

template <size_t N>
void copy_name(char (&dst)[N], std::string_view name) {
    const size_t len = std::min(name.size(), N - 1);
    std::memcpy(dst, name.data(), len);
    dst[len] = '\0';
}

int64_t normalize_param(std::string_view key, int64_t value) {
    if (key == kBitpoolKey) return std::clamp(value, kMinSbcBitpool, kMaxSbcBitpool);
    return value;
}

// A message-mode pipe reports ERROR_MORE_DATA when a message is longer than
// the buffer; the remaining bytes stay readable, so that is not a failure.
bool read_exact(HANDLE pipe, void* buf, DWORD len) {
    auto* dst = static_cast<uint8_t*>(buf);
    DWORD total = 0;
    while (total < len) {
        DWORD got = 0;
        const BOOL ok = ReadFile(pipe, dst + total, len - total, &got, nullptr);
        if (!ok && GetLastError() != ERROR_MORE_DATA) return false;
        if (got == 0) return false;
        total += got;
    }
    return true;
}

bool write_all(HANDLE pipe, const void* buf, DWORD len) {
    DWORD written = 0;
    return WriteFile(pipe, buf, len, &written, nullptr) && written == len;
}

bool write_message(HANDLE pipe, ipc::MsgType type, const void* payload, uint16_t len) {
    const ipc::MsgHeader hdr{ type, len };
    if (!write_all(pipe, &hdr, sizeof(hdr))) return false;
    return len == 0 || write_all(pipe, payload, len);
}

bool write_ack(HANDLE pipe, bool success) {
    const ipc::AckPayload ack{ success ? uint8_t{1} : uint8_t{0}, {0u, 0u, 0u} };
    return write_message(pipe, ipc::MsgType::CodecAck, &ack, sizeof(ack));
}

bool connect_client(HANDLE pipe) {
    return ConnectNamedPipe(pipe, nullptr) || GetLastError() == ERROR_PIPE_CONNECTED;
}

} // namespace

struct IpcServer::Impl {
    HANDLE               pipe        = INVALID_HANDLE_VALUE;
    bool                 running     = false;
    A2dpStream*          stream_     = nullptr;
    owb::ai::AiPipeline* ai_         = nullptr;
    ICodecController*    controller_ = nullptr;

    bool serve_next_message() {
        ipc::MsgHeader hdr{};
        return read_exact(pipe, &hdr, sizeof(hdr)) && handle_message(hdr);
    }

    bool handle_message(const ipc::MsgHeader& hdr) {
        switch (hdr.type) {
            case ipc::MsgType::Ping:      return write_message(pipe, ipc::MsgType::Pong, nullptr, 0);
            case ipc::MsgType::GetStatus: return reply_status();
            case ipc::MsgType::SetCodec:  return handle_set_codec(hdr.payload_len);
            default:                      return false;
        }
    }

    bool reply_status() {
        const ipc::StatusPayload status = build_status();
        return write_message(pipe, ipc::MsgType::StatusReply, &status, sizeof(status));
    }

    ipc::StatusPayload build_status() const {
        ipc::StatusPayload status{};
        copy_name(status.codec_name, kDefaultCodec);
        // Prefer the live pipeline: it reflects what the service is encoding now.
        if (controller_) fill_status_from_controller(status);
        else             fill_status_from_driver(status);
        status.hfp_guard_on = 0u;  // reported by the GUI's Level-1 control
        return status;
    }

    void fill_status_from_controller(ipc::StatusPayload& status) const {
        const std::string name = controller_->codec_name();
        if (!name.empty()) copy_name(status.codec_name, name);
        status.is_capturing = controller_->is_streaming() ? 1u : 0u;
        status.bitrate      = controller_->bitrate();
    }

    void fill_status_from_driver(ipc::StatusPayload& status) const {
        if (!stream_ || !stream_->is_open()) return;
        OWB_DEVICE_STATE dev_state{};
        if (!stream_->get_device_state(&dev_state)) return;
        status.is_capturing = (dev_state.state == OWB_STATE_STREAMING) ? 1u : 0u;
        copy_name(status.codec_name, codec_name_from_id(dev_state.active_codec_id));
    }

    bool handle_set_codec(uint16_t payload_len) {
        std::vector<uint8_t> raw(payload_len);
        if (payload_len > 0 && !read_exact(pipe, raw.data(), payload_len)) return false;
        if (payload_len != sizeof(ipc::SetCodecPayload)) return write_ack(pipe, false);

        ipc::SetCodecPayload request{};
        std::memcpy(&request, raw.data(), sizeof(request));
        return write_ack(pipe, apply_request(request));
    }

    bool apply_request(const ipc::SetCodecPayload& request) {
        const std::string_view target = bounded_string(request.codec_name);
        const std::string_view key    = bounded_string(request.param_key);
        if (target == kAiTarget) return apply_ai_param(key, request.param_value);

        const std::optional<uint32_t> codec_id = codec_id_from_name(target);
        if (!codec_id) return false;
        if (key == kSwitchKey) return switch_codec(*codec_id, key, request.param_value);
        return set_codec_param(key, normalize_param(key, request.param_value));
    }

    bool apply_ai_param(std::string_view key, int64_t value) {
        if (!ai_) return false;
        ai_->set_param(key, value);
        return true;
    }

    // Switches the user-mode encoder; the driver negotiates the codec separately.
    bool switch_codec(uint32_t codec_id, std::string_view key, int64_t value) {
        if (controller_) controller_->set_codec_id(codec_id);
        if (stream_) return stream_->set_codec_config(codec_id, key, value);
        return controller_ != nullptr;
    }

    bool set_codec_param(std::string_view key, int64_t value) {
        return controller_ && controller_->set_codec_param(key, value);
    }
};

IpcServer::IpcServer(A2dpStream* stream, ai::AiPipeline* ai, ICodecController* controller)
    : impl_(std::make_unique<Impl>()) {
    impl_->stream_     = stream;
    impl_->ai_         = ai;
    impl_->controller_ = controller;
}

IpcServer::~IpcServer() { stop(); }

bool IpcServer::start() {
    if (impl_->running) return true;

    impl_->pipe = CreateNamedPipeW(
        ipc::kPipeName,
        PIPE_ACCESS_DUPLEX,
        PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
        PIPE_UNLIMITED_INSTANCES,
        4096, 4096,
        0, nullptr
    );
    if (impl_->pipe == INVALID_HANDLE_VALUE) return false;

    impl_->running = true;
    return true;
}

void IpcServer::stop() {
    if (!impl_->running) return;
    impl_->running = false;
    if (impl_->pipe != INVALID_HANDLE_VALUE) {
        CloseHandle(impl_->pipe);
        impl_->pipe = INVALID_HANDLE_VALUE;
    }
}

bool IpcServer::serve_one() {
    if (!impl_->running || impl_->pipe == INVALID_HANDLE_VALUE)
        return false;
    if (!connect_client(impl_->pipe))
        return false;

    // The session lasts until the client closes its end or sends garbage.
    while (impl_->running && impl_->serve_next_message()) {}

    DisconnectNamedPipe(impl_->pipe);
    return true;
}

} // namespace owb
