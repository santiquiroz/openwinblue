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
    ext->Device        = device;
    ext->IsActive      = FALSE;
    ext->RtpSeqNum     = 0;
    ext->RtpTimestamp  = 0;
    AvdtpContextInit(&ext->Avdtp);

    // Create symbolic link \\.\OpenWinBlue for user-mode access
    DECLARE_CONST_UNICODE_STRING(symLink, L"\\DosDevices\\OpenWinBlue");
    status = WdfDeviceCreateSymbolicLink(device, &symLink);
    if (!NT_SUCCESS(status)) {
        KdPrint(("OpenWinBlue: symbolic link creation failed 0x%x\n", status));
        return status;
    }

    // Register IOCTL dispatch queue
    status = IoctlRegister(device);
    if (!NT_SUCCESS(status)) {
        KdPrint(("OpenWinBlue: IoctlRegister failed 0x%x\n", status));
        return status;
    }

    // Begin AVDTP connection attempt (stub in Phase 2b — will fail gracefully)
    status = L2capOpenSignalingChannel(ext);
    if (status == STATUS_NOT_SUPPORTED) {
        status = STATUS_SUCCESS;  // expected during Phase 2b development
    }

    KdPrint(("OpenWinBlue: device added, AVDTP initialized\n"));
    return status;
}
