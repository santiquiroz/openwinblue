#include "ai_pipeline.h"
#include "noise_reducer.h"

namespace owb::ai {

AiPipeline::AiPipeline() : reducer_(std::make_unique<NoiseReducer>()) {}
AiPipeline::~AiPipeline() = default;

void AiPipeline::process(std::span<int16_t> audio) {
    if (!noise_reduction_) return;

    const int block = NoiseReducer::kFrameSamples * 2;
    size_t offset = 0;
    while (offset + static_cast<size_t>(block) <= audio.size()) {
        reducer_->process(audio.subspan(offset, block));
        offset += block;
    }
}

void AiPipeline::set_param(std::string_view key, int64_t value) {
    if (key == "noise_reduction") {
        noise_reduction_ = (value != 0);
    }
}

} // namespace owb::ai
