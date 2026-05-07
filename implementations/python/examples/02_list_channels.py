r"""
Example 02 - List all channels in an OSF file.

Prints a table with one row per channel: name, physical unit, sample
count, and data type. Useful for getting the complete inventory of
what is in a file before deciding what to look at in detail.

The script lives at implementations/python/examples/ inside the repo,
while the OSF sample files live in the top-level examples/ directory
(three levels up from this script).

Usage examples (the script can be called from any directory):

    python 02_list_channels.py
        # default: loads motorbike.osf from the repo's examples/ folder

    python 02_list_channels.py steam_loco.osf
        # bare filename: looked up in the repo's examples/ folder

    python 02_list_channels.py "V:\github\osf\examples\weather_station.osfz"
        # absolute path: used as given (works for OSFZ too)

    python 02_list_channels.py "..\..\..\examples\steam_loco.osf"
        # relative path: resolved relative to your current directory

Quotes around the path are optional for paths without spaces, but never
hurt. Forward slashes also work on Windows if you prefer them.
"""

from __future__ import annotations

import sys
from pathlib import Path

import osf


def main() -> None:
    # The repo's top-level examples/ directory, three levels up from this script.
    repo_examples = Path(__file__).resolve().parents[3] / "examples"

    if len(sys.argv) > 1:
        arg = sys.argv[1]
        # Bare filename without any path separator? Look it up in the repo's
        # examples/ folder. This makes typical calls short and works regardless
        # of the current working directory.
        if "\\" not in arg and "/" not in arg:
            path = repo_examples / arg
        else:
            path = Path(arg)
    else:
        path = repo_examples / "motorbike.osf"

    if not path.exists():
        sys.exit(
            f"File not found: {path}\n"
            f"Pass an OSF file as argument. Bare filenames are looked up "
            f"in the repo's examples/ directory; paths (relative or "
            f"absolute) are used as given."
        )

    print(f"Loading {path} ...")
    mgr = osf.load(str(path))

    stats = mgr.stats
    file_size_mb = stats.file_size_bytes / (1024 * 1024)

    print(f"  File size:    {stats.file_size_bytes:>12,} bytes  ({file_size_mb:.2f} MiB)")
    if stats.compressed:
        print(f"  Compression:  {stats.compression_format}")
    else:
        print(f"  Compression:  none")
    print(f"  Load time:    {stats.elapsed_ms:>12.1f} ms")
    print(f"  Channels:     {len(mgr):>12,}    Blocks: {stats.blocks_total:,}")
    print()

    # Build the table. Compute column widths from the actual data so
    # nothing is truncated and nothing is over-padded.
    rows = []
    for ch in mgr.channels:
        rows.append(
            (
                ch.index,
                ch.name,
                ch.physical_unit or "",
                ch.sample_count,
                ch.data_type,
                ch.channel_type,
            )
        )

    # Column headers
    headers = ("Idx", "Name", "Unit", "Samples", "Type", "Channel")

    # Width per column: max of header and any data row
    widths = [
        max(len(headers[0]), max(len(str(r[0])) for r in rows)),
        max(len(headers[1]), max(len(r[1]) for r in rows)),
        max(len(headers[2]), max(len(r[2]) for r in rows)),
        max(len(headers[3]), max(len(str(r[3])) for r in rows)),
        max(len(headers[4]), max(len(r[4]) for r in rows)),
        max(len(headers[5]), max(len(r[5]) for r in rows)),
    ]

    # Print header line and separator
    header_line = (
        f"{headers[0]:>{widths[0]}}  "
        f"{headers[1]:<{widths[1]}}  "
        f"{headers[2]:<{widths[2]}}  "
        f"{headers[3]:>{widths[3]}}  "
        f"{headers[4]:<{widths[4]}}  "
        f"{headers[5]:<{widths[5]}}"
    )
    print(header_line)
    print("-" * len(header_line))

    # Print one row per channel
    for r in rows:
        print(
            f"{r[0]:>{widths[0]}}  "
            f"{r[1]:<{widths[1]}}  "
            f"{r[2]:<{widths[2]}}  "
            f"{r[3]:>{widths[3]}}  "
            f"{r[4]:<{widths[4]}}  "
            f"{r[5]:<{widths[5]}}"
        )

    # Summary line at the bottom
    total_samples = sum(r[3] for r in rows)
    print()
    print(f"{len(rows)} channels, {total_samples:,} samples in total.")


if __name__ == "__main__":
    main()