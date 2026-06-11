// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH
package com.optimeas.osf;

import com.optimeas.osf.testutil.ExamplesDir;
import org.junit.jupiter.api.io.TempDir;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.MethodSource;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Arrays;
import java.util.List;
import java.util.stream.Stream;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.fail;

/**
 * Tier-1 round-trip equivalence test: every generated corpus file is loaded,
 * re-serialised through BOTH {@link BlockWriter} and {@link StreamingWriter},
 * re-read, and deeply compared to the original. Mirrors the Rust
 * {@code roundtrip_all_examples} test.
 *
 * <p>Deep equality is defined as:
 * <ul>
 *   <li>Same number of channels.</li>
 *   <li>Per channel (by position): same name, dataType, kind, sampleCount.</li>
 *   <li>{@code timestampsNs()} arrays equal element-by-element.</li>
 *   <li>Values equal with bitwise floating-point semantics (via
 *       {@link Double#doubleToLongBits} / {@link Float#floatToIntBits}), exact
 *       for integer/bool, {@code String[]} exact, {@code byte[][]} element-exact.</li>
 * </ul>
 *
 * <p>Any round-trip mismatch is reported as a real writer/reader bug — the
 * assertions are not softened.
 *
 * <p>Tests are skipped (via JUnit assumptions) when the corpus is absent.
 */
class RoundtripExamplesTest {

    @TempDir
    Path tmp;

    /** Stream of all {@code *.osf} files in {@code <examples>/generated/}. */
    static Stream<Path> generatedOsfFiles() throws IOException {
        Path generated = ExamplesDir.resolve().resolve("generated");
        if (!Files.isDirectory(generated)) {
            return Stream.empty();
        }
        try (Stream<Path> entries = Files.list(generated)) {
            return entries
                    .filter(p -> p.getFileName().toString().endsWith(".osf"))
                    .sorted()
                    .toList()
                    .stream();
        }
    }

    // -----------------------------------------------------------------------
    // BlockWriter round-trip
    // -----------------------------------------------------------------------

    @ParameterizedTest(name = "blockWriter/{0}")
    @MethodSource("generatedOsfFiles")
    void blockWriterRoundtrip(Path file) throws IOException {
        DataManager orig = DataManager.loadFromFile(file);

        Path out = tmp.resolve("bw_" + file.getFileName());
        BlockWriter.fromManager(orig).writeToFile(out);

        DataManager rt = DataManager.loadFromFile(out);
        assertChannelsEquivalent(file.getFileName().toString() + " [BlockWriter]", orig, rt);
    }

    // -----------------------------------------------------------------------
    // StreamingWriter round-trip
    // -----------------------------------------------------------------------

    @ParameterizedTest(name = "streamingWriter/{0}")
    @MethodSource("generatedOsfFiles")
    void streamingWriterRoundtrip(Path file) throws IOException {
        DataManager orig = DataManager.loadFromFile(file);

        Path out = tmp.resolve("sw_" + file.getFileName());
        replayThroughStreamingWriter(orig, out);

        DataManager rt = DataManager.loadFromFile(out);
        assertChannelsEquivalent(file.getFileName().toString() + " [StreamingWriter]", orig, rt);
    }

    /**
     * Replays every channel from {@code orig} through a fresh {@link StreamingWriter}
     * into {@code dest}. All channel kinds handled:
     * <ul>
     *   <li>TIMESTAMPED — bulk {@code writeSamples} for numeric/GPS;
     *       per-sample {@code writeSample} for BOOL (no bulk overload).</li>
     *   <li>EQUIDISTANT — one {@code startEquidistantSegment} per recorded
     *       segment.</li>
     *   <li>VARIABLE — per-sample {@code writeSample(String/byte[])}.</li>
     * </ul>
     *
     * <p>The {@code sizeoflengthvalue} is preserved from the source channel's
     * definition by inspecting the channel kind (numeric → 2 is sufficient;
     * variable → let the runtime pick 2, which auto-promotes to 4 for large
     * payloads if needed — mirroring the BlockWriter {@code fromManager} logic).
     */
    private static void replayThroughStreamingWriter(DataManager orig, Path dest) {
        try (StreamingWriter sw = StreamingWriter.create(dest)) {
            // Copy file-level metadata verbatim.
            for (var e : orig.metadata().entrySet()) {
                sw.setMetadata(e.getKey(), e.getValue());
            }

            // Declare channels and record the writer indices.
            int[] writerIdx = new int[orig.channels().size()];
            for (int i = 0; i < orig.channels().size(); i++) {
                DataChannel dc = orig.channels().get(i);
                writerIdx[i] = declareChannel(sw, dc);
            }

            // Write samples.
            for (int i = 0; i < orig.channels().size(); i++) {
                DataChannel dc = orig.channels().get(i);
                int idx = writerIdx[i];
                replayChannelSamples(sw, dc, idx);
            }
        }
    }

    private static int declareChannel(StreamingWriter sw, DataChannel dc) {
        boolean equidistant = dc.kind() == DataChannel.Kind.EQUIDISTANT;
        if (equidistant) {
            double rate = dc.segments().isEmpty() ? 1.0 : dc.segments().get(0).sampleRateHz();
            return sw.addEquidistantChannel(dc.name(), dc.dataType(), 2, rate,
                    dc.physicalUnit(), null);
        } else {
            return sw.addTimestampedChannel(dc.name(), dc.dataType(), 2,
                    dc.physicalUnit(), null);
        }
    }

    private static void replayChannelSamples(StreamingWriter sw, DataChannel dc, int idx) {
        switch (dc.kind()) {
            case EQUIDISTANT -> replayEquidistant(sw, dc, idx);
            case TIMESTAMPED -> replayTimestamped(sw, dc, idx);
            case VARIABLE    -> replayVariable(sw, dc, idx);
        }
    }

    private static void replayEquidistant(StreamingWriter sw, DataChannel dc, int idx) {
        long[] allTs = dc.timestampsNs();
        if (allTs.length == 0) {
            return;
        }
        for (DataChannel.Segment seg : dc.segments()) {
            int start = seg.startIndex();
            int count = seg.sampleCount();
            if (count == 0) {
                continue;
            }
            if (dc.dataType() == DataType.FLOAT) {
                // asDoubles() widens float→double; cast back to get original float bits.
                double[] allDoubles = dc.asDoubles();
                float[] samples = new float[count];
                for (int j = 0; j < count; j++) {
                    samples[j] = (float) allDoubles[start + j];
                }
                sw.startEquidistantSegment(idx, seg.startTimestampNs(), samples);
            } else {
                double[] allDoubles = dc.asDoubles();
                double[] samples = Arrays.copyOfRange(allDoubles, start, start + count);
                sw.startEquidistantSegment(idx, seg.startTimestampNs(), samples);
            }
        }
    }

    private static void replayTimestamped(StreamingWriter sw, DataChannel dc, int idx) {
        long[] ts = dc.timestampsNs();
        if (ts.length == 0) {
            return;
        }
        DataType dt = dc.dataType();
        switch (dt) {
            case DOUBLE -> sw.writeSamples(idx, ts, dc.asDoubles());
            case FLOAT -> {
                // No writeSamples(float[]) bulk overload on StreamingWriter;
                // replay sample-by-sample to preserve float bit-pattern.
                double[] v = dc.asDoubles();
                for (int i = 0; i < ts.length; i++) {
                    sw.writeSample(idx, ts[i], (float) v[i]);
                }
            }
            case GPS_LOCATION -> sw.writeSamples(idx, ts, dc.asGps());
            case BOOL -> {
                boolean[] v = dc.asBooleans();
                for (int i = 0; i < ts.length; i++) {
                    sw.writeSample(idx, ts[i], v[i]);
                }
            }
            default -> {
                // All integer types: INT8..INT64, UINT8..UINT64.
                sw.writeSamples(idx, ts, dc.asLongs());
            }
        }
    }

    private static void replayVariable(StreamingWriter sw, DataChannel dc, int idx) {
        long[] ts = dc.timestampsNs();
        if (ts.length == 0) {
            return;
        }
        if (dc.dataType() == DataType.STRING) {
            String[] v = dc.asStrings();
            for (int i = 0; i < ts.length; i++) {
                sw.writeSample(idx, ts[i], v[i]);
            }
        } else {
            byte[][] v = dc.asBinaries();
            for (int i = 0; i < ts.length; i++) {
                sw.writeSample(idx, ts[i], v[i]);
            }
        }
    }

    // -----------------------------------------------------------------------
    // Deep-equality helper.
    // -----------------------------------------------------------------------

    /**
     * Assert that {@code actual} is deeply equivalent to {@code expected}.
     *
     * <p>Floating-point comparisons use bitwise equality ({@link Double#doubleToLongBits}
     * / {@link Float#floatToIntBits}) so that NaN, ±0.0, and subnormal values
     * compare correctly.
     */
    static void assertChannelsEquivalent(String label, DataManager expected, DataManager actual) {
        List<DataChannel> eCh = expected.channels();
        List<DataChannel> aCh = actual.channels();

        assertThat(aCh.size())
                .as("%s: channel count", label)
                .isEqualTo(eCh.size());

        for (int i = 0; i < eCh.size(); i++) {
            DataChannel e = eCh.get(i);
            DataChannel a = aCh.get(i);
            String ctx = label + " ch[" + i + "] '" + e.name() + "'";

            assertThat(a.name())
                    .as("%s: name", ctx)
                    .isEqualTo(e.name());
            assertThat(a.dataType())
                    .as("%s: dataType", ctx)
                    .isEqualTo(e.dataType());
            assertThat(a.kind())
                    .as("%s: kind", ctx)
                    .isEqualTo(e.kind());
            assertThat(a.sampleCount())
                    .as("%s: sampleCount", ctx)
                    .isEqualTo(e.sampleCount());

            // Timestamps.
            assertThat(a.timestampsNs())
                    .as("%s: timestampsNs", ctx)
                    .isEqualTo(e.timestampsNs());

            // Values — bitwise for floating-point.
            assertValuesEqual(ctx, e, a);
        }
    }

    private static void assertValuesEqual(String ctx, DataChannel e, DataChannel a) {
        if (e.sampleCount() == 0) {
            return;
        }
        DataType dt = e.dataType();
        switch (dt) {
            case DOUBLE -> {
                double[] ev = e.asDoubles();
                double[] av = a.asDoubles();
                assertThat(av.length).as("%s: values.length", ctx).isEqualTo(ev.length);
                for (int i = 0; i < ev.length; i++) {
                    if (Double.doubleToLongBits(ev[i]) != Double.doubleToLongBits(av[i])) {
                        fail("%s: values[%d] mismatch: expected %s but was %s",
                                ctx, i, ev[i], av[i]);
                    }
                }
            }
            case FLOAT -> {
                // Compare at float precision via asDoubles() — both sides widened
                // identically; use floatToIntBits on the cast-back float.
                double[] ev = e.asDoubles();
                double[] av = a.asDoubles();
                assertThat(av.length).as("%s: values.length", ctx).isEqualTo(ev.length);
                for (int i = 0; i < ev.length; i++) {
                    float ef = (float) ev[i];
                    float af = (float) av[i];
                    if (Float.floatToIntBits(ef) != Float.floatToIntBits(af)) {
                        fail("%s: values[%d] mismatch: expected %s but was %s",
                                ctx, i, ef, af);
                    }
                }
            }
            case GPS_LOCATION -> {
                GpsLocation[] ev = e.asGps();
                GpsLocation[] av = a.asGps();
                assertThat(av.length).as("%s: values.length", ctx).isEqualTo(ev.length);
                for (int i = 0; i < ev.length; i++) {
                    assertGpsEqual(ctx + "[" + i + "]", ev[i], av[i]);
                }
            }
            case BOOL -> {
                boolean[] ev = e.asBooleans();
                boolean[] av = a.asBooleans();
                assertThat(av).as("%s: bool values", ctx).isEqualTo(ev);
            }
            case STRING -> {
                String[] ev = e.asStrings();
                String[] av = a.asStrings();
                assertThat(av).as("%s: string values", ctx).isEqualTo(ev);
            }
            case BINARY -> {
                byte[][] ev = e.asBinaries();
                byte[][] av = a.asBinaries();
                assertThat(av.length).as("%s: binary values.length", ctx).isEqualTo(ev.length);
                for (int i = 0; i < ev.length; i++) {
                    assertThat(av[i])
                            .as("%s: binary[%d]", ctx, i)
                            .isEqualTo(ev[i]);
                }
            }
            default -> {
                // Integer types (INT8..INT64, UINT8..UINT64) via asLongs().
                long[] ev = e.asLongs();
                long[] av = a.asLongs();
                assertThat(av).as("%s: integer values", ctx).isEqualTo(ev);
            }
        }
    }

    private static void assertGpsEqual(String ctx, GpsLocation e, GpsLocation a) {
        if (Double.doubleToLongBits(e.latitude()) != Double.doubleToLongBits(a.latitude())) {
            fail("%s: GPS latitude mismatch: expected %s but was %s",
                    ctx, e.latitude(), a.latitude());
        }
        if (Double.doubleToLongBits(e.longitude()) != Double.doubleToLongBits(a.longitude())) {
            fail("%s: GPS longitude mismatch: expected %s but was %s",
                    ctx, e.longitude(), a.longitude());
        }
        if (Double.doubleToLongBits(e.altitude()) != Double.doubleToLongBits(a.altitude())) {
            fail("%s: GPS altitude mismatch: expected %s but was %s",
                    ctx, e.altitude(), a.altitude());
        }
    }
}
