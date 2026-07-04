// driver/src/ioctl.c
// IOCTL dispatch — receives requests from owb-service.exe.
#include "ioctl.h"
#include "avdtp.h"
#include "l2cap_stream.h"
#include "owb_a2dp.h"
#include "owb_trace.h"
#include "../owb_ioctl.h"

NTSTATUS IoctlRegister(_In_ WDFDEVICE Device) {
    WDF_IO_QUEUE_CONFIG cfg;
    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&cfg, WdfIoQueueDispatchSequential);
    cfg.EvtIoDeviceControl = OwbEvtIoDeviceControl;

    WDFQUEUE queue;
    NTSTATUS status = WdfIoQueueCreate(Device, &cfg, WDF_NO_OBJECT_ATTRIBUTES, &queue);
    OwbLog("IoctlRegister: cola IOCTL -> 0x%x", status);
    return status;
}

VOID OwbEvtIoDeviceControl(
    _In_ WDFQUEUE   Queue,
    _In_ WDFREQUEST Request,
    _In_ size_t     OutputBufferLength,
    _In_ size_t     InputBufferLength,
    _In_ ULONG      IoControlCode)
{
    WDFDEVICE            device  = WdfIoQueueGetDevice(Queue);
    POWB_DEVICE_EXTENSION devExt = OwbGetDeviceExtension(device);
    NTSTATUS             status  = STATUS_SUCCESS;
    ULONG_PTR            info    = 0;

    switch (IoControlCode) {

        case OWB_IOCTL_SEND_AUDIO_FRAME: {
            static ULONG    frameCount  = 0;
            static NTSTATUS lastStatus  = STATUS_SUCCESS;

            PVOID   buf  = NULL;
            size_t  size = 0;
            status = WdfRequestRetrieveInputBuffer(
                Request, sizeof(OWB_SEND_FRAME_INPUT), &buf, &size);
            if (!NT_SUCCESS(status)) {
                OwbLog("SEND_FRAME: RetrieveInputBuffer fallo 0x%x (in=%Iu)",
                       status, InputBufferLength);
                break;
            }

            POWB_SEND_FRAME_INPUT frame = (POWB_SEND_FRAME_INPUT)buf;
            // Validate: data_len must fit in the input buffer AND not overflow
            // the RTP packet length (OWB_RTP_OVERHEAD + data_len <= 0xFFFF).
            if ((SIZE_T)frame->data_len > size - offsetof(OWB_SEND_FRAME_INPUT, data) ||
                frame->data_len > 0xFFFFu - OWB_RTP_OVERHEAD) {
                OwbLog("SEND_FRAME: data_len invalido %lu (buf=%Iu)",
                       frame->data_len, size);
                status = STATUS_INVALID_PARAMETER;
                break;
            }
            status = L2capSendMediaFrame(devExt,
                                         frame->codec_id,
                                         frame->data,
                                         (USHORT)frame->data_len);

            frameCount++;
            if (status != lastStatus) {
                OwbLog("SEND_FRAME: status cambio 0x%x -> 0x%x (frame #%lu codec=%lu len=%lu)",
                       lastStatus, status, frameCount, frame->codec_id, frame->data_len);
                lastStatus = status;
            } else if ((frameCount % 1000u) == 0u) {
                OwbLog("SEND_FRAME: %lu frames (codec=%lu len=%lu status=0x%x)",
                       frameCount, frame->codec_id, frame->data_len, status);
            }
            break;
        }

        case OWB_IOCTL_GET_RF_QUALITY: {
            PVOID   buf  = NULL;
            size_t  size = 0;
            status = WdfRequestRetrieveOutputBuffer(
                Request, sizeof(OWB_RF_QUALITY), &buf, &size);
            if (!NT_SUCCESS(status)) {
                OwbLog("GET_RF_QUALITY: RetrieveOutputBuffer fallo 0x%x", status);
                break;
            }

            OWB_RF_QUALITY* q = (OWB_RF_QUALITY*)buf;
            LONG rssi = -60L;
            L2capGetRssi(devExt, &rssi);  // real query; stub until Phase 3 BRB
            q->rssi_dbm             = rssi;
            q->retransmit_per_mille = 0UL;  // Phase 3: L2CAP retransmission stats
            q->link_quality         = (rssi > -70L) ? 255UL :
                                      (rssi > -80L) ? 128UL : 64UL;
            info = sizeof(OWB_RF_QUALITY);
            break;
        }

        case OWB_IOCTL_SET_CODEC_CONFIG: {
            PVOID   buf  = NULL;
            size_t  size = 0;
            status = WdfRequestRetrieveInputBuffer(
                Request, sizeof(OWB_CODEC_CONFIG), &buf, &size);
            if (!NT_SUCCESS(status)) {
                OwbLog("SET_CODEC_CONFIG: RetrieveInputBuffer fallo 0x%x", status);
                break;
            }

            POWB_CODEC_CONFIG cfg = (POWB_CODEC_CONFIG)buf;
            OwbLog("SET_CODEC_CONFIG: codec=%lu key=%.16s val=%lld",
                   cfg->codec_id, cfg->param_key, cfg->param_value);

            // key starts with "sw" → "switch" — triggers codec negotiation change.
            if (cfg->param_key[0] == 's' && cfg->param_key[1] == 'w') {
                status = AvdtpSetPreferredCodec(devExt, cfg->codec_id);
                OwbLog("SET_CODEC_CONFIG: AvdtpSetPreferredCodec -> 0x%x", status);
            }
            // Other keys (bitpool, freq, quality, etc.) are Phase 5c.
            break;
        }

        case OWB_IOCTL_GET_DEVICE_STATE: {
            static ULONG stateQueryCount = 0;

            PVOID   buf  = NULL;
            size_t  size = 0;
            status = WdfRequestRetrieveOutputBuffer(
                Request, sizeof(OWB_DEVICE_STATE), &buf, &size);
            if (!NT_SUCCESS(status)) {
                OwbLog("GET_DEVICE_STATE: RetrieveOutputBuffer fallo 0x%x", status);
                break;
            }

            OWB_DEVICE_STATE* state = (OWB_DEVICE_STATE*)buf;
            state->state           = (ULONG)devExt->Avdtp.State;
            state->active_codec_id = devExt->Avdtp.ActiveCodecId;
            RtlZeroMemory(state->remote_addr, sizeof(state->remote_addr));
            info = sizeof(OWB_DEVICE_STATE);

            stateQueryCount++;
            if ((stateQueryCount % 50u) == 1u) {
                OwbLog("GET_DEVICE_STATE: #%lu estado=%s codec=%lu",
                       stateQueryCount, OwbAvdtpStateName(devExt->Avdtp.State),
                       devExt->Avdtp.ActiveCodecId);
            }
            break;
        }

        default:
            OwbLog("IOCTL desconocido 0x%08lx (in=%Iu out=%Iu)",
                   IoControlCode, InputBufferLength, OutputBufferLength);
            status = STATUS_INVALID_DEVICE_REQUEST;
            break;
    }

    WdfRequestCompleteWithInformation(Request, status, info);
}
