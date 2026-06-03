// service/codecs/codec_lc3.cpp
#include "codec_lc3.h"
#include <lc3.h>
#include <cstdlib>

namespace owb {

CodecLc3::CodecLc3() { reinit(); }

CodecLc3::~CodecLc3() { std::free(mem_); }

void CodecLc3::reinit() {
    std::free(mem_);
    unsigned sz = lc3_encoder_size(kDtUs, freq_);
    mem_ = std::malloc(sz);
    enc_ = lc3_setup_encoder(kDtUs, freq_, 0, mem_);
}

std::string_view CodecLc3::name() const noexcept { return "LC3"; }

std::ptrdiff_t CodecLc3::encode(std::span<const int16_t> input,
                                 std::span<uint8_t>       output) {
    if (!enc_ || output.empty() || input.empty()) return -1;

    const int frame_samples = lc3_frame_samples(kDtUs, freq_);
    const int frame_bytes   = lc3_frame_bytes(kDtUs, bitrate_);

    if (frame_bytes <= 0 || static_cast<int>(output.size()) < frame_bytes) return -1;
    if (static_cast<int>(input.size()) < frame_samples * 2) return -1;

    int err = lc3_encode(enc_, LC3_PCM_FORMAT_S16,
                          input.data(), 2 /*stride=channels*/,
                          frame_bytes, output.data());
    if (err != 0) return -1;
    return static_cast<std::ptrdiff_t>(frame_bytes);
}

bool CodecLc3::set_param(CodecParam p) {
    if (p.key == "freq") {
        freq_ = static_cast<int>(p.value);
        reinit();
        return true;
    }
    if (p.key == "bitrate") {
        bitrate_ = static_cast<int>(p.value);
        return true;
    }
    return false;
}

std::optional<int64_t> CodecLc3::get_param(std::string_view key) const {
    if (key == "freq")    return freq_;
    if (key == "bitrate") return bitrate_;
    return std::nullopt;
}

} // namespace owb
