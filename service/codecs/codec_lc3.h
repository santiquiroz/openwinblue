#pragma once
#include "codec_interface.h"
#include <cstdlib>

struct lc3_encoder;

namespace owb {

class CodecLc3 final : public ICodec {
public:
    CodecLc3();
    ~CodecLc3() override;

    std::string_view       name()     const noexcept override;
    std::ptrdiff_t         encode(std::span<const int16_t> input,
                                  std::span<uint8_t>       output) override;
    bool                   set_param(CodecParam param)           override;
    std::optional<int64_t> get_param(std::string_view key) const override;

private:
    void reinit();

    struct lc3_encoder* enc_     = nullptr;
    void*               mem_     = nullptr;
    int                 freq_    = 48000;
    int                 bitrate_ = 80000;
    static constexpr int kDtUs   = 10000; // 10ms frame
};

} // namespace owb
