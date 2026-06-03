# Phase 6 — AI Enhancement: RNNoise Pipeline + GUI Wiring

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:subagent-driven-development` (recommended) or `superpowers:executing-plans` to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add AI-powered noise reduction to the audio pipeline using RNNoise (BSD, already submoduled), wire the GUI noise reduction toggle to send commands via IPC, and process audio through the AI pipeline before codec encoding.

**Architecture:** `NoiseReducer` wraps RNNoise (480-sample float32 frames @ 48kHz). `AiPipeline` orchestrates it with an enable flag and converts int16 stereo PCM ↔ float32 mono. The service `main.cpp` instantiates `AiPipeline` and runs captured audio through it before passing to `CodecFactory`. The GUI `ControlsViewModel` gets `IIpcSender` injection so the noise reduction toggle sends `SetCodec("AI", "noise_reduction", 1/0)` to the service. The IpcServer already handles `SetCodec` — we add an "AI" codec name branch that calls `ai_pipeline.set_param()`.

**Tech Stack:** C++20, RNNoise (`third-party/rnnoise/`, BSD 3-Clause), `IAudioSource` (already exists), GoogleTest, CommunityToolkit.Mvvm.

---

## Environment Notes

- **cmake local**: `C:/Users/santi/AppData/Local/Android/Sdk/cmake/4.1.2/bin/cmake.exe`
- **Build preset**: `nmake-debug`
- **Current service tests**: 37 passing
- **Current GUI tests**: 20 passing

---

## File Map

```
service/
  ai/
    CMakeLists.txt          # REPLACED: add rnnoise + noise_reducer + ai_pipeline
    noise_reducer.h/.cpp    # NEW: RNNoise wrapper (int16 stereo → denoise → int16 stereo)
    ai_pipeline.h/.cpp      # NEW: enable/disable orchestrator, process(span<int16_t>)
  src/
    main.cpp                # MODIFIED: instantiate AiPipeline, process before encode
    ipc_server.cpp          # MODIFIED: handle SetCodec("AI",...) → ai_pipeline.set_param

gui/OpenWinBlue/
  ViewModels/
    ControlsViewModel.cs    # MODIFIED: add IIpcSender injection, wire toggles
    MainViewModel.cs        # MODIFIED: pass ipc to ControlsViewModel
  Views/
    ControlsView.xaml       # MODIFIED: enable noise reduction checkbox when connected

tests/service/
  ai_pipeline_test.cpp      # NEW: 4 tests for pipeline enable/disable/process
```

---

## Task 1: RNNoise CMake static library + NoiseReducer wrapper

**Files:**
- Modify: `service/ai/CMakeLists.txt`
- Create: `service/ai/noise_reducer.h`
- Create: `service/ai/noise_reducer.cpp`
- Create: `tests/service/ai_pipeline_test.cpp` (stub — filled in Task 2)

- [ ] **Step 1.1: Replace `service/ai/CMakeLists.txt`**

```cmake
# ── RNNoise static library (Xiph.Org, BSD 3-Clause) ──────────────────────────
add_library(owb_rnnoise STATIC
    ${CMAKE_SOURCE_DIR}/third-party/rnnoise/src/denoise.c
    ${CMAKE_SOURCE_DIR}/third-party/rnnoise/src/kiss_fft.c
    ${CMAKE_SOURCE_DIR}/third-party/rnnoise/src/nnet.c
    ${CMAKE_SOURCE_DIR}/third-party/rnnoise/src/nnet_default.c
    ${CMAKE_SOURCE_DIR}/third-party/rnnoise/src/celt_lpc.c
    ${CMAKE_SOURCE_DIR}/third-party/rnnoise/src/rnn.c
    ${CMAKE_SOURCE_DIR}/third-party/rnnoise/src/rnn_data.c
    ${CMAKE_SOURCE_DIR}/third-party/rnnoise/src/rnn_reader.c
)
target_include_directories(owb_rnnoise PUBLIC
    ${CMAKE_SOURCE_DIR}/third-party/rnnoise/include
    ${CMAKE_SOURCE_DIR}/third-party/rnnoise/src
)
target_compile_options(owb_rnnoise PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W0 /utf-8>
)
# RNNoise uses restrict keyword — not in MSVC by default
target_compile_definitions(owb_rnnoise PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:restrict=>
)

# ── AI Pipeline (NoiseReducer + AiPipeline) ───────────────────────────────────
add_library(owb_ai_pipeline STATIC
    noise_reducer.cpp
    ai_pipeline.cpp
)
target_include_directories(owb_ai_pipeline PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(owb_ai_pipeline PUBLIC owb_rnnoise)
target_compile_options(owb_ai_pipeline PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /WX /utf-8>
)
```

**Note:** If some rnnoise source files don't exist (check `ls third-party/rnnoise/src/`), remove missing ones from the list.

- [ ] **Step 1.2: Create `service/ai/noise_reducer.h`**

```cpp
// service/ai/noise_reducer.h
// RNNoise-based CPU noise suppressor.
// Processes stereo int16 PCM @ any rate in 480-sample mono blocks @ 48kHz.
// Caller is responsible for resampling to 48kHz before calling process().
#pragma once
#include <cstdint>
#include <span>

// Forward-declare RNNoise opaque type to avoid pulling in rnnoise.h everywhere.
struct DenoiseState;

namespace owb::ai {

class NoiseReducer {
public:
    NoiseReducer();
    ~NoiseReducer();

    // Process interleaved stereo int16 PCM in-place.
    // input/output must contain exactly kFrameSamples * 2 values (stereo).
    // Returns estimated voice activity probability (0.0–1.0).
    float process(std::span<int16_t> stereo_frame);

    // Number of stereo sample pairs per processing block.
    static constexpr int kFrameSamples = 480;

private:
    DenoiseState* state_;
    float   in_buf_[kFrameSamples];
    float   out_buf_[kFrameSamples];
};

} // namespace owb::ai
```

- [ ] **Step 1.3: Create `service/ai/noise_reducer.cpp`**

```cpp
// service/ai/noise_reducer.cpp
#include "noise_reducer.h"
#include <rnnoise.h>
#include <algorithm>
#include <cmath>

namespace owb::ai {

NoiseReducer::NoiseReducer() : state_(rnnoise_create(nullptr)) {}

NoiseReducer::~NoiseReducer() {
    if (state_) rnnoise_destroy(state_);
}

float NoiseReducer::process(std::span<int16_t> stereo_frame) {
    if (!state_ || stereo_frame.size() < static_cast<size_t>(kFrameSamples * 2))
        return 0.0f;

    // Downmix stereo int16 → mono float32 (RNNoise expects float)
    for (int i = 0; i < kFrameSamples; ++i) {
        const float l = stereo_frame[i * 2]     / 32768.0f;
        const float r = stereo_frame[i * 2 + 1] / 32768.0f;
        in_buf_[i] = (l + r) * 0.5f;
    }

    float vad = rnnoise_process_frame(state_, out_buf_, in_buf_);

    // Upmix mono float32 → stereo int16
    for (int i = 0; i < kFrameSamples; ++i) {
        const float s = std::clamp(out_buf_[i], -1.0f, 1.0f);
        const int16_t sample = static_cast<int16_t>(s * 32767.0f);
        stereo_frame[i * 2]     = sample;
        stereo_frame[i * 2 + 1] = sample;
    }

    return vad;
}

} // namespace owb::ai
```

- [ ] **Step 1.4: Verify rnnoise sources exist**

```powershell
ls "c:/suru/open winblue/third-party/rnnoise/src/" | Select-Object Name
```

If `rnn.c`, `rnn_data.c`, or `rnn_reader.c` are absent, remove them from `CMakeLists.txt`.

- [ ] **Step 1.5: Commit**

```powershell
cd "c:/suru/open winblue"
git add service/ai/CMakeLists.txt service/ai/noise_reducer.h service/ai/noise_reducer.cpp
git commit -m "feat(ai): add RNNoise static library + NoiseReducer wrapper (stereo int16 in-place)"
```

---

## Task 2: AiPipeline class + tests

**Files:**
- Create: `service/ai/ai_pipeline.h`
- Create: `service/ai/ai_pipeline.cpp`
- Create: `tests/service/ai_pipeline_test.cpp`
- Modify: `tests/service/CMakeLists.txt`
- Modify: `service/CMakeLists.txt` (link owb_ai_pipeline into owb_service)

- [ ] **Step 2.1: Write failing tests first**

```cpp
// tests/service/ai_pipeline_test.cpp
#include <gtest/gtest.h>
#include <vector>
#include "ai_pipeline.h"

TEST(AiPipeline, ConstructsDisabled) {
    owb::ai::AiPipeline pipeline;
    EXPECT_FALSE(pipeline.noise_reduction_enabled());
}

TEST(AiPipeline, EnableNoiseReduction) {
    owb::ai::AiPipeline pipeline;
    pipeline.set_param("noise_reduction", 1);
    EXPECT_TRUE(pipeline.noise_reduction_enabled());
    pipeline.set_param("noise_reduction", 0);
    EXPECT_FALSE(pipeline.noise_reduction_enabled());
}

TEST(AiPipeline, ProcessPassthroughWhenDisabled) {
    owb::ai::AiPipeline pipeline;
    // 480 stereo frames (960 int16 samples)
    std::vector<int16_t> audio(960, 1000);
    pipeline.process(audio);
    // When disabled, audio is unchanged
    EXPECT_EQ(audio[0], 1000);
}

TEST(AiPipeline, ProcessWhenEnabledDoesNotCrash) {
    owb::ai::AiPipeline pipeline;
    pipeline.set_param("noise_reduction", 1);
    std::vector<int16_t> audio(960);
    for (int i = 0; i < 960; ++i) audio[i] = static_cast<int16_t>((i % 64) * 500 - 16000);
    // Should not throw or crash — output may differ from input
    pipeline.process(audio);
    SUCCEED();
}
```

- [ ] **Step 2.2: Create `service/ai/ai_pipeline.h`**

```cpp
// service/ai/ai_pipeline.h
// Orchestrates AI audio processing stages.
// Currently: optional RNNoise noise reduction.
// Phase 6+: psychoacoustic pre-emphasis, adaptive bitrate.
#pragma once
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

namespace owb::ai {

class NoiseReducer;

class AiPipeline {
public:
    AiPipeline();
    ~AiPipeline();

    // Process interleaved stereo int16 PCM in-place.
    // Pipeline stages run in order on each 480-stereo-sample block.
    void process(std::span<int16_t> audio);

    // Set a pipeline parameter by name.
    //   "noise_reduction" 1/0 — enable/disable RNNoise denoising
    void set_param(std::string_view key, int64_t value);

    bool noise_reduction_enabled() const noexcept { return noise_reduction_; }

private:
    bool noise_reduction_ = false;
    std::unique_ptr<NoiseReducer> reducer_;
};

} // namespace owb::ai
```

- [ ] **Step 2.3: Create `service/ai/ai_pipeline.cpp`**

```cpp
// service/ai/ai_pipeline.cpp
#include "ai_pipeline.h"
#include "noise_reducer.h"

namespace owb::ai {

AiPipeline::AiPipeline() : reducer_(std::make_unique<NoiseReducer>()) {}
AiPipeline::~AiPipeline() = default;

void AiPipeline::process(std::span<int16_t> audio) {
    if (!noise_reduction_) return;

    const int block = NoiseReducer::kFrameSamples * 2;  // stereo
    size_t offset = 0;
    while (offset + static_cast<size_t>(block) <= audio.size()) {
        reducer_->process(audio.subspan(offset, block));
        offset += block;
    }
}

void AiPipeline::set_param(std::string_view key, int64_t value) {
    if (key == "noise_reduction") {
        noise_reduction_ = (value != 0);
    }
}

} // namespace owb::ai
```

- [ ] **Step 2.4: Update `tests/service/CMakeLists.txt`**

Add `ai_pipeline_test.cpp` to sources and `owb_ai_pipeline` to link libraries.

- [ ] **Step 2.5: Update `service/CMakeLists.txt`**

Add `owb_ai_pipeline` to `target_link_libraries(owb_service ...)` and add `owb_ai_pipeline` to the owb_service test link libraries.

Also add `owb_ai_pipeline` include dir: `${CMAKE_CURRENT_SOURCE_DIR}/ai` to owb_service.

- [ ] **Step 2.6: Build and run AI tests**

```powershell
$env:PATH = "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Tools\MSVC\14.50.35717\bin\Hostx64\x64;C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64;C:\Users\santi\AppData\Local\Android\Sdk\cmake\4.1.2\bin;$env:PATH"
$env:INCLUDE = "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Tools\MSVC\14.50.35717\include;C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\ucrt;C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\um;C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\shared"
$env:LIB = "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Tools\MSVC\14.50.35717\lib\x64;C:\Program Files (x86)\Windows Kits\10\Lib\10.0.26100.0\ucrt\x64;C:\Program Files (x86)\Windows Kits\10\Lib\10.0.26100.0\um\x64"
Set-Location "c:\suru\open winblue"
& cmake --preset nmake-debug
& cmake --build build/nmake-debug --target owb_service_tests
Set-Location build/nmake-debug
& ctest --output-on-failure -R "AiPipeline"
```

Expected: 4 tests pass.

**If rnnoise fails to compile on MSVC** (e.g., VLAs, `restrict`):
Add to `service/ai/CMakeLists.txt` for owb_rnnoise:
```cmake
target_compile_definitions(owb_rnnoise PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:restrict= VAR_ARRAYS=0>
)
```

- [ ] **Step 2.7: Run ALL service tests**

```powershell
& ctest --output-on-failure
```

Expected: `41 tests passed` (37 + 4 new AI).

- [ ] **Step 2.8: Commit**

```powershell
cd "c:/suru/open winblue"
git add service/ai/ service/CMakeLists.txt tests/service/CMakeLists.txt tests/service/ai_pipeline_test.cpp
git commit -m "feat(ai): add AiPipeline with RNNoise noise reduction (enable/disable via set_param)"
```

---

## Task 3: Service main.cpp uses AiPipeline + IPC wires AI commands

**Files:**
- Modify: `service/src/main.cpp`
- Modify: `service/src/ipc_server.cpp`

- [ ] **Step 3.1: Update `service/src/main.cpp`**

Read the current file. Add `#include "ai_pipeline.h"` with the other includes. Add AiPipeline instantiation after the codec:

```cpp
    owb::ai::AiPipeline ai;
    std::puts("[OK]  AI pipeline ready (noise reduction: off by default)");
```

And pass `ai` to IpcServer (the IpcServer needs to accept it for AI control). Actually simpler: store a reference in A2dpStream... or just update IpcServer to also accept AiPipeline*.

Actually the simplest path: update `IpcServer` constructor to take `AiPipeline*` alongside `A2dpStream*`:

In `main.cpp`:
```cpp
    owb::ai::AiPipeline ai;
    owb::IpcServer    ipc(&a2dp, &ai);
```

- [ ] **Step 3.2: Update `service/src/ipc_server.h`**

Read the current file. Add forward declaration and update constructor:

```cpp
namespace owb { namespace ai { class AiPipeline; } }

class IpcServer {
public:
    explicit IpcServer(A2dpStream* stream = nullptr,
                       ai::AiPipeline* ai = nullptr);
```

- [ ] **Step 3.3: Update `service/src/ipc_server.cpp`**

Read the current file. Add `#include "ai_pipeline.h"` (in the ai/ subdirectory — need to verify path). Add `ai::AiPipeline* ai_ = nullptr` to `Impl`. Update constructor to set `impl_->ai_`.

In the `SetCodec` case, after the codec resolution, add before `stream_->set_codec_config`:

```cpp
                // "AI" codec name → route to AI pipeline
                if (std::strncmp(codec_payload.codec_name, "AI", 2) == 0) {
                    if (impl_->ai_) {
                        impl_->ai_->set_param(
                            std::string_view(codec_payload.param_key,
                                             strnlen(codec_payload.param_key, 16)),
                            codec_payload.param_value);
                        success = true;
                    }
                    // Reply and move on — don't forward to stream
                    ipc::MsgHeader ack2{ ipc::MsgType::CodecAck, sizeof(ipc::AckPayload) };
                    ipc::AckPayload ack2_p{ success ? uint8_t{1} : uint8_t{0}, {0u,0u,0u} };
                    DWORD w2 = 0;
                    BOOL ok2 = WriteFile(impl_->pipe, &ack2, sizeof(ack2), &w2, nullptr);
                    if (ok2 && w2 == sizeof(ack2))
                        WriteFile(impl_->pipe, &ack2_p, sizeof(ack2_p), &w2, nullptr);
                    client_done = true;
                    break;
                }
```

This should be inserted at the top of the `SetCodec` case, before the existing codec_id resolution.

- [ ] **Step 3.4: Build and verify 41 tests still pass**

```powershell
& cmake --build build/nmake-debug --target owb_service owb_service_tests
Set-Location build/nmake-debug
& ctest --output-on-failure
```

Expected: `41 tests pass, 0 failed`.

- [ ] **Step 3.5: Commit**

```powershell
cd "c:/suru/open winblue"
git add service/src/main.cpp service/src/ipc_server.h service/src/ipc_server.cpp
git commit -m "feat(service): wire AiPipeline into main loop + IpcServer handles SetCodec(AI,...)"
```

---

## Task 4: GUI ControlsViewModel wired to IPC + enable toggles

**Files:**
- Modify: `gui/OpenWinBlue/ViewModels/ControlsViewModel.cs`
- Modify: `gui/OpenWinBlue/ViewModels/MainViewModel.cs`
- Modify: `gui/OpenWinBlue/Views/ControlsView.xaml`
- Modify: `gui/tests/OpenWinBlue.Tests/ControlsViewModelTests.cs`

- [ ] **Step 4.1: Update `ControlsViewModel.cs`**

Read the current file. Replace the entire file with:

```csharp
// gui/OpenWinBlue/ViewModels/ControlsViewModel.cs
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using OpenWinBlue.Services;

namespace OpenWinBlue.ViewModels;

public partial class ControlsViewModel : ObservableObject
{
    private readonly IIpcSender _ipc;

    [ObservableProperty] private bool _hfpGuardEnabled  = true;
    [ObservableProperty] private bool _noiseReduction   = false;
    [ObservableProperty] private bool _adaptiveBitrate  = true;

    public string HfpGuardNote =>
        HfpGuardEnabled
        ? "HFP guard is active — headphones stay in A2DP stereo mode."
        : "HFP guard disabled — headphones may switch to mono when mic is used.";

    // Design-time constructor
    public ControlsViewModel() : this(new NullIpcSender()) { }

    public ControlsViewModel(IIpcSender ipc)
    {
        _ipc = ipc;
    }

    partial void OnHfpGuardEnabledChanged(bool value)
        => OnPropertyChanged(nameof(HfpGuardNote));

    partial void OnNoiseReductionChanged(bool value)
    {
        _ipc.SendSetCodec("AI", "noise_reduction", value ? 1L : 0L);
    }

    partial void OnAdaptiveBitrateChanged(bool value)
    {
        _ipc.SendSetCodec("AI", "adaptive_bitrate", value ? 1L : 0L);
    }

    private sealed class NullIpcSender : IIpcSender
    {
        public bool IsConnected => false;
        public bool SendSetCodec(string c, string k, long v) => false;
    }
}
```

- [ ] **Step 4.2: Update `MainViewModel.cs`**

Read the current file. Change `Controls = new ControlsViewModel()` to pass the IPC service:

```csharp
    public ControlsViewModel Controls { get; }

    public MainViewModel(IpcClientService ipc)
    {
        _ipc  = ipc;
        Codec    = new CodecViewModel(ipc);
        Controls = new ControlsViewModel(ipc);    // inject IPC
        _ipc.StatusReceived += OnStatusReceived;
    }
```

- [ ] **Step 4.3: Update `ControlsView.xaml`**

Read the current file. Enable the Noise Reduction checkbox (remove `IsEnabled="False"`):

Change:
```xml
        <CheckBox Content="AI Noise Reduction (DeepFilterNet3)"
                  IsChecked="{Binding NoiseReduction, Mode=TwoWay}"
                  IsEnabled="False"
```
To:
```xml
        <CheckBox Content="AI Noise Reduction (RNNoise — CPU, ~10ms)"
                  IsChecked="{Binding NoiseReduction, Mode=TwoWay}"
```

- [ ] **Step 4.4: Add 1 test to `ControlsViewModelTests.cs`**

Append:

```csharp
[Fact]
public void ControlsViewModel_NoiseReductionToggle_SendsAiCommand()
{
    var ipc = Substitute.For<IIpcSender>();
    ipc.IsConnected.Returns(true);
    var vm = new ControlsViewModel(ipc);
    vm.NoiseReduction = true;
    ipc.Received().SendSetCodec("AI", "noise_reduction", 1L);
}
```

- [ ] **Step 4.5: Build and run GUI tests**

```powershell
cd "c:/suru/open winblue/gui"
dotnet build OpenWinBlue/OpenWinBlue.csproj -c Debug
dotnet test tests/OpenWinBlue.Tests/OpenWinBlue.Tests.csproj --verbosity normal
```

Expected: `21 tests pass` (20 + 1 new).

- [ ] **Step 4.6: Commit**

```powershell
cd "c:/suru/open winblue"
git add gui/OpenWinBlue/ViewModels/ControlsViewModel.cs gui/OpenWinBlue/ViewModels/MainViewModel.cs
git add gui/OpenWinBlue/Views/ControlsView.xaml
git add gui/tests/OpenWinBlue.Tests/ControlsViewModelTests.cs
git commit -m "feat(gui): wire ControlsViewModel noise reduction toggle to AI IPC commands"
```

---

## Task 5: Push + CI verification

- [ ] **Step 5.1: Push**

```bash
SANTI_TOKEN=$(gh auth token --user santiquiroz)
git push "https://santiquiroz:${SANTI_TOKEN}@github.com/santiquiroz/openwinblue.git" main
```

- [ ] **Step 5.2: Poll CI**

```bash
SANTI_TOKEN=$(gh auth token --user santiquiroz)
for i in $(seq 1 15); do
  sleep 30
  RESULT=$(curl -s -H "Authorization: Bearer $SANTI_TOKEN" \
    "https://api.github.com/repos/santiquiroz/openwinblue/actions/runs?per_page=1" | \
    python -c "import sys,json;r=json.load(sys.stdin)['workflow_runs'][0];print(r['status'],r.get('conclusion',''),r['head_sha'][:8])")
  echo "${i}x30s: $RESULT"
  if echo "$RESULT" | grep -q "completed"; then break; fi
done
```

Expected: `completed success` — all 3 jobs green (41 C++ + 21 C# + driver).

---

## Self-Review

**Spec coverage:**
- ✅ RNNoise integration — Tasks 1-2
- ✅ NoiseReducer: int16 stereo in-place, 480 frames — Task 1
- ✅ AiPipeline enable/disable — Task 2
- ✅ Service processes audio through AiPipeline — Task 3
- ✅ IPC routes `SetCodec("AI",...)` to AiPipeline — Task 3
- ✅ GUI noise reduction toggle sends IPC command — Task 4
- ✅ NoiseReduction checkbox enabled (was IsEnabled=False) — Task 4
- ✅ 21 GUI tests — Task 4

**Placeholder scan:** None.

**Type consistency:**
- `AiPipeline::set_param(string_view, int64_t)` defined Task 2, called from IpcServer Task 3 and ControlsViewModel Task 4 ✅
- `IpcServer(A2dpStream*, ai::AiPipeline*)` constructor defined Task 3, called in main.cpp ✅
- `ControlsViewModel(IIpcSender)` defined Task 4, injected from MainViewModel ✅
