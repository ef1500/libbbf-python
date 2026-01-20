# libbbf/__init__.py
from ._bbf import BBFReader, BBFBuilder

from .exceptions import (
    BBFError, 
    BBFNotFoundError, 
    BBFInvalidFormatError, 
    BBFCorruptionError
)

__all__ = [
    "BBFReader", 
    "BBFBuilder",
    # Export exceptions
    "BBFError",
    "BBFNotFoundError",
    "BBFInvalidFormatError",
    "BBFCorruptionError"
]