//
//  IrisSlidePrivTypes.hpp
//  Iris
//
//  Created by Ryan Landvater on 1/10/24.
//

#ifndef IrisSlidePrivTypes_h
#define IrisSlidePrivTypes_h
namespace IrisSlide {

struct CompressTileInfo {
    Buffer          pixelArray          = NULL;
//    Buffer          destinationOptional = NULL;
    Format          format              = Iris::FORMAT_UNDEFINED;
    Encoding        encoding            = TILE_ENCODING_UNDEFINED;
    Quality         quality             = QUALITY_DEFAULT;
    Subsampling     subsampling         = SUBSAMPLE_DEFAULT;
};
struct DecompressTileInfo {
    Buffer          compressed          = NULL;
    Buffer          optionalDestination = NULL;
    Format          desiredFormat       = Iris::FORMAT_UNDEFINED;
    Encoding        encoding            = TILE_ENCODING_UNDEFINED;
};
struct CompressImageInfo {
    Buffer          pixelArray          = NULL;
//    Buffer          destinationOptional = NULL;
    uint32_t        width               = 0;
    uint32_t        height              = 0;
    Format          format              = Iris::FORMAT_UNDEFINED;
    ImageEncoding   encoding            = IMAGE_ENCODING_UNDEFINED;
    Quality         quality             = QUALITY_DEFAULT;
    Subsampling     subsampling         = SUBSAMPLE_DEFAULT;
};
struct DecompressImageInfo {
    Buffer          compressed          = NULL;
    Buffer          optionalDestination = NULL;
    uint32_t        width               = 0;
    uint32_t        height              = 0;
    Format          sourceFormat        = Iris::FORMAT_UNDEFINED;
    Format          desiredFormat       = Iris::FORMAT_UNDEFINED;
    ImageEncoding   encoding            = IMAGE_ENCODING_UNDEFINED;
};
} // END IRIS SLIDE NAMESPACE
#endif /* IrisSlidePrivTypes_h */
