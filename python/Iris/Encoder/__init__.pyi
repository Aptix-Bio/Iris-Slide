from __future__ import annotations
from ..iris_core import Result, Format, Context
from .. import Slide
from .encoder import (
    Encoder as Encoder,
    EncoderDerivation as EncoderDerivation,
    EncoderProgress as EncoderProgress,
    EncoderStatus as EncoderStatus,
    create_encoder as create_encoder,
    dispatch_encoder as dispatch_encoder,
    get_encoder_dst_path as get_encoder_dst_path,
    get_encoder_progress as get_encoder_progress,
    get_encoder_src as get_encoder_src,
    interrupt_encoder as interrupt_encoder,
    reset_encoder as reset_encoder,
    set_encoder_dst_path as set_encoder_dst_path,
    set_encoder_src as set_encoder_src,
)

__all__ = ['encode_slide_file']

def encode_slide_file(source: str, outdir: str = ..., desired_encoding: Slide.Encoding = ..., desired_byte_format: Format = ..., strip_metadata: bool = ..., anonymize: bool = ..., derivation: EncoderDerivation = ..., concurrency: int = None, codec_context: Context = ..., i2s_attributes: dict[str, str] | None = None, i2s_version: int = 0) -> Result:
    """Encode a slide file as an Iris slide.

    i2s_attributes are the slide's Iris Interoperability Specification (I2S)
    attributes as key and value text, written as its METADATA_I2S attributes in
    place of the source's. i2s_version is the I2S version they conform to, as
    IFE's 16-bit ATTRIBUTES.VERSION holds it: major << 8 | minor, so I2S 0.1 is
    1 and I2S 1.0 is 256. It is required with i2s_attributes; 0, an unversioned
    claim, is refused.
    """
    ...
