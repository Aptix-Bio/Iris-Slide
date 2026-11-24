/**
 * @file IrisCorePython.hpp
 * @author Ryan Landvater
 * @brief  Iris Core API Python Bindings.
 * @version 2025.1.0
 * @date 2025-01-14
 *
 * @copyright Copyright (c) 2025
 * Created by Ryan Landvater on 1/14/2025
 *
 */
#include "IrisTypes.hpp"
#include "IrisBuffer.hpp"
namespace Iris {
py::array_t<uint8_t> _read_buffer (const Buffer &buffer)
{
    std::vector<size_t> shape {buffer->size()};
    py::array_t<uint8_t> array (shape);
    auto array_info = array.request();
    memcpy(array_info.ptr, buffer->data(), array_info.size);
    return array;
}
std::string PRINT_EXTENT (const Extent &extent)
{
    std::stringstream descr;
    descr   << "Slide Extent:\n"
            << "\tLayer 0 width: " << extent.width << "px\n"
            << "\tLayer 0 height: " << extent.height << "px\n";
    for (int l = 0; l < extent.layers.size(); ++l) {
        auto& layer = extent.layers[l];
        descr   << "\tLayer ["<<l<<"] extent:\n"
                << "\t\t x_tiles: " << layer.xTiles << "\n"
                << "\t\t y_tiles: " << layer.yTiles << "\n"
                << "\t\t scale: "   << round(layer.scale*10.f)/10.f<< "\n";
    }
    return descr.str();
}
}
static inline void DEFINE_IRIS_TYPES (pybind11::module_& m)
{
    using namespace Iris;
    using namespace pybind11::literals;
    
//    auto m = base.def_submodule
//    ("Core", "This is the Core Iris rendering submodule that allows access to the Iris Rendering System");
    
    py::class_<Iris::Version>                               (m, "Version")
        .def_readonly("major",      &Iris::Version::major)
        .def_readonly("minor",      &Iris::Version::minor)
        .def_readonly("build",      &Iris::Version::build)
        .def("__repr__",[](const Iris::Version &v) {
            std::stringstream version_str;
            version_str<<v.major<<"."<<v.minor<<"."<<v.build;
            return version_str.str();
        })
        .doc() = "Iris version (major, minor, and build number)";
    py::enum_<Iris::Severity>                               (m, "Severity")
        .value("SUCCESS",           Iris::Severity::SUCCESS)
        .value("WARNING",           Iris::Severity::WARNING)
        .value("FAILURE",           Iris::Severity::FAILURE);
    py::enum_<Iris::Domain>                                 (m, "Domain")
        .value("IRIS_HEADERS",          Iris::Domain::IRIS_HEADERS)
        .value("IRIS_FILE_EXTENSION",   Iris::Domain::IRIS_FILE_EXTENSION)
        .value("IRIS_SLIDE",            Iris::Domain::IRIS_SLIDE)
        .value("IRIS_CORE",             Iris::Domain::IRIS_CORE);

    py::class_<Iris::Result>                                (m, "Result")
        .def         ("succeeded",  &Result::succeeded,
                      "True for success and for a warning")
        .def         ("failed",     &Result::failed,
                      "True only for a failure")
        .def         ("is_warning", &Result::is_warning)
        .def_readonly("severity",   &Result::severity)
        .def_readonly("domain",     &Result::domain)
        .def_readonly("code",       &Result::code,
                      "The domain's own error code (for the codec, IrisSlide::SlideError)")
        .def_readonly("message",    &Result::message)
        .def("__repr__",            [](const Iris::Result &result) {
            switch (result.severity) {
                case Iris::Severity::SUCCESS: return std::string("Success\n");
                case Iris::Severity::WARNING: return "Warning: " + result.message + "\n";
                case Iris::Severity::FAILURE: return "Failure: " + result.message + "\n";
            }
            return "Failure: " + result.message + "\n";
        });
    py::enum_<Iris::Format>                                 (m, "Format")
        .value("FORMAT_B8G8R8",     Iris::FORMAT_B8G8R8)
        .value("FORMAT_R8G8B8",     Iris::FORMAT_R8G8B8)
        .value("FORMAT_B8G8R8A8",   Iris::FORMAT_B8G8R8A8)
        .value("FORMAT_R8G8B8A8",   Iris::FORMAT_R8G8B8A8);
    py::class_<Iris::LayerExtent>                           (m, "LayerExtent")
        .def_readonly("x_tiles",    &LayerExtent::xTiles)
        .def_readonly("y_tiles",    &LayerExtent::yTiles)
        .def_readonly("scale",      &LayerExtent::scale)
        .def_readonly("downsample", &LayerExtent::downsample)
        .def("__repr__",            [](const Iris::LayerExtent &layer) {
            std::stringstream descr;
            descr   << "x_tiles: "  << layer.xTiles << "\n"
                    << "y_tiles: "  << layer.yTiles << "\n"
                    << "scale: "    << round(layer.scale*10.f)/10.f<< "\n"
                    << "downsample: " << round(layer.downsample*10.f)/10.f << "\n";
            return descr.str();
        });
    py::class_<Iris::Extent>                                (m, "Extent")
        .def_readonly("width",      &Extent::width)
        .def_readonly("height",     &Extent::height)
        .def_readonly("layers",     &Extent::layers)
        .def("__repr__",            &PRINT_EXTENT);
    py::enum_<Iris::BufferReferenceStrength>                (m, "BufferStrength")
        .value("REFERENCE_WEAK",    REFERENCE_WEAK)
        .value("REFERENCE_STRONG",  REFERENCE_STRONG);
    
    py::class_<Iris::Buffer_t, Iris::Buffer>      (m, "Buffer")
        .def(py::init<BufferReferenceStrength>())
        .def(py::init<BufferReferenceStrength,size_t>())
        .def("get_strength",        &Buffer_t::get_strength)
        .def("data",                &_read_buffer);

}
