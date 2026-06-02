// service/src/audio_capture.h
#pragma once
#include "iaudio_source.h"
#include <memory>

// Forward-declare COM interfaces to avoid pulling all of mmdeviceapi.h here.
struct IMMDevice;
struct IAudioClient;
struct IAudioCaptureClient;

namespace owb {

// WASAPI loopback capture from the system default render endpoint.
// Captures whatever is currently playing on speakers/headphones.
class AudioCapture final : public IAudioSource {
public:
    AudioCapture();
    ~AudioCapture() override;

    bool            start()                              override;
    void            stop()                               override;
    std::ptrdiff_t  read(std::span<int16_t> buffer)     override;
    int             sample_rate() const noexcept         override;
    int             channels()    const noexcept         override;

private:
    struct Impl;
    static void release_impl(Impl* impl); // releases COM objects + mix_format
    std::unique_ptr<Impl> impl_;
};

} // namespace owb
