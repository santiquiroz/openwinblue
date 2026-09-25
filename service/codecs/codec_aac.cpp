// service/codecs/codec_aac.cpp
// AAC encoding via Windows Media Foundation MFT (no third-party library needed).
// The MFT emits raw AAC access units (MF_MT_AAC_PAYLOAD_TYPE 0); each one is wrapped
// in LATM here because A2DP carries AAC as RFC 3016 MP4A-LATM.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mftransform.h>
#include <mferror.h>
#include <wmcodecdsp.h>
#include <algorithm>
#include <cstdlib>
#include <vector>
#include "codec_aac.h"
#include "latm.h"

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfuuid.lib")

namespace owb {
namespace {

constexpr int      kAacFrameSamples    = 1024;  // per channel
constexpr int      kChannels           = 2;
constexpr size_t   kAacFrameValues     = static_cast<size_t>(kAacFrameSamples) * kChannels;
constexpr UINT32   kAacPayloadRaw      = 0;
constexpr UINT32   kAacLcProfileLevel  = 0x29;
constexpr LONGLONG kHundredNsPerSecond = 10000000;
// MF_MT_USER_DATA holds the HEAACWAVEINFO fields that follow WAVEFORMATEX, then the ASC.
constexpr size_t   kHeAacWaveInfoTailBytes = 12;

std::vector<uint8_t> asc_from_user_data(std::span<const uint8_t> user_data) {
    if (user_data.size() <= kHeAacWaveInfoTailBytes) return {};
    return {user_data.begin() + kHeAacWaveInfoTailBytes, user_data.end()};
}

bool set_pcm_input_type(IMFTransform* mft, int freq) {
    IMFMediaType* type = nullptr;
    if (FAILED(MFCreateMediaType(&type))) return false;
    type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    type->SetGUID(MF_MT_SUBTYPE,    MFAudioFormat_PCM);
    type->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, static_cast<UINT32>(freq));
    type->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS,       kChannels);
    type->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE,    16);
    type->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT,    kChannels * 2);
    type->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND,
                    static_cast<UINT32>(freq) * kChannels * 2u);
    const HRESULT hr = mft->SetInputType(0, type, 0);
    type->Release();
    return SUCCEEDED(hr);
}

bool set_aac_output_type(IMFTransform* mft, int freq, int bitrate) {
    IMFMediaType* type = nullptr;
    if (FAILED(MFCreateMediaType(&type))) return false;
    type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    type->SetGUID(MF_MT_SUBTYPE,    MFAudioFormat_AAC);
    type->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, static_cast<UINT32>(freq));
    type->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS,       kChannels);
    type->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE,    16);
    type->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, static_cast<UINT32>(bitrate) / 8u);
    type->SetUINT32(MF_MT_AAC_PAYLOAD_TYPE,         kAacPayloadRaw);
    type->SetUINT32(MF_MT_AAC_AUDIO_PROFILE_LEVEL_INDICATION, kAacLcProfileLevel);
    const HRESULT hr = mft->SetOutputType(0, type, 0);
    type->Release();
    return SUCCEEDED(hr);
}

std::vector<uint8_t> read_user_data(IMFMediaType* type) {
    UINT32 size = 0;
    if (FAILED(type->GetBlobSize(MF_MT_USER_DATA, &size))) return {};
    std::vector<uint8_t> blob(size);
    if (FAILED(type->GetBlob(MF_MT_USER_DATA, blob.data(), size, nullptr))) return {};
    return blob;
}

std::vector<uint8_t> read_audio_specific_config(IMFTransform* mft) {
    IMFMediaType* type = nullptr;
    if (FAILED(mft->GetOutputCurrentType(0, &type))) return {};
    const auto user_data = read_user_data(type);
    type->Release();
    return asc_from_user_data(user_data);
}

LONGLONG frame_time(uint64_t frame_index, int freq) {
    return static_cast<LONGLONG>(frame_index) * kAacFrameSamples * kHundredNsPerSecond / freq;
}

IMFSample* make_pcm_sample(std::span<const int16_t> pcm) {
    const DWORD bytes = static_cast<DWORD>(pcm.size_bytes());
    IMFMediaBuffer* buf = nullptr;
    if (FAILED(MFCreateMemoryBuffer(bytes, &buf))) return nullptr;

    BYTE* data = nullptr;
    buf->Lock(&data, nullptr, nullptr);
    memcpy(data, pcm.data(), bytes);
    buf->Unlock();
    buf->SetCurrentLength(bytes);

    IMFSample* sample = nullptr;
    if (SUCCEEDED(MFCreateSample(&sample))) sample->AddBuffer(buf);
    buf->Release();
    return sample;
}

bool submit_pcm_frame(IMFTransform* mft, std::span<const int16_t> pcm,
                      LONGLONG time, LONGLONG duration) {
    IMFSample* sample = make_pcm_sample(pcm);
    if (!sample) return false;
    // The MF AAC encoder rejects untimed input with MF_E_NO_SAMPLE_TIMESTAMP.
    sample->SetSampleTime(time);
    sample->SetSampleDuration(duration);
    const HRESULT hr = mft->ProcessInput(0, sample, 0);
    sample->Release();
    return SUCCEEDED(hr);
}

std::vector<uint8_t> copy_sample_bytes(IMFSample* sample) {
    IMFMediaBuffer* buf = nullptr;
    if (FAILED(sample->ConvertToContiguousBuffer(&buf))) return {};
    BYTE* ptr   = nullptr;
    DWORD bytes = 0;
    std::vector<uint8_t> out;
    if (SUCCEEDED(buf->Lock(&ptr, nullptr, &bytes))) {
        out.assign(ptr, ptr + bytes);
        buf->Unlock();
    }
    buf->Release();
    return out;
}

IMFSample* make_output_sample(size_t capacity) {
    IMFMediaBuffer* buf = nullptr;
    if (FAILED(MFCreateMemoryBuffer(static_cast<DWORD>(capacity), &buf))) return nullptr;
    IMFSample* sample = nullptr;
    if (SUCCEEDED(MFCreateSample(&sample))) sample->AddBuffer(buf);
    buf->Release();
    return sample;
}

std::vector<uint8_t> drain_access_unit(IMFTransform* mft, size_t capacity) {
    IMFSample* sample = make_output_sample(capacity);
    if (!sample) return {};
    MFT_OUTPUT_DATA_BUFFER out_data = {};
    out_data.pSample = sample;
    DWORD status = 0;
    const HRESULT hr = mft->ProcessOutput(0, 1, &out_data, &status);
    auto au = SUCCEEDED(hr) ? copy_sample_bytes(sample) : std::vector<uint8_t>{};
    sample->Release();
    return au;
}

} // namespace

struct CodecAac::Impl {
    IMFTransform*        mft        = nullptr;
    bool                 mf_started = false;
    std::vector<uint8_t> asc;
    uint64_t             frames_in  = 0;

    ~Impl() {
        if (mft)       { mft->Release();       mft = nullptr; }
        if (mf_started){ MFShutdown(); mf_started = false; }
    }
};

CodecAac::CodecAac() {
    start_mf_encoder();
}

CodecAac::~CodecAac() = default;

void CodecAac::start_mf_encoder() {
    impl_.reset();
    impl_ = std::make_unique<Impl>();
    if (!init_mf_encoder()) impl_.reset();
}

bool CodecAac::init_mf_encoder() {
    if (FAILED(MFStartup(MF_VERSION, MFSTARTUP_LITE))) return false;
    impl_->mf_started = true;

    const HRESULT hr = CoCreateInstance(__uuidof(AACMFTEncoder), nullptr,
                                        CLSCTX_INPROC_SERVER, IID_IMFTransform,
                                        reinterpret_cast<void**>(&impl_->mft));
    if (FAILED(hr)) return false;
    if (!set_pcm_input_type(impl_->mft, freq_)) return false;
    if (!set_aac_output_type(impl_->mft, freq_, bitrate_)) return false;

    impl_->asc = read_audio_specific_config(impl_->mft);
    if (impl_->asc.empty()) return false;

    return SUCCEEDED(impl_->mft->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0));
}

std::string_view CodecAac::name() const noexcept { return "AAC"; }

std::ptrdiff_t CodecAac::encode(std::span<const int16_t> input,
                                 std::span<uint8_t>       output) {
    if (!impl_ || !impl_->mft) return -1;
    if (input.size() < kAacFrameValues || output.empty()) return -1;

    const LONGLONG time     = frame_time(impl_->frames_in, freq_);
    const LONGLONG duration = frame_time(impl_->frames_in + 1, freq_) - time;
    if (!submit_pcm_frame(impl_->mft, input.first(kAacFrameValues), time, duration)) return -1;
    ++impl_->frames_in;

    const auto au = drain_access_unit(impl_->mft, output.size());
    if (au.empty()) return -1;

    const auto packet = latm_pack(impl_->asc, au);
    if (packet.size() > output.size()) return -1;
    std::copy(packet.begin(), packet.end(), output.begin());
    return static_cast<std::ptrdiff_t>(packet.size());
}

bool CodecAac::set_param(CodecParam p) {
    if (p.key == "freq") {
        freq_ = static_cast<int>(p.value);
        start_mf_encoder();
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
