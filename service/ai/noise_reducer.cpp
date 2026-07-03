#include "noise_reducer.h"
#include <rnnoise.h>
#include <algorithm>
#include <cmath>

namespace owb::ai {

NoiseReducer::NoiseReducer() : state_(rnnoise_create(nullptr)) {}

NoiseReducer::~NoiseReducer() {
    if (state_) rnnoise_destroy(state_);
}

float NoiseReducer::process(std::span<int16_t> stereo_frame) {
    if (!state_ || stereo_frame.size() < static_cast<size_t>(kFrameSamples * 2))
        return 0.0f;

    // RNNoise operates on mono float samples in the *int16 range* (±32768),
    // NOT normalised to [-1,1]. Downmix to mono and pass the raw magnitude.
    // Note: this is a voice denoiser — intended for the HFP/mic path, not music.
    for (int i = 0; i < kFrameSamples; ++i) {
        const float l = static_cast<float>(stereo_frame[i * 2]);
        const float r = static_cast<float>(stereo_frame[i * 2 + 1]);
        in_buf_[i] = (l + r) * 0.5f;
    }

    float vad = rnnoise_process_frame(state_, out_buf_, in_buf_);

    for (int i = 0; i < kFrameSamples; ++i) {
        const float s = std::clamp(out_buf_[i], -32768.0f, 32767.0f);
        const int16_t sample = static_cast<int16_t>(s);
        stereo_frame[i * 2]     = sample;
        stereo_frame[i * 2 + 1] = sample;
    }

    return vad;
}

} // namespace owb::ai
