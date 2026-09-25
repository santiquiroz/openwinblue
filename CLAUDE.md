# OpenWinBlue — CLAUDE.md

Free, open-source Windows Bluetooth audio codec manager. Replaces the Windows inbox
A2DP driver (`btavchdt.sys`) to unlock LDAC, aptX, aptX HD, AAC, and adds an AI
enhancement pipeline (RNNoise today; DeepFilterNet3 via ONNX Runtime + DirectML optional).
Not implemented yet (README marks them planned): aptX Low Latency, pre-emphasis,
adaptive bitrate, HFP guard Level 2/3, LE Audio transport for LC3.
No end-to-end validation with real Bluetooth hardware so far.

---

## Project Layout

```
openwinblue/
├── driver/          # KMDF kernel driver (C) — owb_a2dp.sys
├── service/         # Win32 user-mode service (C++20) — owb-service.exe
│   ├── src/         # Service entry, stream pipeline, WASAPI capture, IPC, HFP guard
│   ├── codecs/      # Pluggable codec wrappers
│   └── ai/          # RNNoise + optional DeepFilterNet3 (ONNX Runtime + DirectML)
├── gui/             # WPF application (C# .NET 10, net10.0-windows) — OpenWinBlue.exe
│   ├── OpenWinBlue/ # App project (ViewModels/, Views/, Services/, Models/)
│   └── tests/       # xUnit tests (OpenWinBlue.Tests)
├── third-party/     # Vendored open-source codec libs (git submodules)
├── installer/       # WiX Toolset v5 installer (WixToolset.Sdk)
├── tests/           # Host GoogleTest suites: tests/service, tests/driver (WDK-free parts)
├── tools/           # Build helpers, signing scripts
└── .github/workflows/  # ci.yml: build + test; release.yml: test-signed release package
```

---

## Build Commands

### Full solution (all components)
```powershell
# Prerequisites: VS 18 2026, WDK 11, .NET 10 SDK, CMake 3.28+
# CMake: use nmake-debug preset locally (VS18) or windows-debug preset on CI (VS17 runner)
cmake --preset nmake-debug         # local build (NMake, VS18 cl.exe)
# cmake --preset windows-debug     # CI/VS17 build
dotnet build gui/OpenWinBlue.slnx  # builds GUI (.slnx format — .NET 10 SDK)
```

The CMake build does NOT build the kernel driver: it only builds the WDK-free
`owb_avdtp_packets` library so host tests can link it. The driver (`owb_a2dp.sys`)
is built with MSBuild from `driver/owb_a2dp.vcxproj` and WDK 11 (see the `build-driver`
job in `.github/workflows/ci.yml`):
```powershell
msbuild driver\owb_a2dp.vcxproj /p:Configuration=Debug /p:Platform=x64 /p:SignMode=Off /p:EnableInf2cat=false /t:Build
```

### RNNoise model (optional)
The RNNoise weights are not in the submodule. `sh download_model.sh` inside
`third-party/rnnoise` creates `src/rnnoise_data.c`; `service/ai/CMakeLists.txt` links it
when present, otherwise it links `service/ai/rnnoise_model_stub.c` (passthrough).
CI and release builds do not download it, so published binaries pass audio through.

### Service only (local — NMake)
```powershell
cmake --preset nmake-debug
cmake --build build/nmake-debug --target owb_service
```

### GUI only
```powershell
dotnet build gui/OpenWinBlue.slnx -c Release
```

### Run all tests
```powershell
# C++ tests — fails if no tests are found (CI uses --preset test-all)
ctest --preset test-nmake-debug

# C# tests
dotnet test gui/tests/OpenWinBlue.Tests/OpenWinBlue.Tests.csproj --verbosity normal
```

### Notes on environment
- **cmake**: not on PATH. Location: `C:/Users/santi/AppData/Local/Android/Sdk/cmake/4.1.2/bin/cmake.exe`
  Add to PATH or use full path. VS17 2022 generator unavailable locally (VS18 installed).
  Use `nmake-debug` preset locally.
- **Visual Studio**: VS 18 2026 Community at `C:/Program Files/Microsoft Visual Studio/18/`
  AND BuildTools at `C:/Program Files (x86)/Microsoft Visual Studio/18/BuildTools/`.
  cmake picks cl.exe from BuildTools — use **BuildTools** vcvars64.bat for builds:
  `C:/Program Files (x86)/Microsoft Visual Studio/18/BuildTools/VC/Auxiliary/Build/vcvars64.bat`
  MSVC v145 (cl.exe 19.50.35717)
- **dotnet**: .NET 10 SDK installed. Solution file uses `.slnx` format (new in .NET 10 SDK).
- **CI (GitHub Actions)**: uses `windows-2022` runner (VS17). CI workflow uses the
  `windows-debug` preset, `net10.0-windows` TFM, and `.NET 10.0.x` (setup-dotnet).

### Installer
```powershell
dotnet build installer/OpenWinBlue.wixproj -c Release
```

---

## Key Technologies

| Layer | Stack |
|-------|-------|
| Kernel driver | C, KMDF (WDK 11), INF/CAT |
| Service | C++20, Win32, WASAPI, IOCTL |
| Codec libs | libldac (Apache 2.0), libopenaptx (GPL-3.0-or-later), libsbc (LGPL-2.1-or-later), liblc3 (Apache 2.0); AAC via Windows Media Foundation |
| AI inference | RNNoise (C, CPU); optional ONNX Runtime 1.x + DirectML EP for DeepFilterNet3 (needs the SDK at build time + `deepfilter.onnx` next to the exe) |
| AI models | RNNoise (BSD); DeepFilterNet3 (.onnx, not bundled); custom ONNX models planned |
| GUI | C# .NET 10, WPF, CommunityToolkit.Mvvm, MVVM pattern |
| IPC | Named pipe (service ↔ GUI), binary length-prefixed messages (see ipc_protocol.h) |
| Installer | WiX Toolset v5 |
| Driver signing | Self-signed test signing (`tools/sign-driver.ps1`, release workflow); attestation planned |
| CI | GitHub Actions |

---

## Architecture: How the Pieces Connect

```
GUI (WPF)  ──named pipe──▶  Service (C++)  ──IOCTL──▶  Driver (KMDF)
                                  │                          │
                          ONNX+DirectML AI            BthPort.sys
                          Codec encoding              (Windows BT stack)
                          WASAPI capture              Bluetooth Radio
```

- **Driver** (`owb_a2dp.sys`): Thin kernel layer. Registers as A2DP profile driver,
  handles AVDTP signaling, L2CAP channels, HFP interception. Does NOT encode audio.
- **Service** (`owb-service.exe`): Captures audio via WASAPI, encodes with codec libs,
  sends frames to driver via IOCTL. Runs AI pipeline before encoding. Hosts IPC server.
- **GUI** (`OpenWinBlue.exe`): WPF app / system tray. Connects to service via named pipe.
  Shows device list, codec config, AI toggles, driver management.

---

## Coding Conventions

- **C/C++ style:** K&R braces, `snake_case` for functions/variables, `UPPER_SNAKE` for
  constants, `owb_` prefix on all public symbols in the service, `Owb` prefix in driver.
- **C# style:** Standard .NET conventions — PascalCase types/methods, camelCase fields,
  `_camelCase` private fields. MVVM: ViewModels in `ViewModels/`, Views in `Views/`.
- **No magic numbers:** All codec parameters, bitrates, registry keys go in constants files.
- **Error handling:**
  - Driver: always check `NT_SUCCESS(status)`, propagate NTSTATUS up the call chain.
  - Service: fallible operations return `bool` (e.g. `start()`, `open()`, `send_frame()`);
    lookups return `std::optional`; `ICodec::encode` returns bytes written or `-1`.
  - GUI: log via the static `OWBLogger` (`%LOCALAPPDATA%\OpenWinBlue\owb.log`); show user-visible
    errors through the ViewModel's `StatusMessage` (fatal startup errors: `MessageBox`) — never silently swallow.
- **No global mutable state** in service or GUI. Driver uses device extension struct only.
- **IPC protocol:** Binary, length-prefixed messages (`MsgHeader` + packed payload structs,
  little-endian) over a named pipe — see `service/src/ipc_protocol.h` / `gui/OpenWinBlue/Models/IpcProtocol.cs`.
  Service is always the server. GUI reconnects on disconnect.

---

## Important Files

| File | Purpose |
|------|---------|
| `driver/src/owb_a2dp.c` | Driver entry point, device/queue setup |
| `driver/src/avdtp.c` | AVDTP signaling state machine |
| `driver/src/l2cap_stream.c` | L2CAP media channel management |
| `driver/owb_a2dp.inf` | Driver INF — hardware IDs, service config |
| `service/src/main.cpp` | Entry point: runs as a Windows Service (SCM) or `--console` for dev |
| `service/src/stream_pipeline.cpp` | Runtime orchestrator: capture → AI → encode → driver |
| `service/src/audio_capture.cpp` | WASAPI loopback/exclusive capture |
| `service/src/a2dp_stream.cpp` | IOCTL bridge to driver (RTP framing lives in the driver) |
| `service/src/hfp_guard.cpp` | HFP/A2DP switching prevention |
| `service/src/ipc_server.cpp` | Named pipe server |
| `service/codecs/codec_interface.h` | Abstract codec interface all codecs implement |
| `service/ai/ai_pipeline.cpp` | AI chain: RNNoise `NoiseReducer` + optional `DeepFilter`, toggled via `set_param` |
| `service/ai/deep_filter.cpp` | DeepFilterNet3 ONNX Runtime session + DirectML EP (stub without the SDK) |
| `gui/OpenWinBlue/ViewModels/MainViewModel.cs` | Root VM, composes the other VMs and services |
| `gui/OpenWinBlue/ViewModels/DevicesViewModel.cs` | Bluetooth device list, per-device driver state, apply codec |
| `gui/OpenWinBlue/ViewModels/CodecViewModel.cs` | Codec/bitrate config |
| `gui/OpenWinBlue/ViewModels/DriverViewModel.cs` | Driver install/rollback |
| `gui/OpenWinBlue/ViewModels/ControlsViewModel.cs` | HFP guard and AI toggles |
| `gui/OpenWinBlue/Services/IpcClientService.cs` | Named pipe client |
| `gui/OpenWinBlue/Services/DriverInstallerService.cs` | Driver install/rollback via `pnputil` |

---

## Testing Approach

- **Driver:** only the WDK-free AVDTP packet code is tested on the host (GoogleTest,
  `tests/driver`). No WDF/HLK tests and no virtual Bluetooth device in CI yet.
- **Service:** GoogleTest (`tests/service`, FetchContent v1.14.0). The pipeline test feeds a
  fake `IAudioSource`; IPC tests run a real server on `\\.\pipe\openwinblue-test` with a fake
  codec controller; `A2dpStream` is tested in stub mode (no driver).
- **GUI:** xUnit + NSubstitute (`gui/tests/OpenWinBlue.Tests`). `MainViewModelTests` builds
  real services that call `pnputil` and the Bluetooth APIs.
- **Coverage target:** 80% line coverage for service and GUI code. Driver: 60% (kernel constraints).

---

## Driver Signing Notes

- **Development:** Enable Windows Test Signing Mode (`bcdedit /set testsigning on`, reboot).
  CI builds the driver unsigned (`SignMode=Off`).
- **Releases:** `release.yml` test-signs with a self-signed certificate via
  `tools/sign-driver.ps1` (produces `.cat`, `owb_test.cer`); the installer ships those.
  Microsoft Hardware Dev Center attestation signing is planned, not implemented.
- **Rollback emergency:** `tools/owb-rollback.bat` always present in install dir.
  Deletes `owb-service` and removes the `owb_a2dp.inf` package (its `oemNN.inf`) with
  `pnputil /delete-driver`, so Windows falls back to `btavchdt.sys` after a reboot. Works without the GUI.

---

## Third-Party Libraries (git submodules in `third-party/`)

| Dir | Repo | License |
|-----|------|---------|
| `third-party/libldac` | github.com/anonymix007/libldac (fork of AOSP libldac) | Apache 2.0 |
| `third-party/libopenaptx` | github.com/pali/libopenaptx (0.2.1) | GPL-3.0-or-later |
| `third-party/libsbc` | git.kernel.org/pub/scm/bluetooth/sbc.git | LGPL-2.1-or-later |
| `third-party/liblc3` | github.com/google/liblc3 | Apache 2.0 |
| `third-party/rnnoise` | github.com/xiph/rnnoise | BSD 3-Clause |

Not submodules: GoogleTest (BSD 3-Clause) comes via CMake FetchContent; ONNX Runtime (MIT)
is optional and detected from `ONNXRUNTIME_ROOT` or the default paths in `service/ai/CMakeLists.txt`.
`third-party/libldac-compat` and `third-party/libsbc-compat` are small tracked shim headers.

---

## Commit Message Format

```
<type>: <description>

<optional body>
```

Types: `feat`, `fix`, `refactor`, `test`, `chore`, `docs`, `ci`, `driver`, `codec`

---

## Phase Plan (high level)

| Phase | Deliverable | Status |
|-------|-------------|--------|
| **1 — Foundation** | Repo structure, CMake, .NET project, CI skeleton, submodules | ✅ Done |
| **2 — Driver MVP** | KMDF skeleton, AVDTP, SBC over L2CAP, test signing | ✅ Done |
| **3 — Service MVP** | WASAPI capture, SBC encode, A2DP stream, IPC, HFP guard L1 (L2 scaffolded only) | ✅ Done |
| **4 — GUI MVP** | WPF: device list, SBC config, driver install/rollback, tray icon | ✅ Done |
| **5 — Codecs** | aptX Classic/HD, LDAC, codec factory, AVDTP multi-codec negotiation | ✅ Done |
| **6 — AI** | RNNoise noise reduction, AiPipeline, NoiseReducer, conditional model loading (DFN3 stub) | ✅ Done |
| **7 — Polish** | LC3 encoder (LE Audio transport planned), HFP Level 1 GUI, WiX installer v0.3, CLAUDE.md final | ✅ Done |

"Done" means implemented and host-tested; validation with real Bluetooth devices is pending.
