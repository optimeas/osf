// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH
package com.optimeas.osf;

import com.optimeas.osf.testutil.ExamplesDir;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;

/**
 * TDD tests for {@link BlockWriter}.
 *
 * <p>The primary contract is the round trip: bytes written by the in-memory
 * block writer must decode through {@link DataManager#loadFromFile(Path)} back
 * to the same channels, samples, and timestamps. This pins the file preamble
 * ({@code OSF5 <len>\n} + metablock built via
 * {@link com.optimeas.osf.internal.MetablockBuilder}), the per-block framing
 * produced via the {@link com.optimeas.osf.internal.BlockEncoder}, the
 * reference multi-sample block chunking, the {@code sizeoflengthvalue} auto-bump
 * 2&nbsp;&rarr;&nbsp;4, and {@link BlockWriter#fromManager(DataManager)}.
 *
 * <p>Reference: {@code implementations/cpp/src/block_writer.cpp} and
 * {@code implementations/rust/osf-core/src/writer.rs}.
 */
class BlockWriterTest {

    @Test
    void timestampedDoubleChannelRoundTrips(@TempDir Path dir) throws IOException {
        Path out = dir.resolve("bw-timestamped.osf");

        BlockWriter w = new BlockWriter();
        int ch = w.addTimestampedChannel("Sensor/Temperature", DataType.DOUBLE);
        w.writeSample(ch, 1000L, 1.5);
        w.writeSample(ch, 2000L, 2.5);
        w.writeToFile(out);

        DataManager mgr = DataManager.loadFromFile(out);
        assertThat(mgr.channels()).hasSize(1);
        DataChannel c = mgr.channelByName("Sensor/Temperature").orElseThrow();
        assertThat(c.dataType()).isEqualTo(DataType.DOUBLE);
        assertThat(c.timestampsNs()).containsExactly(1000L, 2000L);
        assertThat(c.asDoubles()).containsExactly(1.5, 2.5);
        assertThat(mgr.stats().truncationSeen()).isFalse();
    }

    @Test
    void preambleIsOsf5MagicLineWithCreatedUtc(@TempDir Path dir) throws IOException {
        Path out = dir.resolve("bw-preamble.osf");
        BlockWriter w = new BlockWriter();
        int ch = w.addTimestampedChannel("c", DataType.DOUBLE);
        w.writeSample(ch, 1L, 1.0);
        w.writeToFile(out);

        byte[] bytes = Files.readAllBytes(out);
        String head = new String(bytes, 0, Math.min(8, bytes.length), StandardCharsets.US_ASCII);
        assertThat(head).startsWith("OSF5 ");

        DataManager mgr = DataManager.loadFromFile(out);
        assertThat(mgr.metadata()).containsKey("created_utc");
        assertThat(mgr.metadata().get("created_utc"))
                .matches("\\d{4}-\\d{2}-\\d{2}T\\d{2}:\\d{2}:\\d{2}Z");
    }

    @Test
    void honorsPinnedCreatedUtc(@TempDir Path dir) throws IOException {
        Path out = dir.resolve("bw-pinned-utc.osf");
        BlockWriter w = new BlockWriter();
        w.setMetadata("created_utc", "2026-01-01T00:00:00Z");
        int ch = w.addTimestampedChannel("c", DataType.DOUBLE);
        w.writeSample(ch, 1L, 1.0);
        w.writeToFile(out);

        DataManager mgr = DataManager.loadFromFile(out);
        assertThat(mgr.metadata().get("created_utc")).isEqualTo("2026-01-01T00:00:00Z");
    }

    @Test
    void equidistantDoubleChannelRoundTrips(@TempDir Path dir) throws IOException {
        Path out = dir.resolve("bw-equidistant.osf");
        double rateHz = 1000.0;
        long startNs = 5_000L;
        double[] samples = {1.0, 2.0, 3.0, 4.0};

        BlockWriter w = new BlockWriter();
        int ch = w.addEquidistantChannel("Sensor/Vibration", DataType.DOUBLE, 2, rateHz);
        w.startEquidistantSegment(ch, startNs, samples);
        w.writeToFile(out);

        DataManager mgr = DataManager.loadFromFile(out);
        DataChannel c = mgr.channelByName("Sensor/Vibration").orElseThrow();
        assertThat(c.kind()).isEqualTo(DataChannel.Kind.EQUIDISTANT);
        assertThat(c.asDoubles()).containsExactly(samples);
        assertThat(mgr.stats().truncationSeen()).isFalse();
    }

    @Test
    void stringChannelRoundTrips(@TempDir Path dir) throws IOException {
        Path out = dir.resolve("bw-strings.osf");
        BlockWriter w = new BlockWriter();
        int ch = w.addTimestampedChannel("events", DataType.STRING);
        w.writeSample(ch, 10L, "alpha");
        w.writeSample(ch, 20L, "beta");
        w.writeToFile(out);

        DataManager mgr = DataManager.loadFromFile(out);
        DataChannel c = mgr.channelByName("events").orElseThrow();
        assertThat(c.timestampsNs()).containsExactly(10L, 20L);
        assertThat(c.asStrings()).containsExactly("alpha", "beta");
    }

    /**
     * A binary channel whose single sample exceeds the 2-byte length field
     * (65535 bytes) must auto-bump {@code sizeoflengthvalue} to 4 and read back
     * correctly. With the no-width {@code addTimestampedChannel} form the writer
     * starts at 2 and promotes; the StreamingWriter (which cannot bump) would
     * have rejected this.
     */
    @Test
    void autoBumpsLengthFieldForOversizedBinarySample(@TempDir Path dir) throws IOException {
        Path out = dir.resolve("bw-autobump.osf");
        byte[] big = new byte[70_000]; // > 65535, needs sizeoflengthvalue=4
        for (int i = 0; i < big.length; i++) {
            big[i] = (byte) (i & 0xFF);
        }

        BlockWriter w = new BlockWriter();
        int ch = w.addTimestampedChannel("blob", DataType.BINARY);
        w.writeSample(ch, 42L, big);
        w.writeToFile(out);

        DataManager mgr = DataManager.loadFromFile(out);
        DataChannel c = mgr.channelByName("blob").orElseThrow();
        assertThat(c.timestampsNs()).containsExactly(42L);
        byte[][] bins = c.asBinaries();
        assertThat(bins.length).isEqualTo(1);
        assertThat(bins[0]).isEqualTo(big);
        // The metablock must record the promoted width.
        assertThat(mgr.channelByName("blob").orElseThrow()).isNotNull();
        assertThat(mgr.stats().truncationSeen()).isFalse();

        // Confirm the promotion is visible in the metablock JSON.
        String json = new String(Files.readAllBytes(out), StandardCharsets.UTF_8);
        assertThat(json).contains("\"sizeoflengthvalue\" : 4");
    }

    /**
     * The explicit-width overload pins {@code sizeoflengthvalue} and an oversized
     * sample must then be rejected rather than silently promoted (mirrors the
     * StreamingWriter's fixed-width behaviour and the reference's "honor caller
     * override" rule).
     */
    @Test
    void explicitWidthIsNotAutoBumpedAndRejectsOverflow(@TempDir Path dir) {
        Path out = dir.resolve("bw-explicit-overflow.osf");
        byte[] big = new byte[70_000];

        BlockWriter w = new BlockWriter();
        int ch = w.addTimestampedChannel("blob", DataType.BINARY, 2);
        w.writeSample(ch, 1L, big);
        assertThatThrownBy(() -> w.writeToFile(out))
                .isInstanceOf(OsfException.class);
    }

    @Test
    void manyTimestampedSamplesRoundTripAcrossChunkedBlocks(@TempDir Path dir) throws IOException {
        // Enough double samples that the writer must split into more than one
        // multi-sample bcAbsTimeStampData block (per-sample = 8+8 = 16 bytes;
        // a 2-byte length field caps a block at 65535 bytes ≈ 4095 samples).
        Path out = dir.resolve("bw-chunked.osf");
        int n = 10_000;
        BlockWriter w = new BlockWriter();
        int ch = w.addTimestampedChannel("c", DataType.DOUBLE);
        long[] ts = new long[n];
        double[] vals = new double[n];
        for (int i = 0; i < n; i++) {
            ts[i] = 1000L + i;
            vals[i] = i * 0.5;
            w.writeSample(ch, ts[i], vals[i]);
        }
        w.writeToFile(out);

        DataManager mgr = DataManager.loadFromFile(out);
        DataChannel c = mgr.channelByName("c").orElseThrow();
        assertThat(c.timestampsNs()).containsExactly(ts);
        assertThat(c.asDoubles()).containsExactly(vals);
        assertThat(mgr.stats().truncationSeen()).isFalse();
    }

    /**
     * {@link BlockWriter#fromManager(DataManager)} reconstructs a writer from a
     * loaded file and re-serialises it; the re-read result must carry the same
     * channels (count / names / types / sample counts) and bitwise-equal values.
     */
    @Test
    void fromManagerRoundTripsGeneratedCorpus(@TempDir Path dir) throws IOException {
        Path src = ExamplesDir.resolve().resolve("generated").resolve("osf5_scalar_numeric.osf");
        org.junit.jupiter.api.Assumptions.assumeTrue(Files.isRegularFile(src),
                "corpus file " + src + " not present — skipping");

        DataManager original = DataManager.loadFromFile(src);

        Path out = dir.resolve("bw-from-manager.osf");
        BlockWriter.fromManager(original).writeToFile(out);
        DataManager copy = DataManager.loadFromFile(out);

        List<DataChannel> origCh = original.channels();
        List<DataChannel> copyCh = copy.channels();
        assertThat(copyCh).hasSameSizeAs(origCh);

        for (DataChannel oc : origCh) {
            DataChannel cc = copy.channelByName(oc.name()).orElseThrow();
            assertThat(cc.dataType()).as("dataType of %s", oc.name()).isEqualTo(oc.dataType());
            assertThat(cc.sampleCount()).as("sampleCount of %s", oc.name())
                    .isEqualTo(oc.sampleCount());
            assertThat(cc.timestampsNs()).as("timestamps of %s", oc.name())
                    .containsExactly(oc.timestampsNs());
            // Numeric channels in this corpus file: compare bitwise via doubles.
            if (oc.dataType() != DataType.STRING && oc.dataType() != DataType.BINARY
                    && oc.dataType() != DataType.GPS_LOCATION) {
                assertThat(cc.asDoubles()).as("values of %s", oc.name())
                        .containsExactly(oc.asDoubles());
            }
        }
    }

    @Test
    void writeWithoutChannelsThrows(@TempDir Path dir) {
        Path out = dir.resolve("bw-empty.osf");
        BlockWriter w = new BlockWriter();
        assertThatThrownBy(() -> w.writeToFile(out)).isInstanceOf(OsfException.class);
    }
}
