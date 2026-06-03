#pragma once
#include <cstdint>
#include <span>

// DeepFilterNet3 noise suppressor via ONNX Runtime + DirectML.
// Compiled only when OWB_HAVE_ONNXRUNTIME is defined (CMake detects the SDK).
// Without ONNX Runtime: process() is a no-op passthrough (stub mode).
//
// Frame size: 480 samples (10 ms at 48 kHz) — matches DeepFilterNet3 input.

namespace owb::ai {

class DeepFilter {
public:
    DeepFilter();
    ~DeepFilter();

    // Returns true if the ONNX Runtime session loaded successfully.
    bool is_available() const noexcept;

    // In-place: filters 'n' stereo int16 frames (n*2 samples).
    // No-op if not available.
    void process(std::span<int16_t> stereo_pcm) noexcept;

private:
    struct Impl;
    Impl* impl_;
};

} // namespace owb::ai
