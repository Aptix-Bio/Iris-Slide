//
//  IrisSlideDicomFrames.cpp
//  Iris
//
//  PS3.10 7.1: a 128-byte preamble, "DICM", the file meta group (explicit VR little endian), the
//  data set. PS3.5 7.1.2: an explicit-VR element is its tag, two VR letters, then a 16-bit length,
//  or, for the VRs of PS3.5 Table 7.1-1, two reserved bytes and a 32-bit length. PS3.5 7.5: an
//  undefined length (FFFFFFFFH) is a sequence of items ending at (FFFE,E0DD), each item ending at
//  (FFFE,E00D) when its own length is undefined. PS3.5 A.4: encapsulated Pixel Data is a Basic
//  Offset Table item followed by fragment items; PS3.3 C.7.6.3.1.8: the Extended Offset Table
//  gives each frame's item position (from the first item after the Basic Offset Table) and length.
//
#include "IrisSlideDicomFrames.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>

namespace IrisSlide {
namespace {
constexpr std::uint64_t PREAMBLE = 128;
constexpr std::string_view PREFIX = "DICM";
constexpr std::uint32_t UNDEFINED_LENGTH = 0xFFFFFFFF;
constexpr std::uint16_t ITEM_GROUP = 0xFFFE;
constexpr std::uint16_t ITEM = 0xE000, ITEM_END = 0xE00D, SEQUENCE_END = 0xE0DD;
constexpr std::uint16_t PIXEL_GROUP = 0x7FE0;
constexpr std::uint16_t EXTENDED_OFFSETS = 0x0001, EXTENDED_LENGTHS = 0x0002, PIXEL_DATA = 0x0010;
constexpr std::uint64_t ITEM_HEADER = 8;
/// PS3.5 Table 7.1-1: the VRs whose length is 32 bits after two reserved bytes.
constexpr std::array<std::string_view, 13> LONG_VRS {"OB", "OD", "OF", "OL", "OV", "OW", "SQ", "SV", "UC", "UN", "UR", "UT", "UV"};

/// Bounds-checked little-endian reads over the mapped file, refusing naming the path and byte.
struct Reader {
    std::span<const std::uint8_t> bytes;
    const std::filesystem::path& path;

    [[noreturn]] void refuse (std::uint64_t at, const std::string& why) const
    {
        throw std::runtime_error("DICOM file " + path.string() + " at byte " + std::to_string(at) + ": " + why);
    }
    void need (std::uint64_t at, std::uint64_t count) const
    {
        if (at > bytes.size() || count > bytes.size() - at) refuse(at, "is cut short (" + std::to_string(bytes.size()) + " bytes)");
    }
    std::uint64_t value (std::uint64_t at, std::uint32_t width) const
    {
        need(at, width);
        std::uint64_t v = 0;
        for (std::uint32_t i = 0; i < width; ++i) v |= std::uint64_t{bytes[at + i]} << (8 * i);
        return v;
    }
    std::uint16_t group (std::uint64_t at) const { return static_cast<std::uint16_t>(value(at, 2)); }
    std::uint16_t element (std::uint64_t at) const { return static_cast<std::uint16_t>(value(at + 2, 2)); }
};

/// An explicit-VR element's value position and length (UNDEFINED_LENGTH when undefined).
struct Element {
    std::uint64_t value_at = 0;
    std::uint32_t length = 0;
};
Element element_at (const Reader& reader, std::uint64_t at)
{
    reader.need(at, 8);
    const std::string_view vr(reinterpret_cast<const char*>(reader.bytes.data() + at + 4), 2);
    if (std::find(LONG_VRS.begin(), LONG_VRS.end(), vr) != LONG_VRS.end())
        return {.value_at = at + 12, .length = static_cast<std::uint32_t>(reader.value(at + 8, 4))};
    return {.value_at = at + 8, .length = static_cast<std::uint32_t>(reader.value(at + 6, 2))};
}

/// The position after an undefined-length sequence whose items start at @p at. Items and nested
/// undefined-length sequences are walked over an explicit stack, so no depth of nesting recurses.
std::uint64_t skip_sequence (const Reader& reader, std::uint64_t at)
{
    enum class Open : std::uint8_t { SEQUENCE, ITEM };
    std::vector<Open> open {Open::SEQUENCE};
    while (!open.empty()) {
        if (reader.group(at) != ITEM_GROUP) {
            // An element inside an item; one of undefined length opens a nested sequence.
            if (open.back() != Open::ITEM) reader.refuse(at, "an element where a sequence's item was expected");
            const Element nested = element_at(reader, at);
            at = nested.value_at;
            if (nested.length == UNDEFINED_LENGTH) open.push_back(Open::SEQUENCE);
            else at += nested.length;
            continue;
        }
        const std::uint16_t marker = reader.element(at);
        const auto length = static_cast<std::uint32_t>(reader.value(at + 4, 4));
        at += ITEM_HEADER;
        const Open expected = marker == ITEM_END ? Open::ITEM : Open::SEQUENCE;
        if (open.back() != expected || (marker != ITEM && marker != ITEM_END && marker != SEQUENCE_END))
            reader.refuse(at - ITEM_HEADER, "an item marker (FFFE," + std::to_string(marker) + ") out of place");
        if (marker == ITEM_END || marker == SEQUENCE_END) open.pop_back();
        else if (length == UNDEFINED_LENGTH) open.push_back(Open::ITEM);
        else at += length;
    }
    return at;
}

std::vector<std::uint64_t> table_of (const Reader& reader, const Element& element, std::uint32_t width)
{
    std::vector<std::uint64_t> entries(element.length / width);
    for (std::size_t i = 0; i < entries.size(); ++i) entries[i] = reader.value(element.value_at + i * width, width);
    return entries;
}
} // namespace

// RTM Dicom_06
DicomFrameIndex::DicomFrameIndex (const std::filesystem::path& path, std::uint32_t frames) :
_path (path)
{
    const Iris::Result mapped = Iris::create_memory({.filepath = path, .read_only = true}, _map);
    if (mapped.failed()) throw std::runtime_error("DICOM file " + path.string() + " did not map: " + mapped.message);
    const Reader reader {.bytes = std::span<const std::uint8_t>(_map.base(), _map.capacity()), .path = _path};
    reader.need(PREAMBLE, PREFIX.size());
    if (std::memcmp(reader.bytes.data() + PREAMBLE, PREFIX.data(), PREFIX.size()) != 0)
        reader.refuse(PREAMBLE, "has no \"DICM\" prefix");

    // The file meta group's length element (0002,0000) says where the data set begins.
    const std::uint64_t meta = PREAMBLE + PREFIX.size();
    if (reader.group(meta) != 0x0002 || reader.element(meta) != 0x0000)
        reader.refuse(meta, "does not begin its file meta group with (0002,0000)");
    const Element meta_length = element_at(reader, meta);
    std::uint64_t at = meta_length.value_at + 4 + reader.value(meta_length.value_at, 4);

    // The data set's top-level elements, to Pixel Data, keeping the Extended Offset Table.
    std::vector<std::uint64_t> extended_offsets, extended_lengths;
    Element pixels {};
    for (bool found = false; !found;) {
        const Element element = element_at(reader, at);
        const std::uint16_t group = reader.group(at), number = reader.element(at);
        found = group == PIXEL_GROUP && number == PIXEL_DATA;
        if (found) { pixels = element; break; }
        if (group == PIXEL_GROUP && number == EXTENDED_OFFSETS) extended_offsets = table_of(reader, element, 8);
        if (group == PIXEL_GROUP && number == EXTENDED_LENGTHS) extended_lengths = table_of(reader, element, 8);
        at = element.length == UNDEFINED_LENGTH ? skip_sequence(reader, element.value_at) : element.value_at + element.length;
    }
    if (pixels.length != UNDEFINED_LENGTH) reader.refuse(pixels.value_at, "holds native Pixel Data; only encapsulated frames are indexed");

    // The Basic Offset Table item, then the fragment items from `first`.
    if (reader.group(pixels.value_at) != ITEM_GROUP || reader.element(pixels.value_at) != ITEM)
        reader.refuse(pixels.value_at, "does not begin Pixel Data with its Basic Offset Table item");
    const Element basic {.value_at = pixels.value_at + ITEM_HEADER,
                         .length = static_cast<std::uint32_t>(reader.value(pixels.value_at + 4, 4))};
    const std::vector<std::uint64_t> basic_offsets = table_of(reader, basic, 4);
    const std::uint64_t first = basic.value_at + basic.length;

    // Every fragment item, in order, until the sequence delimiter.
    std::vector<std::uint64_t> item_starts;
    for (std::uint64_t item = first; reader.element(item) != SEQUENCE_END;) {
        if (reader.group(item) != ITEM_GROUP || reader.element(item) != ITEM)
            reader.refuse(item, "holds something other than a fragment item in Pixel Data");
        const std::uint64_t length = reader.value(item + 4, 4);
        reader.need(item + ITEM_HEADER, length);
        item_starts.push_back(item - first);
        _fragments.push_back({.offset = item + ITEM_HEADER, .length = length});
        item += ITEM_HEADER + length;
    }

    // Which fragments make each frame: one each from the Extended Offset Table or when there are as
    // many fragments as frames; else the Basic Offset Table's frame starts group them.
    const std::vector<std::uint64_t>& starts = !extended_offsets.empty() ? extended_offsets : basic_offsets;
    if (!starts.empty() && starts.size() != frames)
        reader.refuse(first, "has an offset table of " + std::to_string(starts.size()) + " frames where " +
                             std::to_string(frames) + " are declared");
    if (starts.empty() && _fragments.size() != frames)
        reader.refuse(first, "holds " + std::to_string(_fragments.size()) + " fragments and no offset table, where " +
                             std::to_string(frames) + " frames are declared");
    for (std::uint32_t f = 0; f < frames; ++f) {
        const auto position = starts.empty() ? item_starts[f] : starts[f];
        const auto found = std::lower_bound(item_starts.begin(), item_starts.end(), position);
        if (found == item_starts.end() || *found != position)
            reader.refuse(first + position, "has an offset table entry for frame " + std::to_string(f) + " that starts no item");
        const auto from = static_cast<std::uint32_t>(found - item_starts.begin());
        const std::uint64_t end = f + 1 < frames ? (starts.empty() ? item_starts[f + 1] : starts[f + 1]) : UINT64_MAX;
        std::uint32_t count = 0;
        while (from + count < item_starts.size() && item_starts[from + count] < end) ++count;
        if (!extended_lengths.empty()) _fragments[from].length = std::min(_fragments[from].length, extended_lengths[f]);
        _frames.push_back({.first = from, .count = extended_offsets.empty() ? count : 1});
    }
}

std::span<const std::uint8_t> DicomFrameIndex::frame (std::uint32_t index, std::vector<std::uint8_t>& joined) const
{
    if (index >= _frames.size())
        throw std::runtime_error("DICOM file " + _path.string() + " has no frame " + std::to_string(index) +
                                 " (it holds " + std::to_string(_frames.size()) + ")");
    const Frame& frame = _frames[index];
    const std::uint8_t* base = _map.base();
    if (frame.count == 1) return {base + _fragments[frame.first].offset, _fragments[frame.first].length};
    joined.clear();
    for (std::uint32_t i = 0; i < frame.count; ++i) {
        const Fragment& fragment = _fragments[frame.first + i];
        joined.insert(joined.end(), base + fragment.offset, base + fragment.offset + fragment.length);
    }
    return joined;
}
} // END IRIS SLIDE NAMESPACE
