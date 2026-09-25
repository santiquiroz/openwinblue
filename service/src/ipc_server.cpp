// service/src/ipc_server.cpp
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winioctl.h>
#include <sddl.h>

#include "ipc_server.h"
#include "ipc_protocol.h"
#include "a2dp_stream.h"
#include "codec_controller.h"
#include "owb_ioctl.h"
#include "../ai/ai_pipeline.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
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

constexpr DWORD kPipeBufferSize = 4096;

// Every I/O is overlapped so a stop request can cancel it from another thread.
struct PipeIo {
    HANDLE pipe;
    HANDLE io_event;
    HANDLE stop_event;
};

OVERLAPPED new_overlapped(HANDLE io_event) {
    ResetEvent(io_event);
    OVERLAPPED ov{};
    ov.hEvent = io_event;
    return ov;
}

// A message-mode pipe reports ERROR_MORE_DATA when a message is longer than
// the buffer; the remaining bytes stay readable, so that is not a failure.
bool is_in_flight(BOOL started) {
    if (started) return true;
    const DWORD err = GetLastError();
    return err == ERROR_IO_PENDING || err == ERROR_MORE_DATA;
}

// On stop the I/O is cancelled and awaited so the OVERLAPPED never outlives it.
bool await_io(const PipeIo& io, OVERLAPPED& ov, DWORD& transferred) {
    const HANDLE handles[] = { io.io_event, io.stop_event };
    if (WaitForMultipleObjects(2, handles, FALSE, INFINITE) != WAIT_OBJECT_0) {
        CancelIoEx(io.pipe, &ov);
        GetOverlappedResult(io.pipe, &ov, &transferred, TRUE);
        return false;
    }
    return GetOverlappedResult(io.pipe, &ov, &transferred, FALSE)
        || GetLastError() == ERROR_MORE_DATA;
}

bool finish_io(const PipeIo& io, BOOL started, OVERLAPPED& ov, DWORD& transferred) {
    return is_in_flight(started) && await_io(io, ov, transferred);
}

DWORD read_some(const PipeIo& io, void* buf, DWORD len) {
    OVERLAPPED ov = new_overlapped(io.io_event);
    DWORD got = 0;
    const BOOL started = ReadFile(io.pipe, buf, len, nullptr, &ov);
    return finish_io(io, started, ov, got) ? got : 0;
}

bool read_exact(const PipeIo& io, void* buf, DWORD len) {
    auto* dst = static_cast<uint8_t*>(buf);
    DWORD total = 0;
    while (total < len) {
        const DWORD got = read_some(io, dst + total, len - total);
        if (got == 0) return false;
        total += got;
    }
    return true;
}

bool write_all(const PipeIo& io, const void* buf, DWORD len) {
    OVERLAPPED ov = new_overlapped(io.io_event);
    DWORD written = 0;
    const BOOL started = WriteFile(io.pipe, buf, len, nullptr, &ov);
    return finish_io(io, started, ov, written) && written == len;
}

bool write_message(const PipeIo& io, ipc::MsgType type, const void* payload, uint16_t len) {
    const ipc::MsgHeader hdr{ type, len };
    if (!write_all(io, &hdr, sizeof(hdr))) return false;
    return len == 0 || write_all(io, payload, len);
}

bool write_ack(const PipeIo& io, bool success) {
    const ipc::AckPayload ack{ success ? uint8_t{1} : uint8_t{0}, {0u, 0u, 0u} };
    return write_message(io, ipc::MsgType::CodecAck, &ack, sizeof(ack));
}

bool connect_client(const PipeIo& io) {
    OVERLAPPED ov = new_overlapped(io.io_event);
    if (ConnectNamedPipe(io.pipe, &ov) || GetLastError() == ERROR_PIPE_CONNECTED) return true;
    if (GetLastError() != ERROR_IO_PENDING) return false;
    DWORD unused = 0;
    return await_io(io, ov, unused);
}

// FILE_FLAG_FIRST_PIPE_INSTANCE makes creation fail if anyone already owns the
// name, so a squatter cannot impersonate the service to the elevated GUI.
HANDLE create_secured_pipe(const IpcPipeConfig& config) {
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            config.sddl.c_str(), SDDL_REVISION_1, &descriptor, nullptr))
        return INVALID_HANDLE_VALUE;

    SECURITY_ATTRIBUTES attributes{ sizeof(attributes), descriptor, FALSE };
    const HANDLE pipe = CreateNamedPipeW(
        config.name.c_str(),
        PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
        PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
        PIPE_UNLIMITED_INSTANCES,
        kPipeBufferSize, kPipeBufferSize,
        0, &attributes
    );
    LocalFree(descriptor);
    return pipe;
}

void close_handle(HANDLE& handle, HANDLE invalid) {
    if (handle == invalid) return;
    CloseHandle(handle);
    handle = invalid;
}

} // namespace

struct IpcServer::Impl {
    HANDLE                  pipe        = INVALID_HANDLE_VALUE;
    HANDLE                  io_event    = nullptr;
    HANDLE                  stop_event  = nullptr;
    std::atomic<bool>       running{false};
    std::mutex              serve_mtx;
    std::condition_variable serve_idle;
    bool                    serving     = false;
    IpcPipeConfig           config_;
    A2dpStream*             stream_     = nullptr;
    owb::ai::AiPipeline*    ai_         = nullptr;
    ICodecController*       controller_ = nullptr;

    class ServeScope {
    public:
        explicit ServeScope(Impl& owner) : impl_(owner), entered_(owner.enter_serve()) {}
        ~ServeScope() { if (entered_) impl_.leave_serve(); }
        ServeScope(const ServeScope&)            = delete;
        ServeScope& operator=(const ServeScope&) = delete;

        bool entered() const { return entered_; }

    private:
        Impl&      impl_;
        const bool entered_;
    };

    PipeIo io() const { return { pipe, io_event, stop_event }; }

    bool open_pipe() {
        stop_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        io_event   = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        pipe       = create_secured_pipe(config_);
        return stop_event && io_event && pipe != INVALID_HANDLE_VALUE;
    }

    void close_handles() {
        close_handle(pipe, INVALID_HANDLE_VALUE);
        close_handle(io_event, nullptr);
        close_handle(stop_event, nullptr);
    }

    // stop() clears running before taking serve_mtx, so a serve_one() that
    // enters later bails out instead of using handles that are being closed.
    bool enter_serve() {
        std::lock_guard lock(serve_mtx);
        if (!running) return false;
        serving = true;
        return true;
    }

    void leave_serve() {
        {
            std::lock_guard lock(serve_mtx);
            serving = false;
        }
        serve_idle.notify_all();
    }

    void wait_until_idle() {
        std::unique_lock lock(serve_mtx);
        serve_idle.wait(lock, [this] { return !serving; });
    }

    void serve_session() {
        while (running && serve_next_message()) {}
        DisconnectNamedPipe(pipe);
    }

    bool serve_next_message() {
        ipc::MsgHeader hdr{};
        return read_exact(io(), &hdr, sizeof(hdr)) && handle_message(hdr);
    }

    bool handle_message(const ipc::MsgHeader& hdr) {
        switch (hdr.type) {
            case ipc::MsgType::Ping:      return write_message(io(), ipc::MsgType::Pong, nullptr, 0);
            case ipc::MsgType::GetStatus: return reply_status();
            case ipc::MsgType::SetCodec:  return handle_set_codec(hdr.payload_len);
            default:                      return false;
        }
    }

    bool reply_status() {
        const ipc::StatusPayload status = build_status();
        return write_message(io(), ipc::MsgType::StatusReply, &status, sizeof(status));
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
        if (payload_len > 0 && !read_exact(io(), raw.data(), payload_len)) return false;
        if (payload_len != sizeof(ipc::SetCodecPayload)) return write_ack(io(), false);

        ipc::SetCodecPayload request{};
        std::memcpy(&request, raw.data(), sizeof(request));
        return write_ack(io(), apply_request(request));
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

IpcServer::IpcServer(A2dpStream* stream, ai::AiPipeline* ai, ICodecController* controller,
                     IpcPipeConfig config)
    : impl_(std::make_unique<Impl>()) {
    impl_->stream_     = stream;
    impl_->ai_         = ai;
    impl_->controller_ = controller;
    impl_->config_     = std::move(config);
}

IpcServer::~IpcServer() { stop(); }

bool IpcServer::start() {
    if (impl_->running) return true;
    if (!impl_->open_pipe()) {
        impl_->close_handles();
        return false;
    }
    impl_->running = true;
    return true;
}

void IpcServer::stop() {
    if (!impl_->running.exchange(false)) return;
    SetEvent(impl_->stop_event);
    impl_->wait_until_idle();
    impl_->close_handles();
}

bool IpcServer::serve_one() {
    const Impl::ServeScope scope(*impl_);
    if (!scope.entered() || !connect_client(impl_->io()))
        return false;

    // The session lasts until the client closes its end, sends garbage or stop() is called.
    impl_->serve_session();
    return true;
}

} // namespace owb
