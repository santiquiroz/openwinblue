// driver/src/owb_a2dp.h
#pragma once
#include <ntddk.h>
#include <wdf.h>
#include <bthddi.h>
#include <bthsdpddi.h>
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
} OWB_DEVICE_EXTENSION, *POWB_DEVICE_EXTENSION;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(OWB_DEVICE_EXTENSION, OwbGetDeviceExtension)

DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD OwbEvtDeviceAdd;
