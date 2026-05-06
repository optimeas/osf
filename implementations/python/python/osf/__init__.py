"""OSF — Python bindings for the Open Streaming Format.

Distribution name on PyPI is ``osfdata``; the import name is ``osf``.
The split follows the established Python convention (``scikit-learn``
imports as ``sklearn``, ``PyYAML`` imports as ``yaml``,
``beautifulsoup4`` imports as ``bs4``).

This is the scaffold-only release — only ``__version__`` and the
``OsfError`` exception are exposed yet. ``DataManager``, ``Channel``,
``WriterBuilder``, and the ``load`` / ``save`` convenience functions
arrive in subsequent commits of session 7a.
"""

from osf._osf import OsfError, __version__

__all__ = [
    "OsfError",
    "__version__",
]
