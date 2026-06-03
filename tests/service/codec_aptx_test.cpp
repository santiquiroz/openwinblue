#include <gtest/gtest.h>
#include <vector>
#include "codec_aptx.h"

namespace {
std::vector<int16_t> make_pcm_aptx(int stereo_frames) {
    std::vector<int16_t> buf(static_cast<size_t>(stereo_frames * 2));
    for (int i = 0; i < stereo_frames * 2; ++i)
        buf[i] = static_cast<int16_t>((i % 32) * 1000 - 16000);
    return buf;
}
} // namespace

TEST(CodecAptx, NameIsAptX) {
    owb::CodecAptx codec(false);
    EXPECT_EQ(codec.name(), "aptX");
}

TEST(CodecAptxHd, NameIsAptXHD) {
    owb::CodecAptx codec(true);
    EXPECT_EQ(codec.name(), "aptX-HD");
}

TEST(CodecAptx, EncodeProducesOutput) {
    owb::CodecAptx codec(false);
    auto pcm = make_pcm_aptx(4);  // 4 stereo frames = minimum for aptX
    std::vector<uint8_t> out(16);
    auto n = codec.encode(pcm, out);
    EXPECT_GT(n, 0);
}

TEST(CodecAptx, DefaultParamsAreReasonable) {
    owb::CodecAptx codec(false);
    EXPECT_EQ(codec.get_param("freq"),    44100);
    EXPECT_EQ(codec.get_param("hd"),      0);
    EXPECT_EQ(codec.get_param("unknown"), std::nullopt);
}

TEST(CodecAptxHd, DefaultParamsAreHD) {
    owb::CodecAptx codec(true);
    EXPECT_EQ(codec.get_param("hd"), 1);
}
