#include <gtest/gtest.h>
#include <optional>
#include "codec_interface.h"

// Verify the ICodec interface header compiles cleanly
// and that a minimal concrete implementation satisfies the contract.
namespace {

class NullCodec final : public owb::ICodec {
public:
    std::string_view name() const noexcept override { return "null"; }

    std::ptrdiff_t encode(std::span<const int16_t>, std::span<uint8_t>) override {
        return 0;
    }

    bool set_param(owb::CodecParam) override { return false; }

    std::optional<int64_t> get_param(std::string_view) const override {
        return std::nullopt;
    }
};

} // namespace

TEST(CodecInterface, NullCodecSatisfiesContract) {
    NullCodec codec;
    EXPECT_EQ(codec.name(), "null");
    EXPECT_EQ(codec.get_param("anything"), std::nullopt);
    EXPECT_FALSE(codec.set_param({"anything", 0}));
}

TEST(CodecInterface, EncodeReturnsZeroOnEmptyInput) {
    NullCodec codec;
    std::vector<int16_t> input;
    std::vector<uint8_t> output(64);
    EXPECT_EQ(codec.encode(input, output), std::ptrdiff_t{0});
}
