// tests/service/stream_pipeline_test.cpp
#include <gtest/gtest.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winioctl.h>

#include "stream_pipeline.h"
#include "iaudio_source.h"
#include "a2dp_stream.h"
#include "owb_ioctl.h"

namespace {

class FixedRateSource final : public owb::IAudioSource {
public:
    explicit FixedRateSource(int rate) : rate_(rate) {}
    bool           start() override { return true; }
    void           stop() override {}
    std::ptrdiff_t read(std::span<int16_t>) override { return 0; }
    int            sample_rate() const noexcept override { return rate_; }
    int            channels() const noexcept override { return 2; }

private:
    int rate_;
};

struct StreamPipelineTest : ::testing::Test {
    FixedRateSource      source{44100};
    owb::A2dpStream      sink;
    owb::StreamPipeline  pipeline{&source, &sink, nullptr};
};

} // namespace

TEST_F(StreamPipelineTest, SetCodecParam_ReachesActiveCodecWithoutRebuild) {
    EXPECT_TRUE(pipeline.set_codec_param("bitpool", 35));
    EXPECT_EQ(pipeline.codec_id(), OWB_CODEC_SBC);
    EXPECT_EQ(pipeline.bitrate(), 228768u);
}

TEST_F(StreamPipelineTest, SetCodecParam_UnknownKeyIsRejected) {
    EXPECT_FALSE(pipeline.set_codec_param("quality", 1));
}

TEST_F(StreamPipelineTest, SetCodecParam_RejectsRateOtherThanCapture) {
    EXPECT_FALSE(pipeline.set_codec_param("freq", 48000));
    EXPECT_TRUE(pipeline.set_codec_param("freq", 44100));
}

TEST_F(StreamPipelineTest, SetCodecParam_RejectsMonoBecausePipelineFeedsStereo) {
    EXPECT_FALSE(pipeline.set_codec_param("mode", 0));
    EXPECT_TRUE(pipeline.set_codec_param("mode", 2));
}

TEST_F(StreamPipelineTest, Bitrate_ComesFromActiveCodec) {
    EXPECT_EQ(pipeline.bitrate(), 327993u);
    pipeline.set_codec_id(OWB_CODEC_APTXHD);
    EXPECT_EQ(pipeline.bitrate(), 529200u);
}
