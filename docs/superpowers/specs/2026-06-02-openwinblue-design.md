# OpenWinBlue — Design Specification

**Date:** 2026-06-02  
**Status:** Draft — Pending User Review  
**Project:** Free, open-source Windows Bluetooth audio codec manager

---

## 1. Problem Statement

Windows exposes no user interface for selecting Bluetooth audio codecs. Its default A2DP driver:

- Fixes codec priority (aptX Adaptive → AAC → aptX Classic → SBC) with no way to override
- Does not support LDAC, aptX HD, or aptX Low Latency at all
- Automatically switches to Hands-Free Profile (HFP, mono 8–16 kHz) whenever any application opens the headset microphone or creates a "Communications"-category audio stream, permanently degrading quality for the rest of the session
- Locks SBC at 44.1 kHz only, preventing full bitpool and dual-channel configurations
- Provides no feedback on which codec is actually active or on RF link quality

Paid alternatives (Alternative A2DP Driver, ~$17) solve most of this but are closed source, use a proprietary license tied to motherboard hardware ID, require cracking to run for free, and offer no AI-enhancement roadmap.

---

## 2. Goals

| Goal | Priority |
|------|----------|
| Free and permanently open source (GPLv3) | Critical |
| LDAC, aptX HD, aptX LL, aptX Classic, AAC, SBC, LC3 codec support | Critical |
| Prevent automatic HFP/A2DP switching | Critical |
| Manual and adaptive bitrate control | Critical |
| Low latency (match or beat Windows inbox driver) | Critical |
| Sampling-rate selection (44.1 / 48 / 88.2 / 96 kHz) | High |
| Safe driver rollback to Windows inbox driver | Critical |
| Intuitive GUI with real-time codec/signal status | High |
| AI audio enhancement pipeline (noise reduction, upsampling) | Future |
| Windows 10 (1903+) and Windows 11 support | High |
| aptX Adaptive and LE Audio readiness | Future |

---

## 3. State of the Art — Research Summary

### 3.1 Windows Bluetooth Audio Stack

The Windows Classic Bluetooth audio stack has two layers:

```
Applications / WASAPI
       ↓
  Audio Engine (audiodg.exe)
       ↓
  btavchdt.sys  ←── this is what we replace / extend
  (inbox A2DP driver — handles codec negotiation, encoding, L2CAP packetization)
       ↓
  BthPort.sys (Bluetooth protocol stack — L2CAP, HCI)
       ↓
  Bluetooth Radio (USB/PCIe)
```

Key facts:
- `btavchdt.sys` handles the full A2DP source role: codec capability exchange with headphone, audio encoding, L2CAP streaming
- Windows 10 supports: aptX Classic, SBC  
- Windows 11 adds: AAC (21H2+), aptX Adaptive lossless (24H2+, Qualcomm radios only)
- **LDAC, aptX HD, aptX LL are never in the Windows inbox driver**
- HFP switching is automatic and triggered by: (a) microphone endpoint opened, (b) Communications-category audio stream

### 3.2 LE Audio (2025–2026)

Bluetooth LE Audio with LC3 codec is rolling out in Windows 11 22H2+ but remains hardware-gated (Qualcomm Snapdragon X, select Copilot+ PCs as of mid-2026). The Bluetooth SIG is working on High Data Throughput (8 Mbps), which will enable lossless and spatial audio in a future Bluetooth version. Our project targets Classic BT audio now but should design for LE Audio extension.

### 3.3 Codec Landscape

| Codec | Max Bitrate | Latency | Windows Native | Open Source Library | License |
|-------|------------|---------|----------------|--------------------|---------| 
| SBC | ~328 kbps (JT) / ~617 kbps (DC) | ~150 ms | Yes (all) | libsbc (BlueZ) | LGPL |
| AAC | ~250 kbps (device MTU limited) | ~120 ms | Win11 21H2+ | libfdk-aac | Fraunhofer (free but patent-encumbered) |
| aptX Classic | ~352 kbps | ~70 ms | Yes (Win10+) | libopenaptx | LGPL 2.1+ |
| aptX HD | ~576 kbps | ~70 ms | No | libopenaptx | LGPL 2.1+ |
| aptX Low Latency | ~352 kbps | ~40 ms | No | libopenaptx (partial) | LGPL 2.1+ |
| aptX Adaptive | 279–420 kbps (lossy) / up to 1.2 Mbps (lossless) | ~40–80 ms | Win11 24H2 (Qualcomm only) | None (proprietary) | Proprietary |
| LDAC | 330 / 660 / 990 kbps | ~150+ ms | No | libldac (Android AOSP) | Apache 2.0 |
| LC3 | Configurable (scalable) | ~50–80 ms | Win11 22H2+ (hardware gated) | liblc3 (Google) | Apache 2.0 |

**License compatibility with GPLv3:** Apache 2.0 ✅, LGPL 2.1+ ✅, Fraunhofer ⚠️ (patent issue — AAC support will use FAAD2/FAAC under LGPL instead)

### 3.4 Existing Open Source Reference Projects

| Project | Approach | Status | Relevance |
|---------|----------|--------|-----------|
| Niyaz-H/LDAC-A2DP-Driver | KMDF kernel driver + C# WPF GUI | Active (2024) | Best technical reference for driver structure |
| birdybro/OpenA2DP | Win32 APIs + WASAPI + registry tricks | Active | Reference for user-mode BT interaction |
| pali/libopenaptx | aptX / aptX HD codec library (LGPL) | Mature | Direct codec dependency |
| wdv4758h/libldac | LDAC encoder (Apache 2.0, mirrors AOSP) | Mature | Direct codec dependency |
| google/liblc3 | LC3 codec (Apache 2.0) | Active | Future LC3 support |
| BlueZ (Linux) | Full open BT stack with LDAC/aptX | Mature | Architecture reference |

### 3.5 Driver Signing Reality

Windows 10/11 (since 1607) requires kernel-mode drivers to carry a Microsoft-countersigned signature. Options:

| Method | Cost | Works On | Suitable For OSS? |
|--------|------|----------|-------------------|
| WHQL Certification | High (HLK lab testing) | Retail Windows Update | Overkill |
| Attestation Signing (Hardware Dev Center) | **Free** (needs EV cert for account) | Win10/11 Desktop | **Yes — recommended path** |
| Test Signing Mode (`bcdedit /set testsigning on`) | Free | Dev/test only | Dev only |
| OpenCore-based DSE bypass | Free | Varies | Fragile, security risk |

**Conclusion:** Use attestation signing. The one-time EV certificate (~$300) can be obtained via the project's open-source organization/sponsor, or the project ships the driver source and a signed binary from CI (GitHub Actions + Microsoft Partner Center). Users in a pinch can enable Test Signing Mode.

### 3.6 AI Audio Enhancement (State of Art 2025)

Real-time AI audio enhancement viable for integration:

| Model | Latency | Use Case | License |
|-------|---------|----------|---------|
| RNNoise (Mozilla) | ~10–20 ms | Noise reduction (voice) | BSD/MIT |
| DeepFilterNet 3 | ~10–20 ms | Full-spectrum denoising | MIT |
| NU-Wave / AERO | ~offline | Audio super-resolution (upsampling) | Research |
| Custom adaptive bitrate CNN | ~5 ms inference | RF-quality-driven bitrate control | We build it |

The viable Phase 1 AI feature: **RNNoise or DeepFilterNet** as an optional pre-encoding noise-reduction step. This improves perceived quality especially at lower bitrates. Audio upsampling (24-bit / 96 kHz reconstruction from 16-bit / 44.1 kHz source) is viable with NU-Wave or AERO models but adds significant CPU load.

---

## 4. Architecture

### 4.1 Chosen Approach: Phased Kernel + User-Mode Hybrid

After evaluating three approaches:

**Approach A — Pure user-mode registry manipulation:**  
Pros: No driver signing needed. Cons: Only controls parameters the inbox driver exposes. Cannot add LDAC or aptX HD. Cannot prevent HFP switching at the driver level.

**Approach B — Full kernel driver replacement (like AltA2DP):**  
Pros: Full A2DP source control, all codecs, lowest latency. Cons: Requires signed kernel driver, complex kernel audio pipeline.

**Approach C — Hybrid (recommended):**  
Phase 1: KMDF shim driver + user-mode encoding service (simpler kernel code, codec logic stays in user mode). Phase 2: Migrate encoding into kernel for minimum latency. This balances maintainability with power.

**Recommended: Approach C (Hybrid)**

### 4.2 Component Overview

```
┌─────────────────────────────────────────────────────────────────────────┐
│                         OpenWinBlue GUI (WPF)                           │
│  Device List │ Codec Selector │ Bitrate/Sampling │ Driver Mgmt │ AI     │
└─────────────────────┬───────────────────────────────────────────────────┘
                      │ IPC (Named Pipe / COM)
┌─────────────────────▼───────────────────────────────────────────────────┐
│                  OpenWinBlue Service (C++, Win32)                        │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐  ┌────────────┐  │
│  │ BT Device    │  │ Codec Engine │  │ Audio Capture│  │ HFP Guard  │  │
│  │ Manager      │  │ (libldac,    │  │ (WASAPI      │  │ (prevents  │  │
│  │ (Win32 BT    │  │  libopenaptx,│  │  loopback or │  │  A2DP→HFP  │  │
│  │  APIs)       │  │  libsbc,     │  │  virtual src)│  │  switch)   │  │
│  │              │  │  liblc3)     │  │              │  │            │  │
│  └──────┬───────┘  └──────┬───────┘  └──────┬───────┘  └──────┬─────┘  │
│         │                 │                  │                  │        │
│  ┌──────▼─────────────────▼──────────────────▼──────────────────▼─────┐  │
│  │              A2DP Stream Controller                                 │  │
│  │  (L2CAP channel management, RTP packetization, bitrate feedback)    │  │
│  └──────────────────────────────────────────────────────────────────┘  │
└─────────────────────┬───────────────────────────────────────────────────┘
                      │ IOCTL / Registry
┌─────────────────────▼───────────────────────────────────────────────────┐
│              OpenWinBlue Kernel Driver (KMDF, C)                         │
│  ┌──────────────────────────────────────────────────────────────────┐   │
│  │  owb_a2dp.sys                                                     │   │
│  │  - Registers as A2DP Profile Driver (replaces btavchdt.sys)       │   │
│  │  - Handles BRB (Bluetooth Request Blocks) for L2CAP channels       │   │
│  │  - Exposes IOCTL interface to user-mode service                    │   │
│  │  - Intercepts profile selection (blocks unwanted HFP switch)       │   │
│  └──────────────────────────────────────────────────────────────────┘   │
└─────────────────────┬───────────────────────────────────────────────────┘
                      │
┌─────────────────────▼───────────────────────────────────────────────────┐
│              BthPort.sys (Windows inbox BT stack — unchanged)            │
│              Bluetooth Radio (USB adapter / PCIe / built-in)             │
└─────────────────────────────────────────────────────────────────────────┘
```

### 4.3 Kernel Driver (`owb_a2dp.sys`)

**Responsibility:** Replace `btavchdt.sys` as the A2DP profile driver. Handle:
- BRB_REGISTER_PSM for AVDTP (Audio/Video Distribution Transport Protocol, PSM 0x0019)
- A2DP AVDTP signaling: DISCOVER, GET_CAPABILITIES, SET_CONFIGURATION, START, SUSPEND, CLOSE
- Codec capability advertisement (what codecs we support, negotiated with headphone)
- L2CAP media channel management
- IOCTL interface for user-mode service: send encoded frames, receive RF feedback, get/set codec config

**What it does NOT do (stays in user mode):**
- Audio encoding/decoding (expensive, easier to maintain in user mode)
- AI enhancement
- GUI logic

**Driver signing:** Attestation-signed binary distributed via GitHub Releases CI pipeline. Source always available for local compilation + test signing during development.

### 4.4 User-Mode Service (`owb-service.exe`)

**Responsibility:** The "brain" of the system. Runs as a Windows Service.

- **BT Device Manager:** Enumerate paired Bluetooth audio devices, detect capabilities, manage pairing events
- **Codec Engine:** Abstract codec pipeline. Pluggable codec modules:
  - `codec_sbc.cpp` — wraps libsbc, exposes bitpool/subbands/channel/freq parameters
  - `codec_aptx.cpp` — wraps libopenaptx (aptX Classic + HD)
  - `codec_aptx_ll.cpp` — wraps libopenaptx (Low Latency variant)
  - `codec_ldac.cpp` — wraps libldac (Sony AOSP encoder)
  - `codec_aac.cpp` — wraps FAAD2/FAAC (LGPL)
  - `codec_lc3.cpp` — wraps liblc3 (future, LE Audio path)
- **Audio Capture:** WASAPI loopback capture from the current default audio endpoint, or exclusive WASAPI for lowest latency
- **HFP Guard:** Monitors audio session events via `IAudioSessionNotification`. When a Communications-category stream is opened, applies mitigation strategy (configurable: block HFP switch, disable headset mic endpoint, or notify user)
- **A2DP Stream Controller:** Packs encoded frames into RTP/AVDTP packets, sends via IOCTL to kernel driver. Monitors L2CAP buffer fill level for adaptive bitrate feedback
- **IPC Server:** Named pipe server exposing configuration and status to the GUI

### 4.5 GUI Application (`OpenWinBlue.exe`)

**Technology:** C# .NET 8, WPF, MVVM (CommunityToolkit.Mvvm), system tray icon.

**Main views:**

1. **Devices Panel**
   - List of paired Bluetooth audio devices with status indicators (Connected / Active / A2DP / HFP)
   - Per-device codec badge showing currently active codec
   - Quick "Force A2DP" button

2. **Codec Configuration Panel** (context-sensitive per selected device)
   - Codec picker (radio buttons with quality/latency indicator per codec)
   - Codec-specific parameter sliders:
     - LDAC: Quality mode (Best / Balanced / Connection Priority / Manual), bitrate (330 / 660 / 990 kbps)
     - SBC: Bitpool (0–64 + override), channel mode (Joint Stereo / Dual Channel / Stereo / Mono), subbands (4 / 8), freq (44.1 / 48 kHz)
     - aptX: Sampling frequency (44.1 / 48 kHz)
     - aptX HD: Sampling frequency (44.1 / 48 kHz), bit depth (24-bit)
     - AAC: Bitrate (128 / 192 / 256 / 320 kbps), VBR/CBR
   - Adaptive bitrate toggle (auto-reduce quality on poor RF)
   - Sampling rate override (44.1 / 48 / 88.2 / 96 kHz where codec supports)

3. **Audio Quality Panel**
   - Real-time audio level meter
   - Codec effective bitrate meter
   - RF link quality indicator (from RSSI or L2CAP retransmission rate)
   - Latency estimate display per codec

4. **HFP Control Panel**
   - Toggle: "Prevent automatic HFP switching"
   - Option: "Disable headset microphone endpoint while A2DP is active"
   - Option: "Notify only — allow HFP switch but alert me"
   - Current profile indicator (A2DP Stereo / HFP / Switching...)

5. **Driver Management Panel**
   - Driver status: Installed / Not installed / Update available
   - [Install OpenWinBlue Driver] button
   - [Restore Windows Default Driver] button — uninstalls owb_a2dp.inf, re-enables btavchdt.sys
   - Driver version, signing status

6. **Settings**
   - Start minimized to tray
   - Start with Windows
   - Language (es/en)
   - AI Enhancement section (Phase 2): noise reduction toggle, model selection

### 4.6 HFP Prevention Strategy (Critical Feature)

Three levels of protection, user-configurable:

**Level 1 — Registry-based (no driver needed):**  
Disable `Handsfree Telephony` service on the device via registry (`HKLM\SYSTEM\CurrentControlSet\Enum\BTHENUM\...\Services`). Survives reboot. Drawback: disables headset mic completely.

**Level 2 — Audio Session Interception (user-mode service):**  
The service subscribes to `IAudioSessionNotification`. When a Communications stream is detected, it immediately sets the default communications device to a different endpoint (e.g., internal microphone), preventing Windows from routing to the headset mic. The headset stays in A2DP mode.

**Level 3 — Kernel Driver (deepest control):**  
`owb_a2dp.sys` intercepts the HFP profile activation BRB and can refuse or delay it, keeping A2DP active. This is the most robust option but requires the kernel driver to be installed.

---

## 5. Repository Structure

```
openwinblue/
├── driver/                    # KMDF kernel driver (C)
│   ├── src/
│   │   ├── owb_a2dp.c        # Driver entry, device/queue setup
│   │   ├── avdtp.c           # AVDTP signaling state machine
│   │   ├── codec_caps.c      # Codec capability negotiation
│   │   ├── l2cap_stream.c    # L2CAP media channel
│   │   └── ioctl.c           # IOCTL interface to user mode
│   ├── owb_a2dp.inf          # Driver INF file
│   └── CMakeLists.txt
│
├── service/                   # User-mode Windows Service (C++20)
│   ├── src/
│   │   ├── main.cpp
│   │   ├── bt_device_manager.cpp
│   │   ├── audio_capture.cpp
│   │   ├── a2dp_stream.cpp
│   │   ├── hfp_guard.cpp
│   │   └── ipc_server.cpp
│   ├── codecs/
│   │   ├── codec_interface.h  # Abstract codec interface
│   │   ├── codec_sbc.cpp
│   │   ├── codec_aptx.cpp
│   │   ├── codec_aptx_ll.cpp
│   │   ├── codec_ldac.cpp
│   │   ├── codec_aac.cpp
│   │   └── codec_lc3.cpp     # Future
│   ├── ai/
│   │   ├── noise_reducer.cpp  # RNNoise / DeepFilterNet wrapper
│   │   └── upsampler.cpp      # Audio super-resolution (future)
│   └── CMakeLists.txt
│
├── gui/                       # WPF GUI (C# .NET 8)
│   ├── OpenWinBlue.csproj
│   ├── App.xaml
│   ├── ViewModels/
│   │   ├── MainViewModel.cs
│   │   ├── DeviceViewModel.cs
│   │   ├── CodecViewModel.cs
│   │   ├── DriverViewModel.cs
│   │   └── HfpViewModel.cs
│   ├── Views/
│   │   ├── MainWindow.xaml
│   │   ├── DevicesPanel.xaml
│   │   ├── CodecPanel.xaml
│   │   ├── DriverPanel.xaml
│   │   └── HfpPanel.xaml
│   ├── Models/
│   │   ├── BluetoothDevice.cs
│   │   ├── CodecProfile.cs
│   │   └── DriverStatus.cs
│   └── Services/
│       ├── IpcClient.cs       # Named pipe client to service
│       └── DriverInstaller.cs
│
├── third-party/               # Vendored open-source codec libs
│   ├── libldac/              # Sony LDAC (Apache 2.0)
│   ├── libopenaptx/          # aptX/aptX HD (LGPL 2.1+)
│   ├── libsbc/               # SBC from BlueZ (LGPL)
│   ├── liblc3/               # LC3 (Apache 2.0, Google)
│   └── rnnoise/              # Noise reduction (BSD)
│
├── installer/                 # NSIS or WiX installer
├── tests/
│   ├── unit/
│   └── integration/
├── docs/
│   └── superpowers/specs/
├── .github/
│   └── workflows/
│       ├── build.yml
│       └── sign-driver.yml    # Attestation signing via WHDC
├── CMakeLists.txt
├── README.md
└── LICENSE                    # GPLv3
```

---

## 6. Technology Stack

| Layer | Technology | Rationale |
|-------|-----------|-----------|
| Kernel driver | C, KMDF (WDK 11) | Mandatory for A2DP profile replacement |
| User-mode service | C++20, Win32 | Low overhead, direct BT/audio API access |
| Codec libs | C (libldac, libopenaptx, libsbc, liblc3) | Battle-tested, appropriate licenses |
| GUI | C# .NET 8, WPF | Mature ecosystem, system tray support, MVVM |
| GUI framework | CommunityToolkit.Mvvm | Official MS MVVM toolkit |
| Build system | CMake + dotnet CLI | Cross-component build |
| Installer | WiX Toolset v4 | Industry-standard Windows installer |
| Driver signing | Microsoft Hardware Dev Center (attestation) | Free for open-source maintainers |
| CI/CD | GitHub Actions | Build, test, sign, publish releases |
| License | GPLv3 | Compatible with LGPL codec libs |

---

## 7. Codec Priority and Latency Targets

When connecting to a device, OpenWinBlue negotiates codecs in this priority order (user-configurable):

```
1. aptX Low Latency   ~40 ms   (if device supports — gaming/video priority)
2. aptX Adaptive      ~40–80 ms (if device + Qualcomm radio supports)
3. aptX HD            ~70 ms
4. LDAC HQ (990 kbps) ~150 ms  (if user selected quality priority)
5. aptX Classic       ~70 ms
6. AAC                ~120 ms
7. SBC (high bitpool) ~150 ms
8. SBC (standard)     ~150 ms   (fallback, always works)
```

User can override priority order and force a specific codec per device.

---

## 8. Driver Installation and Rollback

### Install flow
1. GUI calls `DriverInstaller.cs` which runs an elevated installer process
2. Installer runs `pnputil /add-driver owb_a2dp.inf /install` (standard Windows driver installation)
3. Windows Device Manager shows the new driver under "Bluetooth audio devices"
4. Service is installed and started

### Rollback flow
1. GUI calls rollback
2. Service sends IOCTL to driver to suspend streams gracefully
3. `pnputil /delete-driver owb_a2dp.inf /uninstall /reboot` restores `btavchdt.sys`
4. Service is stopped and removed
5. System prompts for reboot if needed

### Safe mode
If the system fails to boot with OpenWinBlue driver active, Windows safe mode automatically disables non-essential drivers. From safe mode, user can run the rollback manually or use the provided `owb-rollback.bat` script.

---

## 9. Phase Plan

### Phase 1 — Foundation (MVP)
- [ ] Repository setup, CI, third-party codec vendoring
- [ ] KMDF driver skeleton: device/queue setup, INF, attestation signing CI pipeline
- [ ] AVDTP signaling state machine (DISCOVER, GET_CAPS, SET_CONFIG, START)
- [ ] SBC codec (first working codec, mandatory per spec)
- [ ] WASAPI audio capture pipeline in service
- [ ] L2CAP media streaming (RTP + A2DP packetization)
- [ ] IPC (named pipe) between service and GUI
- [ ] WPF GUI: device list, SBC configuration, driver install/rollback, HFP guard Level 1 & 2
- [ ] Installer (WiX)

### Phase 2 — Extended Codecs
- [ ] aptX Classic codec integration (libopenaptx)
- [ ] aptX HD codec integration (libopenaptx)
- [ ] aptX Low Latency codec integration
- [ ] LDAC codec integration (libldac)
- [ ] AAC codec integration (FAAC/FAAD2)
- [ ] Adaptive bitrate controller (L2CAP buffer feedback loop)
- [ ] RF quality indicator in GUI (from retransmission rate)
- [ ] Kernel HFP guard (Level 3 interception in owb_a2dp.sys)

### Phase 3 — Quality & AI
- [ ] LC3 codec integration (liblc3, LE Audio path)
- [ ] RNNoise integration (optional noise reduction pre-encoding)
- [ ] WASAPI exclusive mode for minimum kernel-to-codec latency
- [ ] Audio super-resolution / upsampling (pre-encoding, optional)
- [ ] aptX Adaptive (pending Qualcomm SDK availability)
- [ ] Real-time spectral analyzer in GUI
- [ ] Multi-language localization (ES / EN / PT / ZH)

---

## 10. Key Technical Challenges

### Challenge 1: AVDTP Signaling Correctness
The A2DP AVDTP protocol is complex (discover, get capabilities, set configuration, open, start, suspend, close, abort). Incorrect implementation causes devices to refuse the connection or fall back to SBC. Mitigation: Reference BlueZ's avdtp.c as implementation guide. Test against multiple headphone chipsets.

### Challenge 2: Driver Signing for Open Source
Attestation signing requires an EV certificate to register the Hardware Dev Center account (cost: ~$300/year). Mitigation: Project GitHub sponsors fund this. The CI pipeline handles signing automatically on tagged releases. Development builds use Test Signing Mode with clear instructions.

### Challenge 3: HFP Switching Prevention Reliability
Level 2 (audio session interception) works for most apps but may race-condition against Windows HFP activation. Mitigation: Kernel-level interception (Level 3) is the definitive fix, delivered in Phase 2.

### Challenge 4: Per-Device Hardware ID Matching
Different headphone chipsets (Qualcomm, Mediatek, Actions Semi) report different A2DP capability fields. Some devices claim codec support and then reject the codec during connection (known issue with Edifier, Bose from AltA2DP FAQ). Mitigation: Build a community-maintained compatibility database (JSON file in repo) with per-device quirks/workarounds.

### Challenge 5: LDAC Decoder (for A2DP Sink)
Sony's LDAC encoder is open source (Apache 2.0) but the decoder is proprietary. OpenWinBlue only implements the A2DP source role (PC → headphones), so we only need the encoder. This is architecturally clean.

---

## 11. Licensing

| Component | License | Why |
|-----------|---------|-----|
| OpenWinBlue kernel driver | GPLv3 | Copyleft, promotes openness |
| OpenWinBlue service | GPLv3 | Compatible with LGPL codec libs |
| OpenWinBlue GUI | GPLv3 | Unified license |
| libldac | Apache 2.0 | Compatible with GPLv3 |
| libopenaptx | LGPL 2.1+ | Compatible with GPLv3 |
| libsbc | LGPL 2.1 | Compatible with GPLv3 |
| liblc3 | Apache 2.0 | Compatible with GPLv3 |
| RNNoise | BSD 3-Clause | Compatible with GPLv3 |

**Note on AAC patents:** The fdk-aac Fraunhofer license is not GPLv3-compatible because it restricts commercial patent use. The project will use FAAC (LGPL) for AAC encoding instead, which has slightly lower quality but is fully open and license-compatible.

---

## 12. Future Vision

The Bluetooth audio landscape is moving toward:
- **High Data Throughput (HDT)** in Bluetooth 6.x (2026+): up to 8 Mbps, enabling true lossless HD audio
- **aptX Lossless / LDAC at 990 kbps**: effectively transparent audio over BT
- **Spatial audio over BT**: multi-channel streaming via LE Audio
- **AI codec enhancement**: Using small transformer models or RNN to reconstruct high-frequency content lost by lossy codecs — effectively getting near-lossless perceived quality from LDAC 660 kbps streams
- **Adaptive neural bitrate**: ML model predicting optimal bitrate from RF environment features, reducing dropouts without sacrificing average quality

OpenWinBlue's plugin codec architecture is designed to accommodate all of these as additional `codec_*.cpp` modules and kernel IOCTL extensions.

---

## 13. Security Considerations

- Driver runs in kernel mode: must be carefully audited for buffer overflows, integer overflows, and null pointer dereferences
- Service runs as LocalSystem only during driver IOCTL; audio capture can run as LocalService
- IPC pipe has DACL restricting access to the local user's session
- Driver rollback is always available without requiring the service to be running
- No telemetry, no network calls from the driver or service

---

---

## 14. AI Enhancement System (Deep Dive)

### 14.1 Design Principle: Universal GPU Compatibility

All AI features use a single inference stack: **ONNX Runtime + DirectML execution provider**. This gives:

| Hardware | Supported via | Notes |
|----------|--------------|-------|
| NVIDIA GPU (DX12) | DirectML EP | All cards since GTX 10xx |
| AMD GPU (DX12) | DirectML EP | All cards since RX 400 |
| Intel iGPU/Arc | DirectML EP | Iris Xe and newer |
| Qualcomm NPU | DirectML / DirectML 2.0 | Copilot+ Snapdragon X |
| Intel NPU | DirectML 2.0 | Copilot+ Lunar Lake |
| AMD NPU (XDNA) | DirectML 2.0 | Copilot+ Ryzen AI 300 |
| CPU (any) | ONNX Runtime CPU EP | Always available fallback |

**Fallback chain per inference session:**
```
NPU (DirectML 2.0) → Discrete GPU (DirectML) → iGPU (DirectML) → CPU (ORT CPU EP)
```
The user can override the device selection in Settings, or leave it on "Auto" and the system picks the fastest available.

All models are distributed as `.onnx` files alongside the application. No Python, no PyTorch, no CUDA drivers required. ONNX Runtime is a single DLL (~10 MB).

### 14.2 AI Feature Modules

#### Module 1 — Noise Reduction (Phase 2, pre-encoding)

**What it does:** Removes background noise from the system audio before encoding and sending to the headphones. Primarily useful for microphone-fed scenarios (calls) but also cleans up playback of noisy recordings.

**Model:** DeepFilterNet3 (exported to ONNX)
- 3 ONNX components: `dfn3_enc.onnx`, `dfn3_erb_dec.onnx`, `dfn3_df_dec.onnx`
- Real-time: yes — processes 20ms frames, per-frame GPU latency ~1.7ms (CPU ~10ms)
- License: MIT
- Quality: PESQ 3.5–4.0+, STOI >0.95 — best open-source model for full-spectrum denoising

**Lightweight fallback:** RNNoise (CPU only, ~10ms, <1MB model, BSD license)
- Ideal for low-end CPUs with no GPU
- Less effective on music, excellent for voice

**GUI controls:**
- On/Off toggle
- Engine selector: Auto / DeepFilterNet3 / RNNoise (CPU)
- Intensity: Light / Balanced / Aggressive (maps to DeepFilterNet3 `post_filter` threshold)
- Real-time noise level meter (from VAD probability output of RNNoise/DFN3)

---

#### Module 2 — Psychoacoustic Pre-Emphasis (Phase 2, pre-encoding)

**What it does:** Applies a codec-aware parametric EQ curve to the audio *before* encoding. Each codec compresses certain frequency bands more than others — pre-emphasis boosts those bands so the post-decoding result sounds more balanced.

**Implementation:** Not a neural model — this is a fast DSP module. Codec profiles:
- **LDAC @ 330 kbps:** Boost 8–14 kHz by +2–3 dB; attenuate bass >200 Hz by −1 dB (LDAC struggles with transients at low bitrate)
- **SBC (low bitpool):** Boost 4–8 kHz by +2 dB; slight de-emphasis of sub-bass
- **AAC @ 128 kbps:** Gentle treble boost +1 dB at 10 kHz
- **User profiles:** Fully custom EQ curves saved per-device, per-codec

**Why this matters:** LDAC at 330 kbps sounds "muddy" partly because of HF attenuation. Pre-emphasis recovers 30–40% of the perceptual quality loss for free.

**GUI controls:**
- On/Off toggle
- Profile selector: Auto (matches active codec) / Custom
- EQ visualizer (graphic EQ-style, editable per band)

---

#### Module 3 — Adaptive Bitrate Intelligence (Phase 2)

**What it does:** Replaces the simple threshold-based adaptive bitrate with an ML model that predicts the optimal bitrate 500ms ahead, based on:
- L2CAP packet retransmission rate (from kernel IOCTL — exact signal quality measure)
- Historical RSSI trend (direction: improving or degrading)
- Audio content type: music / speech / silence (classified by a tiny CNN, ~50KB ONNX model, CPU-only)
- Time-of-day pattern (learns your environment over time)

**Why better than threshold-based:** Simple threshold-based cuts quality abruptly when signal drops. The AI predicts degradation early and lowers bitrate smoothly before dropouts occur, then recovers gradually. Result: fewer audible artifacts.

**Implementation:** Small recurrent model (~500KB ONNX), runs entirely on CPU, ~0.5ms inference per decision. Updates bitrate decision every 200ms.

**GUI controls:**
- On/Off toggle ("Smart Adaptive Bitrate")
- Aggressiveness: Conservative / Balanced / Aggressive (how early it starts reducing quality)
- History graph: bitrate vs signal quality over last 60 seconds

---

#### Module 4 — Bandwidth Extension / Audio Upsampling (Phase 3, pre-encoding)

**What it does:** Reconstructs high-frequency audio content (8–22 kHz) from a limited-bandwidth source (e.g., 44.1 kHz/16-bit PCM from a compressed streaming service) before encoding to LDAC at 96 kHz/990 kbps. Maximizes the quality potential of high-bitrate codecs when the source is compressed.

**Model:** CNN-based bandwidth extension (similar approach to AudioSR / NU-Wave 2)
- ONNX export, GPU-accelerated via DirectML
- Frame size: 40ms, GPU latency: ~5ms, CPU latency: ~40ms (acceptable within BT codec buffering)
- NOTE: Only active if source audio is <48 kHz and target codec supports >48 kHz (LDAC HQ, aptX HD)

**GUI controls:**
- On/Off toggle ("High-Res Upsampling")
- Target sample rate: 48 kHz / 88.2 kHz / 96 kHz
- Source quality hint: "Streaming (lossy)" / "Lossless source" / "Auto-detect"

---

#### Module 5 — Codec Artifact Removal (Phase 3, post-processing for voice)

**What it does:** When HFP is active (voice call mode, SBC 16 kHz mono), applies a PostGAN-style generative model to enhance the decoded audio received *from* the headset microphone before sending it to the calling application. Cleans up the "phone quality" effect.

**Model:** GAN-based speech post-processor (PostGAN architecture)
- ONNX, ~2MB model
- Adds ~5ms latency
- Only active on HFP input path (not the A2DP music path)

**GUI controls:**
- On/Off toggle ("Voice Enhancement (HFP)")
- Available only when HFP is active

### 14.3 AI Module Pipeline Position

```
[System Audio / Microphone]
        │
        ▼
[Module 2: Psychoacoustic Pre-Emphasis] ← DSP, always-on option, ~0.1ms
        │
        ▼
[Module 4: Bandwidth Extension]  ← ONNX+DirectML, optional, ~5-40ms
        │
        ▼
[Module 1: Noise Reduction]      ← ONNX+DirectML/CPU, optional, ~5-20ms
        │
        ▼
[Codec Encoder: LDAC/aptX/SBC...] ← C codec library
        │
        ▼
[Module 3: Adaptive Bitrate AI]  ← ONNX CPU, monitors all stages, ~0.5ms
        │
        ▼
[L2CAP Bluetooth Transmission]
```

Total added AI latency budget: 25–65ms (well within BT codec's own buffering).

### 14.4 AI Enhancement GUI Panel (Complete Design)

```
┌─────────────────────────────────────────────────────────────────┐
│  🤖  AI Audio Enhancement                        [Master ON/OFF] │
│─────────────────────────────────────────────────────────────────│
│  Inference Device:  [Auto ▼]  Current: AMD RX 7600 (DirectML)   │
│  GPU Load: ▓▓░░░░░░  3%   |  AI Latency added: +12ms            │
│─────────────────────────────────────────────────────────────────│
│                                                                   │
│  ┌──────────────────────────────────────────┐                   │
│  │ 🎙 Noise Reduction                  [ON] │                   │
│  │ Engine: [DeepFilterNet3 ▼]               │                   │
│  │ Intensity: ○ Light  ● Balanced  ○ Max    │                   │
│  │ Noise level: ████░░░░  42%               │                   │
│  └──────────────────────────────────────────┘                   │
│                                                                   │
│  ┌──────────────────────────────────────────┐                   │
│  │ 🎛 Psychoacoustic Pre-Emphasis      [ON] │                   │
│  │ Profile: [Auto — LDAC 330kbps ▼]         │                   │
│  │ [Edit EQ Curve...]                        │                   │
│  └──────────────────────────────────────────┘                   │
│                                                                   │
│  ┌──────────────────────────────────────────┐                   │
│  │ 📶 Smart Adaptive Bitrate          [ON] │                   │
│  │ Aggressiveness: ○ Conservative ● Balanced│                   │
│  │ [View bitrate history chart]             │                   │
│  └──────────────────────────────────────────┘                   │
│                                                                   │
│  ┌──────────────────────────────────────────┐                   │
│  │ ✨ Hi-Res Upsampling              [OFF]  │                   │
│  │ (Phase 3 — not yet available)            │                   │
│  └──────────────────────────────────────────┘                   │
│                                                                   │
│  ┌──────────────────────────────────────────┐                   │
│  │ 🎤 Voice Enhancement (HFP)        [OFF] │                   │
│  │ (Inactive — device in A2DP mode)         │                   │
│  └──────────────────────────────────────────┘                   │
└─────────────────────────────────────────────────────────────────┘
```

---

## 15. Driver Signing — GUI Flow and User Experience

This section addresses the practical challenge of distributing a kernel-mode driver as an open-source project, without requiring users to understand Windows driver signing concepts.

### 15.1 Three Installation Paths

OpenWinBlue supports three driver installation modes, selected automatically based on the user's system:

**Path A — Signed Release Binary (recommended, most users)**
The GitHub Actions CI pipeline builds and submits the driver to Microsoft's Hardware Dev Center for attestation signing on every tagged release. The resulting signed `.cat` and `.sys` files are bundled in the installer. Users install with one click, no special configuration needed.

**Path B — Windows Test Signing Mode (developers / contributors)**
For building from source. User enables Test Signing Mode, the application uses a self-signed test certificate for the driver. Requires a one-time reboot.

**Path C — Unsigned (Test Signing Mode OFF, first-launch detection)**
If the user installs an unsigned build without enabling Test Signing Mode, the driver installation will fail. The GUI detects this and guides the user.

### 15.2 First-Run Driver Wizard (GUI)

On first launch, before any audio device configuration, the GUI shows a **Driver Setup Wizard**:

```
┌──────────────────────────────────────────────────────────────┐
│                  OpenWinBlue — Driver Setup                   │
│──────────────────────────────────────────────────────────────│
│                                                               │
│  OpenWinBlue needs to install its audio driver to enable      │
│  advanced codec support (LDAC, aptX HD, etc.)                 │
│                                                               │
│  ┌─────────────────────────────────────────────────────┐     │
│  │  ✅  You are running the official signed release.    │     │
│  │      The driver is Microsoft-attested and safe.      │     │
│  │                                                      │     │
│  │  [Install Driver — 1 click]                          │     │
│  └─────────────────────────────────────────────────────┘     │
│                                                               │
│  ──── OR (if unsigned build detected) ────────────────────── │
│                                                               │
│  ┌─────────────────────────────────────────────────────┐     │
│  │  ⚠️  This build is not signed by Microsoft.          │     │
│  │                                                      │     │
│  │  Option 1: Download the signed official release      │     │
│  │  [→ Download v1.x.x from GitHub]                     │     │
│  │                                                      │     │
│  │  Option 2: Enable Windows Test Mode (developers)     │     │
│  │  This adds a watermark to your desktop.              │     │
│  │  [Enable Test Mode + Reboot]  (runs elevated cmd)    │     │
│  └─────────────────────────────────────────────────────┘     │
│                                                               │
│  ──── OR ──────────────────────────────────────────────────── │
│                                                               │
│  ┌─────────────────────────────────────────────────────┐     │
│  │  ⏩  Skip driver installation                        │     │
│  │      Use registry-only mode (SBC/aptX only,          │     │
│  │      no LDAC, basic HFP prevention)                  │     │
│  └─────────────────────────────────────────────────────┘     │
└──────────────────────────────────────────────────────────────┘
```

### 15.3 Driver Status Card (always visible in Driver Panel)

```
┌──────────────────────────────────────────────────────────────┐
│  🔧  Driver Management                                        │
│──────────────────────────────────────────────────────────────│
│                                                               │
│  Status:  ✅ Active — owb_a2dp.sys v1.2.0                    │
│  Signing: 🔒 Microsoft Attestation Signed (WHDC)              │
│  Mode:    Replaces btavchdt.sys (full codec control)          │
│                                                               │
│  [Update Driver]      [Reinstall]      [Rollback to Windows]  │
│                                                               │
│──────────────────────────────────────────────────────────────│
│  ⚠️  Warning states (shown contextually):                     │
│                                                               │
│  🔴 Driver not installed — [Install Now]                      │
│  🟡 Unsigned build — [Download signed release]                │
│  🟡 Test Mode active — watermark visible on desktop           │
│  🔴 Driver install failed — [View error log] [Try Test Mode]  │
│  🟢 Using registry-only mode — limited codec selection        │
└──────────────────────────────────────────────────────────────┘
```

### 15.4 Rollback Guarantee

The "Rollback to Windows Default" button is always visible and always functional, even if the service crashes. The rollback:
1. Calls `pnputil /delete-driver owb_a2dp.inf /uninstall` (elevated)
2. Re-enables `btavchdt.sys` via devcon
3. Stops and unregisters the OpenWinBlue service
4. Prompts for reboot if needed

A standalone `owb-rollback.bat` script is included in the installation directory for emergency recovery without the GUI.

### 15.5 Safe Mode Recovery

Instructions included in the installer README: if the system fails to boot normally with the driver, boot into Safe Mode (F8 at startup) → run `owb-rollback.bat` from `C:\Program Files\OpenWinBlue\` as Administrator.

---

*Spec updated: 2026-06-02. Added sections 14 (AI Enhancement System) and 15 (Driver Signing — GUI Flow). Next step: user review, then invoke writing-plans skill for implementation plan.*
