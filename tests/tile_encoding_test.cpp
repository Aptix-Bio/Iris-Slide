// RTM Decode_09, Decode_10
// TileEncodingAv1Refused_3.6. Decided 2026-11-25: tile encoding 3 is
// TILE_ENCODING_AV1, a bare AV1 sequence in the Low Overhead Bitstream Format, and the AVIF
// tile encoding is removed. Iris-Slide neither decodes nor encodes AV1 tiles yet
// (development/ARCHITECTURE.md Q11), so a tile table that declares it is refused with an error
// naming the encoding, instead of handing the stream to a decoder of another format.
#include <array>
#include <cstdint>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "IrisSlidePriv.hpp"

using Catch::Matchers::ContainsSubstring;

namespace {
// An AV1 temporal delimiter OBU: header byte 0x12 (obu_type 2, obu_has_size_field set) and
// obu_size 0. The refusal comes before the stream is read, so its content only has to be
// non-empty.
constexpr std::array<std::uint8_t, 2> AV1_TEMPORAL_DELIMITER {0x12, 0x00};
constexpr std::uint32_t TILE_SIDE = 256;
constexpr std::uint32_t RGBA_BYTES = 4;
}

TEST_CASE("A tile declared TILE_ENCODING_AV1 is refused on decode, naming the encoding",
          "[Decode][Decode_09]")
{
    const IrisSlide::Context_t context {IrisSlide::ContextCreateInfo {}};
    const Iris::Buffer stream = Iris::Wrap_weak_buffer_fom_data
        (AV1_TEMPORAL_DELIMITER.data(), AV1_TEMPORAL_DELIMITER.size());
    REQUIRE_THROWS_WITH(context.decompress_tile({
                            .compressed     = stream,
                            .desiredFormat  = Iris::FORMAT_R8G8B8A8,
                            .encoding       = IrisSlide::TILE_ENCODING_AV1,
                        }),
                        ContainsSubstring("TILE_ENCODING_AV1"));
}

TEST_CASE("A tile requested as TILE_ENCODING_AV1 is refused on encode, naming the encoding",
          "[Encode][Decode_09]")
{
    const IrisSlide::Context_t context {IrisSlide::ContextCreateInfo {}};
    const Iris::Buffer pixels = Iris::Create_strong_buffer(TILE_SIDE * TILE_SIDE * RGBA_BYTES);
    REQUIRE_THROWS_WITH(context.compress_tile({
                            .pixelArray = pixels,
                            .format     = Iris::FORMAT_R8G8B8A8,
                            .encoding   = IrisSlide::TILE_ENCODING_AV1,
                        }),
                        ContainsSubstring("TILE_ENCODING_AV1"));
}

// TileEncodingBlocksRefused_3.7. Decided 2026-11-25: tile encodings 4 and 5 are the GPU
// block-compressed TILE_ENCODING_UASTC and TILE_ENCODING_XUASTC. Iris-Slide neither transcodes
// nor encodes them yet (development/ARCHITECTURE.md Q13, Q14), so a tile table that declares
// one is refused with an error naming it, as AV1 is.
namespace {
struct BlockEncoding {
    IrisSlide::Encoding encoding;
    const char*         name;
};
// The bytes of a Zstandard frame's magic number, little-endian (RFC 8878 section 3.1.1). The
// refusal comes before the stream is read, so its content only has to be non-empty.
constexpr std::array<std::uint8_t, 4> ZSTANDARD_MAGIC {0x28, 0xB5, 0x2F, 0xFD};
}

TEST_CASE("A tile declared TILE_ENCODING_UASTC or TILE_ENCODING_XUASTC is refused on decode "
          "and on encode, naming the encoding", "[Decode][Encode][Decode_10]")
{
    const auto block = GENERATE(BlockEncoding {IrisSlide::TILE_ENCODING_UASTC,  "TILE_ENCODING_UASTC"},
                                BlockEncoding {IrisSlide::TILE_ENCODING_XUASTC, "TILE_ENCODING_XUASTC"});
    const IrisSlide::Context_t context {IrisSlide::ContextCreateInfo {}};
    const Iris::Buffer stream = Iris::Wrap_weak_buffer_fom_data
        (ZSTANDARD_MAGIC.data(), ZSTANDARD_MAGIC.size());
    REQUIRE_THROWS_WITH(context.decompress_tile({
                            .compressed     = stream,
                            .desiredFormat  = Iris::FORMAT_R8G8B8A8,
                            .encoding       = block.encoding,
                        }),
                        ContainsSubstring(block.name));
    const Iris::Buffer pixels = Iris::Create_strong_buffer(TILE_SIDE * TILE_SIDE * RGBA_BYTES);
    REQUIRE_THROWS_WITH(context.compress_tile({
                            .pixelArray = pixels,
                            .format     = Iris::FORMAT_R8G8B8A8,
                            .encoding   = block.encoding,
                        }),
                        ContainsSubstring(block.name));
}
