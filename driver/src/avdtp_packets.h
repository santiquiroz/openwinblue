// driver/src/avdtp_packets.h
// WDK-free AVDTP packet builders and parsers, shared by the driver and host tests.
#pragma once
// Only <stddef.h>: the WDK kernel CRT ships no <stdint.h> or <stdbool.h>.
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// SEID 0x00 is forbidden by AVDTP, so it doubles as "not found".
#define OWB_AVDTP_SEID_NONE 0x00u

// ACP SEID + INT SEID + Media Transport + the largest Media Codec (aptX HD, LOSC 13).
#define OWB_AVDTP_SET_CONFIGURATION_MAX_LEN 19u

// Returns the SEID of the first endpoint in a DISCOVER response that is an audio
// sink and not in use, or OWB_AVDTP_SEID_NONE.
unsigned char owb_avdtp_find_free_audio_sink_seid(const unsigned char* data, size_t len);

// Returns non-zero when the GET_CAPABILITIES response advertises codec_id (OWB_CODEC_*).
int owb_avdtp_capabilities_contain_codec(const unsigned char* caps, size_t caps_len,
                                         unsigned long codec_id);

// Returns preferred_codec_id when the sink supports it and it is configurable over
// A2DP; OWB_CODEC_SBC otherwise.
unsigned long owb_avdtp_select_codec(unsigned long preferred_codec_id,
                                     const unsigned char* caps, size_t caps_len);

// Writes the SET_CONFIGURATION command payload for codec_id into buf and returns its
// length, or 0 when the codec has no A2DP configuration or buf is too small.
size_t owb_avdtp_build_set_configuration(unsigned long codec_id,
                                         unsigned char acp_seid, unsigned char int_seid,
                                         unsigned char* buf, size_t buf_len);

#ifdef __cplusplus
}
#endif
