# OpenWinBlue — CLAUDE.md

Free, open-source Windows Bluetooth audio codec manager. Replaces the Windows inbox
A2DP driver (`btavchdt.sys`) to unlock LDAC, aptX HD, aptX LL, AAC, and adds an AI
enhancement pipeline via ONNX Runtime + DirectML (any DX12 GPU).

---

## Project Layout

```
openwinblue/
├── driver/          # KMDF kernel driver (C) — owb_a2dp.sys
├── service/         # Win32 user-mode service (C++20) — owb-service.exe
│   └── codecs/      # Pluggable codec wrappers
│   └── ai/          # ONNX Runtime + DirectML AI pipeline
├── gui/             # WPF application (C# .NET 10, net10.0-windows) — OpenWinBlue.exe
├── third-party/     # Vendored open-source codec libs (git submodules)
├── installer/       # WiX Toolset v4 installer
├── tests/           # Unit + integration tests
├── tools/           # Build helpers, signing scripts
└── .github/workflows/  # CI: build, test, driver attestation signing
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
# C++ tests — from build dir
cd build/nmake-debug && ctest --output-on-failure

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
| Codec libs | libldac (Apache 2.0), libopenaptx (LGPL), libsbc (LGPL), liblc3 (Apache 2.0) |
| AI inference | ONNX Runtime 1.x + DirectML EP (any DX12 GPU / NPU / CPU fallback) |
| AI models | DeepFilterNet3 (.onnx), RNNoise (BSD), custom ONNX models |
| GUI | C# .NET 10, WPF, CommunityToolkit.Mvvm, MVVM pattern |
| IPC | Named pipe (service ↔ GUI), binary length-prefixed messages (see ipc_protocol.h) |
| Installer | WiX Toolset v5 |
| Driver signing | Microsoft Hardware Dev Center attestation (CI pipeline) |
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
  - Service: `std::expected<T, owb_error>` for fallible operations (C++23-style, backported).
  - GUI: log via `ILogger`, show user-visible errors in a dedicated error banner — never silently swallow.
- **No global mutable state** in service or GUI. Driver uses device extension struct only.
- **IPC protocol:** Binary, length-prefixed messages (`MsgHeader` + packed payload structs,
  little-endian) over a named pipe — see `service/src/ipc_protocol.h` / `gui/Models/IpcProtocol.cs`.
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
| `service/ai/ai_pipeline.cpp` | ONNX Runtime session management, DirectML EP |
| `gui/ViewModels/MainViewModel.cs` | Root VM, service IPC client |
| `gui/ViewModels/DeviceViewModel.cs` | Per-device state |
| `gui/ViewModels/CodecViewModel.cs` | Codec/bitrate config |
| `gui/ViewModels/DriverViewModel.cs` | Driver install/rollback |
| `gui/ViewModels/AiViewModel.cs` | AI features toggle/config |
| `gui/Services/IpcClient.cs` | Named pipe client |
| `gui/Services/DriverInstaller.cs` | Elevated driver install/uninstall |

---

## Testing Approach

- **Driver:** Windows Driver Framework (WDF) unit test framework + Hardware Lab Kit (HLK)
  for integration. For CI: use virtual BT device via HID emulator.
- **Service:** GoogleTest. Codec wrappers tested with reference PCM files. IPC tested with
  mock pipe. Audio capture tested with WASAPI loopback mock.
- **GUI:** xUnit + WPF test host. ViewModels tested with mock IPC client.
- **Coverage target:** 80% line coverage for service and GUI code. Driver: 60% (kernel constraints).

---

## Driver Signing Notes

- **Development:** Enable Windows Test Signing Mode (`bcdedit /set testsigning on`, reboot).
  CI uses a self-signed test cert for PRs.
- **Releases:** GitHub Actions submits to Microsoft Hardware Dev Center via `winget-pkg` /
  Partner Center API for attestation signing. The signed `.cat` + `.sys` go into the installer.
- **Rollback emergency:** `tools/owb-rollback.bat` always present in install dir.
  Re-enables `btavchdt.sys`, unregisters service. Works without the GUI.

---

## Third-Party Libraries (git submodules in `third-party/`)

| Dir | Repo | License |
|-----|------|---------|
| `third-party/libldac` | android.googlesource.com/platform/external/libldac | Apache 2.0 |
| `third-party/libopenaptx` | github.com/pali/libopenaptx | LGPL 2.1+ |
| `third-party/libsbc` | git.kernel.org/pub/scm/bluetooth/bluez (sbc dir) | LGPL 2.1 |
| `third-party/liblc3` | github.com/google/liblc3 | Apache 2.0 |
| `third-party/rnnoise` | github.com/xiph/rnnoise | BSD 3-Clause |
| `third-party/onnxruntime` | NuGet: Microsoft.ML.OnnxRuntime.DirectML | MIT |
| `third-party/googletest` | github.com/google/googletest | BSD 3-Clause |

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
| **3 — Service MVP** | WASAPI capture, SBC encode, A2DP stream, IPC, HFP guard L1/L2 | ✅ Done |
| **4 — GUI MVP** | WPF: device list, SBC config, driver install/rollback, tray icon | ✅ Done |
| **5 — Codecs** | aptX Classic/HD, LDAC, codec factory, AVDTP multi-codec negotiation | ✅ Done |
| **6 — AI** | RNNoise noise reduction, AiPipeline, NoiseReducer, conditional model loading | ✅ Done |
| **7 — Polish** | LC3 (LE Audio), HFP Level 1 GUI, WiX installer v0.3, CLAUDE.md final | ✅ Done |
