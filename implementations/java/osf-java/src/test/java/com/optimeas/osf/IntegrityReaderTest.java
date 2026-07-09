// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH
package com.optimeas.osf;

import com.optimeas.osf.testutil.ExamplesDir;
import org.junit.jupiter.api.Test;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.zip.GZIPOutputStream;

import static org.assertj.core.api.Assertions.*;

/**
 * Read-side integration + negative tests for the OSF5 integrity profile level
 * crc (metablock CRC, frame CRC, signature-block skip). Cross-validates against
 * the reference corpus written by the Rust and Delphi writers.
 */
class IntegrityReaderTest {

    private static Path integrity(String name) {
        return ExamplesDir.resolve().resolve("generated").resolve("integrity").resolve(name);
    }

    private static byte[] read(Path p) throws IOException {
        return Files.readAllBytes(p);
    }

    private static DataManager load(byte[] bytes) {
        return DataManager.load(new ByteArrayInputStream(bytes));
    }

    /** blockStart = headerByteLength + metablockLength. */
    private static int blockStart(byte[] bytes) {
        MagicHeader h = MagicHeaderParser.parse(bytes);
        return h.headerByteLength() + (int) h.metablockLength();
    }

    // ---- Cross-implementation reference files ----

    @Test
    void readsAllFourReferenceFilesClean() throws IOException {
        String[] files = {
            "osf5_crc_equidistant.osf", "osf5_crc_variable.osf",
            "osf5_equidistant_crc_delphi.osf", "osf5_variable_crc_delphi.osf"};
        for (String f : files) {
            DataManager mgr = DataManager.loadFromFile(integrity(f));
            assertThat(mgr.stats().integrity()).as(f).isEqualTo(IntegrityProfile.CRC32C);
            assertThat(mgr.stats().blocksCrcFailed()).as(f).isZero();
            assertThat(mgr.stats().blocksSignatureSkipped()).as(f).isZero();
            assertThat(mgr.stats().verificationStatus()).as(f).isEqualTo("crc_valid");
            assertThat(mgr.channels()).as(f).isNotEmpty();
        }
    }

    // ---- Negative suite ----

    @Test
    void metablockByteFlipIsRejected() throws IOException {
        byte[] bytes = read(integrity("osf5_crc_equidistant.osf"));
        MagicHeader h = MagicHeaderParser.parse(bytes);
        int off = h.headerByteLength() + 20; // inside the metablock JSON
        bytes[off] ^= 0xFF;
        assertThatThrownBy(() -> load(bytes))
                .isInstanceOf(OsfException.MetablockCrcMismatch.class);
    }

    @Test
    void numericBlockByteFlipCountsCrcFailure() throws IOException {
        byte[] bytes = read(integrity("osf5_crc_equidistant.osf"));
        bytes[bytes.length - 2] ^= 0xFF; // inside the last frame's trailing CRC
        DataManager mgr = load(bytes);
        assertThat(mgr.stats().blocksCrcFailed()).isGreaterThanOrEqualTo(1);
        assertThat(mgr.stats().verificationStatus()).isEqualTo("invalid");
    }

    @Test
    void stringBlockByteFlipCountsCrcFailure() throws IOException {
        byte[] bytes = read(integrity("osf5_crc_variable.osf"));
        bytes[bytes.length - 2] ^= 0xFF;
        DataManager mgr = load(bytes);
        assertThat(mgr.stats().blocksCrcFailed()).isGreaterThanOrEqualTo(1);
        assertThat(mgr.stats().verificationStatus()).isEqualTo("invalid");
    }

    @Test
    void signatureBlockIsSkippedAndCounted() throws IOException {
        // Inject a control-byte-9 signature block on reserved channel 0xFFFE
        // (u32 length) into a crc file (active profile). It must be skipped,
        // counted, and leave the channel data intact.
        byte[] base = read(integrity("osf5_crc_equidistant.osf"));
        int start = blockStart(base);

        ByteArrayOutputStream inner = new ByteArrayOutputStream();
        inner.write(0x09); // control byte 9
        for (int i = 0; i < 20; i++) inner.write(0xAA);
        byte[] innerBytes = inner.toByteArray();

        ByteArrayOutputStream sig = new ByteArrayOutputStream();
        sig.write(0xFE); sig.write(0xFF);                       // u16 0xFFFE LE
        writeU32Le(sig, innerBytes.length + 4);                 // u32 length incl. frame CRC
        sig.write(innerBytes);
        writeU32Le(sig, 0);                                     // opaque frame-CRC placeholder

        byte[] sigBytes = sig.toByteArray();
        byte[] injected = new byte[base.length + sigBytes.length];
        System.arraycopy(base, 0, injected, 0, start);
        System.arraycopy(sigBytes, 0, injected, start, sigBytes.length);
        System.arraycopy(base, start, injected, start + sigBytes.length, base.length - start);

        DataManager baseline = load(base);
        DataManager mgr = load(injected);
        assertThat(mgr.stats().blocksSignatureSkipped()).isEqualTo(1);
        assertThat(mgr.stats().blocksCrcFailed()).isZero();
        assertThat(mgr.channels()).hasSameSizeAs(baseline.channels());
    }

    @Test
    void gzipWrappedCrcFileReadsAndVerifies() throws IOException {
        byte[] raw = read(integrity("osf5_crc_equidistant.osf"));
        ByteArrayOutputStream gz = new ByteArrayOutputStream();
        try (GZIPOutputStream g = new GZIPOutputStream(gz)) {
            g.write(raw);
        }
        DataManager mgr = load(gz.toByteArray());
        assertThat(mgr.stats().compressed()).isTrue();
        assertThat(mgr.stats().integrity()).isEqualTo(IntegrityProfile.CRC32C);
        assertThat(mgr.stats().blocksCrcFailed()).isZero();
        assertThat(mgr.stats().verificationStatus()).isEqualTo("crc_valid");
    }

    private static void writeU32Le(ByteArrayOutputStream out, long v) {
        for (int i = 0; i < 4; i++) out.write((int) ((v >>> (8 * i)) & 0xFF));
    }
}
