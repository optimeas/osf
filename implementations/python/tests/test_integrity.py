# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Optimeas GmbH

"""Tests for the OSF5 integrity profile (level ``crc``) bindings."""

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


def test_read_crc_reference_file():
    mgr = osf.load(_example("generated/integrity/osf5_crc_equidistant.osf"))
    assert mgr.stats.integrity == "crc32c"
    assert mgr.stats.blocks_crc_failed == 0
    assert mgr.stats.blocks_signature_skipped == 0
    assert mgr.stats.verification_status == "crc_valid"
    assert len(mgr) == 3


def test_writer_builder_with_integrity_roundtrip(tmp_path):
    builder = osf.WriterBuilder().with_integrity("crc32c").creator("pytest")
    idx = builder.add_channel(
        name="Sensor/Temp", data_type="double", channel_type="scalar"
    )
    values = np.array([1.5, 2.5, 3.5, 4.5, 5.5], dtype=np.float64)
    builder.add_equidistant_segment(
        idx, start_ns=1_000, sample_rate_hz=100.0, values=values
    )
    out = tmp_path / "crc_builder.osf"
    builder.write_to_file(str(out))

    # The header carries the crc32c token.
    with open(out, "rb") as fh:
        first_line = fh.readline()
    assert b" crc32c:" in first_line

    mgr = osf.load(str(out))
    assert mgr.stats.integrity == "crc32c"
    assert mgr.stats.blocks_crc_failed == 0
    temp = mgr.channel("Sensor/Temp")
    assert temp is not None
    np.testing.assert_array_equal(temp.samples(), values)


def test_save_with_integrity_kwarg(tmp_path):
    src = osf.load(_example("generated/osf5_mixed.osf"))
    out = tmp_path / "crc_save.osf"
    osf.save(src, str(out), integrity="crc32c")

    mgr = osf.load(str(out))
    assert mgr.stats.integrity == "crc32c"
    assert mgr.stats.blocks_crc_failed == 0
    assert len(mgr) == len(src)
    for a, b in zip(src.channels, mgr.channels):
        assert a.name == b.name
        assert a.sample_count == b.sample_count


def test_corrupt_crc_block_is_reported(tmp_path):
    # Write a crc file, then flip the last byte (the last block's frame CRC).
    builder = osf.WriterBuilder().with_integrity("crc32c")
    idx = builder.add_channel(
        name="Sensor/Value", data_type="double", channel_type="scalar"
    )
    builder.add_equidistant_segment(
        idx, start_ns=0, sample_rate_hz=10.0, values=np.arange(5, dtype=np.float64)
    )
    good = tmp_path / "good.osf"
    builder.write_to_file(str(good))

    data = bytearray(good.read_bytes())
    data[-1] ^= 0xFF  # corrupt the trailing frame CRC
    bad = tmp_path / "bad.osf"
    bad.write_bytes(bytes(data))

    mgr = osf.load(str(bad))
    assert mgr.stats.blocks_crc_failed >= 1
    assert mgr.stats.verification_status == "invalid"


def test_save_rejects_ed25519():
    src = osf.load(_example("generated/osf5_mixed.osf"))
    with pytest.raises(ValueError):
        osf.save(src, "unused.osf", integrity="ed25519")


@pytest.mark.skip(
    reason="no signed reference file yet (signing is not implemented); "
    "the signature_unverifiable status is covered by the Rust unit tests"
)
def test_signed_file_reports_signature_unverifiable():
    # Placeholder: once a signed reference file exists, loading it should
    # yield mgr.stats.integrity == "ed25519" and
    # mgr.stats.verification_status == "signature_unverifiable".
    ...


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
    assert mgr.stats.blocks_total == 3
    assert mgr.stats.blocks_read == 2
    ch = mgr.channel("Sensor/Double")
    assert ch.sample_count == 10
