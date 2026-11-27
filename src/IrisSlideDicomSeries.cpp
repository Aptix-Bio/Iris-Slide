//
//  IrisSlideDicomSeries.cpp
//  Iris
//
//  libdicom 1.3.0 reads each instance's metadata; every libdicom handle is owned by a
//  std::unique_ptr with libdicom's destroy function. Nested sequences are walked by a loop over
//  the path of tags, so no function calls itself (the style guide's Control flow).
//
#include "IrisSlideDicomSeries.hpp"

#include <algorithm>
#include <charconv>
#include <array>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <optional>
#include <stdexcept>

extern "C" {
#include <dicom/dicom.h>
}

namespace IrisSlide {
namespace {
constexpr std::uint32_t TAG_IMAGE_TYPE                 = 0x00080008;
constexpr std::uint32_t TAG_SOP_INSTANCE_UID           = 0x00080018;
constexpr std::uint32_t TAG_SERIES_INSTANCE_UID        = 0x0020000E;
constexpr std::uint32_t TAG_DIMENSION_ORGANIZATION     = 0x00209311;
constexpr std::uint32_t TAG_PHOTOMETRIC                = 0x00280004;
constexpr std::uint32_t TAG_NUMBER_OF_FRAMES           = 0x00280008;
constexpr std::uint32_t TAG_ROWS                       = 0x00280010;
constexpr std::uint32_t TAG_COLUMNS                    = 0x00280011;
constexpr std::uint32_t TAG_PIXEL_SPACING              = 0x00280030;
constexpr std::uint32_t TAG_PIXEL_MEASURES_SEQUENCE    = 0x00289110;
constexpr std::uint32_t TAG_TOTAL_PIXEL_MATRIX_COLUMNS = 0x00480006;
constexpr std::uint32_t TAG_TOTAL_PIXEL_MATRIX_ROWS    = 0x00480007;
constexpr std::uint32_t TAG_SHARED_FUNCTIONAL_GROUPS   = 0x52009229;
constexpr std::uint32_t TAG_ICC_PROFILE                = 0x00282000;
constexpr std::uint32_t TAG_OPTICAL_PATH_SEQUENCE      = 0x00480105;
constexpr std::uint32_t TAG_OBJECTIVE_LENS_POWER       = 0x00480112;
constexpr std::uint32_t IMAGE_TYPE_FLAVOR_VALUE        = 2;   // ImageType value 3, zero-based
constexpr char          VALUE_SEPARATOR                = '\\';
constexpr std::string_view VOLUME     = "VOLUME";
constexpr std::string_view TILED_FULL = "TILED_FULL";
// PS3.3 C.8.12.4.1.1: Pixel Spacing sits in the first item of each sequence on this path.
constexpr std::array<std::uint32_t, 2> PIXEL_SPACING_PATH {TAG_SHARED_FUNCTIONAL_GROUPS, TAG_PIXEL_MEASURES_SEQUENCE};

using Handle = std::unique_ptr<DcmFilehandle, decltype(&dcm_filehandle_destroy)>;

/// libdicom's message for @p error, cleared, or @p otherwise when it set none.
std::string take_error (DcmError*& error, const std::string& otherwise)
{
    const char* message = error ? dcm_error_get_message(error) : nullptr;
    std::string text = message ? std::string(message) : otherwise;
    dcm_error_clear(&error);
    return text;
}
std::string tag_name (std::uint32_t tag)
{
    const char* keyword = dcm_dict_keyword_from_tag(tag);
    return keyword ? keyword : std::to_string(tag);
}
[[noreturn]] void refuse (const std::filesystem::path& path, std::uint32_t tag, const std::string& why)
{
    throw std::runtime_error("DICOM instance " + path.string() + ": " + tag_name(tag) + " " + why);
}
// RTM Dicom_03: an attribute is read, or refused naming the file, the attribute and why.
DcmElement* element_of (const DcmDataSet* data, std::uint32_t tag)
{
    const bool is_present = dcm_dataset_contains(data, tag);
    return is_present ? dcm_dataset_get(nullptr, data, tag) : nullptr;
}
std::string text_of (const std::filesystem::path& path, const DcmDataSet* data, std::uint32_t tag,
                     std::uint32_t index = 0)
{
    DcmElement* element = element_of(data, tag);
    if (!element) refuse(path, tag, "is absent");
    DcmError* error = nullptr;
    const char* value = nullptr;
    const bool is_read = dcm_element_get_value_string(&error, element, index, &value);
    if (!is_read || !value)
        refuse(path, tag, "is unreadable: " + take_error(error, "no value " + std::to_string(index + 1)));
    return value;
}
std::uint64_t integer_of (const std::filesystem::path& path, const DcmDataSet* data, std::uint32_t tag)
{
    DcmElement* element = element_of(data, tag);
    if (!element) refuse(path, tag, "is absent");
    DcmError* error = nullptr;
    int64_t value = 0;
    const bool is_read = dcm_element_get_value_integer(&error, element, 0, &value);
    if (!is_read) refuse(path, tag, "is unreadable: " + take_error(error, "no detail"));
    if (value < 0) refuse(path, tag, "is negative: " + std::to_string(value));
    return static_cast<std::uint64_t>(value);
}
/// The data set at the end of @p path_of_sequences: the first item of each sequence in turn.
const DcmDataSet* nested_item (const std::filesystem::path& path, const DcmDataSet* data,
                               std::span<const std::uint32_t> path_of_sequences)
{
    for (const auto tag : path_of_sequences) {
        DcmElement* element = element_of(data, tag);
        if (!element) refuse(path, tag, "is absent");
        DcmError* error = nullptr;
        DcmSequence* sequence = nullptr;
        const bool is_sequence = dcm_element_get_value_sequence(&error, element, &sequence);
        if (!is_sequence)
            refuse(path, tag, "is not a sequence: " + take_error(error, "no detail"));
        data = dcm_sequence_get(&error, sequence, 0);
        if (!data) refuse(path, tag, "has no item: " + take_error(error, "empty"));
    }
    return data;
}
std::uint32_t count_of (const std::filesystem::path& path, std::uint32_t tag, const std::string& text);
double decimal_of (const std::filesystem::path& path, std::uint32_t tag, const std::string& text);

/// The first optical path's objective power and ICC profile, each optional (PS3.3 C.8.12.5): an
/// absent one stays 0 or empty, an unreadable one is refused naming it.
void read_optical_path (const std::filesystem::path& path, const DcmDataSet* data, DicomInstance& instance)
{
    if (!element_of(data, TAG_OPTICAL_PATH_SEQUENCE)) return;
    const DcmDataSet* optical = nested_item(path, data, std::array{TAG_OPTICAL_PATH_SEQUENCE});
    if (element_of(optical, TAG_OBJECTIVE_LENS_POWER))
        instance.objective_power = decimal_of(path, TAG_OBJECTIVE_LENS_POWER, text_of(path, optical, TAG_OBJECTIVE_LENS_POWER));
    DcmElement* icc = element_of(optical, TAG_ICC_PROFILE);
    if (!icc) return;
    DcmError* error = nullptr;
    const void* bytes = nullptr;
    const bool is_read = dcm_element_get_value_binary(&error, icc, &bytes);
    if (!is_read || !bytes)
        refuse(path, TAG_ICC_PROFILE, "is unreadable: " + take_error(error, "no value"));
    instance.icc_profile.assign(static_cast<const char*>(bytes), dcm_element_get_length(icc));
}
std::string trimmed (std::string_view text);
/// An IS value (PS3.5 6.2): a positive integer, refused naming the file, the attribute and the text.
std::uint32_t count_of (const std::filesystem::path& path, std::uint32_t tag, const std::string& text)
{
    const std::string value = trimmed(text);
    std::uint32_t count = 0;
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), count);
    if (value.empty() || error != std::errc() || end != value.data() + value.size() || count == 0)
        refuse(path, tag, "\"" + text + "\" is not a positive count");
    return count;
}
/// A DS value (PS3.5 6.2), spaces trimmed: a finite decimal filling the text, or none. Each caller
/// refuses with its own context (the file and attribute, or the whole Pixel Spacing text).
std::optional<double> decimal_value (std::string_view text)
{
    const std::string value = trimmed(text);
    char* end = nullptr;
    const double parsed = value.empty() ? 0.0 : std::strtod(value.c_str(), &end);
    const bool is_decimal = !value.empty() && end == value.c_str() + value.size() && std::isfinite(parsed);
    if (!is_decimal) return std::nullopt;
    return parsed;
}
double decimal_of (const std::filesystem::path& path, std::uint32_t tag, const std::string& text)
{
    const auto value = decimal_value(text);
    if (!value) refuse(path, tag, "\"" + text + "\" is not a decimal");
    return *value;
}
std::string trimmed (std::string_view text)
{
    const auto first = text.find_first_not_of(' ');
    if (first == std::string_view::npos) return {};
    return std::string(text.substr(first, text.find_last_not_of(' ') - first + 1));
}
/// One positive decimal of Pixel Spacing, or a refusal naming the @p whole text.
double positive_decimal (std::string_view text, std::string_view whole)
{
    const auto value = decimal_value(text);
    if (!value || *value <= 0.0)
        throw std::runtime_error("Pixel Spacing \"" + std::string(whole) + "\" is not two positive decimals: \"" +
                                 trimmed(text) + "\"");
    return *value;
}
std::string sizes_text (const DicomInstance& instance)
{
    return std::to_string(instance.total_columns) + " x " + std::to_string(instance.total_rows);
}
} // namespace

// RTM Dicom_03
const DicomInstance& highest_volume (std::span<const DicomInstance> series)
{
    const DicomInstance* highest = nullptr;
    const DicomInstance* tied    = nullptr;
    for (const auto& instance : series) {
        if (instance.image_flavor != VOLUME) continue;
        const auto area = instance.total_columns * instance.total_rows;
        const auto best = highest ? highest->total_columns * highest->total_rows : 0;
        if (highest && area == best) tied = &instance;
        if (!highest || area > best) { highest = &instance; tied = nullptr; }
    }
    if (!highest) throw std::runtime_error("the DICOM series has no VOLUME instance among its " +
                                           std::to_string(series.size()) + " instances");
    if (tied) throw std::runtime_error("the DICOM series has two highest-resolution VOLUME instances, " +
                                       highest->sop_uid + " and " + tied->sop_uid + ", both " + sizes_text(*highest));
    check_tiled_full(*highest);
    return *highest;
}

void check_tiled_full (const DicomInstance& instance)
{
    if (instance.dimension_organization != TILED_FULL)
        throw std::runtime_error("DICOM instance " + instance.sop_uid + " (" + instance.path.string() +
                                 ") has DimensionOrganizationType \"" + instance.dimension_organization +
                                 "\"; only TILED_FULL frames are read, since their order is row-major without "
                                 "per-frame positions");
}

PixelSpacing parse_pixel_spacing (std::string_view text)
{
    const auto separator = text.find(VALUE_SEPARATOR);
    if (separator == std::string_view::npos || text.find(VALUE_SEPARATOR, separator + 1) != std::string_view::npos)
        throw std::runtime_error("Pixel Spacing \"" + std::string(text) + "\" is not two values");
    return {.row_mm    = positive_decimal(text.substr(0, separator), text),
            .column_mm = positive_decimal(text.substr(separator + 1), text)};
}

DicomInstance read_dicom_instance (const std::filesystem::path& path)
{
    DcmError* error = nullptr;
    Handle handle(dcm_filehandle_create_from_file(&error, path.string().c_str()), &dcm_filehandle_destroy);
    if (!handle) throw std::runtime_error("DICOM instance " + path.string() + " did not open: " +
                                          take_error(error, "no detail"));
    const DcmDataSet* data = dcm_filehandle_get_metadata_subset(&error, handle.get());
    if (!data) throw std::runtime_error("DICOM instance " + path.string() + " has no readable metadata: " +
                                        take_error(error, "no detail"));
    const char* syntax = dcm_filehandle_get_transfer_syntax_uid(handle.get());
    if (!syntax) throw std::runtime_error("DICOM instance " + path.string() + " declares no transfer syntax");

    DicomInstance instance {
        .path            = path,
        .sop_uid         = text_of(path, data, TAG_SOP_INSTANCE_UID),
        .series_uid      = text_of(path, data, TAG_SERIES_INSTANCE_UID),
        .transfer_syntax = syntax,
        .image_flavor    = text_of(path, data, TAG_IMAGE_TYPE, IMAGE_TYPE_FLAVOR_VALUE),
        .dimension_organization = element_of(data, TAG_DIMENSION_ORGANIZATION)
                                ? text_of(path, data, TAG_DIMENSION_ORGANIZATION) : std::string(),
        .total_columns   = integer_of(path, data, TAG_TOTAL_PIXEL_MATRIX_COLUMNS),
        .total_rows      = integer_of(path, data, TAG_TOTAL_PIXEL_MATRIX_ROWS),
        .columns         = static_cast<std::uint32_t>(integer_of(path, data, TAG_COLUMNS)),
        .rows            = static_cast<std::uint32_t>(integer_of(path, data, TAG_ROWS)),
        // IS is text in libdicom; PS3.3 C.7.6.6: an instance without NumberOfFrames holds one frame.
        .frames          = element_of(data, TAG_NUMBER_OF_FRAMES)
                         ? count_of(path, TAG_NUMBER_OF_FRAMES, text_of(path, data, TAG_NUMBER_OF_FRAMES)) : 1,
    };
    // Absent spacing is a value here (DicomSource refuses it on the instance it encodes); spacing
    // that is present and unreadable is refused, whichever instance it is on.
    const bool has_measures = element_of(data, TAG_SHARED_FUNCTIONAL_GROUPS) != nullptr;
    const DcmDataSet* measures = has_measures ? nested_item(path, data, PIXEL_SPACING_PATH) : nullptr;
    if (measures && element_of(measures, TAG_PIXEL_SPACING))
        instance.spacing = parse_pixel_spacing(text_of(path, measures, TAG_PIXEL_SPACING, 0) + VALUE_SEPARATOR +
                                               text_of(path, measures, TAG_PIXEL_SPACING, 1));
    instance.photometric = text_of(path, data, TAG_PHOTOMETRIC);
    read_optical_path(path, data, instance);
    return instance;
}

std::vector<DicomInstance> read_dicom_series (const std::filesystem::path& instance)
{
    const DicomInstance named = read_dicom_instance(instance);
    std::vector<std::filesystem::path> candidates;
    for (const auto& entry : std::filesystem::directory_iterator(instance.parent_path()))
        if (entry.is_regular_file() && entry.path().extension() == ".dcm") candidates.push_back(entry.path());
    std::sort(candidates.begin(), candidates.end());
    std::vector<DicomInstance> series;
    for (const auto& candidate : candidates) {
        DicomInstance read = std::filesystem::equivalent(candidate, instance) ? named : read_dicom_instance(candidate);
        if (read.series_uid == named.series_uid) series.push_back(std::move(read));
    }
    return series;
}
} // END IRIS SLIDE NAMESPACE
