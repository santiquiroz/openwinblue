// driver/owb_codec_ids.h
// Codec IDs shared by the driver, the service and host-side tests.
// Plain C header with no includes, so pure-C driver modules can use it too.
#pragma once

// Codec IDs — matches owb::ipc::SetCodecPayload.codec_name convention
#define OWB_CODEC_SBC    0u
#define OWB_CODEC_LDAC   1u
#define OWB_CODEC_APTX   2u
#define OWB_CODEC_APTXHD 3u
#define OWB_CODEC_AAC              4u
#define OWB_CODEC_LC3              5u
#define OWB_CODEC_APTX_ADAPTIVE    6u
