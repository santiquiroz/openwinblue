// service/codecs/latm.cpp
#include "latm.h"

namespace owb {
namespace {

constexpr uint32_t kVariableRateBufferFullness = 0xFF;
constexpr size_t   kPayloadLengthEscape        = 255;

class BitWriter {
public:
    void put(uint32_t value, int bits) {
        for (int i = bits - 1; i >= 0; --i) put_bit((value >> i) & 1u);
    }

    void put_bytes(std::span<const uint8_t> bytes) {
        for (const uint8_t b : bytes) put(b, 8);
    }

    std::vector<uint8_t> take() { return std::move(bytes_); }

private:
    void put_bit(uint32_t bit) {
        const size_t offset = bit_count_ % 8;
        if (offset == 0) bytes_.push_back(0);
        if (bit != 0) bytes_.back() |= static_cast<uint8_t>(0x80u >> offset);
        ++bit_count_;
    }

    std::vector<uint8_t> bytes_;
    size_t               bit_count_ = 0;
};

void write_stream_mux_config(BitWriter& w, std::span<const uint8_t> asc) {
    w.put(0, 1);                            // audioMuxVersion
    w.put(1, 1);                            // allStreamsSameTimeFraming
    w.put(0, 6);                            // numSubFrames
    w.put(0, 4);                            // numProgram
    w.put(0, 3);                            // numLayer
    w.put_bytes(asc);                       // AudioSpecificConfig
    w.put(0, 3);                            // frameLengthType
    w.put(kVariableRateBufferFullness, 8);  // latmBufferFullness
    w.put(0, 1);                            // otherDataPresent
    w.put(0, 1);                            // crcCheckPresent
}

void write_payload_length_info(BitWriter& w, size_t length) {
    for (; length >= kPayloadLengthEscape; length -= kPayloadLengthEscape)
        w.put(kPayloadLengthEscape, 8);
    w.put(static_cast<uint32_t>(length), 8);
}

} // namespace

std::vector<uint8_t> latm_pack(std::span<const uint8_t> audio_specific_config,
                               std::span<const uint8_t> access_unit) {
    BitWriter w;
    w.put(0, 1);  // useSameStreamMux = 0: every packet carries its StreamMuxConfig
    write_stream_mux_config(w, audio_specific_config);
    write_payload_length_info(w, access_unit.size());
    w.put_bytes(access_unit);
    return w.take();
}

} // namespace owb
