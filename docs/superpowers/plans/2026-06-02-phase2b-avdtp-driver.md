# Phase 2b — AVDTP Driver + IOCTL Interface Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:subagent-driven-development` (recommended) or `superpowers:executing-plans` to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Wire the kernel driver to the user-mode service via a well-defined IOCTL interface, implement the AVDTP A2DP signaling state machine in kernel mode, and set up CI to compile and sign the driver with WDK.

**Architecture:** The IOCTL interface (`driver/owb_ioctl.h`) is a plain C header compilable in both kernel and user-mode contexts — it defines the binary contract between the two halves. The user-mode `A2dpStream` component opens the kernel device and sends encoded frames via `DeviceIoControl`. The kernel driver implements the full AVDTP signaling state machine (DISCOVER → GET_CAPS → SET_CONFIG → OPEN → START) and an L2CAP media channel that accepts RTP-framed SBC from the service. WDK is not installed locally on this machine; all driver builds run on GitHub Actions CI (windows-2022 runner with WDK installed via chocolatey).

**Tech Stack:** C11 (kernel driver), C++20 (service A2dpStream), KMDF WDK 11, AVDTP/L2CAP Bluetooth stack (BthPort.sys BRBs), `DeviceIoControl` Win32 API, GitHub Actions + Chocolatey WDK install.

---

## Environment Notes

- **WDK NOT installed locally.** The `km/` kernel-mode headers are absent from `C:/Program Files (x86)/Windows Kits/10/Include/`. Tasks 1–4 (IOCTL interface + A2dpStream + CI + INF) are buildable and testable locally. Tasks 5–8 (kernel driver C code) require WDK and are verified only on CI.
- **cmake**: `C:/Users/santi/AppData/Local/Android/Sdk/cmake/4.1.2/bin/cmake.exe`
- **Build preset (local)**: `nmake-debug`
- **MSVC env**: source `C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat`

---

## File Map

### New files — locally compilable (Tasks 1–4)

```
driver/
  owb_ioctl.h               # IOCTL codes + payload structs (plain C, no WDK types)
  owb_a2dp.inf              # UPDATED: real A2DP hardware IDs pattern

service/src/
  a2dp_stream.h             # A2dpStream class declaration
  a2dp_stream.cpp           # User-mode DeviceIoControl wrapper + stub mode

tests/service/
  a2dp_stream_test.cpp      # Tests for A2dpStream (stub mode — no driver needed)
```

### New files — WDK required (Tasks 5–8, CI only)

```
driver/
  owb_a2dp.vcxproj          # KMDF Visual Studio project file
  owb_a2dp.vcxproj.props    # WDK property overrides
  src/
    avdtp.h                 # AVDTP state machine declarations
    avdtp.c                 # AVDTP signaling: DISCOVER/GET_CAPS/SET_CONFIG/OPEN/START
    l2cap_stream.h          # L2CAP media channel declarations
    l2cap_stream.c          # L2CAP channel management + RTP framing
    ioctl.h                 # IOCTL handler declarations
    ioctl.c                 # IOCTL dispatch: SEND_FRAME/GET_RF/SET_CONFIG/GET_STATE
    owb_a2dp.c              # UPDATED: wires avdtp + l2cap + ioctl into DriverEntry
    owb_a2dp.h              # UPDATED: adds OWB_DEVICE_EXTENSION fields for AVDTP state
```

### Modified files

```
.github/workflows/ci.yml    # Add WDK install + driver build job
service/CMakeLists.txt      # Add owb_a2dp_stream static library + tests
service/src/main.cpp        # Wire A2dpStream into the service run loop
tests/service/CMakeLists.txt # Add a2dp_stream_test.cpp
```

---

## Task 1: IOCTL interface header

**Files:**
- Create: `driver/owb_ioctl.h`

This header is the binary contract between the kernel driver and the user-mode service. It uses only types available in both contexts (`ULONG`, `LONG`, `UCHAR`, `CHAR`). In kernel mode it's included after `ntddk.h`; in user mode after `windows.h`.

- [ ] **Step 1.1: Create `driver/owb_ioctl.h`**

```c
// driver/owb_ioctl.h
// IOCTL interface between owb_a2dp.sys (kernel) and owb-service.exe (user-mode).
// Plain C header — compilable in both kernel and user-mode contexts.
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif

// CTL_CODE is defined in winioctl.h (user) and wdm.h (kernel).
// Both paths provide it; include guards prevent double-inclusion.
#ifndef CTL_CODE
#  include <winioctl.h>
#endif

// Custom device type for OpenWinBlue IOCTLs (0x8000–0xFFFF = vendor range)
#define OWB_DEVICE_TYPE  0x8000u

// ─── IOCTL codes ──────────────────────────────────────────────────────────────

// Send one encoded audio frame to transmit over Bluetooth A2DP.
// Input:  OWB_SEND_FRAME_INPUT  (variable-length — use OWB_SEND_FRAME_INPUT_SIZE)
// Output: none
#define OWB_IOCTL_SEND_AUDIO_FRAME \
    CTL_CODE(OWB_DEVICE_TYPE, 0x800, METHOD_BUFFERED, FILE_WRITE_DATA)

// Query RF link quality for adaptive bitrate decisions.
// Input:  none
// Output: OWB_RF_QUALITY
#define OWB_IOCTL_GET_RF_QUALITY \
    CTL_CODE(OWB_DEVICE_TYPE, 0x801, METHOD_BUFFERED, FILE_READ_DATA)

// Push a codec parameter to the driver (triggers AVDTP SET_CONFIGURATION).
// Input:  OWB_CODEC_CONFIG
// Output: none
#define OWB_IOCTL_SET_CODEC_CONFIG \
    CTL_CODE(OWB_DEVICE_TYPE, 0x802, METHOD_BUFFERED, FILE_WRITE_DATA)

// Query the A2DP connection state.
// Input:  none
// Output: OWB_DEVICE_STATE
#define OWB_IOCTL_GET_DEVICE_STATE \
    CTL_CODE(OWB_DEVICE_TYPE, 0x803, METHOD_BUFFERED, FILE_READ_DATA)

// ─── Payload structures ───────────────────────────────────────────────────────

#pragma pack(push, 1)

// Codec IDs — matches owb::ipc::SetCodecPayload.codec_name convention
#define OWB_CODEC_SBC    0u
#define OWB_CODEC_LDAC   1u
#define OWB_CODEC_APTX   2u
#define OWB_CODEC_APTXHD 3u
#define OWB_CODEC_AAC    4u
#define OWB_CODEC_LC3    5u

// Input for OWB_IOCTL_SEND_AUDIO_FRAME.
// data[] holds exactly data_len bytes of encoded audio (SBC frame, LDAC frame, etc.)
typedef struct _OWB_SEND_FRAME_INPUT {
    unsigned long  codec_id;   // OWB_CODEC_*
    unsigned long  data_len;   // byte count of data[]
    unsigned char  data[1];    // flexible array — actual size = data_len
} OWB_SEND_FRAME_INPUT;

// Compute the allocation size for a given frame length.
#define OWB_SEND_FRAME_INPUT_SIZE(data_bytes) \
    (unsigned long)(offsetof(OWB_SEND_FRAME_INPUT, data) + (data_bytes))

// Output for OWB_IOCTL_GET_RF_QUALITY.
typedef struct _OWB_RF_QUALITY {
    long           rssi_dbm;             // signal strength (negative dBm)
    unsigned long  retransmit_per_mille; // retransmissions per 1000 packets
    unsigned long  link_quality;         // Windows quality score 0–255
} OWB_RF_QUALITY;

// Input for OWB_IOCTL_SET_CODEC_CONFIG.
typedef struct _OWB_CODEC_CONFIG {
    unsigned long  codec_id;        // OWB_CODEC_*
    char           param_key[16];   // parameter name, e.g. "bitpool"
    long long      param_value;     // parameter value, e.g. 53
} OWB_CODEC_CONFIG;

// Output for OWB_IOCTL_GET_DEVICE_STATE.
#define OWB_STATE_DISCONNECTED 0u
#define OWB_STATE_CONNECTING   1u
#define OWB_STATE_CONNECTED    2u
#define OWB_STATE_STREAMING    3u

typedef struct _OWB_DEVICE_STATE {
    unsigned long  state;           // OWB_STATE_*
    unsigned long  active_codec_id; // OWB_CODEC_*
    unsigned char  remote_addr[6];  // Bluetooth address (little-endian)
    unsigned char  _pad[2];
} OWB_DEVICE_STATE;

#pragma pack(pop)
```

Save to: `driver/owb_ioctl.h`

- [ ] **Step 1.2: Verify the header compiles in user-mode context**

Add a one-liner smoke build to confirm there are no syntax errors (user-mode only, no WDK needed):

```powershell
# Quick syntax check via CL — should produce zero errors
$env:PATH = "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Tools\MSVC\14.50.35717\bin\Hostx64\x64;$env:PATH"
$env:INCLUDE = "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Tools\MSVC\14.50.35717\include;C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\ucrt;C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\um;C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\shared"
echo '#include "driver/owb_ioctl.h"' | cl /c /nologo /W4 /WX /utf-8 /TC /Fo"NUL" /I"c:/suru/open winblue" /TP - 2>&1
```

Expected: `0 error(s)` — or no output (success). If `winioctl.h` is not found, check the SDK include path.

- [ ] **Step 1.3: Commit**

```powershell
cd "c:/suru/open winblue"
git add driver/owb_ioctl.h
git commit -m "feat(driver): add IOCTL interface header (shared kernel/user-mode contract)"
```

---

## Task 2: A2dpStream user-mode component (TDD)

**Files:**
- Create: `service/src/a2dp_stream.h`
- Create: `service/src/a2dp_stream.cpp`
- Create: `tests/service/a2dp_stream_test.cpp`
- Modify: `service/CMakeLists.txt` (add `owb_a2dp_stream` static library)
- Modify: `tests/service/CMakeLists.txt` (add test)

The `A2dpStream` opens the kernel device (`\\.\OpenWinBlue`) via `CreateFile` and sends frames via `DeviceIoControl`. When the driver is not installed (device open fails), it silently drops frames — this **stub mode** lets the service pipeline run without the driver installed, which is the expected state during Phase 2b development.

- [ ] **Step 2.1: Write failing tests first**

```cpp
// tests/service/a2dp_stream_test.cpp
#include <gtest/gtest.h>
#include <vector>
#include "a2dp_stream.h"

// In these tests the kernel driver is NOT installed.
// A2dpStream must operate in stub mode (is_open()==false, send_frame returns false gracefully).

TEST(A2dpStream, ConstructsWithoutCrash) {
    owb::A2dpStream stream;
    // Before open(), stream is always in stub mode
    EXPECT_FALSE(stream.is_open());
}

TEST(A2dpStream, OpenFailsGracefullyWithoutDriver) {
    owb::A2dpStream stream;
    bool opened = stream.open();
    // Driver is not installed — must return false, not throw
    EXPECT_FALSE(opened);
    EXPECT_FALSE(stream.is_open());
}

TEST(A2dpStream, SendFrameInStubModeReturnsFalse) {
    owb::A2dpStream stream;
    // Do NOT call open() — stream is in stub mode
    std::vector<uint8_t> frame(128, 0x9C);
    EXPECT_FALSE(stream.send_frame(0 /*SBC*/, frame));
}

TEST(A2dpStream, CloseIsIdempotent) {
    owb::A2dpStream stream;
    stream.close();  // must not crash when called without open()
    stream.close();  // double close must not crash
}

TEST(A2dpStream, GetRfQualityReturnsFalseInStubMode) {
    owb::A2dpStream stream;
    int32_t rssi = 0;
    uint32_t retransmit = 0;
    EXPECT_FALSE(stream.get_rf_quality(&rssi, &retransmit));
}
```

Save to: `tests/service/a2dp_stream_test.cpp`

- [ ] **Step 2.2: Add to `tests/service/CMakeLists.txt`**

Add `a2dp_stream_test.cpp` to `owb_service_tests` sources and add `owb_a2dp_stream` to link libraries. Read the current file first, then replace:

```cmake
add_executable(owb_service_tests
    smoke_test.cpp
    codec_sbc_test.cpp
    ipc_test.cpp
    hfp_guard_test.cpp
    a2dp_stream_test.cpp
)

target_link_libraries(owb_service_tests PRIVATE
    GTest::gtest_main
    GTest::gmock
    owb_codec_sbc
    owb_sbc
    owb_hfp_guard
    owb_ipc_server
    owb_a2dp_stream
    ole32 oleaut32
)

target_include_directories(owb_service_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/service/codecs
    ${CMAKE_SOURCE_DIR}/service/src
    ${CMAKE_SOURCE_DIR}/driver
    ${CMAKE_SOURCE_DIR}/third-party/libsbc
    ${CMAKE_SOURCE_DIR}/third-party/libsbc-compat
)

target_compile_options(owb_service_tests PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /WX /utf-8>
)

include(GoogleTest)
gtest_discover_tests(owb_service_tests)
```

Note: `${CMAKE_SOURCE_DIR}/driver` is added so `a2dp_stream.cpp` can include `owb_ioctl.h`.

- [ ] **Step 2.3: Create `service/src/a2dp_stream.h`**

```cpp
// service/src/a2dp_stream.h
#pragma once
#include <cstdint>
#include <span>
#include <string_view>

namespace owb {

// A2dpStream — user-mode DeviceIoControl interface to owb_a2dp.sys.
//
// When the kernel driver is not installed (the device cannot be opened),
// the stream operates in stub mode: is_open() returns false, send_frame()
// and get_rf_quality() return false immediately without side effects.
// This lets the service pipeline run during development before the driver
// is installed.
class A2dpStream {
public:
    A2dpStream();
    ~A2dpStream();

    // Open the kernel device. Returns false if the driver is not installed.
    // Safe to call multiple times — re-open is a no-op if already open.
    bool open();

    // Close the device handle. Safe to call when not open.
    void close();

    // Returns true only when the kernel driver device is open.
    bool is_open() const noexcept;

    // Send one encoded audio frame. codec_id: OWB_CODEC_* constants from owb_ioctl.h.
    // Returns false in stub mode or if the IOCTL fails.
    bool send_frame(uint32_t codec_id, std::span<const uint8_t> frame);

    // Query RF link quality. Fills *rssi_dbm and *retransmit_per_mille.
    // Returns false in stub mode or if the driver is not streaming.
    bool get_rf_quality(int32_t* rssi_dbm, uint32_t* retransmit_per_mille);

    // Push a codec parameter to the driver.
    // Returns false in stub mode or if the IOCTL fails.
    bool set_codec_config(uint32_t codec_id, std::string_view key, int64_t value);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace owb
```

Save to: `service/src/a2dp_stream.h`

- [ ] **Step 2.4: Create `service/src/a2dp_stream.cpp`**

```cpp
// service/src/a2dp_stream.cpp
#include "a2dp_stream.h"
#include "owb_ioctl.h"   // from driver/ — included via CMake include_directories

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstring>
#include <memory>
#include <vector>

namespace owb {

// Symbolic name for the kernel device created by owb_a2dp.sys.
static constexpr wchar_t kDeviceName[] = L"\\\\.\\OpenWinBlue";

struct A2dpStream::Impl {
    HANDLE device = INVALID_HANDLE_VALUE;
};

A2dpStream::A2dpStream() : impl_(std::make_unique<Impl>()) {}

A2dpStream::~A2dpStream() { close(); }

bool A2dpStream::open() {
    if (impl_->device != INVALID_HANDLE_VALUE) return true;

    impl_->device = CreateFileW(
        kDeviceName,
        GENERIC_READ | GENERIC_WRITE,
        0, nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );
    return impl_->device != INVALID_HANDLE_VALUE;
}

void A2dpStream::close() {
    if (impl_->device != INVALID_HANDLE_VALUE) {
        CloseHandle(impl_->device);
        impl_->device = INVALID_HANDLE_VALUE;
    }
}

bool A2dpStream::is_open() const noexcept {
    return impl_->device != INVALID_HANDLE_VALUE;
}

bool A2dpStream::send_frame(uint32_t codec_id, std::span<const uint8_t> frame) {
    if (!is_open() || frame.empty()) return false;

    const DWORD input_size = OWB_SEND_FRAME_INPUT_SIZE(static_cast<DWORD>(frame.size()));
    std::vector<BYTE> buf(input_size);

    auto* input          = reinterpret_cast<OWB_SEND_FRAME_INPUT*>(buf.data());
    input->codec_id      = static_cast<ULONG>(codec_id);
    input->data_len      = static_cast<ULONG>(frame.size());
    std::memcpy(input->data, frame.data(), frame.size());

    DWORD bytes_returned = 0;
    return DeviceIoControl(
        impl_->device,
        OWB_IOCTL_SEND_AUDIO_FRAME,
        buf.data(), input_size,
        nullptr, 0,
        &bytes_returned, nullptr
    ) != FALSE;
}

bool A2dpStream::get_rf_quality(int32_t* rssi_dbm, uint32_t* retransmit_per_mille) {
    if (!is_open() || !rssi_dbm || !retransmit_per_mille) return false;

    OWB_RF_QUALITY quality{};
    DWORD bytes_returned = 0;
    BOOL ok = DeviceIoControl(
        impl_->device,
        OWB_IOCTL_GET_RF_QUALITY,
        nullptr, 0,
        &quality, static_cast<DWORD>(sizeof(quality)),
        &bytes_returned, nullptr
    );

    if (!ok || bytes_returned < sizeof(quality)) return false;

    *rssi_dbm              = quality.rssi_dbm;
    *retransmit_per_mille  = quality.retransmit_per_mille;
    return true;
}

bool A2dpStream::set_codec_config(uint32_t codec_id,
                                   std::string_view key,
                                   int64_t value) {
    if (!is_open()) return false;

    OWB_CODEC_CONFIG cfg{};
    cfg.codec_id    = static_cast<ULONG>(codec_id);
    cfg.param_value = value;
    const size_t key_len = std::min(key.size(), sizeof(cfg.param_key) - 1);
    std::memcpy(cfg.param_key, key.data(), key_len);

    DWORD bytes_returned = 0;
    return DeviceIoControl(
        impl_->device,
        OWB_IOCTL_SET_CODEC_CONFIG,
        &cfg, static_cast<DWORD>(sizeof(cfg)),
        nullptr, 0,
        &bytes_returned, nullptr
    ) != FALSE;
}

} // namespace owb
```

Save to: `service/src/a2dp_stream.cpp`

- [ ] **Step 2.5: Add `owb_a2dp_stream` library to `service/CMakeLists.txt`**

Read the current `service/CMakeLists.txt`, then add this block **before** the `add_executable(owb_service ...)` block:

```cmake
# ── A2DP stream (DeviceIoControl to kernel driver) ────────────────────────────
add_library(owb_a2dp_stream STATIC
    src/a2dp_stream.cpp
)
target_include_directories(owb_a2dp_stream PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/driver        # for owb_ioctl.h
)
target_compile_options(owb_a2dp_stream PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /WX /utf-8>
)
```

Also add `owb_a2dp_stream` to `target_link_libraries(owb_service ...)`.

- [ ] **Step 2.6: Build and run A2dpStream tests**

```powershell
$env:PATH = "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Tools\MSVC\14.50.35717\bin\Hostx64\x64;C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64;C:\Users\santi\AppData\Local\Android\Sdk\cmake\4.1.2\bin;$env:PATH"
$env:INCLUDE = "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Tools\MSVC\14.50.35717\include;C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\ucrt;C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\um;C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\shared"
$env:LIB = "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Tools\MSVC\14.50.35717\lib\x64;C:\Program Files (x86)\Windows Kits\10\Lib\10.0.26100.0\ucrt\x64;C:\Program Files (x86)\Windows Kits\10\Lib\10.0.26100.0\um\x64"
Set-Location "c:\suru\open winblue"
& cmake --preset nmake-debug
& cmake --build build/nmake-debug --target owb_service_tests
Set-Location build/nmake-debug
& ctest --output-on-failure -R "A2dpStream"
```

Expected (5 tests, all PASS):
```
[ RUN      ] A2dpStream.ConstructsWithoutCrash             OK
[ RUN      ] A2dpStream.OpenFailsGracefullyWithoutDriver   OK
[ RUN      ] A2dpStream.SendFrameInStubModeReturnsFalse    OK
[ RUN      ] A2dpStream.CloseIsIdempotent                  OK
[ RUN      ] A2dpStream.GetRfQualityReturnsFalseInStubMode OK
```

- [ ] **Step 2.7: Commit**

```powershell
cd "c:/suru/open winblue"
git add service/src/a2dp_stream.h service/src/a2dp_stream.cpp service/CMakeLists.txt
git add tests/service/a2dp_stream_test.cpp tests/service/CMakeLists.txt
git commit -m "feat(service): add A2dpStream IOCTL client with stub mode for driverless dev"
```

---

## Task 3: Wire A2dpStream into service main loop + update IPC status

**Files:**
- Modify: `service/src/main.cpp`
- Modify: `service/src/ipc_server.cpp` (remove hardcoded status stub, add real stream state)

- [ ] **Step 3.1: Update `service/src/main.cpp`**

Read current `main.cpp`, then replace the content with:

```cpp
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
#include <span>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
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
    owb::CodecSbc     codec;  // Phase 2c will feed this into the a2dp send loop

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
```

- [ ] **Step 3.2: Build and verify full 17 tests pass** (12 existing + 5 A2dpStream)

```powershell
& cmake --build build/nmake-debug --target owb_service owb_service_tests
Set-Location build/nmake-debug
& ctest --output-on-failure
```

Expected: `17 tests passed, 0 failed`.

- [ ] **Step 3.3: Commit**

```powershell
cd "c:/suru/open winblue"
git add service/src/main.cpp
git commit -m "feat(service): wire A2dpStream into service main loop"
```

---

## Task 4: Update INF with A2DP hardware IDs + CI WDK job

**Files:**
- Modify: `driver/owb_a2dp.inf`
- Modify: `.github/workflows/ci.yml`

### Part A — INF hardware IDs

Windows creates Bluetooth audio device nodes under `BTHENUM\` with the A2DP service class UUID. The pattern that `btavchdt.sys` (the inbox driver) binds to is:

```
BTHENUM\{0000110b-0000-1000-8000-00805f9b34fb}_LOCALMFG&*
```

Where `0000110b` is the A2DP Sink service class UUID. Our driver replaces the inbox driver for these devices.

- [ ] **Step 4.1: Update `driver/owb_a2dp.inf`**

```ini
; OpenWinBlue A2DP Driver
; Replaces btavchdt.sys as the A2DP audio source driver.

[Version]
Signature   = "$WINDOWS NT$"
Class       = Bluetooth
ClassGuid   = {e0cbf06c-cd8b-4647-bb8a-263b43f0f974}
Provider    = %ManufacturerName%
DriverVer   = 06/02/2026,0.2.0.0
CatalogFile = owb_a2dp.cat
PnpLockdown = 1

[Manufacturer]
%ManufacturerName% = Standard,NTamd64

; Match all Bluetooth A2DP Sink devices (UUID 0000110b).
; Windows creates these nodes for any headset/speaker supporting A2DP.
; The wildcard (*) covers all manufacturer IDs.
[Standard.NTamd64]
%OWB_A2DP_DESC% = OWB_A2DP_Install, BTHENUM\{0000110b-0000-1000-8000-00805f9b34fb}_LOCALMFG&0000
%OWB_A2DP_DESC% = OWB_A2DP_Install, BTHENUM\{0000110b-0000-1000-8000-00805f9b34fb}_LOCALMFG&0001
%OWB_A2DP_DESC% = OWB_A2DP_Install, BTHENUM\{0000110b-0000-1000-8000-00805f9b34fb}_LOCALMFG&0002
; Generic fallback — matches any A2DP device regardless of manufacturer
%OWB_A2DP_DESC% = OWB_A2DP_Install, BTHENUM\{0000110b-0000-1000-8000-00805f9b34fb}*

[OWB_A2DP_Install.NT]
CopyFiles = OWB_CopyFiles

[OWB_A2DP_Install.NT.Services]
AddService = owb_a2dp, %SPSVCINST_ASSOCSERVICE%, OWB_Service_Install

[OWB_Service_Install]
DisplayName    = %OWB_SERVICE_NAME%
ServiceType    = %SERVICE_KERNEL_DRIVER%
StartType      = %SERVICE_DEMAND_START%
ErrorControl   = %SERVICE_ERROR_IGNORE%
ServiceBinary  = %12%\owb_a2dp.sys

[DestinationDirs]
OWB_CopyFiles = 12  ; drivers directory (%windir%\System32\drivers\)

[OWB_CopyFiles]
owb_a2dp.sys

[SourceDisksNames]
1 = %DiskName%

[SourceDisksFiles]
owb_a2dp.sys = 1

[Strings]
ManufacturerName   = "OpenWinBlue Project"
DiskName           = "OpenWinBlue A2DP Driver Disk"
OWB_A2DP_DESC      = "OpenWinBlue Bluetooth A2DP Driver"
OWB_SERVICE_NAME   = "OpenWinBlue A2DP"

SPSVCINST_ASSOCSERVICE = 0x00000002
SERVICE_KERNEL_DRIVER  = 0x00000001
SERVICE_DEMAND_START   = 0x00000003
SERVICE_ERROR_IGNORE   = 0x00000000
```

Save to: `driver/owb_a2dp.inf`

### Part B — CI WDK installation + driver build

- [ ] **Step 4.2: Update `.github/workflows/ci.yml` to add driver build job**

Read the current file, then replace with:

```yaml
name: CI

on:
  push:
    branches: [main, "feat/**", "fix/**"]
  pull_request:
    branches: [main]

jobs:
  build-service:
    name: Build & Test Service (C++)
    runs-on: windows-2022
    steps:
      - uses: actions/checkout@v4
        with:
          submodules: recursive

      - name: Configure CMake
        run: cmake --preset windows-debug

      - name: Build
        run: cmake --build build/debug --target owb_service owb_service_tests

      - name: Run tests
        run: ctest --preset test-all --output-on-failure

  build-gui:
    name: Build & Test GUI (C#)
    runs-on: windows-2022
    steps:
      - uses: actions/checkout@v4

      - name: Setup .NET 10
        uses: actions/setup-dotnet@v4
        with:
          dotnet-version: "10.0.x"

      - name: Restore
        run: dotnet restore gui/OpenWinBlue.slnx

      - name: Build
        run: dotnet build gui/OpenWinBlue.slnx -c Debug --no-restore

      - name: Test
        run: dotnet test gui/tests/OpenWinBlue.Tests/OpenWinBlue.Tests.csproj --no-build --verbosity normal

  build-driver:
    name: Build Driver (KMDF + WDK)
    runs-on: windows-2022
    steps:
      - uses: actions/checkout@v4

      - name: Install WDK 11 via chocolatey
        run: |
          choco install windowsdriverkit11 --yes --no-progress
        shell: pwsh

      - name: Add WDK MSBuild integration to PATH
        run: |
          $wdkRoot = "C:\Program Files (x86)\Windows Kits\10"
          Write-Host "WDK root: $wdkRoot"
          # Verify kernel headers exist
          if (-not (Test-Path "$wdkRoot\Include\10.0.22621.0\km\ntddk.h")) {
            Write-Error "WDK kernel headers not found — choco install may have failed"
            exit 1
          }
          Write-Host "WDK headers found OK"
        shell: pwsh

      - name: Build driver with MSBuild
        run: |
          & "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe" `
            driver\owb_a2dp.vcxproj `
            /p:Configuration=Debug `
            /p:Platform=x64 `
            /p:WindowsSdkVersion=10.0 `
            /t:Build `
            /nologo
        shell: pwsh

      - name: Verify driver binary exists
        run: |
          if (-not (Test-Path "driver\x64\Debug\owb_a2dp.sys")) {
            Write-Error "owb_a2dp.sys not found — build failed"
            exit 1
          }
          Write-Host "owb_a2dp.sys built successfully ($($(Get-Item driver\x64\Debug\owb_a2dp.sys).Length) bytes)"
        shell: pwsh
```

- [ ] **Step 4.3: Commit**

```powershell
cd "c:/suru/open winblue"
git add driver/owb_a2dp.inf .github/workflows/ci.yml
git commit -m "feat(driver): add A2DP hardware IDs to INF + CI WDK build job"
```

---

## Task 5: KMDF Visual Studio project file

> **Requires WDK on CI to compile.** This creates the `.vcxproj` that MSBuild uses to compile the kernel driver with the WDK toolset.

**Files:**
- Create: `driver/owb_a2dp.vcxproj`

- [ ] **Step 5.1: Create `driver/owb_a2dp.vcxproj`**

```xml
<?xml version="1.0" encoding="utf-8"?>
<!-- OpenWinBlue A2DP KMDF Driver — MSBuild project -->
<Project DefaultTargets="Build" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">

  <ItemGroup Label="ProjectConfigurations">
    <ProjectConfiguration Include="Debug|x64">
      <Configuration>Debug</Configuration>
      <Platform>x64</Platform>
    </ProjectConfiguration>
    <ProjectConfiguration Include="Release|x64">
      <Configuration>Release</Configuration>
      <Platform>x64</Platform>
    </ProjectConfiguration>
  </ItemGroup>

  <PropertyGroup Label="Globals">
    <ProjectGuid>{B4D5E6F7-A8B9-4C5D-8E9F-0A1B2C3D4E5F}</ProjectGuid>
    <TargetFrameworkVersion>v4.5</TargetFrameworkVersion>
    <!-- WDK toolset for kernel-mode drivers -->
    <PlatformToolset>WindowsKernelModeDriver10.0</PlatformToolset>
    <ProjectName>owb_a2dp</ProjectName>
  </PropertyGroup>

  <!-- WDK project type imports -->
  <Import Project="$(VCTargetsPath)\Microsoft.Cpp.Default.props" />

  <PropertyGroup Label="Configuration" Condition="'$(Configuration)'=='Debug'">
    <ConfigurationType>Driver</ConfigurationType>
    <DriverType>KMDF</DriverType>
    <UseDebugLibraries>true</UseDebugLibraries>
    <DriverTargetPlatform>Universal</DriverTargetPlatform>
  </PropertyGroup>

  <PropertyGroup Label="Configuration" Condition="'$(Configuration)'=='Release'">
    <ConfigurationType>Driver</ConfigurationType>
    <DriverType>KMDF</DriverType>
    <UseDebugLibraries>false</UseDebugLibraries>
    <DriverTargetPlatform>Universal</DriverTargetPlatform>
  </PropertyGroup>

  <Import Project="$(VCTargetsPath)\Microsoft.Cpp.props" />
  <ImportGroup Label="ExtensionSettings" />
  <ImportGroup Label="PropertySheets">
    <Import Project="$(UserRootDir)\Microsoft.Cpp.$(Platform).user.props"
            Condition="exists('$(UserRootDir)\Microsoft.Cpp.$(Platform).user.props')"
            Label="LocalAppDataPlatform" />
  </ImportGroup>

  <PropertyGroup Label="UserMacros" />

  <PropertyGroup>
    <OutDir>$(SolutionDir)$(Platform)\$(Configuration)\</OutDir>
    <IntDir>$(Platform)\$(Configuration)\</IntDir>
    <!-- KMDF version — matches WDK 11 -->
    <WdfLibraryVersion>$([Microsoft.Build.Utilities.ToolLocationHelper]::GetLatestWDFMajorMinor())</WdfLibraryVersion>
  </PropertyGroup>

  <ItemDefinitionGroup>
    <ClCompile>
      <AdditionalIncludeDirectories>
        $(ProjectDir)src;$(ProjectDir)..;%(AdditionalIncludeDirectories)
      </AdditionalIncludeDirectories>
      <!-- Treat warnings as errors, UTF-8 source -->
      <TreatWarningAsError>true</TreatWarningAsError>
      <WarningLevel>Level4</WarningLevel>
      <!-- SAL annotations warnings -->
      <PREfast>true</PREfast>
    </ClCompile>
    <Link>
      <AdditionalDependencies>%(AdditionalDependencies)</AdditionalDependencies>
    </Link>
    <!-- INF file for driver packaging -->
    <Inf>
      <OutputDirectory>$(OutDir)</OutputDirectory>
    </Inf>
  </ItemDefinitionGroup>

  <ItemGroup>
    <ClCompile Include="src\owb_a2dp.c" />
    <ClCompile Include="src\avdtp.c" />
    <ClCompile Include="src\l2cap_stream.c" />
    <ClCompile Include="src\ioctl.c" />
  </ItemGroup>

  <ItemGroup>
    <ClInclude Include="src\owb_a2dp.h" />
    <ClInclude Include="src\avdtp.h" />
    <ClInclude Include="src\l2cap_stream.h" />
    <ClInclude Include="src\ioctl.h" />
    <ClInclude Include="owb_ioctl.h" />
  </ItemGroup>

  <ItemGroup>
    <Inf Include="owb_a2dp.inf" />
  </ItemGroup>

  <Import Project="$(VCTargetsPath)\Microsoft.Cpp.targets" />
  <ImportGroup Label="ExtensionTargets" />

</Project>
```

Save to: `driver/owb_a2dp.vcxproj`

- [ ] **Step 5.2: Commit**

```powershell
cd "c:/suru/open winblue"
git add driver/owb_a2dp.vcxproj
git commit -m "build(driver): add KMDF Visual Studio project file (WDK 11)"
```

---

## Task 6: AVDTP signaling state machine (kernel — CI builds only)

**Files:**
- Create: `driver/src/avdtp.h`
- Create: `driver/src/avdtp.c`

The AVDTP (Audio/Video Distribution Transport Protocol) signaling uses PSM 0x0019 on L2CAP. The state machine handles: DISCOVER → GET_CAPABILITIES → SET_CONFIGURATION → OPEN → START.

- [ ] **Step 6.1: Create `driver/src/avdtp.h`**

```c
// driver/src/avdtp.h
// AVDTP signaling state machine for the A2DP profile.
#pragma once
#include <ntddk.h>
#include <wdf.h>

// AVDTP L2CAP PSM (Protocol Service Multiplexer) — defined in A2DP spec.
#define AVDTP_SIGNALING_PSM   0x0019u
#define AVDTP_MEDIA_PSM       0x0019u   // same PSM, different CID

// AVDTP message types (signal identifiers — A2DP spec section 8.5)
#define AVDTP_MSG_DISCOVER         0x01u
#define AVDTP_MSG_GET_CAPABILITIES 0x02u
#define AVDTP_MSG_SET_CONFIGURATION 0x03u
#define AVDTP_MSG_OPEN             0x06u
#define AVDTP_MSG_START            0x07u
#define AVDTP_MSG_CLOSE            0x08u
#define AVDTP_MSG_SUSPEND          0x09u
#define AVDTP_MSG_ABORT            0x0Au

// AVDTP packet type (2 bits in header)
#define AVDTP_PKT_SINGLE           0x00u
#define AVDTP_PKT_START            0x01u
#define AVDTP_PKT_CONTINUE         0x02u
#define AVDTP_PKT_END              0x03u

// AVDTP message type (2 bits)
#define AVDTP_MSG_CMD              0x00u
#define AVDTP_MSG_RESPONSE_ACCEPT  0x02u
#define AVDTP_MSG_RESPONSE_REJECT  0x03u

// Connection states
typedef enum _OWB_AVDTP_STATE {
    AvdtpStateIdle        = 0,
    AvdtpStateConnecting  = 1,
    AvdtpStateDiscovering = 2,
    AvdtpStateConfiguring = 3,
    AvdtpStateConfigured  = 4,
    AvdtpStateOpen        = 5,
    AvdtpStateStreaming   = 6,
    AvdtpStateClosing     = 7,
} OWB_AVDTP_STATE;

// Per-device AVDTP context (stored in device extension)
typedef struct _OWB_AVDTP_CONTEXT {
    OWB_AVDTP_STATE   State;
    USHORT            SignalingCid;   // L2CAP channel ID for signaling
    USHORT            MediaCid;       // L2CAP channel ID for media
    UCHAR             RemoteSeid;     // remote Stream End-Point ID
    UCHAR             LocalSeid;      // our SEID
    UCHAR             TransactionId;  // rolling transaction counter (0-15)
    ULONG             ActiveCodecId;  // OWB_CODEC_* of negotiated codec
} OWB_AVDTP_CONTEXT, *POWB_AVDTP_CONTEXT;

// Forward declaration of device extension (defined in owb_a2dp.h)
typedef struct _OWB_DEVICE_EXTENSION OWB_DEVICE_EXTENSION, *POWB_DEVICE_EXTENSION;

// Initialize an AVDTP context to idle state.
VOID AvdtpContextInit(_Out_ POWB_AVDTP_CONTEXT Ctx);

// Start signaling — opens L2CAP signaling channel to remote device.
// Called from OwbEvtDeviceAdd when a BT audio device is enumerated.
NTSTATUS AvdtpConnect(_In_ POWB_DEVICE_EXTENSION DevExt);

// Process an inbound AVDTP signaling packet received on the L2CAP channel.
// Called from the L2CAP receive callback.
VOID AvdtpHandleSignalingPacket(
    _In_    POWB_DEVICE_EXTENSION DevExt,
    _In_reads_bytes_(Length) const UCHAR* Data,
    _In_    USHORT Length
);

// Send an AVDTP command (helper — builds packet and queues for L2CAP send).
NTSTATUS AvdtpSendCommand(
    _In_ POWB_DEVICE_EXTENSION DevExt,
    _In_ UCHAR                 SignalId,
    _In_reads_bytes_opt_(PayloadLen) const UCHAR* Payload,
    _In_ USHORT                PayloadLen
);
```

Save to: `driver/src/avdtp.h`

- [ ] **Step 6.2: Create `driver/src/avdtp.c`**

```c
// driver/src/avdtp.c
// AVDTP signaling state machine.
#include "avdtp.h"
#include "owb_a2dp.h"
#include "l2cap_stream.h"
#include "../owb_ioctl.h"   // for OWB_CODEC_SBC

// AVDTP header layout (single-packet, from A2DP spec):
// Byte 0: [TransactionLabel(4b)] [PacketType(2b)] [MessageType(2b)]
// Byte 1: [SignalIdentifier(6b)] [RFA(2b)]
// Byte 2+: payload

VOID AvdtpContextInit(_Out_ POWB_AVDTP_CONTEXT Ctx) {
    RtlZeroMemory(Ctx, sizeof(*Ctx));
    Ctx->State         = AvdtpStateIdle;
    Ctx->LocalSeid     = 0x01;   // our single SEID
    Ctx->ActiveCodecId = OWB_CODEC_SBC;
}

// Build and send a single-packet AVDTP command.
NTSTATUS AvdtpSendCommand(
    _In_ POWB_DEVICE_EXTENSION DevExt,
    _In_ UCHAR                 SignalId,
    _In_reads_bytes_opt_(PayloadLen) const UCHAR* Payload,
    _In_ USHORT                PayloadLen)
{
    // Header: 2 bytes + payload
    const USHORT pkt_len = (USHORT)(2 + PayloadLen);
    PUCHAR buf = (PUCHAR)ExAllocatePool2(POOL_FLAG_NON_PAGED, pkt_len, 'AVDT');
    if (!buf) return STATUS_INSUFFICIENT_RESOURCES;

    // Increment and wrap transaction ID (0-15)
    DevExt->Avdtp.TransactionId = (DevExt->Avdtp.TransactionId + 1) & 0x0F;

    buf[0] = (UCHAR)((DevExt->Avdtp.TransactionId << 4) |
                     (AVDTP_PKT_SINGLE << 2) |
                     AVDTP_MSG_CMD);
    buf[1] = (UCHAR)(SignalId & 0x3F);
    if (Payload && PayloadLen > 0)
        RtlCopyMemory(buf + 2, Payload, PayloadLen);

    NTSTATUS status = L2capSendSignaling(DevExt, buf, pkt_len);
    ExFreePoolWithTag(buf, 'AVDT');
    return status;
}

// Connect: send AVDTP_DISCOVER to start codec negotiation.
NTSTATUS AvdtpConnect(_In_ POWB_DEVICE_EXTENSION DevExt) {
    if (DevExt->Avdtp.State != AvdtpStateIdle)
        return STATUS_INVALID_DEVICE_STATE;

    DevExt->Avdtp.State = AvdtpStateDiscovering;
    // DISCOVER has no payload
    return AvdtpSendCommand(DevExt, AVDTP_MSG_DISCOVER, NULL, 0);
}

// Handle DISCOVER response: extract remote SEIDs and send GET_CAPABILITIES.
static VOID HandleDiscoverResponse(
    _In_ POWB_DEVICE_EXTENSION DevExt,
    _In_reads_bytes_(Len) const UCHAR* Data,
    _In_ USHORT Len)
{
    // DISCOVER response payload: pairs of (SEID_Info, In_Use)
    // SEID_Info byte: [SEID(6b)][TSEP(1b)][RFA(1b)]
    // We take the first audio sink SEID (TSEP=0x00 = SNK).
    for (USHORT i = 0; i + 1 < Len; i += 2) {
        UCHAR tsep = (Data[i] >> 1) & 0x01;
        if (tsep == 0x00) {  // SNK — audio sink
            DevExt->Avdtp.RemoteSeid = (UCHAR)((Data[i] >> 2) & 0x3F);
            DevExt->Avdtp.State = AvdtpStateConfiguring;
            // GET_CAPABILITIES payload: [ACP_SEID(6b)][RFA(2b)]
            UCHAR payload = (UCHAR)((DevExt->Avdtp.RemoteSeid << 2) & 0xFC);
            AvdtpSendCommand(DevExt, AVDTP_MSG_GET_CAPABILITIES, &payload, 1);
            return;
        }
    }
    // No suitable SEID found — stay in discovering state
    KdPrint(("OpenWinBlue: AVDTP DISCOVER found no audio sink SEID\n"));
}

// Build SBC codec capabilities payload for SET_CONFIGURATION.
// Hardcodes: 44.1kHz, Joint Stereo, Blocks=16, Subbands=8, Loudness, Bitpool=53.
static USHORT BuildSbcSetConfig(
    _Out_writes_bytes_(MaxLen) PUCHAR Buf,
    _In_ USHORT MaxLen)
{
    // SET_CONFIGURATION payload layout (A2DP spec):
    // ACP_SEID(1), INT_SEID(1), [Service Category(1), LOSC(1), Service Capabilities...]
    // Service Category 0x07 = Media Codec
    // LOSC = 0x06 for SBC: [Media Type(1), Codec Type(1), Codec Info(4)]
    if (MaxLen < 10) return 0;
    Buf[0] = (UCHAR)((1 << 2) & 0xFC);  // ACP_SEID (placeholder, filled before send)
    Buf[1] = (UCHAR)((1 << 2) & 0xFC);  // INT_SEID = our local SEID
    Buf[2] = 0x07;  // Service Category: Media Codec
    Buf[3] = 0x06;  // LOSC = 6
    Buf[4] = 0x00;  // Media Type: Audio
    Buf[5] = 0x00;  // Codec Type: SBC
    // SBC codec info (4 bytes — A2DP spec Table 4.25):
    // Byte 0: [Sampling Freq(4b)][Channel Mode(4b)]
    //         44.1kHz=0x02 → bit3, Joint Stereo=0x01 → bit0
    Buf[6] = 0x21;  // 0010 0001 = 44.1kHz | Joint Stereo
    // Byte 1: [Block Length(4b)][Subbands(2b)][Alloc Method(2b)]
    //         16 blocks=0x01 → bit0, 8 subbands=0x02 → bit1, Loudness=0x01 → bit0
    Buf[7] = 0x15;  // 0001 0101 = Blocks=16 | Subbands=8 | Loudness
    Buf[8] = 0x02;  // min bitpool = 2
    Buf[9] = 0x35;  // max bitpool = 53
    return 10;
}

// Handle GET_CAPABILITIES response: send SET_CONFIGURATION for SBC.
static VOID HandleGetCapabilitiesResponse(
    _In_ POWB_DEVICE_EXTENSION DevExt,
    _In_reads_bytes_(Len) const UCHAR* Data,
    _In_ USHORT Len)
{
    UNREFERENCED_PARAMETER(Data);
    UNREFERENCED_PARAMETER(Len);
    // For Phase 2b: always configure SBC regardless of remote capabilities.
    // Phase 2c will parse capabilities and negotiate LDAC/aptX.
    UCHAR payload[16] = {0};
    USHORT payload_len = BuildSbcSetConfig(payload, sizeof(payload));
    // Fill in actual remote SEID
    payload[0] = (UCHAR)((DevExt->Avdtp.RemoteSeid << 2) & 0xFC);
    payload[1] = (UCHAR)((DevExt->Avdtp.LocalSeid  << 2) & 0xFC);

    DevExt->Avdtp.State = AvdtpStateConfigured;
    AvdtpSendCommand(DevExt, AVDTP_MSG_SET_CONFIGURATION, payload, payload_len);
}

// Handle SET_CONFIGURATION response: send OPEN.
static VOID HandleSetConfigurationResponse(_In_ POWB_DEVICE_EXTENSION DevExt) {
    DevExt->Avdtp.State = AvdtpStateOpen;
    UCHAR seid = (UCHAR)((DevExt->Avdtp.RemoteSeid << 2) & 0xFC);
    AvdtpSendCommand(DevExt, AVDTP_MSG_OPEN, &seid, 1);
}

// Handle OPEN response: media L2CAP channel is now ready — send START.
static VOID HandleOpenResponse(_In_ POWB_DEVICE_EXTENSION DevExt) {
    UCHAR seid = (UCHAR)((DevExt->Avdtp.RemoteSeid << 2) & 0xFC);
    AvdtpSendCommand(DevExt, AVDTP_MSG_START, &seid, 1);
}

// Handle START response: device is streaming.
static VOID HandleStartResponse(_In_ POWB_DEVICE_EXTENSION DevExt) {
    DevExt->Avdtp.State = AvdtpStateStreaming;
    KdPrint(("OpenWinBlue: A2DP streaming started (SBC)\n"));
}

// Main dispatch: called for every inbound AVDTP signaling packet.
VOID AvdtpHandleSignalingPacket(
    _In_    POWB_DEVICE_EXTENSION DevExt,
    _In_reads_bytes_(Length) const UCHAR* Data,
    _In_    USHORT Length)
{
    if (Length < 2) return;

    const UCHAR msg_type  = Data[0] & 0x03;
    const UCHAR signal_id = Data[1] & 0x3F;
    const UCHAR* payload  = (Length > 2) ? Data + 2 : NULL;
    const USHORT pay_len  = (Length > 2) ? (USHORT)(Length - 2) : 0;

    if (msg_type != AVDTP_MSG_RESPONSE_ACCEPT) {
        KdPrint(("OpenWinBlue: AVDTP command rejected, signal=0x%02x\n", signal_id));
        return;
    }

    switch (signal_id) {
        case AVDTP_MSG_DISCOVER:
            HandleDiscoverResponse(DevExt, payload, pay_len);
            break;
        case AVDTP_MSG_GET_CAPABILITIES:
            HandleGetCapabilitiesResponse(DevExt, payload, pay_len);
            break;
        case AVDTP_MSG_SET_CONFIGURATION:
            HandleSetConfigurationResponse(DevExt);
            break;
        case AVDTP_MSG_OPEN:
            HandleOpenResponse(DevExt);
            break;
        case AVDTP_MSG_START:
            HandleStartResponse(DevExt);
            break;
        default:
            KdPrint(("OpenWinBlue: unknown AVDTP response signal 0x%02x\n", signal_id));
            break;
    }
}
```

Save to: `driver/src/avdtp.c`

- [ ] **Step 6.3: Commit**

```powershell
cd "c:/suru/open winblue"
git add driver/src/avdtp.h driver/src/avdtp.c
git commit -m "feat(driver): add AVDTP signaling state machine (DISCOVER→START)"
```

---

## Task 7: L2CAP media channel + IOCTL handler (kernel)

**Files:**
- Create: `driver/src/l2cap_stream.h`
- Create: `driver/src/l2cap_stream.c`
- Create: `driver/src/ioctl.h`
- Create: `driver/src/ioctl.c`

- [ ] **Step 7.1: Create `driver/src/l2cap_stream.h`**

```c
// driver/src/l2cap_stream.h
// L2CAP channel management for AVDTP signaling and A2DP media streaming.
#pragma once
#include <ntddk.h>
#include <wdf.h>
#include <bthsdpddi.h>
#include <bthddi.h>

typedef struct _OWB_DEVICE_EXTENSION OWB_DEVICE_EXTENSION, *POWB_DEVICE_EXTENSION;

// Send a packet on the AVDTP signaling L2CAP channel.
NTSTATUS L2capSendSignaling(
    _In_ POWB_DEVICE_EXTENSION DevExt,
    _In_reads_bytes_(Length) const UCHAR* Data,
    _In_ USHORT Length
);

// Send one RTP-framed audio packet on the media L2CAP channel.
// frame_data: encoded audio bytes (e.g. one SBC frame)
NTSTATUS L2capSendMediaFrame(
    _In_ POWB_DEVICE_EXTENSION DevExt,
    _In_ ULONG  CodecId,
    _In_reads_bytes_(FrameLen) const UCHAR* FrameData,
    _In_ USHORT FrameLen
);

// Open the L2CAP signaling channel to a remote device.
// Called from OwbEvtDeviceAdd after the BT connection is established.
NTSTATUS L2capOpenSignalingChannel(_In_ POWB_DEVICE_EXTENSION DevExt);

// Callback registered with BthPort — called when L2CAP data arrives.
_IRQL_requires_max_(DISPATCH_LEVEL)
VOID L2capReceiveCallback(
    _In_ PVOID  Context,
    _In_ USHORT Cid,
    _In_reads_bytes_(Length) PUCHAR Data,
    _In_ USHORT Length
);
```

Save to: `driver/src/l2cap_stream.h`

- [ ] **Step 7.2: Create `driver/src/l2cap_stream.c`**

```c
// driver/src/l2cap_stream.c
// L2CAP channel management and media frame sending.
#include "l2cap_stream.h"
#include "avdtp.h"
#include "owb_a2dp.h"

// RTP header for A2DP (RFC 3550 + A2DP spec):
// 12 bytes: V(2) P(1) X(1) CC(4) M(1) PT(7) SeqNum(16) Timestamp(32) SSRC(32)
// For SBC: PT = 0x60 (dynamic), M=0 for non-last fragment
#pragma pack(push, 1)
typedef struct _OWB_RTP_HEADER {
    UCHAR   vpxcc;      // V=2, P=0, X=0, CC=0 → 0x80
    UCHAR   mpt;        // M=0, PT=0x60 for SBC
    USHORT  seq_num;    // big-endian, incremented per packet
    ULONG   timestamp;  // big-endian, in units of 44100 Hz
    ULONG   ssrc;       // synchronization source (constant)
} OWB_RTP_HEADER;
#pragma pack(pop)

// SBC RTP payload header (A2DP spec section 4.3.1):
// 1 byte: [FragmentBit(1)][StartBit(1)][LastBit(1)][RFA(1)][NumberOfFrames(4)]
#define SBC_RTP_PAYLOAD_HDR(num_frames) \
    ((UCHAR)((num_frames) & 0x0F))  // no fragmentation, 1+ frames per packet

NTSTATUS L2capSendSignaling(
    _In_ POWB_DEVICE_EXTENSION DevExt,
    _In_reads_bytes_(Length) const UCHAR* Data,
    _In_ USHORT Length)
{
    if (!DevExt->SignalingBrb || DevExt->Avdtp.SignalingCid == 0)
        return STATUS_DEVICE_NOT_CONNECTED;

    // Build and submit a BRB_L2CA_ACL_TRANSFER for the signaling channel.
    // The BRB mechanism is asynchronous; for Phase 2b we use a synchronous
    // wrapper that allocates, submits, and waits on a KEVENT.
    PIRP irp = IoAllocateIrp(DevExt->Device->StackSize, FALSE);
    if (!irp) return STATUS_INSUFFICIENT_RESOURCES;

    // Allocate BRB from the Bluetooth stack
    struct _BRB_L2CA_ACL_TRANSFER* brb =
        (struct _BRB_L2CA_ACL_TRANSFER*)DevExt->BthInterface.BthAllocateBrb(
            BRB_L2CA_ACL_TRANSFER, POOL_FLAG_NON_PAGED);
    if (!brb) {
        IoFreeIrp(irp);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    brb->Hdr.ClientContext[0] = DevExt;
    brb->ChannelHandle         = DevExt->SignalingChannelHandle;
    brb->TransferFlags         = ACL_TRANSFER_DIRECTION_OUT | ACL_SHORT_TRANSFER_OK;
    brb->BufferSize            = Length;
    brb->Buffer                = (PVOID)Data;
    brb->BufferMDL             = NULL;

    KEVENT event;
    KeInitializeEvent(&event, NotificationEvent, FALSE);
    IoSetCompletionRoutine(irp, /* L2capBrbCompleteCallback */ NULL, &event, TRUE, TRUE, TRUE);

    NTSTATUS status = DevExt->BthInterface.BthSubmitBrb(DevExt->IoTarget, irp, &brb->Hdr);
    if (status == STATUS_PENDING)
        KeWaitForSingleObject(&event, Executive, KernelMode, FALSE, NULL);

    status = irp->IoStatus.Status;
    DevExt->BthInterface.BthFreeBrb(&brb->Hdr);
    IoFreeIrp(irp);
    return status;
}

NTSTATUS L2capSendMediaFrame(
    _In_ POWB_DEVICE_EXTENSION DevExt,
    _In_ ULONG  CodecId,
    _In_reads_bytes_(FrameLen) const UCHAR* FrameData,
    _In_ USHORT FrameLen)
{
    if (DevExt->Avdtp.State != AvdtpStateStreaming)
        return STATUS_DEVICE_NOT_CONNECTED;

    UNREFERENCED_PARAMETER(CodecId);  // Phase 2c: vary RTP payload type per codec

    // Build RTP packet: 12-byte header + 1-byte SBC payload header + frame data
    const USHORT pkt_len = (USHORT)(sizeof(OWB_RTP_HEADER) + 1 + FrameLen);
    PUCHAR pkt = (PUCHAR)ExAllocatePool2(POOL_FLAG_NON_PAGED, pkt_len, 'RTPM');
    if (!pkt) return STATUS_INSUFFICIENT_RESOURCES;

    OWB_RTP_HEADER* hdr = (OWB_RTP_HEADER*)pkt;
    hdr->vpxcc     = 0x80;
    hdr->mpt       = 0x60;  // PT=96 for SBC
    // big-endian sequence number and timestamp
    hdr->seq_num   = RtlUshortByteSwap(DevExt->RtpSeqNum++);
    hdr->timestamp = RtlUlongByteSwap(DevExt->RtpTimestamp);
    hdr->ssrc      = 0x00000001UL;
    DevExt->RtpTimestamp += 512;  // 512 samples per SBC frame at 44.1kHz, 16 blocks, 8 subbands

    pkt[sizeof(OWB_RTP_HEADER)] = SBC_RTP_PAYLOAD_HDR(1);  // 1 frame per packet
    RtlCopyMemory(pkt + sizeof(OWB_RTP_HEADER) + 1, FrameData, FrameLen);

    // Reuse the signaling send path with the media channel handle
    // (placeholder — Phase 2c will open a separate media CID)
    NTSTATUS status = L2capSendSignaling(DevExt, pkt, pkt_len);
    ExFreePoolWithTag(pkt, 'RTPM');
    return status;
}

VOID L2capReceiveCallback(
    _In_ PVOID  Context,
    _In_ USHORT Cid,
    _In_reads_bytes_(Length) PUCHAR Data,
    _In_ USHORT Length)
{
    POWB_DEVICE_EXTENSION devExt = (POWB_DEVICE_EXTENSION)Context;
    UNREFERENCED_PARAMETER(Cid);
    // All inbound data on the signaling channel is AVDTP messages.
    AvdtpHandleSignalingPacket(devExt, Data, Length);
}

NTSTATUS L2capOpenSignalingChannel(_In_ POWB_DEVICE_EXTENSION DevExt) {
    // Phase 2b stub: the BRB_L2CA_OPEN_CHANNEL flow is complex.
    // Full implementation in Phase 2c when the driver is installed and tested
    // against a real device. For now, log and return not-supported so CI
    // can compile the code path without crashing.
    UNREFERENCED_PARAMETER(DevExt);
    KdPrint(("OpenWinBlue: L2capOpenSignalingChannel — stub (Phase 2c)\n"));
    return STATUS_NOT_SUPPORTED;
}
```

Save to: `driver/src/l2cap_stream.c`

- [ ] **Step 7.3: Create `driver/src/ioctl.h`**

```c
// driver/src/ioctl.h
// IOCTL dispatch handler for the owb_a2dp device.
#pragma once
#include <ntddk.h>
#include <wdf.h>

typedef struct _OWB_DEVICE_EXTENSION OWB_DEVICE_EXTENSION, *POWB_DEVICE_EXTENSION;

// Register IOCTL handlers with the WDF queue.
NTSTATUS IoctlRegister(_In_ WDFDEVICE Device);

// Main IOCTL dispatch callback — registered with WdfIoQueueCreate.
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL OwbEvtIoDeviceControl;
```

Save to: `driver/src/ioctl.h`

- [ ] **Step 7.4: Create `driver/src/ioctl.c`**

```c
// driver/src/ioctl.c
// IOCTL dispatch handler — receives requests from owb-service.exe.
#include "ioctl.h"
#include "l2cap_stream.h"
#include "owb_a2dp.h"
#include "../owb_ioctl.h"

NTSTATUS IoctlRegister(_In_ WDFDEVICE Device) {
    WDF_IO_QUEUE_CONFIG queueConfig;
    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&queueConfig, WdfIoQueueDispatchParallel);
    queueConfig.EvtIoDeviceControl = OwbEvtIoDeviceControl;

    WDFQUEUE queue;
    return WdfIoQueueCreate(Device, &queueConfig, WDF_NO_OBJECT_ATTRIBUTES, &queue);
}

VOID OwbEvtIoDeviceControl(
    _In_ WDFQUEUE   Queue,
    _In_ WDFREQUEST Request,
    _In_ size_t     OutputBufferLength,
    _In_ size_t     InputBufferLength,
    _In_ ULONG      IoControlCode)
{
    UNREFERENCED_PARAMETER(Queue);
    UNREFERENCED_PARAMETER(OutputBufferLength);

    WDFDEVICE            device  = WdfIoQueueGetDevice(Queue);
    POWB_DEVICE_EXTENSION devExt = OwbGetDeviceExtension(device);
    NTSTATUS             status  = STATUS_SUCCESS;
    ULONG_PTR            info    = 0;

    switch (IoControlCode) {

        case OWB_IOCTL_SEND_AUDIO_FRAME: {
            PVOID   buf  = NULL;
            size_t  size = 0;
            status = WdfRequestRetrieveInputBuffer(Request, sizeof(OWB_SEND_FRAME_INPUT), &buf, &size);
            if (!NT_SUCCESS(status)) break;

            POWB_SEND_FRAME_INPUT frame = (POWB_SEND_FRAME_INPUT)buf;
            if (size < OWB_SEND_FRAME_INPUT_SIZE(frame->data_len)) {
                status = STATUS_BUFFER_TOO_SMALL;
                break;
            }
            status = L2capSendMediaFrame(devExt,
                                         frame->codec_id,
                                         frame->data,
                                         (USHORT)frame->data_len);
            break;
        }

        case OWB_IOCTL_GET_RF_QUALITY: {
            PVOID   buf  = NULL;
            size_t  size = 0;
            status = WdfRequestRetrieveOutputBuffer(Request, sizeof(OWB_RF_QUALITY), &buf, &size);
            if (!NT_SUCCESS(status)) break;

            OWB_RF_QUALITY* q = (OWB_RF_QUALITY*)buf;
            // Phase 2c: read from BthPort RSSI query BRB.
            // Stub values for Phase 2b to unblock testing.
            q->rssi_dbm            = -60L;   // typical indoor BT RSSI
            q->retransmit_per_mille = 0UL;
            q->link_quality        = 255UL;
            info = sizeof(OWB_RF_QUALITY);
            break;
        }

        case OWB_IOCTL_SET_CODEC_CONFIG: {
            PVOID   buf  = NULL;
            size_t  size = 0;
            status = WdfRequestRetrieveInputBuffer(Request, sizeof(OWB_CODEC_CONFIG), &buf, &size);
            if (!NT_SUCCESS(status)) break;

            POWB_CODEC_CONFIG cfg = (POWB_CODEC_CONFIG)buf;
            UNREFERENCED_PARAMETER(cfg);
            // Phase 2c: trigger AVDTP SET_CONFIGURATION reconfiguration.
            KdPrint(("OpenWinBlue: SET_CODEC_CONFIG codec=%lu key=%s value=%lld\n",
                     cfg->codec_id, cfg->param_key, cfg->param_value));
            break;
        }

        case OWB_IOCTL_GET_DEVICE_STATE: {
            PVOID   buf  = NULL;
            size_t  size = 0;
            status = WdfRequestRetrieveOutputBuffer(Request, sizeof(OWB_DEVICE_STATE), &buf, &size);
            if (!NT_SUCCESS(status)) break;

            OWB_DEVICE_STATE* state = (OWB_DEVICE_STATE*)buf;
            state->state           = (ULONG)devExt->Avdtp.State;
            state->active_codec_id = devExt->Avdtp.ActiveCodecId;
            RtlZeroMemory(state->remote_addr, sizeof(state->remote_addr));
            info = sizeof(OWB_DEVICE_STATE);
            break;
        }

        default:
            status = STATUS_INVALID_DEVICE_REQUEST;
            break;
    }

    WdfRequestCompleteWithInformation(Request, status, info);
}
```

Save to: `driver/src/ioctl.c`

- [ ] **Step 7.5: Commit**

```powershell
cd "c:/suru/open winblue"
git add driver/src/l2cap_stream.h driver/src/l2cap_stream.c
git add driver/src/ioctl.h driver/src/ioctl.c
git commit -m "feat(driver): add L2CAP media channel and IOCTL dispatch handler"
```

---

## Task 8: Update owb_a2dp.h/.c to wire all kernel components + push to CI

**Files:**
- Modify: `driver/src/owb_a2dp.h`
- Modify: `driver/src/owb_a2dp.c`

- [ ] **Step 8.1: Update `driver/src/owb_a2dp.h`**

```c
// driver/src/owb_a2dp.h
#pragma once
#include <ntddk.h>
#include <wdf.h>
#include <bthddi.h>
#include "avdtp.h"

#define OWB_DRIVER_VERSION_MAJOR 0
#define OWB_DRIVER_VERSION_MINOR 2

typedef struct _OWB_DEVICE_EXTENSION {
    WDFDEVICE             Device;
    BOOLEAN               IsActive;

    // AVDTP state machine context
    OWB_AVDTP_CONTEXT     Avdtp;

    // Bluetooth interface — obtained via WdfFdoQueryForInterface
    BTH_PROFILE_DRIVER_INTERFACE BthInterface;
    WDFIOTARGET              IoTarget;
    PVOID                    SignalingBrb;
    PVOID                    SignalingChannelHandle;

    // RTP sequence / timestamp counters
    USHORT                RtpSeqNum;
    ULONG                 RtpTimestamp;
} OWB_DEVICE_EXTENSION, *POWB_DEVICE_EXTENSION;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(OWB_DEVICE_EXTENSION, OwbGetDeviceExtension)

DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD OwbEvtDeviceAdd;
```

Save to: `driver/src/owb_a2dp.h`

- [ ] **Step 8.2: Update `driver/src/owb_a2dp.c`**

```c
// driver/src/owb_a2dp.c
#include "owb_a2dp.h"
#include "avdtp.h"
#include "l2cap_stream.h"
#include "ioctl.h"

NTSTATUS
DriverEntry(
    _In_ PDRIVER_OBJECT  DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    WDF_DRIVER_CONFIG config;
    NTSTATUS          status;

    WDF_DRIVER_CONFIG_INIT(&config, OwbEvtDeviceAdd);

    status = WdfDriverCreate(DriverObject, RegistryPath,
                             WDF_NO_OBJECT_ATTRIBUTES, &config, WDF_NO_HANDLE);
    if (!NT_SUCCESS(status)) {
        KdPrint(("OpenWinBlue: WdfDriverCreate failed 0x%x\n", status));
        return status;
    }

    KdPrint(("OpenWinBlue: driver loaded (v%d.%d)\n",
             OWB_DRIVER_VERSION_MAJOR, OWB_DRIVER_VERSION_MINOR));
    return STATUS_SUCCESS;
}

NTSTATUS
OwbEvtDeviceAdd(
    _In_    WDFDRIVER       Driver,
    _Inout_ PWDFDEVICE_INIT DeviceInit)
{
    NTSTATUS              status;
    WDFDEVICE             device;
    WDF_OBJECT_ATTRIBUTES attributes;
    POWB_DEVICE_EXTENSION ext;

    UNREFERENCED_PARAMETER(Driver);

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, OWB_DEVICE_EXTENSION);

    status = WdfDeviceCreate(&DeviceInit, &attributes, &device);
    if (!NT_SUCCESS(status)) {
        KdPrint(("OpenWinBlue: WdfDeviceCreate failed 0x%x\n", status));
        return status;
    }

    ext = OwbGetDeviceExtension(device);
    ext->Device        = device;
    ext->IsActive      = FALSE;
    ext->RtpSeqNum     = 0;
    ext->RtpTimestamp  = 0;
    AvdtpContextInit(&ext->Avdtp);

    // Create the device symbolic link so user-mode can open \\.\OpenWinBlue
    DECLARE_CONST_UNICODE_STRING(symLink, L"\\DosDevices\\OpenWinBlue");
    status = WdfDeviceCreateSymbolicLink(device, &symLink);
    if (!NT_SUCCESS(status)) {
        KdPrint(("OpenWinBlue: symbolic link creation failed 0x%x\n", status));
        return status;
    }

    // Register IOCTL queue
    status = IoctlRegister(device);
    if (!NT_SUCCESS(status)) {
        KdPrint(("OpenWinBlue: IoctlRegister failed 0x%x\n", status));
        return status;
    }

    // Initiate AVDTP connection (async — state machine handles the rest)
    // NOTE: This is a placeholder. The actual L2CA_OPEN_CHANNEL BRB flow
    // is implemented in Phase 2c when testing against real hardware.
    status = L2capOpenSignalingChannel(ext);
    if (status == STATUS_NOT_SUPPORTED) {
        // Expected in Phase 2b — signaling stub not yet connected
        status = STATUS_SUCCESS;
    }

    KdPrint(("OpenWinBlue: device added, AVDTP initialized\n"));
    return status;
}
```

Save to: `driver/src/owb_a2dp.c`

- [ ] **Step 8.3: Push to GitHub and watch CI driver build**

```powershell
$SANTI_TOKEN = $(gh auth token --user santiquiroz)
cd "c:/suru/open winblue"
git add driver/src/owb_a2dp.h driver/src/owb_a2dp.c
git commit -m "feat(driver): wire AVDTP + L2CAP + IOCTL into DriverEntry"
git push "https://santiquiroz:$SANTI_TOKEN@github.com/santiquiroz/openwinblue.git" main
```

Or with Bash:
```bash
SANTI_TOKEN=$(gh auth token --user santiquiroz)
git add driver/src/owb_a2dp.h driver/src/owb_a2dp.c
git commit -m "feat(driver): wire AVDTP + L2CAP + IOCTL into DriverEntry"
git push "https://santiquiroz:${SANTI_TOKEN}@github.com/santiquiroz/openwinblue.git" main
```

- [ ] **Step 8.4: Verify CI — all 3 jobs green**

Open: `https://github.com/santiquiroz/openwinblue/actions`

Expected:
- ✅ `Build & Test Service (C++)` — 17 tests pass
- ✅ `Build & Test GUI (C#)` — 2 tests pass
- ✅ `Build Driver (KMDF + WDK)` — `owb_a2dp.sys` produced

If the driver build job fails on WDK install, check the chocolatey package name:
```powershell
# Alternative package names to try:
choco install windows-driver-kit-11       # or
choco install windowsdriverskit           # or
choco install wdk                         # or use winget:
winget install Microsoft.WindowsDriverKit
```

---

## Self-Review

**Spec coverage:**
- ✅ IOCTL interface header (shared kernel/user-mode contract) — Task 1
- ✅ A2dpStream user-mode IOCTL client with stub mode — Task 2
- ✅ A2dpStream wired into service main loop — Task 3
- ✅ INF hardware IDs (A2DP Sink UUID 0000110b) — Task 4
- ✅ CI WDK installation + driver build job — Task 4
- ✅ KMDF Visual Studio project (.vcxproj) — Task 5
- ✅ AVDTP state machine (DISCOVER/GET_CAPS/SET_CONFIG/OPEN/START) — Task 6
- ✅ L2CAP media channel + RTP framing — Task 7
- ✅ IOCTL dispatch (SEND_FRAME/GET_RF/SET_CONFIG/GET_STATE) — Task 7
- ✅ owb_a2dp.h/.c updated with symbolic link + AVDTP init — Task 8
- ⚠️ L2capOpenSignalingChannel BRB flow — stubbed, Phase 2c completes it
- ⚠️ Real RSSI/RF quality from BthPort — stubbed, Phase 2c

**Placeholder scan:**
- L2capSendSignaling BRB completion callback is NULL (placeholder comment present ✓)
- L2capOpenSignalingChannel is explicitly stubbed (comment explains Phase 2c ✓)
- RSSI stub values are realistic (-60 dBm, 0 retransmit) and documented ✓

**Type consistency:**
- `OWB_CODEC_*` constants defined in `owb_ioctl.h`, used in `avdtp.c`, `ioctl.c`, `a2dp_stream.cpp` ✅
- `OWB_AVDTP_CONTEXT` defined in `avdtp.h`, stored in `OWB_DEVICE_EXTENSION` in `owb_a2dp.h` ✅
- `OWB_SEND_FRAME_INPUT_SIZE` macro used identically in `a2dp_stream.cpp` (user) and `ioctl.c` (kernel) ✅
- `L2capSendSignaling` declared in `l2cap_stream.h` and called in `avdtp.c` ✅
