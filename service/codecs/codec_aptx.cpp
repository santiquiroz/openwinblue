// service/codecs/codec_aptx.cpp
//
// libopenaptx encodes 24-bit signed LE stereo interleaved PCM.
// Input block: 4 stereo samples = 4×(L3B + R3B) = 24 bytes per block.
// Output:      4 bytes (aptX Classic) or 6 bytes (aptX HD) per block.
//
// ICodec::encode() receives int16 PCM, so each sample is upscaled to 24-bit
// by shifting left 8 bits (zero-padding the LSB) before handing to aptx_encode.

#include "codec_aptx.h"

extern "C" {
#include <openaptx.h>
}

#include <array>
#include <cstring>

namespace owb {

// aptX processes exactly 4 stereo samples per block.
// Each 24-bit sample occupies 3 bytes (little-endian signed).
static constexpr size_t kSamplesPerBlock    = 4;   // stereo frames per block
static constexpr size_t kBytesPerSample24   = 3;   // bytes per 24-bit mono sample
static constexpr size_t kChannels           = 2;
// Input to aptx_encode for one block: 4 stereo samples × 3 bytes × 2 ch = 24 bytes
static constexpr size_t kInputBytesPerBlock = kSamplesPerBlock * kBytesPerSample24 * kChannels;
// Max output: 6 bytes per block (HD), 4 bytes (Classic)
static constexpr size_t kMaxOutputPerBlock  = 6;

// Convert a single int16 sample to 3-byte little-endian 24-bit (sign-extended).
static void write_s24le(uint8_t* dst, int16_t s) noexcept {
    const int32_t s24 = static_cast<int32_t>(s) << 8;
    dst[0] = static_cast<uint8_t>( s24        & 0xFF);
    dst[1] = static_cast<uint8_t>((s24 >>  8) & 0xFF);
    dst[2] = static_cast<uint8_t>((s24 >> 16) & 0xFF);
}

// Pack one block of 4 stereo int16 frames into 24-byte interleaved s24le buffer.
// libopenaptx input layout per block: LLLRRRLLLRRRLLLRRRLLLRRR
// i.e. 4 × (L_byte0 L_byte1 L_byte2) then 4 × (R_byte0 R_byte1 R_byte2)
// BUT looking at the header comment for aptx_encode:
//   "input buffer must contain sequence of the 24 bytes in format
//    LLLRRRLLLRRRLLLRRRLLLRRR (L-left, R-right)"
// which means: for each of the 4 sample positions: 3-byte L then 3-byte R.
// So layout is: [L0][R0][L1][R1][L2][R2][L3][R3] where each is 3 bytes = 24 bytes.
static void pack_block_s24le(const int16_t* stereo_frames,
                              uint8_t* block24) noexcept {
    for (size_t i = 0; i < kSamplesPerBlock; ++i) {
        write_s24le(block24 + i * kBytesPerSample24 * kChannels + 0,
                    stereo_frames[i * kChannels + 0]); // L
        write_s24le(block24 + i * kBytesPerSample24 * kChannels + kBytesPerSample24,
                    stereo_frames[i * kChannels + 1]); // R
    }
}

// ── CodecAptx ────────────────────────────────────────────────────────────────

CodecAptx::CodecAptx(bool hd)
    : ctx_(aptx_init(hd ? 1 : 0))
    , hd_(hd)
{}

CodecAptx::~CodecAptx() {
    if (ctx_) aptx_finish(ctx_);
}

std::string_view CodecAptx::name() const noexcept {
    return hd_ ? "aptX-HD" : "aptX";
}

std::ptrdiff_t CodecAptx::encode(std::span<const int16_t> input,
                                  std::span<uint8_t>       output) {
    if (!ctx_ || output.empty() || input.empty()) return -1;

    // Need at least 4 stereo frames (4 left + 4 right = 8 samples).
    const size_t kMinSamples = kSamplesPerBlock * kChannels;
    if (input.size() < kMinSamples) return -1;

    // Process all complete blocks.
    const size_t num_blocks = input.size() / kMinSamples;

    std::array<uint8_t, kInputBytesPerBlock>  block24{};
    std::array<uint8_t, kMaxOutputPerBlock>   block_out{};

    std::ptrdiff_t total_written = 0;
    size_t out_offset = 0;

    for (size_t b = 0; b < num_blocks; ++b) {
        pack_block_s24le(input.data() + b * kMinSamples, block24.data());

        size_t written = 0;
        size_t processed = aptx_encode(
            ctx_,
            block24.data(),
            kInputBytesPerBlock,
            block_out.data(),
            block_out.size(),
            &written
        );

        if (processed != kInputBytesPerBlock || written == 0) {
            if (total_written == 0) return -1;
            break;
        }

        if (out_offset + written > output.size()) break;

        std::memcpy(output.data() + out_offset, block_out.data(), written);
        out_offset   += written;
        total_written += static_cast<std::ptrdiff_t>(written);
    }

    return total_written > 0 ? total_written : -1;
}

bool CodecAptx::set_param(CodecParam p) {
    if (p.key == "freq") {
        freq_ = static_cast<int>(p.value);
        return true;
    }
    return false;
}

std::optional<int64_t> CodecAptx::get_param(std::string_view key) const {
    if (key == "freq") return freq_;
    if (key == "hd")   return hd_ ? 1 : 0;
    return std::nullopt;
}

} // namespace owb
