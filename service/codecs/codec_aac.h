#pragma once
#include <memory>
#include "codec_interface.h"

namespace owb {

// AAC codec wrapper.
// Encoding uses Windows Media Foundation (MFT) AAC encoder — no external library.
// Output: one AAC-LC access unit per encode(), wrapped in LATM (AudioMuxElement with
// in-band StreamMuxConfig) as A2DP AAC requires.
// Default: 44100 Hz, 256 kbps.
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
    void start_mf_encoder();
    bool init_mf_encoder();

    struct Impl;
    std::unique_ptr<Impl> impl_;

    int freq_    = 44100;
    int bitrate_ = 256000;
};

} // namespace owb
