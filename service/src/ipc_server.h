// service/src/ipc_server.h
#pragma once
#include "ipc_protocol.h"
#include <memory>
#include <string>

namespace owb { class A2dpStream; class ICodecController; }
namespace owb::ai { class AiPipeline; }

namespace owb {

// Only LocalSystem (the service account) and elevated administrators (the
// requireAdministrator GUI) may open the pipe.
inline constexpr wchar_t kIpcPipeSddl[] = L"D:P(A;;GA;;;SY)(A;;GA;;;BA)";

struct IpcPipeConfig {
    std::wstring name = ipc::kPipeName;
    std::wstring sddl = kIpcPipeSddl;
};

// Named-pipe IPC server.
// The GUI connects to \\.\pipe\openwinblue and exchanges binary messages
// defined in ipc_protocol.h.
//
// serve_one() blocks until one client connects, exchanges messages until the
// client closes its end, then disconnects. Call in a loop on a dedicated thread.
// stop() may be called from any other thread: it cancels the pending I/O and
// returns once serve_one() has left the pipe.
class IpcServer {
public:
    explicit IpcServer(A2dpStream* stream = nullptr,
                       ai::AiPipeline* ai = nullptr,
                       ICodecController* controller = nullptr,
                       IpcPipeConfig config = {});
    ~IpcServer();

    // Create the named pipe. Returns false on failure, including when another
    // server already owns the pipe name.
    bool start();

    // Stop accepting connections, release a blocked serve_one() and close the pipe.
    void stop();

    // Block until one client connects and serve its session until it closes.
    // Returns false if stopped or an error occurred.
    bool serve_one();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace owb
