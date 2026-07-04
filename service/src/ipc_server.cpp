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
#include "owb_log.h"
#include <cstring>
#include <string>
#include <string_view>

namespace owb {

struct IpcServer::Impl {
    HANDLE               pipe        = INVALID_HANDLE_VALUE;
    bool                 running     = false;
    A2dpStream*          stream_     = nullptr;
    owb::ai::AiPipeline* ai_         = nullptr;
    ICodecController*    controller_ = nullptr;
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
    if (impl_->pipe == INVALID_HANDLE_VALUE) {
        OWB_LOG_ERROR("CreateNamedPipeW failed: %lu", GetLastError());
        return false;
    }

    impl_->running = true;
    OWB_LOG_INFO("IPC server started listening on \\\\.\\pipe\\openwinblue");
    return true;
}

void IpcServer::stop() {
    if (!impl_->running) return;
    OWB_LOG_INFO("IPC server stopping");
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

    OWB_LOG_DEBUG("IPC client connected");

    // Message loop for this client connection
    bool client_done = false;
    while (!client_done && impl_->running) {
        ipc::MsgHeader hdr{};
        DWORD bytes_read = 0;
        BOOL ok = ReadFile(impl_->pipe, &hdr, sizeof(hdr), &bytes_read, nullptr);

        if (!ok || bytes_read < sizeof(hdr)) break;

        switch (hdr.type) {
            case ipc::MsgType::Ping: {
                OWB_LOG_INFO("IPC message: Ping");
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
                OWB_LOG_INFO("IPC message: GetStatus");
                // TODO(phase2c): wire real state from AudioCapture + HfpGuard
                ipc::MsgHeader reply{ ipc::MsgType::StatusReply,
                                      sizeof(ipc::StatusPayload) };
                ipc::StatusPayload status{};

                // Prefer the live pipeline for codec/streaming state; it reflects
                // what the service is actually encoding right now.
                if (impl_->controller_) {
                    const std::string name = impl_->controller_->codec_name();
                    strncpy_s(status.codec_name, sizeof(status.codec_name),
                              name.empty() ? "SBC" : name.c_str(), _TRUNCATE);
                    status.is_capturing = impl_->controller_->is_streaming() ? 1u : 0u;
                } else if (impl_->stream_ && impl_->stream_->is_open()) {
                    OWB_DEVICE_STATE devState{};
                    if (impl_->stream_->get_device_state(&devState)) {
                        status.is_capturing = (devState.state == OWB_STATE_STREAMING) ? 1u : 0u;
                        static const char* kNames[] =
                            {"SBC","LDAC","aptX","aptX-HD","AAC","LC3","aptX-Adaptive"};
                        if (devState.active_codec_id < (sizeof(kNames)/sizeof(kNames[0])))
                            strncpy_s(status.codec_name, sizeof(status.codec_name),
                                      kNames[devState.active_codec_id], _TRUNCATE);
                    } else {
                        strncpy_s(status.codec_name, sizeof(status.codec_name), "SBC", _TRUNCATE);
                    }
                } else {
                    strncpy_s(status.codec_name, sizeof(status.codec_name), "SBC", _TRUNCATE);
                }
                status.hfp_guard_on = 0u;  // reported by the GUI's Level-1 control

                OWB_LOG_INFO("GetStatus reply: codec_name=%.32s, is_capturing=%u",
                             status.codec_name, status.is_capturing);

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
                    OWB_LOG_WARN("SetCodec payload too short");
                    client_done = true;
                    break;
                }
                ipc::SetCodecPayload codec_payload{};
                DWORD payload_read = 0;
                BOOL payload_ok = ReadFile(impl_->pipe, &codec_payload,
                         sizeof(codec_payload), &payload_read, nullptr);

                if (!payload_ok || payload_read < sizeof(codec_payload)) {
                    OWB_LOG_WARN("ReadFile SetCodec payload failed");
                    client_done = true;
                    break;
                }

                // "AI" codec name → route to AI pipeline, not the driver
                if (std::strncmp(codec_payload.codec_name, "AI", 2) == 0) {
                    const size_t klen = strnlen(codec_payload.param_key,
                                               sizeof(codec_payload.param_key));
                    OWB_LOG_INFO("IPC SetCodec: AI, param_key=%.32s, param_value=%lld",
                                 codec_payload.param_key, (long long)codec_payload.param_value);
                    
                    bool ai_success = false;
                    if (impl_->ai_) {
                        impl_->ai_->set_param(
                            std::string_view(codec_payload.param_key, klen),
                            codec_payload.param_value);
                        ai_success = true;
                    }
                    ipc::MsgHeader ai_ack{ ipc::MsgType::CodecAck, sizeof(ipc::AckPayload) };
                    ipc::AckPayload ai_ack_p{ ai_success ? uint8_t{1} : uint8_t{0}, {0u,0u,0u} };
                    DWORD aw = 0;
                    BOOL aok = WriteFile(impl_->pipe, &ai_ack, sizeof(ai_ack), &aw, nullptr);
                    if (aok && aw == sizeof(ai_ack))
                        WriteFile(impl_->pipe, &ai_ack_p, sizeof(ai_ack_p), &aw, nullptr);
                    client_done = true;
                    break;
                }

                // Resolve codec name string → OWB_CODEC_* ID
                uint32_t codec_id = OWB_CODEC_SBC;  // default
                if      (std::strncmp(codec_payload.codec_name, "LDAC",    4) == 0) codec_id = OWB_CODEC_LDAC;
                else if (std::strncmp(codec_payload.codec_name, "aptX-HD", 7) == 0) codec_id = OWB_CODEC_APTXHD;
                else if (std::strncmp(codec_payload.codec_name, "aptX",    4) == 0) codec_id = OWB_CODEC_APTX;

                const size_t key_len = strnlen(codec_payload.param_key,
                                              sizeof(codec_payload.param_key));
                OWB_LOG_INFO("IPC SetCodec: codec_name=%.32s, codec_id=%u, param_key=%.32s, param_value=%lld",
                             codec_payload.codec_name, codec_id,
                             codec_payload.param_key, (long long)codec_payload.param_value);

                // Switch the user-mode encoder so the service actually produces
                // frames in the requested codec (driver negotiation is separate).
                if (impl_->controller_)
                    impl_->controller_->set_codec_id(codec_id);

                // Forward to driver via A2dpStream
                bool success = false;
                if (impl_->stream_) {
                    success = impl_->stream_->set_codec_config(
                        codec_id,
                        std::string_view(codec_payload.param_key, key_len),
                        codec_payload.param_value);
                }
                if (impl_->controller_ && !impl_->stream_) success = true;

                OWB_LOG_INFO("SetCodec result: success=%d, codec_id=%u", success ? 1 : 0, codec_id);

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
