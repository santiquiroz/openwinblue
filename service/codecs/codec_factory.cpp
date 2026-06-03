// service/codecs/codec_factory.cpp
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winioctl.h>    // CTL_CODE — must precede owb_ioctl.h

#include "codec_factory.h"
#include "codec_sbc.h"
#include "codec_aptx.h"
#include "codec_ldac.h"

namespace owb {

std::unique_ptr<ICodec> CodecFactory::create(uint32_t codec_id) {
    switch (codec_id) {
        case OWB_CODEC_SBC:    return std::make_unique<CodecSbc>();
        case OWB_CODEC_LDAC:   return std::make_unique<CodecLdac>();
        case OWB_CODEC_APTX:   return std::make_unique<CodecAptx>(false);
        case OWB_CODEC_APTXHD: return std::make_unique<CodecAptx>(true);
        default:               return std::make_unique<CodecSbc>();
    }
}

} // namespace owb
