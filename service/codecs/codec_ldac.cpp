// service/codecs/codec_ldac.cpp
#include "codec_ldac.h"
#include <ldacBT.h>

namespace owb {

CodecLdac::CodecLdac() {
    handle_ = ldacBT_get_handle();
    reinit();
}

CodecLdac::~CodecLdac() {
    if (handle_) {
        ldacBT_close_handle(handle_);
        ldacBT_free_handle(handle_);
    }
}

void CodecLdac::reinit() {
    if (!handle_) return;
    ldacBT_close_handle(handle_);
    ldacBT_init_handle_encode(
        handle_, kMtu, quality_,
        LDACBT_CHANNEL_MODE_STEREO,
        LDACBT_SMPL_FMT_S16, freq_
    );
}

std::string_view CodecLdac::name() const noexcept { return "LDAC"; }

// LDAC encodes one LSU of 128 samples per channel per call → 256 interleaved.
int CodecLdac::input_frame_samples() const noexcept { return LDACBT_ENC_LSU * 2; }

std::ptrdiff_t CodecLdac::encode(std::span<const int16_t> input,
                                  std::span<uint8_t>       output) {
    if (!handle_ || output.empty() || input.empty()) return -1;

    int pcm_used    = 0;
    int stream_used = 0;
    int frame_num   = 0;

    // ldacBT_encode takes void* for PCM — cast away const (library does not modify it)
    void* pcm_ptr = const_cast<int16_t*>(input.data());
    int result = ldacBT_encode(
        handle_,
        pcm_ptr, &pcm_used,
        output.data(), &stream_used,
        &frame_num
    );
    if (result != 0) return -1;           // encoder error
    if (stream_used <= 0) return 0;       // still buffering — not an error
    return static_cast<std::ptrdiff_t>(stream_used);
}

bool CodecLdac::set_param(CodecParam p) {
    if (p.key == "quality") {
        quality_ = static_cast<int>(p.value);
        ldacBT_set_eqmid(handle_, quality_);
        return true;
    }
    if (p.key == "freq") {
        freq_ = static_cast<int>(p.value);
        reinit();
        return true;
    }
    return false;
}

std::optional<int64_t> CodecLdac::get_param(std::string_view key) const {
    if (key == "quality") return quality_;
    if (key == "freq")    return freq_;
    if (key == "bitrate") return ldacBT_get_bitrate(handle_);
    return std::nullopt;
}

} // namespace owb
