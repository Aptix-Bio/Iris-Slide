from __future__ import annotations
import collections.abc
import typing
__all__: list[str] = ['Encoder', 'EncoderProgress', 'EncoderState', 'create_encoder', 'dispatch_encoder', 'encoder_output_name', 'encoder_progress', 'interrupt_encoder']
class EncoderState:
    """
    Members:
    
      READY
    
      ACTIVE
    
      DONE
    
      FAILED
    
      INTERRUPTED
    """
    ACTIVE: typing.ClassVar[EncoderState]
    DONE: typing.ClassVar[EncoderState]
    FAILED: typing.ClassVar[EncoderState]
    INTERRUPTED: typing.ClassVar[EncoderState]
    READY: typing.ClassVar[EncoderState]
    __members__: typing.ClassVar[dict[str, EncoderState]]
    @typing.overload
    def __eq__(self, other: EncoderState) -> bool:
        ...
    @typing.overload
    def __eq__(self, other: typing.Any) -> bool:
        ...
    def __getstate__(self) -> int:
        ...
    def __hash__(self) -> int:
        ...
    def __index__(self) -> int:
        ...
    def __init__(self, value: typing.SupportsInt | typing.SupportsIndex) -> None:
        ...
    def __int__(self) -> int:
        ...
    @typing.overload
    def __ne__(self, other: EncoderState) -> bool:
        ...
    @typing.overload
    def __ne__(self, other: typing.Any) -> bool:
        ...
    def __repr__(self) -> str:
        ...
    def __setstate__(self, state: typing.SupportsInt | typing.SupportsIndex) -> None:
        ...
    def __str__(self) -> str:
        ...
    @property
    def name(self) -> str:
        ...
    @property
    def value(self) -> int:
        ...
class EncoderProgress:
    """
    A snapshot of an encode
    """
    @property
    def error(self) -> str:
        ...
    @property
    def file(self) -> str:
        ...
    @property
    def fraction(self) -> float:
        ...
    @property
    def memory_budget(self) -> int:
        ...
    @property
    def peak_resident_bytes(self) -> int:
        ...
    @property
    def resident_bytes(self) -> int:
        ...
    @property
    def state(self) -> EncoderState:
        ...
class Encoder:
    """
    An encoder for one slide; it encodes once
    """
def create_encoder(source: str, destination: str, quality: typing.SupportsInt | typing.SupportsIndex = 90, subsampling: str = '422', memory_budget: typing.SupportsInt | typing.SupportsIndex = 0, concurrency: typing.SupportsInt | typing.SupportsIndex = ..., layer_step: typing.SupportsInt | typing.SupportsIndex = 2, i2s_attributes: collections.abc.Mapping[str, str] = {}, i2s_version: typing.SupportsInt | typing.SupportsIndex = 0) -> Encoder:
    """
    An encoder for one slide (a DICOM whole-slide instance: JPEG, JPEG 2000 or HTJ2K frames), writing <source stem>.jpeg.iris into destination; raises RuntimeError naming what it refuses
    """
def dispatch_encoder(encoder: Encoder) -> None:
    """
    Starts the encode and returns; raises when the encoder was already dispatched
    """
def encoder_output_name(source: str) -> str:
    """
    The file name an encode of source writes
    """
def encoder_progress(encoder: Encoder) -> EncoderProgress:
    """
    A snapshot of the encode
    """
def interrupt_encoder(encoder: Encoder) -> None:
    """
    Stops an active encode; it leaves no file
    """
