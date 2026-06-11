// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH
package com.optimeas.osf;

import org.junit.jupiter.api.Test;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.nio.charset.StandardCharsets;
import java.util.List;
import java.util.Optional;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;

/**
 * Unit tests for the {@link DataManager} read pipeline against a synthetic,
 * hand-built OSF5 file. Validates the end-to-end magic-header → metablock →
 * block-reader → {@link ChannelAssembler} chain without depending on the
 * reference corpus (that is exercised separately by {@code ManagerExamplesTest}).
 */
class DataManagerTest {

    // ---------------------------------------------------------------
    // Little-endian byte builder (independent of the internal test
    // fixtures, which live in a non-exported package).
    // ---------------------------------------------------------------

    private static final class Buf {
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
        Buf f64(double v) { return i64(Double.doubleToRawLongBits(v)); }
        Buf raw(byte[] b) { out.writeBytes(b); return this; }
        byte[] toBytes() { return out.toByteArray(); }
    }

    /**
     * Build an OSF5 file: magic header line + JSON metablock for one
     * timestamped {@code double} channel named {@code "Sensor/Temp"} (index 0)
     * + one multi-sample {@code bcAbsTimeStampData} block carrying 3 samples
     * (1.0, 2.0, 3.0) at timestamps 100, 200, 300 ns.
     */
    private static byte[] syntheticOsf5() {
        String metaJson = "{\"osf\":{"
                + "\"version\":5,"
                + "\"file\":{\"creator\":\"DataManagerTest\"},"
                + "\"channels\":[{"
                + "\"index\":0,"
                + "\"name\":\"Sensor/Temp\","
                + "\"datatype\":\"double\","
                + "\"channeltype\":\"scalar\","
                + "\"sizeoflengthvalue\":2"
                + "}]}}";
        byte[] metaBytes = metaJson.getBytes(StandardCharsets.UTF_8);

        // Block: bcAbsTimeStampData, multi (bit 7), 3 (i64 ts, f64 v) samples.
        Buf body = new Buf().u32(3)
                .i64(100).f64(1.0)
                .i64(200).f64(2.0)
                .i64(300).f64(3.0);
        byte[] bodyBytes = body.toBytes();
        int control = 8 | 0x80; // bcAbsTimeStampData + multi
        int blockLen = 1 + bodyBytes.length; // control + body
        Buf block = new Buf().u16(0).u16(blockLen).u8(control).raw(bodyBytes);

        Buf file = new Buf();
        file.raw(("OSF5 " + metaBytes.length + "\n").getBytes(StandardCharsets.US_ASCII));
        file.raw(metaBytes);
        file.raw(block.toBytes());
        return file.toBytes();
    }

    @Test
    void loadsSyntheticOsf5WithOneTimestampedDoubleChannel() {
        DataManager mgr = DataManager.load(new ByteArrayInputStream(syntheticOsf5()));

        List<DataChannel> channels = mgr.channels();
        assertThat(channels).hasSize(1);

        Optional<DataChannel> byName = mgr.channelByName("Sensor/Temp");
        assertThat(byName).isPresent();
        DataChannel ch = byName.get();
        assertThat(ch.dataType()).isEqualTo(DataType.DOUBLE);
        assertThat(ch.sampleCount()).isEqualTo(3);
        assertThat(ch.asDoubles()).containsExactly(1.0, 2.0, 3.0);
        assertThat(ch.timestampsNs()).containsExactly(100L, 200L, 300L);

        assertThat(mgr.channelByIndex(0)).isPresent();
        assertThat(mgr.channelByName("missing")).isEmpty();
        assertThat(mgr.channelByIndex(99)).isEmpty();
    }

    @Test
    void metadataAndStatsExposed() {
        DataManager mgr = DataManager.load(new ByteArrayInputStream(syntheticOsf5()));
        assertThat(mgr.metadata()).containsEntry("creator", "DataManagerTest");
        assertThat(mgr.stats().blocksRead()).isEqualTo(1);
        assertThat(mgr.stats().truncationSeen()).isFalse();
    }

    @Test
    void incompatibleAccessorThrows() {
        DataManager mgr = DataManager.load(new ByteArrayInputStream(syntheticOsf5()));
        DataChannel ch = mgr.channelByName("Sensor/Temp").orElseThrow();
        // double channel: asLongs / asStrings must reject.
        assertThatThrownBy(ch::asLongs).isInstanceOf(OsfException.class);
        assertThatThrownBy(ch::asStrings).isInstanceOf(OsfException.class);
        assertThatThrownBy(ch::asBinaries).isInstanceOf(OsfException.class);
        assertThatThrownBy(ch::asGps).isInstanceOf(OsfException.class);
    }
}
