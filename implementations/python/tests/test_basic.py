# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Optimeas GmbH

"""Basic smoke tests for the osfdata Python bindings.

Run with::

    cd implementations/python
    .venv/Scripts/maturin develop
    .venv/Scripts/pytest tests/

The tests resolve the example files relative to this file, so
``cd``-ing into the python directory or running pytest from the repo
root both work.
"""

from __future__ import annotations

import os

import numpy as np
import pytest

import osf

EXAMPLES = os.path.normpath(
    os.path.join(os.path.dirname(__file__), "..", "..", "..", "examples")
)


def _example(name: str) -> str:
    return os.path.join(EXAMPLES, name)


def test_module_metadata_is_present():
    assert osf.__version__
    assert issubclass(osf.OsfError, Exception)


def test_load_steam_loco():
    mgr = osf.load(_example("steam_loco.osf"))
    assert len(mgr) == 123
    assert not mgr.stats.compressed
    assert mgr.stats.compression_format is None


def test_load_weather_station_osfz_detects_gzip():
    mgr = osf.load(_example("weather_station.osfz"))
    assert mgr.stats.compressed
    assert mgr.stats.compression_format == "gzip"
    assert len(mgr) > 0


def test_channel_lookup_by_name_and_index():
    mgr = osf.load(_example("steam_loco.osf"))
    ch = mgr.channel("GPS.PosFixMode")
    assert ch is not None
    assert ch.name == "GPS.PosFixMode"

    by_index = mgr.channel_by_index(ch.index)
    assert by_index is not None
    assert by_index.name == ch.name

    assert mgr.channel("not_a_real_channel") is None
    assert mgr.channel_by_index(40000) is None


def test_channel_samples_match_dtype():
    mgr = osf.load(_example("steam_loco.osf"))
    for ch in mgr.channels:
        if ch.is_empty:
            continue
        arr = ch.samples()
        if ch.data_type == "double":
            assert isinstance(arr, np.ndarray)
            assert arr.dtype == np.float64
            assert arr.shape == (ch.sample_count,)
            break
    else:
        pytest.fail("steam_loco.osf should have at least one non-empty double channel")


def test_timestamps_ns_returns_int64():
    mgr = osf.load(_example("steam_loco.osf"))
    ch = next(c for c in mgr.channels if not c.is_empty)
    ts = ch.timestamps_ns()
    assert isinstance(ts, np.ndarray)
    assert ts.dtype == np.int64
    assert ts.shape == (ch.sample_count,)


def test_segments_visible_on_osf5_equidistant():
    mgr = osf.load(_example("generated/osf5_equidistant.osf"))
    eq_channels = [c for c in mgr.channels if c.channel_type == "equidistant"]
    assert eq_channels, "osf5_equidistant.osf must contain equidistant channels"
    for ch in eq_channels:
        assert len(ch.segments) >= 1
        for seg in ch.segments:
            assert seg.sample_rate_hz > 0
            assert seg.sample_count > 0
            assert isinstance(seg.start_timestamp_ns, int)


def test_string_channel_returns_list_of_str():
    mgr = osf.load(_example("generated/osf5_timestamped_string.osf"))
    string_channels = [c for c in mgr.channels if c.data_type == "string"]
    assert string_channels
    ch = string_channels[0]
    if ch.sample_count > 0:
        values = ch.samples()
        assert isinstance(values, list)
        assert all(isinstance(s, str) for s in values)


def test_save_round_trips(tmp_path):
    src_mgr = osf.load(_example("generated/osf5_mixed.osf"))
    out = tmp_path / "roundtrip.osf"
    osf.save(src_mgr, str(out))

    rt_mgr = osf.load(str(out))
    assert len(src_mgr) == len(rt_mgr)
    for src_ch, rt_ch in zip(src_mgr.channels, rt_mgr.channels):
        assert src_ch.name == rt_ch.name
        assert src_ch.sample_count == rt_ch.sample_count
        assert src_ch.data_type == rt_ch.data_type


def test_writer_builder_basic(tmp_path):
    builder = (
        osf.WriterBuilder()
        .creator("pytest")
        .tag("unit-test")
        .reason("CI")
    )
    idx_temp = builder.add_channel(
        name="Sensor/Temp",
        data_type="double",
        channel_type="scalar",
        physical_unit="C",
    )
    idx_pressure = builder.add_channel(
        name="Sensor/Pressure",
        data_type="int32",
        channel_type="scalar",
    )

    temp_values = np.array([18.4, 18.5, 18.6, 18.7, 18.8], dtype=np.float64)
    builder.add_equidistant_segment(
        idx_temp,
        start_ns=1_700_000_000_000_000_000,
        sample_rate_hz=1.0,
        values=temp_values,
    )

    pressure_ts = np.array([100, 200, 300], dtype=np.int64)
    pressure_values = np.array([1000, 1010, 1020], dtype=np.int32)
    builder.add_timestamped_samples(idx_pressure, pressure_ts, pressure_values)

    out = tmp_path / "builder.osf"
    builder.write_to_file(str(out))

    mgr = osf.load(str(out))
    assert len(mgr) == 2

    temp = mgr.channel("Sensor/Temp")
    assert temp is not None
    assert temp.channel_type == "equidistant"
    np.testing.assert_array_equal(temp.samples(), temp_values)

    pressure = mgr.channel("Sensor/Pressure")
    assert pressure is not None
    assert pressure.channel_type == "timestamped"
    np.testing.assert_array_equal(pressure.timestamps_ns(), pressure_ts)
    np.testing.assert_array_equal(pressure.samples(), pressure_values)


def test_writer_rejects_wrong_dtype_for_equidistant(tmp_path):
    builder = osf.WriterBuilder()
    idx = builder.add_channel(name="X", data_type="double", channel_type="scalar")
    bad = np.array([1, 2, 3], dtype=np.int32)
    with pytest.raises(ValueError):
        builder.add_equidistant_segment(idx, start_ns=0, sample_rate_hz=1.0, values=bad)


def test_writer_consumed_after_write(tmp_path):
    builder = osf.WriterBuilder()
    builder.add_channel(name="X", data_type="double", channel_type="scalar")
    out = tmp_path / "x.osf"
    builder.write_to_file(str(out))
    with pytest.raises(osf.OsfError):
        builder.write_to_file(str(out))


def test_zero_length_block_is_skipped_and_counted():
    """The malformed corpus file reads through with its anomaly counted.

    A zero-length data block is a non-conforming writer artefact (OSF-UP3);
    the reader skips it, counts it, and keeps scanning. Five samples sit
    before the bad frame and five behind it, so a reader that stopped at the
    frame would report 5.
    """
    path = _example("generated/malformed/osf5_zero_length_block.osf")
    mgr = osf.load(path)
    assert mgr.stats.blocks_skipped_zero_length == 1
    # blocks_total == 3 / blocks_read == 2 reflect how this corpus file's
    # writer happened to chunk ten samples into two real blocks plus the one
    # bad frame — not a rule OSF-UP3 itself imposes. If these fail after a
    # writer/fixture change, re-check the corpus file's block layout before
    # suspecting a reader regression.
    assert mgr.stats.blocks_total == 3
    assert mgr.stats.blocks_read == 2
    ch = mgr.channel("Sensor/Double")
    assert ch.sample_count == 10


def test_repr_strings_are_descriptive():
    mgr = osf.load(_example("steam_loco.osf"))
    ch = mgr.channel("GPS.PosFixMode")
    assert ch is not None

    assert "DataManager" in repr(mgr)
    assert "Channel" in repr(ch)
    assert "GPS.PosFixMode" in repr(ch)
    assert "ReaderStats" in repr(mgr.stats)
    if ch.segments:
        assert "Segment" in repr(ch.segments[0])
