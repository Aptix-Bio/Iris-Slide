// RTM Identity_07
// EncoderTakesI2S_6.8. Decided 2026-11-25: the encoder takes a slide's Iris
// Interoperability Specification (I2S) attributes from its caller, as plain key and value text
// with their format and version (EncodeSlideInfo::attributes), and writes them as the slide's
// METADATA_I2S attributes block in place of the pairs read from the source. Iris-Slide does not
// depend on I2S: whether a key belongs to I2S is I2S's check; the encoder refuses only what
// IFE's attribute blocks cannot hold, and an I2S claim with no version, naming the pair.
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <random>
#include <string>
#include <thread>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "IrisSlidePriv.hpp"
#include "IFE_Builder.hpp"

using Catch::Matchers::ContainsSubstring;

namespace {
constexpr std::uint32_t TILE_SIDE = 256;
constexpr std::uint32_t RGBA_BYTES = 4;
constexpr std::uint8_t GREY = 0x80;
// I2S 0.1 under the packing proposed in ARCHITECTURE Q12, major << 8 | minor. The packing is
// open in both specifications; the test names the value it passes, as any caller must.
constexpr std::uint16_t I2S_VERSION_0_1 = (0U << 8U) | 1U;
// A source property the fixture carries as free text, which the caller's pairs replace.
constexpr char SOURCE_KEY[] = "aperio.Filename";
constexpr auto ENCODER_POLL = std::chrono::milliseconds(10);

/// A directory of its own under the temp directory, removed with everything in it at scope exit.
struct ScratchDirectory {
    std::filesystem::path path;
    ScratchDirectory()
    {
        std::random_device entropy;
        path = std::filesystem::temp_directory_path() /
               ("IrisSlideTest_" + std::to_string(uint64_t{entropy()} << 32U | entropy()));
        std::filesystem::create_directories(path);
    }
    ScratchDirectory(const ScratchDirectory&) = delete;
    ScratchDirectory& operator=(const ScratchDirectory&) = delete;
    ~ScratchDirectory()
    {
        // Nothing to report: a test that failed may have left nothing to remove.
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
};

/// A one-tile JPEG slide whose attributes are one free-text source property, written through
/// IFE's Builder from generated pixels, so no slide file is checked in.
std::filesystem::path write_fixture_slide(const std::filesystem::path& directory)
{
    using namespace Iris::File::Serialization;
    const IrisSlide::Context_t context {IrisSlide::ContextCreateInfo {}};
    const Iris::Buffer pixels = Iris::Create_strong_buffer(TILE_SIDE * TILE_SIDE * RGBA_BYTES);
    std::memset(pixels->append(TILE_SIDE * TILE_SIDE * RGBA_BYTES), GREY, pixels->size());
    const Iris::Buffer tile = context.compress_tile({
        .pixelArray = pixels,
        .format     = Iris::FORMAT_R8G8B8A8,
        .encoding   = IrisSlide::TILE_ENCODING_JPEG,
    });

    const auto path = directory / "fixture.iris";
    const Iris::File::BuilderTileTableInfo table {
        .encoding = IrisSlide::TILE_ENCODING_JPEG,
        .format   = Iris::FORMAT_R8G8B8A8,
        .extent   = {.width = TILE_SIDE, .height = TILE_SIDE, .layers = {Iris::LayerExtent {}}},
    };
    auto builder = Iris::File::Builder::create({.filepath = path, .tile_frames = false});
    builder.set_tile_table(table);
    builder.append_tile(0, 0, static_cast<const Iris::File::BYTE*>(tile->data()), tile->size());

    const Iris::File::Offset offsets_at = builder->append(TileOffsetsCreateInfo {.entries = builder.tile_offsets()});
    const Iris::File::Offset extents_at = builder->append(LayerExtentsCreateInfo {
        .entries = {{.X_TILES = 1, .Y_TILES = 1, .SCALE = 1.F, .Z_PLANES = 0}}});
    const Iris::File::Offset table_at = builder->append(TileTableCreateInfo {
        .ENCODING             = Iris::File::constants::TileEncodings::TILE_ENCODING_JPEG,
        .FORMAT               = Iris::File::constants::PixelFormats::FORMAT_R8G8B8A8,
        .TILE_OFFSETS_OFFSET  = offsets_at,
        .LAYER_EXTENTS_OFFSET = extents_at,
        .X_EXTENT             = TILE_SIDE,
        .Y_EXTENT             = TILE_SIDE,
        .TILE_LENGTH          = TILE_SIDE});
    const std::vector<AttributeSizeEntry> pairs {{.key = SOURCE_KEY, .value = "fixture.svs"}};
    const Iris::File::Offset sizes_at = builder->append(AttributeSizesCreateInfo {.entries = pairs});
    const Iris::File::Offset bytes_at = builder->append(AttributeBytesCreateInfo {.entries = pairs});
    const Iris::File::Offset attributes_at = builder->append(AttributesCreateInfo {
        .FORMAT       = Iris::File::constants::MetadataFormats::METADATA_FREE_TEXT,
        .SIZES_OFFSET = sizes_at,
        .BYTES_OFFSET = bytes_at});
    const Iris::File::Offset metadata_at = builder->append(MetadataCreateInfo {.ATTRIBUTES_OFFSET = attributes_at});
    builder.finalize({.tileTable = table_at, .metadata = metadata_at});
    return path;
}

/// The pairs an I2S caller passes: what I2S::Attributes::to_pairs() gives, keyed by name, with
/// a value outside ASCII so the UTF-8 is shown carried byte for byte.
IrisSlide::Attributes i2s_attributes()
{
    IrisSlide::Attributes attributes;
    attributes.type    = IrisSlide::METADATA_I2S;
    attributes.version = I2S_VERSION_0_1;
    attributes["Manufacturer"]          = u8"Leica Biosystems Nußloch";
    attributes["ManufacturerModelName"] = u8"GT450";
    return attributes;
}

/// Encodes source into destination with the caller's attributes and waits for the encoder to
/// settle. Returns dispatch's Result; progress receives the final status and error text.
Iris::Result encode(const std::filesystem::path& source, const std::filesystem::path& destination,
                    const IrisSlide::Attributes& attributes, IrisSlide::EncoderProgress& progress)
{
    IrisSlide::EncodeSlideInfo info {
        .srcFilePath     = source.string(),
        .dstFilePath     = destination.string(),
        .desiredEncoding = IrisSlide::TILE_ENCODING_JPEG,
        .concurrency     = 1,
    };
    info.attributes = attributes;
    const IrisSlide::Encoder encoder = IrisSlide::create_encoder(info);
    REQUIRE(encoder);
    const Iris::Result dispatched = IrisSlide::dispatch_encoder(encoder);
    // The encoder runs on its own threads and publishes only a status to poll.
    while (encoder->get_status() == IrisSlide::ENCODER_ACTIVE)
        std::this_thread::sleep_for(ENCODER_POLL);
    REQUIRE(IrisSlide::get_encoder_progress(encoder, progress).succeeded());
    return dispatched;
}
}

TEST_CASE("The caller's I2S pairs are written as METADATA_I2S with their version, in place of the source's pairs",
          "[Encode][Identity_07]")
{
    const ScratchDirectory scratch;
    const auto source = write_fixture_slide(scratch.path);
    const auto output = scratch.path / "out";
    std::filesystem::create_directories(output);

    IrisSlide::EncoderProgress progress;
    REQUIRE(encode(source, output, i2s_attributes(), progress).succeeded());
    INFO(progress.errorMsg);
    REQUIRE(progress.status == IrisSlide::ENCODER_INACTIVE);

    const IrisSlide::Slide slide = IrisSlide::open_slide({.filePath = (output / "fixture.iris").string()});
    REQUIRE(slide);
    const IrisSlide::Attributes& written = slide->get_slide_info().metadata.attributes;
    CHECK(written.type == IrisSlide::METADATA_I2S);
    CHECK(written.version == I2S_VERSION_0_1);
    CHECK(static_cast<const std::unordered_map<std::string, std::u8string>&>(written) ==
          static_cast<const std::unordered_map<std::string, std::u8string>&>(i2s_attributes()));
    CHECK_FALSE(written.contains(SOURCE_KEY));
}

TEST_CASE("Attributes IFE cannot hold, or an I2S claim with no version, are refused naming the pair",
          "[Encode][Identity_07]")
{
    const ScratchDirectory scratch;
    const auto source = write_fixture_slide(scratch.path);
    const auto output = scratch.path / "out";
    std::filesystem::create_directories(output);

    IrisSlide::Attributes attributes = i2s_attributes();
    std::string named;
    SECTION("a key outside ASCII") {
        attributes[named = "Manufactur\xC3\xA9r"] = u8"Leica";
    }
    SECTION("a value that is not UTF-8") {
        // 0xC3 opens a two-byte sequence; 0x28 '(' cannot continue it.
        attributes[named = "DeviceSerialNumber"] = std::u8string {u8'\xC3', u8'\x28'};
    }
    SECTION("an I2S claim with no version") {
        attributes.version = 0;
        named = "METADATA_I2S";
    }
    SECTION("a format other than I2S") {
        attributes.type = IrisSlide::METADATA_FREE_TEXT;
        named = "METADATA_FREE_TEXT";
    }

    IrisSlide::EncoderProgress progress;
    const Iris::Result result = encode(source, output, attributes, progress);
    CHECK(result.failed());
    CHECK_THAT(result.message, ContainsSubstring(named));
    CHECK_FALSE(std::filesystem::exists(output / "fixture.iris"));
}
