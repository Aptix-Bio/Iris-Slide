//
//  IrisSlideEncoder.hpp
//  Iris
//
//  The encoder (ARCHITECTURE D14). Its public surface (EncoderDesc, create_encoder,
//  dispatch_encoder, encoder_progress, interrupt_encoder, encoder_output_name) is declared in
//  Iris-Headers' IrisSlideCore.hpp. One way in, one way out: the encoder reads a Source (open_source
//  is the only code that knows file formats) and writes compressed tiles through IFE's Builder
//  (compress_tile is the only code that knows tile encodings).
//
#ifndef IrisSlideEncoder_hpp
#define IrisSlideEncoder_hpp
#include <memory>

#include "IrisSlideCore.hpp"
#include "IrisSlideMemoryPressure.hpp"
#include "IrisSlideSource.hpp"

namespace IrisSlide {
// RTM Encode_09
/// Reads the operating system's memory pressure (memory_pressure, or a test's stand-in).
using PressureProbe = MemoryPressure (*)() noexcept;
/// create_encoder with the source already open: the core's one way in, which the public
/// create_encoder funnels to after open_source; tests and future sources call it directly.
/// @p probe is read for the system's memory pressure (D26); a test passes its own.
[[nodiscard]] Result create_encoder (const EncoderDesc& desc, std::unique_ptr<Source> source, Encoder& encoder,
                                     PressureProbe probe = memory_pressure) noexcept;
} // END IRIS SLIDE NAMESPACE
#endif /* IrisSlideEncoder_hpp */
