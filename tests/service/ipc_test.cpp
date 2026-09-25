// tests/service/ipc_test.cpp
#include <gtest/gtest.h>
#include <atomic>
#include <thread>
#include <chrono>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <aclapi.h>
#include <sddl.h>

#include "ipc_server.h"
#include "ipc_protocol.h"
#include "codec_controller.h"
#include "owb_codec_ids.h"

namespace {

// A private name keeps the tests away from a running owb-service.
constexpr wchar_t kTestPipeName[] = L"\\\\.\\pipe\\openwinblue-test";

// The production DACL admits only SYSTEM and elevated admins; Owner Rights lets
// the non-elevated test process talk to the pipe it created.
owb::IpcPipeConfig test_pipe() {
    return { kTestPipeName, L"D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;GA;;;OW)" };
}

owb::IpcPipeConfig test_pipe_with_production_dacl() {
    return { kTestPipeName, owb::kIpcPipeSddl };
}

} // namespace

// Helper: connect as a named-pipe client and send Ping, expect Pong back.
static bool client_ping(int timeout_ms = 3000) {
    if (!WaitNamedPipeW(kTestPipeName, static_cast<DWORD>(timeout_ms)))
        return false;

    HANDLE pipe = CreateFileW(
        kTestPipeName,
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
    owb::IpcServer server(nullptr, nullptr, nullptr, test_pipe());
    ASSERT_TRUE(server.start());

    // Run one serve_one() on a background thread, client connects from this thread
    std::thread t([&server] { server.serve_one(); });

    bool got_pong = client_ping(3000);
    t.join();

    EXPECT_TRUE(got_pong);
}

TEST(IpcServer, StopIsIdempotent) {
    owb::IpcServer server(nullptr, nullptr, nullptr, test_pipe());
    server.start();
    server.stop();
    server.stop();  // must not crash
}

TEST(IpcServer, SetCodec_WhenNotConnected_RepliesWithCodecAck) {
    owb::IpcServer server(nullptr, nullptr, nullptr, test_pipe());  // null stream — stub mode
    ASSERT_TRUE(server.start());

    std::thread t([&server] { server.serve_one(); });

    if (!WaitNamedPipeW(kTestPipeName, 3000)) {
        t.join();
        GTEST_SKIP() << "Pipe not available";
    }

    HANDLE pipe = CreateFileW(kTestPipeName,
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

namespace {

class FakeCodecController final : public owb::ICodecController {
public:
    void set_codec_id(uint32_t codec_id) override {
        switched_ids.push_back(codec_id);
        active_id = codec_id;
    }
    bool set_codec_param(std::string_view key, int64_t value) override {
        params.emplace_back(std::string(key), value);
        return true;
    }
    uint32_t    codec_id() const noexcept override { return active_id; }
    std::string codec_name() const override         { return "SBC"; }
    bool        is_streaming() const noexcept override { return false; }
    uint32_t    bitrate() const override            { return reported_bitrate; }

    std::vector<uint32_t>                        switched_ids;
    std::vector<std::pair<std::string, int64_t>> params;
    uint32_t                                     active_id        = OWB_CODEC_SBC;
    uint32_t                                     reported_bitrate = 0;
};

class PipeClient {
public:
    PipeClient() {
        if (!WaitNamedPipeW(kTestPipeName, 3000)) return;
        pipe_ = CreateFileW(kTestPipeName, GENERIC_READ | GENERIC_WRITE,
                            0, nullptr, OPEN_EXISTING, 0, nullptr);
    }
    ~PipeClient() { close(); }

    PipeClient(const PipeClient&)            = delete;
    PipeClient& operator=(const PipeClient&) = delete;

    bool is_open() const { return pipe_ != INVALID_HANDLE_VALUE; }

    void close() {
        if (!is_open()) return;
        CloseHandle(pipe_);
        pipe_ = INVALID_HANDLE_VALUE;
    }

    bool write(const void* data, DWORD len) {
        DWORD written = 0;
        return WriteFile(pipe_, data, len, &written, nullptr) && written == len;
    }

    bool read(void* data, DWORD len) {
        DWORD got = 0;
        return ReadFile(pipe_, data, len, &got, nullptr) && got == len;
    }

    bool get_status(owb::ipc::StatusPayload& status) {
        const owb::ipc::MsgHeader req{ owb::ipc::MsgType::GetStatus, 0 };
        owb::ipc::MsgHeader reply{};
        return write(&req, sizeof(req))
            && read(&reply, sizeof(reply))
            && reply.type == owb::ipc::MsgType::StatusReply
            && reply.payload_len == sizeof(status)
            && read(&status, sizeof(status));
    }

    int set_codec(const char* codec, const char* key, int64_t value) {
        owb::ipc::SetCodecPayload payload{};
        strncpy_s(payload.codec_name, sizeof(payload.codec_name), codec, _TRUNCATE);
        strncpy_s(payload.param_key,  sizeof(payload.param_key),  key,   _TRUNCATE);
        payload.param_value = value;
        const owb::ipc::MsgHeader hdr{ owb::ipc::MsgType::SetCodec,
                                       static_cast<uint16_t>(sizeof(payload)) };
        if (!write(&hdr, sizeof(hdr)) || !write(&payload, sizeof(payload))) return -1;
        return read_ack();
    }

    int read_ack() {
        owb::ipc::MsgHeader reply{};
        owb::ipc::AckPayload ack{};
        if (!read(&reply, sizeof(reply)) || reply.type != owb::ipc::MsgType::CodecAck)
            return -1;
        if (reply.payload_len != sizeof(ack) || !read(&ack, sizeof(ack))) return -1;
        return ack.success;
    }

private:
    HANDLE pipe_ = INVALID_HANDLE_VALUE;
};

} // namespace

TEST(IpcServer, MultipleMessagesOnOneConnection) {
    owb::IpcServer server(nullptr, nullptr, nullptr, test_pipe());
    ASSERT_TRUE(server.start());
    std::thread t([&server] { server.serve_one(); });

    int replies = 0;
    {
        PipeClient client;
        ASSERT_TRUE(client.is_open());
        owb::ipc::StatusPayload status{};
        for (int i = 0; i < 3; ++i) {
            if (client.get_status(status)) ++replies;
        }
    }
    t.join();

    EXPECT_EQ(replies, 3);
}

TEST(IpcServer, SetCodec_MapsAllGuiCodecNames) {
    FakeCodecController fake;
    owb::IpcServer server(nullptr, nullptr, &fake, test_pipe());
    ASSERT_TRUE(server.start());
    std::thread t([&server] { server.serve_one(); });

    const std::vector<std::pair<const char*, uint32_t>> expected = {
        {"SBC",           OWB_CODEC_SBC},
        {"LDAC",          OWB_CODEC_LDAC},
        {"aptX",          OWB_CODEC_APTX},
        {"aptX-HD",       OWB_CODEC_APTXHD},
        {"AAC",           OWB_CODEC_AAC},
        {"LC3",           OWB_CODEC_LC3},
        {"aptX-Adaptive", OWB_CODEC_APTX_ADAPTIVE},
    };
    std::vector<int> acks;
    int unknown_ack = -1;
    {
        PipeClient client;
        ASSERT_TRUE(client.is_open());
        for (const auto& [name, id] : expected)
            acks.push_back(client.set_codec(name, "switch", 1));
        unknown_ack = client.set_codec("Foo", "switch", 1);
    }
    t.join();

    ASSERT_EQ(fake.switched_ids.size(), expected.size());
    for (size_t i = 0; i < expected.size(); ++i) {
        EXPECT_EQ(fake.switched_ids[i], expected[i].second) << expected[i].first;
        EXPECT_EQ(acks[i], 1) << expected[i].first;
    }
    EXPECT_EQ(unknown_ack, 0);
}

TEST(IpcServer, SetCodec_ShortPayloadIsRejectedAndSessionContinues) {
    FakeCodecController fake;
    owb::IpcServer server(nullptr, nullptr, &fake, test_pipe());
    ASSERT_TRUE(server.start());
    std::thread t([&server] { server.serve_one(); });

    int ack = -1;
    bool status_ok = false;
    {
        PipeClient client;
        ASSERT_TRUE(client.is_open());
        const uint8_t short_payload[8] = {'A', 'A', 'C', 0, 0, 0, 0, 0};
        const owb::ipc::MsgHeader hdr{ owb::ipc::MsgType::SetCodec,
                                       static_cast<uint16_t>(sizeof(short_payload)) };
        if (client.write(&hdr, sizeof(hdr)) && client.write(short_payload, sizeof(short_payload)))
            ack = client.read_ack();
        owb::ipc::StatusPayload status{};
        status_ok = client.get_status(status);
    }
    t.join();

    EXPECT_EQ(ack, 0);
    EXPECT_TRUE(status_ok);
    EXPECT_TRUE(fake.switched_ids.empty());
}

TEST(IpcServer, SetCodec_ParamDoesNotRebuildCodec) {
    FakeCodecController fake;
    owb::IpcServer server(nullptr, nullptr, &fake, test_pipe());
    ASSERT_TRUE(server.start());
    std::thread t([&server] { server.serve_one(); });

    int ack = -1;
    {
        PipeClient client;
        ASSERT_TRUE(client.is_open());
        ack = client.set_codec("SBC", "bitpool", 40);
    }
    t.join();

    EXPECT_EQ(ack, 1);
    EXPECT_TRUE(fake.switched_ids.empty());
    ASSERT_EQ(fake.params.size(), 1u);
    EXPECT_EQ(fake.params[0].first, "bitpool");
    EXPECT_EQ(fake.params[0].second, 40);
}

TEST(IpcServer, SetCodec_BitpoolIsClampedToA2dpRange) {
    FakeCodecController fake;
    owb::IpcServer server(nullptr, nullptr, &fake, test_pipe());
    ASSERT_TRUE(server.start());
    std::thread t([&server] { server.serve_one(); });

    {
        PipeClient client;
        ASSERT_TRUE(client.is_open());
        client.set_codec("SBC", "bitpool", 64);
        client.set_codec("SBC", "bitpool", 1);
    }
    t.join();

    ASSERT_EQ(fake.params.size(), 2u);
    EXPECT_EQ(fake.params[0].second, 53);
    EXPECT_EQ(fake.params[1].second, 2);
}

TEST(IpcServer, GetStatus_ReportsControllerBitrate) {
    FakeCodecController fake;
    fake.reported_bitrate = 327993;
    owb::IpcServer server(nullptr, nullptr, &fake, test_pipe());
    ASSERT_TRUE(server.start());
    std::thread t([&server] { server.serve_one(); });

    owb::ipc::StatusPayload status{};
    bool ok = false;
    {
        PipeClient client;
        ASSERT_TRUE(client.is_open());
        ok = client.get_status(status);
    }
    t.join();

    ASSERT_TRUE(ok);
    EXPECT_EQ(status.bitrate, 327993u);
    EXPECT_STREQ(status.codec_name, "SBC");
}

namespace {

constexpr auto kStopDeadline = std::chrono::seconds(2);

bool wait_until_set(const std::atomic<bool>& flag, std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!flag && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    return flag;
}

// Unblocks a serve_one() that stop() failed to release so the test can still join.
void rescue_blocked_server(std::thread& serve_thread) {
    CancelSynchronousIo(serve_thread.native_handle());
    PipeClient unblocker;
}

bool stop_releases_server(owb::IpcServer& server, std::thread& serve_thread,
                          const std::atomic<bool>& serve_returned) {
    std::atomic<bool> stop_returned{false};
    std::thread stop_thread([&] { server.stop(); stop_returned = true; });

    const bool released = wait_until_set(serve_returned, kStopDeadline)
                       && wait_until_set(stop_returned, kStopDeadline);
    if (!released) rescue_blocked_server(serve_thread);
    serve_thread.join();
    stop_thread.join();
    return released;
}

bool is_process_elevated() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
    TOKEN_ELEVATION elevation{};
    DWORD size = 0;
    const BOOL ok = GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size);
    CloseHandle(token);
    return ok && elevation.TokenIsElevated != 0;
}

std::wstring read_pipe_dacl(const wchar_t* name) {
    HANDLE pipe = CreateFileW(name, READ_CONTROL, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (pipe == INVALID_HANDLE_VALUE) return {};

    PSECURITY_DESCRIPTOR descriptor = nullptr;
    const DWORD rc = GetSecurityInfo(pipe, SE_KERNEL_OBJECT, DACL_SECURITY_INFORMATION,
                                     nullptr, nullptr, nullptr, nullptr, &descriptor);
    CloseHandle(pipe);
    if (rc != ERROR_SUCCESS) return {};

    LPWSTR sddl = nullptr;
    std::wstring result;
    if (ConvertSecurityDescriptorToStringSecurityDescriptorW(
            descriptor, SDDL_REVISION_1, DACL_SECURITY_INFORMATION, &sddl, nullptr)) {
        result = sddl;
        LocalFree(sddl);
    }
    LocalFree(descriptor);
    return result;
}

} // namespace

TEST(IpcServer, StopUnblocksPendingServeOne) {
    owb::IpcServer server(nullptr, nullptr, nullptr, test_pipe());
    ASSERT_TRUE(server.start());

    std::atomic<bool> serve_returned{false};
    std::thread serve_thread([&] { server.serve_one(); serve_returned = true; });
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    EXPECT_TRUE(stop_releases_server(server, serve_thread, serve_returned))
        << "stop() did not release serve_one() within 2 s";
}

TEST(IpcServer, StopUnblocksServeOneDuringSession) {
    owb::IpcServer server(nullptr, nullptr, nullptr, test_pipe());
    ASSERT_TRUE(server.start());

    std::atomic<bool> serve_returned{false};
    std::thread serve_thread([&] { server.serve_one(); serve_returned = true; });

    PipeClient client;
    ASSERT_TRUE(client.is_open());
    owb::ipc::StatusPayload status{};
    ASSERT_TRUE(client.get_status(status));

    EXPECT_TRUE(stop_releases_server(server, serve_thread, serve_returned))
        << "stop() did not release a session waiting for the next message";
}

TEST(IpcServer, SecondServerInstanceFails) {
    owb::IpcServer first(nullptr, nullptr, nullptr, test_pipe());
    ASSERT_TRUE(first.start());

    owb::IpcServer second(nullptr, nullptr, nullptr, test_pipe());
    EXPECT_FALSE(second.start());
}

TEST(IpcServer, PipeIsCreatedWithConfiguredDacl) {
    const owb::IpcPipeConfig readable_by_owner{
        kTestPipeName, std::wstring(owb::kIpcPipeSddl) + L"(A;;GR;;;OW)" };
    owb::IpcServer server(nullptr, nullptr, nullptr, readable_by_owner);
    ASSERT_TRUE(server.start());

    EXPECT_EQ(read_pipe_dacl(kTestPipeName), L"D:P(A;;FA;;;SY)(A;;FA;;;BA)(A;;FR;;;OW)");
}

TEST(IpcServer, PipeRejectsNonElevatedClient) {
    if (is_process_elevated()) GTEST_SKIP() << "Needs a non-elevated process";

    owb::IpcServer server(nullptr, nullptr, nullptr, test_pipe_with_production_dacl());
    ASSERT_TRUE(server.start());

    HANDLE pipe = CreateFileW(kTestPipeName, GENERIC_READ | GENERIC_WRITE,
                              0, nullptr, OPEN_EXISTING, 0, nullptr);
    const DWORD err = GetLastError();
    if (pipe != INVALID_HANDLE_VALUE) CloseHandle(pipe);

    EXPECT_EQ(pipe, INVALID_HANDLE_VALUE);
    EXPECT_EQ(err, static_cast<DWORD>(ERROR_ACCESS_DENIED));
}
