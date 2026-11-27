// RTM Encode_09, Encode_10, Encode_11, Encode_12, Concurrency_04
// EncoderRewrite_24 (ARCHITECTURE D14): the rebuilt encoder reads a Source, derives every lower
// layer by the 2x reduction until the slide fits one tile, compresses each tile, and writes them
// through IFE's Builder in a fixed order, so the same source and options give the same bytes; every
// resident buffer is charged to one byte budget; every call returns a Result.
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <span>
#include <string>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "IrisSlidePriv.hpp"
#include "IrisSlideEncoder_.hpp"
#include "IrisSlideReduce.hpp"

using Catch::Matchers::ContainsSubstring;
namespace Rebuilt = IrisSlide::Rebuilt;

namespace {
constexpr std::uint32_t RGBA = 4;
constexpr std::uint8_t WHITE = 0xFF;
constexpr double JPEG_PSNR_FLOOR_DB = 35.0;   // quality 90, 4:2:2 on a smooth pattern
constexpr auto POLL = std::chrono::milliseconds(20);
constexpr std::size_t TILE_BYTES = std::size_t{IrisSlide::RECUT_TILE} * IrisSlide::RECUT_TILE * RGBA;

/// A smooth synthetic slide with fine texture, white beyond its extent.
class PatternSource final : public IrisSlide::Source {
public:
    explicit PatternSource(IrisSlide::LayerSize size) : _size(size) {}
    IrisSlide::LayerSize size() const noexcept override { return _size; }
    const IrisSlide::SourceMeasurements& measurements() const noexcept override { return _measurements; }
    std::uint64_t read_scratch_bytes() const noexcept override { return 0; }
    void read_tile(std::uint32_t x, std::uint32_t y, std::span<std::uint8_t> rgba) override
    {
        for (std::uint32_t row = 0; row < IrisSlide::RECUT_TILE; ++row)
            for (std::uint32_t column = 0; column < IrisSlide::RECUT_TILE; ++column) {
                const std::uint64_t gx = std::uint64_t{x} * 256 + column, gy = std::uint64_t{y} * 256 + row;
                auto* px = &rgba[(std::size_t{row} * 256 + column) * RGBA];
                const bool inside = gx < _size.width && gy < _size.height;
                px[0] = inside ? static_cast<std::uint8_t>(64 + (gx / 16 + gy / 32) % 128) : WHITE;
                px[1] = inside ? static_cast<std::uint8_t>(32 + (gy * 3 / 64) % 160) : WHITE;
                px[2] = inside ? static_cast<std::uint8_t>(96 + (gx * 5 / 128) % 96) : WHITE;
                px[3] = WHITE;
            }
    }
private:
    IrisSlide::LayerSize _size;
    IrisSlide::SourceMeasurements _measurements {.microns_per_pixel = 0.25, .magnification = 40.0};
};

struct ScratchFolder {
    std::filesystem::path path;
    ScratchFolder()
    {
        path = std::filesystem::temp_directory_path() / ("IrisSlideTest_rewrite_" + std::to_string(std::random_device{}()));
        std::filesystem::create_directories(path);
    }
    ScratchFolder(const ScratchFolder&) = delete;
    ScratchFolder& operator=(const ScratchFolder&) = delete;
    ~ScratchFolder() { std::error_code ignored; std::filesystem::remove_all(path, ignored); }
};

/// A PatternSource whose tile (2, 3) cannot be read.
class FailingSource final : public IrisSlide::Source {
public:
    explicit FailingSource(IrisSlide::LayerSize size) : _pattern(size) {}
    IrisSlide::LayerSize size() const noexcept override { return _pattern.size(); }
    const IrisSlide::SourceMeasurements& measurements() const noexcept override { return _pattern.measurements(); }
    std::uint64_t read_scratch_bytes() const noexcept override { return 0; }
    void read_tile(std::uint32_t x, std::uint32_t y, std::span<std::uint8_t> rgba) override
    {
        if (x == 2 && y == 3) throw std::runtime_error("tile (2, 3) is unreadable");
        _pattern.read_tile(x, y, rgba);
    }
private:
    PatternSource _pattern;
};

/// The files @p folder holds, hidden ones included.
std::size_t files_in(const std::filesystem::path& folder)
{
    return static_cast<std::size_t>(std::distance(std::filesystem::directory_iterator(folder), std::filesystem::directory_iterator()));
}

/// Waits for @p encoder to leave ACTIVE and returns its last progress.
Rebuilt::EncoderProgress wait_for(const Rebuilt::Encoder& encoder)
{
    Rebuilt::EncoderProgress progress;
    do {
        std::this_thread::sleep_for(POLL);
        REQUIRE(Rebuilt::progress(encoder, progress).succeeded());
    } while (progress.state == Rebuilt::EncoderState::ACTIVE);
    return progress;
}

/// Encodes a PatternSource of @p size into @p folder and waits for it to end.
Rebuilt::EncoderProgress encode(IrisSlide::LayerSize size, const std::filesystem::path& folder,
                                std::uint64_t budget = 0, std::uint32_t concurrency = std::thread::hardware_concurrency())
{
    Rebuilt::Encoder encoder;
    const Rebuilt::EncoderDesc desc {.source = folder / "pattern.dcm", .destination = folder,
                                     .memory_budget = budget, .concurrency = concurrency};
    const auto created = Rebuilt::create_encoder(desc, std::make_unique<PatternSource>(size), encoder);
    INFO(created.message);
    REQUIRE(created.succeeded());
    const auto started = Rebuilt::dispatch(encoder);
    INFO(started.message);
    REQUIRE(started.succeeded());
    Rebuilt::EncoderProgress progress;
    do {
        std::this_thread::sleep_for(POLL);
        REQUIRE(Rebuilt::progress(encoder, progress).succeeded());
    } while (progress.state == Rebuilt::EncoderState::ACTIVE);
    INFO(progress.error);
    REQUIRE(progress.state == Rebuilt::EncoderState::DONE);
    return progress;
}

std::vector<char> bytes_of(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
} // namespace

TEST_CASE("The output is named for its source and tile encoding", "[Encode_09]")
{
    std::string name;
    REQUIRE(Rebuilt::output_name("slides/9a98aa37.dcm", IrisSlide::TILE_ENCODING_JPEG, name).succeeded());
    CHECK(name == "9a98aa37.jpeg.iris");
    CHECK_THAT(Rebuilt::output_name("slides/9a98aa37.dcm", IrisSlide::TILE_ENCODING_AV1, name).message,
               ContainsSubstring("TILE_ENCODING_AV1"));
}

TEST_CASE("create_encoder refuses what it cannot encode, naming the value", "[Encode_09]")
{
    const ScratchFolder folder;
    Rebuilt::Encoder encoder;
    const auto source = [] { return std::make_unique<PatternSource>(IrisSlide::LayerSize{.width = 600, .height = 600}); };
    Rebuilt::EncoderDesc desc {.source = folder.path / "s.dcm", .destination = folder.path};
    desc.encoding = IrisSlide::TILE_ENCODING_AV1;
    CHECK_THAT(Rebuilt::create_encoder(desc, source(), encoder).message, ContainsSubstring("TILE_ENCODING_AV1"));
    desc.encoding = IrisSlide::TILE_ENCODING_JPEG;
    desc.quality = 0;
    CHECK_THAT(Rebuilt::create_encoder(desc, source(), encoder).message, ContainsSubstring("quality 0"));
    desc.quality = IrisSlide::QUALITY_DEFAULT;
    desc.destination = folder.path / "missing";
    CHECK_THAT(Rebuilt::create_encoder(desc, source(), encoder).message, ContainsSubstring("missing"));
    desc.destination = folder.path;
    desc.memory_budget = 1024;
    CHECK_THAT(Rebuilt::create_encoder(desc, source(), encoder).message, ContainsSubstring("1024"));
    desc.source = folder.path / "absent.dcm";
    desc.memory_budget = 0;
    CHECK_THAT(Rebuilt::create_encoder(desc, encoder).message, ContainsSubstring("absent.dcm"));
}

TEST_CASE("An encoder dispatched twice refuses the second dispatch by name", "[Encode_09]")
{
    const ScratchFolder folder;
    Rebuilt::Encoder encoder;
    const Rebuilt::EncoderDesc desc {.source = folder.path / "s.dcm", .destination = folder.path};
    REQUIRE(Rebuilt::create_encoder(desc, std::make_unique<PatternSource>(IrisSlide::LayerSize{.width = 600, .height = 600}), encoder).succeeded());
    REQUIRE(Rebuilt::dispatch(encoder).succeeded());
    const auto again = Rebuilt::dispatch(encoder);
    CHECK(again.failed());
    CHECK_THAT(again.message, ContainsSubstring("dispatched"));
    Rebuilt::EncoderProgress progress;
    do {
        std::this_thread::sleep_for(POLL);
        REQUIRE(Rebuilt::progress(encoder, progress).succeeded());
    } while (progress.state == Rebuilt::EncoderState::ACTIVE);
    CHECK(progress.state == Rebuilt::EncoderState::DONE);
}

TEST_CASE("A 9-tile-wide slide gets 4 derived layers, the last one tile, its base close to the source",
          "[Encode_10][Concurrency_04]")
{
    const ScratchFolder folder;
    const IrisSlide::LayerSize size {.width = 2300, .height = 2100};
    const auto done = encode(size, folder.path);
    const auto slide = IrisSlide::open_slide({.filePath = done.file.string()});
    REQUIRE(slide);
    IrisSlide::SlideInfo info;
    REQUIRE(IrisSlide::get_slide_info(slide, info).succeeded());
    REQUIRE(info.extent.layers.size() == 5);
    const std::vector<std::pair<std::uint32_t, std::uint32_t>> tiles {{1, 1}, {2, 2}, {3, 3}, {5, 5}, {9, 9}};
    for (std::size_t l = 0; l < tiles.size(); ++l) {
        CHECK(info.extent.layers[l].xTiles == tiles[l].first);
        CHECK(info.extent.layers[l].yTiles == tiles[l].second);
    }
    CHECK(info.metadata.micronsPerPixel > 0.f);
    PatternSource source(size);
    std::vector<std::uint8_t> expected(TILE_BYTES);
    source.read_tile(4, 4, expected);
    const auto decoded = IrisSlide::read_slide_tile({.slide = slide, .layerIndex = 4, .tileIndex = 4 * 9 + 4,
                                                     .desiredFormat = Iris::FORMAT_R8G8B8A8});
    REQUIRE(decoded);
    const auto* got = static_cast<const std::uint8_t*>(decoded->data());
    double squared = 0;
    for (std::size_t i = 0; i < expected.size(); ++i)
        if (i % RGBA != 3) squared += (double(expected[i]) - got[i]) * (double(expected[i]) - got[i]);
    const double psnr = 10.0 * std::log10(255.0 * 255.0 / (squared / (expected.size() * 3 / 4)));
    INFO("PSNR " << psnr << " dB");
    CHECK(psnr >= JPEG_PSNR_FLOOR_DB);
}

TEST_CASE("The same source and options give a byte-identical file", "[Encode_12]")
{
    const ScratchFolder first, second;
    const IrisSlide::LayerSize size {.width = 2300, .height = 2100};
    const auto a = encode(size, first.path), b = encode(size, second.path);
    const auto bytes_a = bytes_of(a.file), bytes_b = bytes_of(b.file);
    REQUIRE_FALSE(bytes_a.empty());
    CHECK(bytes_a == bytes_b);
}

TEST_CASE("A slide whose decoded size is over 4x the budget encodes within the budget", "[Encode_11]")
{
    const ScratchFolder folder;
    const IrisSlide::LayerSize size {.width = 2300, .height = 20400};   // 9 x 80 tiles, 189 MB decoded
    constexpr std::uint64_t BUDGET = 40ull << 20;
    constexpr std::uint32_t WORKERS = 2;
    const auto done = encode(size, folder.path, BUDGET, WORKERS);
    INFO("peak " << done.peak_resident_bytes << " of " << BUDGET);
    CHECK(done.peak_resident_bytes <= BUDGET);
    CHECK(done.peak_resident_bytes > BUDGET / 4);   // the budget was charged, and used
    CHECK(std::uint64_t{9} * 80 * TILE_BYTES > 4 * BUDGET);
}

TEST_CASE("An interrupted encode ends INTERRUPTED and leaves no file once dropped", "[Encode_09]")
{
    const ScratchFolder folder;
    Rebuilt::Encoder encoder;
    const Rebuilt::EncoderDesc desc {.source = folder.path / "s.dcm", .destination = folder.path, .concurrency = 1};
    REQUIRE(Rebuilt::create_encoder(desc, std::make_unique<PatternSource>(IrisSlide::LayerSize{.width = 2300, .height = 20400}), encoder).succeeded());
    REQUIRE(Rebuilt::dispatch(encoder).succeeded());
    REQUIRE(Rebuilt::interrupt(encoder).succeeded());
    CHECK(wait_for(encoder).state == Rebuilt::EncoderState::INTERRUPTED);
    encoder.reset();
    CHECK(files_in(folder.path) == 0);
}

TEST_CASE("Dropping an active encoder stops it and leaves no file", "[Encode_09]")
{
    const ScratchFolder folder;
    Rebuilt::Encoder encoder;
    const Rebuilt::EncoderDesc desc {.source = folder.path / "s.dcm", .destination = folder.path, .concurrency = 1};
    REQUIRE(Rebuilt::create_encoder(desc, std::make_unique<PatternSource>(IrisSlide::LayerSize{.width = 2300, .height = 20400}), encoder).succeeded());
    REQUIRE(Rebuilt::dispatch(encoder).succeeded());
    encoder.reset();
    CHECK(files_in(folder.path) == 0);
}

TEST_CASE("A tile that cannot be read fails the encode naming it, leaving no file", "[Encode_09]")
{
    const ScratchFolder folder;
    Rebuilt::Encoder encoder;
    const Rebuilt::EncoderDesc desc {.source = folder.path / "s.dcm", .destination = folder.path};
    REQUIRE(Rebuilt::create_encoder(desc, std::make_unique<FailingSource>(IrisSlide::LayerSize{.width = 2300, .height = 2100}), encoder).succeeded());
    REQUIRE(Rebuilt::dispatch(encoder).succeeded());
    const auto ended = wait_for(encoder);
    CHECK(ended.state == Rebuilt::EncoderState::FAILED);
    CHECK_THAT(ended.error, ContainsSubstring("tile (2, 3)"));
    encoder.reset();
    CHECK(files_in(folder.path) == 0);
}

TEST_CASE("A dispatch that cannot make its file fails, and the encoder can still be interrupted and dropped", "[Encode_09]")
{
    const ScratchFolder folder;
    const auto gone = folder.path / "gone";
    std::filesystem::create_directories(gone);
    Rebuilt::Encoder encoder;
    const Rebuilt::EncoderDesc desc {.source = folder.path / "s.dcm", .destination = gone};
    REQUIRE(Rebuilt::create_encoder(desc, std::make_unique<PatternSource>(IrisSlide::LayerSize{.width = 600, .height = 600}), encoder).succeeded());
    std::filesystem::remove(gone);
    CHECK(Rebuilt::dispatch(encoder).failed());
    Rebuilt::EncoderProgress progress;
    REQUIRE(Rebuilt::progress(encoder, progress).succeeded());
    CHECK(progress.state == Rebuilt::EncoderState::FAILED);
    CHECK(Rebuilt::interrupt(encoder).failed());   // refused by name: there is no active encode
    encoder.reset();
}

TEST_CASE("One worker and every worker give the same bytes", "[Encode_12]")
{
    const ScratchFolder one, many;
    const IrisSlide::LayerSize size {.width = 2300, .height = 2100};
    const auto a = encode(size, one.path, 0, 1), b = encode(size, many.path);
    CHECK(bytes_of(a.file) == bytes_of(b.file));
}

TEST_CASE("Slides of one tile, one row and odd sizes get every layer down to one tile", "[Encode_10]")
{
    const std::vector<std::pair<IrisSlide::LayerSize, std::size_t>> cases {
        {{.width = 200, .height = 150}, 1}, {{.width = 5000, .height = 100}, 6}, {{.width = 257, .height = 257}, 2}};
    for (const auto& [size, layers] : cases) {
        const ScratchFolder folder;
        INFO(size.width << " x " << size.height);
        const auto done = encode(size, folder.path);
        const auto slide = IrisSlide::open_slide({.filePath = done.file.string()});
        REQUIRE(slide);
        IrisSlide::SlideInfo info;
        REQUIRE(IrisSlide::get_slide_info(slide, info).succeeded());
        CHECK(info.extent.layers.size() == layers);
        CHECK(info.extent.layers.front().xTiles == 1);
        CHECK(info.extent.layers.front().yTiles == 1);
    }
}

TEST_CASE("Measurements are written normalised to the lowest layer", "[Encode_10]")
{
    // 5 layers: the lowest is 16x smaller, so 0.25 um/px becomes 4 and 40x becomes 2.5x.
    const ScratchFolder folder;
    const auto done = encode({.width = 2300, .height = 2100}, folder.path);
    const auto slide = IrisSlide::open_slide({.filePath = done.file.string()});
    REQUIRE(slide);
    IrisSlide::SlideInfo info;
    REQUIRE(IrisSlide::get_slide_info(slide, info).succeeded());
    CHECK(info.metadata.micronsPerPixel == 4.0f);
    CHECK(info.metadata.magnification == 2.5f);
}

TEST_CASE("A derived tile is the reference reduction of the source, within JPEG's loss", "[Pyramid_02]")
{
    const ScratchFolder folder;
    const IrisSlide::LayerSize size {.width = 2300, .height = 2100};
    const auto done = encode(size, folder.path);
    PatternSource source(size);
    std::vector<std::vector<std::uint8_t>> tiles(9 * 9, std::vector<std::uint8_t>(TILE_BYTES));
    for (std::uint32_t y = 0; y < 9; ++y)
        for (std::uint32_t x = 0; x < 9; ++x) source.read_tile(x, y, tiles[y * 9 + x]);
    std::vector<std::uint8_t> region(std::size_t{IrisSlide::REDUCE_REGION} * IrisSlide::REDUCE_REGION * RGBA), expected(TILE_BYTES);
    IrisSlide::gather_region(size, 2, 1, [&tiles](std::uint32_t x, std::uint32_t y) {
        return std::span<const std::uint8_t>(tiles[y * 9 + x]); }, region);
    IrisSlide::reduce_2x_reference(region, expected);
    IrisSlide::whiten_beyond(IrisSlide::reduced(size), 2, 1, expected);
    const auto slide = IrisSlide::open_slide({.filePath = done.file.string()});
    REQUIRE(slide);
    const auto decoded = IrisSlide::read_slide_tile({.slide = slide, .layerIndex = 3, .tileIndex = 1 * 5 + 2,
                                                     .desiredFormat = Iris::FORMAT_R8G8B8A8});
    REQUIRE(decoded);
    const auto* got = static_cast<const std::uint8_t*>(decoded->data());
    double squared = 0;
    for (std::size_t i = 0; i < expected.size(); ++i)
        if (i % RGBA != 3) squared += (double(expected[i]) - got[i]) * (double(expected[i]) - got[i]);
    const double psnr = 10.0 * std::log10(255.0 * 255.0 / (squared / (expected.size() * 3 / 4)));
    INFO("PSNR " << psnr << " dB");
    CHECK(psnr >= JPEG_PSNR_FLOOR_DB);
}
