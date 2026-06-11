// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH
package com.optimeas.osf.internal;

import com.optimeas.osf.ChannelDef;
import com.optimeas.osf.ChannelType;
import com.optimeas.osf.DataType;

import java.io.ByteArrayOutputStream;
import java.nio.charset.StandardCharsets;
import java.util.LinkedHashMap;
import java.util.Map;

/**
 * Byte-sequence builders for {@link BlockReaderTest}.
 *
 * <p>Every helper produces the exact on-disk layout documented in
 * {@code docs/en/osf_general.md} and mirrored by the Rust reference
 * {@code implementations/rust/osf-core/src/{block,reader}.rs} and the C++
 * reference {@code implementations/cpp/src/{block,reader}.cpp}.
 *
 * <p>Per-block framing (little-endian throughout):
 * <pre>
 *   [u16 channelIndex][length field (sizeOfLengthValue bytes)][u8 control][body…]
 * </pre>
 * where {@code length} is the byte count of {@code control + body}.
 *
 * <p>Control byte: bit 7 is the multi-sample flag; bits 0–6 select the block
 * type (5=ContinuedData, 6=StartData, 7=ContinuedRelStampData,
 * 8=AbsTimeStampData; 0/2/≥9 reserved; 1/3/4 deprecated).
 */
final class BlockFixtures {
    private BlockFixtures() {}

    // Control-byte kind values (bits 0–6).
    static final int CTRL_RESERVED = 0;
    static final int CTRL_TRUSTED_TS = 1;
    static final int CTRL_TIMEBASE_REALIGN = 2;
    static final int CTRL_STATUS_EVENT = 3;
    static final int CTRL_MESSAGE_EVENT = 4;
    static final int CTRL_CONTINUED = 5;
    static final int CTRL_START = 6;
    static final int CTRL_CONTINUED_REL = 7;
    static final int CTRL_ABS_TS = 8;
    static final int MULTI_BIT = 0x80;

    /** Mutable little-endian byte builder. */
    static final class Buf {
        private final ByteArrayOutputStream out = new ByteArrayOutputStream();

        Buf u8(int v) { out.write(v & 0xFF); return this; }
        Buf u16(int v) { out.write(v & 0xFF); out.write((v >>> 8) & 0xFF); return this; }
        Buf u32(long v) {
            for (int i = 0; i < 4; i++) out.write((int) ((v >>> (8 * i)) & 0xFF));
            return this;
        }
        Buf i64(long v) {
            for (int i = 0; i < 8; i++) out.write((int) ((v >>> (8 * i)) & 0xFF));
            return this;
        }
        Buf i16(int v) { return u16(v); }
        Buf i32(int v) { return u32(v & 0xFFFFFFFFL); }
        Buf f32(float v) { return u32(Integer.toUnsignedLong(Float.floatToRawIntBits(v))); }
        Buf f64(double v) { return i64(Double.doubleToRawLongBits(v)); }
        Buf raw(byte[] b) { out.writeBytes(b); return this; }
        byte[] toBytes() { return out.toByteArray(); }
    }

    static Buf buf() { return new Buf(); }

    // ---------------------------------------------------------------
    // Channel-definition helpers
    // ---------------------------------------------------------------

    static ChannelDef channel(int index, DataType dt, int sizeOfLengthValue) {
        return new ChannelDef(index, "ch" + index, dt, ChannelType.SCALAR,
                sizeOfLengthValue, 0L, null, Map.of());
    }

    static ChannelDef channel(int index, DataType dt, ChannelType ct, int sizeOfLengthValue) {
        return new ChannelDef(index, "ch" + index, dt, ct,
                sizeOfLengthValue, 0L, null, Map.of());
    }

    static Map<Integer, ChannelDef> channelsByIndex(ChannelDef... defs) {
        Map<Integer, ChannelDef> m = new LinkedHashMap<>();
        for (ChannelDef d : defs) m.put(d.index(), d);
        return m;
    }

    /** A single {@code double} timestamped channel at the given index, sizeOfLengthValue=2. */
    static Map<Integer, ChannelDef> singleDoubleChannel(int index) {
        return channelsByIndex(channel(index, DataType.DOUBLE, 2));
    }

    // ---------------------------------------------------------------
    // Block builders. `body` is the bytes after the control byte; this
    // helper prepends [u16 index][length][u8 control] with the length
    // computed as 1 (control) + body.length, written at the requested
    // width.
    // ---------------------------------------------------------------

    static byte[] frame(int channelIndex, int sizeOfLengthValue, int control, byte[] body) {
        Buf b = buf().u16(channelIndex);
        int length = 1 + body.length;
        if (sizeOfLengthValue == 2) b.u16(length);
        else b.u32(length);
        b.u8(control);
        b.raw(body);
        return b.toBytes();
    }

    // --- bcAbsTimeStampData (control 8) ---

    /** Single-sample (bit 7 clear) absolute-timestamped i64. body = [i64 ts][i64 v]. */
    static byte[] absTsInt64Single(int idx, int sizeOfLengthValue, long ts, long value) {
        byte[] body = buf().i64(ts).i64(value).toBytes();
        return frame(idx, sizeOfLengthValue, CTRL_ABS_TS, body);
    }

    /** Multi-sample (bit 7 set) absolute-timestamped double. body = [u32 N][(i64 ts, f64 v)…]. */
    static byte[] absTsDoubleMulti(int idx, int sizeOfLengthValue, long[] ts, double[] vals) {
        Buf body = buf().u32(ts.length);
        for (int i = 0; i < ts.length; i++) body.i64(ts[i]).f64(vals[i]);
        return frame(idx, sizeOfLengthValue, CTRL_ABS_TS | MULTI_BIT, body.toBytes());
    }

    /** GPS single-sample (bit 7 clear). body = [i64 ts][f64 lat][f64 lon][f64 alt]. */
    static byte[] absTsGpsSingle(int idx, int sizeOfLengthValue, long ts,
                                 double lat, double lon, double alt) {
        byte[] body = buf().i64(ts).f64(lat).f64(lon).f64(alt).toBytes();
        return frame(idx, sizeOfLengthValue, CTRL_ABS_TS, body);
    }

    /**
     * String/binary absolute-timestamped, multi form (bit 7 set), N=1.
     * body = [u32 N=1][i64 ts][payload bytes]. The caller supplies the exact
     * on-disk payload bytes (including any trailing 0x00 for OSF4 fixtures).
     */
    static byte[] absTsStringOrBinaryMultiN1(int idx, int sizeOfLengthValue,
                                             long ts, byte[] onDiskPayload) {
        byte[] body = buf().u32(1).i64(ts).raw(onDiskPayload).toBytes();
        return frame(idx, sizeOfLengthValue, CTRL_ABS_TS | MULTI_BIT, body);
    }

    static byte[] utf8(String s) { return s.getBytes(StandardCharsets.UTF_8); }

    // --- bcStartData (control 6) ---

    /** Single-sample start (bit 7 clear). body = [i64 ts][f64 rate][f64 value]. */
    static byte[] startDoubleSingle(int idx, int sizeOfLengthValue,
                                    long ts, double rate, double value) {
        byte[] body = buf().i64(ts).f64(rate).f64(value).toBytes();
        return frame(idx, sizeOfLengthValue, CTRL_START, body);
    }

    /** Multi-sample start (bit 7 set). body = [i64 ts][f64 rate][u32 N][N×f32]. */
    static byte[] startFloatMulti(int idx, int sizeOfLengthValue,
                                  long ts, double rate, float[] values) {
        Buf body = buf().i64(ts).f64(rate).u32(values.length);
        for (float v : values) body.f32(v);
        return frame(idx, sizeOfLengthValue, CTRL_START | MULTI_BIT, body.toBytes());
    }

    // --- bcContinuedData (control 5) ---

    /** Multi-sample continued (bit 7 set). body = [u32 N][N×i16]. */
    static byte[] continuedInt16Multi(int idx, int sizeOfLengthValue, short[] values) {
        Buf body = buf().u32(values.length);
        for (short v : values) body.i16(v);
        return frame(idx, sizeOfLengthValue, CTRL_CONTINUED | MULTI_BIT, body.toBytes());
    }

    // --- bcContinuedRelStampData (control 7) ---

    /** Multi-sample rel-stamp (bit 7 set). body = [u32 N][(u32 delta, i16 v)…]. */
    static byte[] continuedRelInt16Multi(int idx, int sizeOfLengthValue,
                                         long[] deltas, short[] values) {
        Buf body = buf().u32(deltas.length);
        for (int i = 0; i < deltas.length; i++) body.u32(deltas[i]).i16(values[i]);
        return frame(idx, sizeOfLengthValue, CTRL_CONTINUED_REL | MULTI_BIT, body.toBytes());
    }

    // ---------------------------------------------------------------
    // Truncation helper: one valid block, then a partial trailing block
    // (a channel index + a length field, then fewer payload bytes than
    // the length promises). The reader must return the first block and
    // flag truncation without throwing.
    // ---------------------------------------------------------------

    static byte[] oneGoodBlockThenTruncated(int idx, int sizeOfLengthValue) {
        byte[] good = absTsInt64Single(idx, sizeOfLengthValue, 1000L, 7L);
        // Second block: index + length=10 (u16) but only 5 payload bytes.
        Buf b = buf().raw(good).u16(idx);
        if (sizeOfLengthValue == 2) b.u16(10); else b.u32(10);
        b.raw(new byte[]{8, 0, 0, 0, 0}); // 5 bytes < 10
        return b.toBytes();
    }
}
