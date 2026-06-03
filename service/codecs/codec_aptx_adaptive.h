#pragma once
#include "codec_interface.h"

namespace owb {

// aptX Adaptive codec stub.
// The Qualcomm aptX Adaptive library is proprietary and not publicly available.
// This stub satisfies the ICodec interface so the factory and GUI work correctly.
// encode() returns -1 until a licensed implementation is linked.
// Parameters: freq (44100/48000/96000), bitrate (279000/420000/600000).
class CodecAptxAdaptive final : public ICodec {
public:
    std::string_view       name()     const noexcept override { return "aptX-Adaptive"; }
    std::ptrdiff_t         encode(std::span<const int16_t>, std::span<uint8_t>) override { return -1; }
    bool                   set_param(CodecParam p) override {
        if (p.key == "freq")    { freq_    = static_cast<int>(p.value); return true; }
        if (p.key == "bitrate") { bitrate_ = static_cast<int>(p.value); return true; }
        return false;
    }
    std::optional<int64_t> get_param(std::string_view key) const override {
        if (key == "freq")    return freq_;
        if (key == "bitrate") return bitrate_;
        return std::nullopt;
    }

private:
    int freq_    = 48000;
    int bitrate_ = 420000;
};

} // namespace owb
