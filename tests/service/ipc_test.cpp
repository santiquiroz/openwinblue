// tests/service/ipc_test.cpp
#include <gtest/gtest.h>
#include <thread>
#include <chrono>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include "ipc_server.h"
#include "ipc_protocol.h"

// Helper: connect as a named-pipe client and send Ping, expect Pong back.
static bool client_ping(int timeout_ms = 3000) {
    if (!WaitNamedPipeW(owb::ipc::kPipeName, static_cast<DWORD>(timeout_ms)))
        return false;

    HANDLE pipe = CreateFileW(
        owb::ipc::kPipeName,
        GENERIC_READ | GENERIC_WRITE,
        0, nullptr, OPEN_EXISTING,
        0, nullptr
    );
    if (pipe == INVALID_HANDLE_VALUE) return false;

    // Send Ping
    owb::ipc::MsgHeader ping{ owb::ipc::MsgType::Ping, 0 };
    DWORD written = 0;
    WriteFile(pipe, &ping, sizeof(ping), &written, nullptr);

    // Read Pong
    owb::ipc::MsgHeader pong{};
    DWORD read_bytes = 0;
    BOOL ok = ReadFile(pipe, &pong, sizeof(pong), &read_bytes, nullptr);
    CloseHandle(pipe);

    return ok && read_bytes == sizeof(pong) && pong.type == owb::ipc::MsgType::Pong;
}

TEST(IpcServer, PingPongRoundTrip) {
    owb::IpcServer server;
    ASSERT_TRUE(server.start());

    // Run one serve_one() on a background thread, client connects from this thread
    std::thread t([&server] { server.serve_one(); });

    bool got_pong = client_ping(3000);
    t.join();

    EXPECT_TRUE(got_pong);
}

TEST(IpcServer, StopIsIdempotent) {
    owb::IpcServer server;
    server.start();
    server.stop();
    server.stop();  // must not crash
}

TEST(IpcServer, SetCodec_WhenNotConnected_RepliesWithCodecAck) {
    owb::IpcServer server(nullptr);  // null stream — stub mode
    ASSERT_TRUE(server.start());

    std::thread t([&server] { server.serve_one(); });

    if (!WaitNamedPipeW(owb::ipc::kPipeName, 3000)) {
        t.join();
        GTEST_SKIP() << "Pipe not available";
    }

    HANDLE pipe = CreateFileW(owb::ipc::kPipeName,
        GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (pipe == INVALID_HANDLE_VALUE) { t.join(); GTEST_SKIP(); }

    // Build SetCodec message
    owb::ipc::MsgHeader hdr{ owb::ipc::MsgType::SetCodec,
                              sizeof(owb::ipc::SetCodecPayload) };
    owb::ipc::SetCodecPayload payload{};
    strncpy_s(payload.codec_name, sizeof(payload.codec_name), "LDAC",   _TRUNCATE);
    strncpy_s(payload.param_key,  sizeof(payload.param_key),  "switch", _TRUNCATE);
    payload.param_value = 1;

    DWORD written = 0;
    WriteFile(pipe, &hdr,     sizeof(hdr),     &written, nullptr);
    WriteFile(pipe, &payload, sizeof(payload), &written, nullptr);

    // Read CodecAck reply
    owb::ipc::MsgHeader reply{};
    DWORD read_bytes = 0;
    BOOL ok = ReadFile(pipe, &reply, sizeof(reply), &read_bytes, nullptr);
    CloseHandle(pipe);
    t.join();

    EXPECT_TRUE(ok);
    if (ok) EXPECT_EQ(reply.type, owb::ipc::MsgType::CodecAck);
}
