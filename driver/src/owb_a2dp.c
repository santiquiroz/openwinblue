// driver/src/owb_a2dp.c
// GUIDs are instantiated by <initguid.h> in owb_a2dp.h.
#include "owb_a2dp.h"
#include "avdtp.h"
#include "l2cap_stream.h"
#include "ioctl.h"
#include "owb_trace.h"
#include <wdmsec.h>   // SDDL_DEVOBJ_SYS_ALL_ADMIN_ALL

// PASSIVE_LEVEL worker: reads pending AVDTP signaling data via BRB and processes it.
// Enqueued by L2capSignalingIndicationCallback at DISPATCH_LEVEL when
// IndicationRecvPacket fires with PendingRecvLen set.
VOID OwbAvdtpWorkCallback(_In_ WDFWORKITEM WorkItem)
{
    WDFDEVICE device = (WDFDEVICE)WdfWorkItemGetParentObject(WorkItem);
    POWB_DEVICE_EXTENSION ext = OwbGetDeviceExtension(device);

    if (!ext->BthInterface.BthAllocateBrb || ext->PendingRecvLen == 0) {
        OwbLog("AvdtpWork: nada que leer (iface=%d pending=%lu)",
               ext->BthInterface.BthAllocateBrb != NULL, ext->PendingRecvLen);
        return;
    }

    const ULONG readLen = min(ext->PendingRecvLen, (ULONG)sizeof(ext->AvdtpWorkBuf));
    OwbLog("AvdtpWork: leyendo %lu bytes del canal de signaling", readLen);
    ext->PendingRecvLen = 0;

    // Submit BRB_L2CA_ACL_TRANSFER IN to read the pending packet from BthPort.
    struct _BRB_L2CA_ACL_TRANSFER* brb =
        (struct _BRB_L2CA_ACL_TRANSFER*)
        ext->BthInterface.BthAllocateBrb(BRB_L2CA_ACL_TRANSFER, 'OWBR');
    if (!brb) {
        OwbLog("AvdtpWork: BthAllocateBrb fallo (sin memoria)");
        return;
    }

    brb->BtAddress     = ext->RemoteBtAddress;
    brb->ChannelHandle = ext->SignalingChannelHandle;
    brb->TransferFlags = ACL_TRANSFER_DIRECTION_IN | ACL_SHORT_TRANSFER_OK;
    brb->BufferSize    = readLen;
    brb->Buffer        = ext->AvdtpWorkBuf;
    brb->BufferMDL     = NULL;

    NTSTATUS status = L2capSubmitBrb(ext, (PBRB)brb, sizeof(*brb));
    if (NT_SUCCESS(status) && brb->BufferSize > 0) {
        OwbLog("AvdtpWork: recibidos %lu bytes, procesando AVDTP", brb->BufferSize);
        ext->AvdtpWorkLen = (USHORT)brb->BufferSize;
        AvdtpHandleSignalingPacket(ext, ext->AvdtpWorkBuf, ext->AvdtpWorkLen);
        ext->AvdtpWorkLen = 0;
    } else {
        OwbLog("AvdtpWork: BRB IN fallo 0x%x (size=%lu)", status, brb->BufferSize);
    }

    ext->BthInterface.BthFreeBrb((PBRB)brb);
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
        OwbLog("WdfDriverCreate fallo 0x%x", status);
        return status;
    }

    OwbLog("driver cargado v%d.%d",
           OWB_DRIVER_VERSION_MAJOR, OWB_DRIVER_VERSION_MINOR);
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

    OwbLog("EvtDeviceAdd: inicio");

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, OWB_DEVICE_EXTENSION);

    // Restrict the control device to SYSTEM + Administrators. The user-mode
    // service runs as LocalSystem; normal user processes must not be able to
    // open \\.\OpenWinBlue and inject audio frames or codec-config IOCTLs.
    // SDDL literal de SDDL_DEVOBJ_SYS_ALL_ADM_ALL (evita linkear wdmsec.lib).
    DECLARE_CONST_UNICODE_STRING(sddl, L"D:P(A;;GA;;;SY)(A;;GA;;;BA)");
    (VOID)WdfDeviceInitAssignSDDLString(DeviceInit, &sddl);

    status = WdfDeviceCreate(&DeviceInit, &attributes, &device);
    if (!NT_SUCCESS(status)) {
        OwbLog("WdfDeviceCreate fallo 0x%x", status);
        return status;
    }
    OwbLog("EvtDeviceAdd: WDFDEVICE creado");

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
        OwbLog("EvtDeviceAdd: BT addr remota %012I64x", ext->RemoteBtAddress);
    } else {
        OwbLog("EvtDeviceAdd: DevicePropertyAddress fallo 0x%x (len=%lu) - continuo",
               status, resultLen);
        status = STATUS_SUCCESS;
    }

    // Acquire BthPort profile driver interface.
    // Version 0x0200 (BTHDDI_PROFILE_DRIVER_INTERFACE_VERSION_FOR_QI) per bthddi.h.
    status = WdfFdoQueryForInterface(device,
                                     &GUID_BTHDDI_PROFILE_DRIVER_INTERFACE,
                                     (PINTERFACE)&ext->BthInterface,
                                     sizeof(BTH_PROFILE_DRIVER_INTERFACE),
                                     BTHDDI_PROFILE_DRIVER_INTERFACE_VERSION_FOR_QI,
                                     NULL);
    if (!NT_SUCCESS(status)) {
        OwbLog("EvtDeviceAdd: QI BthPort DDI fallo 0x%x - continuo SIN interfaz BT", status);
        status = STATUS_SUCCESS;
    } else {
        OwbLog("EvtDeviceAdd: interfaz BthPort DDI adquirida");
    }

    // Create symbolic link \\.\OpenWinBlue for user-mode DeviceIoControl.
    DECLARE_CONST_UNICODE_STRING(symLink, L"\\DosDevices\\OpenWinBlue");
    status = WdfDeviceCreateSymbolicLink(device, &symLink);
    if (!NT_SUCCESS(status)) {
        OwbLog("EvtDeviceAdd: symlink DosDevices fallo 0x%x", status);
        return status;
    }
    OwbLog("EvtDeviceAdd: symlink \\\\.\\OpenWinBlue creado");

    // Register IOCTL dispatch queue.
    status = IoctlRegister(device);
    if (!NT_SUCCESS(status)) {
        OwbLog("EvtDeviceAdd: IoctlRegister fallo 0x%x", status);
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
        OwbLog("EvtDeviceAdd: WdfWorkItemCreate fallo 0x%x", status);
        return status;
    }

    // Create a pre-allocated WDFREQUEST for synchronous BRB submission.
    // Reused across all BRB calls via WdfRequestReuse (see L2capSubmitBrb).
    WDF_OBJECT_ATTRIBUTES reqAttribs;
    WDF_OBJECT_ATTRIBUTES_INIT(&reqAttribs);
    reqAttribs.ParentObject = device;
    status = WdfRequestCreate(&reqAttribs, ext->BthIoTarget, &ext->BrbRequest);
    if (!NT_SUCCESS(status)) {
        OwbLog("EvtDeviceAdd: WdfRequestCreate (BrbRequest) fallo 0x%x", status);
        return status;
    }

    // Begin AVDTP signaling channel open.
    status = L2capOpenSignalingChannel(ext);
    OwbLog("EvtDeviceAdd: L2capOpenSignalingChannel -> 0x%x", status);
    if (status == STATUS_PENDING ||
        status == STATUS_NOT_SUPPORTED ||
        status == STATUS_DEVICE_NOT_READY) {
        status = STATUS_SUCCESS;  // expected until BthInterface is acquired
    }

    OwbLog("EvtDeviceAdd: dispositivo agregado v%d.%d (status final 0x%x)",
           OWB_DRIVER_VERSION_MAJOR, OWB_DRIVER_VERSION_MINOR, status);
    return status;
}
