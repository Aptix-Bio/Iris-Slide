//
//  IrisSlideEncoder_.cpp
//  Iris
//
//  The rebuilt encoder (ARCHITECTURE D14), cut over to IrisSlideEncoder.cpp when done.
//
//  The work is a fixed schedule of rows: every row of every layer, in the order a sequential
//  encode would make them, base rows read in order and each derived row as soon as the rows it
//  reads are made. A row's tiles are worked as tasks on the encoder's thread pool (Iris::Async):
//  base rows are read by one task at a time (a Source is read by one thread), compressed per
//  tile; derived rows are reduced and compressed per tile. The task that finishes a row's last
//  tile counts its row done and calls advance(), which starts what has become ready, writes
//  finished rows to IFE's Builder in schedule order, and releases what no row still reads.
//  advance() is claimed by one thread at a time with an atomic flag; a thread that finds it
//  claimed leaves a request and moves on, and the claimant runs again for it (claim and move on,
//  the style guide's Control flow), so nothing on the encode's path waits.
//
#include "IrisSlideEncoder_.hpp"

#include <algorithm>
#include <atomic>
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

namespace IrisSlide::Rebuilt {
namespace {
constexpr std::size_t RGBA = 4;
constexpr std::uint64_t TILE_BYTES = std::uint64_t{RECUT_TILE} * RECUT_TILE * RGBA;
/// The default budget is this fraction of physical memory (Q20, decided 2026-11-27).
constexpr std::uint64_t BUDGET_DIVISOR = 4;
/// A derived row reads up to 4 rows of the layer below; with the row being made, 5 rows of each
/// layer is the smallest working set an encode can progress in.
constexpr std::uint64_t ROWS_HELD = 5;
constexpr int QUALITY_MIN = 1, QUALITY_MAX = 100;

std::uint64_t physical_memory () noexcept
{
#if defined(__APPLE__)
    std::uint64_t bytes = 0;
    std::size_t length = sizeof(bytes);
    return sysctlbyname("hw.memsize", &bytes, &length, nullptr, 0) == 0 ? bytes : 0;
#elif defined(_WIN32)
    MEMORYSTATUSEX status {};
    status.dwLength = sizeof(status);
    return GlobalMemoryStatusEx(&status) ? status.ullTotalPhys : 0;
#else
    const long pages = sysconf(_SC_PHYS_PAGES), page = sysconf(_SC_PAGE_SIZE);
    return pages > 0 && page > 0 ? std::uint64_t(pages) * std::uint64_t(page) : 0;
#endif
}

/// The encoding's file-name word, or null for one this encoder does not write.
const char* written_encoding (Encoding encoding) noexcept
{
    switch (encoding) {
        case TILE_ENCODING_JPEG:      return "jpeg";
        case TILE_ENCODING_UNDEFINED:
        case TILE_ENCODING_IRIS:
        case TILE_ENCODING_AV1:
        case TILE_ENCODING_UASTC:
        case TILE_ENCODING_XUASTC:    return nullptr;
    }
    return nullptr;
}
const char* encoding_name (Encoding encoding) noexcept
{
    switch (encoding) {
        case TILE_ENCODING_UNDEFINED: return "TILE_ENCODING_UNDEFINED";
        case TILE_ENCODING_IRIS:      return "TILE_ENCODING_IRIS";
        case TILE_ENCODING_JPEG:      return "TILE_ENCODING_JPEG";
        case TILE_ENCODING_AV1:       return "TILE_ENCODING_AV1";
        case TILE_ENCODING_UASTC:     return "TILE_ENCODING_UASTC";
        case TILE_ENCODING_XUASTC:    return "TILE_ENCODING_XUASTC";
    }
    return "an unknown tile encoding";
}

/// The encoder's one way out for pixels: a 256-pixel RGBA8 tile compressed in the IFE tile
/// encoding the file declares. An encoding is one case here (JPEG: libjpeg-turbo through the
/// codec context).
Buffer compress_tile (const Context& context, Encoding encoding, std::vector<std::uint8_t>& rgba,
                      Quality quality, Subsampling subsampling)
{
    switch (encoding) {
        case TILE_ENCODING_JPEG:
            return context->compress_tile({.pixelArray  = Iris::Wrap_weak_buffer_fom_data(rgba.data(), rgba.size()),
                                           .format      = Iris::FORMAT_R8G8B8A8,
                                           .encoding    = encoding,
                                           .quality     = quality,
                                           .subsampling = subsampling});
        case TILE_ENCODING_UNDEFINED:
        case TILE_ENCODING_IRIS:
        case TILE_ENCODING_AV1:
        case TILE_ENCODING_UASTC:
        case TILE_ENCODING_XUASTC:
            break;
    }
    throw std::runtime_error(std::string("the rebuilt encoder does not write ") + encoding_name(encoding));
}

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
    std::uint32_t     across = 0, down = 0;
    std::vector<Row>  rows;
};
struct Job {
    std::uint32_t depth;   // 0 is the source's own layer, the highest resolution
    std::uint32_t row;
};
/// The rows of layer depth - 1 that row @p row of layer depth reads: its region spans source
/// rows 512·row - 3 to 512·row + 515, tile rows 2·row - 1 to 2·row + 2, clamped to the layer.
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
    bool can_start (const Job& job) const noexcept;
    void start (const Job& job);
    void read_row (std::uint32_t row) noexcept;
    void work_tile (std::uint32_t depth, std::uint32_t row, std::uint32_t x) noexcept;
    void tile_done (std::uint32_t depth, std::uint32_t row) noexcept;
    void write (const Job& job);
    void release_pixels (Row& row) noexcept;
    void finish ();
    void fail (const std::string& why) noexcept;
    void charge (std::int64_t bytes) noexcept;
    void issue (auto&& body);

    EncoderDesc                  _desc;
    std::filesystem::path        _file, _scratch;
    std::unique_ptr<Source>      _source;
    Context                      _context;
    std::vector<Layer>           _layers;
    std::vector<Job>             _schedule;
    std::size_t                  _next_job = 0, _next_write = 0;   // advance()'s, under its claim
    std::uint64_t                _tiles_total = 0;
    std::atomic<std::uint64_t>   _tiles_written {0};
    std::uint64_t                _budget;
    std::atomic<std::int64_t>    _resident {0};
    std::atomic<std::int64_t>    _peak {0};
    std::atomic<bool>            _reading {false};
    std::atomic<bool>            _advancing {false};
    std::atomic<std::uint32_t>   _advance_requests {0};
    std::atomic<EncoderState>    _state {EncoderState::READY};
    std::atomic_flag             _failure_claimed = ATOMIC_FLAG_INIT;
    std::string                  _error;
    Iris::File::Builder          _builder;
    // Declared last, destroyed first: the pool's destructor runs its pending tasks, so nothing
    // above is destroyed while a task can still reach it.
    std::unique_ptr<Iris::Async::Pool> _pool;
};

Encoder_t::Encoder_t (const EncoderDesc& desc, std::unique_ptr<Source> source, std::uint64_t budget) :
_desc       (desc),
_file       (desc.destination / output_name(desc.source, desc.encoding)),
_scratch    (desc.destination / ("." + output_name(desc.source, desc.encoding) + ".partial")),
_source     (std::move(source)),
_context    (std::make_shared<Context_t>(ContextCreateInfo {})),
_budget     (budget)
{
    // The layers, the source's first, each the 2x reduction of the one before, until one tile.
    for (LayerSize size = _source->size();; size = reduced(size)) {
        Layer& layer = _layers.emplace_back();
        layer.size   = size;
        layer.across = tiles_across(size);
        layer.down   = tiles_down(size);
        layer.rows   = std::vector<Row>(layer.down);
        for (auto& row : layer.rows) {
            row.pixels.resize(layer.across);
            row.streams.resize(layer.across);
        }
        _tiles_total += std::uint64_t{layer.across} * layer.down;
        if (layer.across == 1 && layer.down == 1) break;
    }
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
    _pool.reset();                                   // runs or discards every pending task first
    _builder = Iris::File::Builder();                // releases the scratch file's mapping
    if (_state.load() != EncoderState::DONE) {
        std::error_code ignored;                     // nothing to report: the file may never have been made
        std::filesystem::remove(_scratch, ignored);
    }
}

void Encoder_t::issue (auto&& body)
{
    if (!_pool->issue_task(std::forward<decltype(body)>(body)))
        fail("the encoder's thread pool refused a task: it was terminated");
}

void Encoder_t::dispatch ()
{
    auto expected = EncoderState::READY;
    if (!_state.compare_exchange_strong(expected, EncoderState::ACTIVE))
        throw std::runtime_error("the encoder was already dispatched; an encoder encodes once");
    std::filesystem::remove(_scratch);
    _builder = Iris::File::Builder::create({
        // A compressed tile never outgrows its raw pixels; only written pages cost disk.
        .capacity    = Iris::File::BuilderCreateInfo{}.capacity + _tiles_total * TILE_BYTES,
        .filepath    = _scratch,
        .tile_frames = false,
    });
    Iris::Extent extent {.width  = static_cast<std::uint32_t>(_layers.back().size.width),
                         .height = static_cast<std::uint32_t>(_layers.back().size.height)};
    // IFE numbers layers from the lowest resolution, whose scale is 1.
    for (std::size_t l = 0; l < _layers.size(); ++l) {
        const Layer& layer = _layers[_layers.size() - 1 - l];
        extent.layers.push_back({.xTiles = layer.across, .yTiles = layer.down,
                                 .scale = float(1u << l), .downsample = float(1u << (_layers.size() - 1 - l))});
    }
    _builder.set_tile_table({.encoding = _desc.encoding, .format = Iris::FORMAT_R8G8B8A8, .extent = extent});
    _pool = std::make_unique<Iris::Async::Pool>(std::max<std::uint32_t>(1, _desc.concurrency));
    advance();
}

void Encoder_t::advance () noexcept
{
    _advance_requests.fetch_add(1);
    if (_advancing.exchange(true)) return;           // the claimant will see the request
    for (;;) {
        _advance_requests.store(0);
        try {
            if (_state.load() == EncoderState::ACTIVE) step();
        } catch (const std::exception& error) {
            fail(error.what());
        }
        _advancing.store(false);
        if (_advance_requests.load() == 0 || _advancing.exchange(true)) return;
    }
}

bool Encoder_t::can_start (const Job& job) const noexcept
{
    if (job.depth == 0) {
        if (_reading.load()) return false;           // a Source is read by one task at a time
        const bool idle = _next_write == _next_job;  // nothing else holds memory that will free
        return idle || _resident.load() + std::int64_t(_layers[0].across * TILE_BYTES) <= std::int64_t(_budget);
    }
    const auto [first, last] = rows_read(job.row, _layers[job.depth - 1].down);
    for (std::uint32_t k = first; k <= last; ++k)
        if (_layers[job.depth - 1].rows[k].stage.load() < Row::Stage::DONE) return false;
    return true;
}

void Encoder_t::step ()
{
    while (_next_job < _schedule.size() && can_start(_schedule[_next_job])) start(_schedule[_next_job++]);
    while (_next_write < _next_job && _layers[_schedule[_next_write].depth].rows[_schedule[_next_write].row].stage.load() == Row::Stage::DONE)
        write(_schedule[_next_write++]);
    if (_next_write == _schedule.size()) finish();
}

void Encoder_t::start (const Job& job)
{
    Row& row = _layers[job.depth].rows[job.row];
    const std::uint32_t across = _layers[job.depth].across;
    row.remaining.store(across);
    row.stage.store(Row::Stage::WORKING);
    charge(std::int64_t(across * TILE_BYTES));
    if (job.depth == 0) {
        _reading.store(true);
        issue([this, r = job.row] { read_row(r); });
        return;
    }
    for (std::uint32_t x = 0; x < across; ++x)
        issue([this, d = job.depth, r = job.row, x] { work_tile(d, r, x); });
}

void Encoder_t::read_row (std::uint32_t r) noexcept
{
    try {
        Row& row = _layers[0].rows[r];
        for (std::uint32_t x = 0; x < _layers[0].across && _state.load() == EncoderState::ACTIVE; ++x) {
            row.pixels[x].resize(TILE_BYTES);
            _source->read_tile(x, r, row.pixels[x]);
        }
        _reading.store(false);
        for (std::uint32_t x = 0; x < _layers[0].across; ++x)
            issue([this, r, x] { work_tile(0, r, x); });
    } catch (const std::exception& error) {
        fail(error.what());
    }
    advance();
}

void Encoder_t::work_tile (std::uint32_t depth, std::uint32_t r, std::uint32_t x) noexcept
{
    try {
        if (_state.load() != EncoderState::ACTIVE) return;
        Row& row = _layers[depth].rows[r];
        if (depth > 0) {
            // Gather this tile's region from the layer below, reduce it, and make what lies
            // beyond this layer's extent white.
            const Layer& below = _layers[depth - 1];
            std::vector<std::uint8_t> region(std::size_t{REDUCE_REGION} * REDUCE_REGION * RGBA);
            gather_region(below.size, x, r,
                          [&below](std::uint32_t tx, std::uint32_t ty) {
                              return std::span<const std::uint8_t>(below.rows[ty].pixels[tx]);
                          },
                          region);
            row.pixels[x].resize(TILE_BYTES);
            reduce_2x(region, row.pixels[x]);
            whiten_beyond(_layers[depth].size, x, r, row.pixels[x]);
        }
        row.streams[x] = compress_tile(_context, _desc.encoding, row.pixels[x], _desc.quality, _desc.subsampling);
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
    if (row.remaining.fetch_sub(1) != 1) return;     // another tile of the row is still working
    row.stage.store(Row::Stage::DONE);
    advance();
}

void Encoder_t::write (const Job& job)
{
    Layer& layer = _layers[job.depth];
    Row& row = layer.rows[job.row];
    const auto ife_layer = static_cast<std::uint32_t>(_layers.size() - 1 - job.depth);
    for (std::uint32_t x = 0; x < layer.across; ++x) {
        Buffer& stream = row.streams[x];
        _builder.append_tile(ife_layer, job.row * layer.across + x,
                             static_cast<const BYTE*>(stream->data()), stream->size(), 0);
        charge(-std::int64_t(stream->size()));
        stream = nullptr;
    }
    _tiles_written.fetch_add(layer.across);
    row.stage.store(Row::Stage::WRITTEN);
    if (row.readers == 0) release_pixels(row);
    if (job.depth == 0) return;
    const auto [first, last] = rows_read(job.row, _layers[job.depth - 1].down);
    for (std::uint32_t k = first; k <= last; ++k) {
        Row& below = _layers[job.depth - 1].rows[k];
        if (--below.readers == 0 && below.stage.load() == Row::Stage::WRITTEN) release_pixels(below);
    }
}

void Encoder_t::release_pixels (Row& row) noexcept
{
    std::int64_t freed = 0;
    for (auto& pixels : row.pixels) {
        freed += std::int64_t(pixels.capacity() ? TILE_BYTES : 0);
        std::vector<std::uint8_t>().swap(pixels);
    }
    charge(-freed);
}

void Encoder_t::finish ()
{
    using namespace Iris::File::Serialization;
    // The structure after the tiles, as IFE lays it out; only the source's measurements are
    // carried (D2): no attributes, no associated images.
    const auto& measured = _source->measurements();
    const float to_lowest = float(1u << (_layers.size() - 1));   // the lowest layer's downsample
    std::vector<LayerExtentEntry> extents;
    for (std::size_t l = 0; l < _layers.size(); ++l) {
        const Layer& layer = _layers[_layers.size() - 1 - l];
        extents.push_back({.X_TILES = layer.across, .Y_TILES = layer.down, .SCALE = float(1u << l), .Z_PLANES = 0});
    }
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
    const Version codec = get_codec_version();
    _builder->fill(metadata_at, MetadataCreateInfo{
        .CODEC_MAJOR       = static_cast<std::uint16_t>(codec.major),
        .CODEC_MINOR       = static_cast<std::uint16_t>(codec.minor),
        .CODEC_BUILD       = static_cast<std::uint16_t>(codec.build),
        .ICC_COLOR_OFFSET  = icc_at,
        // IFE normalises both to layer scale 1, the lowest resolution.
        .MICRONS_PIXEL     = float(measured.microns_per_pixel) * to_lowest,
        .MAGNIFICATION     = float(measured.magnification) / to_lowest,
    });
    _builder.finalize({.tileTable = table_at, .metadata = metadata_at});
    _builder = Iris::File::Builder();   // the mapping is released before the rename
    std::filesystem::rename(_scratch, _file);
    _state.store(EncoderState::DONE);
}

void Encoder_t::fail (const std::string& why) noexcept
{
    if (_failure_claimed.test_and_set()) return;     // the first failure is the one reported
    try { _error = why; } catch (...) {}             // a failed copy leaves the state to say it failed
    auto active = EncoderState::ACTIVE;
    _state.compare_exchange_strong(active, EncoderState::FAILED);
}

void Encoder_t::charge (std::int64_t bytes) noexcept
{
    const std::int64_t now = _resident.fetch_add(bytes) + bytes;
    std::int64_t peak = _peak.load();
    while (now > peak && !_peak.compare_exchange_weak(peak, now)) {}
}

EncoderProgress Encoder_t::snapshot () const
{
    const EncoderState state = _state.load();
    return {.state               = state,
            .fraction            = float(_tiles_written.load()) / float(_tiles_total),
            .file                = _file,
            .error               = state == EncoderState::FAILED ? _error : std::string(),
            .resident_bytes      = std::uint64_t(std::max<std::int64_t>(0, _resident.load())),
            .peak_resident_bytes = std::uint64_t(std::max<std::int64_t>(0, _peak.load())),
            .memory_budget       = _budget};
}

void Encoder_t::interrupt ()
{
    auto active = EncoderState::ACTIVE;
    if (!_state.compare_exchange_strong(active, EncoderState::INTERRUPTED))
        throw std::runtime_error("only an active encode is interrupted");
    _pool->terminate();
}

// MARK: - The boundary

std::string output_name (const std::filesystem::path& source, Encoding encoding)
{
    const char* word = written_encoding(encoding);
    if (!word) throw std::runtime_error(std::string("the rebuilt encoder does not write ") + encoding_name(encoding));
    return source.stem().string() + "." + word + ".iris";
}

Result create_encoder (const EncoderDesc& desc, Encoder& encoder) noexcept
{
    std::unique_ptr<Source> source;
    try {
        source = open_source(desc.source);
    } catch (const std::exception& error) {
        return codec_failure(SlideError::ENCODER_FAILED, error.what());
    }
    return create_encoder(desc, std::move(source), encoder);
}

Result create_encoder (const EncoderDesc& desc, std::unique_ptr<Source> source, Encoder& encoder) noexcept
{
    try {
        if (!source) throw std::runtime_error("create_encoder was given no source");
        if (!written_encoding(desc.encoding))
            throw std::runtime_error(std::string("the rebuilt encoder does not write ") + encoding_name(desc.encoding));
        if (desc.quality < QUALITY_MIN || desc.quality > QUALITY_MAX)
            throw std::runtime_error("quality " + std::to_string(desc.quality) + " is outside 1-100");
        if (!std::filesystem::is_directory(desc.destination))
            throw std::runtime_error("the destination " + desc.destination.string() + " is not a folder");
        std::uint64_t working_set = 0;
        for (LayerSize size = source->size();; size = reduced(size)) {
            working_set += ROWS_HELD * tiles_across(size) * TILE_BYTES;
            if (tiles_across(size) == 1 && tiles_down(size) == 1) break;
        }
        const std::uint64_t budget = desc.memory_budget ? desc.memory_budget : physical_memory() / BUDGET_DIVISOR;
        if (budget < working_set)
            throw std::runtime_error("a memory budget of " + std::to_string(budget) + " bytes is below the " +
                                     std::to_string(working_set) + " bytes this slide needs at once");
        encoder = std::make_shared<Encoder_t>(desc, std::move(source), budget);
        return Result::success();
    } catch (const std::exception& error) {
        return codec_failure(SlideError::ENCODER_FAILED, error.what());
    } catch (...) {
        return codec_failure(SlideError::ENCODER_FAILED, "create_encoder failed with an unknown exception");
    }
}

Result dispatch (const Encoder& encoder) noexcept
{
    try {
        if (!encoder) throw std::runtime_error("dispatch was given no encoder");
        encoder->dispatch();
        return Result::success();
    } catch (const std::exception& error) {
        return codec_failure(SlideError::ENCODER_FAILED, error.what());
    } catch (...) {
        return codec_failure(SlideError::ENCODER_FAILED, "dispatch failed with an unknown exception");
    }
}

Result progress (const Encoder& encoder, EncoderProgress& progress) noexcept
{
    try {
        if (!encoder) throw std::runtime_error("progress was given no encoder");
        progress = encoder->snapshot();
        return Result::success();
    } catch (const std::exception& error) {
        return codec_failure(SlideError::ENCODER_FAILED, error.what());
    } catch (...) {
        return codec_failure(SlideError::ENCODER_FAILED, "progress failed with an unknown exception");
    }
}

Result interrupt (const Encoder& encoder) noexcept
{
    try {
        if (!encoder) throw std::runtime_error("interrupt was given no encoder");
        encoder->interrupt();
        return Result::success();
    } catch (const std::exception& error) {
        return codec_failure(SlideError::ENCODER_FAILED, error.what());
    } catch (...) {
        return codec_failure(SlideError::ENCODER_FAILED, "interrupt failed with an unknown exception");
    }
}
} // END IRIS SLIDE REBUILT NAMESPACE
