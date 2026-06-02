// driver/src/owb_a2dp.h
#pragma once
#include <ntddk.h>
#include <wdf.h>
// bthddi.h provides: BTH_ADDR, BTH_PROFILE_DRIVER_INTERFACE, L2CAP_CHANNEL_HANDLE,
// BRB types (BRB_L2CA_OPEN_CHANNEL, BRB_L2CA_ACL_TRANSFER), and all BT constants.
// It pulls in bthdef.h and bthioctl.h internally — do not include them separately.
#include <bthddi.h>
#include "avdtp.h"

#define OWB_DRIVER_VERSION_MAJOR 0
#define OWB_DRIVER_VERSION_MINOR 3

typedef struct _OWB_DEVICE_EXTENSION {
    WDFDEVICE                    Device;
    BOOLEAN                      IsActive;

    // Remote Bluetooth device address (read from PnP DevicePropertyAddress)
    BTH_ADDR                     RemoteBtAddress;

    // BthPort profile driver interface (BthAllocateBrb / BthFreeBrb / BthSubmitBrb)
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

    // Work item for deferring AVDTP signaling processing to PASSIVE_LEVEL.
    // L2CAP receive callbacks can arrive at DISPATCH_LEVEL — all AVDTP
    // processing (which allocates BRBs via WdfRequestCreate) must run
    // at PASSIVE_LEVEL via this work item.
    WDFWORKITEM              AvdtpWorkItem;
    UCHAR                    AvdtpWorkBuf[672]; // L2CAP_DEFAULT_MTU bytes
    USHORT                   AvdtpWorkLen;
} OWB_DEVICE_EXTENSION, *POWB_DEVICE_EXTENSION;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(OWB_DEVICE_EXTENSION, OwbGetDeviceExtension)

DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD OwbEvtDeviceAdd;
EVT_WDF_WORKITEM OwbAvdtpWorkCallback;
