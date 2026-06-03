// service/codecs/codec_aac.cpp
// AAC encoding via Windows Media Foundation MFT (no third-party library needed).
// MF AAC encoder outputs ADTS-framed AAC at the configured bitrate.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mftransform.h>
#include <mferror.h>
#include <wmcodecdsp.h>
#include <cstdlib>
#include "codec_aac.h"

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfuuid.lib")

// CLSID for the Windows built-in AAC encoder MFT
static const CLSID CLSID_AACMFTEncoder_local =
    { 0x93AF0C51, 0x2275, 0x45d2,
      { 0xA3, 0x5B, 0x24, 0x39, 0x06, 0x16, 0x0E, 0xEB } };

namespace owb {

struct CodecAac::Impl {
    IMFTransform*  mft         = nullptr;
    IMFMediaBuffer* in_buf     = nullptr;
    bool           mf_started  = false;

    ~Impl() {
        if (mft)       { mft->Release();       mft = nullptr; }
        if (in_buf)    { in_buf->Release();    in_buf = nullptr; }
        if (mf_started){ MFShutdown(); mf_started = false; }
    }
};

CodecAac::CodecAac() : impl_(new Impl()) {
    if (!init_mf_encoder()) {
        delete impl_;
        impl_ = nullptr;
    }
}

CodecAac::~CodecAac() {
    delete impl_;
}

bool CodecAac::init_mf_encoder() {
    HRESULT hr = MFStartup(MF_VERSION, MFSTARTUP_LITE);
    if (FAILED(hr)) return false;
    impl_->mf_started = true;

    hr = CoCreateInstance(CLSID_AACMFTEncoder_local, nullptr,
                          CLSCTX_INPROC_SERVER, IID_IMFTransform,
                          reinterpret_cast<void**>(&impl_->mft));
    if (FAILED(hr)) return false;

    // Input type: PCM stereo int16
    IMFMediaType* in_type = nullptr;
    MFCreateMediaType(&in_type);
    in_type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    in_type->SetGUID(MF_MT_SUBTYPE,    MFAudioFormat_PCM);
    in_type->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, static_cast<UINT32>(freq_));
    in_type->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS,        2);
    in_type->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE,    16);
    in_type->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT,     4); // 2ch * 2B
    in_type->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND,
                       static_cast<UINT32>(freq_) * 4u);
    hr = impl_->mft->SetInputType(0, in_type, 0);
    in_type->Release();
    if (FAILED(hr)) return false;

    // Output type: AAC at configured bitrate
    IMFMediaType* out_type = nullptr;
    MFCreateMediaType(&out_type);
    out_type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    out_type->SetGUID(MF_MT_SUBTYPE,    MFAudioFormat_AAC);
    out_type->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, static_cast<UINT32>(freq_));
    out_type->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS,        2);
    out_type->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND,
                        static_cast<UINT32>(bitrate_) / 8u);
    // ADTS output for A2DP transport
    out_type->SetUINT32(MF_MT_AAC_AUDIO_PROFILE_LEVEL_INDICATION, 0x29); // AAC-LC
    hr = impl_->mft->SetOutputType(0, out_type, 0);
    out_type->Release();
    if (FAILED(hr)) return false;

    return SUCCEEDED(impl_->mft->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0));
}

void CodecAac::shutdown_mf_encoder() {
    if (impl_) {
        delete impl_;
        impl_ = new Impl();
    }
}

std::string_view CodecAac::name() const noexcept { return "AAC"; }

std::ptrdiff_t CodecAac::encode(std::span<const int16_t> input,
                                 std::span<uint8_t>       output) {
    if (!impl_ || !impl_->mft) return -1;
    if (input.empty() || output.empty()) return -1;

    // AAC frame = 1024 samples per channel
    constexpr int kAacFrameSamples = 1024;
    if (static_cast<int>(input.size()) < kAacFrameSamples * 2) return -1;

    const DWORD in_bytes = static_cast<DWORD>(kAacFrameSamples * 2 * sizeof(int16_t));

    // Wrap input PCM in an MF sample
    IMFMediaBuffer* buf = nullptr;
    if (FAILED(MFCreateMemoryBuffer(in_bytes, &buf))) return -1;

    BYTE* data = nullptr;
    buf->Lock(&data, nullptr, nullptr);
    memcpy(data, input.data(), in_bytes);
    buf->Unlock();
    buf->SetCurrentLength(in_bytes);

    IMFSample* in_sample = nullptr;
    MFCreateSample(&in_sample);
    in_sample->AddBuffer(buf);
    buf->Release();

    HRESULT hr = impl_->mft->ProcessInput(0, in_sample, 0);
    in_sample->Release();
    if (FAILED(hr)) return -1;

    // Drain one output frame
    MFT_OUTPUT_DATA_BUFFER out_data = {};
    IMFSample* out_sample = nullptr;
    MFCreateSample(&out_sample);
    IMFMediaBuffer* out_buf = nullptr;
    MFCreateMemoryBuffer(static_cast<DWORD>(output.size()), &out_buf);
    out_sample->AddBuffer(out_buf);
    out_buf->Release();
    out_data.pSample = out_sample;

    DWORD status = 0;
    hr = impl_->mft->ProcessOutput(0, 1, &out_data, &status);
    if (FAILED(hr)) {
        out_sample->Release();
        return -1;
    }

    IMFMediaBuffer* result_buf = nullptr;
    out_sample->ConvertToContiguousBuffer(&result_buf);
    BYTE* out_ptr   = nullptr;
    DWORD out_bytes = 0;
    result_buf->Lock(&out_ptr, nullptr, &out_bytes);
    if (out_bytes > output.size()) out_bytes = static_cast<DWORD>(output.size());
    memcpy(output.data(), out_ptr, out_bytes);
    result_buf->Unlock();
    result_buf->Release();
    out_sample->Release();

    return static_cast<std::ptrdiff_t>(out_bytes);
}

bool CodecAac::set_param(CodecParam p) {
    if (p.key == "freq") {
        freq_ = static_cast<int>(p.value);
        shutdown_mf_encoder();
        impl_ = new Impl();
        init_mf_encoder();
        return true;
    }
    if (p.key == "bitrate") {
        bitrate_ = static_cast<int>(p.value);
        return true;
    }
    return false;
}

std::optional<int64_t> CodecAac::get_param(std::string_view key) const {
    if (key == "freq")    return freq_;
    if (key == "bitrate") return bitrate_;
    return std::nullopt;
}

} // namespace owb
