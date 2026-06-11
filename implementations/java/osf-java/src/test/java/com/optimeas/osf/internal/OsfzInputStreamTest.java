// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH
package com.optimeas.osf.internal;

import org.junit.jupiter.api.Test;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.ArrayList;
import java.util.List;
import java.util.zip.DeflaterOutputStream;
import java.util.zip.GZIPOutputStream;

import static org.assertj.core.api.Assertions.assertThat;

/**
 * Unit tests for {@link OsfzInputStream#wrap}: gzip transparent inflate,
 * plain pass-through, and (optional) zlib transparent inflate.
 *
 * <p>Detection bytes verified against
 * {@code implementations/rust/osf-core/src/compression.rs} and
 * {@code implementations/cpp/src/compression.cpp}:
 * <ul>
 *   <li>gzip: {@code 0x1F 0x8B}</li>
 *   <li>zlib: {@code 0x78 {0x01, 0x5E, 0x9C, 0xDA}}</li>
 *   <li>plain: anything else</li>
 * </ul>
 */
class OsfzInputStreamTest {

    // A payload that does NOT start with 0x1F or 0x78 so it is never
    // accidentally classified as compressed.
    private static final byte[] PAYLOAD = "OSF5 42\n{\"hello\":\"world\"}".getBytes();

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
        // DeflaterOutputStream with default Deflater produces zlib-wrapped
        // deflate (RFC 1950), leading bytes 0x78 0x9C at default compression.
        try (DeflaterOutputStream def = new DeflaterOutputStream(baos)) {
            def.write(data);
        }
        return baos.toByteArray();
    }

    /** Collect format strings reported by the onFormat callback. */
    private static List<String> formats(InputStream wrapped) {
        // This helper is only used to check that the field was set; for
        // simplicity we pass a collector list in the test rather than here.
        return List.of();
    }

    // ---------------------------------------------------------------
    // gzip
    // ---------------------------------------------------------------

    @Test
    void gzip_roundTrip_decompressesAndReportsFormat() throws IOException {
        byte[] compressed = gzipCompress(PAYLOAD);
        assertThat(compressed[0] & 0xFF).isEqualTo(0x1F);
        assertThat(compressed[1] & 0xFF).isEqualTo(0x8B);

        List<String> reported = new ArrayList<>();
        InputStream wrapped = OsfzInputStream.wrap(
                new ByteArrayInputStream(compressed), reported::add);

        byte[] got = wrapped.readAllBytes();

        assertThat(got).isEqualTo(PAYLOAD);
        assertThat(reported).containsExactly("gzip");
    }

    @Test
    void gzip_emptyPayload_decompressesAndReportsFormat() throws IOException {
        byte[] compressed = gzipCompress(new byte[0]);
        List<String> reported = new ArrayList<>();
        InputStream wrapped = OsfzInputStream.wrap(
                new ByteArrayInputStream(compressed), reported::add);

        byte[] got = wrapped.readAllBytes();

        assertThat(got).isEmpty();
        assertThat(reported).containsExactly("gzip");
    }

    // ---------------------------------------------------------------
    // plain
    // ---------------------------------------------------------------

    @Test
    void plain_passThrough_noFormatCallback() throws IOException {
        List<String> reported = new ArrayList<>();
        InputStream wrapped = OsfzInputStream.wrap(
                new ByteArrayInputStream(PAYLOAD), reported::add);

        byte[] got = wrapped.readAllBytes();

        assertThat(got).isEqualTo(PAYLOAD);
        // plain: onFormat must NOT be called with "gzip" or "zlib"
        assertThat(reported).doesNotContain("gzip", "zlib");
    }

    @Test
    void plain_emptyStream_passThrough_noFormatCallback() throws IOException {
        List<String> reported = new ArrayList<>();
        InputStream wrapped = OsfzInputStream.wrap(
                new ByteArrayInputStream(new byte[0]), reported::add);

        byte[] got = wrapped.readAllBytes();

        assertThat(got).isEmpty();
        assertThat(reported).doesNotContain("gzip", "zlib");
    }

    @Test
    void plain_singleByte_passThrough() throws IOException {
        // A single byte starting with 0x78 cannot be confirmed as zlib
        // (no second byte); must be treated as plain.
        byte[] data = {0x78};
        List<String> reported = new ArrayList<>();
        InputStream wrapped = OsfzInputStream.wrap(
                new ByteArrayInputStream(data), reported::add);

        byte[] got = wrapped.readAllBytes();

        assertThat(got).isEqualTo(data);
        assertThat(reported).doesNotContain("gzip", "zlib");
    }

    @Test
    void plain_0x78_withInvalidSecondByte_isPlain() throws IOException {
        // 0x78 0xFF — first byte looks like zlib candidate but second byte
        // is not in {0x01, 0x5E, 0x9C, 0xDA}; must pass through as plain.
        byte[] data = {0x78, (byte) 0xFF, 0x00, 0x01};
        List<String> reported = new ArrayList<>();
        InputStream wrapped = OsfzInputStream.wrap(
                new ByteArrayInputStream(data), reported::add);

        byte[] got = wrapped.readAllBytes();

        assertThat(got).isEqualTo(data);
        assertThat(reported).doesNotContain("gzip", "zlib");
    }

    // ---------------------------------------------------------------
    // zlib
    // ---------------------------------------------------------------

    @Test
    void zlib_roundTrip_decompressesAndReportsFormat() throws IOException {
        byte[] compressed = zlibCompress(PAYLOAD);
        // Verify the DeflaterOutputStream produced a recognised zlib header.
        assertThat(compressed[0] & 0xFF).isEqualTo(0x78);
        int second = compressed[1] & 0xFF;
        assertThat(second).isIn(0x01, 0x5E, 0x9C, 0xDA);

        List<String> reported = new ArrayList<>();
        InputStream wrapped = OsfzInputStream.wrap(
                new ByteArrayInputStream(compressed), reported::add);

        byte[] got = wrapped.readAllBytes();

        assertThat(got).isEqualTo(PAYLOAD);
        assertThat(reported).containsExactly("zlib");
    }
}
