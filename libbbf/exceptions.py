# libbbf/exceptions.py

class BBFError(Exception):
    """Base class for all exceptions in the libbbf library."""
    pass

class BBFNotFoundError(BBFError, FileNotFoundError):
    """Raised when the BBF file cannot be found."""
    pass

class BBFInvalidFormatError(BBFError):
    """Raised when the file exists but isn't a valid BBF (wrong magic/header)."""
    pass

class BBFCorruptionError(BBFError):
    """Raised when a hash check fails."""
    def __init__(self, message: str, asset_index: int = -1):
        super().__init__(message)
        self.asset_index = asset_index