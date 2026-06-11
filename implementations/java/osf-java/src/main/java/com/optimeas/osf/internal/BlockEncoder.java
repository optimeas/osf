// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH
package com.optimeas.osf.internal;

import com.optimeas.osf.GpsLocation;
import com.optimeas.osf.OsfException;

import java.io.ByteArrayOutputStream;
import java.nio.charset.StandardCharsets;

/**
 * Internal write-side mirror of {@link BlockReader}: encodes one complete OSF5
 * data block (channel-index frame + length field + control byte + body) into a
 * {@code byte[]}.
 *
 * <p>This is a faithful port of the reference encoders
 * {@code implementations/cpp/src/block_encode.cpp} and
 * {@code implementations/rust/osf-core/src/writer.rs}; control-byte values, the
 * bit-7 / N-prefix rule, {@code sizeOfLengthValue} handling, and the OSF5
 * no-null-terminator rule for string / binary payloads are taken verbatim from
 * those and {@code docs/en/osf_general.md}.
 *
 * <h2>Per-block framing (all little-endian)</h2>
 * <pre>
 *   [u16 channelIndex][length field (sizeOfLengthValue bytes)][u8 control][body…]
 * </pre>
 * where {@code length} is the byte count of {@code control + body}.
 *
 * <h2>Control bytes (bits 0–6) + multi flag (bit 7)</h2>
 * <ul>
 *   <li>{@code 0x05} bcContinuedData (equidistant continuation)</li>
 *   <li>{@code 0x06} bcStartData (equidistant opener: i64 ts + f64 rate)</li>
 *   <li>{@code 0x08} bcAbsTimeStampData (per-sample i64 timestamps)</li>
 *   <li>{@code 0x80} multi-sample flag — set iff {@code count != 1}; when set
 *       the body begins with a {@code u32} sample count N, otherwise exactly one
 *       sample follows with no N prefix.</li>
 * </ul>
 *
 * <p>String and binary samples are emitted one per block in the single-sample
 * compact form (bit-7 clear, no N prefix) and written verbatim — OSF5 appends
 * <em>no</em> trailing {@code 0x00} (spec rev 2026-05-24). This matches the
 * reference writers, so byte-identity against the OSF5 corpus is achievable.
 */
public final class BlockEncoder {

    // Control-byte kind values (bits 0–6) — confirmed against block.rs /
    // block_encode.cpp.
    private static final int CTRL_CONTINUED = 0x05;
    private static final int CTRL_START = 0x06;
    private static final int CTRL_ABS_TS = 0x08;
    private static final int MULTI_BIT = 0x80;

    private BlockEncoder() {}

    // ---------------------------------------------------------------
    // bcAbsTimeStampData — timestamped numeric (and GPS)
    // ---------------------------------------------------------------

    /**
     * Encode a {@code bcAbsTimeStampData} block carrying per-sample i64
     * timestamps interleaved with typed numeric values.
     *
     * @param channelIndex    channel index (0..65535)
     * @param timestampsNs    per-sample absolute timestamps (ns)
     * @param values          typed numeric values, parallel to timestamps;
     *                        must be a numeric {@link Block.Values} variant
     *                        (not string/binary/GPS)
     * @param sizeOfLengthValue length-field width on disk (2 or 4)
     * @return one complete block
     */
    public static byte[] timestampedBlock(int channelIndex, long[] timestampsNs,
                                          Block.Values values, int sizeOfLengthValue) {
        int count = values.length();
        if (timestampsNs.length != count) {
            throw new OsfException.MalformedFile(
                    "timestampedBlock: timestamps.length=" + timestampsNs.length
                    + " != values.length=" + count);
        }
        boolean multi = count != 1;
        Body body = new Body();
        body.u8(CTRL_ABS_TS | (multi ? MULTI_BIT : 0));
        if (multi) {
            body.u32(count);
        }
        for (int i = 0; i < count; i++) {
            body.i64(timestampsNs[i]);
            appendNumericSample(body, values, i);
        }
        return frame(channelIndex, sizeOfLengthValue, body);
    }

    /**
     * Encode a {@code bcAbsTimeStampData} block of GPS locations. Each sample is
     * {@code [i64 ts][f64 lat][f64 lon][f64 alt]} (24-byte location).
     */
    public static byte[] timestampedGpsBlock(int channelIndex, long[] timestampsNs,
                                             GpsLocation[] values, int sizeOfLengthValue) {
        int count = values.length;
        if (timestampsNs.length != count) {
            throw new OsfException.MalformedFile(
                    "timestampedGpsBlock: timestamps.length=" + timestampsNs.length
                    + " != values.length=" + count);
        }
        boolean multi = count != 1;
        Body body = new Body();
        body.u8(CTRL_ABS_TS | (multi ? MULTI_BIT : 0));
        if (multi) {
            body.u32(count);
        }
        for (int i = 0; i < count; i++) {
            body.i64(timestampsNs[i]);
            body.f64(values[i].latitude());
            body.f64(values[i].longitude());
            body.f64(values[i].altitude());
        }
        return frame(channelIndex, sizeOfLengthValue, body);
    }

    // ---------------------------------------------------------------
    // bcStartData / bcContinuedData — equidistant (float / double only)
    // ---------------------------------------------------------------

    /**
     * Encode a {@code bcStartData} block: an equidistant segment opener carrying
     * the absolute start timestamp, the sample rate, and the first numeric
     * samples. Spec rev 2026-05-04: equidistant data is {@code float} /
     * {@code double} only.
     */
    public static byte[] startDataBlock(int channelIndex, long startTimestampNs,
                                        double sampleRateHz, Block.Values values,
                                        int sizeOfLengthValue) {
        requireFloatOrDouble(values, "startDataBlock");
        int count = values.length();
        boolean multi = count != 1;
        Body body = new Body();
        body.u8(CTRL_START | (multi ? MULTI_BIT : 0));
        body.i64(startTimestampNs);
        body.f64(sampleRateHz);
        if (multi) {
            body.u32(count);
        }
        for (int i = 0; i < count; i++) {
            appendNumericSample(body, values, i);
        }
        return frame(channelIndex, sizeOfLengthValue, body);
    }

    /**
     * Encode a {@code bcContinuedData} block: a continuation of the current
     * equidistant segment (numeric values only; timing derives from the
     * channel's most recent {@code bcStartData}).
     */
    public static byte[] continuedDataBlock(int channelIndex, Block.Values values,
                                            int sizeOfLengthValue) {
        requireFloatOrDouble(values, "continuedDataBlock");
        int count = values.length();
        boolean multi = count != 1;
        Body body = new Body();
        body.u8(CTRL_CONTINUED | (multi ? MULTI_BIT : 0));
        if (multi) {
            body.u32(count);
        }
        for (int i = 0; i < count; i++) {
            appendNumericSample(body, values, i);
        }
        return frame(channelIndex, sizeOfLengthValue, body);
    }

    // ---------------------------------------------------------------
    // bcAbsTimeStampData — variable string / binary, one sample per block
    // ---------------------------------------------------------------

    /**
     * Encode a single-sample {@code string} block:
     * {@code [control=0x08][i64 ts][utf8 bytes]} — bit-7 clear, no N prefix,
     * OSF5 no trailing {@code 0x00}.
     */
    public static byte[] variableStringBlock(int channelIndex, long timestampNs,
                                             String value, int sizeOfLengthValue) {
        byte[] utf8 = value.getBytes(StandardCharsets.UTF_8);
        return variableBinaryBlock(channelIndex, timestampNs, utf8, sizeOfLengthValue);
    }

    /**
     * Encode a single-sample {@code binary} block:
     * {@code [control=0x08][i64 ts][bytes]} — bit-7 clear, no N prefix, payload
     * written verbatim (OSF5 has no null terminator; a trailing {@code 0x00} is
     * legitimate content and is preserved).
     */
    public static byte[] variableBinaryBlock(int channelIndex, long timestampNs,
                                             byte[] value, int sizeOfLengthValue) {
        Body body = new Body();
        body.u8(CTRL_ABS_TS); // single-sample form: bit-7 clear, no N prefix
        body.i64(timestampNs);
        body.raw(value);
        return frame(channelIndex, sizeOfLengthValue, body);
    }

    // ---------------------------------------------------------------
    // Framing + numeric dispatch
    // ---------------------------------------------------------------

    /**
     * Prepend the {@code [u16 channelIndex][length][...body]} frame. The length
     * field is {@code sizeOfLengthValue} bytes wide and counts the body bytes
     * (control byte + payload).
     */
    private static byte[] frame(int channelIndex, int sizeOfLengthValue, Body body) {
        if (sizeOfLengthValue != 2 && sizeOfLengthValue != 4) {
            throw new OsfException.MalformedFile(
                    "sizeOfLengthValue must be 2 or 4, got " + sizeOfLengthValue);
        }
        byte[] payload = body.toBytes();
        long max = (sizeOfLengthValue == 2) ? 0xFFFFL : 0xFFFFFFFFL;
        if (payload.length > max) {
            throw new OsfException.MalformedFile(
                    "block payload " + payload.length
                    + " bytes too large for sizeOfLengthValue=" + sizeOfLengthValue);
        }
        Body out = new Body();
        out.u16(channelIndex);
        if (sizeOfLengthValue == 2) {
            out.u16(payload.length);
        } else {
            out.u32(payload.length);
        }
        out.raw(payload);
        return out.toBytes();
    }

    private static void requireFloatOrDouble(Block.Values values, String where) {
        if (!(values instanceof Block.FloatValues) && !(values instanceof Block.DoubleValues)) {
            throw new OsfException.MalformedFile(
                    where + ": equidistant blocks support only float and double "
                    + "(spec rev 2026-05-04), got " + values.getClass().getSimpleName());
        }
    }

    /** Append one little-endian sample of the numeric {@code values} at slot {@code i}. */
    private static void appendNumericSample(Body body, Block.Values values, int i) {
        if (values instanceof Block.BoolValues v) {
            body.u8(v.values()[i] ? 1 : 0);
        } else if (values instanceof Block.ByteValues v) {
            body.u8(v.values()[i]);
        } else if (values instanceof Block.UByteValues v) {
            body.u8(v.values()[i]);
        } else if (values instanceof Block.ShortValues v) {
            body.i16(v.values()[i]);
        } else if (values instanceof Block.UShortValues v) {
            body.i16(v.values()[i]);
        } else if (values instanceof Block.IntValues v) {
            body.i32(v.values()[i]);
        } else if (values instanceof Block.UIntValues v) {
            body.i32(v.values()[i]);
        } else if (values instanceof Block.LongValues v) {
            body.i64(v.values()[i]);
        } else if (values instanceof Block.ULongValues v) {
            body.i64(v.values()[i]);
        } else if (values instanceof Block.FloatValues v) {
            body.f32(v.values()[i]);
        } else if (values instanceof Block.DoubleValues v) {
            body.f64(v.values()[i]);
        } else {
            throw new OsfException.MalformedFile(
                    "numeric block does not support value type "
                    + values.getClass().getSimpleName());
        }
    }

    /** Mutable little-endian byte builder. Mirrors the reference binary writers. */
    private static final class Body {
        private final ByteArrayOutputStream out = new ByteArrayOutputStream();

        void u8(int v) { out.write(v & 0xFF); }
        void u16(int v) { out.write(v & 0xFF); out.write((v >>> 8) & 0xFF); }
        void u32(long v) {
            for (int i = 0; i < 4; i++) out.write((int) ((v >>> (8 * i)) & 0xFF));
        }
        void i16(short v) { u16(v & 0xFFFF); }
        void i32(int v) { u32(v & 0xFFFFFFFFL); }
        void i64(long v) {
            for (int i = 0; i < 8; i++) out.write((int) ((v >>> (8 * i)) & 0xFF));
        }
        void f32(float v) { u32(Integer.toUnsignedLong(Float.floatToRawIntBits(v))); }
        void f64(double v) { i64(Double.doubleToRawLongBits(v)); }
        void raw(byte[] b) { out.writeBytes(b); }

        byte[] toBytes() { return out.toByteArray(); }
    }
}
