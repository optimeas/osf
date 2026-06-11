// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH
package com.optimeas.osf;

import com.optimeas.osf.internal.Block;
import com.optimeas.osf.internal.BlockChunking;
import com.optimeas.osf.internal.BlockEncoder;
import com.optimeas.osf.internal.MetablockBuilder;

import java.io.Closeable;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.channels.FileChannel;
import java.nio.charset.StandardCharsets;
import java.nio.file.Path;
import java.nio.file.StandardOpenOption;
import java.time.Instant;
import java.time.ZoneOffset;
import java.time.format.DateTimeFormatter;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/**
 * Power-loss-safe, streaming OSF5 writer.
 *
 * <p>{@code StreamingWriter} is the streaming counterpart to the in-memory
 * {@link BlockWriter}: it writes the file preamble (magic-header line +
 * metablock) once up front, then <b>batches samples into multi-sample blocks</b>
 * and emits each completed block to the {@link FileChannel}, calling
 * {@link FileChannel#force(boolean) force(true)} (fsync) immediately after the
 * block reaches the channel. Durability is therefore <em>per block</em>: a crash
 * leaves a prefix of whole, fsync'd blocks on disk; the best-effort reader
 * recovers every block before the cut and flags
 * {@link ReaderStats#truncationSeen()} for any partial trailing bytes.
 *
 * <p>The batching uses the <b>same chunking arithmetic as
 * {@link BlockWriter}</b> ({@link BlockChunking}), so for the same channels,
 * samples, {@code sizeoflengthvalue} and {@code created_utc}, the two writers
 * produce <b>byte-identical OSF5</b> — that is the §7 "on-disk-identical"
 * guarantee. See {@code WriterIdentityTest}.
 *
 * <p>A block is emitted (and fsync'd) when a channel's per-block accumulator
 * reaches {@code maxSamplesPerBlock} for its datatype, when the channel's
 * block-kind changes, or on {@link #flush()} / {@link #close()}. Variable
 * {@code string} / {@code binary} samples are written one block per sample
 * (variable payloads are never batched, matching the reference). Equidistant
 * segments accumulate until the segment is superseded / flushed, then emit a
 * {@code bcStartData} opener plus {@code bcContinuedData} chunks at the same
 * granularity as {@link BlockWriter}.
 *
 * <p>This is a faithful port of the C++ reference
 * {@code implementations/cpp/src/streaming_writer.cpp} (the chunked write loops
 * using {@code max_samples_per_*_block}, the per-block {@code do_write_block}
 * fsync gate, the once-up-front preamble, the float/double-only equidistant
 * restriction). Like the reference, the streaming writer fixes
 * {@code sizeoflengthvalue} per channel up front and <em>cannot</em> auto-bump
 * it — a variable sample that would overflow the declared length field is
 * rejected rather than silently promoted.
 *
 * <h2>Lifecycle</h2>
 * <ol>
 *   <li>{@link #create(Path)} opens the {@link FileChannel}
 *       ({@code CREATE / TRUNCATE_EXISTING / WRITE}).</li>
 *   <li>Declare channels with {@link #addTimestampedChannel} /
 *       {@link #addEquidistantChannel} (Configure phase).</li>
 *   <li>The preamble is written lazily on the first sample, or eagerly via
 *       {@link #begin()}.</li>
 *   <li>Stream samples; each completed block is {@code force(true)}d.</li>
 *   <li>{@link #close()} emits any buffered partial blocks, forces and closes.</li>
 * </ol>
 *
 * <p>Equidistant channels accept {@code float}/{@code double} only (spec rev
 * 2026-05-04); declaring any other data type as equidistant throws
 * {@link IllegalArgumentException}.
 */
public final class StreamingWriter implements Closeable {

    private static final DateTimeFormatter CREATED_UTC =
            DateTimeFormatter.ofPattern("yyyy-MM-dd'T'HH:mm:ss'Z'").withZone(ZoneOffset.UTC);

    private enum Phase { CONFIGURE, STREAMING, CLOSED }

    /** Block-family lock per channel, mirroring the reference's kind lock. */
    private enum Kind { UNSET, TIMESTAMPED, EQUIDISTANT, VARIABLE }

    /** One accumulated equidistant segment (float / double only). */
    private static final class EqSegment {
        final long startTimestampNs;
        final double sampleRateHz;
        Block.Values values;
        EqSegment(long startTimestampNs, double sampleRateHz, Block.Values values) {
            this.startTimestampNs = startTimestampNs;
            this.sampleRateHz = sampleRateHz;
            this.values = values;
        }
    }

    private static final class Chan {
        final ChannelDef def;
        Kind kind = Kind.UNSET;

        // Timestamped-numeric / GPS accumulator (parallel timestamps + values).
        final List<Long> timestamps = new ArrayList<>();
        Block.Values numericValues; // one concrete numeric variant, grown by append
        final List<GpsLocation> gps = new ArrayList<>();

        // Equidistant accumulator: the currently open segment (if any).
        EqSegment openSegment;

        Chan(ChannelDef def) { this.def = def; }
    }

    private final FileChannel channel;
    private final Map<String, String> metadata = new LinkedHashMap<>();
    private final List<Chan> channels = new ArrayList<>();
    private final Map<Integer, Double> rateByIndex = new LinkedHashMap<>();
    private Phase phase = Phase.CONFIGURE;

    private StreamingWriter(FileChannel channel) {
        this.channel = channel;
    }

    /**
     * Open a streaming writer on {@code path}, truncating any existing file.
     *
     * @param path target file
     * @return a writer in the Configure phase
     * @throws OsfException on I/O failure opening the file
     */
    public static StreamingWriter create(Path path) {
        try {
            FileChannel fc = FileChannel.open(path,
                    StandardOpenOption.CREATE,
                    StandardOpenOption.TRUNCATE_EXISTING,
                    StandardOpenOption.WRITE);
            return new StreamingWriter(fc);
        } catch (IOException e) {
            throw new OsfException("failed to open OSF file " + path + ": " + e.getMessage(), e);
        }
    }

    // ---------------------------------------------------------------
    // File-level metadata (optional; created_utc is injected at begin()).
    // ---------------------------------------------------------------

    /**
     * Set a file-level metadata entry written verbatim into the metablock
     * {@code osf.file} object. Must be called before the preamble is written
     * (i.e. while still in the Configure phase).
     *
     * @param key   wire field name (e.g. {@code "creator"}, {@code "tag"})
     * @param value the value
     */
    public void setMetadata(String key, String value) {
        requireConfigure("setMetadata");
        metadata.put(key, value);
    }

    // ---------------------------------------------------------------
    // Channel declaration (Configure phase only).
    // ---------------------------------------------------------------

    /**
     * Declare a timestamped channel (each sample carries its own absolute
     * timestamp). Supports every numeric data type plus GPS, string and binary.
     *
     * @param name              fully-qualified channel name
     * @param type              the channel's data type
     * @param sizeOfLengthValue length-prefix width on disk; 2 or 4
     * @return the channel index for use in {@code writeSample}
     */
    public int addTimestampedChannel(String name, DataType type, int sizeOfLengthValue) {
        return addTimestampedChannel(name, type, sizeOfLengthValue, null, null);
    }

    /**
     * Declare a timestamped channel with an optional physical unit and extra
     * scalar string attributes.
     *
     * @param name              fully-qualified channel name
     * @param type              the channel's data type
     * @param sizeOfLengthValue length-prefix width on disk; 2 or 4
     * @param physicalUnit      physical unit string, or {@code null}
     * @param attributes        extra metablock string attributes, or {@code null}
     * @return the channel index
     */
    public int addTimestampedChannel(String name, DataType type, int sizeOfLengthValue,
                                     String physicalUnit, Map<String, String> attributes) {
        requireConfigure("addTimestampedChannel");
        validateChannel(name, type, sizeOfLengthValue);
        ChannelType ct = ChannelType.SCALAR; // normalised (Delphi/reference convention)
        ChannelDef def = new ChannelDef(channels.size(), name, type, ct,
                sizeOfLengthValue, 0L, physicalUnit, copyAttrs(attributes));
        channels.add(new Chan(def));
        return def.index();
    }

    /**
     * Declare an equidistant channel (samples spaced at a fixed rate; only the
     * segment start carries a timestamp). Equidistant channels accept
     * {@code float} / {@code double} only (spec rev 2026-05-04).
     *
     * @param name              fully-qualified channel name
     * @param type              {@link DataType#FLOAT} or {@link DataType#DOUBLE}
     * @param sizeOfLengthValue length-prefix width on disk; 2 or 4
     * @param sampleRateHz      sample rate in Hz; must be a positive finite value
     * @return the channel index
     * @throws IllegalArgumentException if {@code type} is not float/double or the
     *         sample rate is not a positive finite value
     */
    public int addEquidistantChannel(String name, DataType type, int sizeOfLengthValue,
                                     double sampleRateHz) {
        return addEquidistantChannel(name, type, sizeOfLengthValue, sampleRateHz, null, null);
    }

    /**
     * Declare an equidistant channel with an optional physical unit and extra
     * scalar string attributes.
     *
     * @param name              fully-qualified channel name
     * @param type              {@link DataType#FLOAT} or {@link DataType#DOUBLE}
     * @param sizeOfLengthValue length-prefix width on disk; 2 or 4
     * @param sampleRateHz      sample rate in Hz; positive finite
     * @param physicalUnit      physical unit string, or {@code null}
     * @param attributes        extra metablock string attributes, or {@code null}
     * @return the channel index
     */
    public int addEquidistantChannel(String name, DataType type, int sizeOfLengthValue,
                                     double sampleRateHz, String physicalUnit,
                                     Map<String, String> attributes) {
        requireConfigure("addEquidistantChannel");
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
        // timeincrement (ns) — carried in the metablock for equidistant channels.
        long timeIncrementNs = Math.max(1L, Math.round(1.0e9 / sampleRateHz));
        ChannelDef def = new ChannelDef(channels.size(), name, type, ChannelType.EQUIDISTANT,
                sizeOfLengthValue, timeIncrementNs, physicalUnit, copyAttrs(attributes));
        channels.add(new Chan(def));
        rateByIndex.put(def.index(), sampleRateHz);
        return def.index();
    }

    // ---------------------------------------------------------------
    // Preamble.
    // ---------------------------------------------------------------

    /**
     * Write the file preamble — the {@code OSF5 <len>\n} magic-header line and
     * the JSON metablock — and {@code force(true)} it. Called automatically on
     * the first sample; call it explicitly to pin the preamble before any data.
     *
     * <p>Injects {@code created_utc} (ISO-8601 UTC, e.g.
     * {@code 2026-06-11T08:30:00Z}) into the metadata if not already present,
     * matching the reference {@code format_utc_now}.
     *
     * @throws OsfException if no channels were declared, or on I/O failure
     */
    public void begin() {
        if (phase == Phase.STREAMING) {
            return;
        }
        if (phase == Phase.CLOSED) {
            throw new OsfException("begin: writer is closed");
        }
        if (channels.isEmpty()) {
            throw new OsfException("begin: no channels declared");
        }
        metadata.putIfAbsent("created_utc", CREATED_UTC.format(Instant.now()));

        List<ChannelDef> defs = new ArrayList<>(channels.size());
        for (Chan c : channels) {
            defs.add(c.def);
        }
        byte[] metablock = MetablockBuilder.buildOsf5Json(5, metadata, defs);
        byte[] magic = ("OSF5 " + metablock.length + "\n").getBytes(StandardCharsets.US_ASCII);

        writeAll(magic);
        writeAll(metablock);
        forceChannel();
        phase = Phase.STREAMING;
    }

    // ---------------------------------------------------------------
    // writeSample — timestamped scalar overloads (accumulate; emit when full).
    // ---------------------------------------------------------------

    /** Write one timestamped {@code double} sample. */
    public void writeSample(int channelIndex, long timestampNs, double value) {
        Chan c = beginAndRequireTimestamped(channelIndex, DataType.DOUBLE);
        c.timestamps.add(timestampNs);
        c.numericValues = new Block.DoubleValues(append(asDoubles(c.numericValues), value));
        maybeEmitTimestamped(c);
    }

    /** Write one timestamped {@code float} sample. */
    public void writeSample(int channelIndex, long timestampNs, float value) {
        Chan c = beginAndRequireTimestamped(channelIndex, DataType.FLOAT);
        c.timestamps.add(timestampNs);
        c.numericValues = new Block.FloatValues(append(asFloats(c.numericValues), value));
        maybeEmitTimestamped(c);
    }

    /** Write one timestamped {@code boolean} sample. */
    public void writeSample(int channelIndex, long timestampNs, boolean value) {
        Chan c = beginAndRequireTimestamped(channelIndex, DataType.BOOL);
        c.timestamps.add(timestampNs);
        boolean[] cur = (c.numericValues instanceof Block.BoolValues b) ? b.values() : new boolean[0];
        boolean[] next = Arrays.copyOf(cur, cur.length + 1);
        next[cur.length] = value;
        c.numericValues = new Block.BoolValues(next);
        maybeEmitTimestamped(c);
    }

    /**
     * Write one timestamped integer sample. The channel must be one of the
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
        beginIfNeeded();
        c.timestamps.add(timestampNs);
        appendInteger(c, dt, value);
        maybeEmitTimestamped(c);
    }

    /** Write one timestamped {@code int} sample (widens to {@link #writeSample(int, long, long)}). */
    public void writeSample(int channelIndex, long timestampNs, int value) {
        writeSample(channelIndex, timestampNs, (long) value);
    }

    /** Write one timestamped GPS sample. */
    public void writeSample(int channelIndex, long timestampNs, GpsLocation value) {
        Chan c = beginAndRequireTimestamped(channelIndex, DataType.GPS_LOCATION);
        c.timestamps.add(timestampNs);
        c.gps.add(value);
        maybeEmitTimestamped(c);
    }

    /** Write one timestamped {@code string} sample (one block per sample). */
    public void writeSample(int channelIndex, long timestampNs, String value) {
        Chan c = require(channelIndex);
        if (c.def.dataType() != DataType.STRING) {
            throw new OsfException("channel " + channelIndex
                    + ": writeSample(String) requires a string channel, channel is "
                    + c.def.dataType());
        }
        lockVariable(c);
        beginIfNeeded();
        byte[] block = BlockEncoder.variableStringBlock(
                channelIndex, timestampNs, value, c.def.sizeOfLengthValue());
        writeBlock(block);
    }

    /** Write one timestamped {@code binary} sample (one block per sample). */
    public void writeSample(int channelIndex, long timestampNs, byte[] value) {
        Chan c = require(channelIndex);
        if (c.def.dataType() != DataType.BINARY) {
            throw new OsfException("channel " + channelIndex
                    + ": writeSample(byte[]) requires a binary channel, channel is "
                    + c.def.dataType());
        }
        lockVariable(c);
        beginIfNeeded();
        byte[] block = BlockEncoder.variableBinaryBlock(
                channelIndex, timestampNs, value, c.def.sizeOfLengthValue());
        writeBlock(block);
    }

    // ---------------------------------------------------------------
    // writeSamples — timestamped batch overloads (efficient bulk append).
    // ---------------------------------------------------------------

    /** Write a batch of timestamped {@code double} samples. */
    public void writeSamples(int channelIndex, long[] timestampsNs, double[] values) {
        requireSameLength(timestampsNs, values.length);
        Chan c = beginAndRequireTimestamped(channelIndex, DataType.DOUBLE);
        for (int i = 0; i < values.length; i++) {
            c.timestamps.add(timestampsNs[i]);
            c.numericValues = new Block.DoubleValues(append(asDoubles(c.numericValues), values[i]));
            maybeEmitTimestamped(c);
        }
    }

    /** Write a batch of timestamped {@code float} samples. */
    public void writeSamples(int channelIndex, long[] timestampsNs, float[] values) {
        requireSameLength(timestampsNs, values.length);
        Chan c = beginAndRequireTimestamped(channelIndex, DataType.FLOAT);
        for (int i = 0; i < values.length; i++) {
            c.timestamps.add(timestampsNs[i]);
            c.numericValues = new Block.FloatValues(append(asFloats(c.numericValues), values[i]));
            maybeEmitTimestamped(c);
        }
    }

    /**
     * Write a batch of timestamped integer samples to an integer channel. Each
     * value is narrowed to the channel's width on encode.
     */
    public void writeSamples(int channelIndex, long[] timestampsNs, long[] values) {
        requireSameLength(timestampsNs, values.length);
        Chan c = require(channelIndex);
        DataType dt = c.def.dataType();
        if (!isInteger(dt)) {
            throw new OsfException("channel " + channelIndex
                    + ": writeSamples(long[]) requires an integer data type, channel is " + dt);
        }
        lockTimestamped(c, dt);
        beginIfNeeded();
        for (int i = 0; i < values.length; i++) {
            c.timestamps.add(timestampsNs[i]);
            appendInteger(c, dt, values[i]);
            maybeEmitTimestamped(c);
        }
    }

    /** Write a batch of timestamped GPS samples. */
    public void writeSamples(int channelIndex, long[] timestampsNs, GpsLocation[] values) {
        requireSameLength(timestampsNs, values.length);
        Chan c = beginAndRequireTimestamped(channelIndex, DataType.GPS_LOCATION);
        for (int i = 0; i < values.length; i++) {
            c.timestamps.add(timestampsNs[i]);
            c.gps.add(values[i]);
            maybeEmitTimestamped(c);
        }
    }

    // ---------------------------------------------------------------
    // Equidistant.
    // ---------------------------------------------------------------

    /**
     * Open an equidistant segment using the channel's configured sample rate. The
     * samples accumulate in memory and are emitted (as a {@code bcStartData}
     * opener plus chunked {@code bcContinuedData} blocks) when the segment is
     * superseded by another {@code startEquidistantSegment}, or on
     * {@link #flush()} / {@link #close()} — exactly mirroring {@link BlockWriter}.
     *
     * @param channelIndex the equidistant channel
     * @param startNs      absolute start timestamp of the segment (ns)
     * @param samples      the segment's {@code double} samples
     */
    public void startEquidistantSegment(int channelIndex, long startNs, double[] samples) {
        Chan c = require(channelIndex);
        if (c.def.dataType() != DataType.DOUBLE) {
            throw new OsfException("channel " + channelIndex
                    + ": startEquidistantSegment(double[]) requires a double channel, channel is "
                    + c.def.dataType());
        }
        lockEquidistant(c);
        beginIfNeeded();
        flushEquidistant(c);
        double rate = rateByIndex.getOrDefault(channelIndex, 0.0);
        c.openSegment = new EqSegment(startNs, rate, new Block.DoubleValues(samples.clone()));
    }

    /**
     * Open an equidistant segment of {@code float} samples.
     *
     * @param channelIndex the equidistant channel
     * @param startNs      absolute start timestamp of the segment (ns)
     * @param samples      the segment's {@code float} samples
     */
    public void startEquidistantSegment(int channelIndex, long startNs, float[] samples) {
        Chan c = require(channelIndex);
        if (c.def.dataType() != DataType.FLOAT) {
            throw new OsfException("channel " + channelIndex
                    + ": startEquidistantSegment(float[]) requires a float channel, channel is "
                    + c.def.dataType());
        }
        lockEquidistant(c);
        beginIfNeeded();
        flushEquidistant(c);
        double rate = rateByIndex.getOrDefault(channelIndex, 0.0);
        c.openSegment = new EqSegment(startNs, rate, new Block.FloatValues(samples.clone()));
    }

    /**
     * Append {@code double} samples to the channel's currently open equidistant
     * segment. The samples join the open segment's in-memory buffer and are
     * emitted at the next segment change / flush / close.
     *
     * @param channelIndex the equidistant channel
     * @param samples      additional samples for the open segment
     * @throws OsfException if no segment is open on the channel
     */
    public void appendEquidistantSamples(int channelIndex, double[] samples) {
        Chan c = require(channelIndex);
        lockEquidistant(c);
        if (c.openSegment == null) {
            throw new OsfException("channel " + channelIndex
                    + ": appendEquidistantSamples without an open segment "
                    + "(call startEquidistantSegment first)");
        }
        if (!(c.openSegment.values instanceof Block.DoubleValues dv)) {
            throw new OsfException("channel " + channelIndex
                    + ": appendEquidistantSamples(double[]) on a non-double segment");
        }
        c.openSegment.values = new Block.DoubleValues(concat(dv.values(), samples));
    }

    /**
     * Append {@code float} samples to the channel's open equidistant segment.
     *
     * @param channelIndex the equidistant channel
     * @param samples      additional samples for the open segment
     * @throws OsfException if no segment is open on the channel
     */
    public void appendEquidistantSamples(int channelIndex, float[] samples) {
        Chan c = require(channelIndex);
        lockEquidistant(c);
        if (c.openSegment == null) {
            throw new OsfException("channel " + channelIndex
                    + ": appendEquidistantSamples without an open segment "
                    + "(call startEquidistantSegment first)");
        }
        if (!(c.openSegment.values instanceof Block.FloatValues fv)) {
            throw new OsfException("channel " + channelIndex
                    + ": appendEquidistantSamples(float[]) on a non-float segment");
        }
        c.openSegment.values = new Block.FloatValues(concat(fv.values(), samples));
    }

    // ---------------------------------------------------------------
    // flush / close.
    // ---------------------------------------------------------------

    /**
     * Emit every channel's buffered partial blocks and {@code force(true)} the
     * file. After {@code flush()} returns, all samples handed to the writer so far
     * are durable as whole blocks on disk. A no-op before the preamble is written
     * or after {@link #close()}.
     *
     * @throws OsfException on I/O failure
     */
    public void flush() {
        if (phase != Phase.STREAMING) {
            return;
        }
        for (Chan c : channels) {
            switch (c.kind) {
                case TIMESTAMPED -> flushTimestamped(c);
                case EQUIDISTANT -> flushEquidistant(c);
                default -> { /* VARIABLE already emits per sample; UNSET has nothing */ }
            }
        }
        forceChannel();
    }

    /**
     * Emit any buffered partial blocks, force and close the underlying file
     * channel. Idempotent: a second call is a no-op. If no preamble was written
     * (no channels or no samples), the file is still closed.
     *
     * @throws OsfException on I/O failure during the final flush/force/close
     */
    @Override
    public void close() {
        if (phase == Phase.CLOSED) {
            return;
        }
        try {
            if (phase == Phase.STREAMING) {
                flush();
            }
            if (channel.isOpen()) {
                channel.close();
            }
        } catch (IOException e) {
            phase = Phase.CLOSED;
            throw new OsfException("failed to close OSF file: " + e.getMessage(), e);
        }
        phase = Phase.CLOSED;
    }

    // ---------------------------------------------------------------
    // Emission internals.
    // ---------------------------------------------------------------

    /**
     * Emit one full multi-sample timestamped block whenever the channel's
     * accumulator reaches {@code maxSamplesPerBlock}. Mirrors the reference's
     * chunked write loop; partial remainders are emitted at {@link #flush()}.
     */
    private void maybeEmitTimestamped(Chan c) {
        int sov = c.def.sizeOfLengthValue();
        if (c.def.dataType() == DataType.GPS_LOCATION) {
            int maxPer = BlockChunking.maxSamplesPerTimestampedGps(sov);
            while (c.gps.size() >= maxPer) {
                emitTimestampedGpsChunk(c, maxPer, sov);
            }
        } else {
            int valueSize = numericValueSize(c.numericValues);
            int maxPer = BlockChunking.maxSamplesPerTimestamped(valueSize, sov);
            while (count(c.numericValues) >= maxPer) {
                emitTimestampedNumericChunk(c, maxPer, sov);
            }
        }
    }

    /** Emit all buffered timestamped samples as a final (partial) block. */
    private void flushTimestamped(Chan c) {
        int sov = c.def.sizeOfLengthValue();
        if (c.def.dataType() == DataType.GPS_LOCATION) {
            if (!c.gps.isEmpty()) {
                emitTimestampedGpsChunk(c, c.gps.size(), sov);
            }
        } else {
            int n = count(c.numericValues);
            if (n > 0) {
                emitTimestampedNumericChunk(c, n, sov);
            }
        }
    }

    /** Emit the first {@code chunk} buffered numeric samples, then drop them. */
    private void emitTimestampedNumericChunk(Chan c, int chunk, int sov) {
        int idx = c.def.index();
        long[] ts = new long[chunk];
        for (int i = 0; i < chunk; i++) {
            ts[i] = c.timestamps.get(i);
        }
        byte[] block = BlockEncoder.timestampedBlock(idx, ts, slice(c.numericValues, 0, chunk), sov);
        writeBlock(block);
        dropFront(c.timestamps, chunk);
        c.numericValues = sliceFrom(c.numericValues, chunk);
    }

    /** Emit the first {@code chunk} buffered GPS samples, then drop them. */
    private void emitTimestampedGpsChunk(Chan c, int chunk, int sov) {
        int idx = c.def.index();
        long[] ts = new long[chunk];
        GpsLocation[] v = new GpsLocation[chunk];
        for (int i = 0; i < chunk; i++) {
            ts[i] = c.timestamps.get(i);
            v[i] = c.gps.get(i);
        }
        byte[] block = BlockEncoder.timestampedGpsBlock(idx, ts, v, sov);
        writeBlock(block);
        dropFront(c.timestamps, chunk);
        dropFront(c.gps, chunk);
    }

    /**
     * Emit the channel's open equidistant segment (a {@code bcStartData} opener
     * plus chunked {@code bcContinuedData} blocks) and clear it. Same chunk
     * boundaries as {@link BlockWriter#emitChannel}.
     */
    private void flushEquidistant(Chan c) {
        EqSegment seg = c.openSegment;
        if (seg == null) {
            return;
        }
        c.openSegment = null;
        int idx = c.def.index();
        int valueSize = numericValueSize(seg.values);
        int total = seg.values.length();
        int sov = c.def.sizeOfLengthValue();
        int maxStart = BlockChunking.maxSamplesPerStart(valueSize, sov);
        int maxCont = BlockChunking.maxSamplesPerContinued(valueSize, sov);

        int first = Math.min(total, maxStart);
        writeBlock(BlockEncoder.startDataBlock(idx, seg.startTimestampNs, seg.sampleRateHz,
                slice(seg.values, 0, first), sov));
        int written = first;
        while (written < total) {
            int chunk = Math.min(total - written, maxCont);
            writeBlock(BlockEncoder.continuedDataBlock(idx, slice(seg.values, written, chunk), sov));
            written += chunk;
        }
    }

    private Chan beginAndRequireTimestamped(int channelIndex, DataType expected) {
        Chan c = require(channelIndex);
        if (c.def.dataType() != expected) {
            throw new OsfException("channel " + channelIndex + ": data type mismatch, channel is "
                    + c.def.dataType() + " but sample is " + expected);
        }
        lockTimestamped(c, expected);
        beginIfNeeded();
        return c;
    }

    private Chan require(int channelIndex) {
        if (phase == Phase.CLOSED) {
            throw new OsfException("writer is closed");
        }
        if (channelIndex < 0 || channelIndex >= channels.size()) {
            throw new OsfException("unknown channel index " + channelIndex
                    + " (declared " + channels.size() + " channels)");
        }
        return channels.get(channelIndex);
    }

    private void beginIfNeeded() {
        if (phase == Phase.CONFIGURE) {
            begin();
        }
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

    /** Write one complete block frame, then fsync — the power-loss-safe gate. */
    private void writeBlock(byte[] block) {
        writeAll(block);
        forceChannel();
    }

    private void writeAll(byte[] data) {
        if (phase == Phase.CLOSED) {
            throw new OsfException("writer is closed");
        }
        ByteBuffer buf = ByteBuffer.wrap(data);
        try {
            while (buf.hasRemaining()) {
                channel.write(buf);
            }
        } catch (IOException e) {
            throw new OsfException("OSF write failed: " + e.getMessage(), e);
        }
    }

    private void forceChannel() {
        try {
            channel.force(true);
        } catch (IOException e) {
            throw new OsfException("OSF fsync (force) failed: " + e.getMessage(), e);
        }
    }

    private void requireConfigure(String op) {
        if (phase != Phase.CONFIGURE) {
            throw new OsfException(op + ": writer is past the Configure phase");
        }
    }

    private static void requireSameLength(long[] timestampsNs, int valueCount) {
        if (timestampsNs.length != valueCount) {
            throw new OsfException("writeSamples: timestamps.length=" + timestampsNs.length
                    + " != values.length=" + valueCount);
        }
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

    private static Map<String, String> copyAttrs(Map<String, String> attributes) {
        return (attributes == null) ? Map.of() : new LinkedHashMap<>(attributes);
    }

    private static boolean isInteger(DataType dt) {
        return switch (dt) {
            case INT8, INT16, INT32, INT64, UINT8, UINT16, UINT32, UINT64 -> true;
            default -> false;
        };
    }

    // ---------------------------------------------------------------
    // Value-accumulator helpers over Block.Values.
    // ---------------------------------------------------------------

    private static int count(Block.Values v) {
        return (v == null) ? 0 : v.length();
    }

    private static double[] asDoubles(Block.Values v) {
        return (v instanceof Block.DoubleValues d) ? d.values() : new double[0];
    }

    private static float[] asFloats(Block.Values v) {
        return (v instanceof Block.FloatValues f) ? f.values() : new float[0];
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
            return BlockChunking.GPS_VALUE_SIZE;
        }
        // null or unknown — only reached when no numeric samples are buffered.
        return 8;
    }

    /** {@code values[start, start+count)} as a new {@link Block.Values}. */
    private static Block.Values slice(Block.Values v, int start, int count) {
        int end = start + count;
        if (v instanceof Block.DoubleValues x) {
            return new Block.DoubleValues(Arrays.copyOfRange(x.values(), start, end));
        } else if (v instanceof Block.FloatValues x) {
            return new Block.FloatValues(Arrays.copyOfRange(x.values(), start, end));
        } else if (v instanceof Block.LongValues x) {
            return new Block.LongValues(Arrays.copyOfRange(x.values(), start, end));
        } else if (v instanceof Block.ULongValues x) {
            return new Block.ULongValues(Arrays.copyOfRange(x.values(), start, end));
        } else if (v instanceof Block.IntValues x) {
            return new Block.IntValues(Arrays.copyOfRange(x.values(), start, end));
        } else if (v instanceof Block.UIntValues x) {
            return new Block.UIntValues(Arrays.copyOfRange(x.values(), start, end));
        } else if (v instanceof Block.ShortValues x) {
            return new Block.ShortValues(Arrays.copyOfRange(x.values(), start, end));
        } else if (v instanceof Block.UShortValues x) {
            return new Block.UShortValues(Arrays.copyOfRange(x.values(), start, end));
        } else if (v instanceof Block.ByteValues x) {
            return new Block.ByteValues(Arrays.copyOfRange(x.values(), start, end));
        } else if (v instanceof Block.UByteValues x) {
            return new Block.UByteValues(Arrays.copyOfRange(x.values(), start, end));
        } else if (v instanceof Block.BoolValues x) {
            return new Block.BoolValues(Arrays.copyOfRange(x.values(), start, end));
        }
        throw new OsfException("slice: unsupported numeric Values "
                + (v == null ? "null" : v.getClass().getSimpleName()));
    }

    /** The numeric accumulator with its first {@code drop} samples removed. */
    private static Block.Values sliceFrom(Block.Values v, int drop) {
        int len = v.length();
        if (drop >= len) {
            return null;
        }
        return slice(v, drop, len - drop);
    }

    private static void dropFront(List<?> list, int n) {
        list.subList(0, n).clear();
    }

    // ---------------------------------------------------------------
    // Primitive-array growth utilities.
    // ---------------------------------------------------------------

    private static double[] append(double[] a, double v) {
        double[] r = Arrays.copyOf(a, a.length + 1);
        r[a.length] = v;
        return r;
    }

    private static float[] append(float[] a, float v) {
        float[] r = Arrays.copyOf(a, a.length + 1);
        r[a.length] = v;
        return r;
    }

    private static long[] append(long[] a, long v) {
        long[] r = Arrays.copyOf(a, a.length + 1);
        r[a.length] = v;
        return r;
    }

    private static int[] append(int[] a, int v) {
        int[] r = Arrays.copyOf(a, a.length + 1);
        r[a.length] = v;
        return r;
    }

    private static short[] append(short[] a, short v) {
        short[] r = Arrays.copyOf(a, a.length + 1);
        r[a.length] = v;
        return r;
    }

    private static byte[] append(byte[] a, byte v) {
        byte[] r = Arrays.copyOf(a, a.length + 1);
        r[a.length] = v;
        return r;
    }

    private static double[] concat(double[] a, double[] b) {
        double[] r = Arrays.copyOf(a, a.length + b.length);
        System.arraycopy(b, 0, r, a.length, b.length);
        return r;
    }

    private static float[] concat(float[] a, float[] b) {
        float[] r = Arrays.copyOf(a, a.length + b.length);
        System.arraycopy(b, 0, r, a.length, b.length);
        return r;
    }
}
