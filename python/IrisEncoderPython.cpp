//
//  IrisEncoderPython.cpp
//  Iris
//
//  Iris.Encoder's native module: the encoder's public API (IrisSlideCore.hpp), every failing
//  Result raised as RuntimeError with its message, so a script cannot miss a refusal.
//
#include <cstdint>
#include <map>
#include <optional>
#include <string>

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/stl/filesystem.h>
#include <pybind11/numpy.h>
namespace py = pybind11;

#include "IrisTypesPython.hpp"
#include "IrisSlidePython.hpp"
#include "IrisSlideEncoderOptions.hpp"

namespace {
// RTM Bindings_01
/// Python's handle on an encoder: the encoder's object type is defined in its own source file.
struct EncoderHandle {
    IrisSlide::Encoder encoder;
};
void raise_if_failed (const Iris::Result& result)
{
    if (result.failed()) throw std::runtime_error(result.message);
}
/// The encoder's own defaults, so Python's keyword defaults are the core's and are written once.
const IrisSlide::EncoderDesc DEFAULTS;

EncoderHandle create (const std::string& source, const std::string& destination, std::uint64_t quality,
                      const std::optional<std::string>& subsampling, std::uint64_t memory_budget, std::uint64_t concurrency,
                      std::uint64_t layer_step, const std::map<std::string, std::string>& i2s_attributes,
                      std::uint64_t i2s_version, const std::string& encoding, std::uint64_t effort)
{
    // Python's int is unbounded: each is range-checked before it narrows (raised as ValueError),
    // so a large number is refused instead of wrapped.
    const IrisSlide::Encoding tiles = IrisSlide::encoding_of(encoding);
    IrisSlide::EncoderDesc desc {.source        = source,
                                 .destination   = destination,
                                 .encoding      = tiles,
                                 .quality       = IrisSlide::narrowed<IrisSlide::Quality>(quality, "quality"),
                                 // The encoding's default unless named: 4:2:0 alone for AV1 (D22).
                                 .subsampling   = subsampling ? IrisSlide::subsampling_of(*subsampling)
                                                              : IrisSlide::default_subsampling(tiles),
                                 .memory_budget = memory_budget,
                                 .concurrency   = IrisSlide::narrowed<std::uint32_t>(concurrency, "concurrency"),
                                 .layer_step    = IrisSlide::narrowed<std::uint32_t>(layer_step, "layer step"),
                                 .effort        = IrisSlide::narrowed<IrisSlide::Effort>(effort, "effort")};
    // RTM Identity_07: Python's str arrives as UTF-8; the encoder checks the pairs and the version.
    if (!i2s_attributes.empty()) {
        desc.attributes.type    = IrisSlide::METADATA_I2S;
        desc.attributes.version = IrisSlide::narrowed<std::uint16_t>(i2s_version, "I2S version");
        for (const auto& [key, value] : i2s_attributes)
            desc.attributes[key] = std::u8string(value.begin(), value.end());
    }
    EncoderHandle handle;
    raise_if_failed(IrisSlide::create_encoder(desc, handle.encoder));
    return handle;
}
} // namespace

PYBIND11_MODULE(encoder, m)
{
    py::enum_<IrisSlide::EncoderState>(m, "EncoderState")
        .value("READY",       IrisSlide::EncoderState::READY)
        .value("ACTIVE",      IrisSlide::EncoderState::ACTIVE)
        .value("DONE",        IrisSlide::EncoderState::DONE)
        .value("FAILED",      IrisSlide::EncoderState::FAILED)
        .value("INTERRUPTED", IrisSlide::EncoderState::INTERRUPTED);
    py::class_<IrisSlide::EncoderProgress>(m, "EncoderProgress", "A snapshot of an encode")
        .def_readonly("state",               &IrisSlide::EncoderProgress::state)
        .def_readonly("fraction",            &IrisSlide::EncoderProgress::fraction)
        .def_property_readonly("file",       [](const IrisSlide::EncoderProgress& p) { return p.file.string(); })
        .def_readonly("error",               &IrisSlide::EncoderProgress::error)
        .def_readonly("resident_bytes",      &IrisSlide::EncoderProgress::resident_bytes)
        .def_readonly("peak_resident_bytes", &IrisSlide::EncoderProgress::peak_resident_bytes)
        .def_readonly("memory_budget",       &IrisSlide::EncoderProgress::memory_budget);
    py::class_<EncoderHandle>(m, "Encoder", "An encoder for one slide; it encodes once");
    m.def("create_encoder", &create,
          "An encoder for one slide (a DICOM whole-slide instance: JPEG, JPEG 2000 or HTJ2K frames), writing "
          "<source stem>.<encoding>.iris into destination, its tiles 'jpeg' or 'av1'; subsampling left as None "
          "takes the encoding's default ('422' for jpeg; av1 is '420' alone); effort is how hard to work on each tile, 0 (fastest) to 6 (slowest, usually smallest), av1 only; raises RuntimeError naming what it refuses",
          py::arg("source"), py::arg("destination"), py::arg("quality") = std::uint64_t{DEFAULTS.quality},
          py::arg("subsampling") = std::optional<std::string>(),
          py::arg("memory_budget") = DEFAULTS.memory_budget,
          // Shown as "..." in signatures and stubs: the default is this machine's hardware threads.
          py::arg_v("concurrency", std::uint64_t{DEFAULTS.concurrency}, "..."),
          py::arg("layer_step") = std::uint64_t{DEFAULTS.layer_step},
          py::arg("i2s_attributes") = std::map<std::string, std::string>(), py::arg("i2s_version") = std::uint64_t{0},
          // RTM Encode_16: appended, so a caller passing the earlier arguments by position is unchanged.
          py::arg("encoding") = std::string(IrisSlide::encoding_word(DEFAULTS.encoding)),
          py::arg("effort") = std::uint64_t{DEFAULTS.effort});
    m.def("dispatch_encoder", [](const EncoderHandle& e) { raise_if_failed(IrisSlide::dispatch_encoder(e.encoder)); },
          "Starts the encode and returns; raises when the encoder was already dispatched", py::arg("encoder"));
    m.def("encoder_progress", [](const EncoderHandle& e) {
              IrisSlide::EncoderProgress progress;
              raise_if_failed(IrisSlide::encoder_progress(e.encoder, progress));
              return progress;
          }, "A snapshot of the encode", py::arg("encoder"));
    m.def("interrupt_encoder", [](const EncoderHandle& e) { raise_if_failed(IrisSlide::interrupt_encoder(e.encoder)); },
          "Stops an active encode; it leaves no file", py::arg("encoder"));
    m.def("encoder_output_name", [](const std::string& source, const std::string& encoding) {
              std::string name;
              raise_if_failed(IrisSlide::encoder_output_name(source, IrisSlide::encoding_of(encoding), name));
              return name;
          }, "The file name an encode of source writes in encoding ('jpeg' or 'av1')", py::arg("source"),
          py::arg("encoding") = std::string(IrisSlide::encoding_word(DEFAULTS.encoding)));
}
