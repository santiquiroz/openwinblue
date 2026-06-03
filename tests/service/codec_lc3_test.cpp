// tests/service/codec_lc3_test.cpp
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <gtest/gtest.h>
#include <vector>
#include "codec_lc3.h"

namespace {
std::vector<int16_t> make_pcm_lc3(int stereo_frames) {
    std::vector<int16_t> buf(static_cast<size_t>(stereo_frames * 2));
    for (int i = 0; i < stereo_frames * 2; ++i)
        buf[i] = static_cast<int16_t>((i % 64) * 512 - 16384);
    return buf;
}
} // namespace

TEST(CodecLc3, NameIsLC3) {
    owb::CodecLc3 codec;
    EXPECT_EQ(codec.name(), "LC3");
}

TEST(CodecLc3, DefaultFreqIs48000) {
    owb::CodecLc3 codec;
    EXPECT_EQ(codec.get_param("freq"), 48000);
}

TEST(CodecLc3, DefaultBitrateIs80000) {
    owb::CodecLc3 codec;
    EXPECT_EQ(codec.get_param("bitrate"), 80000);
}

TEST(CodecLc3, UnknownParamReturnsNullopt) {
    owb::CodecLc3 codec;
    EXPECT_EQ(codec.get_param("nonexistent"), std::nullopt);
}

TEST(CodecLc3, EncodeProducesOutput) {
    owb::CodecLc3 codec;
    // LC3 frame = 480 stereo samples at 48kHz/10ms
    auto pcm = make_pcm_lc3(480);
    std::vector<uint8_t> out(512);
    auto n = codec.encode(pcm, out);
    EXPECT_GT(n, 0) << "LC3 encode should produce output";
}
