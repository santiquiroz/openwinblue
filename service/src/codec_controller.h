// service/src/codec_controller.h
#pragma once
#include <cstdint>
#include <string>

namespace owb {

// Minimal runtime control surface the IPC server uses to drive the streaming
// pipeline, without depending on the concrete StreamPipeline type (keeps the
// IPC library free of codec/driver link dependencies). StreamPipeline implements
// this; tests pass nullptr.
class ICodecController {
public:
    virtual ~ICodecController() = default;
    virtual void        set_codec_id(uint32_t codec_id) = 0;
    virtual uint32_t    codec_id() const noexcept       = 0;
    virtual std::string codec_name() const              = 0;
    virtual bool        is_streaming() const noexcept   = 0;
};

} // namespace owb
