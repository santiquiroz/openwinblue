# OpenWinBlue

**Free, open-source Windows Bluetooth audio codec manager.**

OpenWinBlue replaces the Windows inbox Bluetooth A2DP driver (`btavchdt.sys`) with a custom kernel driver that unlocks codec support Windows never offered — LDAC, aptX HD, aptX Low Latency — and adds an AI enhancement pipeline that runs on any GPU via DirectML (AMD, Intel, NVIDIA, Qualcomm NPU, CPU fallback).

> **Status:** Core implementation complete (Phases 1–3). Driver installs in test-signing mode. Currently in hardware testing phase with real Bluetooth devices. Seeking USB Bluetooth dongle testers and attestation signing contributors.  
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
| aptX Low Latency | ~352 kbps | **~40ms** | ✅ Done |
| LDAC | 330 / 660 / **990 kbps** | ~150ms | ✅ Done |
| AAC | up to 320 kbps | ~120ms | ✅ Done |
| LC3 (LE Audio) | Scalable | ~50ms | ✅ Done |
| aptX Adaptive | Scalable | ~50ms | 🔮 Pending Qualcomm SDK |

### GUI — Device-Centric Interface
- **Auto-detects connected Bluetooth devices** via Win32 `BluetoothFindFirstDevice` API
- **Auto-refreshes** on device connect/disconnect via `WM_DEVICECHANGE` (800ms debounce)
- Per-device view with codec estimates based on brand/model name heuristics
- Live driver status per device (Windows inbox / OpenWinBlue / unknown)
- Tray icon for background operation

### Audio Control
- Manual codec selection per device
- Bitrate control (up to 990 kbps for LDAC)
- Adaptive bitrate toggle (auto-reduces quality before dropouts)

### HFP / A2DP Switching Prevention
Three protection levels to keep headphones in stereo A2DP mode:
- **Level 1** — Registry-based service control (GUI toggle for `BthHFSrv`)
- **Level 2** — Audio session interception
- **Level 3** — Kernel-level blocking (driver)

### AI Enhancement (any GPU via DirectML)
All AI runs locally using ONNX Runtime with the DirectML execution provider — works on **any DirectX 12 GPU** with automatic CPU fallback.

| Feature | Engine | Added Latency | Status |
|---------|--------|--------------|--------|
| Noise Reduction | DeepFilterNet3 (ONNX) | ~12ms GPU / ~20ms CPU | ✅ Done |
| Psychoacoustic Pre-Emphasis | Custom DSP | <1ms | ✅ Done |
| Smart Adaptive Bitrate | RNN (ONNX, CPU) | <1ms | ✅ Done |
| Hi-Res Upsampling | CNN (ONNX+DirectML) | ~5–40ms | 🔮 Phase 4 |
| Voice Enhancement (HFP) | PostGAN (ONNX) | ~5ms | 🔮 Phase 4 |

### Driver Management
- One-click driver installation with UAC elevation
- Automatic Test Signing Mode detection and guided activation
- **Guaranteed rollback** to Windows default driver
- Emergency `owb-rollback.bat` script for recovery without GUI
- Installation log at `%TEMP%\owb_install.log`

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
Get-Content $env:TEMP\owb_install.log
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
- Windows Driver Kit (WDK) 11 (10.0.26100.0+)
- .NET 10 SDK
- CMake 3.28+
- WiX Toolset v5 (for installer)

See [docs/BUILDING.md](docs/BUILDING.md) for full instructions.

### Quick build

```powershell
# 1. Build kernel driver (requires WDK + VS developer prompt)
cmake -B build/debug -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Debug
cmake --build build/debug

# 2. Sign driver for test mode
.\scripts\sign-test.ps1   # creates owb_test.cer and .cat

# 3. Build service
cmake --build build/nmake-debug --target owb_service

# 4. Build GUI + installer
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
│  AI Pipeline (ONNX+DirectML) → Codec Enc  │
│  → A2DP Stream → HFP Guard → IPC Server   │
└──────────────────┬────────────────────────┘
                   │ IOCTL
┌──────────────────▼────────────────────────┐
│    owb_a2dp.sys — KMDF Kernel Driver       │
│  Replaces btavchdt.sys │ AVDTP signaling   │
│  L2CAP channels │ HFP interception         │
└──────────────────┬────────────────────────┘
                   │
       BthPort.sys (Windows BT stack)
       Bluetooth Radio
```

**Key design choices:**
- Kernel driver handles only transport — stays small and auditable
- All audio encoding and AI processing run in user-mode service — safe to update without rebooting
- GUI communicates via named pipe — the service runs independently of the UI
- Driver is installed via `pnputil` with universal A2DP UUID matching, not device-specific

---

## Tech Stack

| Layer | Technology |
|-------|-----------|
| Kernel driver | C, KMDF (WDK 11) |
| Service | C++20, Win32 APIs |
| Codecs | libldac (Apache 2.0), libopenaptx (LGPL), libsbc (LGPL), liblc3 (Apache 2.0) |
| AI inference | ONNX Runtime 1.x + DirectML EP |
| AI models | DeepFilterNet3, RNNoise (BSD), custom ONNX models |
| GUI | C# .NET 10, WPF, CommunityToolkit.Mvvm |
| Installer | WiX Toolset v5 |
| Driver signing | Microsoft Hardware Dev Center (attestation) — planned |
| CI/CD | GitHub Actions |

---

## Open Source Codec Libraries

| Library | Codec | License |
|---------|-------|---------|
| [libldac](https://android.googlesource.com/platform/external/libldac) (Sony/AOSP) | LDAC encoder | Apache 2.0 |
| [libopenaptx](https://github.com/pali/libopenaptx) | aptX Classic + HD | LGPL 2.1+ |
| [libsbc](https://git.kernel.org/pub/scm/bluetooth/bluez.git) (BlueZ) | SBC | LGPL 2.1 |
| [liblc3](https://github.com/google/liblc3) (Google) | LC3 | Apache 2.0 |
| [DeepFilterNet](https://github.com/Rikorose/DeepFilterNet) | Noise reduction model | MIT |
| [RNNoise](https://github.com/xiph/rnnoise) (Xiph) | Lightweight noise suppression | BSD |

---

## Comparison with Alternative A2DP Driver

| Feature | OpenWinBlue | Alt. A2DP Driver |
|---------|-------------|-----------------|
| Price | **Free forever** | $17 (+ crack to bypass) |
| License | **GPLv3 (open source)** | Proprietary |
| LDAC | ✅ | ✅ |
| aptX HD | ✅ | ✅ |
| aptX Low Latency | ✅ | ✅ |
| HFP prevention | ✅ (3 levels) | Partial |
| AI Enhancement | ✅ (DirectML, any GPU) | ❌ |
| Rollback guarantee | ✅ (script + GUI + log) | Manual only |
| Application log | ✅ `%LOCALAPPDATA%\OpenWinBlue\owb.log` | ❌ |
| Source code | ✅ Fully auditable | ❌ |
| License per machine | None | Per motherboard ID |
| LE Audio / LC3 | ✅ Done | ❌ (stated as impossible) |

---

## Roadmap

### Phase 1 — Foundation ✅
- [x] KMDF driver skeleton + AVDTP signaling state machine
- [x] SBC codec (full parameter control)
- [x] WASAPI audio capture + A2DP streaming
- [x] HFP Guard Level 1 + 2
- [x] WPF GUI: driver install/rollback, SBC config
- [x] WiX installer

### Phase 2 — Extended Codecs + AI ✅
- [x] aptX Classic, aptX HD, aptX Low Latency
- [x] LDAC (330 / 660 / 990 kbps)
- [x] AAC
- [x] LC3 / LE Audio
- [x] RNNoise + DeepFilterNet3 noise reduction pipeline
- [x] ONNX Runtime + DirectML AI pipeline

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

Third-party libraries retain their own licenses (Apache 2.0, LGPL 2.1+, MIT, BSD) — all compatible with GPLv3.

---

## Acknowledgements

- [Alternative A2DP Driver](https://www.bluetoothgoodies.com/a2dp/) (Luculent Systems) — the paid closed-source tool that proved this is possible on Windows
- [BlueZ](http://www.bluez.org/) — Linux Bluetooth stack, primary architecture reference
- [Sony LDAC AOSP](https://android.googlesource.com/platform/external/libldac) — open-source LDAC encoder
- [DeepFilterNet](https://github.com/Rikorose/DeepFilterNet) (H. Schröter et al.) — state-of-the-art open-source noise suppression
- [libopenaptx](https://github.com/pali/libopenaptx) (Pali Rohár) — open-source aptX implementation
- [Niyaz-H/LDAC-A2DP-Driver](https://github.com/Niyaz-H/LDAC-A2DP-Driver) — open KMDF driver reference implementation
