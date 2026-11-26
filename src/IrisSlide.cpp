//
//  IrisSlide.cpp
//  Iris
//
//  Created by Ryan Landvater on 1/9/24.
//
#include <assert.h>
#include "IrisSlidePriv.hpp"

namespace IrisSlide {
Iris::Result is_iris_slide_file(const std::string &file_path) noexcept
{
    try {
        // RTM Api_06: IFE's own domain and code are kept; the message gains the path.
        Iris::File::Parser parser;
        Iris::Result result = Iris::File::create_parser({.filepath = file_path}, parser);
        if (result.succeeded())
            result = parser.is_iris_slide_file();
        if (result.failed())
            result.message = "Iris File Extension test failed (" + file_path + "): " + result.message;
        return result;
    } catch (const std::exception&e) {
        return codec_failure(SlideError::FILE_NOT_OPENED, "Iris File Extension test failed ("+
            file_path + "): " +
            e.what() + "\n");
    }
}
Iris::Result validate_slide (const struct SlideOpenInfo &info) noexcept
{
    try {
        // RTM Api_06: a path IFE cannot map returns IFE's own Result.
        Iris::File::Parser parser;
        const Iris::Result opened = Iris::File::create_parser({.filepath = info.filePath}, parser);
        return opened.failed() ? opened : parser.validate_file_structure();
    } catch (const std::exception &e) {
        return codec_failure(SlideError::FILE_NOT_OPENED, "Iris File Extension slide (" + 
            info.filePath + ") failed validation: " +
            e.what () + "\n");
    }
}
Slide open_slide (const struct SlideOpenInfo &info) noexcept
{
    try {
        // Create a context if not provided
        Context context = info.context;
        if (context == nullptr) {
            ContextCreateInfo context_info {};
            context = std::make_shared<Context_t>(context_info);
        }
        if (context == nullptr) 
            throw std::runtime_error("No valid context");
        
        // The Parser maps the file read-only and owns the mapping for as long
        // as the slide holds it.
        Iris::File::Parser parser;
        throw_if_failed(Iris::File::create_parser({.filepath = info.filePath}, parser));
        return std::make_shared<Slide_t>(context, parser);
        
    } catch (const std::exception &e) {
        std::cerr   << "Failed to open the slide "
                    << info.filePath << ": "
                    << e.what() << "\n";
        return nullptr;
    }
}
Iris::Result get_slide_info(const Slide &slide, SlideInfo& info) noexcept
{
    try {
        if (!slide)
            throw std::runtime_error("no valid slide object");
        
        info = slide->get_slide_info();
        
        return Iris::Result::success();
    } catch (const std::exception& e) {
        return codec_failure(SlideError::SLIDE_INFO_UNREADABLE, std::string("Failed to read slide info: ") + e.what());
    }
}
Buffer read_slide_tile(const SlideTileReadInfo &info) noexcept
{
    try {
        // Ensure the slide object is valid
        if (info.slide == NULL)
            throw std::runtime_error("No valid codec slide object");
        
        // Read the slide tile
        auto result = info.slide->read_slide_tile(info);
        return result;
        
    } catch (const std::exception& e) {
        std::cerr << "Failed to read the slide tile"
                    << "[layer " << info.layerIndex
                    << ", tile " << info.tileIndex
                    << "]: " << e.what() << "\n";
        return NULL;
    }   return NULL;
}
Result get_associated_image_info(const Slide &slide, AssociatedImageInfo &info) noexcept
{
    try {
        if (!slide) throw std::runtime_error
            ("No valid codec slide object");
        if (!info.imageLabel.size()) throw std::runtime_error
            ("No image label provided within AssociatedImageInfo struct");
        
        info = slide->get_assoc_image_info(info.imageLabel);
        return Iris::Result::success();
        
    } catch (const std::exception& e) {
        return codec_failure(SlideError::ASSOCIATED_IMAGE_UNREADABLE, std::string("Failed to get associated image info: ") +
            e.what());
    }
}
Buffer read_associated_image(const AssociatedImageReadInfo &info) noexcept
{
    try {
        if (!info.slide) throw std::runtime_error
            ("No valid slide object");
        if (!info.imageLabel.size()) throw std::runtime_error
            ("No image label provided within AssociatedImageReadInfo struct");
        
        return info.slide->read_assc_image(info);
        
    } catch (const std::exception& error) {
        std::cerr   << "Failed to read the associated image labeled \""
                    << info.imageLabel <<  "\": " << error.what();
        return NULL;
    }   return NULL;
}
Iris::Result annotate_slide(const Annotation &annotation) noexcept
{
    try {
        if (annotation.slide == NULL)
            throw std::runtime_error("No valid codec slide object");
        
        assert(false && "IrisSlide::annotate_slide() in IrisSlide.cpp not yet fully written");
//        return annotation.slide->write_slide_annotation(annotation);
    } catch (std::runtime_error& e) {
        return codec_failure(SlideError::ANNOTATION_NOT_WRITTEN, std::string("Failed to annotate the codec slide: ") +
            e.what());
    }   return codec_failure(SlideError::ANNOTATION_WRITE_UNSUPPORTED, "annotate_slide is not yet implemented");
}
Iris::Result get_slide_annotations(const Slide &slide, Annotations &annotations) noexcept
{
    try {
        if (slide == NULL)
            throw std::runtime_error("No valid codec slide object");
        
        assert(false && "IrisSlide::get_slide_annotations() in IrisSlide.cpp not yet fully written");
        annotations.clear();
        
        return Iris::Result::success();
    } catch (std::runtime_error& e) {
        return codec_failure(SlideError::ANNOTATIONS_UNREADABLE, std::string("Failed to retrieve slide annotation objects: ") +
            e.what());
    }
}
Slide_t::Slide_t    (const Context& cxt, const Iris::File::Parser& parser) :
_context                                (cxt),
_parser                                 (parser)
{
    // RTM Api_06: lift now, so a structurally damaged file is refused when it
    // is opened rather than on its first read.
    throw_if_failed(_parser.abstraction(_abstraction));
}
Version Slide_t::get_slide_codec_version() const
{
    return _abstraction->metadata.codec;
}
SlideInfo Slide_t::get_slide_info() const
{
    const auto& file = *_abstraction;
    return SlideInfo {
        .format         = file.tileTable.format,
        .encoding       = file.tileTable.encoding,
        .extent         = file.tileTable.extent,
        .metadata       = file.metadata,
    };
}
const Iris::File::Parser& Slide_t::get_slide_parser() const
{
    return _parser;
}
const Iris::File::Abstraction::File& Slide_t::abstraction() const
{
    return *_abstraction;
}
Buffer Slide_t::get_slide_tile_entry(uint32_t layer, uint32_t tile_indx) const
{
    std::span<const Iris::BYTE> stream;
    throw_if_failed(_parser.tile(layer, tile_indx, stream));
    return Iris::Copy_strong_buffer_from_data(stream.data(), stream.size());
}
Buffer Slide_t::read_slide_tile(const SlideTileReadInfo &info) const
{
    std::span<const Iris::BYTE> stream;
    throw_if_failed(_parser.tile(info.layerIndex, info.tileIndex, stream));
    if (stream.empty()) throw std::runtime_error
        ("no tile at layer " + std::to_string(info.layerIndex) +
         ", tile " + std::to_string(info.tileIndex) + " (NULL_TILE)");
    Buffer src      = Iris::Wrap_weak_buffer_fom_data (stream.data(), stream.size());
    
    // Initialize the write destination
    Buffer dst_buffer   = nullptr;
    size_t dst_size     = 0;
    switch (info.desiredFormat) {
        case FORMAT_UNDEFINED: throw std::runtime_error
            ("desired format in SLideTileReadInfo is undefined");
        case Iris::FORMAT_B8G8R8:
        case Iris::FORMAT_R8G8B8:
            dst_size = TILE_PIX_AREA * 3;
            break;
        case Iris::FORMAT_B8G8R8A8:
        case Iris::FORMAT_R8G8B8A8:
            dst_size = TILE_PIX_AREA * 4;
            break;
    } if (!dst_size) throw std::runtime_error
        ("invalid desired slide format in SlideTileReadInfo");
    
    // Check to see if there is a destination provided to write into, and if that
    // destination buffer is sufficiently large to hold the unpacked data.
    if (info.optionalDestination && info.optionalDestination->capacity() >= dst_size)
            dst_buffer = info.optionalDestination;
    else    dst_buffer = Iris::Create_strong_buffer(dst_size);
    
    // Return the decompressed file structure
    dst_buffer = _context->decompress_tile({
        .compressed             = src,
        .optionalDestination    = dst_buffer,
        .desiredFormat          = info.desiredFormat,
        .encoding               = _abstraction->tileTable.encoding,
    });
    if (!dst_buffer) throw std::runtime_error
        ("Failed to decompress slide tile");
    
    return dst_buffer;
}
AssociatedImageInfo Slide_t::get_assoc_image_info (const std::string &image_label) const
{
    const auto& images   = _abstraction->images;
    const auto image_itr = images.find(image_label);
    if (image_itr == images.cend())
        throw std::runtime_error("get_assoc_image_info failed as there is no image with label \""+
                                 image_label + "\" within the slide file.");
    
    return image_itr->second.info;
}
Buffer Slide_t::get_assoc_image (const std::string &image_label) const
{
    std::span<const Iris::BYTE> stream;
    throw_if_failed(_parser.image(image_label, stream));
    return Copy_strong_buffer_from_data(stream.data(), stream.size());
}
Buffer Slide_t::read_assc_image (const AssociatedImageReadInfo &info) const
{
    const auto  image   = get_assoc_image_info(info.imageLabel);
    std::span<const Iris::BYTE> stream;
    throw_if_failed(_parser.image(info.imageLabel, stream));
    Buffer src          = Iris::Wrap_weak_buffer_fom_data (stream.data(), stream.size());
    
    // Initialize the write destination
    Buffer dst_buffer   = nullptr;
    size_t image_pixels = image.width * image.height;
    size_t dst_size     = 0;
    switch (info.desiredFormat) {
        case FORMAT_UNDEFINED: throw std::runtime_error
            ("desired format in SLideTileReadInfo is undefined");
        case Iris::FORMAT_B8G8R8:
        case Iris::FORMAT_R8G8B8:
            dst_size = image_pixels * 3; // 3 bytes per pixel format
            break;
        case Iris::FORMAT_B8G8R8A8:
        case Iris::FORMAT_R8G8B8A8:
            dst_size = image_pixels * 4; // 4 bytes per pixel format
            break;
    } if (!dst_size) throw std::runtime_error
        ("invalid desired slide format in SlideTileReadInfo");
    
    // Check to see if there is a destination provided to write into, and if that
    // destination buffer is sufficiently large to hold the unpacked data.
    if (info.optionalDestination && info.optionalDestination->capacity() >= dst_size)
            dst_buffer = info.optionalDestination;
    else    dst_buffer = Iris::Create_strong_buffer(dst_size);
    
    // Return the decompressed file structure
    dst_buffer = _context->decompress_image(DecompressImageInfo {
        .compressed             = src,
        .optionalDestination    = dst_buffer,
        .width                  = image.width,
        .height                 = image.height,
        .sourceFormat           = image.sourceFormat,
        .desiredFormat          = info.desiredFormat,
        .encoding               = image.encoding,
    });
    if (!dst_buffer) throw std::runtime_error
        ("Failed to decompress slide tile");
    
    return dst_buffer;
}
Iris::Result Slide_t::write_slide_annotation (const IrisSlide::Annotation &annotation)
{
    return codec_failure(SlideError::ANNOTATION_WRITE_UNSUPPORTED, "write_slide_annotation is not yet implemented");
}
} // END IRIS SLIDE NAMESPACE
