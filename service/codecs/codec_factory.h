// service/codecs/codec_factory.h
#pragma once
#include "codec_interface.h"
#include <memory>
#include <cstdint>

// owb_ioctl.h defines OWB_CODEC_* constants and guards itself with
// #ifndef CTL_CODE so it is safe to include here. Callers that need
// windows.h / winioctl.h must include those before this header.
#include "owb_ioctl.h"

namespace owb {

class CodecFactory {
public:
    static std::unique_ptr<ICodec> create(uint32_t codec_id);
};

} // namespace owb
