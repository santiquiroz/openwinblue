#pragma once
#include <cstdint>
#include <span>
#include <string_view>

namespace owb {

// Parameters for codec configuration — each codec defines its own set.
// Values are passed as key/value pairs to keep the interface stable.
struct CodecParam {
    std::string_view key;
    int64_t          value;
};

// Abstract interface every codec wrapper must implement.
class ICodec {
public:
    virtual ~ICodec() = default;

    // Short codec name returned to GUI/IPC (e.g. "LDAC", "SBC", "aptX-HD").
    virtual std::string_view name() const noexcept = 0;

    // Encode PCM interleaved stereo int16 samples into the codec's output format.
    // input:  samples × 2 int16 (left, right, left, right …)
    // output: caller-allocated buffer; returns bytes written, or -1 on error.
    virtual int encode(std::span<const int16_t> input,
                       std::span<uint8_t>       output) = 0;

    // Apply a configuration parameter. Returns false if the key is unknown.
    virtual bool set_param(CodecParam param) = 0;

    // Retrieve a configuration parameter. Returns INT64_MIN if unknown.
    virtual int64_t get_param(std::string_view key) const = 0;
};

} // namespace owb
