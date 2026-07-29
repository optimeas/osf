// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH
package com.optimeas.osf;

import com.optimeas.osf.internal.JsonMetablockParser;
import com.optimeas.osf.internal.XmlMetablockParser;
import com.optimeas.osf.testutil.ExamplesDir;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

import static org.assertj.core.api.Assertions.assertThat;

/**
 * OSF-UP4 writer negative proof: neither Java writer emits control byte 4
 * ({@code bcMessageEvent}), in either its plain ({@code 0x04}) or multi-sample
 * ({@code 0x84}) form.
 *
 * <p>{@code bcMessageEvent} became <b>read</b>-mandatory on this branch
 * (DECISIONS §26). Read obligation is not write permission — §26 says writers
 * must never emit it. Most of that guarantee is static and is cleared by
 * reading the code ({@code internal/BlockEncoder.java:50-53} defines exactly
 * three control constants: {@code 0x05}, {@code 0x06} and {@code 0x08}), so it
 * is documented in {@code examples/README.md} rather than tested here. What is
 * <em>not</em> static, and is new since this branch started, is the round trip:
 * before OSF-UP4 a {@code bcMessageEvent} block was skipped and silently
 * dropped by a load-and-rewrite; now it decodes, so its content reaches
 * {@link BlockWriter#fromManager(DataManager)} and gets re-emitted in
 * <em>some</em> encoding. This suite pins which one, for both writers.
 *
 * <p>Two independent assertions per case:
 * <ol>
 *   <li><b>Byte level.</b> Walk the raw block stream and read the control byte
 *       of every frame directly, independent of the reader.</li>
 *   <li><b>Reader level.</b> Read the output back and assert the sample content
 *       survives, so output that is "clean" only because the writer dropped the
 *       channel cannot pass.</li>
 * </ol>
 *
 * <p>The suite opens with an anti-vacuity guard: the same detector is pointed
 * at {@code examples/generated/osf4_message_event_string.osf}, which carries
 * exactly five control-byte-4 frames, and is required to find all five.
 */
class WriterMessageEventAuditTest {

    /** The block type this whole suite is about (DECISIONS §26). */
    private static final int CONTROL_MESSAGE_EVENT = 0x04;
    /** Bit 7 — the multi-sample flag, masked off before comparing a block type. */
    private static final int MULTI_SAMPLE_FLAG = 0x80;
    /** {@code bcAbsTimeStampData} — what a decoded message event is re-emitted as. */
    private static final int CONTROL_ABS_TIMESTAMP = 0x08;

    /** Channel index of {@code Demo.Message} in the committed corpus pair. */
    private static final int MESSAGE_CHANNEL_INDEX = 1;

    /** One frame of the block stream, with its control byte. */
    private record Frame(int channel, long len, int control) {
        /** Block type with the multi-sample bit masked off. */
        int blockType() {
            return control & ~MULTI_SAMPLE_FLAG;
        }
    }

    /**
     * Walk the block stream and return every frame with its control byte.
     *
     * <p>Unlike the OSF-UP3 walker this one is <b>width-aware per channel</b>
     * and handles both metablock dialects: the corpus pair is OSF4/XML and
     * declares {@code Demo.Counter} with {@code sizeoflengthvalue = 2} but
     * {@code Demo.Message} with {@code 4}, while writer output is OSF5/JSON. A
     * walker that assumed one width — or one dialect — would misparse the very
     * file the anti-vacuity guard depends on.
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
        assertThat(parts[0]).as("magic").isIn("OSF4", "OSF5");
        int metaLen = Integer.parseInt(parts[1]);
        byte[] metaBytes = Arrays.copyOfRange(bytes, nl + 1, nl + 1 + metaLen);

        Metablock meta = "OSF4".equals(parts[0])
                ? new XmlMetablockParser().parse(metaBytes)
                : new JsonMetablockParser().parse(metaBytes);

        int[] widthByIndex = new int[65536];
        for (ChannelDef def : meta.channels()) {
            assertThat(def.sizeOfLengthValue())
                    .as("channel %d sizeoflengthvalue", def.index())
                    .isIn(2, 4);
            widthByIndex[def.index()] = def.sizeOfLengthValue();
        }

        List<Frame> out = new ArrayList<>();
        int pos = nl + 1 + metaLen;
        while (pos + 2 < bytes.length) {
            int channel = (bytes[pos] & 0xFF) | ((bytes[pos + 1] & 0xFF) << 8);
            int width = widthByIndex[channel];
            assertThat(width)
                    .as("block stream refers to undeclared channel %d at offset %d", channel, pos)
                    .isNotZero();
            assertThat(pos + 2 + width).as("truncated length field at %d", pos)
                    .isLessThanOrEqualTo(bytes.length);
            long len = width == 2
                    ? ((bytes[pos + 2] & 0xFFL) | ((bytes[pos + 3] & 0xFFL) << 8))
                    : ((bytes[pos + 2] & 0xFFL) | ((bytes[pos + 3] & 0xFFL) << 8)
                       | ((bytes[pos + 4] & 0xFFL) << 16) | ((bytes[pos + 5] & 0xFFL) << 24));
            assertThat(len)
                    .as("zero-length frame on channel %d at offset %d - this walker "
                        + "reads the control byte and cannot classify such a frame",
                        channel, pos)
                    .isNotZero();
            out.add(new Frame(channel, len, bytes[pos + 2 + width] & 0xFF));
            pos += 2 + width + (int) len;
        }
        assertThat(pos).as("frame walk lands exactly on EOF").isEqualTo(bytes.length);
        return out;
    }

    /** Every frame whose block type is {@code bcMessageEvent}, bit 7 ignored. */
    private static List<Frame> messageEventFrames(byte[] bytes) {
        return frames(bytes).stream()
                .filter(f -> f.blockType() == CONTROL_MESSAGE_EVENT)
                .toList();
    }

    private static Path corpus(String name) {
        return ExamplesDir.resolve().resolve("generated").resolve(name);
    }

    /** The five {@code Demo.Message} samples as {@code "<ts>=<text>"} strings. */
    private static List<String> messageSamples(DataManager mgr) {
        DataChannel ch = mgr.channelByName("Demo.Message")
                .orElseThrow(() -> new AssertionError("Demo.Message declared"));
        String[] texts = ch.asStrings();
        long[] ts = ch.timestampsNs();
        assertThat(texts).hasSameSizeAs(ts);
        List<String> out = new ArrayList<>(texts.length);
        for (int i = 0; i < texts.length; i++) {
            out.add(ts[i] + "=" + texts[i]);
        }
        return out;
    }

    // ---------------------------------------------------------------
    // Anti-vacuity guard.
    // ---------------------------------------------------------------

    /**
     * Point the detector at the corpus file that <em>does</em> carry control
     * byte 4 and require it to find all five frames. Without this, a walker
     * that silently found nothing would make every assertion below pass for the
     * wrong reason.
     *
     * <p>The equivalent file is checked in the same test as a negative control:
     * it holds the same five samples encoded as {@code bcAbsTimeStampData}, so
     * a detector that merely returned "everything" would fail here.
     */
    @Test
    void theDetectorFiresOnTheKnownMessageEventCorpusFile() throws IOException {
        byte[] legacy = Files.readAllBytes(corpus("osf4_message_event_string.osf"));
        List<Frame> found = messageEventFrames(legacy);
        assertThat(found).as("the five known control-byte-4 frames").hasSize(5);
        assertThat(found).allSatisfy(f -> {
            assertThat(f.channel()).isEqualTo(MESSAGE_CHANNEL_INDEX);
            assertThat(f.control()).as("bit 7 clear in the corpus")
                    .isEqualTo(CONTROL_MESSAGE_EVENT);
        });

        byte[] equivalent =
                Files.readAllBytes(corpus("osf4_message_event_string_equivalent.osf"));
        assertThat(messageEventFrames(equivalent))
                .as("the equivalent file encodes the same samples as "
                    + "bcAbsTimeStampData, so the detector must find nothing")
                .isEmpty();
    }

    // ---------------------------------------------------------------
    // The round trip — the one path where read support could leak into
    // write output.
    // ---------------------------------------------------------------

    /**
     * Load the {@code bcMessageEvent} corpus file, write it back out through
     * {@link BlockWriter#fromManager(DataManager)}, and pin what the output
     * carries.
     *
     * <p>Before OSF-UP4 the block was skipped, so this round trip lost the
     * channel silently. Now it decodes into the existing time-stamped
     * representation (§26), and {@code copyChannelData} re-emits it through
     * {@code writeSample(int, long, String)}, i.e. as
     * {@code bcAbsTimeStampData}. Control byte 4 does not survive the round
     * trip, and neither does the loss.
     */
    @Test
    void blockWriterRoundTripEmitsNoControlByteFour(@TempDir Path dir) throws IOException {
        DataManager mgr = DataManager.loadFromFile(corpus("osf4_message_event_string.osf"));
        List<String> before = messageSamples(mgr);
        assertThat(before).as("corpus must decode five samples to re-emit").hasSize(5);

        Path out = dir.resolve("bw-roundtrip.osf");
        BlockWriter.fromManager(mgr).writeToFile(out);
        byte[] bytes = Files.readAllBytes(out);

        assertMessageChannelIsAbsTimestamped(bytes, mgr);

        // Reader level: the content survived. Output with no control byte 4
        // because the channel was dropped would fail here.
        DataManager back = DataManager.loadFromFile(out);
        assertThat(messageSamples(back))
                .as("Demo.Message content across the round trip").isEqualTo(before);
        assertThat(back.stats().blocksSkippedDeprecatedType())
                .as("deprecated block types in round-trip output").isZero();
        assertThat(back.stats().blocksSkippedReservedType())
                .as("reserved/unspecified block shapes in round-trip output").isZero();
    }

    /**
     * The same round trip through {@link StreamingWriter}, which has no
     * {@code fromManager} of its own: the copy is driven sample by sample from
     * the loaded manager, which is how a streaming re-emit would be written by
     * hand. Same expectation — {@code bcAbsTimeStampData}, never control byte 4.
     */
    @Test
    void streamingWriterRoundTripEmitsNoControlByteFour(@TempDir Path dir) throws IOException {
        DataManager mgr = DataManager.loadFromFile(corpus("osf4_message_event_string.osf"));
        List<String> before = messageSamples(mgr);

        Path out = dir.resolve("sw-roundtrip.osf");
        List<DataChannel> chans = mgr.channels();
        try (StreamingWriter w = StreamingWriter.create(out)) {
            // StreamingWriter declares every channel before the first sample
            // (Configure phase), so the copy runs in two passes.
            int[] target = new int[chans.size()];
            for (int c = 0; c < chans.size(); c++) {
                DataChannel dc = chans.get(c);
                target[c] = w.addTimestampedChannel(dc.name(), dc.dataType(), 4);
            }
            for (int c = 0; c < chans.size(); c++) {
                DataChannel dc = chans.get(c);
                long[] ts = dc.timestampsNs();
                if (dc.dataType() == DataType.STRING) {
                    String[] texts = dc.asStrings();
                    for (int i = 0; i < ts.length; i++) {
                        w.writeSample(target[c], ts[i], texts[i]);
                    }
                } else {
                    long[] values = dc.asLongs();
                    for (int i = 0; i < ts.length; i++) {
                        w.writeSample(target[c], ts[i], (int) values[i]);
                    }
                }
            }
        }
        byte[] bytes = Files.readAllBytes(out);

        assertMessageChannelIsAbsTimestamped(bytes, mgr);

        DataManager back = DataManager.loadFromFile(out);
        assertThat(messageSamples(back))
                .as("Demo.Message content across the streaming round trip")
                .isEqualTo(before);
    }

    /**
     * Shared byte-level assertion: no control byte 4 anywhere in {@code bytes},
     * and the {@code Demo.Message} frames specifically are
     * {@code bcAbsTimeStampData} with bit 7 clear (one block per sample — the
     * variable-length paths of both writers are single-sample by construction).
     */
    private static void assertMessageChannelIsAbsTimestamped(byte[] bytes, DataManager source) {
        assertThat(messageEventFrames(bytes))
                .as("control-byte-4 frames in writer output").isEmpty();

        int msgIndex = -1;
        List<DataChannel> chans = source.channels();
        for (int i = 0; i < chans.size(); i++) {
            if ("Demo.Message".equals(chans.get(i).name())) {
                msgIndex = i;
            }
        }
        assertThat(msgIndex).as("Demo.Message present in the source").isNotNegative();

        final int idx = msgIndex;
        List<Frame> msgFrames = frames(bytes).stream().filter(f -> f.channel() == idx).toList();
        assertThat(msgFrames).as("one block per Demo.Message sample").hasSize(5);
        assertThat(msgFrames).allSatisfy(f -> assertThat(f.control())
                .as("Demo.Message re-emitted as bcAbsTimeStampData, bit 7 clear")
                .isEqualTo(CONTROL_ABS_TIMESTAMP));
    }

    // ---------------------------------------------------------------
    // Every writer entry point at once.
    // ---------------------------------------------------------------

    /**
     * Exercise every block-producing entry point of both writers and assert the
     * emitted control bytes come from the closed set
     * {@code {bcContinuedData, bcStartData, bcAbsTimeStampData}}.
     *
     * <p>This is the executable counterpart to the static reading:
     * {@code BlockEncoder} defines exactly three control constants and never
     * names a message-event value, so no writer input can select block type 4.
     * The cases cover the equidistant, timestamped-numeric, GPS, string and
     * binary paths — including the GPS encoder, which the OSF-UP3 audit left
     * with no executable coverage at all.
     */
    @Test
    void noWriterEntryPointEmitsABlockTypeOutsideTheExpectedSet(@TempDir Path dir)
            throws IOException {
        BlockWriter bw = new BlockWriter();
        int eq = bw.addEquidistantChannel("Demo.Eq", DataType.DOUBLE, 2, 1000.0);
        int ts = bw.addTimestampedChannel("Demo.Ts", DataType.DOUBLE, 2);
        int gps = bw.addTimestampedChannel("Demo.Gps", DataType.GPS_LOCATION, 2);
        int str = bw.addTimestampedChannel("Demo.Message", DataType.STRING, 4);
        int bin = bw.addTimestampedChannel("Demo.Blob", DataType.BINARY, 4);
        bw.startEquidistantSegment(eq, 1_000_000_000L, new double[] {1.0, 2.0, 3.0});
        bw.startEquidistantSegment(eq, 2_000_000_000L, new double[] {4.0});
        bw.writeSample(ts, 10L, 0.5);
        bw.writeSample(ts, 20L, 1.5);
        bw.writeSample(gps, 30L, new GpsLocation(47.55, 7.94, 290.0));
        bw.writeSample(str, 40L, "hello");
        bw.writeSample(str, 50L, "");
        bw.writeSample(bin, 60L, new byte[] {0x04, (byte) 0x84});

        ByteArrayOutputStream buf = new ByteArrayOutputStream();
        bw.writeTo(buf);
        assertOnlyExpectedBlockTypes(buf.toByteArray(), "BlockWriter");

        Path swOut = dir.resolve("sw-all-paths.osf");
        try (StreamingWriter sw = StreamingWriter.create(swOut)) {
            int seq = sw.addEquidistantChannel("Demo.Eq", DataType.DOUBLE, 2, 1000.0);
            int sts = sw.addTimestampedChannel("Demo.Ts", DataType.DOUBLE, 2);
            int sgps = sw.addTimestampedChannel("Demo.Gps", DataType.GPS_LOCATION, 2);
            int sstr = sw.addTimestampedChannel("Demo.Message", DataType.STRING, 4);
            int sbin = sw.addTimestampedChannel("Demo.Blob", DataType.BINARY, 4);
            sw.startEquidistantSegment(seq, 1_000_000_000L, new double[] {1.0, 2.0, 3.0});
            sw.startEquidistantSegment(seq, 2_000_000_000L, new double[] {4.0});
            sw.writeSample(sts, 10L, 0.5);
            sw.writeSample(sgps, 30L, new GpsLocation(47.55, 7.94, 290.0));
            sw.writeSample(sstr, 40L, "hello");
            sw.writeSample(sbin, 60L, new byte[] {0x04, (byte) 0x84});
        }
        assertOnlyExpectedBlockTypes(Files.readAllBytes(swOut), "StreamingWriter");
    }

    private static void assertOnlyExpectedBlockTypes(byte[] bytes, String label) {
        List<Frame> fr = frames(bytes);
        assertThat(fr).as("%s emitted blocks", label).isNotEmpty();
        assertThat(fr).allSatisfy(f -> assertThat(f.blockType())
                .as("%s frame on channel %d carries block type 0x%02X - the "
                    + "encoder defines only bcContinuedData (5), bcStartData (6) "
                    + "and bcAbsTimeStampData (8)", label, f.channel(), f.blockType())
                .isIn(0x05, 0x06, CONTROL_ABS_TIMESTAMP));
    }
}
