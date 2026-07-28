// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH
package com.optimeas.osf;

import com.optimeas.osf.internal.BlockChunking;
import com.optimeas.osf.internal.JsonMetablockParser;
import com.optimeas.osf.testutil.ExamplesDir;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;

import static org.assertj.core.api.Assertions.assertThat;

/**
 * OSF-UP3 writer negative proof: neither Java writer emits a data block whose
 * per-channel length field reads {@code 0}.
 *
 * <p>A zero-length data block is a non-conforming writer artefact — a
 * conforming block always carries at least its control byte (DECISIONS §25).
 * Blocks of that shape were observed in real field recordings in July 2026 and
 * the producing writer is still unknown; these tests clear
 * {@link BlockWriter} and {@link StreamingWriter}.
 *
 * <p>Two independent assertions per case:
 * <ol>
 *   <li><b>Byte level.</b> Walk the raw block stream and check every length
 *       field directly, independent of the reader.</li>
 *   <li><b>Reader level.</b> Read the file back and assert
 *       {@code stats().blocksSkippedZeroLength() == 0} plus the sample data, so
 *       a file that looks clean only because the reader gave up early cannot
 *       pass.</li>
 * </ol>
 *
 * <p>The cases are the three risk shapes the audit enumerated: chunking-loop
 * boundaries (including payloads that divide <em>exactly</em> by the chunk
 * size), empty variable-length ({@code string} / {@code binary}) samples, and
 * channels declared but never written to. The empty-equidistant-segment case is
 * the one place where these writers emit a block for zero samples at all —
 * {@code BlockWriter.emitEquidistant} and
 * {@code StreamingWriter.flushEquidistant} write the {@code bcStartData} opener
 * unconditionally — so it is the case with genuine doubt rather than a
 * formality.
 */
class WriterZeroLengthAuditTest {

    /** One {@code [u16 channel][u16 len]} frame header of the block stream. */
    private record Frame(int channel, int len) {}

    /**
     * Walk the block stream and return every frame header.
     *
     * <p>Assumes every channel uses {@code sizeoflengthvalue = 2} — asserted
     * against the parsed metablock rather than assumed in a comment. That
     * matters for {@link #detectorsFireOnTheKnownMalformedCorpusFile()}, which
     * reads a corpus file this class did not write: were it regenerated with
     * width 4, an unchecked walker would misparse it and fail with a confusing
     * EOF mismatch instead of a direct message.
     */
    private static List<Frame> frames(byte[] bytes) {
        int nl = -1;
        for (int i = 0; i < bytes.length; i++) {
            if (bytes[i] == '\n') {
                nl = i;
                break;
            }
        }
        assertThat(nl).as("magic header line terminator").isGreaterThan(0);
        String line = new String(bytes, 0, nl, StandardCharsets.US_ASCII);
        String[] parts = line.trim().split("\\s+");
        assertThat(parts[0]).isEqualTo("OSF5");
        int metaLen = Integer.parseInt(parts[1]);

        Metablock meta = new JsonMetablockParser()
                .parse(java.util.Arrays.copyOfRange(bytes, nl + 1, nl + 1 + metaLen));
        for (ChannelDef def : meta.channels()) {
            assertThat(def.sizeOfLengthValue())
                    .as("channel %d declares sizeoflengthvalue - this frame "
                        + "walker assumes 2", def.index())
                    .isEqualTo(2);
        }

        List<Frame> out = new ArrayList<>();
        int pos = nl + 1 + metaLen;
        while (pos + 4 <= bytes.length) {
            int channel = (bytes[pos] & 0xFF) | ((bytes[pos + 1] & 0xFF) << 8);
            int len = (bytes[pos + 2] & 0xFF) | ((bytes[pos + 3] & 0xFF) << 8);
            out.add(new Frame(channel, len));
            // The +4 is the frame header itself, so a zero-length frame still
            // advances the walk; the caller asserts len != 0.
            pos += 4 + len;
        }
        assertThat(pos).as("frame walk lands exactly on EOF").isEqualTo(bytes.length);
        return out;
    }

    /** Assert no frame carries a zero length field, and return the frames. */
    private static List<Frame> assertNoZeroLengthFrame(Path file) throws IOException {
        List<Frame> fr = frames(Files.readAllBytes(file));
        assertThat(fr).allSatisfy(f -> assertThat(f.len())
                .as("length field of a frame on channel %d", f.channel())
                .isNotZero());
        return fr;
    }

    /** Read the file back and assert the reader saw no zero-length block. */
    private static DataManager readBackClean(Path file) {
        DataManager mgr = DataManager.loadFromFile(file);
        assertThat(mgr.stats().blocksSkippedZeroLength())
                .as("zero-length block skips in writer output")
                .isZero();
        assertThat(mgr.stats().truncationSeen()).isFalse();
        return mgr;
    }

    // ---------------------------------------------------------------
    // Anti-vacuity guard.
    // ---------------------------------------------------------------

    /**
     * Point the same two detectors at the hand-assembled corpus file that
     * <em>does</em> carry a zero-length frame and prove they fire. Without
     * this, a frame walker that silently found nothing — or a counter the
     * reader never increments — would make every test below pass for the wrong
     * reason.
     */
    @Test
    void detectorsFireOnTheKnownMalformedCorpusFile() throws IOException {
        Path file = ExamplesDir.resolve()
                .resolve("generated").resolve("malformed")
                .resolve("osf5_zero_length_block.osf");

        List<Frame> fr = frames(Files.readAllBytes(file));
        assertThat(fr).filteredOn(f -> f.len() == 0)
                .as("the known zero-length frame")
                .hasSize(1);

        DataManager mgr = DataManager.loadFromFile(file);
        assertThat(mgr.stats().blocksSkippedZeroLength()).isEqualTo(1L);
    }

    // ---------------------------------------------------------------
    // Risk shape 3 — channels declared but never written.
    // ---------------------------------------------------------------

    @Test
    void blockWriterEmitsNoBlockForAnUnwrittenChannel(@TempDir Path dir) throws IOException {
        Path out = dir.resolve("bw-silent.osf");
        BlockWriter w = new BlockWriter();
        int silent = w.addTimestampedChannel("Sensor/Silent", DataType.DOUBLE, 2);
        int loud = w.addTimestampedChannel("Sensor/Loud", DataType.DOUBLE, 2);
        w.writeSample(loud, 1_000L, 1.0);
        w.writeToFile(out);

        assertThat(assertNoZeroLengthFrame(out)).noneMatch(f -> f.channel() == silent);
        assertThat(readBackClean(out).channels()).hasSize(2);
    }

    @Test
    void streamingWriterEmitsNoBlockForAnUnwrittenChannel(@TempDir Path dir) throws IOException {
        Path out = dir.resolve("sw-silent.osf");
        int silent;
        int loud;
        try (StreamingWriter w = StreamingWriter.create(out)) {
            silent = w.addTimestampedChannel("Sensor/Silent", DataType.DOUBLE, 2);
            loud = w.addTimestampedChannel("Sensor/Loud", DataType.DOUBLE, 2);
            w.writeSample(loud, 1_000L, 1.0);
        }

        assertThat(assertNoZeroLengthFrame(out)).noneMatch(f -> f.channel() == silent);
        assertThat(readBackClean(out).channels()).hasSize(2);
    }

    // ---------------------------------------------------------------
    // Risk shape 1, degenerate end — an empty equidistant segment.
    // ---------------------------------------------------------------

    /**
     * {@code startEquidistantSegment} accepts an empty sample array (there is
     * no length guard), and the emitter writes the opener unconditionally. The
     * resulting block carries zero samples but still carries its control byte,
     * the timestamp, the rate and the {@code u32} count — 21 bytes, never 0.
     */
    @Test
    void blockWriterEmptyEquidistantSegmentEmitsANonEmptyBlock(@TempDir Path dir)
            throws IOException {
        Path out = dir.resolve("bw-empty-segment.osf");
        BlockWriter w = new BlockWriter();
        int ch = w.addEquidistantChannel("Sensor/Eq", DataType.DOUBLE, 2, 1000.0);
        w.startEquidistantSegment(ch, 1_000_000_000L, new double[0]);
        w.startEquidistantSegment(ch, 2_000_000_000L, new double[] {1.0, 2.0, 3.0});
        w.writeToFile(out);

        List<Frame> fr = assertNoZeroLengthFrame(out);
        assertThat(fr.get(0)).isEqualTo(new Frame(ch, 21));

        DataChannel c = readBackClean(out).channelByName("Sensor/Eq").orElseThrow();
        assertThat(c.asDoubles()).containsExactly(1.0, 2.0, 3.0);
    }

    @Test
    void streamingWriterEmptyEquidistantSegmentEmitsANonEmptyBlock(@TempDir Path dir)
            throws IOException {
        Path out = dir.resolve("sw-empty-segment.osf");
        int ch;
        try (StreamingWriter w = StreamingWriter.create(out)) {
            ch = w.addEquidistantChannel("Sensor/Eq", DataType.DOUBLE, 2, 1000.0);
            w.startEquidistantSegment(ch, 1_000_000_000L, new double[0]);
            w.startEquidistantSegment(ch, 2_000_000_000L, new double[] {1.0, 2.0, 3.0});
        }

        List<Frame> fr = assertNoZeroLengthFrame(out);
        assertThat(fr.get(0)).isEqualTo(new Frame(ch, 21));

        DataChannel c = readBackClean(out).channelByName("Sensor/Eq").orElseThrow();
        assertThat(c.asDoubles()).containsExactly(1.0, 2.0, 3.0);
    }

    // ---------------------------------------------------------------
    // Risk shape 2 — empty string / binary samples.
    // ---------------------------------------------------------------

    /**
     * OSF5 appends no trailing {@code 0x00}, so this is the version where an
     * empty payload could plausibly reach length 0 — it does not:
     * {@code BlockEncoder.variableBinaryBlock} writes the control byte and the
     * i64 timestamp first, i.e. 9 bytes for an empty sample.
     */
    @Test
    void blockWriterEmptyVariableSamplesEmitNineByteBlocks(@TempDir Path dir) throws IOException {
        Path out = dir.resolve("bw-empty-variable.osf");
        BlockWriter w = new BlockWriter();
        int s = w.addTimestampedChannel("Sensor/Str", DataType.STRING, 2);
        int b = w.addTimestampedChannel("Sensor/Bin", DataType.BINARY, 2);
        w.writeSample(s, 1_000L, "");
        w.writeSample(s, 2_000L, "x");
        w.writeSample(b, 1_000L, new byte[0]);
        w.writeSample(b, 2_000L, new byte[] {(byte) 0xAA});
        w.writeToFile(out);

        List<Frame> fr = assertNoZeroLengthFrame(out);
        assertThat(fr).contains(new Frame(s, 9), new Frame(b, 9));

        DataManager mgr = readBackClean(out);
        assertThat(mgr.channelByName("Sensor/Str").orElseThrow().asStrings())
                .containsExactly("", "x");
        assertThat(mgr.channelByName("Sensor/Bin").orElseThrow().asBinaries()[0]).isEmpty();
    }

    @Test
    void streamingWriterEmptyVariableSamplesEmitNineByteBlocks(@TempDir Path dir)
            throws IOException {
        Path out = dir.resolve("sw-empty-variable.osf");
        int s;
        int b;
        try (StreamingWriter w = StreamingWriter.create(out)) {
            s = w.addTimestampedChannel("Sensor/Str", DataType.STRING, 2);
            b = w.addTimestampedChannel("Sensor/Bin", DataType.BINARY, 2);
            w.writeSample(s, 1_000L, "");
            w.writeSample(s, 2_000L, "x");
            w.writeSample(b, 1_000L, new byte[0]);
            w.writeSample(b, 2_000L, new byte[] {(byte) 0xAA});
        }

        List<Frame> fr = assertNoZeroLengthFrame(out);
        assertThat(fr).contains(new Frame(s, 9), new Frame(b, 9));

        DataManager mgr = readBackClean(out);
        assertThat(mgr.channelByName("Sensor/Str").orElseThrow().asStrings())
                .containsExactly("", "x");
        assertThat(mgr.channelByName("Sensor/Bin").orElseThrow().asBinaries()[0]).isEmpty();
    }

    // ---------------------------------------------------------------
    // Risk shape 1 — chunk boundaries that divide exactly.
    // ---------------------------------------------------------------

    /**
     * Sample counts chosen so the payload divides <em>exactly</em> by the chunk
     * size: the opener plus two full continuations must produce exactly three
     * blocks and no trailing empty fourth.
     */
    @Test
    void blockWriterEquidistantExactChunkMultipleEmitsNoTrailingEmptyBlock(@TempDir Path dir)
            throws IOException {
        int maxStart = BlockChunking.maxSamplesPerStart(8, 2);
        int maxCont = BlockChunking.maxSamplesPerContinued(8, 2);
        int total = maxStart + 2 * maxCont;
        double[] samples = new double[total];
        for (int i = 0; i < total; i++) {
            samples[i] = i;
        }

        Path out = dir.resolve("bw-exact-equidistant.osf");
        BlockWriter w = new BlockWriter();
        int ch = w.addEquidistantChannel("Sensor/Eq", DataType.DOUBLE, 2, 1000.0);
        w.startEquidistantSegment(ch, 1_000_000_000L, samples);
        w.writeToFile(out);

        assertThat(assertNoZeroLengthFrame(out)).hasSize(3);
        assertThat(readBackClean(out).channelByName("Sensor/Eq").orElseThrow().asDoubles())
                .hasSize(total);
    }

    /**
     * Same boundary for the timestamped path: two exact blockfuls must produce
     * two blocks and no empty third.
     */
    @Test
    void blockWriterTimestampedExactChunkMultipleEmitsNoTrailingEmptyBlock(@TempDir Path dir)
            throws IOException {
        int maxPer = BlockChunking.maxSamplesPerTimestamped(8, 2);
        int total = 2 * maxPer;

        Path out = dir.resolve("bw-exact-timestamped.osf");
        BlockWriter w = new BlockWriter();
        int ch = w.addTimestampedChannel("Sensor/Ts", DataType.DOUBLE, 2);
        for (int i = 0; i < total; i++) {
            w.writeSample(ch, 1_000L + i, i * 0.25);
        }
        w.writeToFile(out);

        assertThat(assertNoZeroLengthFrame(out)).hasSize(2);
        assertThat(readBackClean(out).channelByName("Sensor/Ts").orElseThrow().timestampsNs())
                .hasSize(total);
    }

    /**
     * The streaming writer emits a block as soon as the accumulator reaches
     * {@code maxSamplesPerTimestamped}, then {@code flush()} drains the
     * remainder — the boundary case is a buffer that empties exactly, where a
     * missing {@code n > 0} guard in {@code flushTimestamped} would emit an
     * empty final block.
     */
    @Test
    void streamingWriterTimestampedExactChunkMultipleEmitsNoTrailingEmptyBlock(@TempDir Path dir)
            throws IOException {
        int maxPer = BlockChunking.maxSamplesPerTimestamped(8, 2);
        int total = 2 * maxPer;

        Path out = dir.resolve("sw-exact-timestamped.osf");
        try (StreamingWriter w = StreamingWriter.create(out)) {
            int ch = w.addTimestampedChannel("Sensor/Ts", DataType.DOUBLE, 2);
            for (int i = 0; i < total; i++) {
                w.writeSample(ch, 1_000L + i, i * 0.25);
            }
        }

        assertThat(assertNoZeroLengthFrame(out)).hasSize(2);
        assertThat(readBackClean(out).channelByName("Sensor/Ts").orElseThrow().timestampsNs())
                .hasSize(total);
    }
}
