#pragma once
#include <cstdint>
#include <span>
#include <vector>

namespace owb {

// Wraps one AAC access unit in an AudioMuxElement(muxConfigPresent = 1) carrying an
// in-band StreamMuxConfig (audioMuxVersion 0), as A2DP AAC expects (RFC 3016 MP4A-LATM).
// audio_specific_config must be byte-exact (no padding bits), e.g. 2 bytes for AAC-LC.
std::vector<uint8_t> latm_pack(std::span<const uint8_t> audio_specific_config,
                               std::span<const uint8_t> access_unit);

} // namespace owb
