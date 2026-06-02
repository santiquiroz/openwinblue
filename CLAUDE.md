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
├── gui/             # WPF application (C# .NET 8) — OpenWinBlue.exe
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
# Prerequisites: VS 2022, WDK 11, .NET 8 SDK, CMake 3.28+
cmake --preset windows-release    # builds driver + service
dotnet build gui/OpenWinBlue.sln  # builds GUI
```

### Driver only
```powershell
cmake --preset driver-debug
```

### Service only
```powershell
cmake --preset service-release
```

### GUI only
```powershell
dotnet build gui/OpenWinBlue.sln -c Release
```

### Run all tests
```powershell
ctest --preset test-all            # C++ unit tests (GoogleTest)
dotnet test tests/gui/             # C# unit tests (xUnit)
```

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
| GUI | C# .NET 8, WPF, CommunityToolkit.Mvvm, MVVM pattern |
| IPC | Named pipe (service ↔ GUI), JSON messages |
| Installer | WiX Toolset v4 |
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
- **IPC protocol:** All messages are UTF-8 JSON lines over a named pipe. Schema versioned.
  Service is always the server. GUI reconnects on disconnect.

---

## Important Files

| File | Purpose |
|------|---------|
| `driver/src/owb_a2dp.c` | Driver entry point, device/queue setup |
| `driver/src/avdtp.c` | AVDTP signaling state machine |
| `driver/src/l2cap_stream.c` | L2CAP media channel management |
| `driver/owb_a2dp.inf` | Driver INF — hardware IDs, service config |
| `service/src/main.cpp` | Service entry, SCM registration |
| `service/src/audio_capture.cpp` | WASAPI loopback/exclusive capture |
| `service/src/a2dp_stream.cpp` | RTP packetization + IOCTL to driver |
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

| Phase | Deliverable |
|-------|-------------|
| **1 — Foundation** | Repo structure, CMake, .NET project, CI skeleton, submodules |
| **2 — Driver MVP** | KMDF skeleton, AVDTP, SBC over L2CAP, test signing |
| **3 — Service MVP** | WASAPI capture, SBC encode, A2DP stream, IPC, HFP guard L1/L2 |
| **4 — GUI MVP** | WPF: device list, SBC config, driver install/rollback, tray icon |
| **5 — Codecs** | aptX Classic/HD/LL, LDAC, AAC |
| **6 — AI** | ONNX Runtime + DirectML, DeepFilterNet3, RNNoise, adaptive bitrate |
| **7 — Polish** | LC3, aptX Adaptive, multilanguage, Wix installer, GitHub release CI |
