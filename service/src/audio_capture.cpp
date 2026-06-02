// service/src/audio_capture.cpp
//
// WASAPI loopback capture.
// Requires COM initialized by caller (CoInitializeEx).
//
#include "audio_capture.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>

#include <cstring>
#include <algorithm>

namespace owb {

namespace {
// Minimal RAII COM pointer — avoids ATL/WRL dependency
template<typename T>
struct ComPtr {
    T* p = nullptr;
    ~ComPtr() { if (p) { p->Release(); p = nullptr; } }
    T** operator&() { return &p; }
    T*  operator->() const { return p; }
    explicit operator bool() const { return p != nullptr; }
    ComPtr() = default;
    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;
};
} // namespace

struct AudioCapture::Impl {
    ComPtr<IMMDeviceEnumerator> enumerator;
    ComPtr<IMMDevice>           device;
    ComPtr<IAudioClient>        client;
    ComPtr<IAudioCaptureClient> capture;
    WAVEFORMATEX*               mix_format    = nullptr;
    int                         sample_rate_  = 44100;
    int                         channels_     = 2;
    bool                        running_      = false;
};

AudioCapture::AudioCapture() : impl_(std::make_unique<Impl>()) {}
AudioCapture::~AudioCapture() { stop(); }

bool AudioCapture::start() {
    if (impl_->running_) return true;

    HRESULT hr = CoCreateInstance(
        __uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
        __uuidof(IMMDeviceEnumerator),
        reinterpret_cast<void**>(&impl_->enumerator.p)
    );
    if (FAILED(hr)) return false;

    hr = impl_->enumerator->GetDefaultAudioEndpoint(
        eRender, eConsole, &impl_->device.p
    );
    if (FAILED(hr)) return false;

    hr = impl_->device->Activate(
        __uuidof(IAudioClient), CLSCTX_ALL, nullptr,
        reinterpret_cast<void**>(&impl_->client.p)
    );
    if (FAILED(hr)) return false;

    hr = impl_->client->GetMixFormat(&impl_->mix_format);
    if (FAILED(hr)) return false;

    impl_->sample_rate_ = static_cast<int>(impl_->mix_format->nSamplesPerSec);
    impl_->channels_    = static_cast<int>(impl_->mix_format->nChannels);

    constexpr REFERENCE_TIME kBufDuration = 10 * 10000 * 100; // 100ms
    hr = impl_->client->Initialize(
        AUDCLNT_SHAREMODE_SHARED,
        AUDCLNT_STREAMFLAGS_LOOPBACK,
        kBufDuration, 0,
        impl_->mix_format, nullptr
    );
    if (FAILED(hr)) return false;

    hr = impl_->client->GetService(
        __uuidof(IAudioCaptureClient),
        reinterpret_cast<void**>(&impl_->capture.p)
    );
    if (FAILED(hr)) return false;

    hr = impl_->client->Start();
    if (FAILED(hr)) return false;

    impl_->running_ = true;
    return true;
}

void AudioCapture::stop() {
    if (!impl_->running_) return;
    impl_->client->Stop();
    if (impl_->mix_format) {
        CoTaskMemFree(impl_->mix_format);
        impl_->mix_format = nullptr;
    }
    impl_->running_ = false;
}

std::ptrdiff_t AudioCapture::read(std::span<int16_t> buffer) {
    if (!impl_->running_ || !impl_->capture.p) return -1;

    BYTE*  data          = nullptr;
    UINT32 frames_avail  = 0;
    DWORD  flags         = 0;

    HRESULT hr = impl_->capture->GetBuffer(&data, &frames_avail, &flags, nullptr, nullptr);
    if (hr == AUDCLNT_S_BUFFER_EMPTY || frames_avail == 0) return 0;
    if (FAILED(hr)) return -1;

    const auto   ch            = static_cast<size_t>(impl_->channels_);
    const size_t samples_avail = frames_avail * ch;
    const size_t to_copy       = std::min(samples_avail, buffer.size());

    if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
        std::fill(buffer.begin(),
                  buffer.begin() + static_cast<ptrdiff_t>(to_copy),
                  int16_t{0});
    } else {
        // WASAPI shared mode returns IEEE float32 — convert to int16
        const float* src = reinterpret_cast<const float*>(data);
        for (size_t i = 0; i < to_copy; ++i) {
            float s = src[i];
            s = s >  1.0f ?  1.0f : (s < -1.0f ? -1.0f : s);
            buffer[i] = static_cast<int16_t>(s * 32767.0f);
        }
    }

    impl_->capture->ReleaseBuffer(frames_avail);
    return static_cast<std::ptrdiff_t>(to_copy / ch);
}

int AudioCapture::sample_rate() const noexcept { return impl_->sample_rate_; }
int AudioCapture::channels()    const noexcept { return impl_->channels_; }

} // namespace owb
