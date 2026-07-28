# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Optimeas GmbH

"""OSF-UP4 writer negative proof for the Python bindings.

``bcMessageEvent`` (control byte 4) became **read**-mandatory on this branch
(DECISIONS §26). Read obligation is not write permission — §26 says writers
must never emit it.

The bindings assemble no blocks of their own: ``osf.save`` forwards to
``osf_core::writer::write_to_file`` / ``WriterBuilder::from_manager``
(``implementations/python/src/writer.rs:374-389``), so this is the same code
path the Rust suite covers. That reading is *why* the audit clears Python, but
the equivalent audit for zero-length blocks (OSF-UP3) recorded "Python has no
executable evidence at all" as an open gap, so this module closes it: one
anti-vacuity guard and one round trip, run against the real extension module.

Run with::

    cd implementations/python
    .venv/Scripts/pytest tests/test_writer_message_event_audit.py
"""

from __future__ import annotations

import os

import osf

EXAMPLES = os.path.normpath(
    os.path.join(os.path.dirname(__file__), "..", "..", "..", "examples")
)

#: The block type this module is about (DECISIONS §26).
CONTROL_MESSAGE_EVENT = 0x04
#: Bit 7 — the multi-sample flag, masked off before comparing a block type.
MULTI_SAMPLE_FLAG = 0x80
#: ``bcAbsTimeStampData`` — what a decoded message event is re-emitted as.
CONTROL_ABS_TIMESTAMP = 0x08


def _example(name: str) -> str:
    return os.path.join(EXAMPLES, name)


def _frames(data: bytes) -> list[tuple[int, int, int]]:
    """Walk the block stream and return ``(channel, length, control)`` per frame.

    Per-channel width-aware and dialect-aware: the corpus pair is OSF4/XML and
    declares ``Demo.Counter`` at ``sizeoflengthvalue=2`` but ``Demo.Message`` at
    ``4``, while writer output is OSF5/JSON. A walker assuming a single width —
    or a single metablock dialect — would misparse the very file the
    anti-vacuity guard depends on.

    The widths are read straight out of the metablock text rather than through a
    parser, deliberately: the point of a byte-level walker is not to depend on
    the library agreeing with itself.
    """
    nl = data.index(b"\n")
    magic, meta_len_text = data[:nl].decode("ascii").split()
    assert magic in ("OSF4", "OSF5"), magic
    meta_len = int(meta_len_text)
    meta = data[nl + 1 : nl + 1 + meta_len].decode("utf-8")

    widths: dict[int, int] = {}
    if magic == "OSF4":
        import re

        for m in re.finditer(r"<channel\b[^>]*>", meta):
            idx = re.search(r'index="(\d+)"', m.group(0))
            sov = re.search(r'sizeoflengthvalue="(\d+)"', m.group(0))
            assert idx is not None, m.group(0)
            widths[int(idx.group(1))] = int(sov.group(1)) if sov else 2
    else:
        import json

        doc = json.loads(meta)
        for ch in doc["osf"]["channels"]:
            widths[int(ch["index"])] = int(ch.get("sizeoflengthvalue", 2))
    assert widths, "metablock declared no channels"
    assert set(widths.values()) <= {2, 4}, widths

    frames: list[tuple[int, int, int]] = []
    pos = nl + 1 + meta_len
    while pos + 2 < len(data):
        channel = int.from_bytes(data[pos : pos + 2], "little")
        assert channel in widths, f"undeclared channel {channel} at offset {pos}"
        width = widths[channel]
        length = int.from_bytes(data[pos + 2 : pos + 2 + width], "little")
        assert length >= 1, (
            f"zero-length frame on channel {channel} at offset {pos} - this "
            "walker reads the control byte and cannot classify such a frame"
        )
        frames.append((channel, length, data[pos + 2 + width]))
        pos += 2 + width + length
    assert pos == len(data), "frame walk did not land exactly on EOF"
    return frames


def _message_event_frames(data: bytes) -> list[tuple[int, int, int]]:
    return [f for f in _frames(data) if f[2] & ~MULTI_SAMPLE_FLAG == CONTROL_MESSAGE_EVENT]


def test_the_detector_fires_on_the_known_message_event_corpus_file():
    """Anti-vacuity guard.

    Point the detector at the corpus file that *does* carry control byte 4 and
    require it to find all five frames; a walker that silently found nothing
    would make the round-trip assertion below pass for the wrong reason. The
    equivalent file is the negative control — same five samples, encoded as
    ``bcAbsTimeStampData`` — so a detector that merely answered "everything"
    fails here.
    """
    with open(_example("generated/osf4_message_event_string.osf"), "rb") as fh:
        legacy = fh.read()
    found = _message_event_frames(legacy)
    assert len(found) == 5, found
    assert all(channel == 1 for channel, _len, _ctrl in found)
    assert all(ctrl == CONTROL_MESSAGE_EVENT for _ch, _len, ctrl in found)

    with open(
        _example("generated/osf4_message_event_string_equivalent.osf"), "rb"
    ) as fh:
        equivalent = fh.read()
    assert _message_event_frames(equivalent) == []


def test_save_of_a_message_event_file_emits_no_control_byte_four(tmp_path):
    """The round trip is the one path where read support could leak into write
    output: before OSF-UP4 the block was skipped and a load-and-rewrite dropped
    the channel silently; now it decodes, so ``osf.save`` re-emits it.

    It comes back out as ``bcAbsTimeStampData`` (``0x08``), bit 7 clear, one
    block per sample — and the content survives, which is asserted separately so
    that output free of byte 4 *because the channel was dropped* cannot pass.
    """
    src = osf.load(_example("generated/osf4_message_event_string.osf"))
    before = src.channel("Demo.Message").samples()
    before_ts = src.channel("Demo.Message").timestamps_ns()
    assert len(before) == 5

    out = tmp_path / "message-event-roundtrip.osf"
    osf.save(src, str(out))
    with open(out, "rb") as fh:
        data = fh.read()

    assert _message_event_frames(data) == [], "writer output carries control byte 4"

    msg_index = [c.name for c in src.channels].index("Demo.Message")
    msg_frames = [f for f in _frames(data) if f[0] == msg_index]
    assert len(msg_frames) == 5, msg_frames
    assert all(ctrl == CONTROL_ABS_TIMESTAMP for _ch, _len, ctrl in msg_frames)

    back = osf.load(str(out))
    assert back.channel("Demo.Message").samples() == before
    assert list(back.channel("Demo.Message").timestamps_ns()) == list(before_ts)
