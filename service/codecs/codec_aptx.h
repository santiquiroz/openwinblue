// service/codecs/codec_aptx.h
#pragma once
#include "codec_interface.h"
#include <memory>

struct aptx_context;

namespace owb {

class CodecAptx final : public ICodec {
public:
    explicit CodecAptx(bool hd = false);
    ~CodecAptx() override;

    std::string_view        name()     const noexcept override;
    std::ptrdiff_t          encode(std::span<const int16_t> input,
                                   std::span<uint8_t>       output) override;
    bool                    set_param(CodecParam param)           override;
    std::optional<int64_t>  get_param(std::string_view key) const override;

private:
    struct aptx_context* ctx_;
    bool  hd_;
    int   freq_ = 44100;
};

} // namespace owb
