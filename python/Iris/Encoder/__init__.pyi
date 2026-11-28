from __future__ import annotations
from .encoder import (Encoder as Encoder, EncoderProgress as EncoderProgress, EncoderState as EncoderState,
                      create_encoder as create_encoder, dispatch_encoder as dispatch_encoder,
                      encoder_output_name as encoder_output_name, encoder_progress as encoder_progress,
                      interrupt_encoder as interrupt_encoder)

__all__ = ["encode_slide_file", "EncoderState", "EncoderProgress", "Encoder", "create_encoder",
           "dispatch_encoder", "encoder_progress", "interrupt_encoder", "encoder_output_name"]

def encode_slide_file(source: str, outdir: str = ..., encoding: str = ..., quality: int | None = ...,
                      subsampling: str | None = ...,
                      layer_step: int | None = ..., effort: int | None = ..., memory_budget: int | None = ...,
                      concurrency: int | None = ...,
                      i2s_attributes: dict[str, str] | None = ..., i2s_version: int = ...,
                      show_progress: bool = ...) -> str: ...
