// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH
package com.optimeas.osf;

import com.optimeas.osf.internal.Block;
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
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/**
 * Power-loss-safe, sample-by-sample OSF5 writer.
 *
 * <p>{@code StreamingWriter} is the streaming counterpart to the in-memory
 * {@code BlockWriter}: it writes the file preamble (magic-header line +
 * metablock) once up front and then emits one OSF5 data block per
 * {@code writeSample} call, calling {@link FileChannel#force(boolean)
 * force(true)} (fsync) immediately after each completed block reaches the
 * channel. A crash therefore leaves a prefix of whole, durable blocks on disk;
 * the best-effort reader recovers every block before the cut and flags
 * {@link ReaderStats#truncationSeen()} for any partial trailing bytes.
 *
 * <p>This is a faithful port of the C++ reference
 * {@code implementations/cpp/src/streaming_writer.cpp} (the
 * write-then-{@code force()} I/O gate {@code do_write_block}, the once-up-front
 * preamble in {@code start()}, the float/double-only equidistant restriction)
 * and the metablock/block shapes from
 * {@code implementations/rust/osf-core/src/writer.rs}. Like the reference, the
 * streaming writer fixes {@code sizeoflengthvalue} per channel up front and
 * <em>cannot</em> auto-bump it — a sample that would overflow the declared
 * length field is rejected rather than silently promoted.
 *
 * <h2>Lifecycle</h2>
 * <ol>
 *   <li>{@link #create(Path)} opens the {@link FileChannel}
 *       ({@code CREATE / TRUNCATE_EXISTING / WRITE}).</li>
 *   <li>Declare channels with {@link #addTimestampedChannel} /
 *       {@link #addEquidistantChannel} (Configure phase).</li>
 *   <li>The preamble is written lazily on the first {@code writeSample} /
 *       {@code startEquidistantSegment}, or eagerly via {@link #begin()}.</li>
 *   <li>Stream samples; each completed block is {@code force(true)}d.</li>
 *   <li>{@link #close()} forces and closes the channel.</li>
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

    private static final class Chan {
        final ChannelDef def;
        Kind kind = Kind.UNSET;
        boolean segmentOpen = false; // equidistant only
        Chan(ChannelDef def) { this.def = def; }
    }

    private final FileChannel channel;
    private final Map<String, String> metadata = new LinkedHashMap<>();
    private final List<Chan> channels = new ArrayList<>();
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
        // Remember the configured rate for the segment/append APIs.
        channels.add(new Chan(def));
        rateByIndex.put(def.index(), sampleRateHz);
        return def.index();
    }

    private final Map<Integer, Double> rateByIndex = new LinkedHashMap<>();

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
    // writeSample — timestamped overloads. Each writes one block + force.
    // ---------------------------------------------------------------

    /** Write one timestamped {@code double} sample. */
    public void writeSample(int channelIndex, long timestampNs, double value) {
        Chan c = beginAndRequireTimestamped(channelIndex, DataType.DOUBLE);
        emitTimestampedNumeric(c, timestampNs, new Block.DoubleValues(new double[]{value}));
    }

    /** Write one timestamped {@code long} ({@code int64}) sample. */
    public void writeSample(int channelIndex, long timestampNs, long value) {
        Chan c = require(channelIndex);
        DataType dt = c.def.dataType();
        Block.Values v = switch (dt) {
            case INT64 -> new Block.LongValues(new long[]{value});
            case UINT64 -> new Block.ULongValues(new long[]{value});
            case INT32 -> new Block.IntValues(new int[]{(int) value});
            case UINT32 -> new Block.UIntValues(new int[]{(int) value});
            case INT16 -> new Block.ShortValues(new short[]{(short) value});
            case UINT16 -> new Block.UShortValues(new short[]{(short) value});
            case INT8 -> new Block.ByteValues(new byte[]{(byte) value});
            case UINT8 -> new Block.UByteValues(new byte[]{(byte) value});
            default -> throw new OsfException("channel " + channelIndex
                    + ": writeSample(long) requires an integer data type, channel is " + dt);
        };
        lockTimestamped(c, dt);
        emitTimestampedNumeric(c, timestampNs, v);
    }

    /** Write one timestamped {@code int} sample (widens to {@link #writeSample(int, long, long)}). */
    public void writeSample(int channelIndex, long timestampNs, int value) {
        writeSample(channelIndex, timestampNs, (long) value);
    }

    /** Write one timestamped {@code float} sample. */
    public void writeSample(int channelIndex, long timestampNs, float value) {
        Chan c = beginAndRequireTimestamped(channelIndex, DataType.FLOAT);
        emitTimestampedNumeric(c, timestampNs, new Block.FloatValues(new float[]{value}));
    }

    /** Write one timestamped {@code boolean} sample. */
    public void writeSample(int channelIndex, long timestampNs, boolean value) {
        Chan c = beginAndRequireTimestamped(channelIndex, DataType.BOOL);
        emitTimestampedNumeric(c, timestampNs, new Block.BoolValues(new boolean[]{value}));
    }

    /** Write one timestamped GPS sample. */
    public void writeSample(int channelIndex, long timestampNs, GpsLocation value) {
        Chan c = beginAndRequireTimestamped(channelIndex, DataType.GPS_LOCATION);
        byte[] block = BlockEncoder.timestampedGpsBlock(
                channelIndex, new long[]{timestampNs}, new GpsLocation[]{value},
                c.def.sizeOfLengthValue());
        writeBlock(block);
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
    // Equidistant.
    // ---------------------------------------------------------------

    /**
     * Open an equidistant segment using the channel's configured sample rate,
     * writing the samples as a {@code bcStartData} block at {@code startNs}.
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
        double rate = rateByIndex.getOrDefault(channelIndex, 0.0);
        byte[] block = BlockEncoder.startDataBlock(
                channelIndex, startNs, rate, new Block.DoubleValues(samples.clone()),
                c.def.sizeOfLengthValue());
        writeBlock(block);
        c.segmentOpen = true;
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
        double rate = rateByIndex.getOrDefault(channelIndex, 0.0);
        byte[] block = BlockEncoder.startDataBlock(
                channelIndex, startNs, rate, new Block.FloatValues(samples.clone()),
                c.def.sizeOfLengthValue());
        writeBlock(block);
        c.segmentOpen = true;
    }

    /**
     * Append {@code double} samples to the channel's currently open equidistant
     * segment, writing them as a {@code bcContinuedData} block.
     *
     * @param channelIndex the equidistant channel
     * @param samples      additional samples for the open segment
     * @throws OsfException if no segment is open on the channel
     */
    public void appendEquidistantSamples(int channelIndex, double[] samples) {
        Chan c = require(channelIndex);
        lockEquidistant(c);
        if (!c.segmentOpen) {
            throw new OsfException("channel " + channelIndex
                    + ": appendEquidistantSamples without an open segment "
                    + "(call startEquidistantSegment first)");
        }
        byte[] block = BlockEncoder.continuedDataBlock(
                channelIndex, new Block.DoubleValues(samples.clone()), c.def.sizeOfLengthValue());
        writeBlock(block);
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
        if (!c.segmentOpen) {
            throw new OsfException("channel " + channelIndex
                    + ": appendEquidistantSamples without an open segment "
                    + "(call startEquidistantSegment first)");
        }
        byte[] block = BlockEncoder.continuedDataBlock(
                channelIndex, new Block.FloatValues(samples.clone()), c.def.sizeOfLengthValue());
        writeBlock(block);
    }

    // ---------------------------------------------------------------
    // close.
    // ---------------------------------------------------------------

    /**
     * Flush, force and close the underlying file channel. Idempotent: a second
     * call is a no-op. If no preamble was written (no channels or no samples),
     * the file is still closed (leaving whatever bytes, if any, were written).
     *
     * @throws OsfException on I/O failure during the final force/close
     */
    @Override
    public void close() {
        if (phase == Phase.CLOSED) {
            return;
        }
        try {
            if (channel.isOpen()) {
                // Final durability barrier for anything pending (already forced
                // per block, but harmless and matches the reference's close()).
                if (phase == Phase.STREAMING) {
                    channel.force(true);
                }
                channel.close();
            }
        } catch (IOException e) {
            phase = Phase.CLOSED;
            throw new OsfException("failed to close OSF file: " + e.getMessage(), e);
        }
        phase = Phase.CLOSED;
    }

    // ---------------------------------------------------------------
    // Internals.
    // ---------------------------------------------------------------

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

    private void emitTimestampedNumeric(Chan c, long timestampNs, Block.Values value) {
        byte[] block = BlockEncoder.timestampedBlock(
                c.def.index(), new long[]{timestampNs}, value, c.def.sizeOfLengthValue());
        writeBlock(block);
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
}
