// driver/src/owb_a2dp.c
// GUIDs are instantiated by <initguid.h> in owb_a2dp.h.
#include "owb_a2dp.h"
#include "avdtp.h"
#include "l2cap_stream.h"
#include "ioctl.h"
#include "owb_trace.h"

// DEVPKEY_Bluetooth_DeviceAddress {2BD67D8B-8BEB-48D5-87E0-6CDA3428040A}, 1
// La BT address remota expuesta por el stack BT como string hex de 12 chars
// (p.ej. "88C9E86B1EBF"). DevicePropertyAddress (legacy) viene vacía en los
// nodos BTHENUM de este equipo, así que se usa esta.
DEFINE_DEVPROPKEY(OWB_DEVPKEY_Bluetooth_DeviceAddress,
                  0x2BD67D8B, 0x8BEB, 0x48D5, 0x87, 0xE0, 0x6C, 0xDA,
                  0x34, 0x28, 0x04, 0x0A, 1);

// Convierte un string hex ("88C9E86B1EBF") a BTH_ADDR (48 bits bajos).
static BTH_ADDR OwbParseBtAddrHex(_In_ PCWSTR Str)
{
    BTH_ADDR addr = 0;
    for (PCWSTR p = Str; *p != L'\0'; ++p) {
        WCHAR c = *p;
        BTH_ADDR nibble;
        if      (c >= L'0' && c <= L'9') nibble = (BTH_ADDR)(c - L'0');
        else if (c >= L'a' && c <= L'f') nibble = (BTH_ADDR)(10 + (c - L'a'));
        else if (c >= L'A' && c <= L'F') nibble = (BTH_ADDR)(10 + (c - L'A'));
        else continue;
        addr = (addr << 4) | nibble;
    }
    return addr & 0x0000FFFFFFFFFFFFull;
}

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

    // Seguridad del device: la deja PnP por defecto (SYSTEM full; el servicio
    // corre como LocalSystem y abre \\.\OpenWinBlue sin problema). NO se usa
    // WdfDeviceInitAssignSDDLString aquí: es para control devices, y en un PnP
    // FDO hacía fallar WdfDeviceCreate con STATUS_INVALID_SECURITY_DESCR
    // (0xC0000079). El endurecimiento SYS+Admin se hará por el INF [DDInstall.HW].
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

    // BT address remota vía DEVPKEY_Bluetooth_DeviceAddress (string hex).
    WDF_DEVICE_PROPERTY_DATA propData;
    WDF_DEVICE_PROPERTY_DATA_INIT(&propData, &OWB_DEVPKEY_Bluetooth_DeviceAddress);

    WCHAR       addrBuf[32] = { 0 };
    ULONG       reqLen   = 0;
    DEVPROPTYPE propType = 0;
    status = WdfDeviceQueryPropertyEx(device, &propData,
                                      sizeof(addrBuf), addrBuf, &reqLen, &propType);
    if (NT_SUCCESS(status) && propType == DEVPROP_TYPE_STRING) {
        ext->RemoteBtAddress = OwbParseBtAddrHex(addrBuf);
        OwbLog("EvtDeviceAdd: BT addr remota %012I64x", ext->RemoteBtAddress);
    } else {
        OwbLog("EvtDeviceAdd: query BT addr fallo 0x%x type=%lu - continuo",
               status, propType);
    }
    status = STATUS_SUCCESS;

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

    // Abrir el canal de señalización AVDTP es BEST-EFFORT: es una tarea de
    // runtime, no un requisito para crear el device. Su fallo (link BT no listo,
    // RemoteBtAddress==0, BthInterface aún no adquirida, etc.) NO debe hacer
    // fallar EvtDeviceAdd — si no, WDF destruye el device y desaparece el
    // control interface \\.\OpenWinBlue, y el servicio nunca puede conectar ni
    // disparar la negociación. La conexión se reintenta en runtime.
    NTSTATUS l2capStatus = L2capOpenSignalingChannel(ext);
    OwbLog("EvtDeviceAdd: L2capOpenSignalingChannel -> 0x%x (best-effort)", l2capStatus);

    OwbLog("EvtDeviceAdd: dispositivo agregado v%d.%d OK",
           OWB_DRIVER_VERSION_MAJOR, OWB_DRIVER_VERSION_MINOR);
    return STATUS_SUCCESS;
}
