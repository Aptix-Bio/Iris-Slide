//
//  IrisSlideRetile.cpp
//  Iris
//
//  The re-cut's SIMD kernel (ARCHITECTURE D16): written once with Highway and compiled for every
//  target Highway supports on the platform by foreach_target.h, which includes this file once per
//  target; HWY_DYNAMIC_DISPATCH runs the best target the CPU offers. This translation unit is the
//  only one that includes Highway's per-target code, so Highway reaches no other source.
//
// Every target Highway can reach on this platform is compiled (on arm64 the NEON variants, SVE and
// the portable EMU128), so the every-target test covers more than the one the CPU picks.
#ifndef HWY_COMPILE_ALL_ATTAINABLE
#define HWY_COMPILE_ALL_ATTAINABLE
#endif
#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "IrisSlideRetile.cpp"
#include <hwy/foreach_target.h>   // must come before highway.h
#include <hwy/highway.h>

#include "IrisSlideRetile.hpp"

HWY_BEFORE_NAMESPACE();
namespace IrisSlide {
namespace HWY_NAMESPACE {
namespace hn = hwy::HWY_NAMESPACE;

// RTM Pyramid_04
/// Whole vectors: three planes loaded from interleaved RGB, stored interleaved with an opaque
/// fourth plane. The pixels left over after the last whole vector go one at a time.
void Rgb8ToRgba8 (const std::uint8_t* HWY_RESTRICT rgb, std::uint8_t* HWY_RESTRICT rgba, std::size_t pixels)
{
    const hn::ScalableTag<std::uint8_t> d;
    const std::size_t lanes = hn::Lanes(d);
    const auto opaque = hn::Set(d, std::uint8_t{0xFF});
    std::size_t i = 0;
    for (; i + lanes <= pixels; i += lanes) {
        hn::Vec<decltype(d)> r, g, b;
        hn::LoadInterleaved3(d, rgb + 3 * i, r, g, b);
        hn::StoreInterleaved4(r, g, b, opaque, d, rgba + 4 * i);
    }
    for (; i < pixels; ++i) {
        rgba[4 * i + 0] = rgb[3 * i + 0];
        rgba[4 * i + 1] = rgb[3 * i + 1];
        rgba[4 * i + 2] = rgb[3 * i + 2];
        rgba[4 * i + 3] = 0xFF;
    }
}

const char* ChosenTargetName ()
{
    return hwy::TargetName(HWY_TARGET);
}
} // namespace HWY_NAMESPACE
} // END IRIS SLIDE NAMESPACE
HWY_AFTER_NAMESPACE();

#if HWY_ONCE
namespace IrisSlide {
HWY_EXPORT(Rgb8ToRgba8);
HWY_EXPORT(ChosenTargetName);

namespace {
constexpr std::size_t RGB = 3, RGBA = 4;
std::size_t pixels_of (std::span<const std::uint8_t> rgb, std::span<std::uint8_t> rgba)
{
    if (rgb.size() % RGB != 0 || rgba.size() != rgb.size() / RGB * RGBA)
        throw std::runtime_error("RGB8 to RGBA8 needs 4 bytes for every 3: given " + std::to_string(rgb.size()) +
                                 " and " + std::to_string(rgba.size()));
    return rgb.size() / RGB;
}
} // namespace

void rgb8_to_rgba8 (std::span<const std::uint8_t> rgb, std::span<std::uint8_t> rgba)
{
    const std::size_t pixels = pixels_of(rgb, rgba);
    HWY_DYNAMIC_DISPATCH(Rgb8ToRgba8)(rgb.data(), rgba.data(), pixels);
}

void rgb8_to_rgba8_reference (std::span<const std::uint8_t> rgb, std::span<std::uint8_t> rgba)
{
    const std::size_t pixels = pixels_of(rgb, rgba);
    for (std::size_t i = 0; i < pixels; ++i) {
        rgba[RGBA * i + 0] = rgb[RGB * i + 0];
        rgba[RGBA * i + 1] = rgb[RGB * i + 1];
        rgba[RGBA * i + 2] = rgb[RGB * i + 2];
        rgba[RGBA * i + 3] = 0xFF;
    }
}

const char* rgb8_to_rgba8_target () noexcept
{
    return HWY_DYNAMIC_DISPATCH(ChosenTargetName)();
}

std::vector<std::int64_t> rgb8_to_rgba8_targets ()
{
    // HWY_TARGETS here is this file's, every attainable target (HWY_COMPILE_ALL_ATTAINABLE above).
    return hwy::SupportedAndGeneratedTargets();
}
} // END IRIS SLIDE NAMESPACE
#endif // HWY_ONCE
