#include "owb_a2dp.h"

// DriverEntry — called by Windows when the driver is loaded.
NTSTATUS
DriverEntry(
    _In_ PDRIVER_OBJECT  DriverObject,
    _In_ PUNICODE_STRING RegistryPath
)
{
    WDF_DRIVER_CONFIG config;
    NTSTATUS          status;

    WDF_DRIVER_CONFIG_INIT(&config, OwbEvtDeviceAdd);

    status = WdfDriverCreate(
        DriverObject,
        RegistryPath,
        WDF_NO_OBJECT_ATTRIBUTES,
        &config,
        WDF_NO_HANDLE
    );

    if (!NT_SUCCESS(status)) {
        KdPrint(("OpenWinBlue: WdfDriverCreate failed 0x%x\n", status));
        return status;
    }

    KdPrint(("OpenWinBlue: driver loaded (v%d.%d)\n",
             OWB_DRIVER_VERSION_MAJOR,
             OWB_DRIVER_VERSION_MINOR));

    return STATUS_SUCCESS;
}

// OwbEvtDeviceAdd — called when a Bluetooth audio device is enumerated.
NTSTATUS
OwbEvtDeviceAdd(
    _In_    WDFDRIVER       Driver,
    _Inout_ PWDFDEVICE_INIT DeviceInit
)
{
    NTSTATUS               status;
    WDFDEVICE              device;
    WDF_OBJECT_ATTRIBUTES  attributes;
    POWB_DEVICE_EXTENSION  ext;

    UNREFERENCED_PARAMETER(Driver);

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, OWB_DEVICE_EXTENSION);

    status = WdfDeviceCreate(&DeviceInit, &attributes, &device);
    if (!NT_SUCCESS(status)) {
        KdPrint(("OpenWinBlue: WdfDeviceCreate failed 0x%x\n", status));
        return status;
    }

    ext = OwbGetDeviceExtension(device);
    ext->Device   = device;
    ext->IsActive = FALSE;

    KdPrint(("OpenWinBlue: device added\n"));
    return STATUS_SUCCESS;
}
