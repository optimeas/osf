// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH
package com.optimeas.osf;

import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

import java.io.IOException;
import java.nio.channels.FileChannel;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardOpenOption;

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

        // Write four single-sample blocks (each force()d to disk).
        try (StreamingWriter w = StreamingWriter.create(out)) {
            int ch = w.addTimestampedChannel("ch", DataType.DOUBLE, 2);
            w.writeSample(ch, 100L, 1.0);
            w.writeSample(ch, 200L, 2.0);
            w.writeSample(ch, 300L, 3.0);
            w.writeSample(ch, 400L, 4.0);
        }

        long fullLen = Files.size(out);

        // A whole single-sample double block frame is:
        //   [u16 idx][u16 len][ctrl=0x08][i64 ts][f64 value] = 2 + 2 + 1 + 8 + 8 = 21 bytes.
        // Cut 5 bytes off the end so the trailing block is incomplete but the
        // first three blocks remain intact. Mirrors the Rust resize_file
        // regression: the reader returns the blocks before the cut and flags
        // truncation rather than throwing.
        byte[] full = Files.readAllBytes(out);
        byte[] truncated = new byte[(int) fullLen - 5];
        System.arraycopy(full, 0, truncated, 0, truncated.length);
        Files.write(out, truncated, StandardOpenOption.TRUNCATE_EXISTING);

        DataManager mgr = DataManager.loadFromFile(out);
        DataChannel ch = mgr.channelByName("ch").orElseThrow();
        // The last (4th) block was cut: three samples survive, no throw.
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
    void forcePersistsEachBlockSeparately(@TempDir Path dir) throws IOException {
        // White-box check that data hits the file as blocks are written (the
        // force(true) per-block contract): each writeSample grows the on-disk
        // file by exactly one block frame, before close.
        Path out = dir.resolve("force.osf");
        try (StreamingWriter w = StreamingWriter.create(out)) {
            int ch = w.addTimestampedChannel("c", DataType.DOUBLE, 2);
            // begin() writes the preamble lazily (it could also be deferred to
            // the first writeSample; calling it explicitly pins the size below).
            w.begin();
            long afterPreamble = Files.size(out);
            assertThat(afterPreamble).isGreaterThan(0);
            w.writeSample(ch, 1L, 1.0);
            long afterOne = Files.size(out);
            // One single-sample double block frame: 2+2+1+8+8 = 21 bytes.
            assertThat(afterOne).isEqualTo(afterPreamble + 21);
            w.writeSample(ch, 2L, 2.0);
            assertThat(Files.size(out)).isEqualTo(afterOne + 21);
        }
    }
}
