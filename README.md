# OpenWinBlue

**Free, open-source Windows Bluetooth audio codec manager.**

OpenWinBlue replaces the Windows inbox Bluetooth A2DP driver (`btavchdt.sys`) with a custom kernel driver that unlocks codec support Windows never offered — LDAC, aptX HD, aptX Low Latency — and adds an AI enhancement pipeline that runs on any GPU via DirectML (AMD, Intel, NVIDIA, Qualcomm NPU, CPU fallback).

> **Status:** In active development. Phase 1 (foundation + SBC/aptX) in progress.  
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
| SBC (full parameters) | up to 617 kbps (Dual Ch.) | ~150ms | ✅ Phase 1 |
| aptX Classic | ~352 kbps | ~70ms | ✅ Phase 1 |
| aptX HD | ~576 kbps | ~70ms | 🔄 Phase 2 |
| aptX Low Latency | ~352 kbps | **~40ms** | 🔄 Phase 2 |
| LDAC | 330 / 660 / **990 kbps** | ~150ms | 🔄 Phase 2 |
| AAC | up to 320 kbps | ~120ms | 🔄 Phase 2 |
| LC3 (LE Audio) | Scalable | ~50ms | 🔮 Phase 3 |

### Audio Control
- Manual codec selection per device
- Full parameter control: bitrate, bitpool, sampling rate (44.1 / 48 / 88.2 / 96 kHz), channel mode
- Adaptive bitrate (auto-reduces quality before dropouts occur)
- Per-device profiles saved across sessions

### HFP / A2DP Switching Prevention
Three protection levels — registry-based, audio session interception, and kernel-level blocking — to keep your headphones in stereo A2DP mode even when apps open the microphone.

### AI Enhancement (any GPU via DirectML)
All AI runs locally using ONNX Runtime with the DirectML execution provider. Works on **any DirectX 12 GPU** — AMD, Intel, NVIDIA, Qualcomm NPU — with automatic CPU fallback.

| Feature | Engine | Added Latency | Status |
|---------|--------|--------------|--------|
| **Noise Reduction** | DeepFilterNet3 (ONNX) | ~12ms GPU / ~20ms CPU | 🔄 Phase 2 |
| **Psychoacoustic Pre-Emphasis** | Custom DSP | <1ms | 🔄 Phase 2 |
| **Smart Adaptive Bitrate** | RNN (ONNX, CPU) | <1ms | 🔄 Phase 2 |
| **Hi-Res Upsampling** | CNN (ONNX+DirectML) | ~5–40ms | 🔮 Phase 3 |
| **Voice Enhancement (HFP)** | PostGAN (ONNX) | ~5ms | 🔮 Phase 3 |

No CUDA, no vendor-specific SDKs. Single binary works on all Windows 10/11 hardware.

### Driver Management
- One-click driver installation (officially Microsoft attestation-signed on releases)
- **Guaranteed rollback** to Windows default driver — always available even if the app crashes
- Emergency `owb-rollback.bat` script for recovery without the GUI
- Test Signing Mode guidance for developers building from source

---

## Screenshots

*(Coming with first alpha release)*

---

## Installation

### Option 1 — Official Release (Recommended)
1. Download the latest installer from [Releases](https://github.com/santiquiroz/openwinblue/releases)
2. Run `OpenWinBlue-Setup-x.x.x.exe` as Administrator
3. Follow the Driver Setup Wizard — the driver is Microsoft attestation-signed, one click

**Requirements:** Windows 10 (1903+) or Windows 11, Bluetooth adapter

### Option 2 — Build from Source (Developers)
See [BUILDING.md](docs/BUILDING.md) for full instructions. Requires:
- Visual Studio 2022 with "Desktop development with C++" and ".NET desktop development" workloads
- Windows Driver Kit (WDK) 11
- .NET 8 SDK
- CMake 3.28+

> **Note:** Self-built drivers require Windows Test Signing Mode.  
> The app will guide you through enabling it on first launch.

---

## Architecture Overview

```
┌───────────────────────────────────────────┐
│          OpenWinBlue GUI (WPF / C#)        │
│  Devices │ Codec Config │ AI │ Driver Mgmt │
└──────────────────┬────────────────────────┘
                   │ IPC (Named Pipe)
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
- Kernel driver handles only transport (AVDTP signaling, L2CAP, HFP blocking) — stays small and auditable
- All audio encoding and AI processing run in user-mode service — safe to update without rebooting
- GUI communicates via named pipe — the service runs independently of the UI

---

## Tech Stack

| Layer | Technology |
|-------|-----------|
| Kernel driver | C, KMDF (WDK 11) |
| Service | C++20, Win32 APIs |
| Codecs | libldac (Apache 2.0), libopenaptx (LGPL), libsbc (LGPL), liblc3 (Apache 2.0) |
| AI inference | ONNX Runtime 1.x + DirectML EP |
| AI models | DeepFilterNet3, RNNoise (BSD), custom ONNX models |
| GUI | C# .NET 8, WPF, CommunityToolkit.Mvvm |
| Installer | WiX Toolset v4 |
| Driver signing | Microsoft Hardware Dev Center (attestation) |
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
| Rollback guarantee | ✅ (script + GUI) | Manual only |
| Source code | ✅ Fully auditable | ❌ |
| License per machine | None | Per motherboard ID |
| LE Audio / LC3 | 🔮 Phase 3 | ❌ (stated as impossible) |

---

## Roadmap

### Phase 1 — Foundation
- [x] Repository setup, CI, third-party codec vendoring
- [ ] KMDF driver skeleton + attestation signing pipeline
- [ ] AVDTP signaling state machine
- [ ] SBC codec (full parameter control)
- [ ] WASAPI audio capture + A2DP streaming
- [ ] HFP Guard (Level 1 + 2)
- [ ] WPF GUI: devices, SBC config, driver install/rollback
- [ ] WiX installer

### Phase 2 — Extended Codecs + AI
- [ ] aptX Classic, aptX HD, aptX Low Latency
- [ ] LDAC (330 / 660 / 990 kbps)
- [ ] AAC
- [ ] Kernel HFP Guard (Level 3)
- [ ] DeepFilterNet3 noise reduction (ONNX + DirectML)
- [ ] Psychoacoustic pre-emphasis DSP
- [ ] Smart adaptive bitrate (ONNX)
- [ ] RF quality indicator + bitrate history chart

### Phase 3 — Quality + Future Codecs
- [ ] LC3 / LE Audio path
- [ ] Hi-Res Upsampling (DirectML)
- [ ] Voice Enhancement for HFP (PostGAN)
- [ ] aptX Adaptive (pending Qualcomm SDK)
- [ ] High Data Throughput (Bluetooth 6.x, 8 Mbps)
- [ ] Multilanguage (ES / EN / PT / ZH)

---

## Contributing

All contributions welcome. Please read [CONTRIBUTING.md](CONTRIBUTING.md) before opening a PR.

Areas where help is most needed:
- **Driver development (C/KMDF):** AVDTP state machine, L2CAP streaming
- **Codec integration (C++):** Wrapping libldac, libopenaptx
- **AI models (Python → ONNX):** Exporting and optimizing DeepFilterNet3 and bandwidth extension models
- **Testing:** Compatibility testing across headphone chipsets and Bluetooth adapters
- **GUI (C# WPF):** UI design and implementation

---

## Security

The kernel driver (`owb_a2dp.sys`) runs in kernel mode. Security is taken seriously:
- All driver code is auditable (this repository)
- Official releases are built in GitHub Actions CI (transparent, reproducible)
- Signed by Microsoft attestation (Hardware Dev Center) — not a random signature
- Driver handles only Bluetooth audio transport; it does not access the filesystem, network, or user data
- Report vulnerabilities via GitHub Security Advisories (private)

---

## License

OpenWinBlue is licensed under the **GNU General Public License v3.0**.  
See [LICENSE](LICENSE) for the full text.

Third-party libraries retain their own licenses (Apache 2.0, LGPL 2.1+, MIT, BSD) — all compatible with GPLv3.

---

## Acknowledgements

- [Alternative A2DP Driver](https://www.bluetoothgoodies.com/a2dp/) (Luculent Systems) — the paid closed-source tool that proved this is possible on Windows, and whose FAQ was an invaluable technical reference
- [BlueZ](http://www.bluez.org/) — Linux Bluetooth stack, primary architecture reference
- [Sony LDAC AOSP](https://android.googlesource.com/platform/external/libldac) — open-source LDAC encoder
- [DeepFilterNet](https://github.com/Rikorose/DeepFilterNet) (H. Schröter et al.) — state-of-the-art open-source noise suppression
- [libopenaptx](https://github.com/pali/libopenaptx) (Pali Rohár) — open-source aptX implementation
- [Niyaz-H/LDAC-A2DP-Driver](https://github.com/Niyaz-H/LDAC-A2DP-Driver) — open KMDF driver reference implementation
