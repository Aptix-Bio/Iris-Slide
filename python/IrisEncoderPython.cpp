//
//  IrisEncoderPython.cpp
//  Iris
//
//  Created by Ryan Landvater on 2/6/25.
//

#include <map>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/numpy.h>
namespace py    = pybind11;

#include "IrisTypesPython.hpp"
#include "IrisSlidePython.hpp"
namespace IrisSlide {
inline std::string to_string (EncoderStatus status)
{
    switch (status) {
        case ENCODER_INACTIVE:  return "ENCODER_INACTIVE";
        case ENCODER_ACTIVE:    return "ENCODER_ACTIVE";
        case ENCODER_ERROR:     return "ENCODER_ERROR";
        case ENCODER_SHUTDOWN:  return "ENCODER_SHUTDOWN";
    }   return "CORRUPT ENCODER STATUS IDENTIFIED";
}
inline std::string to_string(Encoding encoding)
{
    switch (encoding) {
        case TILE_ENCODING_IRIS: return "ENCODING_IRIS";
        case TILE_ENCODING_JPEG: return "ENCODING_JPEG";
        case TILE_ENCODING_AV1:  return "ENCODING_AV1";
        case TILE_ENCODING_UASTC:  return "ENCODING_UASTC";
        case TILE_ENCODING_XUASTC: return "ENCODING_XUASTC";
        default:
            return "ENCODING UNDEFINED";
    }       return "ENCODING UNDEFINED";
}
inline std::string to_string(Format format)
{
    switch (format) {
        case Iris::FORMAT_B8G8R8:   return "FORMAT_B8G8R8";
        case Iris::FORMAT_R8G8B8:   return "FORMAT_R8G8B8";
        case Iris::FORMAT_B8G8R8A8: return "FORMAT_B8G8R8A8";
        case Iris::FORMAT_R8G8B8A8: return "FORMAT_R8G8B8A8";
        default:
            return "FORMAT_UNDEFINED";
    }       return "FORMAT_UNDEFINED";
}
inline Encoder _create_encoder (const std::string& src_path,
                                const std::string& dst_path,
                                IrisSlide::Encoding desired_encoding,
                                Iris::Format desired_format,
                                bool anonymize,
                                EncoderDerivation::Layers derivation,
                                unsigned concurrency,
                                Context context,
                                const std::map<std::string, std::string>& i2s_attributes,
                                uint16_t i2s_version) {
    EncoderDerivation d_strategy {
        .layers             = derivation,
        .method             = EncoderDerivation::ENCODER_DOWNSAMPLE_AVERAGE
    };
    EncodeSlideInfo encoder_info {
        .srcFilePath        = src_path,
        .dstFilePath        = dst_path,
        .srcFormat          = FORMAT_UNDEFINED,
        .desiredEncoding    = desired_encoding,
        .desiredFormat      = desired_format,
        .anonymize          = anonymize,
        .concurrency        = concurrency,
        .context            = context,
        .derivation         = derivation?&d_strategy:NULL,
    };
    // RTM Identity_07: Python's str arrives as UTF-8, and the encoder checks the pairs and the
    // version when it is dispatched, naming the pair it refuses.
    if (!i2s_attributes.empty()) {
        encoder_info.attributes.type    = METADATA_I2S;
        encoder_info.attributes.version = i2s_version;
        for (const auto& [key, value] : i2s_attributes)
            encoder_info.attributes[key] = std::u8string(value.begin(), value.end());
    }
    return IrisSlide::create_encoder(encoder_info);
}
inline std::tuple<Result,EncoderProgress> _get_encoder_progress (const Encoder& encoder)
{
    EncoderProgress progress;
    return {get_encoder_progress (encoder, progress), progress};
}
inline std::tuple<Result,std::string> _get_encoder_src (const Encoder& encoder)
{
    std::string source_path;
    return {get_encoder_src(encoder, source_path), source_path};
}
inline std::tuple<Result,std::string> _get_encoder_dst_path(const Encoder& encoder)
{
    std::string dst_path;
    return {get_encoder_dst_path(encoder, dst_path), dst_path};
}
}
PYBIND11_MODULE(encoder, m)
{
    using namespace IrisSlide;
    py::enum_<EncoderStatus>                                (m,"EncoderStatus")
        .value("ENCODER_INACTIVE",      ENCODER_INACTIVE)
        .value("ENCODER_ACTIVE",        ENCODER_ACTIVE)
        .value("ENCODER_ERROR",         ENCODER_ERROR)
        .value("ENCODER_SHUTDOWN",      ENCODER_SHUTDOWN);
    
    py::enum_<EncoderDerivation::Layers>                    (m,"EncoderDerivation")
        .value("use_source",            EncoderDerivation::ENCODER_DERIVE_USE_SOURCE)
        .value("layer_2x",              EncoderDerivation::ENCODER_DERIVE_2X_LAYERS)
        .value("layer_4x",              EncoderDerivation::ENCODER_DERIVE_4X_LAYERS);
    
    py::class_<EncoderProgress>                             (m, "EncoderProgress")
        .def_readonly("status",         &EncoderProgress::status)
        .def_readonly("progress",       &EncoderProgress::progress)
        .def_readonly("dstFilePath",    &EncoderProgress::dstFilePath)
        .def_readonly("errorMsg",       &EncoderProgress::errorMsg)
        .doc() = "Encoder progress monitoring structure that returns a snapshot containing the  status of the encoder's progress through an encoding session at the time the get_encoder_progress method was called. If the encoder returns an error status (ENCODER_ERROR), the errorMsg should contain any explaination or explainations";
    
    py::class_<Encoder_t, Encoder>                (m, "Encoder")
        .def("__repr__",[](const Encoder &encoder) {
            std::stringstream descriptor;
            descriptor  << "Iris Slide Encoder Instance <" << encoder.get() << ">:"
                        << "\n\tCodec Context: <"<< encoder->get_context().get()<<">"
                        << "\n\tSource file: " << encoder->get_src_path()
                        << "\n\tDestination: " << (encoder->get_dst_path().size()?encoder->get_dst_path():"{Source Dir}")
                        << "\n\tEncoding: " << to_string(encoder->get_encoding())
                        << "\n\tEncoder Status: " << to_string(encoder->get_status()) << std::endl;
            return descriptor.str();
        })
        .doc()="Slide Encoder class used internally to generate a slide file. This class is stateful, thread-safe, and encodes a single slide at a time in parrallel using a massive amount of the machine's cores. Dispatching an encoder will bring your machine up to about 90-95% of the full CPU capacity so it is not recommended to use more than a single encoder at a time.";

    m.def("create_encoder",&_create_encoder,
          "Method to create an encoder object",
          py::arg("source"),
          "File path to the source slide: DICOM or .iris, and vendor formats in an encoder built with IRIS_USE_OPENSLIDE=ON",
          py::arg("outdir") = std::string(),
          "File path to the output file directory. The encoder will name the file XXX.iris where XXX represents the previous file name (ex /path/to/slide.svs will be named /outdir/slide.iris)",
          py::arg("desired_encoding") = IrisSlide::TILE_ENCODING_DEFAULT,
          "Encoding algorithm used for tile compression. Please refer to 'Iris.Slide.Encoding' values for more information related to encoding types. This will default to the current Iris Slide version's implemenation of 'Default encoding'. ",
          py::arg("desired_byte_format") = Iris::FORMAT_R8G8B8A8,
          "Desired pixel byte format",
          py::arg("anonymize") = false,
          "Whether to strip metadata attributes that may contain PHI from the slide data",
          py::arg("derivation") = IrisSlide::EncoderDerivation::ENCODER_DERIVE_2X_LAYERS,
          "Desired pyramid downsampling structure. Default to 2x layers. Disable with \"use_source\".",
          py::arg("concurrency") = std::thread::hardware_concurrency(),
          "Maximum concurrent thread execution (Default highest performance)",
          py::arg("codec_context") = IrisSlide::Context(),
          "Iris Slide encoding context for GPU based compression",
          py::arg("i2s_attributes") = std::map<std::string, std::string>(),
          "The slide's Iris Interoperability Specification (I2S) attributes as key and value text (what I2S's Attributes.to_pairs gives). When given, they are written as the slide's METADATA_I2S attributes in place of the source's; dispatch refuses a key that is not ASCII or a value past IFE's sizes, naming it",
          py::arg("i2s_version") = uint16_t{0},
          "The I2S version the attributes conform to, as IFE's 16-bit ATTRIBUTES.VERSION holds it: major << 8 | minor, so I2S 0.1 is 1 and I2S 1.0 is 256. Required with i2s_attributes: dispatch refuses version 0, an unversioned claim");
    
    m.def("dispatch_encoder",           &IrisSlide::dispatch_encoder,
          "This method dispatches the Encoder asynchrounously and returns immediately. The encoder will immediately begin encoding the slide data based upon the set encoder parameters, which cannot be changed onced dispatched. The progress of the encoding can be checked by calling get_encoder_status on the encoder",
          py::arg("encoder"), "The endcoder object to dispatch. Once dispatched the encoder parameters cannot be changed.");
    
    m.def("interrupt_encoder",          &IrisSlide::interrupt_encoder,
          "This method interrupts an active encoder object and is generally best used during try-execption blocks to stop the plurality of threads that the encoder will have dispatched during activation. This will return an Iris result flag indicating the successful termination of the encoding process and will put the encoder into the ENCODER_ERROR status.",
          py::arg("encoder"), "The encoder object for which to interrupt execution.");
    
    m.def ("reset_encoder",             &IrisSlide::reset_encoder,
           "This method resets an encoder object to the inactive state and purges prior error information",
           py::arg("encoder"), "The encoder object ro reset.");
    
    m.def("get_encoder_progress", &_get_encoder_progress,
          "Method to return the current status and progress of an encoder object",
          py::arg("encoder"), "the encoder object to check");
    
    m.def("get_encoder_src", &_get_encoder_src,
          "Get the source file path string");
    
    m.def("get_encoder_dst_path", &_get_encoder_dst_path,
          "Get the encoder destination directory path string");
    
    m.def("set_encoder_src", &IrisSlide::set_encoder_src,
          "Set the encoder source file path",
          py::arg("encoder"), "the encoder object to update",
          py::arg("source_path"), "String containing the source slide file path");
    
    m.def("set_encoder_dst_path", &IrisSlide::set_encoder_dst_path,
          "Set the encoder destination file directory path",
          py::arg("encoder"), "the encoder object to update",
          py::arg("destination_path"), "String containing the destination directory path");
}
