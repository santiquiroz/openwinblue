// tests/service/a2dp_stream_test.cpp
#include <gtest/gtest.h>
#include <vector>
#include "a2dp_stream.h"

// In these tests the kernel driver is NOT installed.
// A2dpStream must operate in stub mode (is_open()==false, send_frame returns false gracefully).

TEST(A2dpStream, ConstructsWithoutCrash) {
    owb::A2dpStream stream;
    EXPECT_FALSE(stream.is_open());
}

TEST(A2dpStream, OpenFailsGracefullyWithoutDriver) {
    owb::A2dpStream stream;
    bool opened = stream.open();
    EXPECT_FALSE(opened);
    EXPECT_FALSE(stream.is_open());
}

TEST(A2dpStream, SendFrameInStubModeReturnsFalse) {
    owb::A2dpStream stream;
    std::vector<uint8_t> frame(128, 0x9C);
    EXPECT_FALSE(stream.send_frame(0 /*SBC*/, frame));
}

TEST(A2dpStream, CloseIsIdempotent) {
    owb::A2dpStream stream;
    stream.close();
    stream.close();
}

TEST(A2dpStream, GetRfQualityReturnsFalseInStubMode) {
    owb::A2dpStream stream;
    int32_t rssi = 0;
    uint32_t retransmit = 0;
    EXPECT_FALSE(stream.get_rf_quality(&rssi, &retransmit));
}
