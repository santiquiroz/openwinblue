// driver/src/owb_a2dp.c
#include "owb_a2dp.h"
#include "avdtp.h"
#include "l2cap_stream.h"
#include "ioctl.h"

// PASSIVE_LEVEL worker: processes buffered AVDTP signaling data.
VOID OwbAvdtpWorkCallback(_In_ WDFWORKITEM WorkItem)
{
    WDFDEVICE device = (WDFDEVICE)WdfWorkItemGetParentObject(WorkItem);
    POWB_DEVICE_EXTENSION ext = OwbGetDeviceExtension(device);
    if (ext->AvdtpWorkLen > 0) {
        AvdtpHandleSignalingPacket(ext, ext->AvdtpWorkBuf, ext->AvdtpWorkLen);
        ext->AvdtpWorkLen = 0;
    }
}

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
    ext->Device      = device;
    ext->BthIoTarget = WdfDeviceGetIoTarget(device);
    AvdtpContextInit(&ext->Avdtp);

    // Read the remote Bluetooth address from PnP.
    // DevicePropertyAddress encodes the BT address in the lower 48 bits.
    ULONG_PTR rawAddr  = 0;
    ULONG     resultLen = 0;
    status = WdfDeviceQueryProperty(device,
                                    DevicePropertyAddress,
                                    sizeof(rawAddr),
                                    &rawAddr,
                                    &resultLen);
    if (NT_SUCCESS(status) && resultLen >= sizeof(BTH_ADDR)) {
        ext->RemoteBtAddress = (BTH_ADDR)(rawAddr & 0x0000FFFFFFFFFFFFull);
        KdPrint(("OpenWinBlue: remote BT addr %I64x\n", ext->RemoteBtAddress));
    } else {
        KdPrint(("OpenWinBlue: DevicePropertyAddress query failed 0x%x — continuing\n",
                 status));
        status = STATUS_SUCCESS;
    }

    // Acquire BthPort profile driver interface.
    status = WdfFdoQueryForInterface(device,
                                     &GUID_BTHDDI_PROFILE_DRIVER_INTERFACE,
                                     (PINTERFACE)&ext->BthInterface,
                                     sizeof(BTH_PROFILE_DRIVER_INTERFACE),
                                     BTHDDI_V_CURR_VERSION,
                                     NULL);
    if (!NT_SUCCESS(status)) {
        KdPrint(("OpenWinBlue: WdfFdoQueryForInterface failed 0x%x — continuing\n",
                 status));
        status = STATUS_SUCCESS;
    }

    // Create symbolic link \\.\OpenWinBlue for user-mode DeviceIoControl.
    DECLARE_CONST_UNICODE_STRING(symLink, L"\\DosDevices\\OpenWinBlue");
    status = WdfDeviceCreateSymbolicLink(device, &symLink);
    if (!NT_SUCCESS(status)) {
        KdPrint(("OpenWinBlue: symbolic link failed 0x%x\n", status));
        return status;
    }

    // Register IOCTL dispatch queue.
    status = IoctlRegister(device);
    if (!NT_SUCCESS(status)) {
        KdPrint(("OpenWinBlue: IoctlRegister failed 0x%x\n", status));
        return status;
    }

    // Create the AVDTP deferred work item.
    // Worker runs at PASSIVE_LEVEL — safe to call WdfRequestCreate.
    WDF_WORKITEM_CONFIG workCfg;
    WDF_WORKITEM_CONFIG_INIT(&workCfg, OwbAvdtpWorkCallback);
    WDF_OBJECT_ATTRIBUTES workAttribs;
    WDF_OBJECT_ATTRIBUTES_INIT(&workAttribs);
    workAttribs.ParentObject = device;
    status = WdfWorkItemCreate(&workCfg, &workAttribs, &ext->AvdtpWorkItem);
    if (!NT_SUCCESS(status)) {
        KdPrint(("OpenWinBlue: WdfWorkItemCreate failed 0x%x\n", status));
        return status;
    }

    // Begin AVDTP signaling channel open.
    status = L2capOpenSignalingChannel(ext);
    if (status == STATUS_PENDING ||
        status == STATUS_NOT_SUPPORTED ||
        status == STATUS_DEVICE_NOT_READY) {
        status = STATUS_SUCCESS;  // expected until BthInterface is acquired
    }

    KdPrint(("OpenWinBlue: device added (v%d.%d)\n",
             OWB_DRIVER_VERSION_MAJOR, OWB_DRIVER_VERSION_MINOR));
    return status;
}
