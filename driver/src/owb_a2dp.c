// driver/src/owb_a2dp.c
// GUIDs are instantiated by <initguid.h> in owb_a2dp.h.
#include "owb_a2dp.h"
#include "avdtp.h"
#include "l2cap_stream.h"
#include "ioctl.h"
#include <wdmsec.h>   // SDDL_DEVOBJ_SYS_ALL_ADMIN_ALL

// PASSIVE_LEVEL worker: reads pending AVDTP signaling data via BRB and processes it.
// Enqueued by L2capSignalingIndicationCallback at DISPATCH_LEVEL when
// IndicationRecvPacket fires with PendingRecvLen set.
VOID OwbAvdtpWorkCallback(_In_ WDFWORKITEM WorkItem)
{
    WDFDEVICE device = (WDFDEVICE)WdfWorkItemGetParentObject(WorkItem);
    POWB_DEVICE_EXTENSION ext = OwbGetDeviceExtension(device);

    if (!ext->BthInterface.BthAllocateBrb || ext->PendingRecvLen == 0) return;

    const ULONG readLen = min(ext->PendingRecvLen, (ULONG)sizeof(ext->AvdtpWorkBuf));
    ext->PendingRecvLen = 0;

    // Submit BRB_L2CA_ACL_TRANSFER IN to read the pending packet from BthPort.
    struct _BRB_L2CA_ACL_TRANSFER* brb =
        (struct _BRB_L2CA_ACL_TRANSFER*)
        ext->BthInterface.BthAllocateBrb(BRB_L2CA_ACL_TRANSFER, 'OWBR');
    if (!brb) return;

    brb->BtAddress     = ext->RemoteBtAddress;
    brb->ChannelHandle = ext->SignalingChannelHandle;
    brb->TransferFlags = ACL_TRANSFER_DIRECTION_IN | ACL_SHORT_TRANSFER_OK;
    brb->BufferSize    = readLen;
    brb->Buffer        = ext->AvdtpWorkBuf;
    brb->BufferMDL     = NULL;

    NTSTATUS status = L2capSubmitBrb(ext, (PBRB)brb, sizeof(*brb));
    if (NT_SUCCESS(status) && brb->BufferSize > 0) {
        ext->AvdtpWorkLen = (USHORT)brb->BufferSize;
        AvdtpHandleSignalingPacket(ext, ext->AvdtpWorkBuf, ext->AvdtpWorkLen);
        ext->AvdtpWorkLen = 0;
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

    // Restrict the control device to SYSTEM + Administrators. The user-mode
    // service runs as LocalSystem; normal user processes must not be able to
    // open \\.\OpenWinBlue and inject audio frames or codec-config IOCTLs.
    DECLARE_CONST_UNICODE_STRING(sddl, SDDL_DEVOBJ_SYS_ALL_ADMIN_ALL);
    (VOID)WdfDeviceInitAssignSDDLString(DeviceInit, &sddl);

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
    // Version 0x0200 (BTHDDI_PROFILE_DRIVER_INTERFACE_VERSION_FOR_QI) per bthddi.h.
    status = WdfFdoQueryForInterface(device,
                                     &GUID_BTHDDI_PROFILE_DRIVER_INTERFACE,
                                     (PINTERFACE)&ext->BthInterface,
                                     sizeof(BTH_PROFILE_DRIVER_INTERFACE),
                                     BTHDDI_PROFILE_DRIVER_INTERFACE_VERSION_FOR_QI,
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

    // Create a pre-allocated WDFREQUEST for synchronous BRB submission.
    // Reused across all BRB calls via WdfRequestReuse (see L2capSubmitBrb).
    WDF_OBJECT_ATTRIBUTES reqAttribs;
    WDF_OBJECT_ATTRIBUTES_INIT(&reqAttribs);
    reqAttribs.ParentObject = device;
    status = WdfRequestCreate(&reqAttribs, ext->BthIoTarget, &ext->BrbRequest);
    if (!NT_SUCCESS(status)) {
        KdPrint(("OpenWinBlue: WdfRequestCreate (BrbRequest) failed 0x%x\n", status));
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
