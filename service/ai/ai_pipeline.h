#pragma once
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

namespace owb::ai {

class NoiseReducer;

class AiPipeline {
public:
    AiPipeline();
    ~AiPipeline();

    void process(std::span<int16_t> audio);
    void set_param(std::string_view key, int64_t value);
    bool noise_reduction_enabled() const noexcept { return noise_reduction_; }

private:
    bool noise_reduction_ = false;
    std::unique_ptr<NoiseReducer> reducer_;
};

} // namespace owb::ai
