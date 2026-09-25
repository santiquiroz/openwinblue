// driver/src/avdtp_packets.c
// WDK-free AVDTP packet builders and parsers (AVDTP 1.3 §8.6, A2DP 1.3 §4).
#include "avdtp_packets.h"
#include "owb_codec_ids.h"

#define AVDTP_SEID_SHIFT                 2u
#define AVDTP_SEID_MASK                  0x3Fu
#define AVDTP_SEP_INFO_LEN               2u
#define AVDTP_SEP_IN_USE_BIT             0x02u
#define AVDTP_SEP_TSEP_BIT               0x08u
#define AVDTP_SEP_MEDIA_TYPE_SHIFT       4u

#define AVDTP_MEDIA_TYPE_AUDIO           0x00u
#define AVDTP_CATEGORY_MEDIA_TRANSPORT   0x01u
#define AVDTP_CATEGORY_MEDIA_CODEC       0x07u
#define AVDTP_CAPABILITY_HEADER_LEN      2u

#define A2DP_CODEC_SBC                   0x00u
#define A2DP_CODEC_MPEG24_AAC            0x02u
#define A2DP_CODEC_VENDOR                0xFFu

#define A2DP_VENDOR_APTX                 0x0000004Fu
#define A2DP_VENDOR_APTX_HD              0x000000D7u
#define A2DP_VENDOR_SONY                 0x0000012Du
#define A2DP_APTX_CODEC_ID               0x0001u
#define A2DP_APTX_HD_CODEC_ID            0x0024u
#define A2DP_LDAC_CODEC_ID               0x00AAu

// Codec-type byte + vendor ID (4) + vendor codec ID (2).
#define A2DP_VENDOR_MATCH_LEN            7u
#define A2DP_STANDARD_MATCH_LEN          1u

#define LE16_BYTES(v) (unsigned char)((v) & 0xFFu), (unsigned char)(((v) >> 8) & 0xFFu)
#define LE32_BYTES(v) LE16_BYTES((v) & 0xFFFFu), LE16_BYTES(((v) >> 16) & 0xFFFFu)

// Codec-specific bytes that follow the Media Type octet in a Media Codec capability.
typedef struct {
    unsigned long        codec_id;
    const unsigned char* info;
    size_t               info_len;
    size_t               match_len;
} codec_config_t;

// 44.1 kHz | Joint Stereo, 16 blocks | 8 subbands | Loudness, bitpool 2-53.
static const unsigned char kSbcInfo[] = {
    A2DP_CODEC_SBC, 0x21u, 0x15u, 0x02u, 0x35u,
};

// MPEG-2 AAC LC, 44.1 kHz, stereo, CBR 256 kbps.
static const unsigned char kAacInfo[] = {
    A2DP_CODEC_MPEG24_AAC, 0x80u, 0x01u, 0x04u, 0x03u, 0xE8u, 0x00u,
};

// 44.1 kHz (0x20) | stereo (0x02).
static const unsigned char kAptxInfo[] = {
    A2DP_CODEC_VENDOR, LE32_BYTES(A2DP_VENDOR_APTX), LE16_BYTES(A2DP_APTX_CODEC_ID),
    0x22u,
};

// 44.1 kHz (0x20) | stereo (0x02), then 4 reserved octets.
static const unsigned char kAptxHdInfo[] = {
    A2DP_CODEC_VENDOR, LE32_BYTES(A2DP_VENDOR_APTX_HD), LE16_BYTES(A2DP_APTX_HD_CODEC_ID),
    0x22u, 0x00u, 0x00u, 0x00u, 0x00u,
};

// 44.1 kHz (0x20), stereo channel mode (0x01).
static const unsigned char kLdacInfo[] = {
    A2DP_CODEC_VENDOR, LE32_BYTES(A2DP_VENDOR_SONY), LE16_BYTES(A2DP_LDAC_CODEC_ID),
    0x20u, 0x01u,
};

static const codec_config_t kCodecConfigs[] = {
    { OWB_CODEC_SBC,    kSbcInfo,    sizeof(kSbcInfo),    A2DP_STANDARD_MATCH_LEN },
    { OWB_CODEC_AAC,    kAacInfo,    sizeof(kAacInfo),    A2DP_STANDARD_MATCH_LEN },
    { OWB_CODEC_APTX,   kAptxInfo,   sizeof(kAptxInfo),   A2DP_VENDOR_MATCH_LEN },
    { OWB_CODEC_APTXHD, kAptxHdInfo, sizeof(kAptxHdInfo), A2DP_VENDOR_MATCH_LEN },
    { OWB_CODEC_LDAC,   kLdacInfo,   sizeof(kLdacInfo),   A2DP_VENDOR_MATCH_LEN },
};

static const codec_config_t* find_codec_config(unsigned long codec_id) {
    for (size_t i = 0u; i < sizeof(kCodecConfigs) / sizeof(kCodecConfigs[0]); ++i) {
        if (kCodecConfigs[i].codec_id == codec_id) return &kCodecConfigs[i];
    }
    return NULL;
}

static int bytes_equal(const unsigned char* a, const unsigned char* b, size_t len) {
    for (size_t i = 0u; i < len; ++i) {
        if (a[i] != b[i]) return 0;
    }
    return 1;
}

static void copy_bytes(unsigned char* dst, const unsigned char* src, size_t len) {
    for (size_t i = 0u; i < len; ++i) dst[i] = src[i];
}

static unsigned char encode_seid(unsigned char seid) {
    return (unsigned char)((seid & AVDTP_SEID_MASK) << AVDTP_SEID_SHIFT);
}

static int is_free_audio_sink(const unsigned char* sep_info) {
    const unsigned char media_type = (unsigned char)(sep_info[1] >> AVDTP_SEP_MEDIA_TYPE_SHIFT);
    if (sep_info[0] & AVDTP_SEP_IN_USE_BIT) return 0;
    if (!(sep_info[1] & AVDTP_SEP_TSEP_BIT)) return 0;
    return media_type == AVDTP_MEDIA_TYPE_AUDIO;
}

unsigned char owb_avdtp_find_free_audio_sink_seid(const unsigned char* data, size_t len) {
    if (!data) return OWB_AVDTP_SEID_NONE;
    for (size_t i = 0u; i + AVDTP_SEP_INFO_LEN <= len; i += AVDTP_SEP_INFO_LEN) {
        if (is_free_audio_sink(data + i))
            return (unsigned char)((data[i] >> AVDTP_SEID_SHIFT) & AVDTP_SEID_MASK);
    }
    return OWB_AVDTP_SEID_NONE;
}

// capability points at the Service Category octet of a complete capability.
static int media_codec_matches(const unsigned char* capability, const codec_config_t* config) {
    const unsigned char  losc = capability[1];
    const unsigned char* media_codec = capability + AVDTP_CAPABILITY_HEADER_LEN;
    if (capability[0] != AVDTP_CATEGORY_MEDIA_CODEC) return 0;
    if (losc < 1u + config->match_len) return 0;
    if ((media_codec[0] >> AVDTP_SEP_MEDIA_TYPE_SHIFT) != AVDTP_MEDIA_TYPE_AUDIO) return 0;
    return bytes_equal(media_codec + 1, config->info, config->match_len);
}

static int capabilities_contain_config(const unsigned char* caps, size_t caps_len,
                                       const codec_config_t* config) {
    size_t i = 0u;
    while (i + AVDTP_CAPABILITY_HEADER_LEN <= caps_len) {
        const size_t capability_len = AVDTP_CAPABILITY_HEADER_LEN + caps[i + 1u];
        if (i + capability_len > caps_len) return 0;
        if (media_codec_matches(caps + i, config)) return 1;
        i += capability_len;
    }
    return 0;
}

int owb_avdtp_capabilities_contain_codec(const unsigned char* caps, size_t caps_len,
                                         unsigned long codec_id) {
    const codec_config_t* config = find_codec_config(codec_id);
    if (!caps || !config) return 0;
    return capabilities_contain_config(caps, caps_len, config);
}

unsigned long owb_avdtp_select_codec(unsigned long preferred_codec_id,
                                     const unsigned char* caps, size_t caps_len) {
    if (owb_avdtp_capabilities_contain_codec(caps, caps_len, preferred_codec_id))
        return preferred_codec_id;
    return OWB_CODEC_SBC;
}

size_t owb_avdtp_build_set_configuration(unsigned long codec_id,
                                         unsigned char acp_seid, unsigned char int_seid,
                                         unsigned char* buf, size_t buf_len) {
    const codec_config_t* config = find_codec_config(codec_id);
    if (!buf || !config) return 0u;

    const size_t losc = 1u + config->info_len;
    const size_t total = 2u + AVDTP_CAPABILITY_HEADER_LEN
                       + AVDTP_CAPABILITY_HEADER_LEN + losc;
    if (buf_len < total) return 0u;

    buf[0] = encode_seid(acp_seid);
    buf[1] = encode_seid(int_seid);
    buf[2] = AVDTP_CATEGORY_MEDIA_TRANSPORT;
    buf[3] = 0x00u;
    buf[4] = AVDTP_CATEGORY_MEDIA_CODEC;
    buf[5] = (unsigned char)losc;
    buf[6] = (unsigned char)(AVDTP_MEDIA_TYPE_AUDIO << AVDTP_SEP_MEDIA_TYPE_SHIFT);
    copy_bytes(buf + 7, config->info, config->info_len);
    return total;
}
