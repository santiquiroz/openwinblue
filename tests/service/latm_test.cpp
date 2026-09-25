// tests/service/latm_test.cpp
#include <gtest/gtest.h>
#include <cstdint>
#include <vector>
#include "latm.h"

namespace {

// AAC-LC (AOT 2), stereo, GASpecificConfig all zero.
const std::vector<uint8_t> kAsc44100Stereo = {0x12, 0x10};
const std::vector<uint8_t> kAsc48000Stereo = {0x11, 0x90};

std::vector<uint8_t> counting_bytes(size_t count) {
    std::vector<uint8_t> bytes(count);
    for (size_t i = 0; i < count; ++i) bytes[i] = static_cast<uint8_t>(i + 1);
    return bytes;
}

} // namespace

TEST(Latm, PacksKnownAccessUnit) {
    const auto packed = owb::latm_pack(kAsc44100Stereo, counting_bytes(10));
    const std::vector<uint8_t> expected = {
        0x20, 0x00, 0x12, 0x10, 0x1F, 0xE0, 0x50, 0x08, 0x10,
        0x18, 0x20, 0x28, 0x30, 0x38, 0x40, 0x48, 0x50,
    };
    EXPECT_EQ(packed, expected);
}

TEST(Latm, CarriesAudioSpecificConfigInBand) {
    const std::vector<uint8_t> au = {0xFF};
    const auto packed = owb::latm_pack(kAsc48000Stereo, au);
    const std::vector<uint8_t> expected = {0x20, 0x00, 0x11, 0x90, 0x1F, 0xE0, 0x0F, 0xF8};
    EXPECT_EQ(packed, expected);
}

TEST(Latm, PayloadLengthOver255) {
    const std::vector<uint8_t> au(300, 0xAB);
    const auto packed = owb::latm_pack(kAsc44100Stereo, au);
    // 45 header bits + 0xFF 0x2D length + 300 payload bytes, padded to a byte.
    ASSERT_EQ(packed.size(), 308u);
    const std::vector<uint8_t> prefix(packed.begin(), packed.begin() + 8);
    const std::vector<uint8_t> expected = {0x20, 0x00, 0x12, 0x10, 0x1F, 0xE7, 0xF9, 0x6D};
    EXPECT_EQ(prefix, expected);
    EXPECT_EQ(packed.back(), 0x58);
}

TEST(Latm, PayloadLengthExactly255AddsZeroTerminator) {
    const std::vector<uint8_t> au(255, 0x00);
    const auto packed = owb::latm_pack(kAsc44100Stereo, au);
    ASSERT_EQ(packed.size(), 263u);
    EXPECT_EQ(packed[5], 0xE7);
    EXPECT_EQ(packed[6], 0xF8);
    EXPECT_EQ(packed[7], 0x00);
}
