// service/src/iaudio_source.h
#pragma once
#include <cstdint>
#include <span>

namespace owb {

// Abstract audio input interface.
// Implementations: AudioCapture (WASAPI loopback), MockAudioSource (tests).
class IAudioSource {
public:
    virtual ~IAudioSource() = default;

    // Open the audio source. Returns false on failure.
    virtual bool start() = 0;

    // Stop and release resources.
    virtual void stop() = 0;

    // Read captured PCM interleaved int16 samples.
    // Returns frame count read (>= 0) or -1 on error.
    virtual std::ptrdiff_t read(std::span<int16_t> buffer) = 0;

    // Sample rate in Hz (valid after start()).
    virtual int sample_rate() const noexcept = 0;

    // Number of channels (1 = mono, 2 = stereo).
    virtual int channels()    const noexcept = 0;
};

} // namespace owb
