#include "ai_pipeline.h"
#include "noise_reducer.h"
#include "deep_filter.h"

namespace owb::ai {

AiPipeline::AiPipeline()
    : reducer_(std::make_unique<NoiseReducer>())
    , deep_filter_net_(std::make_unique<DeepFilter>())
{}
AiPipeline::~AiPipeline() = default;

void AiPipeline::process(std::span<int16_t> audio) {
    if (noise_reduction_) {
        const int block = NoiseReducer::kFrameSamples * 2;
        size_t offset = 0;
        while (offset + static_cast<size_t>(block) <= audio.size()) {
            reducer_->process(audio.subspan(offset, block));
            offset += block;
        }
    }
    if (deep_filter_ && deep_filter_net_->is_available()) {
        deep_filter_net_->process(audio);
    }
}

void AiPipeline::set_param(std::string_view key, int64_t value) {
    if (key == "noise_reduction") {
        noise_reduction_ = (value != 0);
    } else if (key == "deep_filter") {
        deep_filter_ = (value != 0);
    }
}

} // namespace owb::ai
