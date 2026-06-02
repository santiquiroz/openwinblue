// service/codecs/codec_sbc.h
#pragma once
#include "codec_interface.h"
#include <memory>
#include <cstdint>

// Forward-declare the C struct so we don't pull sbc.h into every TU.
struct sbc_struct;

namespace owb {

class CodecSbc final : public ICodec {
public:
    // Parameter name constants — use these with get_param/set_param
    static constexpr int64_t kModeJointStereo = 3;
    static constexpr int64_t kModeDualChannel = 1;
    static constexpr int64_t kModeStereo      = 2;
    static constexpr int64_t kModeMono        = 0;

    CodecSbc();
    ~CodecSbc() override;

    // ICodec
    std::string_view         name()     const noexcept override;
    std::ptrdiff_t           encode(std::span<const int16_t> input,
                                    std::span<uint8_t>       output) override;
    bool                     set_param(CodecParam param)           override;
    std::optional<int64_t>   get_param(std::string_view key) const override;

private:
    // Desired codec configuration. Defined and defaulted in codec_sbc.cpp
    // (where sbc.h is included) so named SBC_* constants can be used.
    struct Config;

    void apply_config();

    // Custom deleter calls sbc_finish() before freeing memory.
    struct SbcDeleter { void operator()(sbc_struct* p) const noexcept; };
    std::unique_ptr<sbc_struct, SbcDeleter> sbc_;
    std::unique_ptr<Config> cfg_;
};

} // namespace owb
