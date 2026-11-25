//
//  IrisSlideEncoder.hpp
//  Iris
//
//  Created by Ryan Landvater on 1/14/24.
//

#ifndef IrisSlideEncoder_hpp
#define IrisSlideEncoder_hpp
namespace IrisSlide {
struct EncoderTracker;
using AtomicEncoderStatus           = std::atomic<EncoderStatus>;
class Encoder_t {
    unsigned                        _concurrency    = std::thread::hardware_concurrency();
    bool                            _derive;
    const Context                   _context;
    std::string                     _srcPath;
    std::string                     _dstPath;
    bool                            _anonymize;
    Encoding                        _encoding;
    EncoderDerivation               _derivation;
    Attributes                      _attributes;
    Threads                         _threads;
    EncoderTracker                  _tracker;
    AtomicEncoderStatus             _status;
public:
    explicit Encoder_t    (const IrisSlide::EncodeSlideInfo& info);
    Encoder_t             (const Encoder_t&) = delete;
    Encoder_t& operator = (const Encoder_t&) = delete;
   ~Encoder_t             ();
    
    EncoderStatus get_status        () const;
    Context     get_context         () const;
    std::string get_src_path        () const;
    std::string get_dst_path        () const;
    Encoding    get_encoding        () const;
    Result  get_encoder_progress    (EncoderProgress&) const;
    
    void    set_src_path            (const std::string& source);
    void    set_dst_path            (const std::string& destination);
    void    set_encoding            (Encoding desired_encoding);
    Result  reset_encoder           ();
    Result  dispatch_encoder        ();
    Result  interrupt_encoder       ();
};
} // END IRIS SLIDE NAMESPACE
#endif /* IrisSlideEncoder_hpp */
