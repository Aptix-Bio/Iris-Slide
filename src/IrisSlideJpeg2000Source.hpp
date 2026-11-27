//
//  IrisSlideJpeg2000Source.hpp
//  Iris
//
//  The rebuilt encoder's first source (ARCHITECTURE D17, D18): one TILED_FULL DICOM instance whose
//  frames are JPEG 2000 or HTJ2K, read as 256-pixel RGBA8 tiles.
//
#ifndef IrisSlideJpeg2000Source_hpp
#define IrisSlideJpeg2000Source_hpp
#include <cstdint>
#include <map>
#include <memory>
#include <span>
#include <vector>

#include "IrisSlideDicomSeries.hpp"
#include "IrisSlideRetile.hpp"

struct _DcmFilehandle;

namespace IrisSlide {
// RTM Encode_10, Dicom_05
/// A JPEG 2000 or HTJ2K DICOM instance read as 256-pixel tiles. Each tile is re-cut from the
/// frames it overlaps (recut_tile); a frame row is decoded once and kept while the tiles that
/// cross it are read, so reading tiles in row-major order decodes every frame once. libdicom is
/// not safe to call from two threads on one file, so one thread at a time reads a source.
class Jpeg2000Source {
public:
    /// Opens @p instance for frame reads. Throws std::runtime_error naming the file when its
    /// transfer syntax is not JPEG 2000 or HTJ2K, it is not TILED_FULL, or its frame count is not
    /// one frame per position of a single focal plane.
    explicit Jpeg2000Source (const DicomInstance& instance);
    Jpeg2000Source (const Jpeg2000Source&) = delete;
    Jpeg2000Source& operator= (const Jpeg2000Source&) = delete;
    ~Jpeg2000Source ();

    const DicomInstance& instance () const noexcept { return _instance; }
    SlideGrid grid () const noexcept { return _grid; }

    /// Tile (x, y) of the 256-pixel re-cut into @p rgba (RECUT_TILE² × 4 bytes); white beyond the
    /// slide. Frame rows above the tile's first are released. Throws std::runtime_error naming the
    /// frame that does not read or decode.
    void read_tile (std::uint32_t x, std::uint32_t y, std::span<std::uint8_t> rgba);

private:
    /// The decoded RGB8 frames of frame row @p row, decoding them on first use.
    const std::vector<std::vector<std::uint8_t>>& frame_row (std::uint32_t row);

    DicomInstance                                        _instance;
    SlideGrid                                            _grid;
    std::uint32_t                                        _frames_across = 0;
    std::unique_ptr<_DcmFilehandle, void (*)(_DcmFilehandle*)> _handle;
    std::map<std::uint32_t, std::vector<std::vector<std::uint8_t>>> _rows;
};
} // END IRIS SLIDE NAMESPACE
#endif /* IrisSlideJpeg2000Source_hpp */
