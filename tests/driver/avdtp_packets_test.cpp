// tests/driver/avdtp_packets_test.cpp
// Host-side tests for the pure AVDTP packet builders/parsers used by the driver.
#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <vector>

#include "avdtp_packets.h"
#include "owb_codec_ids.h"

namespace {

using Bytes = std::vector<unsigned char>;

constexpr unsigned char kAcpSeid = 0x01;
constexpr unsigned char kIntSeid = 0x01;

unsigned find_sink(const Bytes& discover) {
    return owb_avdtp_find_free_audio_sink_seid(discover.data(), discover.size());
}

Bytes build_set_config(unsigned long codec_id) {
    std::array<unsigned char, OWB_AVDTP_SET_CONFIGURATION_MAX_LEN> buf{};
    const size_t len = owb_avdtp_build_set_configuration(
        codec_id, kAcpSeid, kIntSeid, buf.data(), buf.size());
    return Bytes(buf.begin(), buf.begin() + static_cast<std::ptrdiff_t>(len));
}

bool caps_contain(const Bytes& caps, unsigned long codec_id) {
    return owb_avdtp_capabilities_contain_codec(caps.data(), caps.size(), codec_id) != 0;
}

unsigned long select_codec(unsigned long preferred, const Bytes& caps) {
    return owb_avdtp_select_codec(preferred, caps.data(), caps.size());
}

// Typical GET_CAPABILITIES response: Media Transport (LOSC 0) comes first.
const Bytes kCapsSbcOnly = {0x01, 0x00, 0x07, 0x06, 0x00, 0x00, 0xFF, 0xFF, 0x02, 0x35};
const Bytes kCapsAptx = {0x01, 0x00, 0x07, 0x09, 0x00, 0xFF, 0x4F, 0x00,
                         0x00, 0x00, 0x01, 0x00, 0x32};
const Bytes kCapsAptxHd = {0x01, 0x00, 0x07, 0x0D, 0x00, 0xFF, 0xD7, 0x00, 0x00,
                           0x00, 0x24, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00};
const Bytes kCapsLdac = {0x01, 0x00, 0x07, 0x0A, 0x00, 0xFF, 0x2D, 0x01,
                         0x00, 0x00, 0xAA, 0x00, 0x3C, 0x07};
const Bytes kCapsAac = {0x01, 0x00, 0x07, 0x08, 0x00, 0x02, 0x80,
                        0x01, 0x8C, 0x83, 0xE8, 0x00};

}  // namespace

TEST(AvdtpDiscover, FindsFreeAudioSink) {
    EXPECT_EQ(find_sink({0x04, 0x08}), 1u);
}

TEST(AvdtpDiscover, IgnoresSinkInUse) {
    EXPECT_EQ(find_sink({0x06, 0x08}), OWB_AVDTP_SEID_NONE);
}

TEST(AvdtpDiscover, IgnoresSource) {
    EXPECT_EQ(find_sink({0x04, 0x00}), OWB_AVDTP_SEID_NONE);
}

TEST(AvdtpDiscover, IgnoresNonAudioSink) {
    // Media type 1 (video) in the high nibble of the second octet.
    EXPECT_EQ(find_sink({0x04, 0x18}), OWB_AVDTP_SEID_NONE);
}

TEST(AvdtpDiscover, SkipsBusyAndSourceEndpointsToFindSink) {
    EXPECT_EQ(find_sink({0x04, 0x00, 0x0A, 0x08, 0x0C, 0x08}), 3u);
}

TEST(AvdtpDiscover, IgnoresTrailingOddByte) {
    EXPECT_EQ(find_sink({0x04, 0x00, 0x08}), OWB_AVDTP_SEID_NONE);
}

TEST(AvdtpDiscover, HandlesEmptyAndNullInput) {
    EXPECT_EQ(find_sink({}), OWB_AVDTP_SEID_NONE);
    EXPECT_EQ(unsigned{owb_avdtp_find_free_audio_sink_seid(nullptr, 2)}, OWB_AVDTP_SEID_NONE);
}

TEST(AvdtpSetConfiguration, SbcBytes) {
    const Bytes expected = {0x04, 0x04, 0x01, 0x00, 0x07, 0x06,
                            0x00, 0x00, 0x21, 0x15, 0x02, 0x35};
    EXPECT_EQ(build_set_config(OWB_CODEC_SBC), expected);
}

TEST(AvdtpSetConfiguration, AacBytes) {
    const Bytes expected = {0x04, 0x04, 0x01, 0x00, 0x07, 0x08, 0x00,
                            0x02, 0x80, 0x01, 0x04, 0x03, 0xE8, 0x00};
    EXPECT_EQ(build_set_config(OWB_CODEC_AAC), expected);
}

TEST(AvdtpSetConfiguration, AptxBytes) {
    const Bytes expected = {0x04, 0x04, 0x01, 0x00, 0x07, 0x09, 0x00, 0xFF,
                            0x4F, 0x00, 0x00, 0x00, 0x01, 0x00, 0x22};
    EXPECT_EQ(build_set_config(OWB_CODEC_APTX), expected);
}

TEST(AvdtpSetConfiguration, AptxHdBytesIncludeFourReservedOctets) {
    const Bytes expected = {0x04, 0x04, 0x01, 0x00, 0x07, 0x0D, 0x00, 0xFF, 0xD7, 0x00,
                            0x00, 0x00, 0x24, 0x00, 0x22, 0x00, 0x00, 0x00, 0x00};
    EXPECT_EQ(build_set_config(OWB_CODEC_APTXHD), expected);
}

TEST(AvdtpSetConfiguration, LdacBytesIncludeFrequencyAndChannelMode) {
    const Bytes expected = {0x04, 0x04, 0x01, 0x00, 0x07, 0x0A, 0x00, 0xFF,
                            0x2D, 0x01, 0x00, 0x00, 0xAA, 0x00, 0x20, 0x01};
    EXPECT_EQ(build_set_config(OWB_CODEC_LDAC), expected);
}

TEST(AvdtpSetConfiguration, EncodesSeidsInUpperSixBits) {
    std::array<unsigned char, OWB_AVDTP_SET_CONFIGURATION_MAX_LEN> buf{};
    const size_t len = owb_avdtp_build_set_configuration(
        OWB_CODEC_SBC, 0x3E, 0x02, buf.data(), buf.size());
    ASSERT_GE(len, 2u);
    EXPECT_EQ(buf[0], 0xF8);
    EXPECT_EQ(buf[1], 0x08);
}

TEST(AvdtpSetConfiguration, LoscMatchesBytesWrittenForEveryCodec) {
    for (unsigned long codec : {OWB_CODEC_SBC, OWB_CODEC_AAC, OWB_CODEC_APTX,
                                OWB_CODEC_APTXHD, OWB_CODEC_LDAC}) {
        const Bytes pkt = build_set_config(codec);
        ASSERT_GE(pkt.size(), 6u) << "codec " << codec;
        EXPECT_EQ(pkt[2], 0x01) << "Media Transport category, codec " << codec;
        EXPECT_EQ(pkt[3], 0x00) << "Media Transport LOSC, codec " << codec;
        EXPECT_EQ(pkt[4], 0x07) << "Media Codec category, codec " << codec;
        EXPECT_EQ(static_cast<size_t>(pkt[5]) + 6u, pkt.size()) << "codec " << codec;
    }
}

TEST(AvdtpSetConfiguration, RejectsBufferTooSmall) {
    std::array<unsigned char, 14> buf{};
    EXPECT_EQ(owb_avdtp_build_set_configuration(OWB_CODEC_APTX, kAcpSeid, kIntSeid,
                                                buf.data(), buf.size()),
              0u);
    EXPECT_EQ(owb_avdtp_build_set_configuration(OWB_CODEC_APTX, kAcpSeid, kIntSeid,
                                                nullptr, 64),
              0u);
}

TEST(AvdtpSetConfiguration, RejectsCodecsWithoutA2dpConfiguration) {
    EXPECT_TRUE(build_set_config(OWB_CODEC_LC3).empty());
    EXPECT_TRUE(build_set_config(OWB_CODEC_APTX_ADAPTIVE).empty());
}

TEST(AvdtpCapabilities, FindsCodecAfterMediaTransport) {
    EXPECT_TRUE(caps_contain(kCapsSbcOnly, OWB_CODEC_SBC));
    EXPECT_TRUE(caps_contain(kCapsAptx, OWB_CODEC_APTX));
    EXPECT_TRUE(caps_contain(kCapsAptxHd, OWB_CODEC_APTXHD));
    EXPECT_TRUE(caps_contain(kCapsLdac, OWB_CODEC_LDAC));
    EXPECT_TRUE(caps_contain(kCapsAac, OWB_CODEC_AAC));
}

TEST(AvdtpCapabilities, DistinguishesVendorCodecs) {
    EXPECT_FALSE(caps_contain(kCapsAptx, OWB_CODEC_APTXHD));
    EXPECT_FALSE(caps_contain(kCapsAptx, OWB_CODEC_LDAC));
    EXPECT_FALSE(caps_contain(kCapsAptx, OWB_CODEC_SBC));
    EXPECT_FALSE(caps_contain(kCapsSbcOnly, OWB_CODEC_AAC));
}

TEST(AvdtpCapabilities, IgnoresTruncatedCapability) {
    const Bytes truncated(kCapsAptx.begin(), kCapsAptx.end() - 2);
    EXPECT_FALSE(caps_contain(truncated, OWB_CODEC_APTX));
    EXPECT_FALSE(caps_contain({}, OWB_CODEC_SBC));
}

TEST(AvdtpSelectCodec, UsesPreferredWhenSinkSupportsIt) {
    EXPECT_EQ(select_codec(OWB_CODEC_LDAC, kCapsLdac), OWB_CODEC_LDAC);
    EXPECT_EQ(select_codec(OWB_CODEC_APTX, kCapsAptx), OWB_CODEC_APTX);
    EXPECT_EQ(select_codec(OWB_CODEC_APTXHD, kCapsAptxHd), OWB_CODEC_APTXHD);
    EXPECT_EQ(select_codec(OWB_CODEC_AAC, kCapsAac), OWB_CODEC_AAC);
}

TEST(AvdtpSelectCodec, FallsBackToSbcWhenPreferredIsMissing) {
    EXPECT_EQ(select_codec(OWB_CODEC_LDAC, kCapsSbcOnly), OWB_CODEC_SBC);
    EXPECT_EQ(select_codec(OWB_CODEC_APTXHD, kCapsAptx), OWB_CODEC_SBC);
}

TEST(AvdtpSelectCodec, FallsBackToSbcForCodecsWithoutA2dpConfiguration) {
    EXPECT_EQ(select_codec(OWB_CODEC_LC3, kCapsSbcOnly), OWB_CODEC_SBC);
    EXPECT_EQ(select_codec(OWB_CODEC_APTX_ADAPTIVE, kCapsAptx), OWB_CODEC_SBC);
}
