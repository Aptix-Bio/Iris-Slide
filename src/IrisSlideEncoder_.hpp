//
//  IrisSlideEncoder_.hpp
//  Iris
//
//  The rebuilt encoder (ARCHITECTURE D14), written beside the current one and cut over to
//  IrisSlideEncoder.cpp when done; until then its surface is private to Iris-Slide, in the
//  namespace IrisSlide::Rebuilt, so it does not collide with the current encoder's.
//
//  One way in, one way out: the encoder reads a Source (open_source is the only code that knows
//  file formats) and writes compressed tiles through IFE's Builder (compress_tile is the only code
//  that knows tile encodings). Between them it derives each lower layer by the 2x reduction.
//
#ifndef IrisSlideEncoder__hpp
#define IrisSlideEncoder__hpp
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>

#include "IrisCore.hpp"
#include "IrisSlideCore.hpp"
#include "IrisSlideSource.hpp"

namespace IrisSlide::Rebuilt {
// RTM Encode_09
/// What to encode, where, and how.
struct EncoderDesc {
    std::filesystem::path source;                       ///< the slide; open_source reads it
    std::filesystem::path destination;                  ///< the folder the .iris file is written to
    Encoding      encoding      = TILE_ENCODING_JPEG;   ///< the IFE tile encoding written
    Quality       quality       = QUALITY_DEFAULT;      ///< JPEG quality, 1-100 (Q19: 90)
    Subsampling   subsampling   = SUBSAMPLE_DEFAULT;    ///< JPEG chroma subsampling (Q19: 4:2:2)
    std::uint64_t memory_budget = 0;                    ///< bytes resident at once; 0: a quarter of physical memory (Q20)
    std::uint32_t concurrency   = std::thread::hardware_concurrency();   ///< worker threads, at least 1
    /// The scale between written layers: 2, 4, 8 … (IFE gives each layer its own scale). Layers are
    /// derived 2x at a time and every log2(step)-th is written, down to one that fits one tile.
    std::uint32_t layer_step    = 2;
};

/// An encoder is used once: dispatched from READY, it ends DONE, FAILED or INTERRUPTED.
enum class EncoderState : std::uint8_t { READY, ACTIVE, DONE, FAILED, INTERRUPTED };

/// A snapshot of an encode.
struct EncoderProgress {
    EncoderState          state = EncoderState::READY;
    float                 fraction = 0.f;               ///< tiles written over tiles in the file
    std::filesystem::path file;                         ///< the .iris file it writes
    std::string           error;                        ///< why it FAILED, naming the offending value
    std::uint64_t         resident_bytes = 0;           ///< tile bytes held now
    std::uint64_t         peak_resident_bytes = 0;      ///< the most ever held
    std::uint64_t         memory_budget = 0;            ///< the budget it keeps to
};

class Encoder_t;
using Encoder = std::shared_ptr<Encoder_t>;

/// An encoder for desc.source (opened with open_source). Refuses, naming the value: an encoding
/// this encoder does not write, a quality outside 1-100, a destination that is not a folder, a
/// memory budget below the smallest working set the slide needs, and a source open_source refuses.
[[nodiscard]] Result create_encoder (const EncoderDesc& desc, Encoder& encoder) noexcept;
/// The same with the source already open: the core's one way in, which create_encoder funnels to.
[[nodiscard]] Result create_encoder (const EncoderDesc& desc, std::unique_ptr<Source> source, Encoder& encoder) noexcept;
/// Starts the encode on the encoder's own thread pool and returns. Refuses an encoder that is not READY.
[[nodiscard]] Result dispatch (const Encoder& encoder) noexcept;
[[nodiscard]] Result progress (const Encoder& encoder, EncoderProgress& progress) noexcept;
/// Stops an ACTIVE encode; it ends INTERRUPTED and leaves no file.
[[nodiscard]] Result interrupt (const Encoder& encoder) noexcept;
/// The output file name for @p source and @p encoding, <source stem>.<encoding>.iris, into
/// @p name. Refuses an encoding this encoder does not write, naming it.
[[nodiscard]] Result output_name (const std::filesystem::path& source, Encoding encoding, std::string& name) noexcept;
} // END IRIS SLIDE REBUILT NAMESPACE
#endif /* IrisSlideEncoder__hpp */
