// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH
package com.optimeas.osf;

import com.optimeas.osf.internal.Block;
import com.optimeas.osf.internal.BlockEncoder;
import com.optimeas.osf.internal.MetablockBuilder;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.OutputStream;
import java.nio.file.Files;
import java.nio.file.Path;
import java.time.Instant;
import java.time.ZoneOffset;
import java.time.format.DateTimeFormatter;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/**
 * In-memory, accumulating OSF5 writer — the block-mode counterpart to the
 * power-loss-safe {@link StreamingWriter}.
 *
 * <p>{@code BlockWriter} collects every channel's samples in memory and emits
 * the whole file in a single pass at {@link #writeToFile(Path)} /
 * {@link #writeTo(OutputStream)}: the magic-header line ({@code OSF5 <len>\n}),
 * the JSON metablock (built via {@link MetablockBuilder#buildOsf5Json}), and
 * then, per channel, the data blocks (encoded via {@link BlockEncoder}).
 *
 * <p>This is a faithful port of the reference block writers
 * {@code implementations/cpp/src/block_writer.cpp} and
 * {@code implementations/rust/osf-core/src/writer.rs}:
 * <ul>
 *   <li><b>Multi-sample chunking.</b> Because the whole channel is known up
 *       front, timestamped-numeric and equidistant runs are packed into
 *       multi-sample blocks (control bit&nbsp;7 set + {@code u32 N} prefix),
 *       split at the reference's {@code max_samples_per_*_block} granularity so
 *       no block overflows its length field. String / binary samples are still
 *       written one sample per block (spec: variable payloads are not batched).</li>
 *   <li><b>{@code sizeoflengthvalue} auto-bump.</b> A channel declared with the
 *       no-width {@link #addTimestampedChannel(String, DataType)} form starts at
 *       2 and is promoted to 4 if any variable sample would overflow the 2-byte
 *       length field. The explicit-width overload honours the caller's choice
 *       and rejects an overflowing sample instead (matching the StreamingWriter,
 *       which cannot bump). Numeric channels are never bumped — they are split
 *       into more blocks instead.</li>
 *   <li><b>{@link #fromManager(DataManager)}.</b> Reconstructs a writer from a
 *       loaded file's channels + samples, for round-trip / copy. Always emits
 *       OSF5 regardless of the source format (DECISIONS §6).</li>
 * </ul>
 *
 * <p>Equidistant channels accept {@code float} / {@code double} only (spec rev
 * 2026-05-04).
 *
 * <h2>Relationship to {@link StreamingWriter} byte output</h2>
 * The two writers are byte-identical only when every channel carries exactly one
 * sample (then {@code count == 1}, the multi-sample flag is clear and the
 * {@code N} prefix is omitted, so a BlockWriter block coincides with a
 * StreamingWriter single-sample block). For multi-sample data the metablock
 * preambles still match (same {@link MetablockBuilder}, same defs / metadata),
 * but the block streams differ in granularity. See {@code WriterIdentityTest}.
 */
public final class BlockWriter {

    private static final DateTimeFormatter CREATED_UTC =
            DateTimeFormatter.ofPattern("yyyy-MM-dd'T'HH:mm:ss'Z'").withZone(ZoneOffset.UTC);

    /** Largest payload (control byte + body) that fits a 2-byte length field. */
    private static final int MAX_PAYLOAD_U16 = 0xFFFF;
    /**
     * Soft cap for the 4-byte length field — a single ~2&nbsp;GB block is
     * already enormous; pinning it just below {@code Integer.MAX_VALUE} avoids
     * overflow on the body-length conversion. Mirrors the Rust
     * {@code MAX_BLOCK_PAYLOAD_U32}.
     */
    private static final int MAX_PAYLOAD_U32 = Integer.MAX_VALUE - 1024;

    /** Block-family lock per channel, mirroring the reference's kind lock. */
    private enum Kind { UNSET, TIMESTAMPED, EQUIDISTANT, VARIABLE }

    /** One accumulated equidistant segment (float / double only). */
    private static final class EqSegment {
        final long startTimestampNs;
        final double sampleRateHz;
        final Block.Values values;
        EqSegment(long startTimestampNs, double sampleRateHz, Block.Values values) {
            this.startTimestampNs = startTimestampNs;
            this.sampleRateHz = sampleRateHz;
            this.values = values;
        }
    }

    /** Per-channel accumulator. The active fields depend on {@link #kind}. */
    private static final class Chan {
        final ChannelDef def;
        /** Whether the caller fixed sizeoflengthvalue (explicit overload). */
        final boolean widthPinned;
        Kind kind = Kind.UNSET;
        boolean segmentOpen = false;

        // Equidistant storage.
        final List<EqSegment> segments = new ArrayList<>();

        // Timestamped numeric / GPS storage (parallel timestamps + flat values).
        final List<Long> timestamps = new ArrayList<>();
        Block.Values numericValues; // grown via append; one concrete variant
        final List<GpsLocation> gps = new ArrayList<>();

        // Variable storage.
        final List<String> strings = new ArrayList<>();
        final List<byte[]> binaries = new ArrayList<>();

        Chan(ChannelDef def, boolean widthPinned) {
            this.def = def;
            this.widthPinned = widthPinned;
        }
    }

    private final Map<String, String> metadata = new LinkedHashMap<>();
    private final List<Chan> channels = new ArrayList<>();
    private final Map<String, Integer> nameToIndex = new LinkedHashMap<>();
    private final Map<Integer, Double> rateByIndex = new LinkedHashMap<>();

    /** Create an empty writer with no channels and no metadata. */
    public BlockWriter() {}

    // ---------------------------------------------------------------
    // File-level metadata.
    // ---------------------------------------------------------------

    /**
     * Set a file-level metadata entry written verbatim into the metablock
     * {@code osf.file} object (e.g. {@code "creator"}, {@code "tag"}, or a pinned
     * {@code "created_utc"}). {@code created_utc} is injected at write time only
     * if not already present (same {@code putIfAbsent} rule as
     * {@link StreamingWriter}).
     *
     * @param key   wire field name
     * @param value the value
     */
    public void setMetadata(String key, String value) {
        metadata.put(key, value);
    }

    // ---------------------------------------------------------------
    // Channel declaration.
    // ---------------------------------------------------------------

    /**
     * Declare a timestamped channel, letting the writer choose
     * {@code sizeoflengthvalue} (starts at 2, auto-bumps to 4 if a variable
     * sample would overflow).
     *
     * @param name fully-qualified channel name
     * @param type the channel's data type
     * @return the channel index for use in {@code writeSample}
     */
    public int addTimestampedChannel(String name, DataType type) {
        return addTimestampedChannelInternal(name, type, 2, false);
    }

    /**
     * Declare a timestamped channel with an explicit {@code sizeoflengthvalue}.
     * The width is honoured as-is (no auto-bump); an overflowing sample is
     * rejected at write time.
     *
     * @param name              fully-qualified channel name
     * @param type              the channel's data type
     * @param sizeOfLengthValue length-prefix width on disk; 2 or 4
     * @return the channel index
     */
    public int addTimestampedChannel(String name, DataType type, int sizeOfLengthValue) {
        return addTimestampedChannelInternal(name, type, sizeOfLengthValue, true);
    }

    private int addTimestampedChannelInternal(String name, DataType type,
                                              int sizeOfLengthValue, boolean widthPinned) {
        validateChannel(name, type, sizeOfLengthValue);
        ChannelDef def = new ChannelDef(channels.size(), name, type, ChannelType.SCALAR,
                sizeOfLengthValue, 0L, null, Map.of());
        registerChannel(def, widthPinned);
        return def.index();
    }

    /**
     * Declare an equidistant channel (samples spaced at a fixed rate; only the
     * segment start carries a timestamp). Equidistant channels accept
     * {@code float} / {@code double} only (spec rev 2026-05-04). The width is
     * pinned (numeric channels never auto-bump — they split into more blocks).
     *
     * @param name              fully-qualified channel name
     * @param type              {@link DataType#FLOAT} or {@link DataType#DOUBLE}
     * @param sizeOfLengthValue length-prefix width on disk; 2 or 4
     * @param sampleRateHz      sample rate in Hz; positive finite
     * @return the channel index
     */
    public int addEquidistantChannel(String name, DataType type, int sizeOfLengthValue,
                                     double sampleRateHz) {
        validateChannel(name, type, sizeOfLengthValue);
        if (type != DataType.FLOAT && type != DataType.DOUBLE) {
            throw new IllegalArgumentException(
                    "equidistant channels support only float and double "
                    + "(spec rev 2026-05-04), got " + type);
        }
        if (!(sampleRateHz > 0.0) || Double.isInfinite(sampleRateHz) || Double.isNaN(sampleRateHz)) {
            throw new IllegalArgumentException(
                    "equidistant sampleRateHz must be a positive finite value, got " + sampleRateHz);
        }
        long timeIncrementNs = Math.max(1L, Math.round(1.0e9 / sampleRateHz));
        ChannelDef def = new ChannelDef(channels.size(), name, type, ChannelType.EQUIDISTANT,
                sizeOfLengthValue, timeIncrementNs, null, Map.of());
        registerChannel(def, true);
        rateByIndex.put(def.index(), sampleRateHz);
        return def.index();
    }

    /**
     * Declare a channel directly from a {@link ChannelDef} (used by
     * {@link #fromManager(DataManager)} to preserve physical unit, attributes and
     * the source {@code sizeoflengthvalue}).
     */
    private int addChannel(ChannelDef def, boolean equidistant, double sampleRateHz) {
        validateChannel(def.name(), def.dataType(), def.sizeOfLengthValue());
        registerChannel(def, true);
        if (equidistant) {
            rateByIndex.put(def.index(), sampleRateHz);
        }
        return def.index();
    }

    private void registerChannel(ChannelDef def, boolean widthPinned) {
        nameToIndex.put(def.name(), def.index());
        channels.add(new Chan(def, widthPinned));
    }

    /** Number of channels declared so far. */
    public int channelCount() {
        return channels.size();
    }

    /** Resolve a channel index by name, or {@code -1} if unknown. */
    public int channelIndex(String name) {
        Integer i = nameToIndex.get(name);
        return (i == null) ? -1 : i;
    }

    // ---------------------------------------------------------------
    // writeSample — timestamped overloads (accumulate; no I/O yet).
    // ---------------------------------------------------------------

    /** Accumulate one timestamped {@code double} sample. */
    public void writeSample(int channelIndex, long timestampNs, double value) {
        Chan c = requireTimestamped(channelIndex, DataType.DOUBLE);
        c.timestamps.add(timestampNs);
        appendDouble(c, value);
    }

    /** Accumulate one timestamped {@code float} sample. */
    public void writeSample(int channelIndex, long timestampNs, float value) {
        Chan c = requireTimestamped(channelIndex, DataType.FLOAT);
        c.timestamps.add(timestampNs);
        appendFloat(c, value);
    }

    /** Accumulate one timestamped {@code boolean} sample. */
    public void writeSample(int channelIndex, long timestampNs, boolean value) {
        Chan c = requireTimestamped(channelIndex, DataType.BOOL);
        c.timestamps.add(timestampNs);
        appendBool(c, value);
    }

    /**
     * Accumulate one timestamped integer sample. The channel must be one of the
     * integer data types ({@code int8..int64}, {@code uint8..uint64}); the value
     * is narrowed to the channel's width on encode.
     */
    public void writeSample(int channelIndex, long timestampNs, long value) {
        Chan c = require(channelIndex);
        DataType dt = c.def.dataType();
        if (!isInteger(dt)) {
            throw new OsfException("channel " + channelIndex
                    + ": writeSample(long) requires an integer data type, channel is " + dt);
        }
        lockTimestamped(c, dt);
        c.timestamps.add(timestampNs);
        appendInteger(c, dt, value);
    }

    /** Accumulate one timestamped {@code int} sample (widens to {@code long}). */
    public void writeSample(int channelIndex, long timestampNs, int value) {
        writeSample(channelIndex, timestampNs, (long) value);
    }

    /** Accumulate one timestamped GPS sample. */
    public void writeSample(int channelIndex, long timestampNs, GpsLocation value) {
        Chan c = requireTimestamped(channelIndex, DataType.GPS_LOCATION);
        c.timestamps.add(timestampNs);
        c.gps.add(value);
    }

    /** Accumulate one timestamped {@code string} sample. */
    public void writeSample(int channelIndex, long timestampNs, String value) {
        Chan c = require(channelIndex);
        if (c.def.dataType() != DataType.STRING) {
            throw new OsfException("channel " + channelIndex
                    + ": writeSample(String) requires a string channel, channel is "
                    + c.def.dataType());
        }
        lockVariable(c);
        c.timestamps.add(timestampNs);
        c.strings.add(value);
    }

    /** Accumulate one timestamped {@code binary} sample. */
    public void writeSample(int channelIndex, long timestampNs, byte[] value) {
        Chan c = require(channelIndex);
        if (c.def.dataType() != DataType.BINARY) {
            throw new OsfException("channel " + channelIndex
                    + ": writeSample(byte[]) requires a binary channel, channel is "
                    + c.def.dataType());
        }
        lockVariable(c);
        c.timestamps.add(timestampNs);
        c.binaries.add(value.clone());
    }

    // ---------------------------------------------------------------
    // Equidistant accumulation.
    // ---------------------------------------------------------------

    /** Accumulate an equidistant segment of {@code double} samples. */
    public void startEquidistantSegment(int channelIndex, long startNs, double[] samples) {
        Chan c = require(channelIndex);
        if (c.def.dataType() != DataType.DOUBLE) {
            throw new OsfException("channel " + channelIndex
                    + ": startEquidistantSegment(double[]) requires a double channel, channel is "
                    + c.def.dataType());
        }
        lockEquidistant(c);
        double rate = rateByIndex.getOrDefault(channelIndex, 0.0);
        c.segments.add(new EqSegment(startNs, rate, new Block.DoubleValues(samples.clone())));
        c.segmentOpen = true;
    }

    /** Accumulate an equidistant segment of {@code float} samples. */
    public void startEquidistantSegment(int channelIndex, long startNs, float[] samples) {
        Chan c = require(channelIndex);
        if (c.def.dataType() != DataType.FLOAT) {
            throw new OsfException("channel " + channelIndex
                    + ": startEquidistantSegment(float[]) requires a float channel, channel is "
                    + c.def.dataType());
        }
        lockEquidistant(c);
        double rate = rateByIndex.getOrDefault(channelIndex, 0.0);
        c.segments.add(new EqSegment(startNs, rate, new Block.FloatValues(samples.clone())));
        c.segmentOpen = true;
    }

    /** Append {@code double} samples to the channel's open equidistant segment. */
    public void appendEquidistantSamples(int channelIndex, double[] samples) {
        Chan c = require(channelIndex);
        lockEquidistant(c);
        if (!c.segmentOpen || c.segments.isEmpty()) {
            throw new OsfException("channel " + channelIndex
                    + ": appendEquidistantSamples without an open segment "
                    + "(call startEquidistantSegment first)");
        }
        EqSegment last = c.segments.get(c.segments.size() - 1);
        if (!(last.values instanceof Block.DoubleValues dv)) {
            throw new OsfException("channel " + channelIndex
                    + ": appendEquidistantSamples(double[]) on a non-double segment");
        }
        double[] merged = concat(dv.values(), samples);
        c.segments.set(c.segments.size() - 1,
                new EqSegment(last.startTimestampNs, last.sampleRateHz,
                        new Block.DoubleValues(merged)));
    }

    /** Append {@code float} samples to the channel's open equidistant segment. */
    public void appendEquidistantSamples(int channelIndex, float[] samples) {
        Chan c = require(channelIndex);
        lockEquidistant(c);
        if (!c.segmentOpen || c.segments.isEmpty()) {
            throw new OsfException("channel " + channelIndex
                    + ": appendEquidistantSamples without an open segment "
                    + "(call startEquidistantSegment first)");
        }
        EqSegment last = c.segments.get(c.segments.size() - 1);
        if (!(last.values instanceof Block.FloatValues fv)) {
            throw new OsfException("channel " + channelIndex
                    + ": appendEquidistantSamples(float[]) on a non-float segment");
        }
        float[] merged = concat(fv.values(), samples);
        c.segments.set(c.segments.size() - 1,
                new EqSegment(last.startTimestampNs, last.sampleRateHz,
                        new Block.FloatValues(merged)));
    }

    // ---------------------------------------------------------------
    // write.
    // ---------------------------------------------------------------

    /**
     * Serialise the whole OSF5 file to {@code path} in one pass.
     *
     * @param path target file
     * @throws OsfException if no channels were declared or on I/O failure
     */
    public void writeToFile(Path path) {
        try (OutputStream out = Files.newOutputStream(path)) {
            writeTo(out);
        } catch (IOException e) {
            throw new OsfException("failed to write OSF file " + path + ": " + e.getMessage(), e);
        }
    }

    /**
     * Serialise the whole OSF5 file to an arbitrary stream in one pass: the
     * magic-header line, the metablock (with {@code created_utc} injected), then
     * every channel's data blocks.
     *
     * @param out the sink
     * @throws OsfException if no channels were declared or on I/O failure
     */
    public void writeTo(OutputStream out) {
        if (channels.isEmpty()) {
            throw new OsfException("writeTo: no channels declared");
        }
        metadata.putIfAbsent("created_utc", CREATED_UTC.format(Instant.now()));

        // Resolve each channel's effective sizeoflengthvalue (auto-bump pass).
        List<ChannelDef> defs = new ArrayList<>(channels.size());
        int[] sov = new int[channels.size()];
        for (int i = 0; i < channels.size(); i++) {
            Chan c = channels.get(i);
            sov[i] = effectiveSizeOfLengthValue(c);
            defs.add(withSize(c.def, sov[i]));
        }

        byte[] metablock = MetablockBuilder.buildOsf5Json(5, metadata, defs);
        byte[] magic = ("OSF5 " + metablock.length + "\n")
                .getBytes(java.nio.charset.StandardCharsets.US_ASCII);

        try {
            out.write(magic);
            out.write(metablock);
            for (int i = 0; i < channels.size(); i++) {
                emitChannel(out, channels.get(i), sov[i]);
            }
            out.flush();
        } catch (IOException e) {
            throw new OsfException("OSF write failed: " + e.getMessage(), e);
        }
    }

    // ---------------------------------------------------------------
    // fromManager.
    // ---------------------------------------------------------------

    /**
     * Reconstruct a writer from a loaded {@link DataManager} — copying its
     * file-level metadata, channel definitions and every sample — for round-trip
     * or copy. Always emits OSF5 (DECISIONS §6).
     *
     * @param mgr a loaded manager
     * @return a writer pre-populated with {@code mgr}'s content
     */
    public static BlockWriter fromManager(DataManager mgr) {
        BlockWriter w = new BlockWriter();
        // Copy file-level metadata verbatim (created_utc included; it is already
        // present in a loaded file, so the putIfAbsent at write time is a no-op).
        for (Map.Entry<String, String> e : mgr.metadata().entrySet()) {
            w.metadata.put(e.getKey(), e.getValue());
        }

        for (DataChannel dc : mgr.channels()) {
            boolean equidistant = dc.kind() == DataChannel.Kind.EQUIDISTANT;
            double rate = equidistant && !dc.segments().isEmpty()
                    ? dc.segments().get(0).sampleRateHz() : 0.0;
            ChannelDef def = channelDefFrom(dc, w.channels.size());
            int idx = w.addChannel(def, equidistant, rate);
            copyChannelData(w, dc, idx);
        }
        return w;
    }

    private static ChannelDef channelDefFrom(DataChannel dc, int index) {
        ChannelType ct = (dc.kind() == DataChannel.Kind.EQUIDISTANT)
                ? ChannelType.EQUIDISTANT : ChannelType.SCALAR;
        // sizeoflengthvalue is not exposed on DataChannel; start at 2 and let the
        // write-time auto-bump promote variable channels as needed.
        return new ChannelDef(index, dc.name(), dc.dataType(), ct, 2, 0L,
                dc.physicalUnit(), Map.of());
    }

    private static void copyChannelData(BlockWriter w, DataChannel dc, int idx) {
        switch (dc.kind()) {
            case EQUIDISTANT -> {
                long[] ts = dc.timestampsNs();
                for (DataChannel.Segment seg : dc.segments()) {
                    int start = seg.startIndex();
                    int count = seg.sampleCount();
                    if (count == 0) {
                        continue;
                    }
                    if (dc.dataType() == DataType.FLOAT) {
                        float[] all = toFloatArray(dc.asDoubles());
                        w.channels.get(idx).segments.add(new EqSegment(
                                seg.startTimestampNs(), seg.sampleRateHz(),
                                new Block.FloatValues(java.util.Arrays.copyOfRange(all, start, start + count))));
                    } else {
                        double[] all = dc.asDoubles();
                        w.channels.get(idx).segments.add(new EqSegment(
                                seg.startTimestampNs(), seg.sampleRateHz(),
                                new Block.DoubleValues(java.util.Arrays.copyOfRange(all, start, start + count))));
                    }
                    w.channels.get(idx).kind = Kind.EQUIDISTANT;
                    w.channels.get(idx).segmentOpen = true;
                }
                // Defensive: a loaded equidistant channel with no segments but
                // values is not expected; nothing to copy then.
                if (ts.length == 0) {
                    return;
                }
            }
            case TIMESTAMPED -> {
                long[] ts = dc.timestampsNs();
                DataType dt = dc.dataType();
                if (dt == DataType.GPS_LOCATION) {
                    GpsLocation[] g = dc.asGps();
                    for (int i = 0; i < ts.length; i++) {
                        w.writeSample(idx, ts[i], g[i]);
                    }
                } else if (dt == DataType.BOOL) {
                    boolean[] v = dc.asBooleans();
                    for (int i = 0; i < ts.length; i++) {
                        w.writeSample(idx, ts[i], v[i]);
                    }
                } else if (dt == DataType.FLOAT) {
                    double[] v = dc.asDoubles();
                    for (int i = 0; i < ts.length; i++) {
                        w.writeSample(idx, ts[i], (float) v[i]);
                    }
                } else if (dt == DataType.DOUBLE) {
                    double[] v = dc.asDoubles();
                    for (int i = 0; i < ts.length; i++) {
                        w.writeSample(idx, ts[i], v[i]);
                    }
                } else if (isInteger(dt)) {
                    long[] v = dc.asLongs();
                    for (int i = 0; i < ts.length; i++) {
                        w.writeSample(idx, ts[i], v[i]);
                    }
                } else {
                    throw new OsfException("fromManager: channel " + dc.name()
                            + " has unsupported timestamped data type " + dt);
                }
            }
            case VARIABLE -> {
                long[] ts = dc.timestampsNs();
                if (dc.dataType() == DataType.STRING) {
                    String[] v = dc.asStrings();
                    for (int i = 0; i < ts.length; i++) {
                        w.writeSample(idx, ts[i], v[i]);
                    }
                } else {
                    byte[][] v = dc.asBinaries();
                    for (int i = 0; i < ts.length; i++) {
                        w.writeSample(idx, ts[i], v[i]);
                    }
                }
            }
            default -> throw new OsfException("fromManager: unknown channel kind " + dc.kind());
        }
    }

    // ---------------------------------------------------------------
    // Emission (chunked multi-sample blocks).
    // ---------------------------------------------------------------

    private void emitChannel(OutputStream out, Chan c, int sov) throws IOException {
        switch (c.kind) {
            case EQUIDISTANT -> emitEquidistant(out, c, sov);
            case TIMESTAMPED -> emitTimestamped(out, c, sov);
            case VARIABLE -> emitVariable(out, c, sov);
            case UNSET -> { /* declared channel with no samples — emit nothing */ }
            default -> throw new OsfException("channel " + c.def.index() + ": unknown kind " + c.kind);
        }
    }

    private void emitEquidistant(OutputStream out, Chan c, int sov) throws IOException {
        int idx = c.def.index();
        for (EqSegment seg : c.segments) {
            int valueSize = numericValueSize(seg.values);
            int total = seg.values.length();
            int maxStart = Math.max(1, (maxPayload(sov) - (1 + 8 + 8 + 4)) / valueSize);
            int maxCont = Math.max(1, (maxPayload(sov) - (1 + 4)) / valueSize);

            int first = Math.min(total, maxStart);
            out.write(BlockEncoder.startDataBlock(idx, seg.startTimestampNs, seg.sampleRateHz,
                    slice(seg.values, 0, first), sov));
            int written = first;
            while (written < total) {
                int chunk = Math.min(total - written, maxCont);
                out.write(BlockEncoder.continuedDataBlock(idx,
                        slice(seg.values, written, chunk), sov));
                written += chunk;
            }
        }
    }

    private void emitTimestamped(OutputStream out, Chan c, int sov) throws IOException {
        int idx = c.def.index();
        long[] ts = toLongArray(c.timestamps);
        if (c.def.dataType() == DataType.GPS_LOCATION) {
            int total = c.gps.size();
            int perSample = 8 + 24;
            int maxPer = Math.max(1, (maxPayload(sov) - (1 + 4)) / perSample);
            GpsLocation[] all = c.gps.toArray(new GpsLocation[0]);
            int written = 0;
            while (written < total) {
                int chunk = Math.min(total - written, maxPer);
                long[] tsChunk = java.util.Arrays.copyOfRange(ts, written, written + chunk);
                GpsLocation[] vChunk = java.util.Arrays.copyOfRange(all, written, written + chunk);
                out.write(BlockEncoder.timestampedGpsBlock(idx, tsChunk, vChunk, sov));
                written += chunk;
            }
            return;
        }
        int total = c.numericValues == null ? 0 : c.numericValues.length();
        if (total == 0) {
            return;
        }
        int valueSize = numericValueSize(c.numericValues);
        int perSample = 8 + valueSize;
        int maxPer = Math.max(1, (maxPayload(sov) - (1 + 4)) / perSample);
        int written = 0;
        while (written < total) {
            int chunk = Math.min(total - written, maxPer);
            long[] tsChunk = java.util.Arrays.copyOfRange(ts, written, written + chunk);
            out.write(BlockEncoder.timestampedBlock(idx, tsChunk,
                    slice(c.numericValues, written, chunk), sov));
            written += chunk;
        }
    }

    private void emitVariable(OutputStream out, Chan c, int sov) throws IOException {
        int idx = c.def.index();
        // One block per sample (spec: variable payloads are not batched).
        if (c.def.dataType() == DataType.STRING) {
            for (int i = 0; i < c.strings.size(); i++) {
                out.write(BlockEncoder.variableStringBlock(idx, c.timestamps.get(i),
                        c.strings.get(i), sov));
            }
        } else {
            for (int i = 0; i < c.binaries.size(); i++) {
                out.write(BlockEncoder.variableBinaryBlock(idx, c.timestamps.get(i),
                        c.binaries.get(i), sov));
            }
        }
    }

    // ---------------------------------------------------------------
    // Auto-bump.
    // ---------------------------------------------------------------

    /**
     * Resolve a channel's effective {@code sizeoflengthvalue}: pinned width is
     * honoured as-is; otherwise a variable channel is promoted 2&nbsp;&rarr;&nbsp;4
     * when its largest sample plus the single-sample variable-block overhead
     * (control byte + i64 timestamp = 9 bytes) would overflow the 2-byte field.
     * Numeric channels are never bumped (they split into more blocks).
     */
    private int effectiveSizeOfLengthValue(Chan c) {
        if (c.widthPinned || c.def.sizeOfLengthValue() == 4 || c.kind != Kind.VARIABLE) {
            return c.def.sizeOfLengthValue();
        }
        int max = 0;
        for (String s : c.strings) {
            max = Math.max(max, s.getBytes(java.nio.charset.StandardCharsets.UTF_8).length);
        }
        for (byte[] b : c.binaries) {
            max = Math.max(max, b.length);
        }
        int needed = 1 + 8 + max; // [control][i64 ts][bytes]
        return (needed > MAX_PAYLOAD_U16) ? 4 : c.def.sizeOfLengthValue();
    }

    private static int maxPayload(int sov) {
        return (sov == 2) ? MAX_PAYLOAD_U16 : MAX_PAYLOAD_U32;
    }

    // ---------------------------------------------------------------
    // Value accumulation helpers — grow the single concrete Values variant.
    // ---------------------------------------------------------------

    private void appendDouble(Chan c, double v) {
        double[] cur = (c.numericValues instanceof Block.DoubleValues d) ? d.values() : new double[0];
        c.numericValues = new Block.DoubleValues(append(cur, v));
    }

    private void appendFloat(Chan c, float v) {
        float[] cur = (c.numericValues instanceof Block.FloatValues f) ? f.values() : new float[0];
        c.numericValues = new Block.FloatValues(append(cur, v));
    }

    private void appendBool(Chan c, boolean v) {
        boolean[] cur = (c.numericValues instanceof Block.BoolValues b) ? b.values() : new boolean[0];
        boolean[] next = java.util.Arrays.copyOf(cur, cur.length + 1);
        next[cur.length] = v;
        c.numericValues = new Block.BoolValues(next);
    }

    private void appendInteger(Chan c, DataType dt, long v) {
        switch (dt) {
            case INT64 -> {
                long[] cur = (c.numericValues instanceof Block.LongValues x) ? x.values() : new long[0];
                c.numericValues = new Block.LongValues(append(cur, v));
            }
            case UINT64 -> {
                long[] cur = (c.numericValues instanceof Block.ULongValues x) ? x.values() : new long[0];
                c.numericValues = new Block.ULongValues(append(cur, v));
            }
            case INT32 -> {
                int[] cur = (c.numericValues instanceof Block.IntValues x) ? x.values() : new int[0];
                c.numericValues = new Block.IntValues(append(cur, (int) v));
            }
            case UINT32 -> {
                int[] cur = (c.numericValues instanceof Block.UIntValues x) ? x.values() : new int[0];
                c.numericValues = new Block.UIntValues(append(cur, (int) v));
            }
            case INT16 -> {
                short[] cur = (c.numericValues instanceof Block.ShortValues x) ? x.values() : new short[0];
                c.numericValues = new Block.ShortValues(append(cur, (short) v));
            }
            case UINT16 -> {
                short[] cur = (c.numericValues instanceof Block.UShortValues x) ? x.values() : new short[0];
                c.numericValues = new Block.UShortValues(append(cur, (short) v));
            }
            case INT8 -> {
                byte[] cur = (c.numericValues instanceof Block.ByteValues x) ? x.values() : new byte[0];
                c.numericValues = new Block.ByteValues(append(cur, (byte) v));
            }
            case UINT8 -> {
                byte[] cur = (c.numericValues instanceof Block.UByteValues x) ? x.values() : new byte[0];
                c.numericValues = new Block.UByteValues(append(cur, (byte) v));
            }
            default -> throw new OsfException("appendInteger: non-integer data type " + dt);
        }
    }

    // ---------------------------------------------------------------
    // Slice / size helpers over Block.Values.
    // ---------------------------------------------------------------

    private static Block.Values slice(Block.Values v, int start, int count) {
        int end = start + count;
        if (v instanceof Block.DoubleValues x) {
            return new Block.DoubleValues(java.util.Arrays.copyOfRange(x.values(), start, end));
        } else if (v instanceof Block.FloatValues x) {
            return new Block.FloatValues(java.util.Arrays.copyOfRange(x.values(), start, end));
        } else if (v instanceof Block.LongValues x) {
            return new Block.LongValues(java.util.Arrays.copyOfRange(x.values(), start, end));
        } else if (v instanceof Block.ULongValues x) {
            return new Block.ULongValues(java.util.Arrays.copyOfRange(x.values(), start, end));
        } else if (v instanceof Block.IntValues x) {
            return new Block.IntValues(java.util.Arrays.copyOfRange(x.values(), start, end));
        } else if (v instanceof Block.UIntValues x) {
            return new Block.UIntValues(java.util.Arrays.copyOfRange(x.values(), start, end));
        } else if (v instanceof Block.ShortValues x) {
            return new Block.ShortValues(java.util.Arrays.copyOfRange(x.values(), start, end));
        } else if (v instanceof Block.UShortValues x) {
            return new Block.UShortValues(java.util.Arrays.copyOfRange(x.values(), start, end));
        } else if (v instanceof Block.ByteValues x) {
            return new Block.ByteValues(java.util.Arrays.copyOfRange(x.values(), start, end));
        } else if (v instanceof Block.UByteValues x) {
            return new Block.UByteValues(java.util.Arrays.copyOfRange(x.values(), start, end));
        } else if (v instanceof Block.BoolValues x) {
            return new Block.BoolValues(java.util.Arrays.copyOfRange(x.values(), start, end));
        }
        throw new OsfException("slice: unsupported numeric Values " + v.getClass().getSimpleName());
    }

    private static int numericValueSize(Block.Values v) {
        if (v instanceof Block.DoubleValues || v instanceof Block.LongValues
                || v instanceof Block.ULongValues) {
            return 8;
        }
        if (v instanceof Block.FloatValues || v instanceof Block.IntValues
                || v instanceof Block.UIntValues) {
            return 4;
        }
        if (v instanceof Block.ShortValues || v instanceof Block.UShortValues) {
            return 2;
        }
        if (v instanceof Block.ByteValues || v instanceof Block.UByteValues
                || v instanceof Block.BoolValues) {
            return 1;
        }
        if (v instanceof Block.GpsValues) {
            return 24;
        }
        throw new OsfException("numericValueSize: unsupported Values " + v.getClass().getSimpleName());
    }

    // ---------------------------------------------------------------
    // Lock / lookup / validation.
    // ---------------------------------------------------------------

    private Chan requireTimestamped(int channelIndex, DataType expected) {
        Chan c = require(channelIndex);
        if (c.def.dataType() != expected) {
            throw new OsfException("channel " + channelIndex + ": data type mismatch, channel is "
                    + c.def.dataType() + " but sample is " + expected);
        }
        lockTimestamped(c, expected);
        return c;
    }

    private Chan require(int channelIndex) {
        if (channelIndex < 0 || channelIndex >= channels.size()) {
            throw new OsfException("unknown channel index " + channelIndex
                    + " (declared " + channels.size() + " channels)");
        }
        return channels.get(channelIndex);
    }

    private void lockTimestamped(Chan c, DataType expected) {
        if (c.kind != Kind.UNSET && c.kind != Kind.TIMESTAMPED) {
            throw new OsfException("channel " + c.def.index() + ": mixed block types "
                    + "(already " + c.kind + ", now timestamped)");
        }
        if (c.def.dataType() != expected) {
            throw new OsfException("channel " + c.def.index() + ": data type mismatch");
        }
        c.kind = Kind.TIMESTAMPED;
    }

    private void lockVariable(Chan c) {
        if (c.kind != Kind.UNSET && c.kind != Kind.VARIABLE) {
            throw new OsfException("channel " + c.def.index() + ": mixed block types "
                    + "(already " + c.kind + ", now variable)");
        }
        c.kind = Kind.VARIABLE;
    }

    private void lockEquidistant(Chan c) {
        if (c.kind != Kind.UNSET && c.kind != Kind.EQUIDISTANT) {
            throw new OsfException("channel " + c.def.index() + ": mixed block types "
                    + "(already " + c.kind + ", now equidistant)");
        }
        c.kind = Kind.EQUIDISTANT;
    }

    private static boolean isInteger(DataType dt) {
        return switch (dt) {
            case INT8, INT16, INT32, INT64, UINT8, UINT16, UINT32, UINT64 -> true;
            default -> false;
        };
    }

    private static void validateChannel(String name, DataType type, int sizeOfLengthValue) {
        if (name == null || name.isEmpty()) {
            throw new IllegalArgumentException("channel name must be non-empty");
        }
        if (type == null || type == DataType.UNSUPPORTED) {
            throw new IllegalArgumentException("channel data type must be a writeable type");
        }
        if (sizeOfLengthValue != 2 && sizeOfLengthValue != 4) {
            throw new IllegalArgumentException(
                    "sizeOfLengthValue must be 2 or 4, got " + sizeOfLengthValue);
        }
    }

    private static ChannelDef withSize(ChannelDef def, int sov) {
        if (def.sizeOfLengthValue() == sov) {
            return def;
        }
        return new ChannelDef(def.index(), def.name(), def.dataType(), def.channelType(),
                sov, def.timeIncrementNs(), def.physicalUnit(), def.attributes());
    }

    // ---------------------------------------------------------------
    // Primitive-array growth utilities.
    // ---------------------------------------------------------------

    private static double[] append(double[] a, double v) {
        double[] r = java.util.Arrays.copyOf(a, a.length + 1);
        r[a.length] = v;
        return r;
    }

    private static float[] append(float[] a, float v) {
        float[] r = java.util.Arrays.copyOf(a, a.length + 1);
        r[a.length] = v;
        return r;
    }

    private static long[] append(long[] a, long v) {
        long[] r = java.util.Arrays.copyOf(a, a.length + 1);
        r[a.length] = v;
        return r;
    }

    private static int[] append(int[] a, int v) {
        int[] r = java.util.Arrays.copyOf(a, a.length + 1);
        r[a.length] = v;
        return r;
    }

    private static short[] append(short[] a, short v) {
        short[] r = java.util.Arrays.copyOf(a, a.length + 1);
        r[a.length] = v;
        return r;
    }

    private static byte[] append(byte[] a, byte v) {
        byte[] r = java.util.Arrays.copyOf(a, a.length + 1);
        r[a.length] = v;
        return r;
    }

    private static double[] concat(double[] a, double[] b) {
        double[] r = java.util.Arrays.copyOf(a, a.length + b.length);
        System.arraycopy(b, 0, r, a.length, b.length);
        return r;
    }

    private static float[] concat(float[] a, float[] b) {
        float[] r = java.util.Arrays.copyOf(a, a.length + b.length);
        System.arraycopy(b, 0, r, a.length, b.length);
        return r;
    }

    private static long[] toLongArray(List<Long> list) {
        long[] r = new long[list.size()];
        for (int i = 0; i < r.length; i++) {
            r[i] = list.get(i);
        }
        return r;
    }

    private static float[] toFloatArray(double[] d) {
        float[] r = new float[d.length];
        for (int i = 0; i < d.length; i++) {
            r[i] = (float) d[i];
        }
        return r;
    }
}
