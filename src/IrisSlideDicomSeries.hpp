//
//  IrisSlideDicomSeries.hpp
//  Iris
//
//  The DICOM whole-slide series the rebuilt encoder reads, and the one instance it takes from it
//  (ARCHITECTURE D18).
//
#ifndef IrisSlideDicomSeries_hpp
#define IrisSlideDicomSeries_hpp
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <optional>
#include <vector>

namespace IrisSlide {
// RTM Dicom_03
/// A pixel's physical size, in millimetres as DICOM stores it, row spacing first (PS3.3 10.7.1.3).
struct PixelSpacing {
    double row_mm    = 0.0;
    double column_mm = 0.0;
};

/// What the encoder reads from one instance of a whole-slide series, before any frame.
struct DicomInstance {
    std::filesystem::path path;
    std::string           sop_uid;
    std::string           series_uid;
    std::string           transfer_syntax;
    std::string           image_flavor;            ///< ImageType value 3: VOLUME, LABEL, OVERVIEW, THUMBNAIL
    std::string           dimension_organization;  ///< DimensionOrganizationType; empty when absent
    std::uint64_t         total_columns = 0;       ///< TotalPixelMatrixColumns
    std::uint64_t         total_rows    = 0;       ///< TotalPixelMatrixRows
    std::uint32_t         columns       = 0;       ///< Columns: a frame's width
    std::uint32_t         rows          = 0;       ///< Rows: a frame's height
    std::uint32_t         frames        = 0;       ///< NumberOfFrames, 1 when absent
    /// Pixel Spacing from SharedFunctionalGroupsSequence › PixelMeasuresSequence; absent on many
    /// LABEL and OVERVIEW instances, which the encoder does not read, so the source requires it.
    std::optional<PixelSpacing> spacing;
    double                objective_power = 0.0;   ///< OpticalPathSequence › ObjectiveLensPower; 0 when absent
    std::string           photometric;             ///< PhotometricInterpretation (0028,0004)
    std::string           icc_profile;             ///< OpticalPathSequence › ICCProfile, verbatim; empty when absent
};

/// The series' highest-resolution VOLUME instance: the largest TotalPixelMatrixColumns ×
/// TotalPixelMatrixRows. Throws std::runtime_error when the series has no VOLUME instance, when
/// two VOLUME instances share the largest size (naming both SOP Instance UIDs, since a tie decides
/// nothing), and when the chosen instance's DimensionOrganizationType is not TILED_FULL (naming
/// it), since only a TILED_FULL instance's frames are in row-major tile order without per-frame
/// positions (PS3.3 C.7.6.17.3).
const DicomInstance& highest_volume (std::span<const DicomInstance> series);

/// Throws std::runtime_error naming @p instance's SOP Instance UID and DimensionOrganizationType
/// unless it is TILED_FULL, whose frames alone are in row-major tile order (PS3.3 C.7.6.17.3).
void check_tiled_full (const DicomInstance& instance);

/// Pixel Spacing's two decimal-string values, joined by DICOM's value separator `\`, each read to
/// the precision it was written with. Throws std::runtime_error naming the text unless it holds
/// exactly two positive decimals.
PixelSpacing parse_pixel_spacing (std::string_view text);

/// One instance's attributes, read with libdicom; Pixel Spacing from SharedFunctionalGroupsSequence
/// › PixelMeasuresSequence when the instance has it. Throws std::runtime_error naming the file and the attribute that is
/// missing or unreadable.
DicomInstance read_dicom_instance (const std::filesystem::path& path);

/// Every .dcm instance in @p instance's folder with its SeriesInstanceUID, @p instance included, in
/// path order. A .dcm in that folder that cannot be read is refused naming it.
std::vector<DicomInstance> read_dicom_series (const std::filesystem::path& instance);
} // END IRIS SLIDE NAMESPACE
#endif /* IrisSlideDicomSeries_hpp */
