#include <gtest/gtest.h>
#include <vector>
#include "ai_pipeline.h"

TEST(AiPipeline, ConstructsDisabled) {
    owb::ai::AiPipeline pipeline;
    EXPECT_FALSE(pipeline.noise_reduction_enabled());
}

TEST(AiPipeline, EnableNoiseReduction) {
    owb::ai::AiPipeline pipeline;
    pipeline.set_param("noise_reduction", 1);
    EXPECT_TRUE(pipeline.noise_reduction_enabled());
    pipeline.set_param("noise_reduction", 0);
    EXPECT_FALSE(pipeline.noise_reduction_enabled());
}

TEST(AiPipeline, ProcessPassthroughWhenDisabled) {
    owb::ai::AiPipeline pipeline;
    std::vector<int16_t> audio(960, 1000);
    pipeline.process(audio);
    EXPECT_EQ(audio[0], 1000);
}

TEST(AiPipeline, ProcessWhenEnabledDoesNotCrash) {
    owb::ai::AiPipeline pipeline;
    pipeline.set_param("noise_reduction", 1);
    std::vector<int16_t> audio(960);
    for (int i = 0; i < 960; ++i) audio[i] = static_cast<int16_t>((i % 64) * 500 - 16000);
    pipeline.process(audio);
    SUCCEED();
}
