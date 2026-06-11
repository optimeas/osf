// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH
package com.optimeas.osf;

import com.optimeas.osf.internal.Block;

import java.util.List;

/**
 * Typed, aggregated in-memory view of one OSF channel.
 *
 * <p>Where {@code internal.Block} is the per-block raw view from the block
 * reader, a {@code DataChannel} is the per-channel aggregate: the on-disk block
 * boundaries are gone and the samples appear as one flat run with parallel
 * absolute timestamps. This mirrors the Rust reference's {@code Channel} enum
 * ({@code implementations/rust/osf-core/src/data_channel.rs}); the three Rust
 * storage variants (equidistant / timestamped / variable) are collapsed here
 * into one class distinguished by {@link Kind}, with the underlying values held
 * in a {@link Block.Values} record so no datatype information is lost.
 *
 * <h2>How samples and timestamps are assembled</h2>
 * <ul>
 *   <li><b>Equidistant</b> ({@link Kind#EQUIDISTANT}) — a {@code bcStartData}
 *       block opens a {@link Segment} at its absolute {@code start_timestamp_ns}
 *       and {@code sample_rate_hz}; following {@code bcContinuedData} blocks
 *       extend the most recent segment. Each {@code bcStartData} opens a new
 *       segment. Timestamps are <em>reconstructed</em>: within a segment, sample
 *       {@code i} lands at
 *       {@code start + (long)(i * 1e9 / sample_rate_hz)} (truncated toward zero,
 *       saturating), per {@code segment_timestamp} in {@code data_channel.rs}.
 *       Time gaps <em>between</em> segments are not interpolated — each segment
 *       starts at its own {@code start_timestamp_ns}. {@link #timestampsNs()}
 *       returns the reconstructed timestamps concatenated across all
 *       segments.</li>
 *   <li><b>Timestamped</b> ({@link Kind#TIMESTAMPED}) — numeric/GPS channel.
 *       {@code bcAbsTimeStampData} carries explicit per-sample timestamps;
 *       {@code bcContinuedRelStampData} carries deltas that are folded to
 *       absolute against the last observed timestamp (anchor). Timestamps and
 *       values accumulate in parallel.</li>
 *   <li><b>Variable</b> ({@link Kind#VARIABLE}) — string/binary channel, always
 *       timestamped via {@code bcAbsTimeStampData}.</li>
 * </ul>
 *
 * <p>The accessors {@link #asDoubles()}, {@link #asLongs()}, {@link #asStrings()},
 * {@link #asBinaries()}, {@link #asGps()} and {@link #asBooleans()} project the
 * stored values; each throws {@link OsfException.UnsupportedType} when the
 * channel's {@link #dataType()} is not compatible with the requested view.
 */
public final class DataChannel {

    /** Storage layout discriminator. */
    public enum Kind {
        /** Equidistant numeric channel — reconstructed timestamps + segments. */
        EQUIDISTANT,
        /** Timestamped numeric/GPS channel — explicit parallel timestamps. */
        TIMESTAMPED,
        /** Timestamped string/binary channel. */
        VARIABLE
    }

    /**
     * One equidistant segment. Every {@code bcStartData} opens a new one with
     * its own absolute start time and sample rate.
     *
     * @param startTimestampNs absolute start timestamp of the segment (ns)
     * @param sampleRateHz     sample rate (Hz) valid until the next segment
     * @param startIndex       first sample index of this segment in the flat
     *                         value storage
     * @param sampleCount      number of samples in this segment
     */
    public record Segment(long startTimestampNs, double sampleRateHz,
                          int startIndex, int sampleCount) {}

    private final int index;
    private final String name;
    private final DataType dataType;
    private final ChannelType channelType;
    private final String physicalUnit;
    private final Kind kind;
    /** Absolute timestamps, parallel to the values; reconstructed for equidistant. */
    private final long[] timestampsNs;
    /** Flat sample storage; {@code null} only for an empty unsupported view. */
    private final Block.Values values;
    /** Segments for an equidistant channel; empty otherwise. */
    private final List<Segment> segments;

    /**
     * Constructor for the assembly layer; {@code
     * com.optimeas.osf.internal.ChannelAssembler} is the sole intended
     * producer. Although nominally {@code public} (the assembler lives in a
     * different, non-exported package), the {@link Block.Values} parameter type
     * is itself internal and not exported, so application code outside the
     * module cannot reference it. Validated invariant:
     * {@code timestampsNs.length == values.length()}.
     */
    public DataChannel(int index, String name, DataType dataType, ChannelType channelType,
                String physicalUnit, Kind kind, long[] timestampsNs,
                Block.Values values, List<Segment> segments) {
        this.index = index;
        this.name = name;
        this.dataType = dataType;
        this.channelType = channelType;
        this.physicalUnit = physicalUnit;
        this.kind = kind;
        this.timestampsNs = timestampsNs;
        this.values = values;
        this.segments = segments;
    }

    /** On-disk channel index (the metablock {@code "index"} attribute). */
    public int index() { return index; }

    /** Fully-qualified channel name. */
    public String name() { return name; }

    /** Resolved data type of the samples. */
    public DataType dataType() { return dataType; }

    /** Resolved channel type from the metablock. */
    public ChannelType channelType() { return channelType; }

    /** Physical unit string, or {@code null} when absent. */
    public String physicalUnit() { return physicalUnit; }

    /** Storage layout of this channel. */
    public Kind kind() { return kind; }

    /** Number of samples in this channel (summed across segments). */
    public long sampleCount() { return values == null ? 0 : values.length(); }

    /**
     * Absolute timestamps in nanoseconds, parallel to the values: explicit for
     * timestamped/variable channels, reconstructed (concatenated across
     * segments) for equidistant channels. The returned array is the channel's
     * own backing array — callers must not mutate it.
     */
    public long[] timestampsNs() { return timestampsNs; }

    /**
     * Read-only view of the equidistant segments; empty for non-equidistant
     * channels.
     */
    public List<Segment> segments() { return segments; }

    // -----------------------------------------------------------
    // Typed accessors. Each throws when the datatype is incompatible.
    // -----------------------------------------------------------

    /**
     * All samples widened to {@code double}. Valid for every numeric data type
     * ({@code bool}→0/1, all integer widths reinterpreted per their signedness,
     * {@code float}/{@code double}).
     *
     * @throws OsfException.UnsupportedType if the channel is not numeric
     */
    public double[] asDoubles() {
        return switch (values) {
            case Block.BoolValues v -> {
                double[] r = new double[v.values().length];
                for (int i = 0; i < r.length; i++) r[i] = v.values()[i] ? 1.0 : 0.0;
                yield r;
            }
            case Block.ByteValues v -> {
                double[] r = new double[v.values().length];
                for (int i = 0; i < r.length; i++) r[i] = v.values()[i];
                yield r;
            }
            case Block.UByteValues v -> {
                double[] r = new double[v.values().length];
                for (int i = 0; i < r.length; i++) r[i] = Byte.toUnsignedInt(v.values()[i]);
                yield r;
            }
            case Block.ShortValues v -> {
                double[] r = new double[v.values().length];
                for (int i = 0; i < r.length; i++) r[i] = v.values()[i];
                yield r;
            }
            case Block.UShortValues v -> {
                double[] r = new double[v.values().length];
                for (int i = 0; i < r.length; i++) r[i] = Short.toUnsignedInt(v.values()[i]);
                yield r;
            }
            case Block.IntValues v -> {
                double[] r = new double[v.values().length];
                for (int i = 0; i < r.length; i++) r[i] = v.values()[i];
                yield r;
            }
            case Block.UIntValues v -> {
                double[] r = new double[v.values().length];
                for (int i = 0; i < r.length; i++) r[i] = Integer.toUnsignedLong(v.values()[i]);
                yield r;
            }
            case Block.LongValues v -> {
                double[] r = new double[v.values().length];
                for (int i = 0; i < r.length; i++) r[i] = v.values()[i];
                yield r;
            }
            case Block.ULongValues v -> {
                double[] r = new double[v.values().length];
                for (int i = 0; i < r.length; i++) {
                    long x = v.values()[i];
                    // Unsigned 64-bit widened to double (matches Long's unsigned semantics).
                    r[i] = (x >= 0) ? (double) x
                            : ((double) (x >>> 1)) * 2.0 + (x & 1L);
                }
                yield r;
            }
            case Block.FloatValues v -> {
                double[] r = new double[v.values().length];
                for (int i = 0; i < r.length; i++) r[i] = v.values()[i];
                yield r;
            }
            case Block.DoubleValues v -> v.values().clone();
            default -> throw typeMismatch("double");
        };
    }

    /**
     * All samples as {@code long}. Valid for the integer data types
     * ({@code int8..int64}, {@code uint8..uint64}, {@code bool}); unsigned
     * values are zero-extended into the {@code long} (use
     * {@code Long.toUnsignedString} for {@code uint64}).
     *
     * @throws OsfException.UnsupportedType if the channel is not an integer type
     */
    public long[] asLongs() {
        return switch (values) {
            case Block.BoolValues v -> {
                long[] r = new long[v.values().length];
                for (int i = 0; i < r.length; i++) r[i] = v.values()[i] ? 1L : 0L;
                yield r;
            }
            case Block.ByteValues v -> {
                long[] r = new long[v.values().length];
                for (int i = 0; i < r.length; i++) r[i] = v.values()[i];
                yield r;
            }
            case Block.UByteValues v -> {
                long[] r = new long[v.values().length];
                for (int i = 0; i < r.length; i++) r[i] = Byte.toUnsignedInt(v.values()[i]);
                yield r;
            }
            case Block.ShortValues v -> {
                long[] r = new long[v.values().length];
                for (int i = 0; i < r.length; i++) r[i] = v.values()[i];
                yield r;
            }
            case Block.UShortValues v -> {
                long[] r = new long[v.values().length];
                for (int i = 0; i < r.length; i++) r[i] = Short.toUnsignedInt(v.values()[i]);
                yield r;
            }
            case Block.IntValues v -> {
                long[] r = new long[v.values().length];
                for (int i = 0; i < r.length; i++) r[i] = v.values()[i];
                yield r;
            }
            case Block.UIntValues v -> {
                long[] r = new long[v.values().length];
                for (int i = 0; i < r.length; i++) r[i] = Integer.toUnsignedLong(v.values()[i]);
                yield r;
            }
            case Block.LongValues v -> v.values().clone();
            case Block.ULongValues v -> v.values().clone(); // raw bits; unsigned interpretation up to caller
            default -> throw typeMismatch("long");
        };
    }

    /**
     * All samples as {@code boolean}. Valid only for a {@code bool} channel.
     *
     * @throws OsfException.UnsupportedType if the channel is not {@code bool}
     */
    public boolean[] asBooleans() {
        if (values instanceof Block.BoolValues v) {
            return v.values().clone();
        }
        throw typeMismatch("boolean");
    }

    /**
     * All samples as {@code String}. Valid only for a {@code string} channel.
     *
     * @throws OsfException.UnsupportedType if the channel is not {@code string}
     */
    public String[] asStrings() {
        if (values instanceof Block.StringValues v) {
            return v.values().clone();
        }
        throw typeMismatch("string");
    }

    /**
     * All samples as {@code byte[]}. Valid only for a {@code binary} channel.
     *
     * @throws OsfException.UnsupportedType if the channel is not {@code binary}
     */
    public byte[][] asBinaries() {
        if (values instanceof Block.BinaryValues v) {
            return v.values().clone();
        }
        throw typeMismatch("binary");
    }

    /**
     * All samples as {@link GpsLocation}. Valid only for a {@code gpslocation}
     * channel.
     *
     * @throws OsfException.UnsupportedType if the channel is not GPS
     */
    public GpsLocation[] asGps() {
        if (values instanceof Block.GpsValues v) {
            return v.values().clone();
        }
        throw typeMismatch("gpslocation");
    }

    private OsfException.UnsupportedType typeMismatch(String requested) {
        return new OsfException.UnsupportedType(
                "channel " + index + " (" + name + ") holds " + dataType
                + " samples; cannot view them as " + requested);
    }
}
