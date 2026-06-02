// service/src/main.cpp
//
// OpenWinBlue user-mode service entry point (Phase 2b).
// Components: SBC codec, WASAPI loopback capture, A2DP stream, HFP guard, IPC server.
//
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <atomic>
#include <csignal>
#include <chrono>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winioctl.h>
#include <combaseapi.h>

#include "audio_capture.h"
#include "hfp_guard.h"
#include "ipc_server.h"
#include "a2dp_stream.h"
#include "codec_sbc.h"

namespace {
std::atomic<bool> g_running{true};
void on_signal(int) { g_running = false; }
} // namespace

int main() {
    std::signal(SIGINT,  on_signal);
    std::signal(SIGTERM, on_signal);

    HRESULT hr_com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr_com) && hr_com != RPC_E_CHANGED_MODE) {
        std::puts("[ERROR] COM initialization failed");
        return EXIT_FAILURE;
    }

    owb::AudioCapture capture;
    owb::HfpGuard     hfp_guard;
    owb::IpcServer    ipc;
    owb::A2dpStream   a2dp;
    owb::CodecSbc     codec;  // Phase 2c feeds this into the a2dp send loop

    std::puts("OpenWinBlue service v0.3 starting\xe2\x80\xa6");

    if (!capture.start()) {
        std::puts("[WARN] WASAPI loopback unavailable");
    } else {
        std::printf("[OK]  Audio capture: %d Hz, %d ch\n",
                    capture.sample_rate(), capture.channels());
    }

    if (hfp_guard.start()) {
        std::puts("[OK]  HFP guard active");
    } else {
        std::puts("[WARN] HFP guard unavailable");
    }

    if (a2dp.open()) {
        std::puts("[OK]  A2DP stream driver connected");
    } else {
        std::puts("[WARN] A2DP kernel driver not installed (stub mode)");
    }

    if (!ipc.start()) {
        std::puts("[ERROR] IPC server failed");
        CoUninitialize();
        return EXIT_FAILURE;
    }
    std::puts("[OK]  IPC server listening on \\\\.\\pipe\\openwinblue");

    std::thread ipc_thread([&ipc] {
        while (ipc.serve_one()) {}
    });

    std::puts("[OK]  Service running. Press Ctrl+C to stop.");
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    std::puts("Shutting down\xe2\x80\xa6");
    ipc.stop();
    a2dp.close();
    hfp_guard.stop();
    capture.stop();
    ipc_thread.join();

    CoUninitialize();
    std::puts("Done.");
    return EXIT_SUCCESS;
}
