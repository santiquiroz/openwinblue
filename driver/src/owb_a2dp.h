#pragma once

#include <ntddk.h>
#include <wdf.h>

// Driver version
#define OWB_DRIVER_VERSION_MAJOR 0
#define OWB_DRIVER_VERSION_MINOR 1

// Device extension — per-device context stored by WDF.
typedef struct _OWB_DEVICE_EXTENSION {
    WDFDEVICE   Device;
    BOOLEAN     IsActive;
} OWB_DEVICE_EXTENSION, *POWB_DEVICE_EXTENSION;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(OWB_DEVICE_EXTENSION, OwbGetDeviceExtension)

// Forward declarations
DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD OwbEvtDeviceAdd;
