# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Optimeas GmbH

r"""
Example 01 - Inspect a few channels from an OSF file.
 
Loads an OSF file, picks four random channels, and prints metadata
plus the first ten samples of each. Useful for getting a quick feel
for what is in a file you have not seen before.
 
The script lives at implementations/python/examples/ inside the repo,
while the OSF sample files live in the top-level examples/ directory
(three levels up from this script).
 
Usage examples (the script can be called from any directory):
 
    python 01_inspect_channels.py
        # default: loads steam_loco.osf from the repo's examples/ folder
 
    python 01_inspect_channels.py motorbike.osf
        # bare filename: looked up in the repo's examples/ folder
 
    python 01_inspect_channels.py "V:\github\osf\examples\motorbike.osf"
        # absolute path: used as given
 
    python 01_inspect_channels.py "..\..\..\examples\motorbike.osf"
        # relative path: resolved relative to your current directory
 
Quotes around the path are optional for paths without spaces, but never
hurt. Forward slashes also work on Windows if you prefer them.
"""

from __future__ import annotations

import random
import sys
from datetime import datetime, timezone
from pathlib import Path

import osf


def format_value(value: object, data_type: str) -> str:
    """Format a single sample value with a sensible width for the data type."""
    if data_type in ("float", "double"):
        # Floating point: a few decimal places, fixed width.
        return f"{value:>12.4f}"
    if data_type in ("string",):
        # Strings can be long — truncate for display.
        text = str(value)
        return repr(text[:40] + ("…" if len(text) > 40 else ""))
    if data_type in ("binary",):
        # Binary: just say how many bytes, do not print raw.
        return f"<binary, {len(value)} bytes>"
    # All integer types and bool fall here.
    return f"{value:>12}"


def format_timestamp(t_ns: int) -> str:
    """Format a nanosecond timestamp as 'dd/mm/yyyy HH:MM:SS,mmm' (UTC).

    OSF stores timestamps as int64 nanoseconds since Unix epoch in UTC.
    For display we use UTC unchanged — converting to a local timezone
    would need a deliberate choice we do not make here.
    """
    # datetime supports microseconds, not nanoseconds. Convert and round.
    seconds, nanos = divmod(int(t_ns), 1_000_000_000)
    micros = nanos // 1000
    dt = datetime.fromtimestamp(seconds, tz=timezone.utc).replace(microsecond=micros)
    millis = dt.microsecond // 1000
    return dt.strftime("%d/%m/%Y %H:%M:%S") + f",{millis:03d}"


def inspect_channel(channel) -> None:
    """Print one channel with its metadata and the first ten samples."""
    unit = channel.physical_unit or ""
    unit_part = f" [{unit}]" if unit else ""
    header = (
        f"{channel.name}{unit_part}  "
        f"(dtype={channel.data_type}, type={channel.channel_type}, "
        f"samples={channel.sample_count})"
    )
    print(header)
    print("-" * min(len(header), 80))

    if channel.is_empty:
        print("  (channel is empty)")
        print()
        return

    # Take up to 10 samples from the start.
    values = channel.samples()
    n = min(10, len(values))

    # Some channels carry timestamps, others are equidistant. We try to
    # show timestamps if they exist; otherwise just an index.
    try:
        timestamps = channel.timestamps_ns()
    except Exception:
        timestamps = None

    for i in range(n):
        if timestamps is not None and len(timestamps) > i:
            t_ns = int(timestamps[i])
            prefix = f"  [{i:>2}]  {format_timestamp(t_ns)}  "
        else:
            prefix = f"  [{i:>2}]  "

        print(prefix + format_value(values[i], channel.data_type))

    if channel.sample_count > n:
        print(f"  ... and {channel.sample_count - n} more samples")
    print()


def main() -> None:
    # Pick the file from command line, fall back to steam_loco.osf in the
    # repository's top-level examples/ directory. The script lives at
    # implementations/python/examples/, so the repo root is three levels up.
    if len(sys.argv) > 1:
        path = Path(sys.argv[1])
    else:
        repo_root = Path(__file__).resolve().parents[3]
        path = repo_root / "examples" / "steam_loco.osf"

    if not path.exists():
        sys.exit(
            f"File not found: {path}\n"
            f"Pass an OSF file as argument, or make sure the repo's "
            f"examples/ directory contains steam_loco.osf."
        )

    print(f"Loading {path} ...")
    mgr = osf.load(str(path))

    stats = mgr.stats
    compression = stats.compression_format or "uncompressed"
    print(
        f"  {len(mgr)} channels, {stats.blocks_total} blocks, "
        f"{compression}, loaded in {stats.elapsed_ms:.1f} ms\n"
    )

    # Pick four channels at random. Use a fixed seed so repeated runs
    # show the same channels — easier when comparing notes with a colleague.
    random.seed(42)
    channels = list(mgr.channels)
    if len(channels) <= 4:
        sample = channels
    else:
        sample = random.sample(channels, 4)

    for ch in sample:
        inspect_channel(ch)


if __name__ == "__main__":
    main()