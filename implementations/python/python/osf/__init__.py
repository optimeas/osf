# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Optimeas GmbH

"""OSF — Python bindings for the Open Streaming Format.

Distribution name on PyPI is ``osfdata``; the import name is ``osf``.
The split follows the established Python convention (``scikit-learn``
imports as ``sklearn``, ``PyYAML`` imports as ``yaml``,
``beautifulsoup4`` imports as ``bs4``).

Quick start::

    import osf

    mgr = osf.load("examples/steam_loco.osf")
    print(f"Channels: {len(mgr)}")
    temp = mgr.channel("Sensor/Temperature")
    arr = temp.samples()      # NumPy array
    ts = temp.timestamps_ns() # NumPy int64 array
"""

from osf._osf import (
    Channel,
    DataManager,
    OsfError,
    ReaderStats,
    Segment,
    WriterBuilder,
    __version__,
    load,
    save,
)

__all__ = [
    "Channel",
    "DataManager",
    "OsfError",
    "ReaderStats",
    "Segment",
    "WriterBuilder",
    "__version__",
    "load",
    "save",
]
