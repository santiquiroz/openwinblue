// service/codecs/codec_sbc.cpp
#include "codec_sbc.h"
#include <sbc/sbc.h>

namespace owb {

// ── Config ─────────────────────────────────────────────────────────────────
// Defined here (not in the header) so we can use SBC_* named constants.
struct CodecSbc::Config {
    uint8_t frequency  = SBC_FREQ_44100;
    uint8_t blocks     = SBC_BLK_16;
    uint8_t subbands   = SBC_SB_8;
    uint8_t mode       = SBC_MODE_JOINT_STEREO;
    uint8_t allocation = SBC_AM_LOUDNESS;
    uint8_t bitpool    = 53;
    uint8_t endian     = SBC_LE;
};

// ── SbcDeleter ─────────────────────────────────────────────────────────────
void CodecSbc::SbcDeleter::operator()(sbc_struct* p) const noexcept {
    sbc_finish(p);
    delete p;
}

// ── Helpers ─────────────────────────────────────────────────────────────────
namespace {
uint8_t freq_to_sbc(int64_t hz) {
    switch (hz) {
        case 16000: return SBC_FREQ_16000;
        case 32000: return SBC_FREQ_32000;
        case 44100: return SBC_FREQ_44100;
        case 48000: return SBC_FREQ_48000;
        default:    return SBC_FREQ_44100;
    }
}
int64_t sbc_to_freq(uint8_t f) {
    switch (f) {
        case SBC_FREQ_16000: return 16000;
        case SBC_FREQ_32000: return 32000;
        case SBC_FREQ_44100: return 44100;
        case SBC_FREQ_48000: return 48000;
        default:             return 44100;
    }
}
} // namespace

// ── Implementation ───────────────────────────────────────────────────────────
void CodecSbc::apply_config() {
    sbc_->frequency  = cfg_->frequency;
    sbc_->blocks     = cfg_->blocks;
    sbc_->subbands   = cfg_->subbands;
    sbc_->mode       = cfg_->mode;
    sbc_->allocation = cfg_->allocation;
    sbc_->bitpool    = cfg_->bitpool;
    sbc_->endian     = cfg_->endian;
}

CodecSbc::CodecSbc()
    : sbc_(new sbc_struct{})
    , cfg_(std::make_unique<Config>())
{
    sbc_init(sbc_.get(), 0);
    apply_config();
}

// Destructor defined here because SbcDeleter requires complete sbc_struct.
CodecSbc::~CodecSbc() = default;

std::string_view CodecSbc::name() const noexcept { return "SBC"; }

// One SBC frame at a time: codesize bytes of PCM → one media packet (frame count 1).
int CodecSbc::input_frame_samples() const noexcept {
    const size_t codesize = sbc_get_codesize(sbc_.get());
    return static_cast<int>(codesize / sizeof(int16_t));
}

std::ptrdiff_t CodecSbc::encode(std::span<const int16_t> input,
                                 std::span<uint8_t>       output) {
    if (output.empty()) return -1;

    const size_t codesize = sbc_get_codesize(sbc_.get());
    if (codesize == 0 || input.size_bytes() < codesize) return -1;

    std::ptrdiff_t total_written = 0;
    size_t in_offset  = 0;
    size_t out_offset = 0;

    while (in_offset + codesize <= input.size_bytes()) {
        const size_t out_remaining = output.size() - out_offset;
        if (out_remaining == 0) break;

        ssize_t frame_written = 0;
        ssize_t consumed = sbc_encode(
            sbc_.get(),
            reinterpret_cast<const uint8_t*>(input.data()) + in_offset,
            input.size_bytes() - in_offset,
            output.data() + out_offset,
            out_remaining,
            &frame_written
        );

        if (consumed < 0 || frame_written <= 0) {
            if (total_written == 0) return -1;
            break;
        }

        in_offset    += static_cast<size_t>(consumed);
        out_offset   += static_cast<size_t>(frame_written);
        total_written += frame_written;
    }

    return total_written;
}

bool CodecSbc::set_param(CodecParam p) {
    if (p.key == "bitpool") {
        cfg_->bitpool = static_cast<uint8_t>(p.value);
        // bitpool-only changes are picked up by sbc_encode without reinit
        sbc_->bitpool = cfg_->bitpool;
        return true;
    }
    if (p.key == "freq") {
        cfg_->frequency = freq_to_sbc(p.value);
        sbc_reinit(sbc_.get(), 0);
        apply_config();
        return true;
    }
    if (p.key == "mode") {
        cfg_->mode = static_cast<uint8_t>(p.value);
        sbc_reinit(sbc_.get(), 0);
        apply_config();
        return true;
    }
    if (p.key == "subbands") {
        cfg_->subbands = (p.value == 4) ? SBC_SB_4 : SBC_SB_8;
        sbc_reinit(sbc_.get(), 0);
        apply_config();
        return true;
    }
    if (p.key == "blocks") {
        switch (p.value) {
            case 4:  cfg_->blocks = SBC_BLK_4;  break;
            case 8:  cfg_->blocks = SBC_BLK_8;  break;
            case 12: cfg_->blocks = SBC_BLK_12; break;
            case 16: cfg_->blocks = SBC_BLK_16; break;
            default: return false;
        }
        sbc_reinit(sbc_.get(), 0);
        apply_config();
        return true;
    }
    if (p.key == "alloc") {
        cfg_->allocation = (p.value == 1) ? SBC_AM_SNR : SBC_AM_LOUDNESS;
        sbc_reinit(sbc_.get(), 0);
        apply_config();
        return true;
    }
    return false;
}

std::optional<int64_t> CodecSbc::get_param(std::string_view key) const {
    if (key == "bitpool")  return cfg_->bitpool;
    if (key == "freq")     return sbc_to_freq(cfg_->frequency);
    if (key == "mode")     return cfg_->mode;
    if (key == "subbands") return (cfg_->subbands == SBC_SB_8) ? 8 : 4;
    if (key == "blocks") {
        switch (cfg_->blocks) {
            case SBC_BLK_4:  return 4;
            case SBC_BLK_8:  return 8;
            case SBC_BLK_12: return 12;
            case SBC_BLK_16: return 16;
            default:         return std::nullopt;
        }
    }
    if (key == "alloc")    return cfg_->allocation;
    return std::nullopt;
}

} // namespace owb
