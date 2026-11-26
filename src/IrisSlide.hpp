//
//  IrisSlide.hpp
//  Iris
//
//  Created by Ryan Landvater on 1/9/24.
//

#ifndef IrisSlideSlide_hpp
#define IrisSlideSlide_hpp
namespace IrisSlide {
class Slide_t {
    const Context                               _context;
    const Iris::File::Parser                    _parser;
    // The parser's one cached lift, set by the constructor: valid while
    // _parser lives, so every read below shares it and copies nothing.
    const Iris::File::Abstraction::File*        _abstraction = nullptr;
public:
    explicit Slide_t                  (const Context&, const Iris::File::Parser&);
    Slide_t                           (const Slide_t&) = delete;
    Slide_t operator =                (const Slide_t&) = delete;

    // Return codec version used to encode the slide
    Version             get_slide_codec_version () const;
    // Return the slide information
    SlideInfo           get_slide_info          () const;
    // Return the read handle this slide forwards to (the encoder passes an Iris
    // source's tiles through with it)
    const Iris::File::Parser& get_slide_parser  () const;
    // Return the slide's lifted structure (the parser's cached lift)
    const Iris::File::Abstraction::File& abstraction          () const;
    // Get the compressed slide tile entry
    Buffer              get_slide_tile_entry    (uint32_t layer, uint32_t tile_indx) const;
    // Read the slide tile entry to return a decompressed tile
    Buffer              read_slide_tile         (const SlideTileReadInfo&) const;
    // Get information about an associated image
    AssociatedImageInfo get_assoc_image_info    (const std::string& image_label) const;
    // Get the compressed associated image stream
    Buffer              get_assoc_image         (const std::string& image_label) const;
    // Read the associated image to return a decompressed image
    Buffer              read_assc_image         (const AssociatedImageReadInfo&) const;

    // Write a slide annotation to the file stream.
    Result              write_slide_annotation  (const IrisSlide::Annotation&);
};
} // END IRIS SLIDE NAMESPACE
#endif /* IrisSlideSlide_hpp */
