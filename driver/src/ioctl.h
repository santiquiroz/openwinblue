// driver/src/ioctl.h
// IOCTL dispatch handler for the owb_a2dp device.
#pragma once
#include <ntddk.h>
#include <wdf.h>

typedef struct _OWB_DEVICE_EXTENSION OWB_DEVICE_EXTENSION, *POWB_DEVICE_EXTENSION;

// Register default IOCTL queue on the WDF device.
NTSTATUS IoctlRegister(_In_ WDFDEVICE Device);

// WDF IOCTL dispatch callback.
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL OwbEvtIoDeviceControl;
