//
//  IrisSlideOpenJpeg.cpp
//  Iris
//
//  OpenJPEG 2.5.4 (BSD-2-Clause) decodes JPEG 2000 and, since 2.5.0, HTJ2K codestreams. Every
//  OpenJPEG object is owned by a std::unique_ptr with OpenJPEG's own destroy function, so a
//  refusal thrown at any step releases what was made before it.
//
#include "IrisSlideOpenJpeg.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>

#include <openjpeg.h>

namespace IrisSlide {
namespace {
constexpr std::uint32_t RGB_COMPONENTS = 3;
constexpr OPJ_UINT32 SAMPLE_BITS = 8;
// ISO/IEC 15444-1 Annex I.5.1: a JP2 file opens with the 12-byte JPEG 2000 Signature box;
// anything else is taken as a raw codestream (DICOM encapsulates raw codestreams, PS3.5 A.4.4).
constexpr std::array<std::uint8_t, 12> JP2_SIGNATURE {0x00, 0x00, 0x00, 0x0C, 0x6A, 0x50,
                                                      0x20, 0x20, 0x0D, 0x0A, 0x87, 0x0A};
// OpenJPEG's read callback signals the end of the stream with (OPJ_SIZE_T)-1 (openjpeg.h,
// opj_stream_read_fn).
constexpr OPJ_SIZE_T STREAM_END = static_cast<OPJ_SIZE_T>(-1);

using Codec  = std::unique_ptr<opj_codec_t,  decltype(&opj_destroy_codec)>;
using Stream = std::unique_ptr<opj_stream_t, decltype(&opj_stream_destroy)>;
using Image  = std::unique_ptr<opj_image_t,  decltype(&opj_image_destroy)>;

/// The codestream OpenJPEG reads from: the caller's bytes and the read position.
struct MemorySource {
    std::span<const std::uint8_t> bytes;
    std::size_t                   position = 0;
};
OPJ_SIZE_T read_source (void* buffer, OPJ_SIZE_T count, void* user)
{
    auto& source = *static_cast<MemorySource*>(user);
    const std::size_t left = source.bytes.size() - source.position;
    if (left == 0) return STREAM_END;
    const std::size_t taken = std::min<std::size_t>(count, left);
    std::memcpy(buffer, source.bytes.data() + source.position, taken);
    source.position += taken;
    return taken;
}
OPJ_OFF_T skip_source (OPJ_OFF_T count, void* user)
{
    auto& source = *static_cast<MemorySource*>(user);
    if (count < 0) return -1;
    const std::size_t left    = source.bytes.size() - source.position;
    const std::size_t skipped = std::min<std::size_t>(static_cast<std::size_t>(count), left);
    source.position += skipped;
    return static_cast<OPJ_OFF_T>(skipped);
}
OPJ_BOOL seek_source (OPJ_OFF_T offset, void* user)
{
    auto& source = *static_cast<MemorySource*>(user);
    if (offset < 0 || static_cast<std::size_t>(offset) > source.bytes.size()) return OPJ_FALSE;
    source.position = static_cast<std::size_t>(offset);
    return OPJ_TRUE;
}
/// OpenJPEG reports errors through a handler; this one keeps the text for the refusal.
void keep_error (const char* message, void* user)
{
    static_cast<std::string*>(user)->append(message ? message : "");
}

[[noreturn]] void refuse (const std::string& what, const std::string& openjpeg_errors)
{
    throw std::runtime_error("JPEG 2000 frame refused: " + what +
                             (openjpeg_errors.empty() ? "" : " (OpenJPEG: " + openjpeg_errors + ")"));
}
std::string shape_text (std::uint64_t columns, std::uint64_t rows)
{
    return std::to_string(columns) + " x " + std::to_string(rows);
}
} // namespace

// RTM Dicom_05
void decode_jpeg2000_frame (std::span<const std::uint8_t> codestream,
                            FrameShape expected,
                            std::span<std::uint8_t> rgb)
{
    const bool jp2 = codestream.size() >= JP2_SIGNATURE.size() &&
        std::equal(JP2_SIGNATURE.begin(), JP2_SIGNATURE.end(), codestream.begin());
    std::string errors;
    Codec codec(opj_create_decompress(jp2 ? OPJ_CODEC_JP2 : OPJ_CODEC_J2K), &opj_destroy_codec);
    if (!codec) refuse("OpenJPEG could not create a decoder", errors);
    opj_set_error_handler(codec.get(), keep_error, &errors);
    opj_dparameters_t parameters;
    opj_set_default_decoder_parameters(&parameters);
    if (!opj_setup_decoder(codec.get(), &parameters)) refuse("the decoder could not be set up", errors);
    // Strict mode makes a truncated codestream an error; by default OpenJPEG decodes what is there.
    if (!opj_decoder_set_strict_mode(codec.get(), OPJ_TRUE)) refuse("strict mode could not be set", errors);

    MemorySource source {.bytes = codestream};
    Stream stream(opj_stream_create(OPJ_J2K_STREAM_CHUNK_SIZE, OPJ_TRUE), &opj_stream_destroy);
    if (!stream) refuse("OpenJPEG could not create a stream", errors);
    opj_stream_set_user_data(stream.get(), &source, nullptr);
    opj_stream_set_user_data_length(stream.get(), codestream.size());
    opj_stream_set_read_function(stream.get(), read_source);
    opj_stream_set_skip_function(stream.get(), skip_source);
    opj_stream_set_seek_function(stream.get(), seek_source);

    opj_image_t* header = nullptr;
    if (!opj_read_header(stream.get(), codec.get(), &header))
        refuse("its header could not be read from " + std::to_string(codestream.size()) + " bytes", errors);
    const Image image(header, &opj_image_destroy);

    // D3: the size comes from the codestream and is compared with what the instance declares.
    const std::uint64_t columns = image->x1 - image->x0;
    const std::uint64_t rows    = image->y1 - image->y0;
    if (columns != expected.columns || rows != expected.rows)
        refuse("its header is " + shape_text(columns, rows) + " where the instance declares " +
               shape_text(expected.columns, expected.rows), errors);
    if (image->numcomps != RGB_COMPONENTS)
        refuse("it has " + std::to_string(image->numcomps) + " components where RGB needs 3", errors);
    for (OPJ_UINT32 c = 0; c < image->numcomps; ++c) {
        const auto& component = image->comps[c];
        if (component.prec != SAMPLE_BITS || component.sgnd != 0 || component.dx != 1 || component.dy != 1)
            refuse("component " + std::to_string(c) + " is " + std::to_string(component.prec) + "-bit, " +
                   (component.sgnd ? "signed" : "unsigned") + ", subsampled " + std::to_string(component.dx) +
                   " x " + std::to_string(component.dy) + " where 8-bit unsigned at full resolution is decoded",
                   errors);
    }
    const std::uint64_t needed = columns * rows * RGB_COMPONENTS;
    if (rgb.size() != needed)
        refuse("the destination holds " + std::to_string(rgb.size()) + " bytes where " +
               shape_text(columns, rows) + " RGB8 needs " + std::to_string(needed), errors);

    if (!opj_decode(codec.get(), stream.get(), image.get()) || !opj_end_decompress(codec.get(), stream.get()))
        refuse("its codestream did not decode", errors);

    // OpenJPEG decodes each component into its own plane of 32-bit samples; the destination is
    // interleaved RGB8. Each sample is within 0-255, the 8-bit unsigned precision checked above.
    const std::size_t pixels = static_cast<std::size_t>(columns * rows);
    for (std::uint32_t c = 0; c < RGB_COMPONENTS; ++c) {
        const OPJ_INT32* plane = image->comps[c].data;
        if (!plane) refuse("component " + std::to_string(c) + " decoded no samples", errors);
        for (std::size_t i = 0; i < pixels; ++i)
            rgb[i * RGB_COMPONENTS + c] = static_cast<std::uint8_t>(plane[i]);
    }
}
} // END IRIS SLIDE NAMESPACE
