//
//  IrisSlideContext.hpp
//  Iris
//
//  Created by Ryan Landvater on 1/9/24.
//

#ifndef IrisSlideContext_hpp
#define IrisSlideContext_hpp
namespace IrisSlide {
using namespace Iris;
class Context_t {
    Device                              _device         = NULL;
    bool                                _gpuAV1Decode   = false;
    bool                                _gpuAV1Encode   = false;
public:
    explicit Context_t        (const ContextCreateInfo&);
    Context_t                 (const Context_t&) = delete;
    Context_t operator =      (const Context_t&) = delete;
   ~Context_t                 ();
    Quality     get_quality             () const;
    Subsampling get_subsampling         () const;
    void        set_quality             (Quality);
    void        set_subsampling         (Subsampling);
    
    Buffer compress_tile                (const CompressTileInfo&) const;
    Buffer decompress_tile              (const DecompressTileInfo&) const;
    Buffer compress_image               (const CompressImageInfo&) const;
    Buffer decompress_image             (const DecompressImageInfo&) const;
};
} // END IRIS SLIDE NAMESPACE
#endif /* IrisSlideContext_hpp */
