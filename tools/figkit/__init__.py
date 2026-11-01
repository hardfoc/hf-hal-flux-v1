"""figkit: code-sourced documentation figures as SVG, light and dark from one layout.

See ``build.py`` for the build and the drift check, ``core.py`` for the drawing calls and ``theme.py`` for the
colour tokens.
"""

from . import theme
from .core import MIN_CONTRAST, PROFILES, READ_WIDTH, Box, C, Fig, text_width, wrap

__all__ = ["Box", "C", "Fig", "MIN_CONTRAST", "PROFILES", "READ_WIDTH", "text_width", "theme", "wrap"]
