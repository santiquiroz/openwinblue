#include "ai_pipeline.h"
#include "noise_reducer.h"
#include "deep_filter.h"
#include "../src/owb_log.h"

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
    OWB_LOG_INFO("AiPipeline::set_param: key=%.*s, value=%lld",
                 (int)key.size(), key.data(), (long long)value);
    
    bool noise_reduction_changed = false;
    bool deep_filter_changed = false;
    
    if (key == "noise_reduction") {
        bool new_val = (value != 0);
        if (new_val != noise_reduction_) {
            noise_reduction_changed = true;
        }
        noise_reduction_ = new_val;
    } else if (key == "deep_filter") {
        bool new_val = (value != 0);
        if (new_val != deep_filter_) {
            deep_filter_changed = true;
        }
        deep_filter_ = new_val;
    }
    
    if (noise_reduction_changed) {
        OWB_LOG_INFO("Noise reduction changed to: %s", noise_reduction_ ? "ON" : "OFF");
    }
    if (deep_filter_changed) {
        OWB_LOG_INFO("Deep filter changed to: %s", deep_filter_ ? "ON" : "OFF");
    }
}

} // namespace owb::ai
