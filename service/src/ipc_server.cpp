// service/src/ipc_server.cpp
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winioctl.h>

#include "ipc_server.h"
#include "ipc_protocol.h"
#include "a2dp_stream.h"
#include "owb_ioctl.h"
#include <cstring>
#include <string_view>

namespace owb {

struct IpcServer::Impl {
    HANDLE      pipe    = INVALID_HANDLE_VALUE;
    bool        running = false;
    A2dpStream* stream_ = nullptr;
};

IpcServer::IpcServer(A2dpStream* stream) : impl_(std::make_unique<Impl>()) { impl_->stream_ = stream; }

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

    // Block until a client connects
    BOOL connected = ConnectNamedPipe(impl_->pipe, nullptr);
    if (!connected && GetLastError() != ERROR_PIPE_CONNECTED)
        return false;

    // Message loop for this client connection
    bool client_done = false;
    while (!client_done && impl_->running) {
        ipc::MsgHeader hdr{};
        DWORD bytes_read = 0;
        BOOL ok = ReadFile(impl_->pipe, &hdr, sizeof(hdr), &bytes_read, nullptr);

        if (!ok || bytes_read < sizeof(hdr)) break;

        switch (hdr.type) {
            case ipc::MsgType::Ping: {
                ipc::MsgHeader pong{ ipc::MsgType::Pong, 0 };
                DWORD written = 0;
                if (!WriteFile(impl_->pipe, &pong, sizeof(pong), &written, nullptr)
                        || written != sizeof(pong)) {
                    client_done = true;
                }
                client_done = true;
                break;
            }
            case ipc::MsgType::GetStatus: {
                // TODO(phase2c): wire real state from AudioCapture + HfpGuard
                ipc::MsgHeader reply{ ipc::MsgType::StatusReply,
                                      sizeof(ipc::StatusPayload) };
                ipc::StatusPayload status{};

                if (impl_->stream_ && impl_->stream_->is_open()) {
                    OWB_DEVICE_STATE devState{};
                    if (impl_->stream_->get_device_state(&devState)) {
                        status.is_capturing = (devState.state == OWB_STATE_STREAMING) ? 1u : 0u;
                        status.bitrate = (devState.state == OWB_STATE_STREAMING) ? 328000u : 0u;
                        static const char* kNames[] = {"SBC","LDAC","aptX","aptX-HD","AAC","LC3"};
                        if (devState.active_codec_id < 6u)
                            strncpy_s(status.codec_name, sizeof(status.codec_name),
                                      kNames[devState.active_codec_id], _TRUNCATE);
                    } else {
                        strncpy_s(status.codec_name, sizeof(status.codec_name), "SBC", _TRUNCATE);
                    }
                } else {
                    strncpy_s(status.codec_name, sizeof(status.codec_name), "SBC", _TRUNCATE);
                }

                DWORD written = 0;
                BOOL hdr_ok = WriteFile(impl_->pipe, &reply, sizeof(reply), &written, nullptr);
                if (hdr_ok && written == sizeof(reply)) {
                    WriteFile(impl_->pipe, &status, sizeof(status), &written, nullptr);
                }
                client_done = true;
                break;
            }
            case ipc::MsgType::SetCodec: {
                // Read SetCodecPayload (codec_name + param_key + param_value)
                if (hdr.payload_len < sizeof(ipc::SetCodecPayload)) {
                    client_done = true;
                    break;
                }
                ipc::SetCodecPayload codec_payload{};
                DWORD payload_read = 0;
                ReadFile(impl_->pipe, &codec_payload,
                         sizeof(codec_payload), &payload_read, nullptr);

                // Resolve codec name string → OWB_CODEC_* ID
                uint32_t codec_id = OWB_CODEC_SBC;  // default
                if      (std::strncmp(codec_payload.codec_name, "LDAC",    4) == 0) codec_id = OWB_CODEC_LDAC;
                else if (std::strncmp(codec_payload.codec_name, "aptX-HD", 7) == 0) codec_id = OWB_CODEC_APTXHD;
                else if (std::strncmp(codec_payload.codec_name, "aptX",    4) == 0) codec_id = OWB_CODEC_APTX;

                // Forward to driver via A2dpStream
                bool success = false;
                if (impl_->stream_) {
                    const size_t key_len = strnlen(codec_payload.param_key,
                                                   sizeof(codec_payload.param_key));
                    success = impl_->stream_->set_codec_config(
                        codec_id,
                        std::string_view(codec_payload.param_key, key_len),
                        codec_payload.param_value);
                }

                // Reply with CodecAck
                ipc::MsgHeader ack{ ipc::MsgType::CodecAck, sizeof(ipc::AckPayload) };
                ipc::AckPayload ack_payload{ success ? uint8_t{1} : uint8_t{0}, {0u, 0u, 0u} };
                DWORD written = 0;
                BOOL ack_ok = WriteFile(impl_->pipe, &ack, sizeof(ack), &written, nullptr);
                if (ack_ok && written == sizeof(ack))
                    WriteFile(impl_->pipe, &ack_payload, sizeof(ack_payload), &written, nullptr);
                client_done = true;
                break;
            }
            default:
                client_done = true;
                break;
        }
    }

    DisconnectNamedPipe(impl_->pipe);
    return true;
}

} // namespace owb
