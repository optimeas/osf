// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH
package com.optimeas.osf;

import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;

import static org.assertj.core.api.Assertions.assertThat;

/**
 * Tier-1.5 cross-writer agreement between {@link StreamingWriter} and
 * {@link BlockWriter}.
 *
 * <h2>True byte-identity — and the conditions for it</h2>
 *
 * <p>Both writers now batch into multi-sample blocks using the <em>same</em>
 * chunking arithmetic ({@code com.optimeas.osf.internal.BlockChunking},
 * a port of the reference {@code max_samples_per_*_block} sizing in
 * {@code implementations/cpp/src/writer_common.cpp} +
 * {@code block_writer.cpp}). The only difference between them is durability
 * cadence: {@link StreamingWriter} {@code force(true)}s after every block,
 * whereas {@link BlockWriter} writes the whole file in one pass. The on-disk
 * bytes are identical.
 *
 * <p>So for the same logical input the two writers produce <b>byte-for-byte
 * identical OSF5 files</b>, provided:
 * <ul>
 *   <li>the same channels with the same explicit {@code sizeoflengthvalue}
 *       (the streaming writer cannot auto-bump, so the caller must pin the same
 *       width on the block writer — done here via the explicit-width
 *       {@code addTimestampedChannel} overload);</li>
 *   <li>the same metadata in the same order, including a pinned
 *       {@code created_utc} (otherwise each writer injects its own wall-clock
 *       value);</li>
 *   <li>the streaming writer is fed so that each channel's run accumulates into
 *       the same block boundaries — driving it via a batch
 *       {@code writeSamples(...)} (or enough samples that batching engages)
 *       reproduces the block writer's single multi-sample block.</li>
 * </ul>
 *
 * <p>This test asserts:
 * <ol>
 *   <li><b>True full-file byte-identity</b> for the single-sample-per-channel
 *       scenario.</li>
 *   <li><b>True full-file byte-identity</b> for a multi-sample scenario (both
 *       writers emit the identical multi-sample block).</li>
 *   <li><b>Read-back equivalence</b> for the multi-sample scenario.</li>
 * </ol>
 */
class WriterIdentityTest {

    private static final String PINNED_UTC = "2026-01-01T00:00:00Z";

    /**
     * Single sample per channel: the writers produce byte-identical files.
     */
    @Test
    void singleSamplePerChannelIsByteIdentical(@TempDir Path dir) throws IOException {
        byte[] streaming = writeStreamingSingle();
        byte[] block = writeBlockSingle();

        assertThat(block)
                .as("BlockWriter and StreamingWriter must be byte-identical for "
                        + "single-sample-per-channel data (count==1 → no multi flag, no N prefix)")
                .isEqualTo(streaming);

        // And both still load to the same content (sanity).
        Path s = dir.resolve("s.osf");
        Path b = dir.resolve("b.osf");
        Files.write(s, streaming);
        Files.write(b, block);
        assertChannelsEqual(DataManager.loadFromFile(s), DataManager.loadFromFile(b));
    }

    /**
     * Multi-sample scenario: now that both writers batch identically, the full
     * files are byte-identical and read-back equivalent.
     */
    @Test
    void multiSampleIsByteIdenticalAndReadBackEquivalent(@TempDir Path dir) throws IOException {
        byte[] streaming = writeStreamingMulti();
        byte[] block = writeBlockMulti();

        assertThat(block)
                .as("BlockWriter and StreamingWriter must be byte-identical for "
                        + "multi-sample data — both batch at the same "
                        + "max_samples_per_block granularity (same sov, same created_utc)")
                .isEqualTo(streaming);

        Path s = dir.resolve("s-multi.osf");
        Path b = dir.resolve("b-multi.osf");
        Files.write(s, streaming);
        Files.write(b, block);
        assertChannelsEqual(DataManager.loadFromFile(s), DataManager.loadFromFile(b));
    }

    // ---------------------------------------------------------------
    // Scenario writers — identical logical data on both writers.
    // ---------------------------------------------------------------

    private static byte[] writeStreamingSingle() throws IOException {
        Path tmp = Files.createTempFile("osf-stream-single", ".osf");
        try {
            try (StreamingWriter w = StreamingWriter.create(tmp)) {
                w.setMetadata("created_utc", PINNED_UTC);
                int ch = w.addTimestampedChannel("Sensor/Temperature", DataType.DOUBLE, 2);
                w.writeSample(ch, 1000L, 1.5);
            }
            return Files.readAllBytes(tmp);
        } finally {
            Files.deleteIfExists(tmp);
        }
    }

    private static byte[] writeBlockSingle() throws IOException {
        BlockWriter w = new BlockWriter();
        w.setMetadata("created_utc", PINNED_UTC);
        int ch = w.addTimestampedChannel("Sensor/Temperature", DataType.DOUBLE, 2);
        w.writeSample(ch, 1000L, 1.5);
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        w.writeTo(out);
        return out.toByteArray();
    }

    private static final int MULTI_N = 5;

    private static byte[] writeStreamingMulti() throws IOException {
        Path tmp = Files.createTempFile("osf-stream-multi", ".osf");
        try {
            try (StreamingWriter w = StreamingWriter.create(tmp)) {
                w.setMetadata("created_utc", PINNED_UTC);
                // Explicit width (2) so it matches the block writer's pinned width;
                // a batch write accumulates into one multi-sample block.
                int ch = w.addTimestampedChannel("Sensor/Temperature", DataType.DOUBLE, 2);
                long[] ts = new long[MULTI_N];
                double[] vs = new double[MULTI_N];
                for (int i = 0; i < MULTI_N; i++) {
                    ts[i] = 1000L + i;
                    vs[i] = i * 1.25;
                }
                w.writeSamples(ch, ts, vs);
            }
            return Files.readAllBytes(tmp);
        } finally {
            Files.deleteIfExists(tmp);
        }
    }

    private static byte[] writeBlockMulti() throws IOException {
        BlockWriter w = new BlockWriter();
        w.setMetadata("created_utc", PINNED_UTC);
        // Pin width to 2 (explicit overload) to match the streaming writer, which
        // cannot auto-bump — required for byte-identity.
        int ch = w.addTimestampedChannel("Sensor/Temperature", DataType.DOUBLE, 2);
        for (int i = 0; i < MULTI_N; i++) {
            w.writeSample(ch, 1000L + i, i * 1.25);
        }
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        w.writeTo(out);
        return out.toByteArray();
    }

    // ---------------------------------------------------------------
    // Helpers.
    // ---------------------------------------------------------------

    private static void assertChannelsEqual(DataManager a, DataManager b) {
        assertThat(b.channels()).hasSameSizeAs(a.channels());
        for (DataChannel ac : a.channels()) {
            DataChannel bc = b.channelByName(ac.name()).orElseThrow();
            assertThat(bc.dataType()).isEqualTo(ac.dataType());
            assertThat(bc.timestampsNs()).containsExactly(ac.timestampsNs());
            assertThat(bc.asDoubles()).containsExactly(ac.asDoubles());
        }
        assertThat(b.metadata()).isEqualTo(a.metadata());
    }
}
