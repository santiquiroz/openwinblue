// service/src/ipc_server.h
#pragma once
#include <memory>

namespace owb { class A2dpStream; class ICodecController; }
namespace owb::ai { class AiPipeline; }

namespace owb {

// Named-pipe IPC server.
// The GUI connects to \\.\pipe\openwinblue and exchanges binary messages
// defined in ipc_protocol.h.
//
// serve_one() blocks until one client connects, exchanges messages,
// then disconnects. Call in a loop on a dedicated thread.
class IpcServer {
public:
    explicit IpcServer(A2dpStream* stream = nullptr,
                       ai::AiPipeline* ai = nullptr,
                       ICodecController* controller = nullptr);
    ~IpcServer();

    // Create the named pipe. Returns false on failure.
    bool start();

    // Signal the server to stop accepting connections and close the pipe.
    void stop();

    // Block until one client connects, exchanges at least one message, disconnects.
    // Returns false if stopped or an error occurred.
    bool serve_one();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace owb
