// tests/service/codec_aac_test.cpp
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <gtest/gtest.h>
#include <vector>
#include "codec_aac.h"

TEST(CodecAac, NameIsAAC) {
    owb::CodecAac codec;
    EXPECT_EQ(codec.name(), "AAC");
}

TEST(CodecAac, DefaultFreqIs44100) {
    owb::CodecAac codec;
    EXPECT_EQ(codec.get_param("freq"), 44100);
}

TEST(CodecAac, DefaultBitrateIs256000) {
    owb::CodecAac codec;
    EXPECT_EQ(codec.get_param("bitrate"), 256000);
}

TEST(CodecAac, UnknownParamReturnsNullopt) {
    owb::CodecAac codec;
    EXPECT_EQ(codec.get_param("nonexistent"), std::nullopt);
}

TEST(CodecAac, SetFreqAccepted) {
    owb::CodecAac codec;
    EXPECT_TRUE(codec.set_param({"freq", 48000}));
    EXPECT_EQ(codec.get_param("freq"), 48000);
}

TEST(CodecAac, EncodeDoesNotCrash) {
    owb::CodecAac codec;
    // 1024 stereo samples = one AAC frame
    std::vector<int16_t> pcm(1024 * 2, 0);
    std::vector<uint8_t> out(4096);
    // Result may be -1 if MF not available in test env; must not crash
    [[maybe_unused]] auto n = codec.encode(pcm, out);
    SUCCEED();
}
