// RTM Pyramid_01, Pyramid_02, Pyramid_03, Pyramid_04
// ReductionWeights_23.1, ScalarReduction_23.2, HighwayReduction_23.3 (ARCHITECTURE D15, D16):
// a 256-pixel tile of layer n+1 is reduced from the 518 x 518 region of layer n it reads (its
// 2 x 2 source tiles, 3 pixels before and 3 after) by a Mitchell-Netravali cubic widened by 2, in
// fixed-point arithmetic, so the scalar reference and every Highway target give the same bytes.
#include <cstdint>
#include <random>
#include <span>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <hwy/targets.h>

#include "IrisSlideReduce.hpp"

namespace {
constexpr std::uint32_t SEED = 20261127;
constexpr std::size_t RGBA = IrisSlide::RGBA_BYTES;
constexpr std::uint8_t WHITE = 0xFF;
constexpr std::size_t REGION_BYTES = std::size_t{IrisSlide::REDUCE_REGION} * IrisSlide::REDUCE_REGION * RGBA;

// The weights are a compile-time table; their sum is checked where they are made, and here.
static_assert(IrisSlide::REDUCE_WEIGHT_SUM == (1 << IrisSlide::REDUCE_WEIGHT_BITS));
} // namespace

TEST_CASE("The reduction's weights are symmetric, sum to the fixed-point unit and have negative lobes",
          "[Pyramid_01]")
{
    const auto& w = IrisSlide::REDUCE_WEIGHTS;
    std::int64_t sum = 0;
    for (std::size_t k = 0; k < w.size(); ++k) {
        sum += w[k];
        CHECK(w[k] == w[w.size() - 1 - k]);
    }
    CHECK(sum == (1 << IrisSlide::REDUCE_WEIGHT_BITS));
    CHECK(w.front() < 0);   // the cubic's outer taps are negative: it sharpens where a box blurs
}

TEST_CASE("A flat region reduces to the same flat tile", "[Pyramid_01][Pyramid_02]")
{
    std::vector<std::uint8_t> region(REGION_BYTES), tile(IrisSlide::TILE_BYTES);
    for (std::size_t i = 0; i < region.size(); i += RGBA) {
        region[i] = 12; region[i + 1] = 200; region[i + 2] = 255; region[i + 3] = WHITE;
    }
    IrisSlide::reduce_2x_reference(region, tile);
    std::size_t wrong = 0;
    for (std::size_t i = 0; i < tile.size(); i += RGBA)
        wrong += tile[i] != 12 || tile[i + 1] != 200 || tile[i + 2] != 255 || tile[i + 3] != WHITE;
    CHECK(wrong == 0);
}

TEST_CASE("A vertical step reduces to the cubic's response, with its overshoot clamped",
          "[Pyramid_02]")
{
    // Columns left of region column 3 + 256 (source x 256) are 0, the rest 255: the output's step
    // sits at output x 128, with the negative lobes overshooting each side before the clamp.
    std::vector<std::uint8_t> region(REGION_BYTES), tile(IrisSlide::TILE_BYTES);
    for (std::uint32_t y = 0; y < IrisSlide::REDUCE_REGION; ++y)
        for (std::uint32_t x = 0; x < IrisSlide::REDUCE_REGION; ++x)
            for (std::uint32_t c = 0; c < RGBA; ++c)
                region[(std::size_t{y} * IrisSlide::REDUCE_REGION + x) * RGBA + c] = x < 3 + 256 ? 0 : WHITE;
    IrisSlide::reduce_2x_reference(region, tile);
    const auto at = [&tile](std::uint32_t x) { return tile[(std::size_t{100} * IrisSlide::RECUT_TILE + x) * RGBA]; };
    CHECK(at(120) == 0);
    CHECK(at(136) == WHITE);
    CHECK(at(126) == 0);       // the undershoot clamps to 0
    CHECK(at(129) == WHITE);   // the overshoot clamps to 255
    CHECK(at(127) > 0);
    CHECK(at(127) < 128);
    CHECK(int(at(127)) + int(at(128)) == 255);   // the response is symmetric about the step
}

TEST_CASE("The Highway reduction equals the scalar reference on every compiled and supported target",
          "[Pyramid_04]")
{
    std::mt19937 random(SEED);
    std::uniform_int_distribution<int> byte(0, 255);
    std::vector<std::vector<std::uint8_t>> regions(3, std::vector<std::uint8_t>(REGION_BYTES));
    for (auto& region : regions)
        for (auto& b : region) b = static_cast<std::uint8_t>(byte(random));
    const auto targets = IrisSlide::reduce_2x_targets();
    REQUIRE_FALSE(targets.empty());
    std::string names;
    for (const auto target : targets) names += std::string(names.empty() ? "" : ", ") + hwy::TargetName(target);
    WARN("Highway targets tested: " << names);
    std::vector<std::uint8_t> simd(IrisSlide::TILE_BYTES), reference(IrisSlide::TILE_BYTES);
    for (const auto target : targets) {
        hwy::SetSupportedTargetsForTest(target);
        INFO("target " << hwy::TargetName(target));
        CHECK(std::string(IrisSlide::reduce_2x_target()) == hwy::TargetName(target));
        for (const auto& region : regions) {
            IrisSlide::reduce_2x(region, simd);
            IrisSlide::reduce_2x_reference(region, reference);
            CHECK(simd == reference);
        }
    }
    hwy::SetSupportedTargetsForTest(0);
}

TEST_CASE("At the slide's edge the edge pixel repeats, and beyond the next layer's extent is white",
          "[Pyramid_03]")
{
    // A 300 x 200 layer in two 256-pixel tiles across, one down: its 2x reduction is 150 x 100,
    // one tile. Pixels left of x = 150 are grey 100, the rest grey 200, so the edge column (299)
    // is 200 and a clamp repeats 200 where a white pad would pull the edge toward 255.
    constexpr std::uint32_t WIDTH = 300, HEIGHT = 200, SPLIT = WIDTH / 2;
    constexpr std::uint8_t LEFT_GREY = 100, RIGHT_GREY = 200;
    constexpr std::uint32_t TILE = IrisSlide::RECUT_TILE;
    constexpr std::size_t RGB = IrisSlide::RGB_BYTES;
    const IrisSlide::LayerSize layer {.width = WIDTH, .height = HEIGHT};
    std::vector<std::vector<std::uint8_t>> tiles(2, std::vector<std::uint8_t>(IrisSlide::TILE_BYTES, WHITE));
    for (std::uint32_t t = 0; t < 2; ++t)
        for (std::size_t pixel = 0; pixel < std::size_t{TILE} * HEIGHT; ++pixel) {
            const std::uint32_t x = t * TILE + static_cast<std::uint32_t>(pixel % TILE);
            if (x >= WIDTH) continue;
            for (std::uint32_t c = 0; c < RGB; ++c) tiles[t][pixel * RGBA + c] = x < SPLIT ? LEFT_GREY : RIGHT_GREY;
        }
    const auto tile_of = [&tiles](std::uint32_t x, std::uint32_t y) {
        return y == 0 && x < 2 ? std::span<const std::uint8_t>(tiles[x]) : std::span<const std::uint8_t>();
    };
    std::vector<std::uint8_t> region(REGION_BYTES), tile(IrisSlide::TILE_BYTES);
    IrisSlide::gather_region(layer, 0, 0, tile_of, region);
    IrisSlide::reduce_2x_reference(region, tile);
    IrisSlide::whiten_beyond(IrisSlide::reduced(layer), 0, 0, tile);
    const auto pixel = [&tile](std::uint32_t x, std::uint32_t y) { return tile[(std::size_t{y} * TILE + x) * RGBA]; };
    constexpr std::uint32_t REDUCED_WIDTH = WIDTH / 2, REDUCED_HEIGHT = HEIGHT / 2;
    CHECK(pixel(REDUCED_WIDTH - 1, 50) == RIGHT_GREY);   // the last column of the reduced layer, from the repeated edge
    CHECK(pixel(10, REDUCED_HEIGHT - 1) == LEFT_GREY);   // the last row, from the repeated bottom edge
    CHECK(pixel(REDUCED_WIDTH, 50) == WHITE);            // beyond the reduced layer's width
    CHECK(pixel(10, REDUCED_HEIGHT) == WHITE);           // beyond its height
}
