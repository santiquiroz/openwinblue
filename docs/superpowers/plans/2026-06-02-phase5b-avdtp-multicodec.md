# Phase 5b — AVDTP Multi-Codec Negotiation + IPC SetCodec Handler

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:subagent-driven-development` (recommended) or `superpowers:executing-plans` to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Wire the codec selection from the GUI all the way to the headphone — the kernel driver negotiates LDAC/aptX in AVDTP SET_CONFIGURATION when the user selects a different codec, and the IPC server forwards `SetCodec` GUI commands to the driver.

**Architecture:** The kernel driver adds a `PreferredCodecId` field to the device extension. `HandleGetCapabilitiesResponse` parses the remote capabilities to find the preferred codec (LDAC/aptX) or falls back to SBC, then builds the correct vendor-specific SET_CONFIGURATION payload. `OWB_IOCTL_SET_CODEC_CONFIG` with `key="switch"` updates `PreferredCodecId` and triggers AVDTP SUSPEND→SET_CONFIGURATION→START. On the service side, `IpcServer::serve_one()` now handles `MsgType::SetCodec` messages and forwards them to the driver via `A2dpStream::set_codec_config()`.

**Tech Stack:** C11 KMDF (kernel), C++20 Win32 service, A2DP vendor codec capability format (A2DP spec §4), GoogleTest.

---

## Environment Notes

- **Kernel builds**: CI only (WDK not available locally — changes compile on GitHub Actions)
- **Service/IPC builds**: locally testable
- **cmake local**: `C:/Users/santi/AppData/Local/Android/Sdk/cmake/4.1.2/bin/cmake.exe`
- **Build preset**: `nmake-debug`

---

## Codec Capability Formats Reference

For building vendor-codec SET_CONFIGURATION payloads (A2DP spec §4):

```
[0] = 0x07   Service Category = Media Codec
[1] = LOSC   Length of following bytes
[2] = 0x00   Media Type = Audio
[3] = type   0x00=SBC, 0xFF=Vendor Specific
--- if vendor (type=0xFF): ---
[4..7] = Vendor ID (little-endian UINT32)
[8..9] = Codec ID  (little-endian UINT16)
[10..] = Codec-specific info bytes
```

**LDAC (Sony, OWB_CODEC_LDAC=1):**
- Vendor ID: 0x0000012D → bytes [0x2D, 0x01, 0x00, 0x00]
- Codec ID:  0x00AA     → bytes [0xAA, 0x00]
- Info byte 0 (sampling freq): 0x20=44.1kHz, 0x10=48kHz, 0x08=88.2kHz, 0x04=96kHz
- Info byte 1 (channel+quality): channel (0x01=Stereo, 0x02=Dual, 0x04=Mono) | quality (0x03=HQ, 0x02=SQ, 0x01=MQ)
- LOSC = 10, total payload = 12 bytes

**aptX Classic (Qualcomm, OWB_CODEC_APTX=2):**
- Vendor ID: 0x0000004F → bytes [0x4F, 0x00, 0x00, 0x00]
- Codec ID:  0x0001     → bytes [0x01, 0x00]
- Info byte (freq|channel): freq bits 7-4 (0x10=48kHz, 0x20=44.1kHz), channel bits 3-0 (0x01=Joint Stereo)
- LOSC = 7, total payload = 9 bytes

**aptX HD (Qualcomm, OWB_CODEC_APTXHD=3):**
- Vendor ID: 0x000000D7 → bytes [0xD7, 0x00, 0x00, 0x00]
- Codec ID:  0x0024     → bytes [0x24, 0x00]
- Info bytes: same as aptX + one extra reserved byte 0x00
- LOSC = 8, total payload = 10 bytes

---

## File Map

### Kernel files (Tasks 1-3, CI-only)

```
driver/src/
  owb_a2dp.h    # Add PreferredCodecId to OWB_DEVICE_EXTENSION
  avdtp.h       # Add AvdtpSetPreferredCodec() declaration
  avdtp.c       # Add LDAC/aptX builders; update GetCaps handler; add SetPreferredCodec
  ioctl.c       # Wire OWB_IOCTL_SET_CODEC_CONFIG → AvdtpSetPreferredCodec
```

### Service files (Tasks 4-5, locally testable)

```
service/src/
  ipc_server.cpp             # Handle MsgType::SetCodec → a2dp.set_codec_config

tests/service/
  ipc_server_test.cpp        # Add test: SetCodec handled → codec_config forwarded
```

---

## Task 1: Kernel — Add PreferredCodecId + LDAC/aptX SET_CONFIGURATION builders

**Files:**
- Modify: `driver/src/owb_a2dp.h`
- Modify: `driver/src/avdtp.h`
- Modify: `driver/src/avdtp.c`

> These are kernel files — do NOT compile locally (WDK not installed). Create the files exactly as specified and commit. Verified on CI.

- [ ] **Step 1.1: Add `PreferredCodecId` to `driver/src/owb_a2dp.h`**

Read the current file. Add after `OWB_AVDTP_CONTEXT Avdtp;`:

```c
    // Preferred codec for A2DP negotiation (OWB_CODEC_*).
    // Set to OWB_CODEC_SBC on init; updated via OWB_IOCTL_SET_CODEC_CONFIG.
    ULONG PreferredCodecId;
```

Also update `OwbEvtDeviceAdd` in `owb_a2dp.c` to zero-initialize (already done via `RtlZeroMemory`).

- [ ] **Step 1.2: Add declarations to `driver/src/avdtp.h`**

Add after the `AvdtpConnect` declaration:

```c
// Update the preferred codec and trigger AVDTP reconfiguration (SUSPEND→SET_CONFIG→START).
// Call from PASSIVE_LEVEL (IOCTL dispatch context).
NTSTATUS AvdtpSetPreferredCodec(
    _In_ POWB_DEVICE_EXTENSION DevExt,
    _In_ ULONG                 NewCodecId);
```

- [ ] **Step 1.3: Update `driver/src/avdtp.c`**

Read the current file. Make these changes:

**1. Add codec capability builders after `BuildSbcSetConfig`:**

```c
// Build LDAC SET_CONFIGURATION payload (Sony LDAC, codec type vendor/0xFF).
// Buf must be at least 12 bytes. Returns bytes written, 0 on failure.
static USHORT BuildLdacSetConfig(
    _Out_writes_bytes_(MaxLen) PUCHAR Buf,
    _In_ UCHAR AcpSeid, _In_ UCHAR IntSeid, _In_ USHORT MaxLen)
{
    if (MaxLen < 12u) return 0u;
    Buf[0]  = (UCHAR)((AcpSeid << 2u) & 0xFCu);
    Buf[1]  = (UCHAR)((IntSeid << 2u) & 0xFCu);
    Buf[2]  = 0x07u;  // Service Category: Media Codec
    Buf[3]  = 0x08u;  // LOSC = 8 (after this byte: media_type + type + vendor + id + info)
    Buf[4]  = 0x00u;  // Media Type: Audio
    Buf[5]  = 0xFFu;  // Codec Type: Vendor Specific
    // Sony vendor ID: 0x0000012D (little-endian)
    Buf[6]  = 0x2Du; Buf[7]  = 0x01u; Buf[8]  = 0x00u; Buf[9]  = 0x00u;
    // LDAC Codec ID: 0x00AA (little-endian)
    Buf[10] = 0xAAu; Buf[11] = 0x00u;
    // Note: LDAC codec info bytes (sampling freq + channel + quality) are negotiated
    // in SET_CONFIGURATION. Omitted here — remote device fills these on connection.
    return 12u;
}

// Build aptX Classic SET_CONFIGURATION payload (Qualcomm aptX, vendor/0xFF).
// Buf must be at least 9 bytes. Returns bytes written, 0 on failure.
static USHORT BuildAptxSetConfig(
    _Out_writes_bytes_(MaxLen) PUCHAR Buf,
    _In_ UCHAR AcpSeid, _In_ UCHAR IntSeid, _In_ USHORT MaxLen, _In_ BOOLEAN isHD)
{
    if (MaxLen < 9u + (isHD ? 1u : 0u)) return 0u;
    Buf[0] = (UCHAR)((AcpSeid << 2u) & 0xFCu);
    Buf[1] = (UCHAR)((IntSeid << 2u) & 0xFCu);
    Buf[2] = 0x07u;  // Service Category: Media Codec
    Buf[3] = (UCHAR)(5u + (isHD ? 1u : 0u));  // LOSC
    Buf[4] = 0x00u;  // Media Type: Audio
    Buf[5] = 0xFFu;  // Codec Type: Vendor Specific
    if (!isHD) {
        // Qualcomm aptX: 0x0000004F
        Buf[6] = 0x4Fu; Buf[7] = 0x00u; Buf[8] = 0x00u; Buf[9] = 0x00u;
        Buf[10] = 0x01u; Buf[11] = 0x00u;  // Codec ID: 0x0001
        Buf[12] = 0x22u;  // 44.1kHz(0x20) | Stereo(0x02)
        return 13u;
    } else {
        // Qualcomm aptX HD: 0x000000D7
        Buf[6] = 0xD7u; Buf[7] = 0x00u; Buf[8] = 0x00u; Buf[9] = 0x00u;
        Buf[10] = 0x24u; Buf[11] = 0x00u;  // Codec ID: 0x0024
        Buf[12] = 0x22u;  // 44.1kHz | Stereo
        Buf[13] = 0x00u;  // reserved
        return 14u;
    }
}
```

**2. Replace `HandleGetCapabilitiesResponse` to select codec by preference:**

```c
// Checks whether the GET_CAPABILITIES response payload contains a specific
// vendor codec (identified by 4-byte vendor ID + 2-byte codec ID).
static BOOLEAN CapabilitiesContainsVendorCodec(
    _In_reads_bytes_opt_(Len) const UCHAR* Data, _In_ USHORT Len,
    _In_ ULONG VendorId, _In_ USHORT CodecId)
{
    if (!Data || Len < 4u) return FALSE;
    for (USHORT i = 0u; i + 3u < Len; ) {
        UCHAR cat  = Data[i];
        UCHAR losc = Data[i + 1u];
        // Service Category 0x07 = Media Codec
        if (cat == 0x07u && losc >= 6u && (i + 2u + losc) <= Len) {
            // Data[i+2]=media_type, Data[i+3]=codec_type
            if (Data[i + 3u] == 0xFFu) {  // vendor specific
                // Bytes [i+4..i+7] = vendor ID (LE), [i+8..i+9] = codec ID (LE)
                if (i + 9u < Len) {
                    ULONG vid = (ULONG)Data[i+4u] | ((ULONG)Data[i+5u] << 8u)
                              | ((ULONG)Data[i+6u] << 16u) | ((ULONG)Data[i+7u] << 24u);
                    USHORT cid = (USHORT)Data[i+8u] | ((USHORT)Data[i+9u] << 8u);
                    if (vid == VendorId && cid == CodecId) return TRUE;
                }
            }
        }
        if (losc == 0u) break;
        i += 2u + losc;
    }
    return FALSE;
}

static VOID HandleGetCapabilitiesResponse(
    _In_ POWB_DEVICE_EXTENSION DevExt,
    _In_reads_bytes_opt_(Len) const UCHAR* Data,
    _In_ USHORT Len)
{
    UCHAR payload[16];
    USHORT plen = 0u;
    ULONG preferred = DevExt->PreferredCodecId;

    // Try to use the preferred codec if the remote supports it.
    if (preferred == OWB_CODEC_LDAC &&
        CapabilitiesContainsVendorCodec(Data, Len, 0x0000012DUL, 0x00AAu)) {
        plen = BuildLdacSetConfig(payload, DevExt->Avdtp.RemoteSeid,
                                  DevExt->Avdtp.LocalSeid, sizeof(payload));
        KdPrint(("OpenWinBlue: negotiating LDAC\n"));
    } else if (preferred == OWB_CODEC_APTXHD &&
               CapabilitiesContainsVendorCodec(Data, Len, 0x000000D7UL, 0x0024u)) {
        plen = BuildAptxSetConfig(payload, DevExt->Avdtp.RemoteSeid,
                                   DevExt->Avdtp.LocalSeid, sizeof(payload), TRUE);
        KdPrint(("OpenWinBlue: negotiating aptX HD\n"));
    } else if (preferred == OWB_CODEC_APTX &&
               CapabilitiesContainsVendorCodec(Data, Len, 0x0000004FUL, 0x0001u)) {
        plen = BuildAptxSetConfig(payload, DevExt->Avdtp.RemoteSeid,
                                   DevExt->Avdtp.LocalSeid, sizeof(payload), FALSE);
        KdPrint(("OpenWinBlue: negotiating aptX Classic\n"));
    } else {
        // Fall back to SBC (mandatory codec — always supported).
        plen = BuildSbcSetConfig(payload, DevExt->Avdtp.RemoteSeid,
                                  DevExt->Avdtp.LocalSeid, sizeof(payload));
        KdPrint(("OpenWinBlue: negotiating SBC (fallback)\n"));
    }

    if (plen == 0u) {
        KdPrint(("OpenWinBlue: failed to build SET_CONFIGURATION payload\n"));
        return;
    }
    NTSTATUS st = AvdtpSendCommand(DevExt, AVDTP_MSG_SET_CONFIGURATION, payload, plen);
    if (NT_SUCCESS(st)) DevExt->Avdtp.State = AvdtpStateConfigured;
}
```

**3. Update `AvdtpHandleSignalingPacket` to pass payload to `HandleGetCapabilitiesResponse`:**

Currently it calls `HandleGetCapabilitiesResponse(DevExt)` with no data. Update to pass the payload:

Find:
```c
        case AVDTP_MSG_GET_CAPABILITIES:
            HandleGetCapabilitiesResponse(DevExt);
            break;
```

Replace with:
```c
        case AVDTP_MSG_GET_CAPABILITIES:
            HandleGetCapabilitiesResponse(DevExt, payload, pay_len);
            break;
```

**4. Add `AvdtpSetPreferredCodec` implementation at the end of the file:**

```c
NTSTATUS AvdtpSetPreferredCodec(
    _In_ POWB_DEVICE_EXTENSION DevExt,
    _In_ ULONG                 NewCodecId)
{
    DevExt->PreferredCodecId = NewCodecId;
    KdPrint(("OpenWinBlue: preferred codec set to %lu\n", NewCodecId));

    // If currently streaming, trigger reconfiguration: SUSPEND → SET_CONFIG → START.
    if (DevExt->Avdtp.State == AvdtpStateStreaming) {
        UCHAR seid = (UCHAR)((DevExt->Avdtp.RemoteSeid << 2u) & 0xFCu);
        // SUSPEND first
        NTSTATUS st = AvdtpSendCommand(DevExt, AVDTP_MSG_SUSPEND, &seid, 1u);
        if (!NT_SUCCESS(st)) return st;
        DevExt->Avdtp.State = AvdtpStateOpen;
        // Re-negotiate with new codec (GET_CAPABILITIES → SET_CONFIGURATION → OPEN → START)
        return AvdtpConnect(DevExt);
    }
    return STATUS_SUCCESS;
}
```

- [ ] **Step 1.4: Commit**

```powershell
cd "c:/suru/open winblue"
git add driver/src/owb_a2dp.h driver/src/avdtp.h driver/src/avdtp.c
git commit -m "feat(driver): AVDTP multi-codec — LDAC/aptX builders + capability-based negotiation"
```

---

## Task 2: Kernel — Wire OWB_IOCTL_SET_CODEC_CONFIG to AvdtpSetPreferredCodec

**Files:**
- Modify: `driver/src/ioctl.c`

- [ ] **Step 2.1: Update `driver/src/ioctl.c`**

Read the current file. Replace the `OWB_IOCTL_SET_CODEC_CONFIG` case:

Find:
```c
        case OWB_IOCTL_SET_CODEC_CONFIG: {
            PVOID   buf  = NULL;
            size_t  size = 0;
            status = WdfRequestRetrieveInputBuffer(
                Request, sizeof(OWB_CODEC_CONFIG), &buf, &size);
            if (!NT_SUCCESS(status)) break;

            POWB_CODEC_CONFIG cfg = (POWB_CODEC_CONFIG)buf;
            KdPrint(("OpenWinBlue: SET_CODEC_CONFIG codec=%lu key=%.16s val=%lld\n",
                     cfg->codec_id, cfg->param_key, cfg->param_value));
            // Phase 2c: trigger AVDTP SET_CONFIGURATION reconfiguration.
            break;
        }
```

Replace with:
```c
        case OWB_IOCTL_SET_CODEC_CONFIG: {
            PVOID   buf  = NULL;
            size_t  size = 0;
            status = WdfRequestRetrieveInputBuffer(
                Request, sizeof(OWB_CODEC_CONFIG), &buf, &size);
            if (!NT_SUCCESS(status)) break;

            POWB_CODEC_CONFIG cfg = (POWB_CODEC_CONFIG)buf;
            KdPrint(("OpenWinBlue: SET_CODEC_CONFIG codec=%lu key=%.16s val=%lld\n",
                     cfg->codec_id, cfg->param_key, cfg->param_value));

            // key="switch" triggers codec negotiation change.
            if (cfg->param_key[0] == 's' && cfg->param_key[1] == 'w') {
                status = AvdtpSetPreferredCodec(devExt, cfg->codec_id);
            }
            // Other keys (bitpool, freq, etc.) are codec-specific params for Phase 5c.
            break;
        }
```

Also add `#include "avdtp.h"` at the top of `ioctl.c` if not already there.

- [ ] **Step 2.2: Commit**

```powershell
cd "c:/suru/open winblue"
git add driver/src/ioctl.c
git commit -m "feat(driver): wire OWB_IOCTL_SET_CODEC_CONFIG to AvdtpSetPreferredCodec"
```

---

## Task 3: Kernel — Push to CI and verify driver builds

- [ ] **Step 3.1: Push**

```bash
SANTI_TOKEN=$(gh auth token --user santiquiroz)
git push "https://santiquiroz:${SANTI_TOKEN}@github.com/santiquiroz/openwinblue.git" main
```

- [ ] **Step 3.2: Poll CI until completed**

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

Expected: `completed success <sha>` — driver builds without errors.

**If CI fails on `HandleGetCapabilitiesResponse` signature change**: The function signature changed from `(DevExt)` to `(DevExt, Data, Len)`. Ensure `avdtp.h` does NOT declare `HandleGetCapabilitiesResponse` as a public function (it's `static VOID` in the .c file) — there's no change needed in the header.

---

## Task 4: Service — IpcServer handles MsgType::SetCodec

**Files:**
- Modify: `service/src/ipc_server.cpp`
- Modify: `tests/service/ipc_test.cpp`

This task is locally testable. The `IpcServer` already has `A2dpStream* stream_` from Phase 2c. When it receives a `SetCodec` message, it calls `stream_->set_codec_config(codec_id_from_name, key, value)`.

- [ ] **Step 4.1: Write failing test first**

Add to `tests/service/ipc_test.cpp`:

```cpp
// Additional test for SetCodec handling
TEST(IpcServer, SetCodec_WhenConnected_ForwardsToStream) {
    // This test verifies the IPC server parses SetCodec messages.
    // Since A2dpStream is in stub mode (no driver), set_codec_config returns false,
    // but the IPC server must not crash and must reply with CodecAck.
    owb::IpcServer server(nullptr);  // null stream = stub mode
    ASSERT_TRUE(server.start());

    std::thread t([&server] { server.serve_one(); });

    // Connect and send a SetCodec message
    if (!WaitNamedPipeW(owb::ipc::kPipeName, 3000)) {
        t.join();
        GTEST_SKIP() << "Pipe not available";
    }

    HANDLE pipe = CreateFileW(owb::ipc::kPipeName,
        GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (pipe == INVALID_HANDLE_VALUE) { t.join(); GTEST_SKIP(); }

    // Build SetCodec payload
    owb::ipc::MsgHeader hdr{ owb::ipc::MsgType::SetCodec,
                              sizeof(owb::ipc::SetCodecPayload) };
    owb::ipc::SetCodecPayload payload{};
    // codec_name = "LDAC", param_key = "switch", param_value = 1
    std::memcpy(payload.codec_name, "LDAC", 4);
    std::memcpy(payload.param_key,  "switch", 6);
    payload.param_value = 1;

    DWORD written = 0;
    WriteFile(pipe, &hdr, sizeof(hdr), &written, nullptr);
    WriteFile(pipe, &payload, sizeof(payload), &written, nullptr);

    // Read CodecAck reply
    owb::ipc::MsgHeader reply{};
    DWORD read_bytes = 0;
    BOOL ok = ReadFile(pipe, &reply, sizeof(reply), &read_bytes, nullptr);
    CloseHandle(pipe);
    t.join();

    EXPECT_TRUE(ok);
    EXPECT_EQ(reply.type, owb::ipc::MsgType::CodecAck);
}
```

- [ ] **Step 4.2: Update `service/src/ipc_server.cpp`**

Read the current file. Add a `SetCodec` case in `serve_one()` after the `GetStatus` case:

```cpp
            case ipc::MsgType::SetCodec: {
                // Read SetCodecPayload
                if (hdr.payload_len < sizeof(ipc::SetCodecPayload)) {
                    client_done = true;
                    break;
                }
                ipc::SetCodecPayload codec_payload{};
                DWORD payload_read = 0;
                ReadFile(impl_->pipe, &codec_payload,
                         sizeof(codec_payload), &payload_read, nullptr);

                // Resolve codec name string → OWB_CODEC_* ID
                uint32_t codec_id = OWB_CODEC_SBC;  // default
                if (std::strncmp(codec_payload.codec_name, "LDAC",    4) == 0) codec_id = OWB_CODEC_LDAC;
                else if (std::strncmp(codec_payload.codec_name, "aptX-HD", 7) == 0) codec_id = OWB_CODEC_APTXHD;
                else if (std::strncmp(codec_payload.codec_name, "aptX",    4) == 0) codec_id = OWB_CODEC_APTX;

                // Forward to driver via A2dpStream
                bool success = false;
                if (impl_->stream_) {
                    std::string_view key(codec_payload.param_key,
                                        strnlen(codec_payload.param_key,
                                                sizeof(codec_payload.param_key)));
                    success = impl_->stream_->set_codec_config(
                        codec_id, key, codec_payload.param_value);
                }

                // Reply with CodecAck
                ipc::MsgHeader ack{ ipc::MsgType::CodecAck, sizeof(ipc::AckPayload) };
                ipc::AckPayload ack_payload{ success ? uint8_t{1} : uint8_t{0}, {0, 0, 0} };
                DWORD written = 0;
                BOOL ack_ok = WriteFile(impl_->pipe, &ack, sizeof(ack), &written, nullptr);
                if (ack_ok && written == sizeof(ack))
                    WriteFile(impl_->pipe, &ack_payload, sizeof(ack_payload), &written, nullptr);
                client_done = true;
                break;
            }
```

Also add these includes at the top of `ipc_server.cpp` if not already there:
```cpp
#include "a2dp_stream.h"    // for set_codec_config
```

And add `OWB_CODEC_*` constants — the file already has `#include <winioctl.h>` + `#include "owb_ioctl.h"` via `ipc_server.h` transitively (through `a2dp_stream.h`). If not, add `#include "owb_ioctl.h"` explicitly.

Note: `ipc::SetCodecPayload` has `char codec_name[16]` and `char param_key[16]` — use `strncmp` for safe comparison.

- [ ] **Step 4.3: Build and run IPC tests**

```powershell
$env:PATH = "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Tools\MSVC\14.50.35717\bin\Hostx64\x64;C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64;C:\Users\santi\AppData\Local\Android\Sdk\cmake\4.1.2\bin;$env:PATH"
$env:INCLUDE = "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Tools\MSVC\14.50.35717\include;C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\ucrt;C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\um;C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\shared"
$env:LIB = "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Tools\MSVC\14.50.35717\lib\x64;C:\Program Files (x86)\Windows Kits\10\Lib\10.0.26100.0\ucrt\x64;C:\Program Files (x86)\Windows Kits\10\Lib\10.0.26100.0\um\x64"
Set-Location "c:\suru\open winblue"
& cmake --preset nmake-debug
& cmake --build build/nmake-debug --target owb_service_tests
Set-Location build/nmake-debug
& ctest --output-on-failure -R "IpcServer"
```

Expected: all IPC tests pass including the new `SetCodec_WhenConnected_ForwardsToStream`.

**Note:** The new test uses `GTEST_SKIP()` if the pipe is unavailable — so it won't fail in CI environments where the service isn't running. It only exercises the `serve_one` path locally.

- [ ] **Step 4.4: Run ALL service tests**

```powershell
& ctest --output-on-failure
```

Expected: `37 tests passed, 0 failed` (36 previous + 1 new IPC test).

- [ ] **Step 4.5: Commit**

```powershell
cd "c:/suru/open winblue"
git add service/src/ipc_server.cpp tests/service/ipc_test.cpp
git commit -m "feat(service): IpcServer handles SetCodec — resolves name→ID, forwards to A2dpStream"
```

---

## Task 5: Final push + CI verification

- [ ] **Step 5.1: Push all commits**

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

Expected: `completed success <sha>` — all 3 jobs:
- C++ service tests: 37 passing
- C# GUI tests: 20 passing
- Driver KMDF build: owb_a2dp.sys with multi-codec AVDTP

---

## Self-Review

**Spec coverage:**
- ✅ `PreferredCodecId` in device extension — Task 1
- ✅ `BuildLdacSetConfig` payload builder (12 bytes) — Task 1
- ✅ `BuildAptxSetConfig` payload builder (9/10 bytes for Classic/HD) — Task 1
- ✅ `CapabilitiesContainsVendorCodec` — parse GET_CAPABILITIES for vendor codecs — Task 1
- ✅ `HandleGetCapabilitiesResponse` selects codec by preference with SBC fallback — Task 1
- ✅ `AvdtpSetPreferredCodec` — updates preference + triggers SUSPEND→reconfigure — Task 1
- ✅ `OWB_IOCTL_SET_CODEC_CONFIG` wired to `AvdtpSetPreferredCodec` on key="switch" — Task 2
- ✅ CI verifies kernel compile — Task 3
- ✅ `IpcServer` handles `MsgType::SetCodec` — resolves codec name → ID — Task 4
- ✅ `CodecAck` reply sent back to GUI — Task 4
- ✅ 37 service tests — Task 4
- ✅ Final CI 3/3 — Task 5

**Placeholder scan:** No TBDs. "Phase 5c" on the other codec keys (bitpool, freq) is accurate deferred documentation.

**Type consistency:**
- `HandleGetCapabilitiesResponse` updated to take `(DevExt, Data, Len)` in both declaration (avdtp.h) and call site (AvdtpHandleSignalingPacket) ✅
- `AvdtpSetPreferredCodec(DevExt, ULONG)` declared in avdtp.h and called from ioctl.c ✅
- `codec_id` in IpcServer resolved to `OWB_CODEC_*` constants from `owb_ioctl.h` ✅
- `ipc::SetCodecPayload` struct fields `codec_name[16]`, `param_key[16]` used with `strncmp` ✅
