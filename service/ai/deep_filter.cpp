// service/ai/deep_filter.cpp
// DeepFilterNet3 via ONNX Runtime — compiled conditionally on OWB_HAVE_ONNXRUNTIME.
// Without ONNX Runtime the stub compiles and is_available() returns false.
#include "deep_filter.h"

#if OWB_HAVE_ONNXRUNTIME
#include <onnxruntime_cxx_api.h>
#include <vector>
#include <string>
#include <cstring>
#include <cstdlib>
#include <algorithm>

namespace owb::ai {

// DeepFilterNet3 processes 480-sample frames at 48 kHz.
static constexpr int kFrameSamples = 480;
static constexpr int kChannels     = 1; // DeepFilter operates on mono; we downmix

struct DeepFilter::Impl {
    Ort::Env                     env{ ORT_LOGGING_LEVEL_WARNING, "DeepFilter" };
    Ort::Session*                session = nullptr;
    Ort::SessionOptions          opts;
    std::vector<float>           in_buf;
    std::vector<float>           out_buf;
    bool                         available = false;

    Impl() {
        opts.SetIntraOpNumThreads(1);
        opts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

        // Try DirectML EP (GPU/NPU) first, fall back to CPU
        try {
            OrtSessionOptionsAppendExecutionProvider_DML(opts, 0);
        } catch (...) {}

        // Model path — placed next to the service executable
        wchar_t exe_path[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, exe_path, MAX_PATH);
        std::wstring model_path(exe_path);
        auto slash = model_path.rfind(L'\\');
        if (slash != std::wstring::npos) model_path.resize(slash + 1);
        model_path += L"deepfilter.onnx";

        try {
            session   = new Ort::Session(env, model_path.c_str(), opts);
            in_buf.resize(kFrameSamples, 0.f);
            out_buf.resize(kFrameSamples, 0.f);
            available = true;
        } catch (...) {
            // Model not found — stub mode
        }
    }

    ~Impl() { delete session; }
};

DeepFilter::DeepFilter()  : impl_(new Impl()) {}
DeepFilter::~DeepFilter() { delete impl_; }

bool DeepFilter::is_available() const noexcept { return impl_->available; }

void DeepFilter::process(std::span<int16_t> stereo_pcm) noexcept {
    if (!impl_->available || !impl_->session) return;

    const int frames = static_cast<int>(stereo_pcm.size()) / 2;
    if (frames <= 0) return;

    // Downmix stereo → mono float32 [-1,1]
    impl_->in_buf.resize(static_cast<size_t>(frames));
    for (int i = 0; i < frames; ++i)
        impl_->in_buf[i] = (stereo_pcm[i * 2] + stereo_pcm[i * 2 + 1]) * (0.5f / 32768.f);

    // Run ONNX session
    auto memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    int64_t shape[] = { 1, static_cast<int64_t>(frames) };

    Ort::Value in_tensor = Ort::Value::CreateTensor<float>(
        memory_info, impl_->in_buf.data(), impl_->in_buf.size(), shape, 2);

    const char* in_names[]  = { "input" };
    const char* out_names[] = { "output" };
    impl_->out_buf.resize(static_cast<size_t>(frames));

    try {
        auto out_tensors = impl_->session->Run(
            Ort::RunOptions{nullptr}, in_names, &in_tensor, 1, out_names, 1);

        float* out_data = out_tensors[0].GetTensorMutableData<float>();

        // Upmix filtered mono → stereo int16
        for (int i = 0; i < frames; ++i) {
            float s = std::clamp(out_data[i], -1.f, 1.f);
            auto  v = static_cast<int16_t>(s * 32767.f);
            stereo_pcm[i * 2]     = v;
            stereo_pcm[i * 2 + 1] = v;
        }
    } catch (...) {}
}

} // namespace owb::ai

#else  // OWB_HAVE_ONNXRUNTIME == 0 — stub

namespace owb::ai {

struct DeepFilter::Impl {};

DeepFilter::DeepFilter()  : impl_(new Impl()) {}
DeepFilter::~DeepFilter() { delete impl_; }

bool DeepFilter::is_available() const noexcept { return false; }
void DeepFilter::process(std::span<int16_t>) noexcept {}

} // namespace owb::ai

#endif
