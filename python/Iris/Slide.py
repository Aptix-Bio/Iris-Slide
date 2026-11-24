"""
Slide submodule that provides access to Iris Encoded Slide Files.
This module re-exports the Slide functionality from the main Iris C++ extension.
"""

# Import the core module directly
from .iris_core import Slide as _slide_module

# Re-export the Slide functionality
AssociatedImageEncoding = _slide_module.AssociatedImageEncoding
AssociatedImageInfo = _slide_module.AssociatedImageInfo
Attributes = _slide_module.Attributes
Context = _slide_module.Context
Encoding = _slide_module.Encoding
Metadata = _slide_module.Metadata
MetadataType = _slide_module.MetadataType
Slide = _slide_module.Slide
SlideInfo = _slide_module.SlideInfo
get_codec_version = _slide_module.get_codec_version
open_slide = _slide_module.open_slide
validate_slide_path = _slide_module.validate_slide_path

__all__ = [
    'AssociatedImageEncoding',
    'AssociatedImageInfo', 
    'Attributes',
    'Context',
    'Encoding',
    'Metadata',
    'MetadataType',
    'Slide',
    'SlideInfo',
    'get_codec_version',
    'open_slide',
    'validate_slide_path'
]
