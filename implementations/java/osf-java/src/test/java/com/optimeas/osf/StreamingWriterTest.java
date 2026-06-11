// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH
package com.optimeas.osf;

import com.optimeas.osf.internal.Block;
import com.optimeas.osf.internal.BlockReader;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardOpenOption;
import java.util.List;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;

/**
 * TDD tests for {@link StreamingWriter}.
 *
 * <p>The correctness contract is the round trip: bytes written by the streaming
 * writer must decode through {@link DataManager#loadFromFile(Path)} back to the
 * same channels, samples, and timestamps. This pins the file preamble
 * ({@code OSF5 <len>\n} + metablock), the per-block framing produced via the
 * {@link com.optimeas.osf.internal.BlockEncoder}, and the power-loss-safe
 * per-block {@code force(true)} behaviour (the truncation-recovery case below).
 *
 * <p>Reference: {@code implementations/cpp/src/streaming_writer.cpp} and
 * {@code implementations/rust/osf-core/src/writer.rs}.
 */
class StreamingWriterTest {

    @Test
    void timestampedDoubleChannelRoundTrips(@TempDir Path dir) throws IOException {
        Path out = dir.resolve("timestamped.osf");

        try (StreamingWriter w = StreamingWriter.create(out)) {
            int ch = w.addTimestampedChannel("Sensor/Temperature", DataType.DOUBLE, 2);
            w.writeSample(ch, 1000L, 1.5);
            w.writeSample(ch, 2000L, 2.5);
        }

        DataManager mgr = DataManager.loadFromFile(out);
        assertThat(mgr.channels()).hasSize(1);
        DataChannel ch = mgr.channelByName("Sensor/Temperature").orElseThrow();
        assertThat(ch.dataType()).isEqualTo(DataType.DOUBLE);
        assertThat(ch.timestampsNs()).containsExactly(1000L, 2000L);
        assertThat(ch.asDoubles()).containsExactly(1.5, 2.5);
        assertThat(mgr.stats().truncationSeen()).isFalse();
    }

    @Test
    void equidistantDoubleChannelRoundTrips(@TempDir Path dir) throws IOException {
        Path out = dir.resolve("equidistant.osf");
        double rateHz = 1000.0; // 1 ms spacing
        long startNs = 5_000L;
        double[] samples = {1.0, 2.0, 3.0, 4.0};

        try (StreamingWriter w = StreamingWriter.create(out)) {
            int ch = w.addEquidistantChannel("Sensor/Vibration", DataType.DOUBLE, 2, rateHz);
            w.startEquidistantSegment(ch, startNs, samples);
        }

        DataManager mgr = DataManager.loadFromFile(out);
        DataChannel ch = mgr.channelByName("Sensor/Vibration").orElseThrow();
        assertThat(ch.kind()).isEqualTo(DataChannel.Kind.EQUIDISTANT);
        assertThat(ch.asDoubles()).containsExactly(samples);

        // Timestamps reconstructed per ChannelAssembler: start + (long)(i*1e9/rate).
        long[] expected = new long[samples.length];
        for (int i = 0; i < samples.length; i++) {
            expected[i] = (i == 0) ? startNs
                    : startNs + (long) ((double) i * 1.0e9 / rateHz);
        }
        assertThat(ch.timestampsNs()).containsExactly(expected);
        assertThat(mgr.stats().truncationSeen()).isFalse();
    }

    @Test
    void equidistantRejectsNonFloatDouble(@TempDir Path dir) throws IOException {
        Path out = dir.resolve("bad-equi.osf");
        try (StreamingWriter w = StreamingWriter.create(out)) {
            assertThatThrownBy(() ->
                    w.addEquidistantChannel("bad", DataType.INT32, 2, 100.0))
                    .isInstanceOf(IllegalArgumentException.class);
        }
    }

    @Test
    void writingToUnknownChannelThrows(@TempDir Path dir) throws IOException {
        Path out = dir.resolve("unknown.osf");
        try (StreamingWriter w = StreamingWriter.create(out)) {
            w.addTimestampedChannel("ok", DataType.DOUBLE, 2);
            assertThatThrownBy(() -> w.writeSample(99, 1L, 1.0))
                    .isInstanceOf(OsfException.class);
        }
    }

    @Test
    void truncatingMidBlockRecoversEarlierBlocks(@TempDir Path dir) throws IOException {
        Path out = dir.resolve("truncated.osf");

        // Emit two whole, force()d blocks with an explicit flush() boundary:
        //   block 1 — a 3-sample multi-sample block (samples 1..3)
        //   block 2 — a 1-sample block (sample 4)
        // Truncation recovery is at BLOCK granularity, so cutting into block 2
        // must still leave block 1 (all three of its samples) intact.
        try (StreamingWriter w = StreamingWriter.create(out)) {
            int ch = w.addTimestampedChannel("ch", DataType.DOUBLE, 2);
            w.writeSample(ch, 100L, 1.0);
            w.writeSample(ch, 200L, 2.0);
            w.writeSample(ch, 300L, 3.0);
            w.flush(); // forces the buffered 3-sample block to disk
            long afterBlock1 = Files.size(out);
            w.writeSample(ch, 400L, 4.0);
            w.flush(); // forces the 1-sample block to disk

            long fullLen = Files.size(out);
            // The trailing (2nd) block occupies fullLen - afterBlock1 bytes; cut
            // into it so it is incomplete but block 1 survives untouched.
            assertThat(fullLen).isGreaterThan(afterBlock1);
            byte[] full = Files.readAllBytes(out);
            int keep = (int) afterBlock1 + 3; // a few bytes into block 2's frame
            byte[] truncated = new byte[keep];
            System.arraycopy(full, 0, truncated, 0, keep);
            Files.write(out, truncated, StandardOpenOption.TRUNCATE_EXISTING);
        }

        DataManager mgr = DataManager.loadFromFile(out);
        DataChannel ch = mgr.channelByName("ch").orElseThrow();
        // The trailing block was cut: block 1's three samples survive, no throw.
        assertThat(ch.asDoubles()).containsExactly(1.0, 2.0, 3.0);
        assertThat(ch.timestampsNs()).containsExactly(100L, 200L, 300L);
        assertThat(mgr.stats().truncationSeen()).isTrue();
    }

    @Test
    void preambleIsOsf5MagicLineFollowedByMetablock(@TempDir Path dir) throws IOException {
        Path out = dir.resolve("preamble.osf");
        try (StreamingWriter w = StreamingWriter.create(out)) {
            int ch = w.addTimestampedChannel("c", DataType.DOUBLE, 2);
            w.writeSample(ch, 1L, 1.0);
        }
        byte[] bytes = Files.readAllBytes(out);
        String head = new String(bytes, 0, Math.min(8, bytes.length), StandardCharsets.US_ASCII);
        assertThat(head).startsWith("OSF5 ");

        // created_utc must be injected into the file metadata.
        DataManager mgr = DataManager.loadFromFile(out);
        assertThat(mgr.metadata()).containsKey("created_utc");
        assertThat(mgr.metadata().get("created_utc")).matches("\\d{4}-\\d{2}-\\d{2}T\\d{2}:\\d{2}:\\d{2}Z");
    }

    @Test
    void timestampedStringChannelRoundTrips(@TempDir Path dir) throws IOException {
        Path out = dir.resolve("strings.osf");
        try (StreamingWriter w = StreamingWriter.create(out)) {
            int ch = w.addTimestampedChannel("events", DataType.STRING, 2);
            w.writeSample(ch, 10L, "alpha");
            w.writeSample(ch, 20L, "beta");
        }
        DataManager mgr = DataManager.loadFromFile(out);
        DataChannel ch = mgr.channelByName("events").orElseThrow();
        assertThat(ch.timestampsNs()).containsExactly(10L, 20L);
        assertThat(ch.asStrings()).containsExactly("alpha", "beta");
    }

    @Test
    void multiSampleChannelBatchesIntoFewerBlocksThanSamples(@TempDir Path dir) throws IOException {
        // White-box: a run of many samples on one channel (well under
        // maxSamplesPerBlock) must be packed into a SINGLE multi-sample block —
        // proving the writer batches rather than emitting one block per sample.
        Path out = dir.resolve("batched.osf");
        int sampleCount = 50;
        try (StreamingWriter w = StreamingWriter.create(out)) {
            int chIdx = w.addTimestampedChannel("c", DataType.DOUBLE, 2);
            for (int i = 0; i < sampleCount; i++) {
                w.writeSample(chIdx, 100L + i, i * 0.5);
            }
        }

        List<Block> blocks = dataBlocks(out);
        assertThat(blocks)
                .as("a %d-sample run must batch into far fewer blocks", sampleCount)
                .hasSizeLessThan(sampleCount);
        // All 50 samples fit in one 2-byte-length block (50*16+5 well under 64KiB).
        assertThat(blocks).hasSize(1);

        // And the data still round-trips.
        DataManager mgr = DataManager.loadFromFile(out);
        DataChannel ch = mgr.channelByName("c").orElseThrow();
        assertThat(ch.timestampsNs()).hasSize(sampleCount);
        assertThat(ch.asDoubles()).hasSize(sampleCount);
        assertThat(ch.asDoubles()[0]).isEqualTo(0.0);
        assertThat(ch.asDoubles()[sampleCount - 1]).isEqualTo((sampleCount - 1) * 0.5);
    }

    @Test
    void batchWriteSamplesRoundTrips(@TempDir Path dir) throws IOException {
        // The batch writeSamples(...) overload accumulates and emits a single
        // multi-sample block, round-tripping to the same data.
        Path out = dir.resolve("batch-api.osf");
        long[] ts = {10L, 20L, 30L, 40L};
        double[] vs = {1.1, 2.2, 3.3, 4.4};
        try (StreamingWriter w = StreamingWriter.create(out)) {
            int chIdx = w.addTimestampedChannel("c", DataType.DOUBLE, 2);
            w.writeSamples(chIdx, ts, vs);
        }

        assertThat(dataBlocks(out)).hasSize(1);
        DataManager mgr = DataManager.loadFromFile(out);
        DataChannel ch = mgr.channelByName("c").orElseThrow();
        assertThat(ch.timestampsNs()).containsExactly(ts);
        assertThat(ch.asDoubles()).containsExactly(vs);
    }

    // ---------------------------------------------------------------
    // Helpers.
    // ---------------------------------------------------------------

    /** Parse an OSF5 file and return its decoded data blocks (white-box). */
    private static List<Block> dataBlocks(Path file) throws IOException {
        byte[] bytes = Files.readAllBytes(file);
        MagicHeader header = MagicHeaderParser.parse(bytes);
        int metaStart = header.headerByteLength();
        int metaLen = (int) header.metablockLength();
        byte[] metaBytes = java.util.Arrays.copyOfRange(bytes, metaStart, metaStart + metaLen);
        Metablock meta = MetablockParser.parse(header.version(), metaBytes);
        byte[] afterMeta = java.util.Arrays.copyOfRange(bytes, metaStart + metaLen, bytes.length);

        java.util.Map<Integer, ChannelDef> byIndex = new java.util.HashMap<>();
        for (ChannelDef def : meta.channels()) {
            byIndex.put(def.index(), def);
        }
        ReaderStats stats = new ReaderStats();
        List<Block> all = BlockReader.readAll(afterMeta, header.version(), byIndex, stats);
        // Filter to real data blocks (skip any forward-compat Skipped entries).
        List<Block> data = new java.util.ArrayList<>();
        for (Block b : all) {
            if (!(b instanceof Block.Skipped)) {
                data.add(b);
            }
        }
        return data;
    }
}
