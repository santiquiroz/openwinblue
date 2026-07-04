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
#include "owb_log.h"

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
        OWB_LOG_ERROR("CoInitializeEx failed: 0x%08lx", (unsigned long)hr_com);
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
        OWB_LOG_WARN("WASAPI loopback unavailable");
        std::puts("[WARN] WASAPI loopback unavailable");
    } else {
        OWB_LOG_INFO("Audio capture: %d Hz, %d ch", capture.sample_rate(), capture.channels());
        std::printf("[OK]  Audio capture: %d Hz, %d ch\n",
                    capture.sample_rate(), capture.channels());
    }

    bool hfp_ok = hfp_guard.start();
    if (hfp_ok) {
        OWB_LOG_INFO("HFP guard activated");
    }
    std::puts(hfp_ok ? "[OK]  HFP guard active"
                     : "[WARN] HFP guard unavailable");
    
    bool a2dp_ok = a2dp.open();
    OWB_LOG_INFO("A2DP stream %s", a2dp_ok ? "connected" : "not available (stub mode)");
    std::puts(a2dp_ok ? "[OK]  A2DP stream driver connected"
                      : "[WARN] A2DP kernel driver not installed (stub mode)");

    bool pipeline_ok = pipeline.start();
    if (pipeline_ok) {
        OWB_LOG_INFO("Stream pipeline started (codec: %s)", pipeline.codec_name().c_str());
        std::printf("[OK]  Stream pipeline running (codec: %s)\n",
                    pipeline.codec_name().c_str());
    } else {
        OWB_LOG_WARN("Stream pipeline could not start (no audio source)");
        std::puts("[WARN] Stream pipeline could not start (no audio source)");
    }

    if (!ipc.start()) {
        OWB_LOG_ERROR("IPC server failed to start");
        std::puts("[ERROR] IPC server failed");
        pipeline.stop();
        CoUninitialize();
        return EXIT_FAILURE;
    }
    OWB_LOG_INFO("IPC server listening on \\\\.\\pipe\\openwinblue");
    std::puts("[OK]  IPC server listening on \\\\.\\pipe\\openwinblue");

    std::thread ipc_thread([&ipc] { while (ipc.serve_one()) {} });

    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    std::puts("Shutting down\xe2\x80\xa6");
    OWB_LOG_INFO("Shutdown phase: stopping IPC server");
    ipc.stop();
    OWB_LOG_INFO("Shutdown phase: stopping pipeline");
    pipeline.stop();
    OWB_LOG_INFO("Shutdown phase: closing A2DP stream");
    a2dp.close();
    OWB_LOG_INFO("Shutdown phase: stopping HFP guard");
    hfp_guard.stop();
    OWB_LOG_INFO("Shutdown phase: stopping audio capture");
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
            OWB_LOG_INFO("Service control: STOP signal received");
            g_running = false;
            report_status(SERVICE_STOP_PENDING, 3000);
            return NO_ERROR;
        case SERVICE_CONTROL_SHUTDOWN:
            OWB_LOG_INFO("Service control: SHUTDOWN signal received");
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
    owb::log::init("owb-service");

    if (argc > 1 && (::wcscmp(argv[1], L"--console") == 0 ||
                     ::wcscmp(argv[1], L"-c") == 0)) {
        OWB_LOG_INFO("Running in console mode");
        int rc = run_console();
        owb::log::shutdown();
        return rc;
    }

    OWB_LOG_INFO("Running as Windows Service");
    SERVICE_TABLE_ENTRYW table[] = {
        { const_cast<LPWSTR>(kServiceName), service_main },
        { nullptr, nullptr },
    };

    // If not launched by the SCM, StartServiceCtrlDispatcher fails with
    // ERROR_FAILED_SERVICE_CONTROLLER_CONNECT — fall back to console mode.
    if (!StartServiceCtrlDispatcherW(table)) {
        if (GetLastError() == ERROR_FAILED_SERVICE_CONTROLLER_CONNECT) {
            OWB_LOG_INFO("SCM dispatcher unavailable, falling back to console mode");
            int rc = run_console();
            owb::log::shutdown();
            return rc;
        }
        owb::log::shutdown();
        return EXIT_FAILURE;
    }
    owb::log::shutdown();
    return EXIT_SUCCESS;
}
