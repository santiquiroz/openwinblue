// driver/src/ioctl.c
// IOCTL dispatch — receives requests from owb-service.exe.
#include "ioctl.h"
#include "l2cap_stream.h"
#include "owb_a2dp.h"
#include "../owb_ioctl.h"

NTSTATUS IoctlRegister(_In_ WDFDEVICE Device) {
    WDF_IO_QUEUE_CONFIG cfg;
    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&cfg, WdfIoQueueDispatchSequential);
    cfg.EvtIoDeviceControl = OwbEvtIoDeviceControl;

    WDFQUEUE queue;
    return WdfIoQueueCreate(Device, &cfg, WDF_NO_OBJECT_ATTRIBUTES, &queue);
}

VOID OwbEvtIoDeviceControl(
    _In_ WDFQUEUE   Queue,
    _In_ WDFREQUEST Request,
    _In_ size_t     OutputBufferLength,
    _In_ size_t     InputBufferLength,
    _In_ ULONG      IoControlCode)
{
    UNREFERENCED_PARAMETER(OutputBufferLength);
    UNREFERENCED_PARAMETER(InputBufferLength);

    WDFDEVICE            device  = WdfIoQueueGetDevice(Queue);
    POWB_DEVICE_EXTENSION devExt = OwbGetDeviceExtension(device);
    NTSTATUS             status  = STATUS_SUCCESS;
    ULONG_PTR            info    = 0;

    switch (IoControlCode) {

        case OWB_IOCTL_SEND_AUDIO_FRAME: {
            PVOID   buf  = NULL;
            size_t  size = 0;
            status = WdfRequestRetrieveInputBuffer(
                Request, sizeof(OWB_SEND_FRAME_INPUT), &buf, &size);
            if (!NT_SUCCESS(status)) break;

            POWB_SEND_FRAME_INPUT frame = (POWB_SEND_FRAME_INPUT)buf;
            // Validate: data_len must fit in the input buffer AND not overflow
            // the RTP packet length (OWB_RTP_OVERHEAD + data_len <= 0xFFFF).
            if ((SIZE_T)frame->data_len > size - offsetof(OWB_SEND_FRAME_INPUT, data) ||
                frame->data_len > 0xFFFFu - OWB_RTP_OVERHEAD) {
                status = STATUS_INVALID_PARAMETER;
                break;
            }
            status = L2capSendMediaFrame(devExt,
                                         frame->codec_id,
                                         frame->data,
                                         (USHORT)frame->data_len);
            break;
        }

        case OWB_IOCTL_GET_RF_QUALITY: {
            PVOID   buf  = NULL;
            size_t  size = 0;
            status = WdfRequestRetrieveOutputBuffer(
                Request, sizeof(OWB_RF_QUALITY), &buf, &size);
            if (!NT_SUCCESS(status)) break;

            OWB_RF_QUALITY* q = (OWB_RF_QUALITY*)buf;
            // Phase 2c: read from BthPort RSSI BRB.
            q->rssi_dbm             = -60L;
            q->retransmit_per_mille = 0UL;
            q->link_quality         = 255UL;
            info = sizeof(OWB_RF_QUALITY);
            break;
        }

        case OWB_IOCTL_SET_CODEC_CONFIG: {
            PVOID   buf  = NULL;
            size_t  size = 0;
            status = WdfRequestRetrieveInputBuffer(
                Request, sizeof(OWB_CODEC_CONFIG), &buf, &size);
            if (!NT_SUCCESS(status)) break;

            POWB_CODEC_CONFIG cfg = (POWB_CODEC_CONFIG)buf;
            KdPrint(("OpenWinBlue: SET_CODEC_CONFIG codec=%lu key=%.16s val=%lld\n",
                     cfg->codec_id, cfg->param_key, cfg->param_value));
            // Phase 2c: trigger AVDTP SET_CONFIGURATION reconfiguration.
            break;
        }

        case OWB_IOCTL_GET_DEVICE_STATE: {
            PVOID   buf  = NULL;
            size_t  size = 0;
            status = WdfRequestRetrieveOutputBuffer(
                Request, sizeof(OWB_DEVICE_STATE), &buf, &size);
            if (!NT_SUCCESS(status)) break;

            OWB_DEVICE_STATE* state = (OWB_DEVICE_STATE*)buf;
            state->state           = (ULONG)devExt->Avdtp.State;
            state->active_codec_id = devExt->Avdtp.ActiveCodecId;
            RtlZeroMemory(state->remote_addr, sizeof(state->remote_addr));
            info = sizeof(OWB_DEVICE_STATE);
            break;
        }

        default:
            status = STATUS_INVALID_DEVICE_REQUEST;
            break;
    }

    WdfRequestCompleteWithInformation(Request, status, info);
}
