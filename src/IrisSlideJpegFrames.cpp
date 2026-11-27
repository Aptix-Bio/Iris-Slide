//
//  IrisSlideJpegFrames.cpp
//  Iris
//
//  libjpeg-turbo 3 (tj3 API); the handle is owned by a std::unique_ptr with tj3Destroy, so a
//  refusal thrown at any step releases it.
//
#include "IrisSlideJpegFrames.hpp"

#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

#include <turbojpeg.h>

namespace IrisSlide {
namespace {
constexpr int SAMPLE_BITS = 8;
constexpr int STOP_ON_WARNING = 1;

[[noreturn]] void refuse (const std::string& what)
{
    throw std::runtime_error("JPEG stream refused: " + what);
}
/// libjpeg-turbo's pixel layout and bytes per pixel for @p format; throws naming any other format.
std::pair<TJPF, std::size_t> layout_of (Iris::Format format)
{
    switch (format) {
        case Iris::FORMAT_R8G8B8:   return {TJPF_RGB, RGB_BYTES};
        case Iris::FORMAT_B8G8R8:   return {TJPF_BGR, RGB_BYTES};
        case Iris::FORMAT_R8G8B8A8: return {TJPF_RGBA, RGBA_BYTES};
        case Iris::FORMAT_B8G8R8A8: return {TJPF_BGRA, RGBA_BYTES};
        case Iris::FORMAT_UNDEFINED: break;
    }
    refuse("pixel format " + std::to_string(int(format)) + " is not RGB, BGR, RGBA or BGRA");
}
} // namespace

// RTM Dicom_05
void decode_jpeg (std::span<const std::uint8_t> stream, FrameShape expected, Iris::Format format,
                  std::span<std::uint8_t> pixels)
{
    const auto [layout, pixel_bytes] = layout_of(format);
    std::unique_ptr<void, decltype(&tj3Destroy)> handle(tj3Init(TJINIT_DECOMPRESS), &tj3Destroy);
    if (!handle) refuse("libjpeg-turbo could not create a decompressor");
    // A warning (a truncated or corrupt stream libjpeg-turbo would otherwise pad) is an error.
    const int stop_set = tj3Set(handle.get(), TJPARAM_STOPONWARNING, STOP_ON_WARNING);
    if (stop_set != 0) refuse(tj3GetErrorStr(handle.get()));
    const int header_read = tj3DecompressHeader(handle.get(), stream.data(), stream.size());
    if (header_read != 0)
        refuse("its header did not read from " + std::to_string(stream.size()) + " bytes: " + tj3GetErrorStr(handle.get()));
    // D3: the size comes from the stream and is compared with what the caller declares.
    const int columns = tj3Get(handle.get(), TJPARAM_JPEGWIDTH), rows = tj3Get(handle.get(), TJPARAM_JPEGHEIGHT);
    if (columns < 0 || rows < 0 || std::uint32_t(columns) != expected.columns || std::uint32_t(rows) != expected.rows)
        refuse("its header is " + std::to_string(columns) + " x " + std::to_string(rows) + " where " +
               std::to_string(expected.columns) + " x " + std::to_string(expected.rows) + " is declared");
    const int precision = tj3Get(handle.get(), TJPARAM_PRECISION);
    if (precision != SAMPLE_BITS) refuse("its samples are " + std::to_string(precision) + "-bit where 8-bit are decoded");
    const std::size_t needed = std::size_t(columns) * std::size_t(rows) * pixel_bytes;
    if (pixels.size() != needed)
        refuse("the destination holds " + std::to_string(pixels.size()) + " bytes where " + std::to_string(needed) + " are needed");
    const int decoded = tj3Decompress8(handle.get(), stream.data(), stream.size(), pixels.data(), 0, layout);
    if (decoded != 0) refuse(std::string("its stream did not decode: ") + tj3GetErrorStr(handle.get()));
}

void decode_jpeg_frame (std::span<const std::uint8_t> stream, FrameShape expected, std::span<std::uint8_t> rgb)
{
    decode_jpeg(stream, expected, Iris::FORMAT_R8G8B8, rgb);
}
} // END IRIS SLIDE NAMESPACE
