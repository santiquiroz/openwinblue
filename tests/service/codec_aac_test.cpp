// tests/service/codec_aac_test.cpp
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <objbase.h>
#include <crtdbg.h>
#include <gtest/gtest.h>
#include <cmath>
#include <cstdint>
#include <span>
#include <vector>
#include "codec_aac.h"

namespace {

constexpr int kFrameSamples   = 1024 * 2;
constexpr int kMaxEncodeTries = 8;
constexpr size_t kLatmHeaderBits = 45;  // useSameStreamMux .. crcCheckPresent

class ComScope {
public:
    ComScope() : hr_(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {}
    ~ComScope() { if (SUCCEEDED(hr_)) CoUninitialize(); }
private:
    HRESULT hr_;
};

std::vector<int16_t> sine_frame(int freq) {
    std::vector<int16_t> pcm(kFrameSamples);
    for (int i = 0; i < kFrameSamples / 2; ++i) {
        const double t = static_cast<double>(i) / freq;
        const auto v = static_cast<int16_t>(8000.0 * std::sin(2.0 * 3.14159265358979 * 1000.0 * t));
        pcm[2 * i] = v;
        pcm[2 * i + 1] = v;
    }
    return pcm;
}

std::vector<uint8_t> first_encoded_packet(owb::CodecAac& codec, int freq) {
    const auto pcm = sine_frame(freq);
    std::vector<uint8_t> out(4096);
    for (int i = 0; i < kMaxEncodeTries; ++i) {
        const auto n = codec.encode(pcm, out);
        if (n > 0) return {out.begin(), out.begin() + n};
    }
    return {};
}

uint32_t read_bits(std::span<const uint8_t> bytes, size_t bit_offset, int count) {
    uint32_t value = 0;
    for (int i = 0; i < count; ++i, ++bit_offset) {
        const uint32_t bit = (bytes[bit_offset / 8] >> (7 - bit_offset % 8)) & 1u;
        value = (value << 1) | bit;
    }
    return value;
}

struct PayloadLength {
    size_t bytes;
    size_t header_bits;
};

PayloadLength read_payload_length(std::span<const uint8_t> packet) {
    PayloadLength len{0, kLatmHeaderBits};
    uint32_t chunk = 255;
    while (chunk == 255) {
        chunk = read_bits(packet, len.header_bits, 8);
        len.bytes += chunk;
        len.header_bits += 8;
    }
    return len;
}

} // namespace

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

TEST(CodecAac, OutputIsLatm) {
    ComScope com;
    owb::CodecAac codec;
    if (!codec.encoder_ready()) GTEST_SKIP() << "MF AAC encoder unavailable";
    const auto packet = first_encoded_packet(codec, 44100);
    ASSERT_FALSE(packet.empty()) << "MF AAC encoder produced no output";
    ASSERT_GE(packet.size(), 7u);

    EXPECT_EQ(packet[0] & 0x80, 0) << "useSameStreamMux must be 0 (config in-band)";
    EXPECT_EQ(packet[0], 0x20);
    EXPECT_EQ(packet[1], 0x00);
    EXPECT_EQ(packet[2], 0x12) << "AudioSpecificConfig: AAC-LC 44.1 kHz stereo";
    EXPECT_EQ(packet[3], 0x10);

    const auto len = read_payload_length(packet);
    EXPECT_GT(len.bytes, 0u);
    EXPECT_EQ(packet.size(), (len.header_bits + 8 * len.bytes + 7) / 8);
}

TEST(CodecAac, OutputCarriesNewConfigAfterFreqChange) {
    ComScope com;
    owb::CodecAac codec;
    if (!codec.encoder_ready()) GTEST_SKIP() << "MF AAC encoder unavailable";
    ASSERT_TRUE(codec.set_param({"freq", 48000}));
    const auto packet = first_encoded_packet(codec, 48000);
    ASSERT_FALSE(packet.empty()) << "MF AAC encoder produced no output";
    ASSERT_GE(packet.size(), 4u);
    EXPECT_EQ(packet[2], 0x11) << "AudioSpecificConfig: AAC-LC 48 kHz stereo";
    EXPECT_EQ(packet[3], 0x90);
}

TEST(CodecAac, SetFreqTwiceNoLeak) {
    ComScope com;
    { owb::CodecAac warmup; }
#ifdef _DEBUG
    _CrtMemState before{};
    _CrtMemState after{};
    _CrtMemState diff{};
    _CrtMemCheckpoint(&before);
#endif
    {
        owb::CodecAac codec;
        EXPECT_TRUE(codec.set_param({"freq", 48000}));
        EXPECT_TRUE(codec.set_param({"freq", 44100}));
    }
#ifdef _DEBUG
    _CrtMemCheckpoint(&after);
    EXPECT_EQ(_CrtMemDifference(&diff, &before, &after), 0) << "CodecAac leaked CRT heap blocks";
#endif
}
