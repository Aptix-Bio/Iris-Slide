//
//  IrisSlideSource.cpp
//  Iris
//
//  The source table and the frame-codec funnel: the only places a container or a codec is named.
//
#include "IrisSlideSource.hpp"

#include <array>
#include <stdexcept>
#include <string>

#include "IrisSlideDicomSource.hpp"
#include "IrisSlideFrameCodec.hpp"
#include "IrisSlideJpegFrames.hpp"

namespace IrisSlide {
namespace {
/// One kind of source: its name, the test that recognises its files, and what opens one.
struct SourceKind {
    const char* name;
    bool (*recognises) (const std::filesystem::path&);
    std::unique_ptr<Source> (*open) (const std::filesystem::path&);
};
// RTM Encode_10: one row per kind of source the encoder reads (Q23: DICOM first).
constexpr std::array<SourceKind, 1> SOURCE_KINDS {{
    {"DICOM", is_dicom_file, open_dicom_source},
}};
} // namespace

std::unique_ptr<Source> open_source (const std::filesystem::path& path)
{
    if (!std::filesystem::is_regular_file(path))
        throw std::runtime_error("the encoder's source " + path.string() + " is not a file");
    std::string declined;
    for (const auto& kind : SOURCE_KINDS) {
        if (kind.recognises(path)) return kind.open(path);
        declined += std::string(declined.empty() ? "" : ", ") + kind.name;
    }
    throw std::runtime_error("the encoder's source " + path.string() + " is no kind it reads (" + declined + ")");
}

// RTM Dicom_05
bool is_decodable (SourceCodec codec) noexcept
{
    switch (codec) {
        case SourceCodec::JPEG:
        case SourceCodec::JPEG_2000:
        case SourceCodec::HTJ2K:    return true;
    }
    return false;
}

void decode_frame (SourceCodec codec, std::span<const std::uint8_t> bytes, FrameShape shape,
                   std::span<std::uint8_t> rgb)
{
    switch (codec) {
        case SourceCodec::JPEG_2000:
        case SourceCodec::HTJ2K:
            decode_jpeg2000_frame(bytes, shape, rgb);
            return;
        case SourceCodec::JPEG:
            decode_jpeg_frame(bytes, shape, rgb);
            return;
    }
    throw std::runtime_error("decode_frame was given an unknown source codec " + std::to_string(int(codec)));
}
} // END IRIS SLIDE NAMESPACE
