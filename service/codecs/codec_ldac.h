// service/codecs/codec_ldac.h
#pragma once
#include "codec_interface.h"

// Forward-declare handle type to avoid pulling in ldacBT.h in every translation unit.
struct _st_ldacbt_handle;
typedef struct _st_ldacbt_handle* HANDLE_LDAC_BT;

namespace owb {

class CodecLdac final : public ICodec {
public:
    CodecLdac();
    ~CodecLdac() override;

    // Non-copyable, non-movable (owns an opaque C handle)
    CodecLdac(const CodecLdac&)            = delete;
    CodecLdac& operator=(const CodecLdac&) = delete;

    std::string_view       name()     const noexcept override;
    std::ptrdiff_t         encode(std::span<const int16_t> input,
                                  std::span<uint8_t>       output) override;
    int                    input_frame_samples() const noexcept   override;
    bool                   set_param(CodecParam param)            override;
    std::optional<int64_t> get_param(std::string_view key) const  override;

private:
    void reinit();

    HANDLE_LDAC_BT handle_ = nullptr;
    int quality_ = 0;    // LDACBT_EQMID_HQ
    int freq_    = 44100;
    static constexpr int kMtu = 895;
};

} // namespace owb
