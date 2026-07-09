// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH
package com.optimeas.osf;

import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;

import static org.assertj.core.api.Assertions.*;

/**
 * Write-side tests for the OSF5 integrity profile level crc: both writers emit
 * the crc32c header token, the metablock CRC, and a per-block frame CRC, and the
 * output reads back with a clean verification status. Frame CRCs are purely
 * additive trailing bytes, so the reconstructed channel data is identical to the
 * plain writers' output.
 */
class WriterIntegrityTest {

    private static final String PINNED_UTC = "2026-01-01T00:00:00Z";

    /** Author a fixed dataset on a BlockWriter, optionally with the crc profile. */
    private static byte[] writeBlock(boolean crc) {
        BlockWriter w = new BlockWriter();
        if (crc) w.setIntegrity(IntegrityProfile.CRC32C);
        w.setMetadata("created_utc", PINNED_UTC);
        int temp = w.addTimestampedChannel("sensor/temp", DataType.DOUBLE, 2);
        int count = w.addTimestampedChannel("sensor/count", DataType.INT64, 2);
        int label = w.addTimestampedChannel("sensor/label", DataType.STRING);
        for (int i = 0; i < 40; i++) {
            w.writeSample(temp, 1_000_000_000L + i * 1_000_000L, 20.0 + i * 0.5);
            w.writeSample(count, 1_000_000_000L + i * 1_000_000L, (long) i);
        }
        w.writeSample(label, 1_000_000_000L, "hello");
        w.writeSample(label, 2_000_000_000L, "world");
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        w.writeTo(out);
        return out.toByteArray();
    }

    /** Author the same dataset on a StreamingWriter. */
    private static void writeStreaming(Path path, boolean crc) {
        try (StreamingWriter w = StreamingWriter.create(path)) {
            if (crc) w.setIntegrity(IntegrityProfile.CRC32C);
            w.setMetadata("created_utc", PINNED_UTC);
            int temp = w.addTimestampedChannel("sensor/temp", DataType.DOUBLE, 2);
            int count = w.addTimestampedChannel("sensor/count", DataType.INT64, 2);
            int label = w.addTimestampedChannel("sensor/label", DataType.STRING, 2);
            for (int i = 0; i < 40; i++) {
                w.writeSample(temp, 1_000_000_000L + i * 1_000_000L, 20.0 + i * 0.5);
                w.writeSample(count, 1_000_000_000L + i * 1_000_000L, (long) i);
            }
            w.writeSample(label, 1_000_000_000L, "hello");
            w.writeSample(label, 2_000_000_000L, "world");
        }
    }

    private static DataManager load(byte[] bytes) {
        return DataManager.load(new ByteArrayInputStream(bytes));
    }

    @Test
    void blockWriterCrcRoundtripMatchesPlain() {
        byte[] crcBytes = writeBlock(true);
        byte[] plainBytes = writeBlock(false);

        String header = new String(crcBytes, 0, Math.min(60, crcBytes.length),
                StandardCharsets.US_ASCII);
        assertThat(header).contains(" crc32c:");

        DataManager crc = load(crcBytes);
        DataManager plain = load(plainBytes);
        assertThat(crc.stats().integrity()).isEqualTo(IntegrityProfile.CRC32C);
        assertThat(crc.stats().blocksCrcFailed()).isZero();
        assertThat(crc.stats().verificationStatus()).isEqualTo("crc_valid");
        assertThat(plain.stats().integrity()).isEqualTo(IntegrityProfile.NONE);
        RoundtripExamplesTest.assertChannelsEquivalent("block crc vs plain", plain, crc);
    }

    @Test
    void streamingWriterCrcRoundtripMatchesPlain(@TempDir Path dir) {
        Path crcPath = dir.resolve("crc.osf");
        Path plainPath = dir.resolve("plain.osf");
        writeStreaming(crcPath, true);
        writeStreaming(plainPath, false);

        DataManager crc = DataManager.loadFromFile(crcPath);
        DataManager plain = DataManager.loadFromFile(plainPath);
        assertThat(crc.stats().integrity()).isEqualTo(IntegrityProfile.CRC32C);
        assertThat(crc.stats().blocksCrcFailed()).isZero();
        assertThat(crc.stats().verificationStatus()).isEqualTo("crc_valid");
        RoundtripExamplesTest.assertChannelsEquivalent("streaming crc vs plain", plain, crc);
    }

    /**
     * For single-sample-per-channel data both writers batch identically, so
     * their crc output — token, metablock CRC and frame CRCs included — is
     * byte-identical.
     */
    @Test
    void bothWritersCrcByteIdentical(@TempDir Path dir) throws IOException {
        BlockWriter bw = new BlockWriter();
        bw.setIntegrity(IntegrityProfile.CRC32C);
        bw.setMetadata("created_utc", PINNED_UTC);
        int ch = bw.addTimestampedChannel("sensor/temp", DataType.DOUBLE, 2);
        bw.writeSample(ch, 1000L, 1.5);
        ByteArrayOutputStream blockOut = new ByteArrayOutputStream();
        bw.writeTo(blockOut);

        Path sp = dir.resolve("s.osf");
        try (StreamingWriter sw = StreamingWriter.create(sp)) {
            sw.setIntegrity(IntegrityProfile.CRC32C);
            sw.setMetadata("created_utc", PINNED_UTC);
            int sch = sw.addTimestampedChannel("sensor/temp", DataType.DOUBLE, 2);
            sw.writeSample(sch, 1000L, 1.5);
        }

        assertThat(blockOut.toByteArray()).isEqualTo(Files.readAllBytes(sp));
    }
}
