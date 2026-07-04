// service/src/stream_pipeline.cpp
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winioctl.h>    // CTL_CODE — must precede owb_ioctl.h

#include "stream_pipeline.h"

#include "iaudio_source.h"
#include "a2dp_stream.h"
#include "codec_interface.h"
#include "codec_factory.h"
#include "../ai/ai_pipeline.h"
#include "owb_ioctl.h"
#include "owb_log.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace owb {

namespace {
constexpr size_t kReadSamples   = 4096;   // interleaved int16 pulled per cycle
constexpr size_t kOutBytes      = 8192;   // per-frame encode scratch
constexpr size_t kBlobFeedMax   = 2048;   // cap for codecs that drain the whole buffer
constexpr size_t kAccumHardCap  = 1u << 18;
constexpr auto   kIdleSleep     = std::chrono::milliseconds(5);
constexpr auto   kReopenSleep   = std::chrono::milliseconds(50);

// Reduce interleaved PCM of arbitrary channel count to stereo interleaved.
void to_stereo(const int16_t* src, size_t frames, int channels,
               std::vector<int16_t>& dst) {
    if (channels == 2) {
        dst.insert(dst.end(), src, src + frames * 2);
        return;
    }
    for (size_t f = 0; f < frames; ++f) {
        const int16_t l = src[f * static_cast<size_t>(channels)];
        const int16_t r = (channels > 1) ? src[f * static_cast<size_t>(channels) + 1] : l;
        dst.push_back(l);
        dst.push_back(r);
    }
}
} // namespace

struct StreamPipeline::Impl {
    IAudioSource*   source;
    A2dpStream*     sink;
    ai::AiPipeline* ai;

    std::thread           thread;
    std::atomic<bool>     running{false};
    std::atomic<bool>     streaming{false};
    std::atomic<uint32_t> codec_id{OWB_CODEC_SBC};

    std::mutex             codec_mtx;
    std::unique_ptr<ICodec> codec;      // guarded by codec_mtx
    std::string            codec_name;  // guarded by codec_mtx

    std::vector<int16_t>   accum;       // stereo interleaved, run()-thread only

    Impl(IAudioSource* s, A2dpStream* k, ai::AiPipeline* a)
        : source(s), sink(k), ai(a) {}

    void rebuild_codec(uint32_t id) {
        std::lock_guard<std::mutex> lock(codec_mtx);
        codec = CodecFactory::create(id);
        if (!codec) {
            OWB_LOG_ERROR("CodecFactory::create returned null for codec_id %u", id);
        } else {
            OWB_LOG_INFO("Codec created: id=%u, name=%.*s", id,
                         static_cast<int>(codec->name().size()), codec->name().data());
        }
        if (codec && source) {
            codec->set_param({"freq", source->sample_rate()});
        }
        codec_name = codec ? std::string(codec->name()) : std::string();
        codec_id   = id;
    }

    void drain(std::vector<uint8_t>& out) {
        std::lock_guard<std::mutex> lock(codec_mtx);
        if (!codec) return;
        const uint32_t id = codec_id.load();
        const int fs = codec->input_frame_samples();

        if (fs > 0) {
            const size_t frame = static_cast<size_t>(fs);
            while (accum.size() >= frame) {
                const std::ptrdiff_t n = codec->encode(
                    std::span<const int16_t>(accum.data(), frame),
                    std::span<uint8_t>(out.data(), out.size()));
                if (n > 0) {
                    sink->send_frame(id, std::span<const uint8_t>(
                        out.data(), static_cast<size_t>(n)));
                    streaming = true;
                } else {
                    static unsigned long encode_fail_count = 0;
                    encode_fail_count++;
                    if (encode_fail_count == 1 || encode_fail_count % 500 == 0) {
                        OWB_LOG_WARN("codec->encode returned %lld (count: %lu)", n, encode_fail_count);
                    }
                }
                accum.erase(accum.begin(),
                            accum.begin() + static_cast<std::ptrdiff_t>(frame));
            }
            return;
        }

        // fs == 0: codec drains the whole buffer (aptX). Feed a bounded,
        // block-aligned chunk (aptX processes 4 stereo frames = 8 samples/block).
        size_t usable = std::min(accum.size(), kBlobFeedMax);
        usable -= (usable % 8);
        if (usable == 0) return;
        const std::ptrdiff_t n = codec->encode(
            std::span<const int16_t>(accum.data(), usable),
            std::span<uint8_t>(out.data(), out.size()));
        if (n > 0) {
            sink->send_frame(id, std::span<const uint8_t>(
                out.data(), static_cast<size_t>(n)));
            streaming = true;
        }
        accum.erase(accum.begin(), accum.begin() + static_cast<std::ptrdiff_t>(usable));
    }

    void run() {
        OWB_LOG_INFO("StreamPipeline::Impl::run() starting");
        std::vector<int16_t> read_buf(kReadSamples);
        std::vector<int16_t> stereo;
        std::vector<uint8_t> out(kOutBytes);
        
        static bool first_frame_logged = false;
        static unsigned long reopen_attempt = 0;

        while (running.load()) {
            if (!sink->is_open() && !sink->open()) {
                streaming = false;
                reopen_attempt++;
                if (reopen_attempt == 1 || reopen_attempt % 200 == 0) {
                    OWB_LOG_WARN("Sink failed to open (attempt %lu)", reopen_attempt);
                }
                std::this_thread::sleep_for(kReopenSleep);
                continue;
            }
            
            reopen_attempt = 0;

            const std::ptrdiff_t frames = source->read(
                std::span<int16_t>(read_buf.data(), read_buf.size()));
            if (frames <= 0) {
                std::this_thread::sleep_for(kIdleSleep);
                continue;
            }
            
            if (!first_frame_logged) {
                OWB_LOG_INFO("First audio frames received: %lld frames", (long long)frames);
                first_frame_logged = true;
            }

            stereo.clear();
            to_stereo(read_buf.data(), static_cast<size_t>(frames),
                      source->channels(), stereo);

            // Self-gated: no-op unless a feature was enabled via IPC.
            if (ai) ai->process(std::span<int16_t>(stereo.data(), stereo.size()));

            accum.insert(accum.end(), stereo.begin(), stereo.end());
            if (accum.size() > kAccumHardCap) {
                OWB_LOG_WARN("Accumulator overflow, clearing buffer (size: %zu)", accum.size());
                accum.clear();
            }

            drain(out);
        }
        streaming = false;
    }
};

StreamPipeline::StreamPipeline(IAudioSource* source, A2dpStream* sink, ai::AiPipeline* ai)
    : impl_(std::make_unique<Impl>(source, sink, ai)) {
    impl_->rebuild_codec(OWB_CODEC_SBC);
}

StreamPipeline::~StreamPipeline() { stop(); }

bool StreamPipeline::start() {
    if (impl_->running.load()) return true;
    if (!impl_->source) {
        OWB_LOG_WARN("StreamPipeline::start() called but no audio source available");
        return false;
    }
    OWB_LOG_INFO("StreamPipeline starting");
    impl_->running = true;
    impl_->thread  = std::thread([this] { impl_->run(); });
    return true;
}

void StreamPipeline::stop() {
    if (!impl_->running.exchange(false)) return;
    OWB_LOG_INFO("StreamPipeline stopping");
    if (impl_->thread.joinable()) impl_->thread.join();
    impl_->streaming = false;
}

void StreamPipeline::set_codec_id(uint32_t codec_id) {
    impl_->rebuild_codec(codec_id);
}

uint32_t StreamPipeline::codec_id() const noexcept {
    return impl_->codec_id.load();
}

std::string StreamPipeline::codec_name() const {
    std::lock_guard<std::mutex> lock(impl_->codec_mtx);
    return impl_->codec_name;
}

bool StreamPipeline::is_streaming() const noexcept {
    return impl_->streaming.load();
}

} // namespace owb
