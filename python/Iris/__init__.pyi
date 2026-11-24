from __future__ import annotations
import numpy
import numpy.typing
import typing
import typing_extensions

# Import all types from the core Iris C++ extension
from .iris_core import (
    Buffer as Buffer,
    BufferStrength as BufferStrength,
    Extent as Extent,
    Format as Format,
    LayerExtent as LayerExtent,
    Domain as Domain,
    Result as Result,
    Severity as Severity,
    Version as Version,
)

# Type hints for lazy-loaded submodules
from typing import TYPE_CHECKING
if TYPE_CHECKING:
    from . import Slide as Slide
    from . import Encoder as Encoder

__all__ = [
    'Buffer', 'BufferStrength', 'Extent', 'Format', 'LayerExtent', 'Domain', 'Result', 'Severity', 'Version'
]

__version__: str

# Lazy attribute access for optional modules
def __getattr__(name: str) -> typing.Any: ...