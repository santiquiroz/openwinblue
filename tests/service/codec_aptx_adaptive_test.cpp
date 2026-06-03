// tests/service/codec_aptx_adaptive_test.cpp
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <gtest/gtest.h>
#include "codec_aptx_adaptive.h"

TEST(CodecAptxAdaptive, NameIsAptXAdaptive) {
    owb::CodecAptxAdaptive codec;
    EXPECT_EQ(codec.name(), "aptX-Adaptive");
}

TEST(CodecAptxAdaptive, DefaultFreqIs48000) {
    owb::CodecAptxAdaptive codec;
    EXPECT_EQ(codec.get_param("freq"), 48000);
}

TEST(CodecAptxAdaptive, DefaultBitrateIs420000) {
    owb::CodecAptxAdaptive codec;
    EXPECT_EQ(codec.get_param("bitrate"), 420000);
}

TEST(CodecAptxAdaptive, UnknownParamReturnsNullopt) {
    owb::CodecAptxAdaptive codec;
    EXPECT_EQ(codec.get_param("nonexistent"), std::nullopt);
}

TEST(CodecAptxAdaptive, EncodeReturnsMinusOneStub) {
    owb::CodecAptxAdaptive codec;
    std::vector<int16_t> pcm(2048, 0);
    std::vector<uint8_t> out(1024);
    EXPECT_EQ(codec.encode(pcm, out), -1) << "stub returns -1 (no proprietary lib)";
}
