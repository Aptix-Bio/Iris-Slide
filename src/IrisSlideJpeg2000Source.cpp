//
//  IrisSlideJpeg2000Source.cpp
//  Iris
//
//  libdicom reads each frame's codestream; OpenJPEG decodes it (decode_jpeg2000_frame); the
//  re-cut assembles 256-pixel tiles from the decoded frames (recut_tile).
//
#include "IrisSlideJpeg2000Source.hpp"

#include <stdexcept>
#include <string>

extern "C" {
#include <dicom/dicom.h>
}

#include "IrisSlideOpenJpeg.hpp"
#include "IrisSlideSourceCodec.hpp"

namespace IrisSlide {
namespace {
constexpr std::size_t RGB = 3;
constexpr std::string_view TILED_FULL = "TILED_FULL";

std::string take_error (DcmError*& error)
{
    const char* message = error ? dcm_error_get_message(error) : nullptr;
    std::string text = message ? message : "no detail";
    dcm_error_clear(&error);
    return text;
}
[[noreturn]] void refuse (const DicomInstance& instance, const std::string& why)
{
    throw std::runtime_error("DICOM instance " + instance.path.string() + " " + why);
}
} // namespace

// RTM Encode_10, Dicom_05
Jpeg2000Source::Jpeg2000Source (const DicomInstance& instance) :
_instance   (instance),
_grid       {.width = instance.total_columns, .height = instance.total_rows,
             .frame = {.columns = instance.columns, .rows = instance.rows}},
_handle     (nullptr, &dcm_filehandle_destroy)
{
    const SourceCodec codec = source_codec(instance.transfer_syntax);   // refuses an unknown syntax
    if (codec != SourceCodec::JPEG_2000 && codec != SourceCodec::HTJ2K)
        refuse(instance, "has transfer syntax " + instance.transfer_syntax + ", which is not JPEG 2000 or HTJ2K");
    if (instance.dimension_organization != TILED_FULL)
        refuse(instance, "has DimensionOrganizationType \"" + instance.dimension_organization +
                         "\"; only TILED_FULL frames are in row-major order");
    if (instance.columns == 0 || instance.rows == 0)
        refuse(instance, "declares frames of " + std::to_string(instance.columns) + " x " +
                         std::to_string(instance.rows) + " pixels");
    _frames_across = static_cast<std::uint32_t>((instance.total_columns + instance.columns - 1) / instance.columns);
    const std::uint64_t frames_down = (instance.total_rows + instance.rows - 1) / instance.rows;
    if (std::uint64_t{instance.frames} != _frames_across * frames_down)
        refuse(instance, "holds " + std::to_string(instance.frames) + " frames where one focal plane of " +
                         std::to_string(_frames_across) + " x " + std::to_string(frames_down) + " is read");
    DcmError* error = nullptr;
    _handle.reset(dcm_filehandle_create_from_file(&error, instance.path.string().c_str()));
    if (!_handle) refuse(instance, "did not open: " + take_error(error));
    if (!dcm_filehandle_get_metadata_subset(&error, _handle.get()) ||
        !dcm_filehandle_prepare_read_frame(&error, _handle.get()))
        refuse(instance, "did not prepare its frames: " + take_error(error));
}

Jpeg2000Source::~Jpeg2000Source () = default;

const std::vector<std::vector<std::uint8_t>>& Jpeg2000Source::frame_row (std::uint32_t row)
{
    const auto kept = _rows.find(row);
    if (kept != _rows.end()) return kept->second;
    std::vector<std::vector<std::uint8_t>> frames(_frames_across);
    for (std::uint32_t column = 0; column < _frames_across; ++column) {
        const std::uint32_t number = row * _frames_across + column + 1;   // DICOM frames count from 1
        DcmError* error = nullptr;
        std::unique_ptr<DcmFrame, decltype(&dcm_frame_destroy)> frame(
            dcm_filehandle_read_frame(&error, _handle.get(), number), &dcm_frame_destroy);
        if (!frame) refuse(_instance, "frame " + std::to_string(number) + " did not read: " + take_error(error));
        const auto* bytes = static_cast<const std::uint8_t*>(static_cast<const void*>(dcm_frame_get_value(frame.get())));
        frames[column].resize(std::size_t{_grid.frame.columns} * _grid.frame.rows * RGB);
        decode_jpeg2000_frame(std::span(bytes, dcm_frame_get_length(frame.get())), _grid.frame, frames[column]);
    }
    return _rows.emplace(row, std::move(frames)).first->second;
}

void Jpeg2000Source::read_tile (std::uint32_t x, std::uint32_t y, std::span<std::uint8_t> rgba)
{
    // Frame rows above this tile's first are not needed by this tile or any after it in
    // row-major order; a read out of that order decodes them again.
    const auto first = static_cast<std::uint32_t>(std::uint64_t{y} * RECUT_TILE / _grid.frame.rows);
    std::erase_if(_rows, [first](const auto& entry) { return entry.first < first; });
    recut_tile(_grid, x, y,
               [this](std::uint32_t column, std::uint32_t row) {
                   return std::span<const std::uint8_t>(frame_row(row)[column]);
               },
               rgba);
}
} // END IRIS SLIDE NAMESPACE
