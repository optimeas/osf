// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH
package com.optimeas.osf;

import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Arrays;

import static org.assertj.core.api.Assertions.assertThat;

/**
 * Tier-1.5 cross-writer agreement between {@link StreamingWriter} and
 * {@link BlockWriter}.
 *
 * <h2>What byte-identity actually holds — and why</h2>
 *
 * <p>The two writers do <em>not</em> use the same block granularity:
 * <ul>
 *   <li>{@link StreamingWriter} emits <b>one single-sample block per
 *       {@code writeSample}</b> (control byte with bit&nbsp;7 clear, no
 *       {@code u32 N} prefix) so a crash leaves a prefix of whole, durable
 *       blocks. This mirrors the C++ {@code streaming_writer.cpp}.</li>
 *   <li>{@link BlockWriter} accumulates and emits <b>multi-sample blocks</b>
 *       (bit&nbsp;7 set + {@code u32 N} prefix), chunked at the reference's
 *       {@code max_samples_per_*_block} granularity. This mirrors the C++
 *       {@code block_writer.cpp} {@code emit_channel} and the Rust
 *       {@code writer.rs} {@code write_abs_timestamp_numeric} /
 *       {@code write_equidistant_segment} (both set {@code MULTI_SAMPLE_FLAG}
 *       whenever {@code count != 1}).</li>
 * </ul>
 *
 * <p>Therefore the two writers are <b>byte-identical only when every channel
 * carries exactly one sample</b> — then {@code count == 1}, BlockWriter's
 * multi-sample flag stays clear, the {@code N} prefix is omitted, and the block
 * frame coincides with the streaming writer's single-sample frame. The C++/Rust
 * references have the same property: their BlockWriter and StreamingWriter are
 * not byte-identical for multi-sample data either, because only the streaming
 * writer is forced to one-sample blocks.
 *
 * <p>Accordingly this test asserts:
 * <ol>
 *   <li><b>True full-file byte-identity</b> for the single-sample-per-channel
 *       scenario (same explicit {@code sizeoflengthvalue}, same pinned
 *       {@code created_utc}).</li>
 *   <li><b>Metablock-preamble byte-identity</b> for a multi-sample scenario
 *       (the {@code OSF5 <len>\n} line + JSON metablock are produced by the same
 *       {@code MetablockBuilder} from the same channel defs and metadata, so the
 *       preambles match even though the block streams differ in granularity).</li>
 *   <li><b>Read-back equivalence</b> for the multi-sample scenario (both files
 *       load to identical channels / values / timestamps via
 *       {@link DataManager}).</li>
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
     * Multi-sample scenario: preambles are byte-identical and the files are
     * read-back equivalent, but the block streams differ in granularity so the
     * full files are NOT byte-identical (see class javadoc).
     */
    @Test
    void multiSamplePreambleIdenticalAndReadBackEquivalent(@TempDir Path dir) throws IOException {
        byte[] streaming = writeStreamingMulti();
        byte[] block = writeBlockMulti();

        int sPre = preambleLength(streaming);
        int bPre = preambleLength(block);
        assertThat(Arrays.copyOf(block, bPre))
                .as("metablock preamble (OSF5 line + JSON) must be byte-identical "
                        + "— same MetablockBuilder, same defs, same pinned created_utc")
                .isEqualTo(Arrays.copyOf(streaming, sPre));

        // Full files are NOT identical (block granularity differs); confirm so
        // the byte-identity claim above is not silently over-broad.
        assertThat(block)
                .as("multi-sample files differ in block granularity")
                .isNotEqualTo(streaming);

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

    private static byte[] writeStreamingMulti() throws IOException {
        Path tmp = Files.createTempFile("osf-stream-multi", ".osf");
        try {
            try (StreamingWriter w = StreamingWriter.create(tmp)) {
                w.setMetadata("created_utc", PINNED_UTC);
                int ch = w.addTimestampedChannel("Sensor/Temperature", DataType.DOUBLE, 2);
                for (int i = 0; i < 5; i++) {
                    w.writeSample(ch, 1000L + i, i * 1.25);
                }
            }
            return Files.readAllBytes(tmp);
        } finally {
            Files.deleteIfExists(tmp);
        }
    }

    private static byte[] writeBlockMulti() throws IOException {
        BlockWriter w = new BlockWriter();
        w.setMetadata("created_utc", PINNED_UTC);
        int ch = w.addTimestampedChannel("Sensor/Temperature", DataType.DOUBLE, 2);
        for (int i = 0; i < 5; i++) {
            w.writeSample(ch, 1000L + i, i * 1.25);
        }
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        w.writeTo(out);
        return out.toByteArray();
    }

    // ---------------------------------------------------------------
    // Helpers.
    // ---------------------------------------------------------------

    /** Length of the {@code OSF5 <len>\n} line + the metablock JSON body. */
    private static int preambleLength(byte[] file) {
        int nl = -1;
        for (int i = 0; i < file.length; i++) {
            if (file[i] == '\n') { nl = i; break; }
        }
        if (nl < 0) {
            throw new IllegalStateException("no magic-header newline found");
        }
        String line = new String(file, 0, nl, java.nio.charset.StandardCharsets.US_ASCII);
        String[] parts = line.split(" ");
        int metaLen = Integer.parseInt(parts[1]);
        return nl + 1 + metaLen;
    }

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
