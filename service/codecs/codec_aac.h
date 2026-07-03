#pragma once
#include "codec_interface.h"

namespace owb {

// AAC codec wrapper.
// Encoding uses Windows Media Foundation (MFT) AAC encoder — no external library.
// Output: ADTS-framed AAC, compatible with A2DP AAC content protection.
// Default: 44100 Hz, 256 kbps, ADTS output.
class CodecAac final : public ICodec {
public:
    CodecAac();
    ~CodecAac() override;

    std::string_view       name()     const noexcept override;
    std::ptrdiff_t         encode(std::span<const int16_t> input,
                                  std::span<uint8_t>       output) override;
    int                    input_frame_samples() const noexcept override { return 1024 * 2; }
    bool                   set_param(CodecParam param)           override;
    std::optional<int64_t> get_param(std::string_view key) const override;

private:
    bool init_mf_encoder();
    void shutdown_mf_encoder();

    struct Impl;
    Impl* impl_ = nullptr;

    int freq_    = 44100;
    int bitrate_ = 256000;
};

} // namespace owb
