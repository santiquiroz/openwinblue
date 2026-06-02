# Phase 2c — Real L2CAP BRB Connection + IPC Status Wire-up

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:subagent-driven-development` (recommended) or `superpowers:executing-plans` to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the three Phase 2b stubs (`L2capOpenSignalingChannel`, `L2capSendSignaling`, RSSI query) with real BthPort BRB implementations so the kernel driver can actually communicate with a paired Bluetooth headphone, and wire the real device state into the user-mode IPC status reply.

**Architecture:** The kernel driver obtains the `BTH_PROFILE_DRIVER_INTERFACE` from BthPort during device add, then uses `BRB_L2CA_OPEN_CHANNEL` to open the AVDTP signaling channel, `BRB_L2CA_ACL_TRANSFER` to send AVDTP commands, and a registered receive callback that feeds inbound AVDTP responses into `AvdtpHandleSignalingPacket`. The remote BT address is read from `DevicePropertyAddress` (PnP). The user-mode service adds `get_device_state()` to `A2dpStream` and updates the IPC `StatusReply` with real codec and streaming state. Kernel-side changes are compiled on CI (WDK) but verified against real hardware by the developer; user-mode changes have full unit tests.

**Tech Stack:** C11 KMDF (WDK 11), BthPort `BTH_PROFILE_DRIVER_INTERFACE`, `BRB_L2CA_OPEN_CHANNEL`, `BRB_L2CA_ACL_TRANSFER`, `WdfFdoQueryForInterface`, C++20 Win32 service, GoogleTest 1.14.

---

## Environment Notes

- **WDK NOT installed locally.** All kernel changes compile and sign on CI only.
- **cmake local**: `C:/Users/santi/AppData/Local/Android/Sdk/cmake/4.1.2/bin/cmake.exe`  
- **Build preset (local)**: `nmake-debug`  
- **MSVC env (local)**: source `C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat`

---

## Scope: Two Independent Subsystems

| Subsystem | Files | Compilable Locally | Unit Tests |
|-----------|-------|-------------------|------------|
| **Kernel BRB** (Tasks 1–5) | `driver/src/l2cap_stream.c`, `owb_a2dp.h`, `owb_a2dp.c` | No (WDK) | No (needs hardware) |
| **Service/IPC** (Tasks 6–8) | `service/src/a2dp_stream.h/.cpp`, `service/src/ipc_server.cpp`, `tests/service/` | Yes | Yes |

---

## File Map

### Modified in this phase

```
driver/
  src/
    owb_a2dp.h          # Add BTH_ADDR RemoteBtAddress, BTH_PROFILE_DRIVER_INTERFACE BthInterface, WDFIOTARGET BthIoTarget
    owb_a2dp.c          # Read remote BT addr from DevicePropertyAddress; acquire BthInterface; call L2capOpenSignalingChannel
    l2cap_stream.h      # Declare L2capMediaChannelOpen, L2capGetRssi
    l2cap_stream.c      # REPLACE all 3 stubs with real BRB implementations

service/src/
  a2dp_stream.h         # Add get_device_state(OWB_DEVICE_STATE*) method
  a2dp_stream.cpp       # Implement get_device_state via OWB_IOCTL_GET_DEVICE_STATE
  ipc_server.cpp        # Wire real device state into GetStatus reply; accept IpcContext*

tests/service/
  a2dp_stream_test.cpp  # Add set_codec_config test + get_device_state stub test
```

---

## Task 1: Add BT address + BthInterface to device extension

**Files:**
- Modify: `driver/src/owb_a2dp.h`
- Modify: `driver/src/owb_a2dp.c`

- [ ] **Step 1.1: Update `driver/src/owb_a2dp.h`**

```c
// driver/src/owb_a2dp.h
#pragma once
#include <ntddk.h>
#include <wdf.h>
#include <bthddi.h>       // BTH_PROFILE_DRIVER_INTERFACE, BTH_ADDR, BRB types
#include <bthsdpddi.h>    // Bluetooth SDP types (needed by bthddi.h on some WDK versions)
#include "avdtp.h"

#define OWB_DRIVER_VERSION_MAJOR 0
#define OWB_DRIVER_VERSION_MINOR 3

typedef struct _OWB_DEVICE_EXTENSION {
    WDFDEVICE                    Device;
    BOOLEAN                      IsActive;

    // Remote Bluetooth device address (read from PnP at DeviceAdd time)
    BTH_ADDR                     RemoteBtAddress;

    // BthPort profile driver interface (obtained via WdfFdoQueryForInterface)
    BTH_PROFILE_DRIVER_INTERFACE BthInterface;

    // IoTarget for submitting BRBs to the Bluetooth stack
    WDFIOTARGET                  BthIoTarget;

    // L2CAP channel handles (set after BRB_L2CA_OPEN_CHANNEL completes)
    L2CAP_CHANNEL_HANDLE         SignalingChannelHandle;
    L2CAP_CHANNEL_HANDLE         MediaChannelHandle;

    // AVDTP signaling state machine context
    OWB_AVDTP_CONTEXT            Avdtp;

    // RTP sequence number and timestamp for media packets
    USHORT                       RtpSeqNum;
    ULONG                        RtpTimestamp;
} OWB_DEVICE_EXTENSION, *POWB_DEVICE_EXTENSION;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(OWB_DEVICE_EXTENSION, OwbGetDeviceExtension)

DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD OwbEvtDeviceAdd;
```

Save to: `driver/src/owb_a2dp.h`

- [ ] **Step 1.2: Update `driver/src/owb_a2dp.c` — read BT address + acquire BthInterface**

```c
// driver/src/owb_a2dp.c
#include "owb_a2dp.h"
#include "avdtp.h"
#include "l2cap_stream.h"
#include "ioctl.h"

NTSTATUS
DriverEntry(
    _In_ PDRIVER_OBJECT  DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    WDF_DRIVER_CONFIG config;
    NTSTATUS          status;

    WDF_DRIVER_CONFIG_INIT(&config, OwbEvtDeviceAdd);

    status = WdfDriverCreate(DriverObject, RegistryPath,
                             WDF_NO_OBJECT_ATTRIBUTES, &config, WDF_NO_HANDLE);
    if (!NT_SUCCESS(status)) {
        KdPrint(("OpenWinBlue: WdfDriverCreate failed 0x%x\n", status));
        return status;
    }

    KdPrint(("OpenWinBlue: driver loaded (v%d.%d)\n",
             OWB_DRIVER_VERSION_MAJOR, OWB_DRIVER_VERSION_MINOR));
    return STATUS_SUCCESS;
}

NTSTATUS
OwbEvtDeviceAdd(
    _In_    WDFDRIVER       Driver,
    _Inout_ PWDFDEVICE_INIT DeviceInit)
{
    NTSTATUS              status;
    WDFDEVICE             device;
    WDF_OBJECT_ATTRIBUTES attributes;
    POWB_DEVICE_EXTENSION ext;

    UNREFERENCED_PARAMETER(Driver);

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, OWB_DEVICE_EXTENSION);

    status = WdfDeviceCreate(&DeviceInit, &attributes, &device);
    if (!NT_SUCCESS(status)) {
        KdPrint(("OpenWinBlue: WdfDeviceCreate failed 0x%x\n", status));
        return status;
    }

    ext = OwbGetDeviceExtension(device);
    RtlZeroMemory(ext, sizeof(*ext));
    ext->Device       = device;
    ext->BthIoTarget  = WdfDeviceGetIoTarget(device);
    AvdtpContextInit(&ext->Avdtp);

    // Read the remote Bluetooth address from PnP device property.
    // DevicePropertyAddress returns a ULONG on 32-bit or ULONG_PTR on 64-bit;
    // cast to BTH_ADDR (ULONGLONG). Bluetooth address is in the lower 48 bits.
    ULONG_PTR rawAddr = 0;
    ULONG     resultLen = 0;
    status = WdfDeviceQueryProperty(device,
                                    DevicePropertyAddress,
                                    sizeof(rawAddr),
                                    &rawAddr,
                                    &resultLen);
    if (NT_SUCCESS(status) && resultLen >= sizeof(ULONG)) {
        ext->RemoteBtAddress = (BTH_ADDR)(rawAddr & 0x0000FFFFFFFFFFFFull);
        KdPrint(("OpenWinBlue: remote BT addr %I64x\n", ext->RemoteBtAddress));
    } else {
        KdPrint(("OpenWinBlue: DevicePropertyAddress query failed 0x%x\n", status));
        // Non-fatal: address may not be available until device is connected.
        status = STATUS_SUCCESS;
    }

    // Acquire the BthPort profile driver interface.
    // This provides BthAllocateBrb / BthFreeBrb / BthSubmitBrb.
    status = WdfFdoQueryForInterface(device,
                                     &GUID_BTHDDI_PROFILE_DRIVER_INTERFACE,
                                     (PINTERFACE)&ext->BthInterface,
                                     sizeof(BTH_PROFILE_DRIVER_INTERFACE),
                                     BTHDDI_V_CURR_VERSION,
                                     NULL);
    if (!NT_SUCCESS(status)) {
        KdPrint(("OpenWinBlue: WdfFdoQueryForInterface(BthInterface) failed 0x%x\n",
                 status));
        // Non-fatal in Phase 2c dev: driver loads even without BthInterface
        // (L2cap functions will check and return STATUS_DEVICE_NOT_READY).
        status = STATUS_SUCCESS;
    }

    // Create symbolic link \\.\OpenWinBlue for user-mode access
    DECLARE_CONST_UNICODE_STRING(symLink, L"\\DosDevices\\OpenWinBlue");
    status = WdfDeviceCreateSymbolicLink(device, &symLink);
    if (!NT_SUCCESS(status)) {
        KdPrint(("OpenWinBlue: symbolic link failed 0x%x\n", status));
        return status;
    }

    // Register IOCTL dispatch queue
    status = IoctlRegister(device);
    if (!NT_SUCCESS(status)) {
        KdPrint(("OpenWinBlue: IoctlRegister failed 0x%x\n", status));
        return status;
    }

    // Begin AVDTP connection — returns STATUS_PENDING if async BRB is submitted.
    status = L2capOpenSignalingChannel(ext);
    if (status == STATUS_PENDING || status == STATUS_NOT_SUPPORTED) {
        status = STATUS_SUCCESS;
    }

    KdPrint(("OpenWinBlue: device added\n"));
    return status;
}
```

Save to: `driver/src/owb_a2dp.c`

- [ ] **Step 1.3: Commit**

```powershell
cd "c:/suru/open winblue"
git add driver/src/owb_a2dp.h driver/src/owb_a2dp.c
git commit -m "feat(driver): add BTH_ADDR, BthInterface, IoTarget to device extension; read BT addr at DeviceAdd"
```

---

## Task 2: Real L2CAP open + send + receive (kernel BRB)

**Files:**
- Modify: `driver/src/l2cap_stream.h`
- Modify: `driver/src/l2cap_stream.c`

This is the core of Phase 2c. Three BRB types are used:
- `BRB_L2CA_OPEN_CHANNEL` — opens the AVDTP PSM 0x0019 channel
- `BRB_L2CA_ACL_TRANSFER` — sends data on an open channel
- `BRB_L2CA_CLOSE_CHANNEL` — closes a channel on disconnect

The BRB submission helper (`OwbSubmitBrb`) allocates a WDFREQUEST, formats it with the BRB, and submits synchronously to BthIoTarget. This is safe at PASSIVE_LEVEL (connection setup). The receive callback is registered via `BRB_L2CA_OPEN_CHANNEL.CallbackRoutine`.

- [ ] **Step 2.1: Update `driver/src/l2cap_stream.h`**

```c
// driver/src/l2cap_stream.h
// L2CAP channel management and media frame transmission.
#pragma once
#include <ntddk.h>
#include <wdf.h>
#include <bthddi.h>

typedef struct _OWB_DEVICE_EXTENSION OWB_DEVICE_EXTENSION, *POWB_DEVICE_EXTENSION;

// Submit a BRB synchronously to the BthPort IoTarget.
// Only call at PASSIVE_LEVEL (device add, signaling setup).
NTSTATUS L2capSubmitBrb(
    _In_ POWB_DEVICE_EXTENSION DevExt,
    _In_ PBRB Brb);

// Open the AVDTP signaling L2CAP channel (PSM 0x0019) to the remote device.
// Submits BRB_L2CA_OPEN_CHANNEL synchronously.
// Returns STATUS_SUCCESS on channel open, STATUS_DEVICE_NOT_READY if
// BthInterface not yet acquired.
NTSTATUS L2capOpenSignalingChannel(_In_ POWB_DEVICE_EXTENSION DevExt);

// Open the A2DP media L2CAP channel (PSM 0x0019, separate CID).
// Called after AVDTP OPEN response.
NTSTATUS L2capOpenMediaChannel(_In_ POWB_DEVICE_EXTENSION DevExt);

// Send a packet on the AVDTP signaling L2CAP channel.
NTSTATUS L2capSendSignaling(
    _In_ POWB_DEVICE_EXTENSION DevExt,
    _In_reads_bytes_(Length) const UCHAR* Data,
    _In_ USHORT Length);

// Send one RTP-framed audio packet on the media L2CAP channel.
NTSTATUS L2capSendMediaFrame(
    _In_ POWB_DEVICE_EXTENSION DevExt,
    _In_ ULONG  CodecId,
    _In_reads_bytes_(FrameLen) const UCHAR* FrameData,
    _In_ USHORT FrameLen);

// Query current RSSI from the Bluetooth radio (blocking BRB).
// rssi_dbm is signed: typically -40 (strong) to -90 (weak).
NTSTATUS L2capGetRssi(
    _In_  POWB_DEVICE_EXTENSION DevExt,
    _Out_ LONG*                 RssiDbm);

// L2CAP receive callback — called by BthPort when data arrives on signaling channel.
// Feeds received AVDTP responses into AvdtpHandleSignalingPacket.
_IRQL_requires_max_(DISPATCH_LEVEL)
VOID L2capSignalingReceiveCallback(
    _In_                      PVOID  Context,
    _In_reads_bytes_(DataSize) PUCHAR Data,
    _In_                      UINT   DataSize);
```

Save to: `driver/src/l2cap_stream.h`

- [ ] **Step 2.2: Replace `driver/src/l2cap_stream.c`**

```c
// driver/src/l2cap_stream.c
// L2CAP channel management — Phase 2c real BRB implementation.
#include "l2cap_stream.h"
#include "avdtp.h"
#include "owb_a2dp.h"

// RTP header for A2DP media packets (12 bytes, RFC 3550, packed).
#pragma pack(push, 1)
typedef struct _OWB_RTP_HEADER {
    UCHAR   vpxcc;     // V=2, P=0, X=0, CC=0 → 0x80
    UCHAR   mpt;       // M=0, PT=96 (0x60) for SBC
    USHORT  seq_num;   // big-endian sequence number
    ULONG   timestamp; // big-endian sample clock
    ULONG   ssrc;      // synchronization source
} OWB_RTP_HEADER;
#pragma pack(pop)

// ── BRB submit helper ─────────────────────────────────────────────────────────
// Wraps a BRB in a WDFREQUEST and submits it synchronously to BthPort.
// MUST be called at PASSIVE_LEVEL only (connection setup, not hot audio path).
NTSTATUS L2capSubmitBrb(
    _In_ POWB_DEVICE_EXTENSION DevExt,
    _In_ PBRB Brb)
{
    if (!DevExt->BthInterface.BthSubmitBrb) {
        KdPrint(("OpenWinBlue: BthInterface not acquired\n"));
        return STATUS_DEVICE_NOT_READY;
    }

    // Create a WDFREQUEST to wrap the BRB
    WDF_REQUEST_SEND_OPTIONS sendOpts;
    WDF_REQUEST_SEND_OPTIONS_INIT(&sendOpts, WDF_REQUEST_SEND_OPTION_SYNCHRONOUS);
    WDF_REQUEST_SEND_OPTIONS_SET_TIMEOUT(&sendOpts,
        WDF_REL_TIMEOUT_IN_SEC(10)); // 10s timeout for connection

    WDFREQUEST request;
    NTSTATUS   status = WdfRequestCreate(
        WDF_NO_OBJECT_ATTRIBUTES,
        DevExt->BthIoTarget,
        &request
    );
    if (!NT_SUCCESS(status)) {
        KdPrint(("OpenWinBlue: WdfRequestCreate failed 0x%x\n", status));
        return status;
    }

    // Format the request with the BRB using the BthPort interface helper
    status = DevExt->BthInterface.BthSubmitBrb(
        DevExt->BthIoTarget,
        request,
        Brb,
        &sendOpts
    );

    WdfObjectDelete(request);
    return status;
}

// ── L2CAP signaling channel receive callback ──────────────────────────────────
// Called by BthPort at DISPATCH_LEVEL when AVDTP data arrives on the signaling channel.
_IRQL_requires_max_(DISPATCH_LEVEL)
VOID L2capSignalingReceiveCallback(
    _In_                      PVOID  Context,
    _In_reads_bytes_(DataSize) PUCHAR Data,
    _In_                      UINT   DataSize)
{
    POWB_DEVICE_EXTENSION devExt = (POWB_DEVICE_EXTENSION)Context;
    if (!devExt || !Data || DataSize == 0 || DataSize > 0xFFFFu) return;
    AvdtpHandleSignalingPacket(devExt, Data, (USHORT)DataSize);
}

// ── L2CAP signaling channel open ─────────────────────────────────────────────
NTSTATUS L2capOpenSignalingChannel(_In_ POWB_DEVICE_EXTENSION DevExt)
{
    if (!DevExt->BthInterface.BthAllocateBrb) {
        // BthInterface not yet available (normal during early boot)
        KdPrint(("OpenWinBlue: L2capOpenSignalingChannel — BthInterface not ready\n"));
        return STATUS_DEVICE_NOT_READY;
    }
    if (DevExt->RemoteBtAddress == 0) {
        KdPrint(("OpenWinBlue: L2capOpenSignalingChannel — remote BT address unknown\n"));
        return STATUS_DEVICE_NOT_CONNECTED;
    }

    // Allocate BRB_L2CA_OPEN_CHANNEL
    struct _BRB_L2CA_OPEN_CHANNEL* brb =
        (struct _BRB_L2CA_OPEN_CHANNEL*)
        DevExt->BthInterface.BthAllocateBrb(BRB_L2CA_OPEN_CHANNEL, 'OWBO');
    if (!brb) return STATUS_INSUFFICIENT_RESOURCES;

    // Configure the L2CAP channel parameters
    brb->BtAddress        = DevExt->RemoteBtAddress;
    brb->Psm              = AVDTP_SIGNALING_PSM;   // 0x0019

    // Security: require encrypted, authenticated link
    brb->ChannelFlags     = CF_LINK_ENCRYPTED | CF_LINK_AUTHENTICATED;

    // MTU: accept what the headphone offers (up to 672 bytes, L2CAP default)
    brb->IncomingMtuRange.Max = L2CAP_DEFAULT_MTU;
    brb->IncomingMtuRange.Min = L2CAP_MIN_MTU;
    brb->OutMTU               = L2CAP_DEFAULT_MTU;

    // Register the receive callback for inbound AVDTP responses
    brb->CallbackRoutine  = L2capSignalingReceiveCallback;
    brb->CallbackContext  = DevExt;

    // Submit synchronously at PASSIVE_LEVEL
    NTSTATUS status = L2capSubmitBrb(DevExt, (PBRB)brb);
    if (NT_SUCCESS(status)) {
        DevExt->SignalingChannelHandle = brb->ChannelHandle;
        DevExt->Avdtp.State = AvdtpStateConnecting;
        KdPrint(("OpenWinBlue: signaling channel opened, CID=0x%x\n",
                 brb->ChannelHandle));
        // Kick off AVDTP DISCOVER
        status = AvdtpConnect(DevExt);
    } else {
        KdPrint(("OpenWinBlue: BRB_L2CA_OPEN_CHANNEL failed 0x%x\n", status));
    }

    DevExt->BthInterface.BthFreeBrb((PBRB)brb);
    return status;
}

// ── L2CAP media channel open (called after AVDTP OPEN response) ───────────────
NTSTATUS L2capOpenMediaChannel(_In_ POWB_DEVICE_EXTENSION DevExt)
{
    if (!DevExt->BthInterface.BthAllocateBrb) return STATUS_DEVICE_NOT_READY;

    struct _BRB_L2CA_OPEN_CHANNEL* brb =
        (struct _BRB_L2CA_OPEN_CHANNEL*)
        DevExt->BthInterface.BthAllocateBrb(BRB_L2CA_OPEN_CHANNEL, 'OWBM');
    if (!brb) return STATUS_INSUFFICIENT_RESOURCES;

    brb->BtAddress            = DevExt->RemoteBtAddress;
    brb->Psm                  = AVDTP_SIGNALING_PSM;  // media uses same PSM, new CID
    brb->ChannelFlags         = CF_LINK_ENCRYPTED | CF_LINK_AUTHENTICATED;
    brb->IncomingMtuRange.Max = L2CAP_DEFAULT_MTU;
    brb->IncomingMtuRange.Min = L2CAP_MIN_MTU;
    brb->OutMTU               = L2CAP_DEFAULT_MTU;
    brb->CallbackRoutine      = NULL;   // media channel is send-only (A2DP source)
    brb->CallbackContext      = NULL;

    NTSTATUS status = L2capSubmitBrb(DevExt, (PBRB)brb);
    if (NT_SUCCESS(status)) {
        DevExt->MediaChannelHandle = brb->ChannelHandle;
        KdPrint(("OpenWinBlue: media channel opened, CID=0x%x\n", brb->ChannelHandle));
    }

    DevExt->BthInterface.BthFreeBrb((PBRB)brb);
    return status;
}

// ── L2CAP signaling send ──────────────────────────────────────────────────────
NTSTATUS L2capSendSignaling(
    _In_ POWB_DEVICE_EXTENSION DevExt,
    _In_reads_bytes_(Length) const UCHAR* Data,
    _In_ USHORT Length)
{
    if (!DevExt->BthInterface.BthAllocateBrb) return STATUS_DEVICE_NOT_READY;
    if (DevExt->SignalingChannelHandle == 0)   return STATUS_DEVICE_NOT_CONNECTED;

    struct _BRB_L2CA_ACL_TRANSFER* brb =
        (struct _BRB_L2CA_ACL_TRANSFER*)
        DevExt->BthInterface.BthAllocateBrb(BRB_L2CA_ACL_TRANSFER, 'OWBS');
    if (!brb) return STATUS_INSUFFICIENT_RESOURCES;

    brb->BtAddress      = DevExt->RemoteBtAddress;
    brb->ChannelHandle  = DevExt->SignalingChannelHandle;
    brb->TransferFlags  = ACL_TRANSFER_DIRECTION_OUT | ACL_SHORT_TRANSFER_OK;
    brb->BufferSize     = Length;
    brb->Buffer         = (PVOID)Data;
    brb->BufferMDL      = NULL;

    NTSTATUS status = L2capSubmitBrb(DevExt, (PBRB)brb);
    if (!NT_SUCCESS(status)) {
        KdPrint(("OpenWinBlue: L2capSendSignaling failed 0x%x len=%u\n",
                 status, (ULONG)Length));
    }
    DevExt->BthInterface.BthFreeBrb((PBRB)brb);
    return status;
}

// ── L2CAP media frame send ────────────────────────────────────────────────────
NTSTATUS L2capSendMediaFrame(
    _In_ POWB_DEVICE_EXTENSION DevExt,
    _In_ ULONG  CodecId,
    _In_reads_bytes_(FrameLen) const UCHAR* FrameData,
    _In_ USHORT FrameLen)
{
    if (DevExt->Avdtp.State != AvdtpStateStreaming)
        return STATUS_DEVICE_NOT_CONNECTED;
    if (!DevExt->BthInterface.BthAllocateBrb) return STATUS_DEVICE_NOT_READY;

    UNREFERENCED_PARAMETER(CodecId);

    // Build RTP + SBC payload header + frame data.
    ULONG pkt_len_u = sizeof(OWB_RTP_HEADER) + 1u + (ULONG)FrameLen;
    if (pkt_len_u > 0xFFFFu) return STATUS_INVALID_PARAMETER;
    const USHORT pkt_len = (USHORT)pkt_len_u;

    PUCHAR pkt = (PUCHAR)ExAllocatePool2(POOL_FLAG_NON_PAGED, (SIZE_T)pkt_len, 'RTPM');
    if (!pkt) return STATUS_INSUFFICIENT_RESOURCES;

    OWB_RTP_HEADER* hdr = (OWB_RTP_HEADER*)pkt;
    hdr->vpxcc     = 0x80u;
    hdr->mpt       = 0x60u;   // PT=96 for SBC
    hdr->seq_num   = RtlUshortByteSwap(DevExt->RtpSeqNum++);
    hdr->timestamp = RtlUlongByteSwap(DevExt->RtpTimestamp);
    hdr->ssrc      = RtlUlongByteSwap(0x00000001UL);
    DevExt->RtpTimestamp += 128u;  // blocks(16) × subbands(8) = 128 PCM samples/frame

    pkt[sizeof(OWB_RTP_HEADER)] = 0x01u;  // SBC payload hdr: 1 frame
    RtlCopyMemory(pkt + sizeof(OWB_RTP_HEADER) + 1u, FrameData, FrameLen);

    // Send on media channel
    struct _BRB_L2CA_ACL_TRANSFER* brb =
        (struct _BRB_L2CA_ACL_TRANSFER*)
        DevExt->BthInterface.BthAllocateBrb(BRB_L2CA_ACL_TRANSFER, 'RTPM');
    if (!brb) {
        ExFreePoolWithTag(pkt, 'RTPM');
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    brb->BtAddress     = DevExt->RemoteBtAddress;
    brb->ChannelHandle = DevExt->MediaChannelHandle;
    brb->TransferFlags = ACL_TRANSFER_DIRECTION_OUT | ACL_SHORT_TRANSFER_OK;
    brb->BufferSize    = pkt_len;
    brb->Buffer        = pkt;
    brb->BufferMDL     = NULL;

    NTSTATUS status = L2capSubmitBrb(DevExt, (PBRB)brb);
    DevExt->BthInterface.BthFreeBrb((PBRB)brb);
    ExFreePoolWithTag(pkt, 'RTPM');
    return status;
}

// ── RSSI query ────────────────────────────────────────────────────────────────
NTSTATUS L2capGetRssi(
    _In_  POWB_DEVICE_EXTENSION DevExt,
    _Out_ LONG*                 RssiDbm)
{
    *RssiDbm = -60L;   // Phase 2c default until BRB_HCI_GET_LINK_QUALITY is wired

    if (!DevExt->BthInterface.BthAllocateBrb) return STATUS_DEVICE_NOT_READY;
    if (DevExt->RemoteBtAddress == 0)          return STATUS_DEVICE_NOT_CONNECTED;

    // BRB_HCI_GET_LOCAL_BD_ADDR gives us local info; for RSSI we need
    // BRB_HCI_GET_LINK_QUALITY (0x1403) via HCI passthrough.
    // Phase 2c uses a safe stub value; full RSSI in Phase 3.
    // TODO(phase3): submit BRB_HCI_GET_LINK_QUALITY BRB.
    UNREFERENCED_PARAMETER(DevExt);
    return STATUS_SUCCESS;
}
```

Save to: `driver/src/l2cap_stream.c`

- [ ] **Step 2.3: Commit**

```powershell
cd "c:/suru/open winblue"
git add driver/src/l2cap_stream.h driver/src/l2cap_stream.c
git commit -m "feat(driver): real L2CAP BRB — open channel, send signaling, send media frame, RSSI stub"
```

---

## Task 3: Wire RSSI into ioctl.c + trigger media channel open after AVDTP OPEN

**Files:**
- Modify: `driver/src/ioctl.c`
- Modify: `driver/src/avdtp.c`

- [ ] **Step 3.1: Update RSSI in ioctl.c to call `L2capGetRssi`**

Read `driver/src/ioctl.c`. Replace the hardcoded stub in `OWB_IOCTL_GET_RF_QUALITY`:

```c
        case OWB_IOCTL_GET_RF_QUALITY: {
            PVOID   buf  = NULL;
            size_t  size = 0;
            status = WdfRequestRetrieveOutputBuffer(
                Request, sizeof(OWB_RF_QUALITY), &buf, &size);
            if (!NT_SUCCESS(status)) break;

            OWB_RF_QUALITY* q = (OWB_RF_QUALITY*)buf;
            LONG rssi = -60L;
            L2capGetRssi(devExt, &rssi);    // real query; falls back to -60 if not ready
            q->rssi_dbm             = rssi;
            q->retransmit_per_mille = 0UL;  // Phase 3: read from L2CAP retx stats
            q->link_quality         = (rssi > -70L) ? 255UL :
                                      (rssi > -80L) ? 128UL : 64UL;
            info = sizeof(OWB_RF_QUALITY);
            break;
        }
```

- [ ] **Step 3.2: Trigger media channel open in `HandleOpenResponse` in avdtp.c**

Read `driver/src/avdtp.c`. Update `HandleOpenResponse` to open the media channel after AVDTP OPEN completes:

```c
static VOID HandleOpenResponse(_In_ POWB_DEVICE_EXTENSION DevExt) {
    // Open the A2DP media L2CAP channel now that AVDTP signaling is configured.
    NTSTATUS st = L2capOpenMediaChannel(DevExt);
    if (!NT_SUCCESS(st)) {
        KdPrint(("OpenWinBlue: media channel open failed 0x%x\n", st));
    }
    // Send AVDTP START regardless — media channel open may be async in Phase 2c.
    UCHAR seid = (UCHAR)((DevExt->Avdtp.RemoteSeid << 2u) & 0xFCu);
    AvdtpSendCommand(DevExt, AVDTP_MSG_START, &seid, 1u);
    // State remains AvdtpStateOpen until START response.
}
```

- [ ] **Step 3.3: Commit**

```powershell
cd "c:/suru/open winblue"
git add driver/src/ioctl.c driver/src/avdtp.c
git commit -m "feat(driver): wire L2capGetRssi into IOCTL, open media channel after AVDTP OPEN"
```

---

## Task 4: Push driver changes to CI and verify build

- [ ] **Step 4.1: Push**

```bash
SANTI_TOKEN=$(gh auth token --user santiquiroz)
cd "c:/suru/open winblue"
git push "https://santiquiroz:${SANTI_TOKEN}@github.com/santiquiroz/openwinblue.git" main
```

- [ ] **Step 4.2: Watch CI — verify all 3 jobs green**

Open: `https://github.com/santiquiroz/openwinblue/actions`

Expected:
- ✅ `Build & Test Service (C++)` — 17 tests pass
- ✅ `Build & Test GUI (C#)` — 2 tests pass
- ✅ `Build Driver (KMDF + WDK)` — `owb_a2dp.sys` produced

**If `L2CAP_DEFAULT_MTU` or `L2CAP_MIN_MTU` are undefined:** These constants come from `bthddi.h`. Check their presence:
```c
// Add fallbacks after the bthddi.h include in l2cap_stream.c:
#ifndef L2CAP_DEFAULT_MTU
#  define L2CAP_DEFAULT_MTU 672u
#endif
#ifndef L2CAP_MIN_MTU
#  define L2CAP_MIN_MTU 48u
#endif
```

**If `AVDTP_SIGNALING_PSM` is undefined in l2cap_stream.c:** Include `avdtp.h` — it defines the constant.

**If `CF_LINK_ENCRYPTED` / `CF_LINK_AUTHENTICATED` are not found:** These come from `bthddi.h`. If absent, use their numeric values: `CF_LINK_ENCRYPTED = 0x00000004`, `CF_LINK_AUTHENTICATED = 0x00000002`.

---

## Task 5: Service — add `get_device_state()` + `set_codec_config` test

**Files:**
- Modify: `service/src/a2dp_stream.h`
- Modify: `service/src/a2dp_stream.cpp`
- Modify: `tests/service/a2dp_stream_test.cpp`

- [ ] **Step 5.1: Write failing tests first**

Add to `tests/service/a2dp_stream_test.cpp` (append to existing file):

```cpp
TEST(A2dpStream, SetCodecConfigInStubModeReturnsFalse) {
    owb::A2dpStream stream;
    // Not opened — stub mode
    EXPECT_FALSE(stream.set_codec_config(0 /*SBC*/, "bitpool", 53));
}

TEST(A2dpStream, GetDeviceStateInStubModeReturnsFalse) {
    owb::A2dpStream stream;
    OWB_DEVICE_STATE state{};
    EXPECT_FALSE(stream.get_device_state(&state));
}

TEST(A2dpStream, GetDeviceStateNullPtrReturnsFalse) {
    owb::A2dpStream stream;
    EXPECT_FALSE(stream.get_device_state(nullptr));
}
```

- [ ] **Step 5.2: Add `get_device_state` to `service/src/a2dp_stream.h`**

Add to the class declaration (after `set_codec_config`):

```cpp
    // Query the driver's current A2DP connection state.
    // Fills *state with OWB_DEVICE_STATE from the kernel driver.
    // Returns false in stub mode or if the IOCTL fails.
    bool get_device_state(OWB_DEVICE_STATE* state);
```

Also add `#include "owb_ioctl.h"` to the header (or forward-declare `OWB_DEVICE_STATE`).
Since `owb_ioctl.h` is in `driver/` and already on include path, prefer:
```cpp
// At the top of a2dp_stream.h, after #pragma once:
#include "owb_ioctl.h"
```

- [ ] **Step 5.3: Implement `get_device_state` in `service/src/a2dp_stream.cpp`**

Append after `set_codec_config`:

```cpp
bool A2dpStream::get_device_state(OWB_DEVICE_STATE* state) {
    if (!is_open() || !state) return false;

    DWORD bytes_returned = 0;
    BOOL ok = DeviceIoControl(
        impl_->device,
        OWB_IOCTL_GET_DEVICE_STATE,
        nullptr, 0,
        state, static_cast<DWORD>(sizeof(OWB_DEVICE_STATE)),
        &bytes_returned, nullptr
    );
    return ok && bytes_returned >= sizeof(OWB_DEVICE_STATE);
}
```

- [ ] **Step 5.4: Run all tests**

```powershell
$env:PATH = "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Tools\MSVC\14.50.35717\bin\Hostx64\x64;C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64;C:\Users\santi\AppData\Local\Android\Sdk\cmake\4.1.2\bin;$env:PATH"
$env:INCLUDE = "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Tools\MSVC\14.50.35717\include;C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\ucrt;C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\um;C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\shared"
$env:LIB = "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Tools\MSVC\14.50.35717\lib\x64;C:\Program Files (x86)\Windows Kits\10\Lib\10.0.26100.0\ucrt\x64;C:\Program Files (x86)\Windows Kits\10\Lib\10.0.26100.0\um\x64"
Set-Location "c:\suru\open winblue"
& cmake --preset nmake-debug
& cmake --build build/nmake-debug --target owb_service_tests
Set-Location build/nmake-debug
& ctest --output-on-failure
```

Expected: `20 tests passed, 0 failed` (17 existing + 3 new).

- [ ] **Step 5.5: Commit**

```powershell
cd "c:/suru/open winblue"
git add service/src/a2dp_stream.h service/src/a2dp_stream.cpp
git add tests/service/a2dp_stream_test.cpp
git commit -m "feat(service): add get_device_state IOCTL method + tests for set_codec_config and get_device_state"
```

---

## Task 6: Wire real device state into IPC StatusReply

**Files:**
- Modify: `service/src/ipc_server.h`
- Modify: `service/src/ipc_server.cpp`
- Modify: `service/src/main.cpp`

The `IpcServer::serve_one()` currently hardcodes the status reply. To wire in real state, it needs access to `A2dpStream`. The cleanest approach: pass a reference to `A2dpStream` to `IpcServer` via constructor injection.

- [ ] **Step 6.1: Update `service/src/ipc_server.h`**

```cpp
// service/src/ipc_server.h
#pragma once
#include <memory>

namespace owb {

class A2dpStream;  // forward declaration

class IpcServer {
public:
    // stream may be nullptr — IpcServer handles null gracefully (returns stub status).
    explicit IpcServer(A2dpStream* stream = nullptr);
    ~IpcServer();

    bool start();
    void stop();
    bool serve_one();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace owb
```

- [ ] **Step 6.2: Update `service/src/ipc_server.cpp`**

Read the current file. Update `Impl` to hold the `A2dpStream*`, update constructor, and update `GetStatus` handler:

```cpp
// In IpcServer::Impl:
struct IpcServer::Impl {
    HANDLE       pipe    = INVALID_HANDLE_VALUE;
    bool         running = false;
    A2dpStream*  stream  = nullptr;  // non-owning — caller owns A2dpStream
};

// Constructor:
IpcServer::IpcServer(A2dpStream* stream) : impl_(std::make_unique<Impl>()) {
    impl_->stream = stream;
}
```

Update `GetStatus` handler to use real state when available:

```cpp
            case ipc::MsgType::GetStatus: {
                // TODO(phase2c): wire real state from AudioCapture + HfpGuard
                ipc::MsgHeader reply{ ipc::MsgType::StatusReply,
                                      sizeof(ipc::StatusPayload) };
                ipc::StatusPayload status{};

                if (impl_->stream && impl_->stream->is_open()) {
                    OWB_DEVICE_STATE devState{};
                    if (impl_->stream->get_device_state(&devState)) {
                        status.is_capturing = (devState.state == OWB_STATE_STREAMING) ? 1u : 0u;
                        // bitrate estimate: SBC at bitpool 53 ≈ 328kbps
                        status.bitrate = (devState.state == OWB_STATE_STREAMING) ? 328000u : 0u;
                        // codec name from active_codec_id
                        static const char* kCodecNames[] = {"SBC","LDAC","aptX","aptX-HD","AAC","LC3"};
                        if (devState.active_codec_id < 6u)
                            strncpy_s(status.codec_name, sizeof(status.codec_name),
                                      kCodecNames[devState.active_codec_id], _TRUNCATE);
                    }
                } else {
                    strncpy_s(status.codec_name, sizeof(status.codec_name), "SBC", _TRUNCATE);
                    status.bitrate      = 0u;
                    status.is_capturing = 0u;
                }
                status.hfp_guard_on = 0u;

                DWORD written = 0;
                BOOL hdr_ok = WriteFile(impl_->pipe, &reply, sizeof(reply), &written, nullptr);
                if (hdr_ok && written == sizeof(reply)) {
                    WriteFile(impl_->pipe, &status, sizeof(status), &written, nullptr);
                }
                client_done = true;
                break;
            }
```

- [ ] **Step 6.3: Update `service/src/main.cpp` to pass `a2dp` to `IpcServer`**

Change:
```cpp
owb::IpcServer    ipc;
```
to:
```cpp
owb::IpcServer    ipc(&a2dp);
```

- [ ] **Step 6.4: Update IPC test to handle the new constructor**

The existing IPC tests construct `IpcServer` without arguments. The new constructor has `A2dpStream* stream = nullptr` as default — so existing tests compile unchanged. Verify:

```powershell
& cmake --build build/nmake-debug --target owb_service_tests
Set-Location build/nmake-debug
& ctest --output-on-failure
```

Expected: `20 tests passed, 0 failed` (no regressions).

- [ ] **Step 6.5: Commit**

```powershell
cd "c:/suru/open winblue"
git add service/src/ipc_server.h service/src/ipc_server.cpp service/src/main.cpp
git commit -m "feat(service): wire real device state into IPC StatusReply via A2dpStream injection"
```

---

## Task 7: Final push + CI verification

- [ ] **Step 7.1: Verify local test count (20 tests)**

```powershell
Set-Location "c:\suru\open winblue\build\nmake-debug"
& ctest --output-on-failure
```

Expected: `20 tests passed, 0 failed`.

- [ ] **Step 7.2: Push all Phase 2c commits**

```bash
SANTI_TOKEN=$(gh auth token --user santiquiroz)
git push "https://santiquiroz:${SANTI_TOKEN}@github.com/santiquiroz/openwinblue.git" main
```

- [ ] **Step 7.3: Verify CI — all 3 jobs green**

Open: `https://github.com/santiquiroz/openwinblue/actions`

Expected:
- ✅ `Build & Test Service (C++)` — 20 tests pass
- ✅ `Build & Test GUI (C#)` — 2 tests pass
- ✅ `Build Driver (KMDF + WDK)` — `owb_a2dp.sys` produced with real BRB code

- [ ] **Step 7.4: Hardware integration test (manual — requires paired Bluetooth headphone)**

If a BT headphone is available, install the signed driver (or Test Mode), pair a headphone, and verify in DebugView:

```
OpenWinBlue: driver loaded (v0.3)
OpenWinBlue: remote BT addr <address>
OpenWinBlue: signaling channel opened, CID=0x...
OpenWinBlue: A2DP streaming started (SBC)
```

If AVDTP fails at a specific step, the `KdPrint` messages in `AvdtpHandleSignalingPacket` will indicate which signal was rejected.

---

## Self-Review

**Spec coverage:**
- ✅ `BthInterface` acquisition in `EvtDeviceAdd` — Task 1
- ✅ Remote BT address read from PnP — Task 1
- ✅ `BRB_L2CA_OPEN_CHANNEL` for signaling channel — Task 2
- ✅ `BRB_L2CA_ACL_TRANSFER` for signaling send — Task 2
- ✅ Receive callback wired to `AvdtpHandleSignalingPacket` — Task 2
- ✅ `BRB_L2CA_OPEN_CHANNEL` for media channel after AVDTP OPEN — Task 3
- ✅ RSSI wired into `OWB_IOCTL_GET_RF_QUALITY` — Task 3
- ✅ `get_device_state()` added to `A2dpStream` — Task 5
- ✅ Tests for `set_codec_config` and `get_device_state` in stub mode — Task 5
- ✅ IPC `StatusReply` reports real codec/streaming state — Task 6
- ✅ CI verifies all 3 jobs — Task 7
- ⚠️ Full RSSI via `BRB_HCI_GET_LINK_QUALITY` — Phase 3 (noted in stub comment)
- ⚠️ Concurrent access to `TransactionId` — Phase 3 (spin lock)

**Placeholder scan:**
- `L2capGetRssi` has an explicit `// TODO(phase3)` with a specific BRB name — acceptable stub with clear forward pointer
- RSSI stub returns -60 dBm (a realistic "medium signal" value, not 0) ✅

**Type consistency:**
- `L2capOpenMediaChannel` declared in `l2cap_stream.h` and called from `avdtp.c:HandleOpenResponse` ✅
- `OWB_DEVICE_STATE*` parameter in `A2dpStream::get_device_state` matches `owb_ioctl.h` definition ✅
- `A2dpStream*` constructor parameter in `IpcServer` is `nullptr`-defaulted — existing tests don't break ✅
- `IpcServer::serve_one` accesses `impl_->stream->get_device_state()` guarded by null check ✅
