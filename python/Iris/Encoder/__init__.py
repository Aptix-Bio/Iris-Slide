"""Iris.Encoder: encode a whole-slide image as an Iris slide (.iris).

The encoder reads a DICOM whole-slide instance (JPEG, JPEG 2000 or HTJ2K frames), derives every
lower layer by a 2x bicubic reduction and writes <source stem>.jpeg.iris. Every failure raises
RuntimeError naming what failed.
"""
from __future__ import annotations

import os
import sys
import time

from .. import iris_core  # registers the shared types first
from .encoder import (EncoderState, EncoderProgress, Encoder, create_encoder, dispatch_encoder,
                      encoder_progress, interrupt_encoder, encoder_output_name)

__all__ = ["encode_slide_file", "EncoderState", "EncoderProgress", "Encoder", "create_encoder",
           "dispatch_encoder", "encoder_progress", "interrupt_encoder", "encoder_output_name"]

_POLL_SECONDS = 0.5


def encode_slide_file(source: str, outdir: str = "", quality: int = 90, subsampling: str = "422",
                      layer_step: int = 2, memory_budget: int = 0, concurrency: int = 0,
                      i2s_attributes: dict[str, str] | None = None, i2s_version: int = 0,
                      show_progress: bool = True) -> str:
    """Encode source as an Iris slide in outdir (default: the source's folder); returns the file written.

    i2s_attributes are the slide's Iris Interoperability Specification (I2S) attributes as key and
    value text, written as its METADATA_I2S attributes. i2s_version is the I2S version they conform
    to, as IFE's 16-bit ATTRIBUTES.VERSION holds it: major << 8 | minor, so I2S 0.1 is 1. It is
    required with i2s_attributes; 0, an unversioned claim, is refused.
    """
    encoder = create_encoder(source, outdir or os.path.dirname(os.path.abspath(source)), quality, subsampling,
                             memory_budget, concurrency, layer_step, i2s_attributes or {}, i2s_version)
    dispatch_encoder(encoder)
    try:
        while (progress := encoder_progress(encoder)).state == EncoderState.ACTIVE:
            if show_progress:
                print(f"\r{progress.fraction * 100:5.1f}%", end="", file=sys.stdout, flush=True)
            time.sleep(_POLL_SECONDS)
    except KeyboardInterrupt:
        interrupt_encoder(encoder)
        raise
    if show_progress:
        print(file=sys.stdout)
    if progress.state != EncoderState.DONE:
        raise RuntimeError(f"the encode did not complete ({progress.state.name}): {progress.error}")
    return progress.file
