//
//  IrisSlideDicomSource.hpp
//  Iris
//
//  The DICOM source module (ARCHITECTURE D18): a whole-slide series' highest-resolution TILED_FULL
//  instance as a Source. The container (libdicom: frame bytes by index) is this module's; the
//  frames' codec is decode_frame's (IrisSlideFrameCodec.hpp), so a codec is added or removed there.
//
#ifndef IrisSlideDicomSource_hpp
#define IrisSlideDicomSource_hpp
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <vector>

#include "IrisSlideDicomSeries.hpp"
#include "IrisSlideSource.hpp"
#include "IrisSlideSourceCodec.hpp"

struct _DcmFilehandle;

namespace IrisSlide {
// RTM Encode_10, Dicom_03
/// Whether @p path is a DICOM file (the "DICM" prefix after the 128-byte preamble, PS3.10 7.1):
/// the source table's test for this module.
bool is_dicom_file (const std::filesystem::path& path);
/// The Source for the series @p path belongs to: its highest-resolution VOLUME instance (D18).
std::unique_ptr<Source> open_dicom_source (const std::filesystem::path& path);

/// One TILED_FULL instance read as 256-pixel tiles. read_tile reads the frames under the tile,
/// decodes them and re-cuts the tile from them (recut_tile), all on the calling thread, so tiles
/// read on several threads decode on several cores. libdicom is not safe for two threads on one
/// file handle, so the source keeps one handle per hardware thread, each with an atomic claim: a
/// reader claims a free handle by compare-and-swap, reads its frames' bytes and releases it; when
/// every handle is claimed it opens a handle of its own for that read, so no reader waits.
class DicomSource final : public Source {
public:
    /// Throws std::runtime_error naming the file when it is not TILED_FULL, its codec is not one
    /// decode_frame decodes, or its frame count is not one per position of one focal plane.
    explicit DicomSource (const DicomInstance& instance);
    DicomSource (const DicomSource&) = delete;
    DicomSource& operator= (const DicomSource&) = delete;
    ~DicomSource () override;

    LayerSize size () const noexcept override { return {.width = _grid.width, .height = _grid.height}; }
    const SourceMeasurements& measurements () const noexcept override { return _measurements; }
    void read_tile (std::uint32_t x, std::uint32_t y, std::span<std::uint8_t> rgba) override;
    std::uint64_t read_scratch_bytes () const noexcept override;

    const DicomInstance& instance () const noexcept { return _instance; }

private:
    using Handle = std::unique_ptr<_DcmFilehandle, void (*)(_DcmFilehandle*)>;
    /// A libdicom handle on the instance, ready for frame reads.
    Handle open_handle () const;
    /// The compressed bytes of frames (first_column..last_column) × (first_row..last_row), row-major.
    std::vector<std::vector<std::uint8_t>> read_frames (std::uint32_t first_column, std::uint32_t last_column,
                                                       std::uint32_t first_row, std::uint32_t last_row);

    DicomInstance                     _instance;
    SourceCodec                       _codec;
    SlideGrid                         _grid;
    SourceMeasurements                _measurements;
    std::uint32_t                     _frames_across = 0;
    std::uint32_t                     _frames_down = 0;
    std::vector<Handle>               _handles;
    std::unique_ptr<std::atomic<bool>[]> _claimed;   // one per handle
};
} // END IRIS SLIDE NAMESPACE
#endif /* IrisSlideDicomSource_hpp */
