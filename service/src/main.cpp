// service/src/main.cpp
//
// OpenWinBlue user-mode service entry point.
//
// Runs in two modes from the same binary:
//   • Windows Service (default): launched by the SCM after installation.
//   • Console (`owb-service.exe --console`): for local development / debugging.
//
// Pipeline: WASAPI capture → AI (optional) → codec encode → A2DP driver IOCTL.
//
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <thread>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <combaseapi.h>

#include "audio_capture.h"
#include "hfp_guard.h"
#include "ipc_server.h"
#include "a2dp_stream.h"
#include "stream_pipeline.h"
#include "codec_factory.h"
#include "ai_pipeline.h"

namespace {

constexpr wchar_t kServiceName[] = L"owb-service";

std::atomic<bool> g_running{true};

SERVICE_STATUS        g_status{};
SERVICE_STATUS_HANDLE g_status_handle = nullptr;

void on_signal(int) { g_running = false; }

// The actual service body: build components, run the pipeline, block until stop.
int run_service() {
    HRESULT hr_com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr_com) && hr_com != RPC_E_CHANGED_MODE) {
        std::puts("[ERROR] COM initialization failed");
        return EXIT_FAILURE;
    }

    owb::AudioCapture   capture;
    owb::HfpGuard       hfp_guard;
    owb::A2dpStream     a2dp;
    owb::ai::AiPipeline ai;
    owb::StreamPipeline pipeline(&capture, &a2dp, &ai);
    owb::IpcServer      ipc(&a2dp, &ai, &pipeline);

    std::puts("OpenWinBlue service starting\xe2\x80\xa6");

    if (!capture.start()) {
        std::puts("[WARN] WASAPI loopback unavailable");
    } else {
        std::printf("[OK]  Audio capture: %d Hz, %d ch\n",
                    capture.sample_rate(), capture.channels());
    }

    std::puts(hfp_guard.start() ? "[OK]  HFP guard active"
                                : "[WARN] HFP guard unavailable");
    std::puts(a2dp.open() ? "[OK]  A2DP stream driver connected"
                          : "[WARN] A2DP kernel driver not installed (stub mode)");

    if (pipeline.start())
        std::printf("[OK]  Stream pipeline running (codec: %s)\n",
                    pipeline.codec_name().c_str());
    else
        std::puts("[WARN] Stream pipeline could not start (no audio source)");

    if (!ipc.start()) {
        std::puts("[ERROR] IPC server failed");
        pipeline.stop();
        CoUninitialize();
        return EXIT_FAILURE;
    }
    std::puts("[OK]  IPC server listening on \\\\.\\pipe\\openwinblue");

    std::thread ipc_thread([&ipc] { while (ipc.serve_one()) {} });

    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    std::puts("Shutting down\xe2\x80\xa6");
    ipc.stop();
    pipeline.stop();
    a2dp.close();
    hfp_guard.stop();
    capture.stop();
    ipc_thread.join();

    CoUninitialize();
    std::puts("Done.");
    return EXIT_SUCCESS;
}

void report_status(DWORD current, DWORD wait_hint = 0) {
    g_status.dwCurrentState  = current;
    g_status.dwWaitHint      = wait_hint;
    g_status.dwControlsAccepted =
        (current == SERVICE_START_PENDING) ? 0u : SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;
    if (g_status_handle) SetServiceStatus(g_status_handle, &g_status);
}

DWORD WINAPI service_ctrl_handler(DWORD ctrl, DWORD, LPVOID, LPVOID) {
    switch (ctrl) {
        case SERVICE_CONTROL_STOP:
        case SERVICE_CONTROL_SHUTDOWN:
            g_running = false;
            report_status(SERVICE_STOP_PENDING, 3000);
            return NO_ERROR;
        case SERVICE_CONTROL_INTERROGATE:
            return NO_ERROR;
        default:
            return ERROR_CALL_NOT_IMPLEMENTED;
    }
}

void WINAPI service_main(DWORD, LPWSTR*) {
    g_status_handle = RegisterServiceCtrlHandlerExW(
        kServiceName, service_ctrl_handler, nullptr);
    if (!g_status_handle) return;

    g_status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    report_status(SERVICE_START_PENDING, 3000);
    report_status(SERVICE_RUNNING);

    const int rc = run_service();

    g_status.dwWin32ExitCode = (rc == EXIT_SUCCESS) ? NO_ERROR : ERROR_SERVICE_SPECIFIC_ERROR;
    report_status(SERVICE_STOPPED);
}

int run_console() {
    std::signal(SIGINT,  on_signal);
    std::signal(SIGTERM, on_signal);
    std::puts("[OK]  Console mode. Press Ctrl+C to stop.");
    return run_service();
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc > 1 && (::wcscmp(argv[1], L"--console") == 0 ||
                     ::wcscmp(argv[1], L"-c") == 0)) {
        return run_console();
    }

    SERVICE_TABLE_ENTRYW table[] = {
        { const_cast<LPWSTR>(kServiceName), service_main },
        { nullptr, nullptr },
    };

    // If not launched by the SCM, StartServiceCtrlDispatcher fails with
    // ERROR_FAILED_SERVICE_CONTROLLER_CONNECT — fall back to console mode.
    if (!StartServiceCtrlDispatcherW(table)) {
        if (GetLastError() == ERROR_FAILED_SERVICE_CONTROLLER_CONNECT)
            return run_console();
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
