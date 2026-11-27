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
constexpr std::uint8_t OPAQUE = 0xFF;
constexpr std::uint32_t RGB = 3;
constexpr std::uint32_t RGBA = 4;

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
    CHECK(rgba == std::vector<std::uint8_t>{1, 2, 3, OPAQUE, 4, 5, 6, OPAQUE});
}

TEST_CASE("240-pixel frames re-cut into 256-pixel tiles keep every pixel, white beyond the slide",
          "[Encode_10]")
{
    // A 600 x 500 slide in 240-pixel frames: 3 x 3 frames, 3 x 2 tiles, the last row and column
    // of tiles partly beyond the slide.
    const IrisSlide::SlideGrid grid {.width = 600, .height = 500, .frame = {.columns = 240, .rows = 240}};
    const std::uint32_t across = 3, down = 3;
    std::vector<std::vector<std::uint8_t>> frames(across * down);
    for (std::uint32_t fy = 0; fy < down; ++fy)
        for (std::uint32_t fx = 0; fx < across; ++fx) {
            auto& frame = frames[fy * across + fx];
            frame.resize(std::size_t{240} * 240 * RGB);
            for (std::uint32_t y = 0; y < 240; ++y)
                for (std::uint32_t x = 0; x < 240; ++x)
                    for (std::uint32_t c = 0; c < RGB; ++c)
                        frame[(std::size_t{y} * 240 + x) * RGB + c] = pattern(fx * 240 + x, fy * 240 + y, c);
        }
    const auto frame_of = [&frames, across](std::uint32_t fx, std::uint32_t fy) {
        return std::span<const std::uint8_t>(frames[fy * across + fx]);
    };
    CHECK(IrisSlide::tiles_across({.width = grid.width, .height = grid.height}) == 3);
    CHECK(IrisSlide::tiles_down({.width = grid.width, .height = grid.height}) == 2);
    std::vector<std::uint8_t> tile(std::size_t{IrisSlide::RECUT_TILE} * IrisSlide::RECUT_TILE * RGBA);
    std::size_t wrong = 0;
    for (std::uint32_t ty = 0; ty < 2; ++ty)
        for (std::uint32_t tx = 0; tx < 3; ++tx) {
            IrisSlide::recut_tile(grid, tx, ty, frame_of, tile);
            for (std::uint32_t y = 0; y < IrisSlide::RECUT_TILE; ++y)
                for (std::uint32_t x = 0; x < IrisSlide::RECUT_TILE; ++x) {
                    const std::uint64_t gx = std::uint64_t{tx} * 256 + x, gy = std::uint64_t{ty} * 256 + y;
                    const bool inside = gx < grid.width && gy < grid.height;
                    const auto* px = &tile[(std::size_t{y} * 256 + x) * RGBA];
                    for (std::uint32_t c = 0; c < RGB; ++c)
                        wrong += px[c] != (inside ? pattern(gx, gy, c) : WHITE);
                    wrong += px[3] != OPAQUE;
                }
        }
    CHECK(wrong == 0);
}
