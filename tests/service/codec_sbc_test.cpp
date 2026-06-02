// tests/service/codec_sbc_test.cpp
#include <gtest/gtest.h>
#include <vector>
#include <cstdint>
#include "codec_sbc.h"

namespace {

// Generate repeating PCM pattern (not silence, so encoder produces real output)
std::vector<int16_t> make_pcm(int frames, int channels = 2) {
    std::vector<int16_t> buf(static_cast<size_t>(frames * channels));
    for (int i = 0; i < frames * channels; ++i)
        buf[i] = static_cast<int16_t>((i % 64) * 512 - 16384);
    return buf;
}

} // namespace

TEST(CodecSbc, NameIsSBC) {
    owb::CodecSbc codec;
    EXPECT_EQ(codec.name(), "SBC");
}

TEST(CodecSbc, DefaultParamsAreReasonable) {
    owb::CodecSbc codec;
    EXPECT_EQ(codec.get_param("bitpool"), 53);
    EXPECT_EQ(codec.get_param("freq"),    44100);
    EXPECT_EQ(codec.get_param("mode"),    owb::CodecSbc::kModeJointStereo);
}

TEST(CodecSbc, UnknownParamReturnsNullopt) {
    owb::CodecSbc codec;
    EXPECT_EQ(codec.get_param("nonexistent"), std::nullopt);
}

TEST(CodecSbc, SetBitpoolAffectsOutput) {
    owb::CodecSbc codec_lo, codec_hi;
    codec_lo.set_param({"bitpool", 20});
    codec_hi.set_param({"bitpool", 53});

    auto pcm = make_pcm(512);
    std::vector<uint8_t> out_lo(512), out_hi(512);

    auto n_lo = codec_lo.encode(pcm, out_lo);
    auto n_hi = codec_hi.encode(pcm, out_hi);

    EXPECT_GT(n_lo, 0);
    EXPECT_GT(n_hi, 0);
    EXPECT_GT(n_hi, n_lo);  // higher bitpool -> bigger frame
}

TEST(CodecSbc, EncodeProducesValidSBCFrame) {
    owb::CodecSbc codec;
    auto pcm = make_pcm(512);
    std::vector<uint8_t> out(2048);
    auto n = codec.encode(pcm, out);
    ASSERT_GT(n, 0);
    EXPECT_EQ(out[0], 0x9C) << "First byte must be SBC sync word";
}

TEST(CodecSbc, EncodeReturnsMinusOneOnTinyOutputBuffer) {
    owb::CodecSbc codec;
    auto pcm = make_pcm(512);
    std::vector<uint8_t> out(1);  // way too small
    EXPECT_EQ(codec.encode(pcm, out), -1);
}
