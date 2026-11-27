// RTM Encode_10, Pyramid_04
// Retiled256_24.7, brought forward 2026-11-27 (ARCHITECTURE D16, D18): a source whose frames are
// not 256 pixels is cut into 256-pixel tiles, each assembled from the frames it overlaps; the row
// spans are converted from the frames' RGB8 to the tiles' RGBA8 by a Highway kernel chosen at run
// time, which equals the scalar reference on every target the build compiled and the CPU runs.
#include <cstddef>
#include <cstdint>
#include <random>
#include <span>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <hwy/targets.h>

#include "IrisSlideRetile.hpp"

namespace {
constexpr std::uint32_t SEED = 20261127;
constexpr std::uint8_t WHITE = 0xFF;
constexpr std::uint8_t ALPHA_OPAQUE = 0xFF;   // named so: <wingdi.h> defines OPAQUE as a macro
constexpr std::uint32_t RGB = 3;
constexpr std::uint32_t RGBA = 4;
constexpr std::uint32_t FRAME = 240;   // the source frame side the 256-pixel tiles are re-cut from
constexpr std::uint32_t TILE = IrisSlide::RECUT_TILE;

std::vector<std::uint8_t> random_bytes(std::size_t count, std::mt19937& random)
{
    std::uniform_int_distribution<int> byte(0, 255);
    std::vector<std::uint8_t> bytes(count);
    for (auto& b : bytes) b = static_cast<std::uint8_t>(byte(random));
    return bytes;
}

/// A synthetic slide's pixel at (x, y): distinct in every channel, so a misplaced span shows.
std::uint8_t pattern(std::uint64_t x, std::uint64_t y, std::uint32_t channel)
{
    return static_cast<std::uint8_t>((x * 7 + y * 13 + channel * 101) % 251);
}

/// Frame (fx, fy) of the synthetic slide: FRAME x FRAME RGB8 pixels of pattern().
std::vector<std::uint8_t> pattern_frame(std::uint32_t fx, std::uint32_t fy)
{
    std::vector<std::uint8_t> frame(std::size_t{FRAME} * FRAME * RGB);
    for (std::size_t i = 0; i < frame.size(); ++i) {
        const std::size_t pixel = i / RGB;
        frame[i] = pattern(std::uint64_t{fx} * FRAME + pixel % FRAME, std::uint64_t{fy} * FRAME + pixel / FRAME,
                           static_cast<std::uint32_t>(i % RGB));
    }
    return frame;
}

/// The values of tile (tx, ty) that differ from the pattern inside the slide, from white beyond
/// it, or from opaque alpha.
std::size_t wrong_values(const std::vector<std::uint8_t>& tile, const IrisSlide::SlideGrid& grid,
                         std::uint32_t tx, std::uint32_t ty)
{
    std::size_t wrong = 0;
    for (std::size_t pixel = 0; pixel < std::size_t{TILE} * TILE; ++pixel) {
        const std::uint64_t gx = std::uint64_t{tx} * TILE + pixel % TILE, gy = std::uint64_t{ty} * TILE + pixel / TILE;
        const bool inside = gx < grid.width && gy < grid.height;
        const auto* px = &tile[pixel * RGBA];
        for (std::uint32_t c = 0; c < RGB; ++c) wrong += px[c] != (inside ? pattern(gx, gy, c) : WHITE);
        wrong += px[RGB] != ALPHA_OPAQUE;
    }
    return wrong;
}
} // namespace

TEST_CASE("RGB8 to RGBA8 equals the scalar reference on every compiled and supported target",
          "[Encode_10][Pyramid_04]")
{
    std::mt19937 random(SEED);
    const std::vector<std::size_t> lengths {0, 1, 2, 15, 16, 17, 31, 63, 64, 65, 239, 240, 255, 256, 1000};
    const auto targets = IrisSlide::rgb8_to_rgba8_targets();
    REQUIRE_FALSE(targets.empty());
    std::string names;
    for (const auto target : targets) names += std::string(names.empty() ? "" : ", ") + hwy::TargetName(target);
    WARN("Highway targets tested: " << names);
    for (const auto target : targets) {
        hwy::SetSupportedTargetsForTest(target);
        INFO("target " << hwy::TargetName(target));
        CHECK(std::string(IrisSlide::rgb8_to_rgba8_target()) == hwy::TargetName(target));
        for (const auto pixels : lengths) {
            const auto rgb = random_bytes(pixels * RGB, random);
            std::vector<std::uint8_t> simd(pixels * RGBA, 0), reference(pixels * RGBA, 0);
            IrisSlide::rgb8_to_rgba8(rgb, simd);
            IrisSlide::rgb8_to_rgba8_reference(rgb, reference);
            INFO(pixels << " pixels");
            CHECK(simd == reference);
        }
    }
    hwy::SetSupportedTargetsForTest(0);   // back to the CPU's own targets
}

TEST_CASE("The scalar reference makes every pixel opaque and keeps its channels", "[Encode_10]")
{
    const std::vector<std::uint8_t> rgb {1, 2, 3, 4, 5, 6};
    std::vector<std::uint8_t> rgba(8, 0);
    IrisSlide::rgb8_to_rgba8_reference(rgb, rgba);
    CHECK(rgba == std::vector<std::uint8_t>{1, 2, 3, ALPHA_OPAQUE, 4, 5, 6, ALPHA_OPAQUE});
}

TEST_CASE("240-pixel frames re-cut into 256-pixel tiles keep every pixel, white beyond the slide",
          "[Encode_10]")
{
    // A 600 x 500 slide in 240-pixel frames: 3 x 3 frames, 3 x 2 tiles, the last row and column
    // of tiles partly beyond the slide.
    const IrisSlide::SlideGrid grid {.width = 600, .height = 500, .frame = {.columns = FRAME, .rows = FRAME}};
    const std::uint32_t across = 3, down = 3;
    std::vector<std::vector<std::uint8_t>> frames;
    for (std::uint32_t fy = 0; fy < down; ++fy)
        for (std::uint32_t fx = 0; fx < across; ++fx) frames.push_back(pattern_frame(fx, fy));
    const auto frame_of = [&frames, across](std::uint32_t fx, std::uint32_t fy) {
        return std::span<const std::uint8_t>(frames[fy * across + fx]);
    };
    CHECK(IrisSlide::tiles_across({.width = grid.width, .height = grid.height}) == 3);
    CHECK(IrisSlide::tiles_down({.width = grid.width, .height = grid.height}) == 2);
    std::vector<std::uint8_t> tile(std::size_t{TILE} * TILE * RGBA);
    std::size_t wrong = 0;
    for (std::uint32_t ty = 0; ty < 2; ++ty)
        for (std::uint32_t tx = 0; tx < 3; ++tx) {
            IrisSlide::recut_tile(grid, tx, ty, frame_of, tile);
            wrong += wrong_values(tile, grid, tx, ty);
        }
    CHECK(wrong == 0);
}
