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

import java.util.List;
import java.util.Map;

import static com.optimeas.osf.internal.BlockFixtures.*;
import static org.junit.jupiter.api.Assertions.*;

/**
 * Control-byte-path coverage for {@link BlockReader}. Each test exercises one
 * decode path verified against the Rust/C++ reference implementations.
 */
class BlockReaderTest {

    private static ReaderStats stats() { return new ReaderStats(); }

    // ---------------------------------------------------------------
    // bcAbsTimeStampData (control 8)
    // ---------------------------------------------------------------

    @Test
    void singleSampleAbsTimestampInt64() {
        var channels = channelsByIndex(channel(0, DataType.INT64, 2));
        byte[] data = absTsInt64Single(0, 2, 0x18ACBBA95F76EC57L, 0L);
        ReaderStats st = stats();
        List<Block> blocks = BlockReader.readAll(data, OsfVersion.OSF5, channels, st);

        assertEquals(1, blocks.size());
        assertEquals(1, st.blocksRead());
        assertFalse(st.truncationSeen());
        var b = (Block.AbsTimestampData) blocks.get(0);
        assertEquals(0, b.channelIndex());
        assertArrayEquals(new long[]{0x18ACBBA95F76EC57L}, b.timestamps());
        var vals = (Block.LongValues) b.values();
        assertArrayEquals(new long[]{0L}, vals.values());
    }

    @Test
    void multiSampleAbsTimestampDouble() {
        var channels = singleDoubleChannel(0);
        byte[] data = absTsDoubleMulti(0, 2, new long[]{100, 200}, new double[]{1.5, 2.5});
        ReaderStats st = stats();
        List<Block> blocks = BlockReader.readAll(data, OsfVersion.OSF5, channels, st);

        assertEquals(1, blocks.size());
        var b = (Block.AbsTimestampData) blocks.get(0);
        assertArrayEquals(new long[]{100, 200}, b.timestamps());
        var vals = (Block.DoubleValues) b.values();
        assertArrayEquals(new double[]{1.5, 2.5}, vals.values());
    }

    @Test
    void absTimestampGpsLocation() {
        var channels = channelsByIndex(channel(0, DataType.GPS_LOCATION, 2));
        byte[] data = absTsGpsSingle(0, 2, 999L, 48.1374, 11.5755, 519.0);
        ReaderStats st = stats();
        List<Block> blocks = BlockReader.readAll(data, OsfVersion.OSF5, channels, st);

        var b = (Block.AbsTimestampData) blocks.get(0);
        assertArrayEquals(new long[]{999L}, b.timestamps());
        var vals = (Block.GpsValues) b.values();
        assertEquals(1, vals.values().length);
        GpsLocation g = vals.values()[0];
        assertEquals(48.1374, g.latitude(), 1e-9);
        assertEquals(11.5755, g.longitude(), 1e-9);
        assertEquals(519.0, g.altitude(), 1e-9);
    }

    // --- string/binary null-terminator rule ---

    @Test
    void absTimestampStringOsf5KeepsPayloadVerbatim() {
        var channels = channelsByIndex(channel(0, DataType.STRING, 4));
        byte[] data = absTsStringOrBinaryMultiN1(0, 4, 42L, utf8("hi"));
        ReaderStats st = stats();
        List<Block> blocks = BlockReader.readAll(data, OsfVersion.OSF5, channels, st);

        var b = (Block.AbsTimestampData) blocks.get(0);
        assertArrayEquals(new long[]{42L}, b.timestamps());
        var vals = (Block.StringValues) b.values();
        assertArrayEquals(new String[]{"hi"}, vals.values());
    }

    @Test
    void absTimestampStringOsf4StripsTrailingNull() {
        var channels = channelsByIndex(channel(0, DataType.STRING, 4));
        // On disk OSF4: "hi" + 0x00.
        byte[] onDisk = buf().raw(utf8("hi")).u8(0).toBytes();
        byte[] data = absTsStringOrBinaryMultiN1(0, 4, 42L, onDisk);
        ReaderStats st = stats();
        List<Block> blocks = BlockReader.readAll(data, OsfVersion.OSF4, channels, st);

        var b = (Block.AbsTimestampData) blocks.get(0);
        var vals = (Block.StringValues) b.values();
        assertArrayEquals(new String[]{"hi"}, vals.values());
    }

    @Test
    void absTimestampBinaryOsf5KeepsTrailingNullByte() {
        var channels = channelsByIndex(channel(0, DataType.BINARY, 4));
        byte[] onDisk = {(byte) 0xFF, (byte) 0xD8, (byte) 0xFF, (byte) 0xE0, 0x00};
        byte[] data = absTsStringOrBinaryMultiN1(0, 4, 123L, onDisk);
        ReaderStats st = stats();
        List<Block> blocks = BlockReader.readAll(data, OsfVersion.OSF5, channels, st);

        var b = (Block.AbsTimestampData) blocks.get(0);
        var vals = (Block.BinaryValues) b.values();
        assertArrayEquals(new byte[]{(byte) 0xFF, (byte) 0xD8, (byte) 0xFF, (byte) 0xE0, 0x00},
                vals.values()[0]);
    }

    @Test
    void absTimestampBinaryOsf4StripsTrailingNullByte() {
        var channels = channelsByIndex(channel(0, DataType.BINARY, 4));
        byte[] onDisk = {(byte) 0xFF, (byte) 0xD8, (byte) 0xFF, (byte) 0xE0, 0x00};
        byte[] data = absTsStringOrBinaryMultiN1(0, 4, 123L, onDisk);
        ReaderStats st = stats();
        List<Block> blocks = BlockReader.readAll(data, OsfVersion.OSF4, channels, st);

        var b = (Block.AbsTimestampData) blocks.get(0);
        var vals = (Block.BinaryValues) b.values();
        assertArrayEquals(new byte[]{(byte) 0xFF, (byte) 0xD8, (byte) 0xFF, (byte) 0xE0},
                vals.values()[0]);
    }

    // ---------------------------------------------------------------
    // bcStartData (control 6) + bcContinuedData (control 5)
    // ---------------------------------------------------------------

    @Test
    void startDataSingleSampleDouble() {
        var channels = singleDoubleChannel(0);
        byte[] data = startDoubleSingle(0, 2, 1_000_000L, 1000.0, 2.5);
        ReaderStats st = stats();
        List<Block> blocks = BlockReader.readAll(data, OsfVersion.OSF5, channels, st);

        var b = (Block.StartData) blocks.get(0);
        assertEquals(1_000_000L, b.startTimestampNs());
        assertEquals(1000.0, b.sampleRateHz(), 1e-9);
        var vals = (Block.DoubleValues) b.values();
        assertArrayEquals(new double[]{2.5}, vals.values());
    }

    @Test
    void startDataMultiSampleFloat() {
        var channels = channelsByIndex(channel(0, DataType.FLOAT, 2));
        float[] v = new float[10];
        for (int i = 0; i < 10; i++) v[i] = i;
        byte[] data = startFloatMulti(0, 2, 7L, 500.0, v);
        ReaderStats st = stats();
        List<Block> blocks = BlockReader.readAll(data, OsfVersion.OSF5, channels, st);

        var b = (Block.StartData) blocks.get(0);
        assertEquals(7L, b.startTimestampNs());
        var vals = (Block.FloatValues) b.values();
        assertEquals(10, vals.values().length);
        assertEquals(3.0f, vals.values()[3], 0f);
    }

    @Test
    void continuedDataMultiSampleInt16() {
        var channels = channelsByIndex(channel(0, DataType.INT16, 2));
        byte[] data = continuedInt16Multi(0, 2, new short[]{0, 10, 20, 30});
        ReaderStats st = stats();
        List<Block> blocks = BlockReader.readAll(data, OsfVersion.OSF5, channels, st);

        var b = (Block.ContinuedData) blocks.get(0);
        var vals = (Block.ShortValues) b.values();
        assertArrayEquals(new short[]{0, 10, 20, 30}, vals.values());
    }

    // ---------------------------------------------------------------
    // bcContinuedRelStampData (control 7, OSF4-era)
    // ---------------------------------------------------------------

    @Test
    void continuedRelStampDataInt16() {
        var channels = channelsByIndex(channel(0, DataType.INT16, 2));
        byte[] data = continuedRelInt16Multi(0, 2, new long[]{100, 200}, new short[]{7, 8});
        ReaderStats st = stats();
        List<Block> blocks = BlockReader.readAll(data, OsfVersion.OSF4, channels, st);

        var b = (Block.RelTimestampData) blocks.get(0);
        assertArrayEquals(new long[]{100, 200}, b.deltasNs());
        var vals = (Block.ShortValues) b.values();
        assertArrayEquals(new short[]{7, 8}, vals.values());
    }

    // ---------------------------------------------------------------
    // Skip paths
    // ---------------------------------------------------------------

    @Test
    void unsupportedDataTypeChannelIsSkippedAndStreamStaysAligned() {
        // Channel 0 has an unsupported data type; two back-to-back blocks.
        var channels = channelsByIndex(channel(0, DataType.UNSUPPORTED, 2));
        byte[] blk = frame(0, 2, CTRL_ABS_TS, new byte[]{1, 2, 3, 4}); // 5-byte payload
        byte[] data = buf().raw(blk).raw(blk).toBytes();
        ReaderStats st = stats();
        List<Block> blocks = BlockReader.readAll(data, OsfVersion.OSF5, channels, st);

        assertEquals(2, blocks.size());
        assertEquals(0, st.blocksRead()); // both skipped, neither counts as read
        for (Block b : blocks) {
            assertInstanceOf(Block.Skipped.class, b);
            assertEquals(0, b.channelIndex());
        }
    }

    @Test
    void unsupportedChannelTypeIsStillRead() {
        // An unknown channeltype must NOT drop/skip a channel — its blocks
        // decode by datatype + block type. Only an unsupported *datatype* skips.
        var channels = channelsByIndex(
                channel(0, DataType.INT64, ChannelType.UNSUPPORTED, 2));
        byte[] data = absTsInt64Single(0, 2, 1000L, 42L);
        ReaderStats st = stats();
        List<Block> blocks = BlockReader.readAll(data, OsfVersion.OSF5, channels, st);

        assertEquals(1, blocks.size());
        assertEquals(1, st.blocksRead());
        assertInstanceOf(Block.AbsTimestampData.class, blocks.get(0));
    }

    @Test
    void reservedAndDeprecatedControlBytesAreSkipped() {
        var channels = channelsByIndex(channel(0, DataType.INT16, 2));
        // 0x09 = bcIntegritySignature control byte on a normal channel in a
        // profile-less file: an unknown/reserved value (>= 9) -> Skipped.
        for (int ctrl : new int[]{CTRL_RESERVED, CTRL_TIMEBASE_REALIGN,
                CTRL_TRUSTED_TS, CTRL_STATUS_EVENT, CTRL_MESSAGE_EVENT, 0x09, 0x55}) {
            byte[] data = frame(0, 2, ctrl, new byte[]{0xA, 0xB});
            ReaderStats st = stats();
            List<Block> blocks = BlockReader.readAll(data, OsfVersion.OSF5, channels, st);
            assertEquals(1, blocks.size(), "ctrl=" + ctrl);
            assertInstanceOf(Block.Skipped.class, blocks.get(0), "ctrl=" + ctrl);
            assertEquals(0, st.blocksRead(), "ctrl=" + ctrl);
        }
    }

    // ---------------------------------------------------------------
    // Truncation / framing edge cases
    // ---------------------------------------------------------------

    @Test
    void emptyStreamYieldsNothingNoTruncation() {
        ReaderStats st = stats();
        List<Block> blocks = BlockReader.readAll(new byte[0], OsfVersion.OSF5,
                Map.of(), st);
        assertTrue(blocks.isEmpty());
        assertFalse(st.truncationSeen());
    }

    @Test
    void oneGoodBlockThenTruncatedReturnsOneBlockAndFlagsTruncation() {
        var channels = channelsByIndex(channel(0, DataType.INT64, 2));
        byte[] data = oneGoodBlockThenTruncated(0, 2);
        ReaderStats st = stats();
        List<Block> blocks = BlockReader.readAll(data, OsfVersion.OSF5, channels, st);

        assertEquals(1, blocks.size());
        assertInstanceOf(Block.AbsTimestampData.class, blocks.get(0));
        assertTrue(st.truncationSeen());
        assertEquals(1, st.blocksRead());
    }

    @Test
    void truncationInChannelIndexIsSilentEof() {
        var channels = channelsByIndex(channel(0, DataType.INT16, 2));
        ReaderStats st = stats();
        // single byte — cannot form a u16 channel index. Clean EOF, no truncation.
        List<Block> blocks = BlockReader.readAll(new byte[]{0}, OsfVersion.OSF5,
                channels, st);
        assertTrue(blocks.isEmpty());
        assertFalse(st.truncationSeen());
    }

    @Test
    void truncationInLengthFieldFlagsTruncation() {
        var channels = channelsByIndex(channel(0, DataType.INT16, 4));
        // index 0, then only 2 bytes of a 4-byte length field.
        byte[] data = {0, 0, 1, 0};
        ReaderStats st = stats();
        List<Block> blocks = BlockReader.readAll(data, OsfVersion.OSF5, channels, st);
        assertTrue(blocks.isEmpty());
        assertTrue(st.truncationSeen());
    }

    @Test
    void unknownChannelIndexStopsBestEffort() {
        var channels = channelsByIndex(channel(0, DataType.INT16, 2));
        // channel 7 is not in the metablock — cannot know length width; stop.
        byte[] data = {7, 0, 1, 0, 0};
        ReaderStats st = stats();
        List<Block> blocks = BlockReader.readAll(data, OsfVersion.OSF5, channels, st);
        assertTrue(blocks.isEmpty());
        assertTrue(st.truncationSeen());
    }

    @Test
    void trailerChannelIndexStopsCleanly() {
        var channels = channelsByIndex(channel(0, DataType.INT16, 2));
        // [u16 0xFFFF][u32 length=2][u8 0][u8 0]; no magic trailer.
        byte[] data = {(byte) 0xFF, (byte) 0xFF, 2, 0, 0, 0, 0, 0};
        ReaderStats st = stats();
        List<Block> blocks = BlockReader.readAll(data, OsfVersion.OSF5, channels, st);
        assertTrue(blocks.isEmpty());
        assertFalse(st.truncationSeen());
    }

    @Test
    void readerStatsDefaultsAndCompressionMutator() {
        ReaderStats st = stats();
        assertEquals(0, st.blocksRead());
        assertFalse(st.truncationSeen());
        assertFalse(st.compressed());
        assertEquals("none", st.compressionFormat());
        st.setCompression("gzip");
        assertTrue(st.compressed());
        assertEquals("gzip", st.compressionFormat());
    }

    // ---------------------------------------------------------------
    // Best-effort contract: malformed-block conditions must not escape
    // readAll — they must flag truncation and stop, returning any prior
    // good blocks. Mirrors the Rust reference's invalid_block → Err path.
    // ---------------------------------------------------------------

    /**
     * A {@code bcContinuedRelStampData} (control 7) block addressed to a
     * {@code STRING} channel — {@code newValueArray} throws
     * {@code OsfException.MalformedFile} because STRING is not a numeric type.
     * The preceding good block must still be returned; truncation must be flagged;
     * no exception must escape {@code readAll}.
     */
    @Test
    void continuedRelStampDataOnNonNumericChannelFlagsGracefulStop() {
        // Channel 0: STRING (non-numeric) — will trigger MalformedFile in newValueArray.
        // Channel 1: INT64  (numeric)     — used for the good block that precedes the bad one.
        var channels = channelsByIndex(
                channel(0, DataType.STRING, 2),
                channel(1, DataType.INT64, 2));

        // Good block: single-sample AbsTs INT64 on channel 1.
        byte[] goodBlock = absTsInt64Single(1, 2, 1000L, 42L);

        // Bad block: multi-sample bcContinuedRelStampData (ctrl 0x87) on the STRING channel 0.
        // body = [u32 N=1][u32 delta][...] — the decoder will try to call newValueArray(STRING, 1)
        // which throws MalformedFile before reading any sample bytes.
        byte[] badBody = buf().u32(1).u32(500).raw(utf8("x")).toBytes();
        byte[] badBlock = frame(0, 2, CTRL_CONTINUED_REL | MULTI_BIT, badBody);

        byte[] data = buf().raw(goodBlock).raw(badBlock).toBytes();

        ReaderStats st = stats();
        List<Block> blocks = BlockReader.readAll(data, OsfVersion.OSF5, channels, st);

        // The good block (channel 1) must be returned.
        assertEquals(1, blocks.size(),
                "prior good block must survive a malformed subsequent block");
        assertInstanceOf(Block.AbsTimestampData.class, blocks.get(0));
        assertEquals(1, blocks.get(0).channelIndex());
        // Truncation must be flagged — best-effort stopped by the bad block.
        assertTrue(st.truncationSeen(),
                "truncationSeen must be true after a malformed block body");
        // The good block was counted; the bad block was not.
        assertEquals(1, st.blocksRead());
    }

    /**
     * A block whose u32 sample-count field encodes a value &gt; {@code Integer.MAX_VALUE}
     * (i.e. bit 31 is set). {@code readSampleCount} must throw
     * {@code OsfException.MalformedFile}; {@code readAll} must catch it and stop
     * best-effort without throwing.
     */
    @Test
    void oversizedSampleCountFlagsGracefulStop() {
        // One good INT64 AbsTs block, then a multi-sample ContinuedData block whose
        // u32 N prefix has bit 31 set (value 0x80000001 = 2147483649 > MAX_VALUE).
        var channels = channelsByIndex(channel(0, DataType.INT64, 2));

        byte[] goodBlock = absTsInt64Single(0, 2, 500L, 99L);

        // Craft the bad block manually: ctrl = CONTINUED | MULTI_BIT (0x85),
        // body starts with the u32 N = 0x80000001.
        byte[] badBody = buf().u32(0x80000001L).toBytes(); // deliberately huge N
        byte[] badBlock = frame(0, 2, CTRL_CONTINUED | MULTI_BIT, badBody);

        byte[] data = buf().raw(goodBlock).raw(badBlock).toBytes();

        ReaderStats st = stats();
        List<Block> blocks = BlockReader.readAll(data, OsfVersion.OSF5, channels, st);

        // Good block is returned; the bad block stops iteration gracefully.
        assertEquals(1, blocks.size());
        assertInstanceOf(Block.AbsTimestampData.class, blocks.get(0));
        assertTrue(st.truncationSeen());
        assertEquals(1, st.blocksRead());
    }
}
