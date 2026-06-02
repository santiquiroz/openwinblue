// service/src/ipc_server.cpp
#include "ipc_server.h"
#include "ipc_protocol.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstring>

namespace owb {

struct IpcServer::Impl {
    HANDLE pipe    = INVALID_HANDLE_VALUE;
    bool   running = false;
};

IpcServer::IpcServer() : impl_(std::make_unique<Impl>()) {}

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
                WriteFile(impl_->pipe, &pong, sizeof(pong), &written, nullptr);
                client_done = true;
                break;
            }
            case ipc::MsgType::GetStatus: {
                ipc::MsgHeader reply{ ipc::MsgType::StatusReply,
                                      sizeof(ipc::StatusPayload) };
                ipc::StatusPayload status{};
                strncpy_s(status.codec_name, sizeof(status.codec_name), "SBC", _TRUNCATE);
                status.bitrate      = 328000;
                status.is_capturing = 0;
                status.hfp_guard_on = 0;

                DWORD written = 0;
                WriteFile(impl_->pipe, &reply,  sizeof(reply),  &written, nullptr);
                WriteFile(impl_->pipe, &status, sizeof(status), &written, nullptr);
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
