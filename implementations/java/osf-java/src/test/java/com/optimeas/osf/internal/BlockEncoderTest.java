// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH
package com.optimeas.osf.internal;

import com.optimeas.osf.ChannelDef;
import com.optimeas.osf.ChannelType;
import com.optimeas.osf.DataType;
import com.optimeas.osf.GpsLocation;
import com.optimeas.osf.OsfVersion;
import com.optimeas.osf.ReaderStats;
import org.junit.jupiter.api.Test;

import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

import static org.assertj.core.api.Assertions.assertThat;

/**
 * TDD tests for {@link BlockEncoder}.
 *
 * <p>The encoder's contract is pinned by <em>decoding</em> its output with the
 * trusted J5 {@link BlockReader} and asserting the decoded {@link Block} equals
 * the inputs. This proves the on-disk framing, control bytes, the bit-7 /
 * N-prefix selection, and the OSF5 (no null-terminator) string/binary layout
 * all match the reference reader. Float comparisons are bitwise.
 *
 * <p>Block framing mirrored from the reference encoder
 * {@code implementations/cpp/src/block_encode.cpp} and
 * {@code implementations/rust/osf-core/src/writer.rs}.
 */
class BlockEncoderTest {

    private static ChannelDef ch(int index, DataType dt, int sov) {
        return new ChannelDef(index, "ch" + index, dt, ChannelType.SCALAR,
                sov, 0L, null, Map.of());
    }

    private static Map<Integer, ChannelDef> channels(ChannelDef... defs) {
        Map<Integer, ChannelDef> m = new LinkedHashMap<>();
        for (ChannelDef d : defs) m.put(d.index(), d);
        return m;
    }

    private static List<Block> decode(byte[] encoded, ChannelDef def) {
        return BlockReader.readAll(encoded, OsfVersion.OSF5,
                channels(def), new ReaderStats());
    }

    // ---------------------------------------------------------------
    // Timestamped numeric (bcAbsTimeStampData)
    // ---------------------------------------------------------------

    @Test
    void timestampedDoubleMultiSampleRoundTrips() {
        ChannelDef def = ch(0, DataType.DOUBLE, 2);
        long[] ts = {1000L, 2000L, 3000L};
        double[] vals = {1.5, -2.25, 3.0e10};
        byte[] enc = BlockEncoder.timestampedBlock(0, ts,
                new Block.DoubleValues(vals), 2);

        List<Block> blocks = decode(enc, def);
        assertThat(blocks).hasSize(1);
        Block.AbsTimestampData b = (Block.AbsTimestampData) blocks.get(0);
        assertThat(b.channelIndex()).isEqualTo(0);
        assertThat(b.timestamps()).containsExactly(ts);
        double[] got = ((Block.DoubleValues) b.values()).values();
        assertThat(got).hasSize(3);
        for (int i = 0; i < vals.length; i++) {
            assertThat(Double.doubleToLongBits(got[i]))
                    .isEqualTo(Double.doubleToLongBits(vals[i]));
        }
    }

    @Test
    void timestampedSingleSampleUsesBit7ClearForm() {
        ChannelDef def = ch(0, DataType.INT64, 2);
        long[] ts = {42L};
        long[] vals = {-7L};
        byte[] enc = BlockEncoder.timestampedBlock(0, ts,
                new Block.LongValues(vals), 2);

        // Single sample → bit-7 clear, no u32 N prefix. Verify the control byte
        // directly: [u16 idx][u16 len][ctrl=0x08]...
        assertThat(enc[4] & 0xFF).isEqualTo(0x08);

        Block.AbsTimestampData b = (Block.AbsTimestampData) decode(enc, def).get(0);
        assertThat(b.timestamps()).containsExactly(ts);
        assertThat(((Block.LongValues) b.values()).values()).containsExactly(vals);
    }

    @Test
    void timestampedInt16RoundTrips() {
        ChannelDef def = ch(3, DataType.INT16, 2);
        long[] ts = {10L, 20L};
        short[] vals = {-300, 12000};
        byte[] enc = BlockEncoder.timestampedBlock(3, ts,
                new Block.ShortValues(vals), 2);
        Block.AbsTimestampData b = (Block.AbsTimestampData) decode(enc, def).get(0);
        assertThat(b.timestamps()).containsExactly(ts);
        assertThat(((Block.ShortValues) b.values()).values()).containsExactly(vals);
    }

    @Test
    void timestampedBoolRoundTrips() {
        ChannelDef def = ch(1, DataType.BOOL, 2);
        long[] ts = {5L, 6L, 7L};
        boolean[] vals = {true, false, true};
        byte[] enc = BlockEncoder.timestampedBlock(1, ts,
                new Block.BoolValues(vals), 2);
        Block.AbsTimestampData b = (Block.AbsTimestampData) decode(enc, def).get(0);
        assertThat(b.timestamps()).containsExactly(ts);
        assertThat(((Block.BoolValues) b.values()).values()).containsExactly(vals);
    }

    @Test
    void timestampedFloatRoundTrips() {
        ChannelDef def = ch(2, DataType.FLOAT, 2);
        long[] ts = {1L, 2L};
        float[] vals = {1.25f, -8.5f};
        byte[] enc = BlockEncoder.timestampedBlock(2, ts,
                new Block.FloatValues(vals), 2);
        Block.AbsTimestampData b = (Block.AbsTimestampData) decode(enc, def).get(0);
        float[] got = ((Block.FloatValues) b.values()).values();
        for (int i = 0; i < vals.length; i++) {
            assertThat(Float.floatToIntBits(got[i]))
                    .isEqualTo(Float.floatToIntBits(vals[i]));
        }
    }

    @Test
    void timestampedGpsRoundTrips() {
        ChannelDef def = ch(0, DataType.GPS_LOCATION, 2);
        long[] ts = {100L, 200L};
        GpsLocation[] vals = {
                new GpsLocation(48.1, 11.5, 520.0),
                new GpsLocation(-33.8688, 151.2093, 19.5),
        };
        byte[] enc = BlockEncoder.timestampedGpsBlock(0, ts, vals, 2);
        Block.AbsTimestampData b = (Block.AbsTimestampData) decode(enc, def).get(0);
        assertThat(b.timestamps()).containsExactly(ts);
        GpsLocation[] got = ((Block.GpsValues) b.values()).values();
        assertThat(got).containsExactly(vals);
    }

    // ---------------------------------------------------------------
    // Equidistant (bcStartData + bcContinuedData)
    // ---------------------------------------------------------------

    @Test
    void startDataMultiSampleRoundTrips() {
        ChannelDef def = ch(0, DataType.DOUBLE, 2);
        double[] vals = {1.0, 2.0, 3.0, 4.0};
        byte[] enc = BlockEncoder.startDataBlock(0, 5000L, 1000.0,
                new Block.DoubleValues(vals), 2);
        Block.StartData b = (Block.StartData) decode(enc, def).get(0);
        assertThat(b.startTimestampNs()).isEqualTo(5000L);
        assertThat(Double.doubleToLongBits(b.sampleRateHz()))
                .isEqualTo(Double.doubleToLongBits(1000.0));
        assertThat(((Block.DoubleValues) b.values()).values()).containsExactly(vals);
    }

    @Test
    void startDataSingleSampleUsesBit7ClearForm() {
        ChannelDef def = ch(0, DataType.FLOAT, 2);
        float[] vals = {7.5f};
        byte[] enc = BlockEncoder.startDataBlock(0, 1L, 100.0,
                new Block.FloatValues(vals), 2);
        // [u16 idx][u16 len][ctrl=0x06]
        assertThat(enc[4] & 0xFF).isEqualTo(0x06);
        Block.StartData b = (Block.StartData) decode(enc, def).get(0);
        assertThat(b.startTimestampNs()).isEqualTo(1L);
        assertThat(((Block.FloatValues) b.values()).values()).containsExactly(vals);
    }

    @Test
    void continuedDataMultiSampleRoundTrips() {
        ChannelDef def = ch(0, DataType.FLOAT, 2);
        float[] vals = {10.0f, 20.0f, 30.0f};
        byte[] enc = BlockEncoder.continuedDataBlock(0,
                new Block.FloatValues(vals), 2);
        Block.ContinuedData b = (Block.ContinuedData) decode(enc, def).get(0);
        assertThat(((Block.FloatValues) b.values()).values()).containsExactly(vals);
    }

    @Test
    void continuedDataSingleSampleUsesBit7ClearForm() {
        ChannelDef def = ch(0, DataType.DOUBLE, 2);
        double[] vals = {99.0};
        byte[] enc = BlockEncoder.continuedDataBlock(0,
                new Block.DoubleValues(vals), 2);
        assertThat(enc[4] & 0xFF).isEqualTo(0x05);
        Block.ContinuedData b = (Block.ContinuedData) decode(enc, def).get(0);
        assertThat(((Block.DoubleValues) b.values()).values()).containsExactly(vals);
    }

    // ---------------------------------------------------------------
    // Variable string / binary (single sample per block, OSF5 no terminator)
    // ---------------------------------------------------------------

    @Test
    void variableStringRoundTripsWithoutTerminator() {
        ChannelDef def = ch(0, DataType.STRING, 2);
        byte[] enc = BlockEncoder.variableStringBlock(0, 1234L, "hello", 2);
        // Single-sample variable form: bit-7 clear control byte.
        assertThat(enc[4] & 0xFF).isEqualTo(0x08);
        // OSF5: no trailing 0x00 — payload length = "hello".length().
        // [u16 idx][u16 len][ctrl][i64 ts][bytes]; len = 1 + 8 + 5 = 14.
        assertThat(enc[2] & 0xFF).isEqualTo(14);

        Block.AbsTimestampData b = (Block.AbsTimestampData) decode(enc, def).get(0);
        assertThat(b.timestamps()).containsExactly(1234L);
        assertThat(((Block.StringValues) b.values()).values()).containsExactly("hello");
    }

    @Test
    void variableStringEndingInZeroIsPreservedVerbatim() {
        ChannelDef def = ch(0, DataType.STRING, 2);
        // A string with content is enough; verify a binary payload that ends
        // in 0x00 is not stripped via the binary test below.
        byte[] enc = BlockEncoder.variableStringBlock(0, 1L, "", 2);
        // Empty string → payload length = 1 + 8 + 0 = 9.
        assertThat(enc[2] & 0xFF).isEqualTo(9);
        Block.AbsTimestampData b = (Block.AbsTimestampData) decode(enc, def).get(0);
        assertThat(((Block.StringValues) b.values()).values()).containsExactly("");
    }

    @Test
    void variableBinaryRoundTripsVerbatim() {
        ChannelDef def = ch(0, DataType.BINARY, 2);
        byte[] payload = {0x01, 0x02, (byte) 0xFF, 0x00};
        byte[] enc = BlockEncoder.variableBinaryBlock(0, 9999L, payload, 2);
        Block.AbsTimestampData b = (Block.AbsTimestampData) decode(enc, def).get(0);
        assertThat(b.timestamps()).containsExactly(9999L);
        byte[][] got = ((Block.BinaryValues) b.values()).values();
        assertThat(got.length).isEqualTo(1);
        // Trailing 0x00 preserved (OSF5 has no null-terminator stripping).
        assertThat(got[0]).containsExactly(payload);
    }

    @Test
    void variableBinaryBumpsToSizeOfLengthValue4() {
        ChannelDef def = ch(0, DataType.BINARY, 4);
        byte[] payload = new byte[70_000]; // > u16 length field
        for (int i = 0; i < payload.length; i++) payload[i] = (byte) i;
        byte[] enc = BlockEncoder.variableBinaryBlock(0, 1L, payload, 4);
        Block.AbsTimestampData b = (Block.AbsTimestampData) decode(enc, def).get(0);
        byte[][] got = ((Block.BinaryValues) b.values()).values();
        assertThat(got[0]).containsExactly(payload);
    }
}
