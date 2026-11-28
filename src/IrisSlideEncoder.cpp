//
//  IrisSlideEncoder.cpp
//  Iris
//
//  The encoder (ARCHITECTURE D14), rebuilt from scratch and cut over 2026-11-27.
//
//  The work is a fixed schedule of rows: every row of every layer, in the order a sequential
//  encode would make them, base rows read in order and each derived row as soon as the rows it
//  reads are made. A row's tiles are worked as tasks on the encoder's thread pool (Iris::Async):
//  each tile is a task that reads its source tile (a Source is read from any number of threads at
//  once), compresses it and stores it; a derived row's tiles are reduced and compressed per tile. The task that finishes a row's last
//  tile counts its row done and calls advance(), which starts what has become ready, writes
//  finished rows to IFE's Builder in schedule order, and releases what no row still reads.
//  advance() is claimed by one thread at a time with an atomic flag; a thread that finds it
//  claimed leaves a request and moves on, and the claimant runs again for it (claim and move on,
//  the style guide's Control flow), so nothing on the encode's path waits.
//
#include "IrisSlideEncoder.hpp"

#include <algorithm>
#include <array>
#include <span>
#include <bit>
#include <cmath>
#include <concepts>
#include <atomic>
#include <limits>
#include <string_view>
#include <cstring>
#include <stdexcept>
#include <vector>

#if defined(__APPLE__)
#include <sys/sysctl.h>
#elif defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "IrisSlidePriv.hpp"
#include "IFE_Builder.hpp"
#include "IrisSlideReduce.hpp"
#include "IrisSlideEncoderOptions.hpp"
#include "IrisSlideAv1Tiles.hpp"

namespace IrisSlide {
namespace {
/// The default budget is this fraction of physical memory (Q20, decided 2026-11-27).
constexpr std::uint64_t BUDGET_DIVISOR = 4;
/// A derived row reads up to 4 rows of the layer below; with the row being made, 5 rows of each
/// layer is the smallest working set an encode can progress in.
constexpr std::uint64_t ROWS_HELD = 5;

/// The machine's physical memory; throws naming the system call that could not say, so the
/// default budget is never silently 0.
std::uint64_t physical_memory ()
{
#if defined(__APPLE__)
    std::uint64_t bytes = 0;
    std::size_t length = sizeof(bytes);
    const bool is_read = sysctlbyname("hw.memsize", &bytes, &length, nullptr, 0) == 0 && bytes > 0;
    if (!is_read) throw std::runtime_error("sysctlbyname(hw.memsize) gave no physical memory size; pass a memory budget");
    return bytes;
#elif defined(_WIN32)
    MEMORYSTATUSEX status {};
    status.dwLength = sizeof(status);
    const bool is_read = GlobalMemoryStatusEx(&status) && status.ullTotalPhys > 0;
    if (!is_read) throw std::runtime_error("GlobalMemoryStatusEx gave no physical memory size (error " +
                                           std::to_string(GetLastError()) + "); pass a memory budget");
    return status.ullTotalPhys;
#else
    const long pages = sysconf(_SC_PHYS_PAGES), page = sysconf(_SC_PAGE_SIZE);
    const bool is_read = pages > 0 && page > 0;
    if (!is_read) throw std::runtime_error("sysconf gave " + std::to_string(pages) + " pages of " + std::to_string(page) +
                                           " bytes; pass a memory budget");
    return std::uint64_t(pages) * std::uint64_t(page);
#endif
}

/// Whether this encoder writes @p encoding (a row of ENCODING_NAMES).
bool is_written_encoding (Encoding encoding) noexcept
{
    return std::any_of(ENCODING_NAMES.begin(), ENCODING_NAMES.end(),
                       [encoding](const EncodingName& row) { return row.encoding == encoding; });
}
/// The encoding's name as IFE's specification spells it (IFE's generated member_name).
std::string encoding_name (Encoding encoding)
{
    return std::string(Iris::File::constants::member_name(static_cast<Iris::File::constants::TileEncodings>(encoding)));
}
/// <source stem>.<encoding word>.iris; throws naming an encoding this encoder does not write.
std::string file_name (const std::filesystem::path& source, Encoding encoding)
{
    if (!is_written_encoding(encoding))
        throw std::runtime_error(std::string("the encoder does not write ") + encoding_name(encoding));
    return source.stem().string() + "." + std::string(encoding_word(encoding)) + ".iris";
}

/// The focal planes of a Z-stack of @p planes in the order a stream carries them (D9): the
/// in-focus plane, which is the middle of the Z order, then the others outward from it, the one
/// above before the one below (2, 3, 1, 4, 0 for five planes).
std::vector<std::uint32_t> stream_order (std::uint32_t planes)
{
    const std::uint32_t in_focus = planes / 2;
    std::vector<std::uint32_t> order {in_focus};
    for (std::uint32_t step = 1; order.size() < planes; ++step) {
        if (in_focus + step < planes) order.push_back(in_focus + step);
        if (step <= in_focus) order.push_back(in_focus - step);
    }
    return order;
}

/// The encoder's one way out for pixels: a 256-pixel RGBA8 tile, its @p planes focal planes back
/// to back in Z order, compressed in the IFE tile encoding the file declares. An encoding is one
/// case here (JPEG: libjpeg-turbo through the codec context, one plane; AV1: libaom, every plane
/// in stream order, IrisSlideAv1Tiles.hpp).
Buffer compress_tile (const Context& context, Encoding encoding, const std::vector<std::uint8_t>& rgba,
                      std::uint32_t planes, Quality quality, Subsampling subsampling, Effort effort)
{
    switch (encoding) {
        case TILE_ENCODING_JPEG:
            return context->compress_tile({// The codec only reads the pixels; the buffer wrapper takes a mutable pointer.
                                           .pixelArray  = Iris::Wrap_weak_buffer_fom_data(const_cast<std::uint8_t*>(rgba.data()), rgba.size()),
                                           .format      = Iris::FORMAT_R8G8B8A8,
                                           .encoding    = encoding,
                                           .quality     = quality,
                                           .subsampling = subsampling});
        case TILE_ENCODING_AV1: {
            // RTM Encode_13: the planes in stream order, the in-focus plane first (D9).
            std::vector<std::span<const std::uint8_t>> ordered;
            for (const std::uint32_t plane : stream_order(planes))
                ordered.push_back(std::span<const std::uint8_t>(rgba).subspan(plane * TILE_BYTES, TILE_BYTES));
            const std::vector<std::uint8_t> stream = encode_av1_tile(ordered, quality, effort);
            return Iris::Copy_strong_buffer_from_data(stream.data(), stream.size());
        }
        case TILE_ENCODING_UNDEFINED:
        case TILE_ENCODING_IRIS:
        case TILE_ENCODING_UASTC:
        case TILE_ENCODING_XUASTC:
            break;
    }
    throw std::runtime_error(std::string("the encoder does not write ") + encoding_name(encoding));
}

// RTM Identity_07
// D10 (decided 2026-11-25): the caller's I2S attributes are refused before anything is opened when
// IFE's attribute blocks cannot hold them. The well-formed UTF-8 sequences of RFC 3629 section 4,
// one row per lead-byte range: the sequence's length and the range its second byte must fall in;
// the second-byte ranges exclude overlong forms, UTF-16 surrogates and code points past U+10FFFF.
struct Utf8Lead {
    std::uint8_t first, last, length, second_low, second_high;
};
constexpr std::uint8_t UTF8_CONTINUATION_LOW  = 0x80;
constexpr std::uint8_t UTF8_CONTINUATION_HIGH = 0xBF;
constexpr std::array<Utf8Lead, 9> UTF8_LEADS {{
    {0x00, 0x7F, 1, 0x00, 0x00}, {0xC2, 0xDF, 2, 0x80, 0xBF}, {0xE0, 0xE0, 3, 0xA0, 0xBF},
    {0xE1, 0xEC, 3, 0x80, 0xBF}, {0xED, 0xED, 3, 0x80, 0x9F}, {0xEE, 0xEF, 3, 0x80, 0xBF},
    {0xF0, 0xF0, 4, 0x90, 0xBF}, {0xF1, 0xF3, 4, 0x80, 0xBF}, {0xF4, 0xF4, 4, 0x80, 0x8F},
}};
constexpr unsigned char ASCII_LAST = 0x7F;
/// The offset of the first byte that begins no well-formed UTF-8 sequence, or npos.
std::size_t first_invalid_utf8 (std::u8string_view text)
{
    for (std::size_t at = 0; at < text.size();) {
        const auto lead = static_cast<std::uint8_t>(text[at]);
        const auto row  = std::find_if(UTF8_LEADS.begin(), UTF8_LEADS.end(),
                                       [lead](const Utf8Lead& r) { return lead >= r.first && lead <= r.last; });
        if (row == UTF8_LEADS.end() || text.size() - at < row->length) return at;
        for (std::size_t i = 1; i < row->length; ++i) {
            const auto byte = static_cast<std::uint8_t>(text[at + i]);
            const auto low  = i == 1 ? row->second_low  : UTF8_CONTINUATION_LOW;
            const auto high = i == 1 ? row->second_high : UTF8_CONTINUATION_HIGH;
            if (byte < low || byte > high) return at;
        }
        at += row->length;
    }
    return std::u8string_view::npos;
}
std::string format_name (MetadataType type)
{
    switch (type) {
        case METADATA_UNDEFINED: return "METADATA_UNDEFINED";
        case METADATA_I2S:       return "METADATA_I2S";
        case METADATA_DICOM:     return "METADATA_DICOM";
        case METADATA_FREE_TEXT: return "METADATA_FREE_TEXT";
    }
    return "metadata type " + std::to_string(static_cast<int>(type));
}
/// Refuses, naming the pair, what IFE's attribute blocks cannot hold (ife-attribute-sizes: a
/// 16-bit KEY_SIZE; ife-attribute-bytes: ASCII keys, UTF-8 values, a 32-bit COUNT of every key and
/// value byte), a format other than METADATA_I2S (D2), and version 0, an unversioned I2S claim.
/// Whether a key belongs to I2S is I2S's check.
void check_attributes (const Attributes& attributes)
{
    if (attributes.empty()) return;
    if (attributes.type != METADATA_I2S)
        throw std::runtime_error("the encoder refused the caller's attributes declared " + format_name(attributes.type) +
                                 ": a caller's pairs are written only as METADATA_I2S");
    if (attributes.version == 0)
        throw std::runtime_error("the encoder refused the caller's METADATA_I2S attributes at version 0: "
                                 "an I2S claim names the I2S version its pairs conform to");
    std::uint64_t bytes = 0;
    for (const auto& [key, value] : attributes) {
        const auto wide = std::find_if(key.begin(), key.end(), [](char c) { return static_cast<unsigned char>(c) > ASCII_LAST; });
        if (wide != key.end())
            throw std::runtime_error("the encoder refused the attribute key \"" + key + "\": byte " +
                                     std::to_string(wide - key.begin()) + " is not ASCII");
        if (key.size() > std::numeric_limits<std::uint16_t>::max())
            throw std::runtime_error("the encoder refused the attribute key \"" + key + "\": " +
                                     std::to_string(key.size()) + " bytes, past the 16-bit KEY_SIZE");
        const auto invalid = first_invalid_utf8(value);
        if (invalid != std::u8string_view::npos)
            throw std::runtime_error("the encoder refused the value of attribute key \"" + key + "\": byte " +
                                     std::to_string(invalid) + " begins no UTF-8 sequence");
        bytes += key.size() + value.size();
    }
    if (bytes > std::numeric_limits<std::uint32_t>::max())
        throw std::runtime_error("the encoder refused the caller's attributes: " + std::to_string(bytes) +
                                 " key and value bytes, past ATTRIBUTE_BYTES' 32-bit COUNT");
}

/// The encoder's phase. SEALING is the end of an encode: the file's structure is written and
/// it is renamed into place, which an interrupt cannot stop halfway. Callers see it as ACTIVE.
enum class Phase : std::uint8_t { READY, ACTIVE, SEALING, DONE, FAILED, INTERRUPTED };
EncoderState public_state (Phase phase) noexcept
{
    switch (phase) {
        case Phase::READY:       return EncoderState::READY;
        case Phase::ACTIVE:
        case Phase::SEALING:     return EncoderState::ACTIVE;
        case Phase::DONE:        return EncoderState::DONE;
        case Phase::FAILED:      return EncoderState::FAILED;
        case Phase::INTERRUPTED: return EncoderState::INTERRUPTED;
    }
    return EncoderState::FAILED;
}
const char* phase_name (Phase phase) noexcept
{
    switch (phase) {
        case Phase::READY:       return "READY";
        case Phase::ACTIVE:      return "ACTIVE";
        case Phase::SEALING:     return "SEALING";
        case Phase::DONE:        return "DONE";
        case Phase::FAILED:      return "FAILED";
        case Phase::INTERRUPTED: return "INTERRUPTED";
    }
    return "an unknown phase";
}
/// The longest failure message kept; a longer one is cut. Fixed, so recording a failure never allocates.
constexpr std::size_t ERROR_CAPACITY = 1024;

/// One row of one layer: its tiles' pixels (kept while a row above still reads them) and their
/// compressed streams (kept until written).
struct Row {
    enum class Stage : std::uint8_t { WAITING, WORKING, DONE, WRITTEN };
    std::vector<std::vector<std::uint8_t>> pixels;
    std::vector<Buffer>                    streams;
    std::atomic<std::uint32_t>             remaining {0};
    std::atomic<Stage>                     stage {Stage::WAITING};
    std::uint32_t                          readers = 0;   // rows above that still read this one
};
struct Layer {
    LayerSize         size;
    bool              written = true;   // false for a 2x layer a larger layer step skips
    std::uint32_t     across = 0, down = 0;
    std::vector<Row>  rows;
};
struct Job {
    std::uint32_t depth;   // 0 is the source's own layer, the highest resolution
    std::uint32_t row;
};
/// How many 2x reductions one layer step is: log2(step), or 0 when the step is not a power of two from 2.
std::uint32_t reductions_per_step (std::uint32_t step) noexcept
{
    return step >= 2 && (step & (step - 1)) == 0 ? static_cast<std::uint32_t>(std::countr_zero(step)) : 0;
}
/// Whether the layer at @p depth 2x reductions below the source ends the pyramid: it is written
/// and fits one tile.
bool is_last_layer (LayerSize size, std::uint32_t depth, std::uint32_t per_step) noexcept
{
    return depth % per_step == 0 && tiles_across(size) == 1 && tiles_down(size) == 1;
}

/// The rows of layer depth - 1 that row @p row of layer depth reads: its region spans source
/// rows 512·row - 3 to 512·row + 514, tile rows 2·row - 1 to 2·row + 2, clamped to the layer.
std::pair<std::uint32_t, std::uint32_t> rows_read (std::uint32_t row, std::uint32_t down_below) noexcept
{
    const std::uint32_t first = row == 0 ? 0 : 2 * row - 1;
    return {first, std::min(2 * row + 2, down_below - 1)};
}
} // namespace

// RTM Encode_09, Encode_10, Encode_11, Encode_12, Concurrency_04
class Encoder_t {
public:
    Encoder_t (const EncoderDesc& desc, std::unique_ptr<Source> source, std::uint64_t budget);
    Encoder_t (const Encoder_t&) = delete;
    Encoder_t& operator= (const Encoder_t&) = delete;
    ~Encoder_t ();

    void dispatch ();
    EncoderProgress snapshot () const;
    void interrupt ();

private:
    void advance () noexcept;
    void step ();
    bool is_base_row_admitted () const noexcept;
    bool is_startable (const Job& job) const noexcept;
    void start_tiles (std::uint32_t depth, std::uint32_t row);
    void work_tile (std::uint32_t depth, std::uint32_t row, std::uint32_t x) noexcept;
    void tile_done (std::uint32_t depth, std::uint32_t row) noexcept;
    void write (const Job& job);
    void release_pixels (Row& row) noexcept;
    void finish ();
    /// The layers as IFE numbers them: from the lowest resolution, whose scale is 1.
    Iris::Extent ife_extent () const;
    /// The plane count IFE records for a layer and a stream: 0, which IFE reads as one plane, for
    /// a single plane, and the Z-stack's count otherwise.
    std::uint16_t z_planes () const noexcept { return _planes > 1 ? static_cast<std::uint16_t>(_planes) : 0; }
    void fail (const std::string& why) noexcept;
    void charge (std::int64_t bytes) noexcept;
    void issue (std::invocable auto&& body);

    EncoderDesc                  _desc;
    std::filesystem::path        _file, _scratch;
    std::unique_ptr<Source>      _source;
    Context                      _context;
    std::uint32_t                _planes;        // every tile's focal planes (RTM Encode_13)
    std::uint64_t                _tile_bytes;    // a tile's pixels: _planes × TILE_BYTES
    std::vector<Layer>           _layers;
    std::vector<Job>             _schedule;
    // advance()'s cursors, under its claim: base rows started in row order, derived rows started
    // and every row written in schedule order.
    std::uint32_t                _next_base = 0;
    std::size_t                  _next_job = 0, _next_write = 0;
    std::uint32_t                _rows_in_flight = 0;   // started and not yet written
    std::uint64_t                _tiles_total = 0;
    std::atomic<std::uint64_t>   _tiles_written {0};
    std::uint64_t                _budget;
    std::uint64_t                _reserve = 0;   // kept free for derived rows and every worker's scratch
    std::atomic<std::int64_t>    _resident {0};
    std::atomic<std::int64_t>    _peak {0};
    std::atomic<bool>            _advancing {false};
    std::atomic<std::uint32_t>   _advance_requests {0};
    std::atomic<Phase>           _phase {Phase::READY};
    std::atomic_flag             _failure_claimed = ATOMIC_FLAG_INIT;
    std::array<char, ERROR_CAPACITY> _error {};
    Iris::File::Builder          _builder;
    // Declared last, destroyed first: the pool's destructor runs its pending tasks, so nothing
    // above is destroyed while a task can still reach it.
    std::unique_ptr<Iris::Async::Pool> _pool;
};

Encoder_t::Encoder_t (const EncoderDesc& desc, std::unique_ptr<Source> source, std::uint64_t budget) :
_desc       (desc),
_file       (desc.destination / file_name(desc.source, desc.encoding)),
_scratch    (desc.destination / ("." + file_name(desc.source, desc.encoding) + ".partial")),
_source     (std::move(source)),
_context    (create_context(ContextCreateInfo {})),
_planes     (_source->planes()),
_tile_bytes (std::uint64_t{TILE_BYTES} * _planes),
_budget     (budget),
_pool       (std::make_unique<Iris::Async::Pool>(desc.concurrency))
{
    // The layers, the source's first, each the 2x reduction of the one before, every
    // log2(step)-th written, until a written one fits one tile.
    const std::uint32_t per_step = reductions_per_step(desc.layer_step);
    for (LayerSize size = _source->size();; size = reduced(size)) {
        const auto depth = static_cast<std::uint32_t>(_layers.size());
        Layer& layer = _layers.emplace_back();
        layer.size    = size;
        layer.written = depth % per_step == 0;
        layer.across = tiles_across(size);
        layer.down   = tiles_down(size);
        layer.rows   = std::vector<Row>(layer.down);
        for (auto& row : layer.rows) {
            row.pixels.resize(layer.across);
            row.streams.resize(layer.across);
        }
        if (layer.written) _tiles_total += std::uint64_t{layer.across} * layer.down;
        if (is_last_layer(size, depth, per_step)) break;
    }
    // A base row is read only with room left for one row of every derived layer and the scratch
    // of every worker, so the rows that free memory can always start.
    for (std::size_t d = 1; d < _layers.size(); ++d) _reserve += std::uint64_t{_layers[d].across} * _tile_bytes;
    _reserve += std::uint64_t{_desc.concurrency} *
                std::max(_source->read_scratch_bytes(), REDUCE_SCRATCH_BYTES);
    // The schedule: each base row, then every derived row whose last input row is now made.
    std::vector<std::uint32_t> made(_layers.size(), 0);
    for (std::uint32_t base = 0; base < _layers[0].down; ++base) {
        _schedule.push_back({0, base});
        made[0] = base + 1;
        for (std::uint32_t d = 1; d < _layers.size(); ++d)
            while (made[d] < _layers[d].down && rows_read(made[d], _layers[d - 1].down).second < made[d - 1])
                _schedule.push_back({d, made[d]++});
    }
    // How many derived rows read each row, so a row's pixels go when its last reader is done.
    for (std::uint32_t d = 1; d < _layers.size(); ++d)
        for (std::uint32_t r = 0; r < _layers[d].down; ++r) {
            const auto [first, last] = rows_read(r, _layers[d - 1].down);
            for (std::uint32_t k = first; k <= last; ++k) ++_layers[d - 1].rows[k].readers;
        }
}

Encoder_t::~Encoder_t ()
{
    // An active encode is interrupted first, so dropping the encoder stops it instead of waiting
    // for it to end; a SEALING one completes, since the pool runs its last task.
    auto active = Phase::ACTIVE;
    const bool is_interrupted = _phase.compare_exchange_strong(active, Phase::INTERRUPTED);
    if (is_interrupted) _pool->terminate();
    _pool.reset();
    _builder = Iris::File::Builder();                // releases the scratch file's mapping
    if (_phase.load() != Phase::DONE) {
        std::error_code ignored;                     // nothing to report: the file may never have been made
        std::filesystem::remove(_scratch, ignored);
    }
}

void Encoder_t::issue (std::invocable auto&& body)
{
    const bool is_issued = _pool->issue_task(std::forward<decltype(body)>(body));
    if (!is_issued) fail("the encoder's thread pool refused a task: it was terminated");
}

void Encoder_t::dispatch ()
{
    auto ready = Phase::READY;
    const bool is_claimed = _phase.compare_exchange_strong(ready, Phase::ACTIVE);
    if (!is_claimed)
        throw std::runtime_error(std::string("the encoder was already dispatched (") + phase_name(ready) +
                                 "); an encoder encodes once");
    try {
        std::filesystem::remove(_scratch);
        _builder = Iris::File::Builder::create({
            // A compressed tile never outgrows its raw pixels; only written pages cost disk.
            .capacity    = Iris::File::BuilderCreateInfo{}.capacity + _tiles_total * _tile_bytes,
            .filepath    = _scratch,
            // A single-plane layer's streams go unframed; a Z-stacked layer's are framed by the Builder
            // whatever this says, since the frame is where IFE records a tile's plane count (RTM Encode_13).
            .tile_frames = false,
        });
        // RTM Encode_13: a Z-stack's every layer carries every plane; one plane leaves the layers single-plane.
        const Iris::Extent extent = ife_extent();
        _builder.set_tile_table({.encoding = _desc.encoding, .format = Iris::FORMAT_R8G8B8A8, .extent = extent,
                                 .planes = std::vector<std::uint16_t>(extent.layers.size(), z_planes())});
    } catch (const std::exception& error) {
        fail(error.what());
        throw;
    }
    // The first step runs on the pool, so the caller's thread never does the encode's work.
    issue([this] { advance(); });
}

Iris::Extent Encoder_t::ife_extent () const
{
    // The written layers, lowest resolution first; the lowest has scale 1 and each above it is
    // layer_step times larger.
    std::vector<const Layer*> written;
    for (auto layer = _layers.rbegin(); layer != _layers.rend(); ++layer)
        if (layer->written) written.push_back(&*layer);
    Iris::Extent extent {.width  = static_cast<std::uint32_t>(written.front()->size.width),
                         .height = static_cast<std::uint32_t>(written.front()->size.height)};
    float scale = 1.f;
    for (std::size_t l = 0; l < written.size(); ++l, scale *= float(_desc.layer_step))
        extent.layers.push_back({.xTiles = written[l]->across, .yTiles = written[l]->down, .scale = scale,
                                 .downsample = float(std::pow(double(_desc.layer_step), double(written.size() - 1 - l)))});
    return extent;
}

void Encoder_t::advance () noexcept
{
    _advance_requests.fetch_add(1);
    const bool is_claimed_elsewhere = _advancing.exchange(true);
    if (is_claimed_elsewhere) return;                // the claimant will see the request
    for (;;) {
        _advance_requests.store(0);
        try {
            const bool is_active = _phase.load() == Phase::ACTIVE;
            if (is_active) step();
        } catch (const std::exception& error) {
            fail(error.what());
        }
        _advancing.store(false);
        // A request that arrived after the store is taken here, unless another thread claimed it.
        if (_advance_requests.load() == 0) return;
        const bool is_reclaimed_elsewhere = _advancing.exchange(true);
        if (is_reclaimed_elsewhere) return;
    }
}

bool Encoder_t::is_base_row_admitted () const noexcept
{
    if (_next_base >= _layers[0].down) return false;
    // With no row in flight, no memory will be freed without this one: it is read whatever the
    // budget, so an encode always progresses. The comparison is unsigned, so a budget of
    // UINT64_MAX means no limit.
    const std::uint64_t resident = std::uint64_t(std::max<std::int64_t>(0, _resident.load()));
    return _rows_in_flight == 0 || resident + std::uint64_t{_layers[0].across} * _tile_bytes + _reserve <= _budget;
}

bool Encoder_t::is_startable (const Job& job) const noexcept
{
    const auto [first, last] = rows_read(job.row, _layers[job.depth - 1].down);
    for (std::uint32_t k = first; k <= last; ++k)
        if (_layers[job.depth - 1].rows[k].stage.load() < Row::Stage::DONE) return false;
    return true;
}

void Encoder_t::step ()
{
    while (is_base_row_admitted()) start_tiles(0, _next_base++);
    // Derived rows start in schedule order as their inputs are made; base rows have their own cursor.
    for (; _next_job < _schedule.size(); ++_next_job) {
        const Job& job = _schedule[_next_job];
        if (job.depth == 0) continue;
        if (!is_startable(job)) break;
        start_tiles(job.depth, job.row);
    }
    while (_next_write < _schedule.size() &&
           _layers[_schedule[_next_write].depth].rows[_schedule[_next_write].row].stage.load() == Row::Stage::DONE)
        write(_schedule[_next_write++]);
    if (_next_write == _schedule.size()) finish();
}

/// A row's tiles go to the pool as tasks, one per tile; the pool's lock-free queue hands each to
/// one worker, which makes the whole tile on its own thread: a base tile is read, decoded,
/// re-cut and compressed (work_tile), a derived tile reduced and compressed.
void Encoder_t::start_tiles (std::uint32_t depth, std::uint32_t r)
{
    Row& row = _layers[depth].rows[r];
    const std::uint32_t across = _layers[depth].across;
    row.remaining.store(across);
    row.stage.store(Row::Stage::WORKING);
    ++_rows_in_flight;
    charge(std::int64_t(across * _tile_bytes));
    for (std::uint32_t x = 0; x < across; ++x)
        issue([this, depth, r, x] { work_tile(depth, r, x); });
}

void Encoder_t::work_tile (std::uint32_t depth, std::uint32_t r, std::uint32_t x) noexcept
{
    try {
        if (_phase.load() != Phase::ACTIVE) return;
        Row& row = _layers[depth].rows[r];
        // The scratch this tile holds while it is made, charged for as long as it is held.
        const std::int64_t scratch = std::int64_t(depth == 0 ? _source->read_scratch_bytes() : REDUCE_SCRATCH_BYTES);
        charge(scratch);
        struct Uncharge { Encoder_t* encoder; std::int64_t bytes; ~Uncharge() { encoder->charge(-bytes); } } uncharge {this, scratch};
        row.pixels[x].resize(_tile_bytes);
        if (depth == 0) {
            _source->read_tile(x, r, row.pixels[x]);
        } else {
            // Each plane on its own: gather its region from the same plane of the layer below,
            // reduce it, and make what lies beyond this layer's extent white.
            const Layer& below = _layers[depth - 1];
            std::vector<std::uint8_t> region(std::size_t{REDUCE_REGION} * REDUCE_REGION * RGBA_BYTES);
            for (std::uint32_t plane = 0; plane < _planes; ++plane) {
                gather_region(below.size, x, r,
                              [&below, plane](std::uint32_t tx, std::uint32_t ty) {
                                  return std::span<const std::uint8_t>(below.rows[ty].pixels[tx]).subspan(plane * TILE_BYTES, TILE_BYTES);
                              },
                              region);
                const auto out = std::span<std::uint8_t>(row.pixels[x]).subspan(plane * TILE_BYTES, TILE_BYTES);
                reduce_2x(region, out);
                whiten_beyond(_layers[depth].size, x, r, out);
            }
        }
        if (!_layers[depth].written) { tile_done(depth, r); return; }   // only reduced from, never stored
        row.streams[x] = compress_tile(_context, _desc.encoding, row.pixels[x], _planes, _desc.quality, _desc.subsampling, _desc.effort);
        if (!row.streams[x]) throw std::runtime_error("tile (" + std::to_string(x) + ", " + std::to_string(r) +
                                                      ") of layer " + std::to_string(depth) + " did not compress");
        charge(std::int64_t(row.streams[x]->size()));
    } catch (const std::exception& error) {
        fail(error.what());
        return;
    }
    tile_done(depth, r);
}

void Encoder_t::tile_done (std::uint32_t depth, std::uint32_t r) noexcept
{
    Row& row = _layers[depth].rows[r];
    const std::uint32_t were_remaining = row.remaining.fetch_sub(1);
    if (were_remaining != 1) return;                 // another tile of the row is still working
    row.stage.store(Row::Stage::DONE);
    advance();
}

void Encoder_t::write (const Job& job)
{
    Layer& layer = _layers[job.depth];
    Row& row = layer.rows[job.row];
    if (layer.written) {
        // IFE numbers written layers from the lowest resolution.
        const std::uint32_t per_step = reductions_per_step(_desc.layer_step);
        const auto ife_layer = static_cast<std::uint32_t>((_layers.size() - 1) / per_step - job.depth / per_step);
        for (std::uint32_t x = 0; x < layer.across; ++x) {
            Buffer& stream = row.streams[x];
            _builder.append_tile(ife_layer, job.row * layer.across + x,
                                 static_cast<const BYTE*>(stream->data()), stream->size(), z_planes());
            charge(-std::int64_t(stream->size()));
            stream = nullptr;
        }
        _tiles_written.fetch_add(layer.across);
    }
    row.stage.store(Row::Stage::WRITTEN);
    if (row.readers == 0) release_pixels(row);
    --_rows_in_flight;
    if (job.depth == 0) return;
    const auto [first, last] = rows_read(job.row, _layers[job.depth - 1].down);
    for (std::uint32_t k = first; k <= last; ++k) {
        Row& below = _layers[job.depth - 1].rows[k];
        const auto readers_left = --below.readers;
        if (readers_left == 0 && below.stage.load() == Row::Stage::WRITTEN) release_pixels(below);
    }
}

void Encoder_t::release_pixels (Row& row) noexcept
{
    std::int64_t freed = 0;
    for (auto& pixels : row.pixels) {
        freed += std::int64_t(pixels.capacity() ? _tile_bytes : 0);
        std::vector<std::uint8_t>().swap(pixels);
    }
    charge(-freed);
}

void Encoder_t::finish ()
{
    auto active = Phase::ACTIVE;
    const bool is_sealing = _phase.compare_exchange_strong(active, Phase::SEALING);
    if (!is_sealing) return;                         // interrupted or failed meanwhile
    using namespace Iris::File::Serialization;
    // The structure after the tiles, as IFE lays it out: the source's measurements (D2) and the
    // caller's attributes (D10); no associated images.
    const auto& measured = _source->measurements();
    const float to_lowest = float(1u << (_layers.size() - 1));   // the lowest layer's downsample
    std::vector<LayerExtentEntry> extents;
    for (const auto& layer : ife_extent().layers)
        extents.push_back({.X_TILES = layer.xTiles, .Y_TILES = layer.yTiles, .SCALE = layer.scale, .Z_PLANES = z_planes()});
    const Offset offsets_at = _builder->append(TileOffsetsCreateInfo{.entries = _builder.tile_offsets()});
    const Offset extents_at = _builder->append(LayerExtentsCreateInfo{.entries = extents});
    const Offset table_at   = _builder->append(TileTableCreateInfo{
        .ENCODING             = static_cast<Iris::File::constants::TileEncodings>(_desc.encoding),
        .FORMAT               = static_cast<Iris::File::constants::PixelFormats>(Iris::FORMAT_R8G8B8A8),
        .TILE_OFFSETS_OFFSET  = offsets_at,
        .LAYER_EXTENTS_OFFSET = extents_at,
        .X_EXTENT             = static_cast<std::uint32_t>(_layers.back().size.width),
        .Y_EXTENT             = static_cast<std::uint32_t>(_layers.back().size.height),
        .TILE_LENGTH          = RECUT_TILE});
    const Offset metadata_at = _builder->claim(METADATA::header_size);
    const Offset icc_at = measured.icc_profile.empty() ? NULL_OFFSET :
        _builder->append(IccProfileCreateInfo{.bytes = reinterpret_cast<const BYTE*>(measured.icc_profile.data()),
                                              .count = measured.icc_profile.size()});
    // RTM Identity_07: the caller's I2S pairs are the slide's one attributes block (D10).
    Offset attributes_at = NULL_OFFSET;
    if (!_desc.attributes.empty()) {
        std::vector<AttributeSizeEntry> pairs;
        pairs.reserve(_desc.attributes.size());
        for (const auto& [key, value] : _desc.attributes)
            pairs.push_back({.key = key, .value = std::string(reinterpret_cast<const char*>(value.data()), value.size())});
        const Offset sizes_at = _builder->append(AttributeSizesCreateInfo{.entries = pairs});
        const Offset bytes_at = _builder->append(AttributeBytesCreateInfo{.entries = pairs});
        attributes_at = _builder->append(AttributesCreateInfo{
            .FORMAT       = static_cast<Iris::File::constants::MetadataFormats>(_desc.attributes.type),
            .VERSION      = _desc.attributes.version,
            .SIZES_OFFSET = sizes_at,
            .BYTES_OFFSET = bytes_at});
    }
    const Version codec = get_codec_version();
    _builder->fill(metadata_at, MetadataCreateInfo{
        .CODEC_MAJOR       = static_cast<std::uint16_t>(codec.major),
        .CODEC_MINOR       = static_cast<std::uint16_t>(codec.minor),
        .CODEC_BUILD       = static_cast<std::uint16_t>(codec.build),
        .ATTRIBUTES_OFFSET = attributes_at,
        .ICC_COLOR_OFFSET  = icc_at,
        // IFE normalises both to layer scale 1, the lowest resolution.
        .MICRONS_PIXEL     = float(measured.microns_per_pixel) * to_lowest,
        .MAGNIFICATION     = float(measured.magnification) / to_lowest,
        .MICRONS_PLANE     = float(measured.microns_between_planes),   // RTM Encode_13; 0 for one plane
    });
    _builder.finalize({.tileTable = table_at, .metadata = metadata_at});
    _builder = Iris::File::Builder();   // the mapping is released before the rename
    std::filesystem::rename(_scratch, _file);
    _phase.store(Phase::DONE);
}

void Encoder_t::fail (const std::string& why) noexcept
{
    const bool is_reported = _failure_claimed.test_and_set();
    if (is_reported) return;                         // the first failure is the one reported
    const std::size_t kept = std::min(why.size(), _error.size() - 1);
    std::memcpy(_error.data(), why.data(), kept);
    _error[kept] = '\0';
    for (Phase from : {Phase::ACTIVE, Phase::SEALING}) {
        const bool is_failed = _phase.compare_exchange_strong(from, Phase::FAILED);
        if (is_failed) return;
    }
}

void Encoder_t::charge (std::int64_t bytes) noexcept
{
    const std::int64_t now = _resident.fetch_add(bytes) + bytes;
    for (std::int64_t peak = _peak.load(); now > peak;) {   // a failed exchange reloads peak
        const bool is_raised = _peak.compare_exchange_weak(peak, now);
        if (is_raised) return;
    }
}

EncoderProgress Encoder_t::snapshot () const
{
    const Phase phase = _phase.load();
    return {.state               = public_state(phase),
            .fraction            = float(_tiles_written.load()) / float(_tiles_total),
            .file                = _file,
            .error               = phase == Phase::FAILED ? std::string(_error.data()) : std::string(),
            .resident_bytes      = std::uint64_t(std::max<std::int64_t>(0, _resident.load())),
            .peak_resident_bytes = std::uint64_t(std::max<std::int64_t>(0, _peak.load())),
            .memory_budget       = _budget};
}

void Encoder_t::interrupt ()
{
    auto active = Phase::ACTIVE;
    const bool is_interrupted = _phase.compare_exchange_strong(active, Phase::INTERRUPTED);
    if (!is_interrupted)
        throw std::runtime_error(std::string("only an active encode is interrupted; this one is ") + phase_name(active) +
                                 (active == Phase::SEALING ? ", writing its file's structure, and completes" : ""));
    _pool->terminate();
}

// MARK: - The boundary

namespace {
/// Runs a boundary's @p body, translating any exception into a Result naming @p call, so no
/// exception crosses the boundary and every boundary reports alike.
Result at_boundary (const char* call, std::invocable auto&& body) noexcept
{
    try {
        body();
        return Result::success();
    } catch (const std::exception& error) {
        return codec_failure(SlideError::ENCODER_FAILED, error.what());
    } catch (...) {
        return codec_failure(SlideError::ENCODER_FAILED, std::string(call) + " failed with an unknown exception");
    }
}

/// Every check the descriptor alone decides, made before a source is opened so a wrong option is
/// refused by name without reading the slide; throws naming the value refused.
void check_desc (const EncoderDesc& desc)
{
    check_attributes(desc.attributes);   // D10
    if (reductions_per_step(desc.layer_step) == 0)
        throw std::runtime_error("layer step " + std::to_string(desc.layer_step) + " is not a power of two from 2");
    if (!is_written_encoding(desc.encoding))
        throw std::runtime_error(std::string("the encoder does not write ") + encoding_name(desc.encoding));
    if (desc.quality < QUALITY_MIN || desc.quality > QUALITY_MAX)
        throw std::runtime_error("quality " + std::to_string(desc.quality) + " is outside 1-100");
    // RTM Encode_16: effort is AV1's speed setting (D25); the JPEG path spends none, so it takes
    // EFFORT_MIN alone.
    if (desc.effort > EFFORT_MAX)
        throw std::runtime_error("effort " + std::to_string(int(desc.effort)) + " is outside " + std::to_string(int(EFFORT_MIN)) +
                                 "-" + std::to_string(int(EFFORT_MAX)));
    if (desc.effort != EFFORT_MIN && desc.encoding != TILE_ENCODING_AV1)
        throw std::runtime_error("effort " + std::to_string(int(desc.effort)) + " is refused for " + encoding_name(desc.encoding) +
                                 ", which has no speed setting; only AV1 tiles take an effort above " + std::to_string(int(EFFORT_MIN)));
    // RTM Encode_13: AV1 tiles are 4:2:0 alone (D22), so another chroma is refused before it could
    // be written as something else.
    if (desc.encoding == TILE_ENCODING_AV1 && desc.subsampling != SUBSAMPLE_420)
        throw std::runtime_error("AV1 tiles are 4:2:0 (subsampling 420); subsampling " +
                                 std::string(subsampling_name(desc.subsampling)) + " is refused");
    if (desc.concurrency == 0) throw std::runtime_error("concurrency 0 is refused: an encode needs at least 1 worker thread");
    if (!std::filesystem::is_directory(desc.destination))
        throw std::runtime_error("the destination " + desc.destination.string() + " is not a folder");
}

/// The encoder for @p source, after the descriptor's checks and the source's; throws naming the value refused.
Encoder make_encoder (const EncoderDesc& desc, std::unique_ptr<Source> source)
{
    if (!source) throw std::runtime_error("create_encoder was given no source");
    check_desc(desc);
    // RTM Encode_13: AV1 alone carries a Z-stack (D9); IFE's plane count is 16-bit.
    const std::uint32_t planes = source->planes();
    if (planes == 0 || planes > std::numeric_limits<std::uint16_t>::max())
        throw std::runtime_error("the source has " + std::to_string(planes) + " focal planes; 1 to 65535 are encoded");
    if (planes > 1 && desc.encoding != TILE_ENCODING_AV1)
        throw std::runtime_error("the source has " + std::to_string(planes) + " focal planes and " + encoding_name(desc.encoding) +
                                 " tiles carry one; a Z-stack is encoded as TILE_ENCODING_AV1 (JPEG holds a single plane)");
    const std::uint32_t per_step = reductions_per_step(desc.layer_step);
    if (source->size().width == 0 || source->size().height == 0)
        throw std::runtime_error("the source is " + std::to_string(source->size().width) + " x " +
                                 std::to_string(source->size().height) + " pixels; an empty slide is not encoded");
    // The smallest working set: 5 rows of every layer and every worker's scratch.
    std::uint64_t working_set = std::uint64_t{desc.concurrency} *
                                std::max(source->read_scratch_bytes(), REDUCE_SCRATCH_BYTES);
    LayerSize size = source->size();
    for (std::uint32_t depth = 0;; ++depth, size = reduced(size)) {
        working_set += ROWS_HELD * tiles_across(size) * std::uint64_t{TILE_BYTES} * planes;
        if (is_last_layer(size, depth, per_step)) break;
    }
    const std::uint64_t budget = desc.memory_budget ? desc.memory_budget : physical_memory() / BUDGET_DIVISOR;
    if (budget < working_set)
        throw std::runtime_error("a memory budget of " + std::to_string(budget) + " bytes is below the " +
                                 std::to_string(working_set) + " bytes this slide needs at once");
    return std::make_shared<Encoder_t>(desc, std::move(source), budget);
}

/// The encoder a boundary was handed, or a throw naming @p call when it was given none.
Encoder_t& encoder_of (const Encoder& encoder, const char* call)
{
    if (!encoder) throw std::runtime_error(std::string(call) + " was given no encoder");
    return *encoder;
}
} // namespace

Result encoder_output_name (const std::filesystem::path& source, Encoding encoding, std::string& name) noexcept
{
    return at_boundary("encoder_output_name", [&name, &source, encoding] { name = file_name(source, encoding); });
}

Result create_encoder (const EncoderDesc& desc, Encoder& encoder) noexcept
{
    return at_boundary("create_encoder", [&desc, &encoder] {
        check_desc(desc);   // refused before the source is opened
        encoder = make_encoder(desc, open_source(desc.source));
    });
}

Result create_encoder (const EncoderDesc& desc, std::unique_ptr<Source> source, Encoder& encoder) noexcept
{
    return at_boundary("create_encoder", [&desc, &source, &encoder] { encoder = make_encoder(desc, std::move(source)); });
}

Result dispatch_encoder (const Encoder& encoder) noexcept
{
    return at_boundary("dispatch_encoder", [&encoder] { encoder_of(encoder, "dispatch_encoder").dispatch(); });
}

Result encoder_progress (const Encoder& encoder, EncoderProgress& progress) noexcept
{
    return at_boundary("encoder_progress", [&encoder, &progress] { progress = encoder_of(encoder, "encoder_progress").snapshot(); });
}

Result interrupt_encoder (const Encoder& encoder) noexcept
{
    return at_boundary("interrupt_encoder", [&encoder] { encoder_of(encoder, "interrupt_encoder").interrupt(); });
}
} // END IRIS SLIDE NAMESPACE
