// RTM Decode_09
// TileEncodingAv1Refused_3.6. Decided 2026-11-25: tile encoding 3 is
// TILE_ENCODING_AV1, a bare AV1 sequence in the Low Overhead Bitstream Format, and the AVIF
// tile encoding is removed. Iris-Slide neither decodes nor encodes AV1 tiles yet
// (development/ARCHITECTURE.md Q11), so a tile table that declares it is refused with an error
// naming the encoding, instead of handing the stream to a decoder of another format.
#include <array>
#include <cstdint>

#include <catch2/catch_test_macros.hpp>
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
