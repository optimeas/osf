# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Optimeas GmbH

r"""
Example 03 - Compute statistics on motorbike telemetry data.

Two analyses on the motorbike sample file:

  (a) Time spent in each speed band, computed from the rear-wheel speed
      channel (v_hinterrad). Bands: 0–30, 30–50, 50–80, 80–110, >110 km/h.

  (b) Exhaust manifold temperature (Kruemmertemperatur_rechter_Zylinder):
        - Basic statistics (min, max, mean, median).
        - Temperature distribution across the speed bands above.
        - Time it took to reach 200 °C from the start of the recording
          (motor warm-up).

A note on methodology:

  Both channels are timestamped, not equidistant. So for the time-spent-
  per-speed-band analysis we cannot just count samples and divide by a
  sample rate; we have to compute the duration between consecutive
  samples and weight by that.

  For correlating temperature with speed, the two channels have very
  different sample rates (temperature is sampled about 9× less often
  than speed). At each temperature sample's timestamp we therefore
  linearly interpolate the speed between its two nearest samples.
  Linear interpolation is the natural choice for continuous physical
  quantities; for discrete state signals (status flags, modes)
  zero-order hold would be more appropriate.

The script lives at implementations/python/examples/ inside the repo,
while the OSF sample files live in the top-level examples/ directory
(three levels up from this script).

Usage examples (the script can be called from any directory):

    python 03_motorbike_stats.py
        # default: loads motorbike.osf from the repo's examples/ folder

    python 03_motorbike_stats.py "V:\github\osf\examples\motorbike.osf"
        # absolute path: used as given
"""

from __future__ import annotations

import sys
from pathlib import Path

import numpy as np

import osf


# Speed band boundaries in km/h. The last value is the upper bound for the
# topmost band; everything beyond it goes into ">110".
SPEED_BANDS = [0, 30, 50, 80, 110]
SPEED_BAND_LABELS = ["0–30", "30–50", "50–80", "80–110", ">110"]

WARMUP_THRESHOLD_C = 200.0


def format_duration(seconds: float) -> str:
    """Format a duration in seconds as 'Hh Mm Ss' or 'Mm Ss' or 'Ss'."""
    total_s = int(seconds)
    h, rem = divmod(total_s, 3600)
    m, s = divmod(rem, 60)
    if h > 0:
        return f"{h}h {m:02d}m {s:02d}s"
    if m > 0:
        return f"{m}m {s:02d}s"
    return f"{s}s"


def speed_band_durations(speeds: np.ndarray, ts_ns: np.ndarray) -> np.ndarray:
    """Sum the time spent in each speed band, returning an array of seconds.

    For timestamped data, each sample is assumed to remain valid until the
    next sample arrives. The duration of sample i is therefore
    ts_ns[i+1] - ts_ns[i]. The last sample has no successor; we drop it.
    """
    if len(speeds) < 2:
        return np.zeros(len(SPEED_BAND_LABELS))

    # Duration each sample was valid, in seconds.
    durations_s = np.diff(ts_ns).astype(np.float64) / 1_000_000_000

    # Speed of each sample except the last (since we used i+1 - i above).
    sample_speeds = speeds[:-1]

    # np.digitize maps each speed to a band index. With our boundaries:
    #   speed < 30        -> 1
    #   30 <= speed < 50  -> 2
    #   50 <= speed < 80  -> 3
    #   80 <= speed < 110 -> 4
    #   speed >= 110      -> 5
    # We shift to 0-based indexing.
    bins = np.digitize(sample_speeds, SPEED_BANDS) - 1
    bins = np.clip(bins, 0, len(SPEED_BAND_LABELS) - 1)

    totals = np.zeros(len(SPEED_BAND_LABELS))
    for i in range(len(SPEED_BAND_LABELS)):
        mask = bins == i
        totals[i] = durations_s[mask].sum()
    return totals


def speed_at_timestamps(
    target_ts_ns: np.ndarray,
    speed_ts_ns: np.ndarray,
    speeds: np.ndarray,
) -> np.ndarray:
    """For each target timestamp, return the linearly interpolated speed.

    Linear interpolation is the natural default for continuous physical
    quantities (speed, temperature, pressure) when no better model of
    the inter-sample behavior is known. For discrete state signals
    (status flags, modes), zero-order hold would be more appropriate;
    we use linear here because both channels in this analysis are
    continuous physical measurements.

    Target timestamps outside the source range get the nearest source
    value (NumPy's np.interp default), which is acceptable for the
    small boundary effect at the very start and end of a recording.
    """
    return np.interp(
        target_ts_ns.astype(np.float64),
        speed_ts_ns.astype(np.float64),
        speeds.astype(np.float64),
    )


def main() -> None:
    repo_examples = Path(__file__).resolve().parents[3] / "examples"

    if len(sys.argv) > 1:
        arg = sys.argv[1]
        if "\\" not in arg and "/" not in arg:
            path = repo_examples / arg
        else:
            path = Path(arg)
    else:
        path = repo_examples / "motorbike.osf"

    if not path.exists():
        sys.exit(f"File not found: {path}")

    print(f"Loading {path} ...")
    mgr = osf.load(str(path))
    stats = mgr.stats
    print(
        f"  {len(mgr)} channels, {stats.blocks_total:,} blocks, "
        f"loaded in {stats.elapsed_ms:.1f} ms\n"
    )

    # ------------------------------------------------------------------
    # (a) Time spent in each speed band
    # ------------------------------------------------------------------

    speed_ch = mgr.channel("v_hinterrad")
    if speed_ch is None:
        sys.exit("Channel 'v_hinterrad' not found in this file.")

    speeds = speed_ch.samples()
    speed_ts_ns = speed_ch.timestamps_ns()

    print("=" * 60)
    print("(a) Time spent in each speed band (rear-wheel speed)")
    print("=" * 60)

    band_durations = speed_band_durations(speeds, speed_ts_ns)
    total_duration = band_durations.sum()
    print(f"\nTotal recording time:  {format_duration(total_duration)}")
    print(f"Speed channel samples: {len(speeds):,}\n")

    print(f"  {'Band':<10} {'Duration':>14}    {'Share':>7}")
    print(f"  {'-'*10} {'-'*14}    {'-'*7}")
    for label, duration in zip(SPEED_BAND_LABELS, band_durations):
        share = 100 * duration / total_duration if total_duration > 0 else 0
        print(f"  {label:<10} {format_duration(duration):>14}    {share:>6.1f}%")

    # ------------------------------------------------------------------
    # (b) Exhaust manifold temperature analysis
    # ------------------------------------------------------------------

    temp_ch = mgr.channel("Kruemmertemperatur_rechter_Zylinder")
    if temp_ch is None:
        sys.exit("Channel 'Kruemmertemperatur_rechter_Zylinder' not found.")

    temps = temp_ch.samples()
    temp_ts_ns = temp_ch.timestamps_ns()

    print()
    print("=" * 60)
    print("(b) Exhaust manifold temperature (right cylinder)")
    print("=" * 60)

    print(f"\nSamples:  {len(temps):,}\n")

    # --- Basic statistics ---
    print(f"  Minimum:  {temps.min():>8.1f} °C")
    print(f"  Maximum:  {temps.max():>8.1f} °C")
    print(f"  Mean:     {temps.mean():>8.1f} °C")
    print(f"  Median:   {np.median(temps):>8.1f} °C")

    # --- Temperature distribution across speed bands ---
    print()
    print("  Mean and max temperature by speed band:")
    print(f"    {'Band':<10} {'Mean':>10}     {'Max':>8}     {'Samples':>8}")
    print(f"    {'-'*10} {'-'*10}     {'-'*8}     {'-'*8}")

    # For each temperature sample, look up the speed at that moment by
    # linear interpolation between the two nearest speed samples.
    temps_speed_at_t = speed_at_timestamps(temp_ts_ns, speed_ts_ns, speeds)

    bins = np.digitize(temps_speed_at_t, SPEED_BANDS) - 1
    bins = np.clip(bins, 0, len(SPEED_BAND_LABELS) - 1)

    for i, label in enumerate(SPEED_BAND_LABELS):
        mask = bins == i
        n = int(mask.sum())
        if n == 0:
            print(f"    {label:<10} {'—':>10}     {'—':>8}     {0:>8}")
            continue
        band_temps = temps[mask]
        print(
            f"    {label:<10} "
            f"{band_temps.mean():>8.1f} °C   "
            f"{band_temps.max():>6.1f} °C   "
            f"{n:>8,}"
        )

    # --- Warm-up time: time until exhaust temperature first exceeds 200 °C ---
    print()
    print(f"  Time to reach {WARMUP_THRESHOLD_C:.0f} °C from the start of recording:")

    # Find the first index where temperature crosses the threshold.
    above = temps >= WARMUP_THRESHOLD_C
    if not above.any():
        print(f"    Threshold never reached (max was {temps.max():.1f} °C).")
    else:
        first_idx = int(np.argmax(above))  # argmax of bool returns first True
        first_ts = int(temp_ts_ns[first_idx])
        recording_start_ts = int(temp_ts_ns[0])
        warmup_seconds = (first_ts - recording_start_ts) / 1_000_000_000
        print(
            f"    Reached at sample {first_idx:,} "
            f"({temps[first_idx]:.1f} °C), "
            f"{format_duration(warmup_seconds)} after start."
        )


if __name__ == "__main__":
    main()