#pragma once
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

namespace owb::ai {

class NoiseReducer;
class DeepFilter;

class AiPipeline {
public:
    AiPipeline();
    ~AiPipeline();

    void process(std::span<int16_t> audio);
    void set_param(std::string_view key, int64_t value);
    bool noise_reduction_enabled() const noexcept { return noise_reduction_; }
    bool deep_filter_enabled()     const noexcept { return deep_filter_; }

private:
    bool noise_reduction_ = false;
    bool deep_filter_     = false;
    std::unique_ptr<NoiseReducer> reducer_;
    std::unique_ptr<DeepFilter>   deep_filter_net_;
};

} // namespace owb::ai
