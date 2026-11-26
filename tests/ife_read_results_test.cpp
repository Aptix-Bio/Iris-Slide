// RTM Api_06
// IfeReadResults_12.5. IFE's read calls return a Result and write their value through a reference
// (IFE R-8, decided 2026-11-25). Iris-Slide's public calls that read a file through IFE return
// IFE's failure Result as it came, with IFE's domain, code and a message naming the offending
// value: a path IFE cannot map, a damaged block. A label no image carries fails naming the label.
// Decided 2026-11-26: an IFE failure thrown inside Iris-Slide (ResultFailure, from
// throw_if_failed) reaches the caller of a Result-returning public call with IFE's domain and code,
// instead of re-wrapped as a std::runtime_error under Iris-Slide's (ResultBoundaries_12.1).
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "IrisSlidePriv.hpp"
#include "IFE_Builder.hpp"

using Catch::Matchers::ContainsSubstring;

namespace {
constexpr std::uint32_t TILE_SIDE = 256;
constexpr std::uint8_t FILLER = 0xCD;          // tile bytes; nothing here decodes them
constexpr std::size_t STREAM_BYTES = 64;
constexpr std::uint8_t FLIP = 0xFF;            // every bit of the low byte of a VALIDATION word

/// A directory of its own under the temp directory, removed with everything in it at scope exit.
struct ScratchDirectory {
    std::filesystem::path path;
    ScratchDirectory()
    {
        std::random_device entropy;
        path = std::filesystem::temp_directory_path() /
               ("IrisSlideIfeTest_" + std::to_string(uint64_t{entropy()} << 32U | entropy()));
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

/// Where the fixture's TILE_TABLE landed, so a test can damage it.
struct Fixture {
    std::filesystem::path path;
    Iris::File::Offset    table = 0;
};

/// A one-tile slide written through IFE's Builder, so no slide file is checked in.
Fixture write_fixture_slide(const std::filesystem::path& directory)
{
    using namespace Iris::File::Serialization;
    Fixture fixture {.path = directory / "fixture.iris"};
    const Iris::File::BuilderTileTableInfo table {
        .encoding = IrisSlide::TILE_ENCODING_JPEG,
        .format   = Iris::FORMAT_R8G8B8A8,
        .extent   = {.width = TILE_SIDE, .height = TILE_SIDE, .layers = {Iris::LayerExtent {}}},
    };
    auto builder = Iris::File::Builder::create({.filepath = fixture.path, .tile_frames = false});
    builder.set_tile_table(table);
    const std::vector<Iris::File::BYTE> stream(STREAM_BYTES, FILLER);
    builder.append_tile(0, 0, stream.data(), stream.size());
    const Iris::File::Offset offsets_at = builder->append(TileOffsetsCreateInfo {.entries = builder.tile_offsets()});
    const Iris::File::Offset extents_at = builder->append(LayerExtentsCreateInfo {
        .entries = {{.X_TILES = 1, .Y_TILES = 1, .SCALE = 1.F, .Z_PLANES = 0}}});
    fixture.table = builder->append(TileTableCreateInfo {
        .ENCODING             = Iris::File::constants::TileEncodings::TILE_ENCODING_JPEG,
        .FORMAT               = Iris::File::constants::PixelFormats::FORMAT_R8G8B8A8,
        .TILE_OFFSETS_OFFSET  = offsets_at,
        .LAYER_EXTENTS_OFFSET = extents_at,
        .X_EXTENT             = TILE_SIDE,
        .Y_EXTENT             = TILE_SIDE,
        .TILE_LENGTH          = TILE_SIDE});
    const Iris::File::Offset metadata_at = builder->append(MetadataCreateInfo {});
    builder.finalize({.tileTable = fixture.table, .metadata = metadata_at});
    return fixture;
}

/// Flip the low byte of the TILE_TABLE's VALIDATION word in place.
void damage_tile_table(const Fixture& fixture)
{
    std::fstream file(fixture.path, std::ios::in | std::ios::out | std::ios::binary);
    file.seekg(static_cast<std::streamoff>(fixture.table));
    const auto low = static_cast<std::uint8_t>(file.get());
    file.seekp(static_cast<std::streamoff>(fixture.table));
    file.put(static_cast<char>(low ^ FLIP));
}

bool is_ife_failure(const Iris::Result& result, Iris::File::FileError code)
{
    return result.failed() && result.domain == Iris::Domain::IRIS_FILE_EXTENSION &&
           result.code_as<Iris::File::FileError>() == code;
}
}

TEST_CASE("A path IFE cannot map fails with IFE's NOT_MAPPED, naming the path", "[Api_06]")
{
    const ScratchDirectory scratch;
    const std::string missing = (scratch.path / "no_such_slide.iris").string();

    const Iris::Result tested = IrisSlide::is_iris_slide_file(missing);
    CHECK(is_ife_failure(tested, Iris::File::FileError::NOT_MAPPED));
    CHECK_THAT(tested.message, ContainsSubstring(missing));

    const Iris::Result validated = IrisSlide::validate_slide({.filePath = missing});
    CHECK(is_ife_failure(validated, Iris::File::FileError::NOT_MAPPED));
    CHECK_THAT(validated.message, ContainsSubstring(missing));
}

TEST_CASE("A damaged block fails validation with IFE's code, naming the block, its byte and the value it stores",
          "[Api_06]")
{
    const ScratchDirectory scratch;
    const Fixture fixture = write_fixture_slide(scratch.path);
    REQUIRE(IrisSlide::validate_slide({.filePath = fixture.path.string()}).succeeded());

    damage_tile_table(fixture);
    const Iris::Result validated = IrisSlide::validate_slide({.filePath = fixture.path.string()});
    CHECK(is_ife_failure(validated, Iris::File::FileError::BAD_VALIDATION));
    CHECK_THAT(validated.message, ContainsSubstring("TILE_TABLE.VALIDATION at byte " + std::to_string(fixture.table) +
                                                    " stores " + std::to_string(fixture.table ^ FLIP)));
    // The slide is refused at open, before any read reaches it.
    CHECK_FALSE(IrisSlide::open_slide({.filePath = fixture.path.string()}));
}

TEST_CASE("A label no associated image carries fails naming the label", "[Api_06]")
{
    const ScratchDirectory scratch;
    const Fixture fixture = write_fixture_slide(scratch.path);
    const IrisSlide::Slide slide = IrisSlide::open_slide({.filePath = fixture.path.string()});
    REQUIRE(slide);

    IrisSlide::AssociatedImageInfo info {.imageLabel = "no such label"};
    const Iris::Result read = IrisSlide::get_associated_image_info(slide, info);
    CHECK(read.failed());
    CHECK_THAT(read.message, ContainsSubstring("no such label"));
}

TEST_CASE("An IFE failure thrown inside Iris-Slide keeps IFE's domain, code and message", "[Api_06]")
{
    const ScratchDirectory scratch;
    const std::string missing = (scratch.path / "no_such_slide.iris").string();
    Iris::File::Parser parser;
    const Iris::Result opened = Iris::File::create_parser({.filepath = missing}, parser);
    REQUIRE(opened.failed());

    try {
        IrisSlide::throw_if_failed(opened);
        FAIL("throw_if_failed returned on a failure");
    } catch (const IrisSlide::ResultFailure& thrown) {
        const Iris::Result result = thrown.result();
        CHECK(is_ife_failure(result, Iris::File::FileError::NOT_MAPPED));
        CHECK_THAT(result.message, ContainsSubstring(missing));
        CHECK_THAT(thrown.what(), ContainsSubstring(missing));
    }
}
