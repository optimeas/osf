r"""
Example 04 - Compute the analyses from example 03, then save them as OSF5.

Same calculations as 03_motorbike_stats.py, but instead of printing the
numbers, we write them to a new OSF5 file alongside this script. The
result file can then be loaded again with osf.load() — making the
analysis itself re-usable downstream.

Naming convention for the output channels:

  Aggregates (single-sample channels) carry the timestamp of the first
  temperature sample of the source recording. They sit under a clear
  hierarchy:

    analysis.speed_band.<band>.duration
    analysis.speed_band.<band>.share
    analysis.temperature.statistics.{min,max,mean,median}
    analysis.temperature.by_band.<band>.{mean,max}
    analysis.warmup.time_to_200C
    analysis.summary.total_recording_duration

  One real time series:

    analysis.derived.speed_band_index    int8 over the speed sample times

Note: OSF channel names use '.' as separator by spec default
(namespacesep). Real field data also uses '.'.

The script lives at implementations/python/examples/ inside the repo,
while the OSF sample files live in the top-level examples/ directory
(three levels up from this script).

Usage examples (the script can be called from any directory):

    python 04_save_analysis.py
        # default: loads motorbike.osf from the repo's examples/ folder

    python 04_save_analysis.py "V:\github\osf\examples\motorbike.osf"
        # absolute path: used as given

The output file is always written next to this script as
'04_motorbike_analysis.osf'.
"""

from __future__ import annotations

import sys
from pathlib import Path

import numpy as np

import osf


SPEED_BANDS = [0, 30, 50, 80, 110]
SPEED_BAND_LABELS = ["band_0_30", "band_30_50", "band_50_80", "band_80_110", "band_above_110"]

WARMUP_THRESHOLD_C = 200.0


def speed_band_durations(speeds: np.ndarray, ts_ns: np.ndarray) -> np.ndarray:
    """Sum the time spent in each speed band, in seconds."""
    if len(speeds) < 2:
        return np.zeros(len(SPEED_BAND_LABELS))

    durations_s = np.diff(ts_ns).astype(np.float64) / 1_000_000_000
    sample_speeds = speeds[:-1]

    bins = np.digitize(sample_speeds, SPEED_BANDS) - 1
    bins = np.clip(bins, 0, len(SPEED_BAND_LABELS) - 1)

    totals = np.zeros(len(SPEED_BAND_LABELS))
    for i in range(len(SPEED_BAND_LABELS)):
        totals[i] = durations_s[bins == i].sum()
    return totals


def speed_at_timestamps(
    target_ts_ns: np.ndarray,
    speed_ts_ns: np.ndarray,
    speeds: np.ndarray,
) -> np.ndarray:
    """Linear interpolation of speed at the given target timestamps."""
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
            in_path = repo_examples / arg
        else:
            in_path = Path(arg)
    else:
        in_path = repo_examples / "motorbike.osf"

    if not in_path.exists():
        sys.exit(f"File not found: {in_path}")

    out_path = Path(__file__).resolve().parent / "04_motorbike_analysis.osf"

    print(f"Loading {in_path} ...")
    mgr = osf.load(str(in_path))
    print(f"  {len(mgr)} channels, loaded in {mgr.stats.elapsed_ms:.1f} ms")

    speed_ch = mgr.channel("v_hinterrad")
    if speed_ch is None:
        sys.exit("Channel 'v_hinterrad' not found.")

    temp_ch = mgr.channel("Kruemmertemperatur_rechter_Zylinder")
    if temp_ch is None:
        sys.exit("Channel 'Kruemmertemperatur_rechter_Zylinder' not found.")

    speeds = speed_ch.samples()
    speed_ts_ns = speed_ch.timestamps_ns()

    temps = temp_ch.samples()
    temp_ts_ns = temp_ch.timestamps_ns()

    # ----- Compute everything -----
    print("Computing analysis ...")

    band_durations = speed_band_durations(speeds, speed_ts_ns)
    total_duration = float(band_durations.sum())

    temp_min = float(temps.min())
    temp_max = float(temps.max())
    temp_mean = float(temps.mean())
    temp_median = float(np.median(temps))

    # Temperature statistics per speed band
    temps_speed_at_t = speed_at_timestamps(temp_ts_ns, speed_ts_ns, speeds)
    temp_bins = np.digitize(temps_speed_at_t, SPEED_BANDS) - 1
    temp_bins = np.clip(temp_bins, 0, len(SPEED_BAND_LABELS) - 1)

    band_temp_means = np.full(len(SPEED_BAND_LABELS), np.nan)
    band_temp_maxes = np.full(len(SPEED_BAND_LABELS), np.nan)
    for i in range(len(SPEED_BAND_LABELS)):
        mask = temp_bins == i
        if mask.any():
            band_temp_means[i] = temps[mask].mean()
            band_temp_maxes[i] = temps[mask].max()

    # Warm-up time
    above = temps >= WARMUP_THRESHOLD_C
    if above.any():
        first_idx = int(np.argmax(above))
        warmup_seconds = float((temp_ts_ns[first_idx] - temp_ts_ns[0]) / 1_000_000_000)
    else:
        warmup_seconds = float("nan")

    # Derived time series: speed-band index at each speed sample time
    speed_band_index = np.digitize(speeds, SPEED_BANDS).astype(np.int8) - 1
    speed_band_index = np.clip(speed_band_index, 0, len(SPEED_BAND_LABELS) - 1)

    # ----- Write to OSF5 -----
    print(f"Writing {out_path.name} ...")

    # All single-value aggregates share the same timestamp: the first
    # temperature sample of the source recording. That ties the analysis
    # to the recording it came from.
    aggregate_ts = np.array([int(temp_ts_ns[0])], dtype=np.int64)

    b = (
        osf.WriterBuilder()
        .creator("04_save_analysis.py")
        .tag("analysis-result")
        .reason(f"Derived analysis of {in_path.name}")
    )

    def add_aggregate(name: str, value: float, unit: str) -> None:
        """Add a single-sample timestamped channel for one aggregate value."""
        idx = b.add_channel(
            name=name,
            data_type="double",
            channel_type="scalar",
            physical_unit=unit,
        )
        b.add_timestamped_samples(
            channel=idx,
            timestamps_ns=aggregate_ts,
            values=np.array([value], dtype=np.float64),
        )

    # Recording duration
    add_aggregate("analysis.summary.total_recording_duration", total_duration, "s")

    # Speed band durations and shares
    for label, duration in zip(SPEED_BAND_LABELS, band_durations):
        add_aggregate(f"analysis.speed_band.{label}.duration", float(duration), "s")
        share = 100 * duration / total_duration if total_duration > 0 else 0
        add_aggregate(f"analysis.speed_band.{label}.share", float(share), "%")

    # Temperature global statistics
    add_aggregate("analysis.temperature.statistics.min", temp_min, "C")
    add_aggregate("analysis.temperature.statistics.max", temp_max, "C")
    add_aggregate("analysis.temperature.statistics.mean", temp_mean, "C")
    add_aggregate("analysis.temperature.statistics.median", temp_median, "C")

    # Temperature per band
    for label, mean_val, max_val in zip(SPEED_BAND_LABELS, band_temp_means, band_temp_maxes):
        if not np.isnan(mean_val):
            add_aggregate(f"analysis.temperature.by_band.{label}.mean", float(mean_val), "C")
            add_aggregate(f"analysis.temperature.by_band.{label}.max", float(max_val), "C")

    # Warm-up
    if not np.isnan(warmup_seconds):
        add_aggregate("analysis.warmup.time_to_200C", warmup_seconds, "s")

    # Derived time series: the band index at each speed sample time
    derived_idx = b.add_channel(
        name="analysis.derived.speed_band_index",
        data_type="int8",
        channel_type="scalar",
    )
    b.add_timestamped_samples(
        channel=derived_idx,
        timestamps_ns=speed_ts_ns,
        values=speed_band_index,
    )

    b.write_to_file(str(out_path))

    # ----- Verify by reading back -----
    print("Verifying ...")
    result = osf.load(str(out_path))
    print(f"  {len(result)} channels written, file size {result.stats.file_size_bytes:,} bytes")

    # Show a few representative values to confirm everything came through
    samples_back = {
        "Total recording duration":
            result.channel("analysis.summary.total_recording_duration").samples()[0],
        "Time in 0-30 km/h":
            result.channel("analysis.speed_band.band_0_30.duration").samples()[0],
        "Time in >110 km/h":
            result.channel("analysis.speed_band.band_above_110.duration").samples()[0],
        "Mean exhaust temperature":
            result.channel("analysis.temperature.statistics.mean").samples()[0],
        "Max exhaust temperature":
            result.channel("analysis.temperature.statistics.max").samples()[0],
    }
    print()
    for k, v in samples_back.items():
        print(f"  {k:<30}  {v:>10.2f}")

    derived = result.channel("analysis.derived.speed_band_index")
    print(f"  Speed band index series:        {derived.sample_count:>10,} samples")

    print()
    print(f"Analysis written to {out_path}")


if __name__ == "__main__":
    main()