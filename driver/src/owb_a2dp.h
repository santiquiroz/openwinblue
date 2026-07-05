// driver/src/owb_a2dp.h
#pragma once
#include <ntddk.h>
#include <wdf.h>
// Correct Bluetooth WDK include order per official bthecho sample:
#include <initguid.h>   // instantiates GUIDs (GUID_BTHDDI_PROFILE_DRIVER_INTERFACE etc.)
#include <bthdef.h>     // BTH_ADDR, BTHSTATUS
#include <bthguid.h>    // BT service GUIDs
#include <bthioctl.h>   // BT IOCTL codes
#include <bthddi.h>     // BTH_PROFILE_DRIVER_INTERFACE, BRB types, L2CAP_CHANNEL_HANDLE
#include "avdtp.h"

#define OWB_DRIVER_VERSION_MAJOR 0
#define OWB_DRIVER_VERSION_MINOR 3

typedef struct _OWB_DEVICE_EXTENSION {
    WDFDEVICE                    Device;
    BOOLEAN                      IsActive;

    // Remote Bluetooth device address (read from PnP DevicePropertyAddress)
    BTH_ADDR                     RemoteBtAddress;

    // BthPort profile driver interface (BthAllocateBrb / BthFreeBrb)
    BTH_PROFILE_DRIVER_INTERFACE BthInterface;

    // IoTarget for submitting BRBs to the Bluetooth stack
    WDFIOTARGET                  BthIoTarget;

    // Pre-allocated WDFREQUEST reused for synchronous BRB submission.
    // Created once in EvtDeviceAdd, reused via WdfRequestReuse each time.
    WDFREQUEST                   BrbRequest;

    // Serializa el uso de BrbRequest: es un único request compartido por todos
    // los paths (open signaling/media, send signaling, send media/SEND_FRAME,
    // BRB IN). Dos WdfRequestReuse concurrentes sobre un request en vuelo
    // corrompen y crashean (0xC0000005) — pasaba en la reconexión mientras el
    // servicio seguía mandando frames. Todos los submits son a PASSIVE_LEVEL.
    WDFWAITLOCK                  BrbLock;

    // L2CAP channel handles (set after BRB_L2CA_OPEN_CHANNEL completes)
    L2CAP_CHANNEL_HANDLE         SignalingChannelHandle;
    L2CAP_CHANNEL_HANDLE         MediaChannelHandle;

    // AVDTP signaling state machine context
    OWB_AVDTP_CONTEXT            Avdtp;

    // Preferred codec for A2DP negotiation (OWB_CODEC_*).
    // Set to OWB_CODEC_SBC on init; updated via OWB_IOCTL_SET_CODEC_CONFIG.
    ULONG                        PreferredCodecId;

    // RTP sequence number and timestamp for media packets
    USHORT                       RtpSeqNum;
    ULONG                        RtpTimestamp;

    // Work item for deferring AVDTP signaling processing to PASSIVE_LEVEL.
    // L2CAP receive callbacks can arrive at DISPATCH_LEVEL — all AVDTP
    // processing (which allocates BRBs via WdfRequestCreate) must run
    // at PASSIVE_LEVEL via this work item.
    WDFWORKITEM              AvdtpWorkItem;
    UCHAR                    AvdtpWorkBuf[672]; // L2CAP_DEFAULT_MTU bytes
    USHORT                   AvdtpWorkLen;
    ULONG                    PendingRecvLen;    // set by callback, read by work item

    // Timer periódico que (re)abre el canal de señalización cuando el AVDTP está
    // Idle: maneja el link no listo en EvtDeviceAdd y la reconexión tras caída
    // del enlace BT. El callback corre a DISPATCH_LEVEL (un timer PASSIVE no está
    // soportado como hijo de este device) y encola ReconnectWorkItem, que hace el
    // trabajo L2CAP a PASSIVE_LEVEL.
    WDFTIMER                 ReconnectTimer;
    WDFWORKITEM              ReconnectWorkItem;
} OWB_DEVICE_EXTENSION, *POWB_DEVICE_EXTENSION;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(OWB_DEVICE_EXTENSION, OwbGetDeviceExtension)

DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD OwbEvtDeviceAdd;
EVT_WDF_WORKITEM OwbAvdtpWorkCallback;
EVT_WDF_WORKITEM OwbReconnectWorkCallback;
EVT_WDF_TIMER    OwbReconnectTimer;
