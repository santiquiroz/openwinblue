# OpenWinBlue

**Free, open-source Windows Bluetooth audio codec manager.**

OpenWinBlue replaces the Windows inbox Bluetooth A2DP driver (`btavchdt.sys`) with a custom kernel driver that unlocks codec support Windows never offered — LDAC, aptX, aptX HD — and adds an AI enhancement pipeline (RNNoise noise reduction today; DirectML models for any GPU planned).

> **Status:** Software audio path wired end-to-end — WASAPI capture → codec encode → driver IOCTL runs on a dedicated pipeline inside a real Windows Service (`owb-service`). Driver installs in test-signing mode. Now in hardware validation with real Bluetooth devices. Seeking USB Bluetooth dongle testers and attestation-signing contributors.  
> Contributions welcome — see [CONTRIBUTING.md](CONTRIBUTING.md).

---

## Why This Exists

Windows gives you zero control over Bluetooth audio quality:

- No codec selection — Windows picks automatically, often the worst option
- **No LDAC support** — Sony never licensed it into the Windows stack
- **No aptX HD or aptX Low Latency** — not present in any Windows version
- **Automatic HFP switching** — any app that touches your microphone drops your headphones from stereo A2DP to mono 8 kHz "hands-free" mode, destroying quality for the rest of the session
- SBC locked at 44.1 kHz with no bitpool control
- No feedback on which codec is actually active

The only solution was [Alternative A2DP Driver](https://www.bluetoothgoodies.com/a2dp/) — which works well but costs $17, is closed source, ties the license to your motherboard, and requires a crack to use for free.

OpenWinBlue is the free, open-source, GPLv3-licensed alternative.

---

## Features

### Codec Support
| Codec | Bitrate | Latency | Status |
|-------|---------|---------|--------|
| SBC (full parameters) | up to 617 kbps (Dual Ch.) | ~150ms | ✅ Done |
| aptX Classic | ~352 kbps | ~70ms | ✅ Done |
| aptX HD | ~576 kbps | ~70ms | ✅ Done |
| aptX Low Latency | ~352 kbps | **~40ms** | 🔮 Planned (not implemented) |
| LDAC | 330 / 660 / **990 kbps** | ~150ms | ✅ Done |
| AAC | up to 320 kbps | ~120ms | ✅ Done |
| LC3 (LE Audio) | Scalable | ~50ms | 🧪 Encoder only (LE Audio transport planned; driver falls back to SBC) |
| aptX Adaptive | Scalable | ~50ms | 🔮 Pending Qualcomm SDK |

✅ Done = encoder implemented and unit-tested in the service, and the driver's AVDTP code can configure the codec. Validation with real Bluetooth devices is still pending.

### GUI — Device-Centric Interface
- **Auto-detects connected Bluetooth devices** via Win32 `BluetoothFindFirstDevice` API
- **Auto-refreshes** on device connect/disconnect via `WM_DEVICECHANGE` (800ms debounce)
- Per-device view with codec estimates based on brand/model name heuristics
- Live driver status per device (Windows inbox / OpenWinBlue / unknown)
- Tray icon for background operation

### Audio Control
- Manual codec selection per device
- Bitrate control (up to 990 kbps for LDAC)
- Adaptive bitrate (auto-reduces quality before dropouts) — **planned**: the GUI toggle exists, but the service ignores it for now

### HFP / A2DP Switching Prevention
Layered protection to keep headphones in stereo A2DP mode:
- **Level 1 (implemented)** — Registry/service control: GUI toggle that disables `BthHFSrv`
- **Level 2 (planned)** — Audio session interception (`HfpGuard` session hooks are scaffolded)
- **Level 3 (planned)** — Kernel-level blocking in the driver

### AI Enhancement
All AI runs locally. RNNoise runs on the CPU (plain C library). DeepFilterNet3 uses ONNX Runtime with the DirectML execution provider (any DirectX 12 GPU, CPU fallback), but only when the service is built against the ONNX Runtime SDK and a `deepfilter.onnx` model sits next to `owb_service.exe`; otherwise it is a passthrough stub.

| Feature | Engine | Added Latency | Status |
|---------|--------|--------------|--------|
| Noise Reduction (voice/HFP) | RNNoise; DeepFilterNet3 optional | ~12ms GPU / ~20ms CPU | ✅ RNNoise (needs the downloaded model, see below; DFN3 model pending) |
| Psychoacoustic Pre-Emphasis | Custom DSP | <1ms | 🔮 Planned (not implemented) |
| Smart Adaptive Bitrate | RNN (ONNX, CPU) | <1ms | 🔮 Planned (GUI toggle only; the service ignores it) |
| Hi-Res Upsampling | CNN (ONNX+DirectML) | ~5–40ms | 🔮 Phase 4 |
| Voice Enhancement (HFP) | PostGAN (ONNX) | ~5ms | 🔮 Phase 4 |

**RNNoise model:** the weights are not part of the `rnnoise` submodule. Run `sh download_model.sh` inside `third-party/rnnoise` (it creates `src/rnnoise_data.c`) before configuring CMake; without it the build links a passthrough stub (`service/ai/rnnoise_model_stub.c`). CI and release builds do not download the model, so published binaries currently pass audio through unchanged when noise reduction is enabled.

### Driver Management
- One-click driver installation with UAC elevation
- Automatic Test Signing Mode detection and guided activation
- **Guaranteed rollback** to Windows default driver
- Emergency `owb-rollback.bat` script for recovery without GUI
- `pnputil` output of every install/rollback is written to the application log (`owb.log`)

---

## Screenshots

*(Coming with first alpha release)*

---

## Installation (Beta — Test Signing Required)

> This beta requires Windows Test Signing Mode because the driver is not yet Microsoft attestation-signed.
> Test Signing Mode requires **Secure Boot to be disabled** in BIOS.

### Steps

1. Download `OpenWinBlue-Setup.msi` from [Releases](https://github.com/santiquiroz/openwinblue/releases)
2. Run the installer as Administrator
3. Open **OpenWinBlue** (right-click → Run as Administrator, or use the desktop shortcut)
4. If Test Signing Mode is not active, the app detects it automatically — click **Activate Test Signing** and approve the UAC prompt
5. **Restart Windows** — required for Test Signing to take effect (you'll see a small watermark on the desktop, this is normal)
6. Pair your Bluetooth headphones/headset from **Windows Settings → Bluetooth** first
7. Open OpenWinBlue, select your device in the list, click **Install Driver** and approve UAC
8. Once installed, select your codec and bitrate

### Requirements
- Windows 10 (1903+) or Windows 11
- Bluetooth USB adapter or built-in Bluetooth
- Secure Boot **disabled** in BIOS (required for test-signed driver)
- Administrator account

### Verifying Test Signing is Active

Run in PowerShell after restarting:
```powershell
bcdedit | Select-String "testsigning"
# Should show: testsigning   Yes
```

---

## Troubleshooting & Logs

All application events are logged to:
```
%LOCALAPPDATA%\OpenWinBlue\owb.log
```

The file rotates automatically when it exceeds 2 MB (previous log saved as `owb.log.bak`).

### View the log

```powershell
# Live tail
Get-Content "$env:LOCALAPPDATA\OpenWinBlue\owb.log" -Wait -Tail 50

# Last 100 lines
Get-Content "$env:LOCALAPPDATA\OpenWinBlue\owb.log" -Tail 100

# Filter errors only
Select-String "ERROR" "$env:LOCALAPPDATA\OpenWinBlue\owb.log"
```

### Driver installation log (pnputil output)

```powershell
Select-String "pnputil" "$env:LOCALAPPDATA\OpenWinBlue\owb.log" -Context 0,10
```

### Verify driver is registered

```powershell
pnputil /enum-drivers | Select-String "owb"
```

### Verify Test Signing is active

```powershell
bcdedit | Select-String "testsigning"
```

### Windows Event Log (driver/BT errors)

```powershell
Get-WinEvent -LogName System |
  Where-Object { $_.Message -match "owb|a2dp|pnputil|bluetooth" } |
  Select-Object -First 20 | Format-List TimeCreated, Message
```

### Emergency rollback (no GUI needed)

If the GUI does not open or the driver causes issues, run as Administrator:
```
C:\Program Files\OpenWinBlue\owb-rollback.bat
```

---

## Build from Source

### Requirements
- Visual Studio 2022+ with "Desktop development with C++" workload
- Windows Driver Kit (WDK) 11 (10.0.26100.0+) — only for the kernel driver
- .NET 10 SDK
- CMake 3.28+
- WiX Toolset v5 — restored automatically as the `WixToolset.Sdk` NuGet package by `dotnet build`

Clone with `--recurse-submodules` (codec libraries live in `third-party/`). CMake builds the service and the host unit tests, **not** the kernel driver: the driver is built with MSBuild from `driver/owb_a2dp.vcxproj`, as the `build-driver` job in `.github/workflows/ci.yml` does.

### Quick build

Run from a Visual Studio developer prompt (`vcvars64.bat`), at the repository root:

```powershell
# 1. Service + host unit tests (CMake, NMake generator)
#    With the "Visual Studio 17 2022" generator use the windows-debug preset instead (as CI does).
cmake --preset nmake-debug
cmake --build build/nmake-debug
ctest --preset test-nmake-debug

# 2. Kernel driver (requires WDK 11) -> driver\x64\Debug\owb_a2dp.sys
msbuild driver\owb_a2dp.vcxproj /p:Configuration=Debug /p:Platform=x64 /p:SignMode=Off /p:EnableInf2cat=false /t:Build

# 3. Test-sign the driver and assemble the package the Debug installer expects
#    (creates owb_a2dp.cat, owb_test.cer and signed copies in build\driver)
.\tools\sign-driver.ps1 -InfPath driver\owb_a2dp.inf -SysPath driver\x64\Debug\owb_a2dp.sys -OutDir build\driver

# 4. GUI + installer
dotnet publish gui/OpenWinBlue/OpenWinBlue.csproj -c Debug -r win-x64 --self-contained true -p:PublishSingleFile=true -o gui/OpenWinBlue/bin/Publish/win-x64
dotnet build installer/OpenWinBlue.wixproj -c Debug
# Output: installer/bin/x64/Debug/OpenWinBlue-Setup.msi
```

---

## Architecture

```
┌───────────────────────────────────────────┐
│          OpenWinBlue GUI (WPF / C#)        │
│  Devices │ Codec Config │ AI │ Driver Mgmt │
│  Auto-refresh via WM_DEVICECHANGE          │
│  Win32 BluetoothFindFirstDevice API        │
└──────────────────┬────────────────────────┘
                   │ IPC (Named Pipe "openwinblue")
┌──────────────────▼────────────────────────┐
│       OpenWinBlue Service (C++, Win32)     │
│  AI Pipeline (RNNoise/ONNX) → Codec Enc   │
│  → A2DP Stream → HFP Guard → IPC Server   │
└──────────────────┬────────────────────────┘
                   │ IOCTL
┌──────────────────▼────────────────────────┐
│    owb_a2dp.sys — KMDF Kernel Driver       │
│  Replaces btavchdt.sys │ AVDTP signaling   │
│  L2CAP channels │ HFP block (planned)      │
└──────────────────┬────────────────────────┘
                   │
       BthPort.sys (Windows BT stack)
       Bluetooth Radio
```

**Key design choices:**
- Kernel driver handles only transport — stays small and auditable. Its control device is
  restricted to SYSTEM/Administrators (SDDL), so only the service can inject frames.
- All audio encoding and AI processing run in a user-mode Windows Service (`owb-service`,
  auto-start LocalSystem) — safe to update without rebooting. A dedicated `StreamPipeline`
  thread drives capture → encode → driver; RTP/media framing is applied per codec in the driver.
- GUI communicates via named pipe — the service runs independently of the UI
- Driver is installed via `pnputil` with universal A2DP UUID matching, not device-specific

---

## Tech Stack

| Layer | Technology |
|-------|-----------|
| Kernel driver | C, KMDF (WDK 11) |
| Service | C++20, Win32 APIs |
| Codecs | libldac (Apache 2.0), libopenaptx (GPL-3.0-or-later), libsbc (LGPL-2.1-or-later), liblc3 (Apache 2.0), AAC via Windows Media Foundation |
| AI inference | RNNoise (C, CPU); optional ONNX Runtime 1.x + DirectML EP for DeepFilterNet3 |
| AI models | RNNoise (BSD); DeepFilterNet3 and custom ONNX models planned |
| GUI | C# .NET 10, WPF, CommunityToolkit.Mvvm |
| Installer | WiX Toolset v5 |
| Driver signing | Self-signed test signing (`tools/sign-driver.ps1`); Microsoft Hardware Dev Center attestation planned |
| CI/CD | GitHub Actions |

---

## Open Source Codec Libraries

| Library | Codec | License |
|---------|-------|---------|
| [libldac](https://github.com/anonymix007/libldac) (fork of Sony's AOSP libldac) | LDAC encoder | Apache 2.0 |
| [libopenaptx](https://github.com/pali/libopenaptx) 0.2.1 | aptX Classic + HD | GPL-3.0-or-later |
| [libsbc](https://git.kernel.org/pub/scm/bluetooth/sbc.git) (BlueZ project) | SBC | LGPL-2.1-or-later |
| [liblc3](https://github.com/google/liblc3) (Google) | LC3 | Apache 2.0 |
| [DeepFilterNet](https://github.com/Rikorose/DeepFilterNet) | Noise reduction model (optional, not bundled) | MIT |
| [RNNoise](https://github.com/xiph/rnnoise) (Xiph) | Lightweight noise suppression | BSD |

---

## Comparison with Alternative A2DP Driver

| Feature | OpenWinBlue | Alt. A2DP Driver |
|---------|-------------|-----------------|
| Price | **Free forever** | $17 (+ crack to bypass) |
| License | **GPLv3 (open source)** | Proprietary |
| LDAC | ✅ | ✅ |
| aptX HD | ✅ | ✅ |
| aptX Low Latency | 🔮 Planned | Yes |
| HFP prevention | ✅ (Level 1; L2/L3 planned) | Partial |
| AI Enhancement | ✅ RNNoise (CPU); DirectML models planned | ❌ |
| Rollback guarantee | ✅ (script + GUI + log) | Manual only |
| Application log | ✅ `%LOCALAPPDATA%\OpenWinBlue\owb.log` | ❌ |
| Source code | ✅ Fully auditable | ❌ |
| License per machine | None | Per motherboard ID |
| LE Audio / LC3 | 🧪 LC3 encoder only; LE Audio transport planned | ❌ (stated as impossible) |

---

## Roadmap

### Phase 1 — Foundation (HFP Level 2 pending)
- [x] KMDF driver skeleton + AVDTP signaling state machine
- [x] SBC codec (full parameter control)
- [x] WASAPI audio capture + A2DP streaming
- [x] HFP Guard Level 1
- [ ] HFP Guard Level 2 (session hooks scaffolded only)
- [x] WPF GUI: driver install/rollback, SBC config
- [x] WiX installer

### Phase 2 — Extended Codecs + AI (partial)
- [x] aptX Classic, aptX HD
- [ ] aptX Low Latency
- [x] LDAC (330 / 660 / 990 kbps)
- [x] AAC
- [x] LC3 encoder
- [ ] LE Audio transport for LC3
- [x] RNNoise noise reduction pipeline (model downloaded separately)
- [x] Optional ONNX Runtime + DirectML wrapper for DeepFilterNet3 (passthrough without SDK + model)
- [ ] Psychoacoustic pre-emphasis
- [ ] Smart adaptive bitrate (GUI toggle exists; service ignores it)

### Phase 3 — GUI Polish + Installer ✅
- [x] Device-centric UI (single view, no tabs)
- [x] Win32 `BluetoothFindFirstDevice` API for device enumeration
- [x] Auto-refresh on BT connect/disconnect (`WM_DEVICECHANGE`)
- [x] Per-device driver status detection
- [x] Codec estimation by brand/model heuristics
- [x] HFP Level 1 controls in GUI
- [x] Desktop shortcut in MSI installer
- [x] Test Signing Mode detection + guided activation
- [x] Centralized application log (`%LOCALAPPDATA%\OpenWinBlue\owb.log`)
- [x] WiX installer v0.3.0

### Next — Hardware Testing & Production Signing
- [ ] End-to-end test with real Bluetooth A2DP device (in progress)
- [ ] Microsoft Hardware Dev Center attestation signing
- [ ] DeepFilterNet3 full ONNX model (replace stub)
- [ ] Multilanguage (ES / EN / PT / ZH)
- [ ] aptX Adaptive (pending Qualcomm SDK)
- [ ] Hi-Res Upsampling (DirectML)
- [ ] Per-device codec profiles saved across sessions

---

## Contributing

All contributions welcome. Please read [CONTRIBUTING.md](CONTRIBUTING.md) before opening a PR.

Areas where help is most needed:
- **Driver testing:** Compatibility across headphone chipsets and Bluetooth adapters
- **Driver development (C/KMDF):** AVDTP state machine, L2CAP streaming
- **Codec integration (C++):** Wrapping libldac, libopenaptx
- **AI models (Python → ONNX):** Exporting and optimizing DeepFilterNet3
- **GUI (C# WPF):** UI design and improvements

---

## Security

The kernel driver (`owb_a2dp.sys`) runs in kernel mode. Security is taken seriously:
- All driver code is auditable (this repository)
- Official releases will be built in GitHub Actions CI (transparent, reproducible)
- Planned signing via Microsoft Hardware Dev Center attestation
- Driver handles only Bluetooth audio transport — no filesystem, network, or user data access
- Report vulnerabilities via GitHub Security Advisories (private)

---

## License

OpenWinBlue is licensed under the **GNU General Public License v3.0**.  
See [LICENSE](LICENSE) for the full text.

Third-party libraries retain their own licenses (Apache 2.0, GPL-3.0-or-later, LGPL-2.1-or-later, MIT, BSD) — all compatible with GPLv3. See [third-party/LICENSES.md](third-party/LICENSES.md).

---

## Acknowledgements

- [Alternative A2DP Driver](https://www.bluetoothgoodies.com/a2dp/) (Luculent Systems) — the paid closed-source tool that proved this is possible on Windows
- [BlueZ](http://www.bluez.org/) — Linux Bluetooth stack, primary architecture reference
- [Sony LDAC AOSP](https://android.googlesource.com/platform/external/libldac) — open-source LDAC encoder
- [DeepFilterNet](https://github.com/Rikorose/DeepFilterNet) (H. Schröter et al.) — state-of-the-art open-source noise suppression
- [libopenaptx](https://github.com/pali/libopenaptx) (Pali Rohár) — open-source aptX implementation
- [Niyaz-H/LDAC-A2DP-Driver](https://github.com/Niyaz-H/LDAC-A2DP-Driver) — open KMDF driver reference implementation
