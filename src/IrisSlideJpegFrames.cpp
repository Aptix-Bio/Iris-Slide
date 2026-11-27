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

#include <turbojpeg.h>

namespace IrisSlide {
namespace {
constexpr int SAMPLE_BITS = 8;
constexpr std::size_t RGB_BYTES_PER_PIXEL = 3;

[[noreturn]] void refuse (const std::string& what)
{
    throw std::runtime_error("JPEG frame refused: " + what);
}
} // namespace

// RTM Dicom_05
void decode_jpeg_frame (std::span<const std::uint8_t> stream, FrameShape expected, std::span<std::uint8_t> rgb)
{
    std::unique_ptr<void, decltype(&tj3Destroy)> handle(tj3Init(TJINIT_DECOMPRESS), &tj3Destroy);
    if (!handle) refuse("libjpeg-turbo could not create a decompressor");
    // A warning (a truncated or corrupt stream libjpeg-turbo would otherwise pad) is an error.
    if (tj3Set(handle.get(), TJPARAM_STOPONWARNING, 1) != 0) refuse(tj3GetErrorStr(handle.get()));
    if (tj3DecompressHeader(handle.get(), stream.data(), stream.size()) != 0)
        refuse("its header did not read from " + std::to_string(stream.size()) + " bytes: " + tj3GetErrorStr(handle.get()));
    // D3: the size comes from the stream and is compared with what the instance declares.
    const int columns = tj3Get(handle.get(), TJPARAM_JPEGWIDTH), rows = tj3Get(handle.get(), TJPARAM_JPEGHEIGHT);
    if (columns < 0 || rows < 0 || std::uint32_t(columns) != expected.columns || std::uint32_t(rows) != expected.rows)
        refuse("its header is " + std::to_string(columns) + " x " + std::to_string(rows) + " where the instance declares " +
               std::to_string(expected.columns) + " x " + std::to_string(expected.rows));
    const int precision = tj3Get(handle.get(), TJPARAM_PRECISION);
    if (precision != SAMPLE_BITS) refuse("its samples are " + std::to_string(precision) + "-bit where 8-bit are decoded");
    const std::size_t needed = std::size_t(columns) * std::size_t(rows) * RGB_BYTES_PER_PIXEL;
    if (rgb.size() != needed)
        refuse("the destination holds " + std::to_string(rgb.size()) + " bytes where " + std::to_string(needed) + " are needed");
    if (tj3Decompress8(handle.get(), stream.data(), stream.size(), rgb.data(), 0, TJPF_RGB) != 0)
        refuse(std::string("its stream did not decode: ") + tj3GetErrorStr(handle.get()));
}
} // END IRIS SLIDE NAMESPACE
