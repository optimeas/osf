// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH
package com.optimeas.osf.internal;

import com.optimeas.osf.GpsLocation;

/**
 * One decoded block from an OSF block stream.
 *
 * <p>Mirrors the Rust {@code block::Block} / {@code BlockKind} and the C++
 * {@code osf::Block}. A {@code Block} is a sealed interface with one record per
 * control-byte variant; downstream assembly (J6) pattern-matches on the
 * concrete type. Every variant carries the {@link #channelIndex()} and enough
 * decoded data to assemble samples:
 *
 * <ul>
 *   <li>{@link AbsTimestampData} — per-sample absolute timestamps ({@code long[]}
 *       nanoseconds) plus typed {@link Values}.</li>
 *   <li>{@link StartData} — equidistant segment opener: absolute start timestamp
 *       (ns), sample rate (Hz), and the first typed numeric {@link Values}.</li>
 *   <li>{@link ContinuedData} — equidistant continuation: typed numeric
 *       {@link Values} only; timing derives from the channel's most recent
 *       {@code StartData}.</li>
 *   <li>{@link RelTimestampData} — OSF4-era per-sample relative deltas
 *       ({@code long[]} nanoseconds, from a {@code uint32} on the wire) plus
 *       typed numeric {@link Values}.</li>
 *   <li>{@link Skipped} — a block the reader consumed for stream alignment but
 *       did not interpret (unsupported channel, deprecated/reserved/unknown
 *       control byte, or zero-length block).</li>
 * </ul>
 *
 * <p>Typed sample values live in the {@link Values} sealed hierarchy: one
 * record per supported datatype holding a primitive array (or object array for
 * string/binary/GPS). The records are nested directly under {@code Block} for
 * concise call-site references (e.g. {@code Block.DoubleValues}) while still
 * implementing the {@link Values} marker that {@code block.values()} returns.
 */
public sealed interface Block
        permits Block.AbsTimestampData, Block.StartData, Block.ContinuedData,
                Block.RelTimestampData, Block.Skipped {

    /** Channel index (0..65535) this block belongs to. */
    int channelIndex();

    /**
     * {@code bcAbsTimeStampData} (control 8): each sample carries its own
     * absolute timestamp in nanoseconds. Supports every datatype, including
     * string/binary/GPS.
     *
     * @param channelIndex source channel
     * @param timestamps   per-sample absolute timestamps (ns); {@code length}
     *                     equals the sample count
     * @param values       typed sample values, parallel to {@code timestamps}
     */
    record AbsTimestampData(int channelIndex, long[] timestamps, Values values)
            implements Block {}

    /**
     * {@code bcStartData} (control 6): opens an equidistant segment. Numeric
     * datatypes only per spec.
     *
     * @param channelIndex      source channel
     * @param startTimestampNs  absolute start timestamp of the segment (ns)
     * @param sampleRateHz      sample rate (Hz), valid until the next
     *                          {@code StartData} of the same channel
     * @param values            typed numeric sample values
     */
    record StartData(int channelIndex, long startTimestampNs, double sampleRateHz,
                     Values values) implements Block {}

    /**
     * {@code bcContinuedData} (control 5): continuation of the current
     * equidistant segment. Time per sample is {@code 1 / sampleRateHz} of the
     * channel's most recent {@code StartData}. Numeric datatypes only.
     *
     * @param channelIndex source channel
     * @param values       typed numeric sample values
     */
    record ContinuedData(int channelIndex, Values values) implements Block {}

    /**
     * {@code bcContinuedRelStampData} (control 7): OSF4-era block where each
     * sample carries a relative time delta in nanoseconds (a {@code uint32} on
     * the wire, widened to {@code long} here). Readers support it; writers
     * never produce it. Numeric datatypes only.
     *
     * @param channelIndex source channel
     * @param deltasNs     per-sample relative deltas (ns), parallel to values
     * @param values       typed numeric sample values
     */
    record RelTimestampData(int channelIndex, long[] deltasNs, Values values)
            implements Block {}

    /**
     * A block the reader consumed for stream alignment but did not interpret.
     *
     * @param channelIndex source channel
     * @param reason       why the block was skipped
     * @param bytesSkipped number of payload bytes consumed (control byte +
     *                     body); zero for a zero-length block
     */
    record Skipped(int channelIndex, SkipReason reason, long bytesSkipped)
            implements Block {}

    /** Why a {@link Skipped} block was not interpreted. */
    enum SkipReason {
        /** Channel's data type is {@code UNSUPPORTED}. */
        UNSUPPORTED_DATA_TYPE,
        /** Channel's channel type is {@code UNSUPPORTED}. */
        UNSUPPORTED_CHANNEL_TYPE,
        /**
         * Deprecated control byte that newer writers no longer emit but readers
         * must tolerate (1 = {@code bcTrustedTimestamp}). {@code bcStatusEvent}
         * (3) has its own reason (see {@link #STATUS_EVENT_BLOCK}); {@code
         * bcMessageEvent} (4) is decoded rather than skipped in its specified
         * cases (OSF-UP4, DECISIONS §26).
         */
        DEPRECATED_BLOCK_TYPE,
        /**
         * A {@code bcStatusEvent} block (control byte 3). Skipped deliberately:
         * its payload is a fixed status word rather than a value of the
         * channel's declared datatype, so it is never a sample of that channel
         * (OSF-UP4, DECISIONS §26). Counted separately from
         * {@link #DEPRECATED_BLOCK_TYPE} so an occurrence stays visible.
         */
        STATUS_EVENT_BLOCK,
        /**
         * Reserved control byte (0 = {@code bcReserved}, 2 = {@code
         * bcTimebaseRealign}), any value ≥ 9 the spec does not currently
         * define, or one of {@code bcMessageEvent}'s (4) two unspecified
         * shapes: the multi-sample bit set, or a channel {@code dataType}
         * other than {@code STRING}/{@code BINARY} (OSF-UP4, DECISIONS §26).
         */
        RESERVED_BLOCK_TYPE,
        /** Frame CRC32C did not verify under an active integrity profile. */
        CRC_FAILED,
        /** Integrity signature block on the reserved channel {@code 0xFFFE}. */
        SIGNATURE_BLOCK,
        /**
         * The block's length field read {@code 0}. A conforming block always
         * carries at least its control byte, so this is a non-conforming writer
         * artefact (OSF-UP3, DECISIONS §25). The frame is nothing but the
         * channel index and the length field — both already consumed — so the
         * reader counts it and keeps scanning.
         */
        ZERO_LENGTH_BLOCK,
    }

    /**
     * Typed sample values for a block. One record per OSF datatype, mirroring
     * the reference's {@code NumericPayload} / {@code TimestampedPayload}
     * variants. Numeric variants hold unboxed primitive arrays; string, binary
     * and GPS use object arrays.
     *
     * <p>{@code bool} is stored as a {@code boolean[]}; the unsigned integer
     * types reuse the same-width signed Java primitive ({@code uint8}→{@code byte},
     * {@code uint16}→{@code short}, {@code uint32}→{@code int},
     * {@code uint64}→{@code long}) — callers reinterpret as unsigned via
     * {@code Byte.toUnsignedInt} / {@code Integer.toUnsignedLong} etc. The
     * concrete record type records the true on-wire type so no information is
     * lost (e.g. {@link UIntValues} vs {@link IntValues}).
     */
    sealed interface Values
            permits BoolValues, ByteValues, UByteValues, ShortValues, UShortValues,
                    IntValues, UIntValues, LongValues, ULongValues,
                    FloatValues, DoubleValues, StringValues, BinaryValues, GpsValues {
        /** Number of samples held. */
        int length();
    }

    record BoolValues(boolean[] values) implements Values {
        public int length() { return values.length; }
    }
    /** {@code int8}. */
    record ByteValues(byte[] values) implements Values {
        public int length() { return values.length; }
    }
    /** {@code uint8} — stored in a {@code byte[]}; reinterpret unsigned. */
    record UByteValues(byte[] values) implements Values {
        public int length() { return values.length; }
    }
    /** {@code int16}. */
    record ShortValues(short[] values) implements Values {
        public int length() { return values.length; }
    }
    /** {@code uint16} — stored in a {@code short[]}; reinterpret unsigned. */
    record UShortValues(short[] values) implements Values {
        public int length() { return values.length; }
    }
    /** {@code int32}. */
    record IntValues(int[] values) implements Values {
        public int length() { return values.length; }
    }
    /** {@code uint32} — stored in an {@code int[]}; reinterpret unsigned. */
    record UIntValues(int[] values) implements Values {
        public int length() { return values.length; }
    }
    /** {@code int64}. */
    record LongValues(long[] values) implements Values {
        public int length() { return values.length; }
    }
    /** {@code uint64} — stored in a {@code long[]}; reinterpret unsigned. */
    record ULongValues(long[] values) implements Values {
        public int length() { return values.length; }
    }
    record FloatValues(float[] values) implements Values {
        public int length() { return values.length; }
    }
    record DoubleValues(double[] values) implements Values {
        public int length() { return values.length; }
    }
    record StringValues(String[] values) implements Values {
        public int length() { return values.length; }
    }
    /** {@code binary} — one {@code byte[]} per sample. */
    record BinaryValues(byte[][] values) implements Values {
        public int length() { return values.length; }
    }
    record GpsValues(GpsLocation[] values) implements Values {
        public int length() { return values.length; }
    }
}
