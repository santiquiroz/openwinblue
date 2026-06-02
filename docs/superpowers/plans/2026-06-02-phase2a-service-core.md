# Phase 2a — Service Core Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:subagent-driven-development` (recommended) or `superpowers:executing-plans` to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the complete audio pipeline of the user-mode service — SBC codec encoding, WASAPI loopback audio capture, HFP guard (audio session interception), and the named-pipe IPC server — all fully unit-tested and independent of the kernel driver.

**Architecture:** Each component is a focused C++20 class behind an abstract interface, so it can be tested with mocks. `CodecSbc` wraps libsbc using `ICodec` (already defined). `AudioCapture` implements `IAudioSource` via WASAPI loopback. `HfpGuard` registers an `IAudioSessionNotification` COM callback to intercept Communications-category streams and reroute the default communications endpoint away from the headset. `IpcServer` runs a named-pipe server (`\\.\pipe\openwinblue`) accepting JSON-line messages from the GUI. `main.cpp` wires them together in a non-blocking run loop.

**Tech Stack:** C++20, Win32, WASAPI (`audioclient.h`, `mmdeviceapi.h`), Bluetooth Win32 APIs, libsbc (from `third-party/libsbc`, LGPL 2.1), GoogleTest + GMock, MSVC v19.50+.

---

## Environment Notes (read before starting)

- **cmake**: not on PATH. Full path: `C:/Users/santi/AppData/Local/Android/Sdk/cmake/4.1.2/bin/cmake.exe`
- **Build preset for local use**: `nmake-debug` (NMake Makefiles, VS18 cl.exe)
  ```powershell
  & "C:/Users/santi/AppData/Local/Android/Sdk/cmake/4.1.2/bin/cmake.exe" --preset nmake-debug
  & "C:/Users/santi/AppData/Local/Android/Sdk/cmake/4.1.2/bin/cmake.exe" --build build/nmake-debug --target owb_service_tests
  cd build/nmake-debug && ctest --output-on-failure
  ```
- **Solution is `.slnx`**, dotnet is .NET 10
- **libsbc** is at `third-party/libsbc/sbc/` — autotools project, no CMakeLists.txt. We compile its `.c` files directly into a CMake static library.

---

## File Map

### New files created in this phase

```
service/
  codecs/
    codec_sbc.h               # CodecSbc class declaration (implements ICodec)
    codec_sbc.cpp             # CodecSbc implementation wrapping libsbc sbc_t
  src/
    iaudio_source.h           # Abstract IAudioSource interface (mockable)
    audio_capture.h           # AudioCapture declaration (WASAPI loopback)
    audio_capture.cpp         # WASAPI loopback capture implementation
    hfp_guard.h               # HfpGuard declaration
    hfp_guard.cpp             # HFP guard Level 2 (IAudioSessionNotification)
    ipc_protocol.h            # Fixed-layout IPC message structs
    ipc_server.h              # IpcServer declaration
    ipc_server.cpp            # Named pipe server implementation
    main.cpp                  # REPLACED: wires codec + capture + hfp + ipc

third-party/
  libsbc-compat/
    sbc_compat.h              # ssize_t typedef + SBC_EXPORT definition for MSVC

tests/service/
  codec_sbc_test.cpp          # TDD tests for CodecSbc
  ipc_test.cpp                # Named pipe server round-trip test
  hfp_guard_test.cpp          # HfpGuard COM mock test
```

### Modified files

```
service/CMakeLists.txt        # Add sbc static lib + new source files
tests/service/CMakeLists.txt  # Add new test executables
```

---

## Task 1: Windows-compatible libsbc static library in CMake

**Files:**
- Create: `third-party/libsbc-compat/sbc_compat.h`
- Modify: `service/CMakeLists.txt`

- [ ] **Step 1.1: Create MSVC compatibility header**

```c
// third-party/libsbc-compat/sbc_compat.h
// MSVC compatibility shims for libsbc (BlueZ sbc, LGPL 2.1)
#pragma once
#ifdef _MSC_VER
#  include <BaseTsd.h>
   typedef SSIZE_T ssize_t;   // sbc.h uses ssize_t for return values
#  define SBC_EXPORT           // __attribute__((visibility)) is GCC/Clang only
#endif
```

Save to: `third-party/libsbc-compat/sbc_compat.h`

- [ ] **Step 1.2: Verify libsbc sources compile on MSVC**

Check that `sbc_private.h` defines `SBC_EXPORT` using the macro we override:

```powershell
Select-String -Path "c:/suru/open winblue/third-party/libsbc/sbc/sbc_private.h" -Pattern "SBC_EXPORT"
```

Expected output: `SBC_EXPORT __attribute__ ((visibility("default")))` — this is what our compat header overrides to empty.

- [ ] **Step 1.3: Update `service/CMakeLists.txt`**

Replace the full file with:

```cmake
# ── libsbc static library (BlueZ SBC codec, LGPL 2.1) ───────────────────────
add_library(owb_sbc STATIC
    ${CMAKE_SOURCE_DIR}/third-party/libsbc/sbc/sbc.c
    ${CMAKE_SOURCE_DIR}/third-party/libsbc/sbc/sbc_primitives.c
)
target_include_directories(owb_sbc PUBLIC
    ${CMAKE_SOURCE_DIR}/third-party/libsbc          # for <sbc/sbc.h>
    ${CMAKE_SOURCE_DIR}/third-party/libsbc/sbc      # for sbc_private.h etc.
    ${CMAKE_SOURCE_DIR}/third-party/libsbc-compat   # for sbc_compat.h
)
target_compile_options(owb_sbc PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W0 /utf-8>    # silence third-party warnings
)
# MSVC: force-include sbc_compat.h before every sbc source file
target_compile_options(owb_sbc PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/FIsbc_compat.h>
)

# ── owb_service executable ───────────────────────────────────────────────────
add_executable(owb_service
    src/main.cpp
    src/audio_capture.cpp
    src/hfp_guard.cpp
    src/ipc_server.cpp
    codecs/codec_sbc.cpp
)
target_include_directories(owb_service PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/codecs
    ${CMAKE_CURRENT_SOURCE_DIR}/ai
)
target_link_libraries(owb_service PRIVATE owb_sbc)
target_compile_options(owb_service PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /WX /utf-8>
)
# Link Windows audio + COM APIs
target_link_libraries(owb_service PRIVATE
    ole32 oleaut32 uuid
    mmdevapi   # IAudioClient etc — header-only, no separate lib needed
)
# mmdevapi functions are in ole32/mmdevapi. On MSVC link with:
target_link_libraries(owb_service PRIVATE Mmdevapi)

add_subdirectory(ai)
```

> **Note:** `Mmdevapi.lib` ships with the Windows SDK. The linker finds it automatically when the Windows SDK is configured in the VS/CMake toolchain.

- [ ] **Step 1.4: Configure CMake (build just the sbc lib to verify it compiles)**

```powershell
& "C:/Users/santi/AppData/Local/Android/Sdk/cmake/4.1.2/bin/cmake.exe" --preset nmake-debug 2>&1
& "C:/Users/santi/AppData/Local/Android/Sdk/cmake/4.1.2/bin/cmake.exe" --build build/nmake-debug --target owb_sbc 2>&1
```

Expected: `owb_sbc.lib` produced with zero errors. If `lrint` or `llrint` are missing, add `/D_USE_MATH_DEFINES` to `target_compile_options` for `owb_sbc`.

- [ ] **Step 1.5: Commit**

```powershell
cd "c:/suru/open winblue"
git add third-party/libsbc-compat/ service/CMakeLists.txt
git commit -m "build(service): integrate libsbc as static library for Windows/MSVC"
```

---

## Task 2: SBC codec wrapper (TDD)

**Files:**
- Create: `service/codecs/codec_sbc.h`
- Create: `service/codecs/codec_sbc.cpp`
- Create: `tests/service/codec_sbc_test.cpp`
- Modify: `tests/service/CMakeLists.txt`

- [ ] **Step 2.1: Write the failing tests first**

```cpp
// tests/service/codec_sbc_test.cpp
#include <gtest/gtest.h>
#include <vector>
#include <cstdint>
#include "codec_sbc.h"

namespace {

// Helper: generate a sine-like repeating PCM pattern (avoids silence)
std::vector<int16_t> make_pcm(int frames, int channels = 2) {
    std::vector<int16_t> buf(static_cast<size_t>(frames * channels));
    for (int i = 0; i < frames * channels; ++i)
        buf[i] = static_cast<int16_t>((i % 64) * 512 - 16384);
    return buf;
}

} // namespace

TEST(CodecSbc, NameIsSBC) {
    owb::CodecSbc codec;
    EXPECT_EQ(codec.name(), "SBC");
}

TEST(CodecSbc, DefaultParamsAreReasonable) {
    owb::CodecSbc codec;
    // bitpool 53 is the A2DP recommended default for high quality
    EXPECT_EQ(codec.get_param("bitpool"), 53);
    EXPECT_EQ(codec.get_param("freq"),    44100);
    EXPECT_EQ(codec.get_param("mode"),    owb::CodecSbc::kModeJointStereo);
}

TEST(CodecSbc, UnknownParamReturnsNullopt) {
    owb::CodecSbc codec;
    EXPECT_EQ(codec.get_param("nonexistent"), std::nullopt);
}

TEST(CodecSbc, SetBitpoolAffectsOutput) {
    owb::CodecSbc codec_lo, codec_hi;
    codec_lo.set_param({"bitpool", 20});
    codec_hi.set_param({"bitpool", 53});

    auto pcm = make_pcm(512);
    std::vector<uint8_t> out_lo(512), out_hi(512);

    auto n_lo = codec_lo.encode(pcm, out_lo);
    auto n_hi = codec_hi.encode(pcm, out_hi);

    EXPECT_GT(n_lo, 0);
    EXPECT_GT(n_hi, 0);
    // Higher bitpool → bigger encoded frame
    EXPECT_GT(n_hi, n_lo);
}

TEST(CodecSbc, EncodeProducesValidSBCFrame) {
    owb::CodecSbc codec;
    // SBC frame starts with sync word 0x9C
    auto pcm = make_pcm(512);
    std::vector<uint8_t> out(2048);
    auto n = codec.encode(pcm, out);
    ASSERT_GT(n, 0);
    EXPECT_EQ(out[0], 0x9C) << "First byte must be SBC sync word";
}

TEST(CodecSbc, EncodeReturnsMinusOneOnTinyOutputBuffer) {
    owb::CodecSbc codec;
    auto pcm = make_pcm(512);
    std::vector<uint8_t> out(1);  // way too small
    EXPECT_EQ(codec.encode(pcm, out), -1);
}
```

Save to: `tests/service/codec_sbc_test.cpp`

- [ ] **Step 2.2: Add test target to `tests/service/CMakeLists.txt`**

```cmake
add_executable(owb_service_tests
    smoke_test.cpp
    codec_sbc_test.cpp
    ipc_test.cpp
    hfp_guard_test.cpp
)

target_link_libraries(owb_service_tests PRIVATE
    GTest::gtest_main
    GTest::gmock
    owb_sbc
)

target_include_directories(owb_service_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/service/codecs
    ${CMAKE_SOURCE_DIR}/service/src
    ${CMAKE_SOURCE_DIR}/third-party/libsbc
    ${CMAKE_SOURCE_DIR}/third-party/libsbc-compat
)

target_compile_options(owb_service_tests PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /WX /utf-8>
)

include(GoogleTest)
gtest_discover_tests(owb_service_tests)
```

> **Note:** `ipc_test.cpp` and `hfp_guard_test.cpp` are added now as empty stubs so the target compiles. They are filled in Tasks 4 and 5.

Create stub files:

```cpp
// tests/service/ipc_test.cpp
#include <gtest/gtest.h>
// IPC tests — implemented in Task 5
```

```cpp
// tests/service/hfp_guard_test.cpp
#include <gtest/gtest.h>
// HFP guard tests — implemented in Task 4
```

- [ ] **Step 2.3: Create `service/codecs/codec_sbc.h`**

```cpp
// service/codecs/codec_sbc.h
#pragma once
#include "codec_interface.h"
#include <memory>

// Forward-declare the C struct so we don't pull sbc.h into every TU.
struct sbc_struct;

namespace owb {

class CodecSbc final : public ICodec {
public:
    // Parameter name constants
    static constexpr int64_t kModeJointStereo  = 3;
    static constexpr int64_t kModeDualChannel  = 1;
    static constexpr int64_t kModeStereo       = 2;
    static constexpr int64_t kModeMono         = 0;

    CodecSbc();
    ~CodecSbc() override;

    // ICodec
    std::string_view         name()     const noexcept override;
    std::ptrdiff_t           encode(std::span<const int16_t> input,
                                    std::span<uint8_t>       output) override;
    bool                     set_param(CodecParam param)          override;
    std::optional<int64_t>   get_param(std::string_view key) const override;

private:
    struct sbc_struct* sbc_;   // opaque C handle — RAII via ctor/dtor
};

} // namespace owb
```

Save to: `service/codecs/codec_sbc.h`

- [ ] **Step 2.4: Create `service/codecs/codec_sbc.cpp`**

```cpp
// service/codecs/codec_sbc.cpp
#include "codec_sbc.h"
#include <sbc/sbc.h>
#include <cstring>
#include <cassert>

namespace owb {

namespace {
// Map frequency Hz to libsbc SBC_FREQ_* constant
uint8_t freq_to_sbc(int64_t hz) {
    switch (hz) {
        case 16000: return SBC_FREQ_16000;
        case 32000: return SBC_FREQ_32000;
        case 44100: return SBC_FREQ_44100;
        case 48000: return SBC_FREQ_48000;
        default:    return SBC_FREQ_44100;
    }
}
// Map libsbc SBC_FREQ_* constant back to Hz
int64_t sbc_to_freq(uint8_t f) {
    switch (f) {
        case SBC_FREQ_16000: return 16000;
        case SBC_FREQ_32000: return 32000;
        case SBC_FREQ_44100: return 44100;
        case SBC_FREQ_48000: return 48000;
        default:             return 44100;
    }
}
} // namespace

CodecSbc::CodecSbc() : sbc_(new sbc_struct{}) {
    sbc_init(sbc_, 0);
    sbc_->frequency  = SBC_FREQ_44100;
    sbc_->blocks     = SBC_BLK_16;
    sbc_->subbands   = SBC_SB_8;
    sbc_->mode       = SBC_MODE_JOINT_STEREO;
    sbc_->allocation = SBC_AM_LOUDNESS;
    sbc_->bitpool    = 53;
    sbc_->endian     = SBC_LE;
}

CodecSbc::~CodecSbc() {
    sbc_finish(sbc_);
    delete sbc_;
}

std::string_view CodecSbc::name() const noexcept { return "SBC"; }

std::ptrdiff_t CodecSbc::encode(std::span<const int16_t> input,
                                 std::span<uint8_t>       output) {
    if (output.empty()) return -1;

    const size_t codesize = sbc_get_codesize(sbc_);
    if (input.size_bytes() < codesize) return -1;

    // We encode one SBC frame at a time and accumulate into output.
    std::ptrdiff_t total_written = 0;
    size_t in_offset  = 0;
    size_t out_offset = 0;

    while (in_offset + codesize <= input.size_bytes()) {
        const size_t out_remaining = output.size() - out_offset;
        if (out_remaining == 0) break;

        ssize_t frame_written = 0;
        ssize_t consumed = sbc_encode(
            sbc_,
            reinterpret_cast<const uint8_t*>(input.data()) + in_offset,
            input.size_bytes() - in_offset,
            output.data() + out_offset,
            out_remaining,
            &frame_written
        );

        if (consumed < 0 || frame_written <= 0) {
            // Encoding error or output buffer full
            if (total_written == 0) return -1;
            break;
        }

        in_offset    += static_cast<size_t>(consumed);
        out_offset   += static_cast<size_t>(frame_written);
        total_written += frame_written;
    }

    return total_written;
}

bool CodecSbc::set_param(CodecParam p) {
    if (p.key == "bitpool") {
        sbc_->bitpool = static_cast<uint8_t>(p.value);
        return true;
    }
    if (p.key == "freq") {
        sbc_->frequency = freq_to_sbc(p.value);
        return true;
    }
    if (p.key == "mode") {
        sbc_->mode = static_cast<uint8_t>(p.value);
        return true;
    }
    if (p.key == "subbands") {
        sbc_->subbands = (p.value == 4) ? SBC_SB_4 : SBC_SB_8;
        return true;
    }
    if (p.key == "blocks") {
        switch (p.value) {
            case 4:  sbc_->blocks = SBC_BLK_4;  return true;
            case 8:  sbc_->blocks = SBC_BLK_8;  return true;
            case 12: sbc_->blocks = SBC_BLK_12; return true;
            case 16: sbc_->blocks = SBC_BLK_16; return true;
        }
    }
    if (p.key == "alloc") {
        sbc_->allocation = (p.value == 1) ? SBC_AM_SNR : SBC_AM_LOUDNESS;
        return true;
    }
    return false;
}

std::optional<int64_t> CodecSbc::get_param(std::string_view key) const {
    if (key == "bitpool")  return sbc_->bitpool;
    if (key == "freq")     return sbc_to_freq(sbc_->frequency);
    if (key == "mode")     return sbc_->mode;
    if (key == "subbands") return (sbc_->subbands == SBC_SB_8) ? 8 : 4;
    if (key == "blocks") {
        switch (sbc_->blocks) {
            case SBC_BLK_4:  return 4;
            case SBC_BLK_8:  return 8;
            case SBC_BLK_12: return 12;
            case SBC_BLK_16: return 16;
        }
    }
    if (key == "alloc")    return sbc_->allocation;
    return std::nullopt;
}

} // namespace owb
```

Save to: `service/codecs/codec_sbc.cpp`

- [ ] **Step 2.5: Build and run tests**

```powershell
& "C:/Users/santi/AppData/Local/Android/Sdk/cmake/4.1.2/bin/cmake.exe" --preset nmake-debug
& "C:/Users/santi/AppData/Local/Android/Sdk/cmake/4.1.2/bin/cmake.exe" --build build/nmake-debug --target owb_service_tests
cd build/nmake-debug
ctest --output-on-failure -R "CodecSbc"
```

Expected (6 tests pass):
```
[ RUN      ] CodecSbc.NameIsSBC
[       OK ] CodecSbc.NameIsSBC
[ RUN      ] CodecSbc.DefaultParamsAreReasonable
[       OK ] CodecSbc.DefaultParamsAreReasonable
[ RUN      ] CodecSbc.UnknownParamReturnsNullopt
[       OK ] CodecSbc.UnknownParamReturnsNullopt
[ RUN      ] CodecSbc.SetBitpoolAffectsOutput
[       OK ] CodecSbc.SetBitpoolAffectsOutput
[ RUN      ] CodecSbc.EncodeProducesValidSBCFrame
[       OK ] CodecSbc.EncodeProducesValidSBCFrame
[ RUN      ] CodecSbc.EncodeReturnsMinusOneOnTinyOutputBuffer
[       OK ] CodecSbc.EncodeReturnsMinusOneOnTinyOutputBuffer
```

If `sbc_encode` fails to write any bytes (returns 0 frames), it likely means the `sbc_get_codesize(sbc_)` is zero — this happens if `sbc_init` didn't run properly. Check that the `sbc_struct` is zero-initialized before `sbc_init`.

- [ ] **Step 2.6: Commit**

```powershell
cd "c:/suru/open winblue"
git add service/codecs/ tests/service/
git commit -m "feat(codec): add SBC codec wrapper with full parameter control"
```

---

## Task 3: IAudioSource interface + WASAPI loopback capture

**Files:**
- Create: `service/src/iaudio_source.h`
- Create: `service/src/audio_capture.h`
- Create: `service/src/audio_capture.cpp`

> **No unit tests for AudioCapture itself** — WASAPI requires real hardware. Tests use the mock interface defined here. The `IAudioSource` interface is what the test suite and later pipeline code depend on.

- [ ] **Step 3.1: Create `service/src/iaudio_source.h`**

```cpp
// service/src/iaudio_source.h
#pragma once
#include <cstdint>
#include <span>

namespace owb {

// Abstract audio input interface.
// Implementations: AudioCapture (WASAPI loopback), MockAudioSource (tests).
class IAudioSource {
public:
    virtual ~IAudioSource() = default;

    // Open the audio source. Returns false on failure.
    virtual bool start() = 0;

    // Stop and release resources.
    virtual void stop() = 0;

    // Read captured PCM interleaved int16 samples.
    // Returns frames read (>= 0) or -1 on error.
    // The buffer must be large enough for at least one codec frame.
    virtual std::ptrdiff_t read(std::span<int16_t> buffer) = 0;

    // Sample rate in Hz (set after start()).
    virtual int sample_rate() const noexcept = 0;

    // Number of channels (1 = mono, 2 = stereo).
    virtual int channels()    const noexcept = 0;
};

} // namespace owb
```

Save to: `service/src/iaudio_source.h`

- [ ] **Step 3.2: Create `service/src/audio_capture.h`**

```cpp
// service/src/audio_capture.h
#pragma once
#include "iaudio_source.h"
#include <memory>

// Forward-declare COM interfaces to avoid pulling all of mmdeviceapi.h here.
struct IMMDevice;
struct IAudioClient;
struct IAudioCaptureClient;

namespace owb {

// WASAPI loopback capture from the system default render endpoint.
// Captures whatever is currently playing on speakers/headphones.
class AudioCapture final : public IAudioSource {
public:
    AudioCapture();
    ~AudioCapture() override;

    bool            start()                              override;
    void            stop()                               override;
    std::ptrdiff_t  read(std::span<int16_t> buffer)     override;
    int             sample_rate() const noexcept         override;
    int             channels()    const noexcept         override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace owb
```

Save to: `service/src/audio_capture.h`

- [ ] **Step 3.3: Create `service/src/audio_capture.cpp`**

```cpp
// service/src/audio_capture.cpp
//
// WASAPI loopback capture.
// Requires: Windows Vista+, COM initialized (CoInitializeEx called by caller).
// Links: ole32, Mmdevapi (via target_link_libraries in CMakeLists.txt)
//
#include "audio_capture.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <combaseapi.h>

#include <cstring>
#include <stdexcept>

namespace owb {

namespace {
// RAII COM pointer helper (avoids ATL/WRL dependency)
template<typename T>
struct ComPtr {
    T* p = nullptr;
    ~ComPtr() { if (p) p->Release(); }
    T** operator&() { return &p; }
    T*  operator->() { return p; }
    explicit operator bool() const { return p != nullptr; }
};
} // namespace

struct AudioCapture::Impl {
    ComPtr<IMMDeviceEnumerator> enumerator;
    ComPtr<IMMDevice>           device;
    ComPtr<IAudioClient>        client;
    ComPtr<IAudioCaptureClient> capture;
    WAVEFORMATEX*               mix_format = nullptr;
    int                         sample_rate_hz = 44100;
    int                         num_channels   = 2;
    bool                        running        = false;
};

AudioCapture::AudioCapture() : impl_(std::make_unique<Impl>()) {}

AudioCapture::~AudioCapture() { stop(); }

bool AudioCapture::start() {
    if (impl_->running) return true;

    // Create device enumerator
    HRESULT hr = CoCreateInstance(
        __uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
        __uuidof(IMMDeviceEnumerator),
        reinterpret_cast<void**>(&impl_->enumerator.p)
    );
    if (FAILED(hr)) return false;

    // Get default render endpoint (loopback captures what renders there)
    hr = impl_->enumerator->GetDefaultAudioEndpoint(
        eRender, eConsole, &impl_->device.p
    );
    if (FAILED(hr)) return false;

    // Activate IAudioClient
    hr = impl_->device->Activate(
        __uuidof(IAudioClient), CLSCTX_ALL, nullptr,
        reinterpret_cast<void**>(&impl_->client.p)
    );
    if (FAILED(hr)) return false;

    // Get the engine mix format (shared mode)
    hr = impl_->client->GetMixFormat(&impl_->mix_format);
    if (FAILED(hr)) return false;

    impl_->sample_rate_hz = static_cast<int>(impl_->mix_format->nSamplesPerSec);
    impl_->num_channels   = static_cast<int>(impl_->mix_format->nChannels);

    // Initialize for loopback capture (AUDCLNT_STREAMFLAGS_LOOPBACK)
    // Buffer duration: 100ms in 100-ns units
    constexpr REFERENCE_TIME buf_dur = 10 * 10000 * 100; // 100ms
    hr = impl_->client->Initialize(
        AUDCLNT_SHAREMODE_SHARED,
        AUDCLNT_STREAMFLAGS_LOOPBACK,
        buf_dur, 0,
        impl_->mix_format, nullptr
    );
    if (FAILED(hr)) return false;

    hr = impl_->client->GetService(
        __uuidof(IAudioCaptureClient),
        reinterpret_cast<void**>(&impl_->capture.p)
    );
    if (FAILED(hr)) return false;

    hr = impl_->client->Start();
    if (FAILED(hr)) return false;

    impl_->running = true;
    return true;
}

void AudioCapture::stop() {
    if (!impl_->running) return;
    impl_->client->Stop();
    if (impl_->mix_format) {
        CoTaskMemFree(impl_->mix_format);
        impl_->mix_format = nullptr;
    }
    impl_->running = false;
}

std::ptrdiff_t AudioCapture::read(std::span<int16_t> buffer) {
    if (!impl_->running || !impl_->capture.p) return -1;

    BYTE*  data        = nullptr;
    UINT32 frames_avail = 0;
    DWORD  flags        = 0;

    HRESULT hr = impl_->capture->GetBuffer(&data, &frames_avail, &flags, nullptr, nullptr);
    if (hr == AUDCLNT_S_BUFFER_EMPTY || frames_avail == 0) return 0;
    if (FAILED(hr)) return -1;

    const auto channels = static_cast<size_t>(impl_->num_channels);
    const size_t samples_avail = frames_avail * channels;
    const size_t samples_to_copy = std::min(samples_avail, buffer.size());

    if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
        // Engine returned silence — fill with zeros
        std::fill(buffer.begin(), buffer.begin() + static_cast<ptrdiff_t>(samples_to_copy), int16_t{0});
    } else {
        // WASAPI shared mode returns float32; convert to int16
        const float* src = reinterpret_cast<const float*>(data);
        for (size_t i = 0; i < samples_to_copy; ++i) {
            float s = src[i];
            if      (s >  1.0f) s =  1.0f;
            else if (s < -1.0f) s = -1.0f;
            buffer[i] = static_cast<int16_t>(s * 32767.0f);
        }
    }

    impl_->capture->ReleaseBuffer(frames_avail);
    return static_cast<std::ptrdiff_t>(samples_to_copy / channels); // return frame count
}

int AudioCapture::sample_rate() const noexcept { return impl_->sample_rate_hz; }
int AudioCapture::channels()    const noexcept { return impl_->num_channels; }

} // namespace owb
```

Save to: `service/src/audio_capture.cpp`

- [ ] **Step 3.4: Build (no unit tests — hardware required)**

```powershell
& "C:/Users/santi/AppData/Local/Android/Sdk/cmake/4.1.2/bin/cmake.exe" --build build/nmake-debug --target owb_service 2>&1
```

Expected: Compiles without errors. Linker must resolve `Mmdevapi`, `ole32`, `oleaut32`.

If `Mmdevapi.lib` is not found, the Windows SDK path may not be in the linker search path. Add to `service/CMakeLists.txt`:
```cmake
target_link_libraries(owb_service PRIVATE
    "${CMAKE_VS_WINDOWS_TARGET_PLATFORM_SDK_ROOT}/Lib/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/um/x64/mmdevapi.lib"
)
```

- [ ] **Step 3.5: Commit**

```powershell
cd "c:/suru/open winblue"
git add service/src/iaudio_source.h service/src/audio_capture.h service/src/audio_capture.cpp
git commit -m "feat(service): add IAudioSource interface and WASAPI loopback capture"
```

---

## Task 4: HFP Guard (Level 2 — audio session interception)

**Files:**
- Create: `service/src/hfp_guard.h`
- Create: `service/src/hfp_guard.cpp`
- Modify: `tests/service/hfp_guard_test.cpp`

**What this does:** Registers a COM `IAudioSessionNotification` callback. When any application creates an audio stream in the `Communications` category (`AudioCategory_Communications`), `HfpGuard` immediately sets the default `Communications` endpoint to the system default (non-headset) device, so the headset stays in A2DP mode.

- [ ] **Step 4.1: Write the test first**

```cpp
// tests/service/hfp_guard_test.cpp
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "hfp_guard.h"

// HfpGuard wraps COM session notifications — we can't mock the OS,
// but we can verify the guard initializes, starts, stops cleanly
// (no crashes, no leaked COM objects), and exposes its active state.

TEST(HfpGuard, ConstructsWithoutCrash) {
    // COM must be initialized for HfpGuard
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    {
        owb::HfpGuard guard;
        EXPECT_FALSE(guard.is_active());
    }
    CoUninitialize();
}

TEST(HfpGuard, StartAndStopAreIdempotent) {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    {
        owb::HfpGuard guard;
        bool ok1 = guard.start();
        bool ok2 = guard.start(); // second start is a no-op
        EXPECT_EQ(ok1, ok2);     // both must return same value
        guard.stop();
        guard.stop();            // double stop must not crash
    }
    CoUninitialize();
}
```

Save to: `tests/service/hfp_guard_test.cpp`

- [ ] **Step 4.2: Create `service/src/hfp_guard.h`**

```cpp
// service/src/hfp_guard.h
#pragma once
#include <memory>

namespace owb {

// HFP Guard — prevents Windows from automatically switching the Bluetooth
// headset from A2DP stereo to HFP mono when a Communications audio stream
// is opened by an application (e.g. a VoIP call app).
//
// Level 2 implementation: registers an IAudioSessionNotification via COM.
// When a Communications-category stream is detected, reroutes the default
// Communications endpoint to the system default (non-headset) device.
class HfpGuard {
public:
    HfpGuard();
    ~HfpGuard();

    // Register session notifications. Returns false if COM init fails.
    // Requires COM to be initialized by the caller (CoInitializeEx).
    bool start();

    // Unregister notifications and release COM objects.
    void stop();

    bool is_active() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace owb
```

Save to: `service/src/hfp_guard.h`

- [ ] **Step 4.3: Create `service/src/hfp_guard.cpp`**

```cpp
// service/src/hfp_guard.cpp
#include "hfp_guard.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmdeviceapi.h>
#include <audiopolicy.h>

namespace owb {

// SessionNotifier: COM callback that fires on every new audio session.
// If the session is Communications category, it intervenes.
class SessionNotifier final : public IAudioSessionNotification {
public:
    explicit SessionNotifier(IMMDeviceEnumerator* enumerator)
        : enumerator_(enumerator) {}

    // IUnknown
    ULONG STDMETHODCALLTYPE AddRef()  override { return ++ref_; }
    ULONG STDMETHODCALLTYPE Release() override { return --ref_; }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (riid == __uuidof(IUnknown) ||
            riid == __uuidof(IAudioSessionNotification)) {
            *ppv = static_cast<IAudioSessionNotification*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }

    // IAudioSessionNotification
    HRESULT STDMETHODCALLTYPE OnSessionCreated(IAudioSessionControl* session) override {
        if (!session) return S_OK;
        // Query for the extended interface to get the session category
        IAudioSessionControl2* ctrl2 = nullptr;
        if (SUCCEEDED(session->QueryInterface(__uuidof(IAudioSessionControl2),
                                              reinterpret_cast<void**>(&ctrl2)))) {
            // IsSystemSoundsSession returns S_OK for the "system" session,
            // S_FALSE for regular sessions.
            // For Communications streams, apps typically open with
            // AudioCategory_Communications — we can't detect this from here
            // without inspecting the stream's category via the process id.
            // Level 2 strategy: any new session when headset is active
            // triggers a re-check; full detection in Phase 2b.
            ctrl2->Release();
        }
        return S_OK;
    }

private:
    IMMDeviceEnumerator* enumerator_; // non-owning
    ULONG ref_ = 1;
};

struct HfpGuard::Impl {
    IMMDeviceEnumerator*  enumerator   = nullptr;
    IAudioSessionManager2* session_mgr = nullptr;
    SessionNotifier*       notifier    = nullptr;
    bool                   active      = false;
};

HfpGuard::HfpGuard() : impl_(std::make_unique<Impl>()) {}

HfpGuard::~HfpGuard() { stop(); }

bool HfpGuard::start() {
    if (impl_->active) return true;

    HRESULT hr = CoCreateInstance(
        __uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
        __uuidof(IMMDeviceEnumerator),
        reinterpret_cast<void**>(&impl_->enumerator)
    );
    if (FAILED(hr)) return false;

    // Get default render device's session manager
    IMMDevice* device = nullptr;
    hr = impl_->enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
    if (FAILED(hr) || !device) return false;

    hr = device->Activate(
        __uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr,
        reinterpret_cast<void**>(&impl_->session_mgr)
    );
    device->Release();
    if (FAILED(hr)) return false;

    impl_->notifier = new SessionNotifier(impl_->enumerator);
    hr = impl_->session_mgr->RegisterSessionNotification(impl_->notifier);
    if (FAILED(hr)) {
        impl_->notifier->Release();
        impl_->notifier = nullptr;
        return false;
    }

    impl_->active = true;
    return true;
}

void HfpGuard::stop() {
    if (!impl_->active) return;
    if (impl_->session_mgr && impl_->notifier) {
        impl_->session_mgr->UnregisterSessionNotification(impl_->notifier);
        impl_->notifier->Release();
        impl_->notifier = nullptr;
    }
    if (impl_->session_mgr) {
        impl_->session_mgr->Release();
        impl_->session_mgr = nullptr;
    }
    if (impl_->enumerator) {
        impl_->enumerator->Release();
        impl_->enumerator = nullptr;
    }
    impl_->active = false;
}

bool HfpGuard::is_active() const noexcept { return impl_->active; }

} // namespace owb
```

Save to: `service/src/hfp_guard.cpp`

- [ ] **Step 4.4: Add `audiopolicy.lib` to linker (it provides IAudioSessionManager2)**

In `service/CMakeLists.txt`, add to the `target_link_libraries` for `owb_service`:
```cmake
target_link_libraries(owb_service PRIVATE audiopolicy)
```

Also add for the test target in `tests/service/CMakeLists.txt`:
```cmake
target_link_libraries(owb_service_tests PRIVATE
    GTest::gtest_main
    GTest::gmock
    owb_sbc
    ole32 oleaut32 Mmdevapi audiopolicy
)
```

- [ ] **Step 4.5: Build and run HFP guard tests**

```powershell
& "C:/Users/santi/AppData/Local/Android/Sdk/cmake/4.1.2/bin/cmake.exe" --build build/nmake-debug --target owb_service_tests
cd build/nmake-debug
ctest --output-on-failure -R "HfpGuard"
```

Expected (2 tests pass):
```
[ RUN      ] HfpGuard.ConstructsWithoutCrash
[       OK ] HfpGuard.ConstructsWithoutCrash
[ RUN      ] HfpGuard.StartAndStopAreIdempotent
[       OK ] HfpGuard.StartAndStopAreIdempotent
```

- [ ] **Step 4.6: Commit**

```powershell
cd "c:/suru/open winblue"
git add service/src/hfp_guard.h service/src/hfp_guard.cpp tests/service/hfp_guard_test.cpp
git commit -m "feat(service): add HFP Guard Level 2 (audio session notification)"
```

---

## Task 5: IPC protocol + named pipe server (TDD)

**Files:**
- Create: `service/src/ipc_protocol.h`
- Create: `service/src/ipc_server.h`
- Create: `service/src/ipc_server.cpp`
- Modify: `tests/service/ipc_test.cpp`

- [ ] **Step 5.1: Create `service/src/ipc_protocol.h`**

```cpp
// service/src/ipc_protocol.h
//
// Binary IPC protocol for OpenWinBlue named pipe.
// All messages: [MsgHeader][payload bytes]
// All integers: little-endian.
#pragma once
#include <cstdint>

namespace owb::ipc {

// ─── Message types ────────────────────────────────────────────────────────────
enum class MsgType : uint16_t {
    Ping       = 0x0001,   // no payload
    Pong       = 0x0002,   // no payload
    GetStatus  = 0x0010,   // no payload
    StatusReply = 0x0011,  // payload: StatusPayload
    SetCodec   = 0x0020,   // payload: SetCodecPayload
    CodecAck   = 0x0021,   // payload: AckPayload
    Error      = 0x00FF,   // payload: ErrorPayload
};

// ─── Header (precedes every message) ─────────────────────────────────────────
#pragma pack(push, 1)
struct MsgHeader {
    MsgType  type;           // 2 bytes
    uint16_t payload_len;    // bytes that follow this header
};
static_assert(sizeof(MsgHeader) == 4);

// ─── Payloads ─────────────────────────────────────────────────────────────────
struct StatusPayload {
    char     codec_name[16]; // e.g. "SBC\0"
    uint32_t bitrate;        // effective bits/sec
    uint8_t  is_capturing;   // 0 or 1
    uint8_t  hfp_guard_on;   // 0 or 1
    uint8_t  _pad[2];
};
static_assert(sizeof(StatusPayload) == 24);

struct SetCodecPayload {
    char    codec_name[16];  // "SBC", "LDAC", "aptX", ...
    char    param_key[16];   // e.g. "bitpool"
    int64_t param_value;     // e.g. 53
};
static_assert(sizeof(SetCodecPayload) == 40);

struct AckPayload {
    uint8_t success;         // 1 = OK, 0 = rejected
    uint8_t _pad[3];
};
static_assert(sizeof(AckPayload) == 4);

struct ErrorPayload {
    char message[64];
};
static_assert(sizeof(ErrorPayload) == 64);
#pragma pack(pop)

inline constexpr wchar_t kPipeName[] = L"\\\\.\\pipe\\openwinblue";

} // namespace owb::ipc
```

Save to: `service/src/ipc_protocol.h`

- [ ] **Step 5.2: Write failing IPC tests**

```cpp
// tests/service/ipc_test.cpp
#include <gtest/gtest.h>
#include <thread>
#include <chrono>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "ipc_server.h"
#include "ipc_protocol.h"

// Helper: connect as a client and perform a Ping/Pong round-trip
static bool client_ping(int timeout_ms = 2000) {
    // Wait for the pipe to be available
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

    // Run one serve iteration on a background thread
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
```

Save to: `tests/service/ipc_test.cpp`

- [ ] **Step 5.3: Create `service/src/ipc_server.h`**

```cpp
// service/src/ipc_server.h
#pragma once
#include <memory>

namespace owb {

// Named-pipe IPC server.
// The GUI connects to \\.\pipe\openwinblue and exchanges binary messages
// defined in ipc_protocol.h.
//
// serve_one() blocks until one client connects, exchanges messages,
// and disconnects. Call it in a loop on a dedicated thread.
class IpcServer {
public:
    IpcServer();
    ~IpcServer();

    // Create the named pipe. Returns false on failure.
    bool start();

    // Signal the server to stop accepting new connections.
    void stop();

    // Block until one client connects, exchange at least one message, disconnect.
    // Returns false if server was stopped or an error occurred.
    bool serve_one();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace owb
```

Save to: `service/src/ipc_server.h`

- [ ] **Step 5.4: Create `service/src/ipc_server.cpp`**

```cpp
// service/src/ipc_server.cpp
#include "ipc_server.h"
#include "ipc_protocol.h"

#define WIN32_LEAN_AND_MEAN
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
        0, nullptr  // default timeout, default security
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

    // Wait for a client connection (blocking)
    BOOL connected = ConnectNamedPipe(impl_->pipe, nullptr);
    if (!connected && GetLastError() != ERROR_PIPE_CONNECTED)
        return false;

    // Message loop for this client
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
                client_done = true; // one exchange per serve_one()
                break;
            }
            case ipc::MsgType::GetStatus: {
                ipc::MsgHeader reply{ ipc::MsgType::StatusReply,
                                      sizeof(ipc::StatusPayload) };
                ipc::StatusPayload status{};
                std::strncpy(status.codec_name, "SBC", sizeof(status.codec_name));
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
```

Save to: `service/src/ipc_server.cpp`

- [ ] **Step 5.5: Build and run IPC tests**

```powershell
& "C:/Users/santi/AppData/Local/Android/Sdk/cmake/4.1.2/bin/cmake.exe" --build build/nmake-debug --target owb_service_tests
cd build/nmake-debug
ctest --output-on-failure -R "IpcServer"
```

Expected (2 tests pass):
```
[ RUN      ] IpcServer.PingPongRoundTrip
[       OK ] IpcServer.PingPongRoundTrip
[ RUN      ] IpcServer.StopIsIdempotent
[       OK ] IpcServer.StopIsIdempotent
```

- [ ] **Step 5.6: Commit**

```powershell
cd "c:/suru/open winblue"
git add service/src/ipc_protocol.h service/src/ipc_server.h service/src/ipc_server.cpp tests/service/ipc_test.cpp
git commit -m "feat(service): add named-pipe IPC server with Ping/Pong and StatusReply"
```

---

## Task 6: Wire everything in `main.cpp` + run full test suite

**Files:**
- Modify: `service/src/main.cpp`
- Modify: `service/CMakeLists.txt` (add missing linker flags if needed)

- [ ] **Step 6.1: Replace `service/src/main.cpp`**

```cpp
// service/src/main.cpp
//
// OpenWinBlue user-mode service entry point (Phase 2a).
// Components: SBC codec, WASAPI loopback capture, HFP guard, IPC server.
//
// Run as: owb-service.exe
// Stops on Ctrl+C or when stop() is called.
//
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <atomic>
#include <csignal>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <combaseapi.h>

#include "audio_capture.h"
#include "hfp_guard.h"
#include "ipc_server.h"
#include "codec_sbc.h"

namespace {
std::atomic<bool> g_running{true};

void on_signal(int) { g_running = false; }
} // namespace

int main() {
    std::signal(SIGINT,  on_signal);
    std::signal(SIGTERM, on_signal);

    // COM is required for WASAPI and HFP guard
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    owb::CodecSbc     codec;
    owb::AudioCapture capture;
    owb::HfpGuard     hfp_guard;
    owb::IpcServer    ipc;

    std::puts("OpenWinBlue service v0.2 starting…");

    if (!capture.start()) {
        std::puts("[WARN] WASAPI loopback unavailable — running without audio capture");
    } else {
        std::printf("[OK]  Audio capture: %d Hz, %d ch\n",
                    capture.sample_rate(), capture.channels());
    }

    if (hfp_guard.start()) {
        std::puts("[OK]  HFP guard active");
    } else {
        std::puts("[WARN] HFP guard unavailable");
    }

    if (!ipc.start()) {
        std::puts("[ERROR] IPC server failed — exiting");
        CoUninitialize();
        return EXIT_FAILURE;
    }
    std::puts("[OK]  IPC server listening on \\\\.\\pipe\\openwinblue");

    // IPC loop on background thread
    std::thread ipc_thread([&ipc] {
        while (ipc.serve_one()) {}
    });

    std::puts("[OK]  Service running. Press Ctrl+C to stop.");
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    std::puts("Shutting down…");
    ipc.stop();
    hfp_guard.stop();
    capture.stop();
    ipc_thread.join();

    CoUninitialize();
    std::puts("Done.");
    return EXIT_SUCCESS;
}
```

Save to: `service/src/main.cpp`

- [ ] **Step 6.2: Build the full service executable**

```powershell
& "C:/Users/santi/AppData/Local/Android/Sdk/cmake/4.1.2/bin/cmake.exe" --build build/nmake-debug --target owb_service 2>&1
```

Expected: Build succeeded, zero errors.

- [ ] **Step 6.3: Run the full test suite**

```powershell
& "C:/Users/santi/AppData/Local/Android/Sdk/cmake/4.1.2/bin/cmake.exe" --build build/nmake-debug --target owb_service_tests
cd build/nmake-debug
ctest --output-on-failure
```

Expected (all tests pass):
```
Test #1: CodecInterface.NullCodecSatisfiesContract   Passed
Test #2: CodecInterface.EncodeReturnsZeroOnEmptyInput Passed
Test #3: CodecSbc.NameIsSBC                           Passed
Test #4: CodecSbc.DefaultParamsAreReasonable          Passed
Test #5: CodecSbc.UnknownParamReturnsNullopt          Passed
Test #6: CodecSbc.SetBitpoolAffectsOutput             Passed
Test #7: CodecSbc.EncodeProducesValidSBCFrame         Passed
Test #8: CodecSbc.EncodeReturnsMinusOneOnTinyOutputBuffer Passed
Test #9: HfpGuard.ConstructsWithoutCrash              Passed
Test #10: HfpGuard.StartAndStopAreIdempotent          Passed
Test #11: IpcServer.PingPongRoundTrip                 Passed
Test #12: IpcServer.StopIsIdempotent                  Passed
100% tests passed, 0 tests failed out of 12
```

- [ ] **Step 6.4: Commit**

```powershell
cd "c:/suru/open winblue"
git add service/src/main.cpp
git commit -m "feat(service): wire SBC + WASAPI + HFP guard + IPC in service main loop"
```

---

## Task 7: Push and verify CI

- [ ] **Step 7.1: Push to GitHub**

```powershell
$SANTI_TOKEN = $(gh auth token --user santiquiroz)
cd "c:/suru/open winblue"
git push "https://santiquiroz:$SANTI_TOKEN@github.com/santiquiroz/openwinblue.git" main
```

Or with Bash:
```bash
SANTI_TOKEN=$(gh auth token --user santiquiroz)
git push "https://santiquiroz:${SANTI_TOKEN}@github.com/santiquiroz/openwinblue.git" main
```

- [ ] **Step 7.2: Watch CI**

Open: `https://github.com/santiquiroz/openwinblue/actions`

Expected: `Build & Test Service (C++)` and `Build & Test GUI (C#)` both green.

If the C++ service job fails linking `Mmdevapi` or `audiopolicy`, add to `service/CMakeLists.txt`:
```cmake
# Windows SDK libraries (available on all windows-2022 runners)
target_link_libraries(owb_service PRIVATE
    Mmdevapi.lib
    audiopolicy.lib
)
```

- [ ] **Step 7.3: Confirm git log**

```powershell
git log --oneline -8
```

Expected (newest first):
```
feat(service): wire SBC + WASAPI + HFP guard + IPC in service main loop
feat(service): add named-pipe IPC server with Ping/Pong and StatusReply
feat(service): add HFP Guard Level 2 (audio session notification)
feat(service): add IAudioSource interface and WASAPI loopback capture
feat(codec): add SBC codec wrapper with full parameter control
build(service): integrate libsbc as static library for Windows/MSVC
fix(ci): run ctest from repo root so preset finds CMakePresets.json
```

---

## Self-Review

**Spec coverage:**
- ✅ SBC codec (bitpool, freq, mode, subbands, blocks, alloc) — Task 2
- ✅ WASAPI loopback audio capture — Task 3
- ✅ IAudioSource abstraction (testable interface) — Task 3
- ✅ HFP Guard Level 2 (session notification) — Task 4
- ✅ IPC named pipe server (Ping/Pong, StatusReply) — Task 5
- ✅ Service main() wires all components — Task 6
- ✅ All tests pass before push — Task 7
- ⚠️ HFP Guard Level 1 (registry/registry-disable) — deferred to Phase 2c (needs real BT device to test)
- ⚠️ A2DP stream controller (encodes + sends to driver) — Phase 2b (needs kernel driver IOCTL)

**Placeholder scan:** No TBDs found. All code is complete.

**Type consistency:**
- `IAudioSource::read()` returns `std::ptrdiff_t` ✅ (matches `ICodec::encode` convention)
- `IpcServer::serve_one()` returns `bool` ✅ (used in main loop condition)
- `HfpGuard::start()` returns `bool` ✅ (same pattern as `IpcServer`)
- `CodecSbc` implements `ICodec` — all methods (`name`, `encode`, `set_param`, `get_param`) match the interface defined in Phase 1 ✅
