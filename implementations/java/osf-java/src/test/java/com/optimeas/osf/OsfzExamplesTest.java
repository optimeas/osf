// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH
package com.optimeas.osf;

import com.optimeas.osf.testutil.ExamplesDir;
import org.junit.jupiter.api.Assumptions;
import org.junit.jupiter.api.Test;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.zip.DeflaterOutputStream;
import java.util.zip.GZIPOutputStream;

import static org.assertj.core.api.Assertions.assertThat;

/**
 * Integration tests for the transparent OSFZ decompression wired into
 * {@link DataManager}.
 *
 * <p>Tests are guarded by {@link ExamplesDir}: a checkout without the corpus
 * skips them rather than failing.
 */
class OsfzExamplesTest {

    // ---------------------------------------------------------------
    // helpers
    // ---------------------------------------------------------------

    private static byte[] gzipCompress(byte[] data) throws IOException {
        ByteArrayOutputStream baos = new ByteArrayOutputStream();
        try (GZIPOutputStream gz = new GZIPOutputStream(baos)) {
            gz.write(data);
        }
        return baos.toByteArray();
    }

    private static byte[] zlibCompress(byte[] data) throws IOException {
        ByteArrayOutputStream baos = new ByteArrayOutputStream();
        try (DeflaterOutputStream def = new DeflaterOutputStream(baos)) {
            def.write(data);
        }
        return baos.toByteArray();
    }

    // ---------------------------------------------------------------
    // weather_station.osfz — real gzip field file
    // ---------------------------------------------------------------

    /**
     * Load the real {@code weather_station.osfz} field file. Assert that the
     * compression flags are set and that at least one channel has samples.
     */
    @Test
    void weatherStation_osfz_loadsWithCompressedFlag() {
        Path examples = ExamplesDir.resolve();
        Path file = examples.resolve("weather_station.osfz");
        Assumptions.assumeTrue(Files.isRegularFile(file),
                "weather_station.osfz not present at " + file + " — skipping");

        DataManager mgr = DataManager.loadFromFile(file);

        assertThat(mgr.stats().compressed())
                .as("compressed flag for weather_station.osfz")
                .isTrue();
        assertThat(mgr.stats().compressionFormat())
                .as("compression format for weather_station.osfz")
                .isEqualTo("gzip");
        assertThat(mgr.channels())
                .as("channels in weather_station.osfz")
                .isNotEmpty();
        boolean anyWithSamples = mgr.channels().stream()
                .anyMatch(c -> c.sampleCount() > 0);
        assertThat(anyWithSamples)
                .as("at least one channel with samples in weather_station.osfz")
                .isTrue();
    }

    // ---------------------------------------------------------------
    // transparent-load equivalence — gzip re-compressed steam_loco.osf
    // ---------------------------------------------------------------

    /**
     * Re-compress {@code steam_loco.osf} to gzip in-memory, load it via
     * {@link DataManager#load(InputStream)}, and assert that the result is
     * equivalent to loading the plain file via
     * {@link DataManager#loadFromFile(Path)}: same channel count and
     * spot-checked first channel's sample count.
     */
    @Test
    void steamLoco_gzipInMemory_equivalentToPlain() throws IOException {
        Path examples = ExamplesDir.resolve();
        Path file = examples.resolve("steam_loco.osf");
        Assumptions.assumeTrue(Files.isRegularFile(file),
                "steam_loco.osf not present at " + file + " — skipping");

        // Load the plain reference.
        DataManager plain = DataManager.loadFromFile(file);
        assertThat(plain.stats().compressed()).isFalse();

        // Re-compress in memory and load via the stream API.
        byte[] raw = Files.readAllBytes(file);
        byte[] compressed = gzipCompress(raw);

        DataManager gzipped = DataManager.load(new ByteArrayInputStream(compressed));

        assertThat(gzipped.stats().compressed()).isTrue();
        assertThat(gzipped.stats().compressionFormat()).isEqualTo("gzip");

        // Channel count must match.
        assertThat(gzipped.channels())
                .as("channel count gzip == plain")
                .hasSize(plain.channels().size());

        // Spot-check: first channel's sample count must match.
        if (!plain.channels().isEmpty()) {
            DataChannel plainFirst = plain.channels().get(0);
            DataChannel gzippedFirst = gzipped.channelByName(plainFirst.name())
                    .orElseThrow(() -> new AssertionError(
                            "channel '" + plainFirst.name() + "' missing from gzip-loaded manager"));
            assertThat(gzippedFirst.sampleCount())
                    .as("sample count for channel '%s' gzip == plain", plainFirst.name())
                    .isEqualTo(plainFirst.sampleCount());
        }
    }

    /**
     * Re-compress {@code steam_loco.osf} to zlib in-memory, load it via
     * {@link DataManager#load(InputStream)}, and assert equivalence to
     * the plain load.
     */
    @Test
    void steamLoco_zlibInMemory_equivalentToPlain() throws IOException {
        Path examples = ExamplesDir.resolve();
        Path file = examples.resolve("steam_loco.osf");
        Assumptions.assumeTrue(Files.isRegularFile(file),
                "steam_loco.osf not present at " + file + " — skipping");

        // Load the plain reference.
        DataManager plain = DataManager.loadFromFile(file);

        // Re-compress in memory (zlib-wrapped deflate) and load.
        byte[] raw = Files.readAllBytes(file);
        byte[] compressed = zlibCompress(raw);

        DataManager zlibed = DataManager.load(new ByteArrayInputStream(compressed));

        assertThat(zlibed.stats().compressed()).isTrue();
        assertThat(zlibed.stats().compressionFormat()).isEqualTo("zlib");

        assertThat(zlibed.channels())
                .as("channel count zlib == plain")
                .hasSize(plain.channels().size());

        if (!plain.channels().isEmpty()) {
            DataChannel plainFirst = plain.channels().get(0);
            DataChannel zlibFirst = zlibed.channelByName(plainFirst.name())
                    .orElseThrow(() -> new AssertionError(
                            "channel '" + plainFirst.name() + "' missing from zlib-loaded manager"));
            assertThat(zlibFirst.sampleCount())
                    .as("sample count for channel '%s' zlib == plain", plainFirst.name())
                    .isEqualTo(plainFirst.sampleCount());
        }
    }

    // ---------------------------------------------------------------
    // plain load — stats.compressed() must stay false
    // ---------------------------------------------------------------

    /**
     * Confirm that loading a plain {@code .osf} file leaves
     * {@link ReaderStats#compressed()} {@code false}.
     */
    @Test
    void steamLoco_plain_notMarkedCompressed() {
        Path examples = ExamplesDir.resolve();
        Path file = examples.resolve("steam_loco.osf");
        Assumptions.assumeTrue(Files.isRegularFile(file),
                "steam_loco.osf not present at " + file + " — skipping");

        DataManager mgr = DataManager.loadFromFile(file);
        assertThat(mgr.stats().compressed()).isFalse();
        assertThat(mgr.stats().compressionFormat()).isEqualTo("none");
    }
}
