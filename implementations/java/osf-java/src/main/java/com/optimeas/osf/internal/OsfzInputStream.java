// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH
package com.optimeas.osf.internal;

import java.io.IOException;
import java.io.InputStream;
import java.io.PushbackInputStream;
import java.util.function.Consumer;
import java.util.zip.GZIPInputStream;
import java.util.zip.InflaterInputStream;

/**
 * Transparent OSFZ decompression on the read path.
 *
 * <p>Detection logic mirrors
 * {@code implementations/rust/osf-core/src/compression.rs} and
 * {@code implementations/cpp/src/compression.cpp}:
 * <ul>
 *   <li>gzip — leading bytes {@code 0x1F 0x8B} (RFC 1952)</li>
 *   <li>zlib — {@code 0x78} followed by one of {@code 0x01 / 0x5E / 0x9C / 0xDA}
 *       (RFC 1950)</li>
 *   <li>plain — anything else (real OSF starts with {@code 'O' = 0x4F})</li>
 * </ul>
 *
 * <p>Format labels: {@code "gzip"}, {@code "zlib"} — matching the Rust
 * {@code CompressionFormat} variant names (lower-cased). Plain files do not
 * trigger the {@code onFormat} callback.
 */
public final class OsfzInputStream {

    // Detection constants — verified against compression.rs lines 123-129
    // and compression.cpp lines 22-29.
    private static final int GZIP_B0 = 0x1F;
    private static final int GZIP_B1 = 0x8B;
    private static final int ZLIB_B0 = 0x78;

    private OsfzInputStream() {}

    /**
     * Wrap {@code in} in a transparent decompressor if the leading bytes
     * indicate gzip or zlib compression; otherwise return the stream
     * unchanged (as a {@link PushbackInputStream} with the peeked bytes
     * unread).
     *
     * <p>The {@code onFormat} callback is called <em>once</em> with
     * {@code "gzip"} or {@code "zlib"} when compression is detected. It is
     * <em>not</em> called for plain input so callers can use it directly as
     * {@link com.optimeas.osf.ReaderStats#setCompression}.
     *
     * <p>A stream shorter than two bytes is treated as plain — the downstream
     * magic-header parser will surface an appropriate error when it tries to
     * read the OSF identifier.
     *
     * @param in       the raw input stream (ownership transferred — do not
     *                 continue reading {@code in} directly after this call)
     * @param onFormat called with the detected format label if compressed
     * @return a ready-to-read {@link InputStream} whose bytes are the
     *         uncompressed content (or the original bytes for plain input)
     * @throws IOException if the initial peek read fails
     */
    public static InputStream wrap(InputStream in, Consumer<String> onFormat)
            throws IOException {
        // Use a 2-byte pushback buffer so we can peek without consuming.
        PushbackInputStream pb = new PushbackInputStream(in, 2);

        byte[] head = new byte[2];
        int got = 0;
        // Read up to 2 bytes; the stream may be shorter.
        int n;
        while (got < 2) {
            n = pb.read(head, got, 2 - got);
            if (n < 0) break;
            got += n;
        }

        if (got < 2) {
            // Stream is too short to classify — unread whatever we got and
            // treat as plain.
            if (got > 0) {
                pb.unread(head, 0, got);
            }
            return pb;
        }

        int b0 = head[0] & 0xFF;
        int b1 = head[1] & 0xFF;

        // Always unread the two bytes so the wrapped decompressor sees the
        // full compressed stream from the beginning (gzip/zlib headers start
        // at byte 0).
        pb.unread(head, 0, 2);

        if (b0 == GZIP_B0 && b1 == GZIP_B1) {
            onFormat.accept("gzip");
            return new GZIPInputStream(pb);
        }

        if (b0 == ZLIB_B0 && (b1 == 0x01 || b1 == 0x5E || b1 == 0x9C || b1 == 0xDA)) {
            onFormat.accept("zlib");
            // InflaterInputStream with the default Deflater handles zlib-wrapped
            // deflate (RFC 1950) — the JDK Inflater reads the zlib header by
            // default (nowrap=false).
            return new InflaterInputStream(pb);
        }

        // Plain — return the pushback stream as-is; the two bytes are already
        // unread so the caller sees the complete original byte sequence.
        return pb;
    }
}
