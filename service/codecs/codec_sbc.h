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
    // Parameter name constants -- use these with get_param/set_param
    static constexpr int64_t kModeJointStereo  = 3;
    static constexpr int64_t kModeDualChannel  = 1;
    static constexpr int64_t kModeStereo       = 2;
    static constexpr int64_t kModeMono         = 0;

    CodecSbc();
    ~CodecSbc() override;

    // ICodec
    std::string_view         name()     const noexcept override;
    std::ptrdiff_t           encode(std::span<const int16_t> input,
                                    std::span<uint8_t>       output) override;
    bool                     set_param(CodecParam param)          override;
    std::optional<int64_t>   get_param(std::string_view key) const override;

private:
    struct Config {
        uint8_t frequency  = 0x02; // SBC_FREQ_44100
        uint8_t blocks     = 0x03; // SBC_BLK_16
        uint8_t subbands   = 0x01; // SBC_SB_8
        uint8_t mode       = 0x03; // SBC_MODE_JOINT_STEREO
        uint8_t allocation = 0x00; // SBC_AM_LOUDNESS
        uint8_t bitpool    = 53;
        uint8_t endian     = 0x00; // SBC_LE
    };

    void apply_config();

    struct sbc_struct* sbc_;   // opaque C handle -- RAII via ctor/dtor
    Config cfg_;
};

} // namespace owb
