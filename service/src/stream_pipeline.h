// service/src/stream_pipeline.h
#pragma once
#include <cstdint>
#include <memory>
#include <string>

#include "codec_controller.h"

namespace owb { class A2dpStream; class IAudioSource; }
namespace owb::ai { class AiPipeline; }

namespace owb {

// StreamPipeline — the missing runtime orchestrator.
//
// On its own thread it pulls PCM from an IAudioSource, (optionally) runs the AI
// pipeline, encodes with the active codec, and pushes each encoded frame to the
// kernel driver via A2dpStream. This is what turns the individual components
// (capture, codec, driver) into an actual audio path.
//
// The active codec is swappable at runtime via set_codec_id() — the IPC server
// calls it when the GUI requests a different codec.
class StreamPipeline final : public ICodecController {
public:
    StreamPipeline(IAudioSource* source, A2dpStream* sink, ai::AiPipeline* ai);
    ~StreamPipeline() override;

    StreamPipeline(const StreamPipeline&)            = delete;
    StreamPipeline& operator=(const StreamPipeline&) = delete;

    // Spawn the streaming thread. Returns false if the source cannot start.
    bool start();

    // Stop the thread and release the codec. Safe to call when not started.
    void stop();

    // ICodecController
    void        set_codec_id(uint32_t codec_id) override;
    uint32_t    codec_id() const noexcept       override;
    std::string codec_name() const              override;
    bool        is_streaming() const noexcept   override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace owb
