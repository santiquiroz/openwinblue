#pragma once
#include <cstdint>
#include <span>

struct DenoiseState;

namespace owb::ai {

class NoiseReducer {
public:
    NoiseReducer();
    ~NoiseReducer();

    float process(std::span<int16_t> stereo_frame);

    static constexpr int kFrameSamples = 480;

private:
    DenoiseState* state_;
    float   in_buf_[kFrameSamples];
    float   out_buf_[kFrameSamples];
};

} // namespace owb::ai
