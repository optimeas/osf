# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Optimeas GmbH
#
# Type stubs for the native osf._osf extension module. These are
# hand-written rather than auto-generated so the public surface is
# intentionally small and readable. Update them in lockstep with
# any change to the #[pymethods] blocks on the Rust side.

from typing import List, Optional

import numpy as np
from numpy.typing import NDArray

__version__: str

class OsfError(Exception):
    """Raised by the OSF reader and writer for any error in the
    underlying Rust core (`osf-core::OsfError`). Carries the original
    error message verbatim."""
    ...


class Segment:
    """One equidistant segment within an :class:`Channel`. Every
    ``bcStartData`` block in the source file produces one segment
    on read."""

    start_timestamp_ns: int
    sample_rate_hz: float
    sample_count: int

    def __repr__(self) -> str: ...


class Channel:
    """Typed in-memory channel. Wraps the manager-side
    ``osf-core::Channel`` enum; the storage variant is reflected in
    :attr:`channel_type`."""

    index: int
    name: str
    data_type: str
    """One of: ``"bool"``, ``"int8"``, ``"int16"``, ``"int32"``,
    ``"int64"``, ``"uint8"``, ``"uint16"``, ``"uint32"``, ``"uint64"``,
    ``"float"``, ``"double"``, ``"string"``, ``"binary"``,
    ``"gpslocation"``."""

    channel_type: str
    """Storage classification: ``"equidistant"``, ``"timestamped"``,
    or ``"variable"``. This is the manager-side grouping, not the
    on-disk ``channeltype`` attribute (which is usually ``"scalar"``
    for everything)."""

    sample_count: int
    physical_unit: Optional[str]
    display_name: Optional[str]
    is_empty: bool
    segments: List[Segment]
    """Equidistant-segment list. Empty for non-equidistant channels."""

    def samples(self) -> NDArray | List[str] | List[bytes]:
        """Sample values.

        Returns a NumPy array for numeric and ``gpslocation``
        channels (shape ``(N,)`` for scalars; shape ``(N, 3)`` for
        ``gpslocation`` with columns ``[lat, lon, alt]``). Returns a
        Python ``list[str]`` for string channels and ``list[bytes]``
        for binary channels.
        """
        ...

    def timestamps_ns(self) -> NDArray[np.int64]:
        """Per-sample timestamps in nanoseconds since the Unix epoch.
        For equidistant channels the timestamps are reconstructed
        from segments on the fly."""
        ...

    def __repr__(self) -> str: ...


class ReaderStats:
    """Read-time telemetry. Mirror of ``osf-core::stats::ReaderStats``."""

    compressed: bool
    compression_format: Optional[str]
    """``"zlib"``, ``"gzip"``, or ``None`` for uncompressed sources."""

    channels_total: int
    channels_with_data: int
    blocks_total: int
    blocks_read: int
    blocks_truncated: int
    elapsed_ms: float
    file_size_bytes: Optional[int]
    header_size_bytes: int
    metablock_size_bytes: int
    data_section_size_bytes: int
    trailer_seen: bool

    integrity: str
    """Declared integrity level: ``"none"``, ``"crc32c"``, or ``"ed25519"``."""
    blocks_crc_failed: int
    """Blocks dropped because their frame CRC did not match."""
    blocks_signature_skipped: int
    """Signature blocks skipped (this reader does not verify signatures)."""
    blocks_skipped_zero_length: int
    """Blocks skipped because their length field read ``0`` — a
    non-conforming writer artefact (OSF-UP3)."""
    verification_status: str
    """``"none"``, ``"crc_valid"``, ``"invalid"``, or
    ``"signature_unverifiable"``."""

    def __repr__(self) -> str: ...
    def __str__(self) -> str:
        """Human-readable, multi-line summary mirroring the Rust
        ``Display`` impl."""
        ...


class DataManager:
    """Read-only, in-memory view of a parsed OSF or OSFZ file.

    Construct via :func:`load`. Channel access by name is the
    documented mandatory form per DECISIONS §10; access by index is
    optional.
    """

    channels: List[Channel]
    stats: ReaderStats

    def channel(self, name: str) -> Optional[Channel]: ...
    def channel_by_index(self, index: int) -> Optional[Channel]: ...
    def __len__(self) -> int: ...
    def __repr__(self) -> str: ...


class WriterBuilder:
    """Accumulator for OSF5 file construction. All builder-style
    methods return ``self`` for chaining; ``write_to_file`` consumes
    the builder."""

    def __init__(self) -> None: ...

    # Builder-style chainable setters.
    def with_integrity(self, profile: str) -> "WriterBuilder":
        """Enable integrity level ``crc`` (``profile="crc32c"``) or disable
        it (``"none"``, default). Signing is not supported by the writer."""
        ...
    def creator(self, value: str) -> "WriterBuilder": ...
    def tag(self, value: str) -> "WriterBuilder": ...
    def reason(self, value: str) -> "WriterBuilder": ...
    def location(self, lat: float, lon: float, alt: float) -> "WriterBuilder": ...
    def namespace_sep(self, value: str) -> "WriterBuilder": ...
    def comment(self, value: str) -> "WriterBuilder": ...

    # Channel registration.
    def add_channel(
        self,
        name: str,
        data_type: str,
        channel_type: str,
        *,
        physical_unit: Optional[str] = ...,
        physical_dimension: Optional[str] = ...,
        display_name: Optional[str] = ...,
        mime_type: Optional[str] = ...,
        reference: Optional[str] = ...,
        comment: Optional[str] = ...,
        size_of_length_value: int = ...,
        time_increment_ns: Optional[int] = ...,
    ) -> int:
        """Register a channel and return its index."""
        ...

    # Sample appenders.
    def add_equidistant_segment(
        self,
        channel: int,
        start_ns: int,
        sample_rate_hz: float,
        values: NDArray,
    ) -> None:
        """Append an equidistant segment. ``values`` must be a 1D
        ``float32`` or ``float64`` NumPy array (spec rev 2026-05-04
        limits equidistant blocks to those two types)."""
        ...

    def add_timestamped_samples(
        self,
        channel: int,
        timestamps_ns: NDArray[np.int64],
        values: NDArray,
    ) -> None:
        """Append timestamped numeric samples. ``timestamps_ns`` must
        be ``int64``; ``values`` may be any of the supported numeric
        dtypes (bool / int8..int64 / uint8..uint64 / float32 /
        float64). The dtype must match the channel's declared
        ``data_type``."""
        ...

    def add_string_samples(
        self,
        channel: int,
        timestamps_ns: NDArray[np.int64],
        values: List[str],
    ) -> None: ...

    def add_binary_samples(
        self,
        channel: int,
        timestamps_ns: NDArray[np.int64],
        values: List[bytes],
    ) -> None: ...

    def write_to_file(self, path: str) -> None:
        """Serialise the file to ``path``. Always emits OSF5
        (DECISIONS §6). Consumes the builder."""
        ...

    def __repr__(self) -> str: ...


def load(path: str) -> DataManager:
    """Open an OSF or OSFZ file and return a :class:`DataManager`.

    Detects gzip / zlib OSFZ wrappers transparently. Releases the
    GIL during the I/O work.
    """
    ...


def save(manager: DataManager, path: str, *, integrity: Optional[str] = None) -> None:
    """Write ``manager`` back to disk as an OSF5 file.

    Always emits OSF5 — even when the manager was loaded from an
    OSF4 source — per DECISIONS §6. Pass ``integrity="crc32c"`` to emit
    the integrity profile (metablock token + per-block frame CRC32C);
    the default writes no integrity data. Releases the GIL during the
    write.
    """
    ...
