// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH
package com.optimeas.osf;

import java.io.ByteArrayInputStream;
import java.io.IOException;
import java.io.InputStream;

/**
 * Parses the OSF magic-header line.
 *
 * <p>Every OSF file starts with a single ASCII line:
 * <pre>{@code <IDENTIFIER> <metablock_length>\n}</pre>
 *
 * <p>Accepted identifiers:
 * <ul>
 *   <li>{@code OSF4}, {@code OCEAN_STREAM_FORMAT4},
 *       {@code OCEAN_STREAMING_FORMAT4} → {@link OsfVersion#OSF4}
 *   <li>{@code OSF5} → {@link OsfVersion#OSF5}
 * </ul>
 *
 * <p>A trailing {@code \r} before {@code \n} (CRLF) is silently tolerated
 * even though the spec mandates LF only.
 *
 * <p>The {@link #parse(InputStream)} overload reads ONLY the header bytes
 * and leaves the stream positioned at the first byte of the metablock.
 *
 * @throws OsfException.MalformedFile on any parse error.
 */
public final class MagicHeaderParser {

    /** Cap on the magic-header line length (bytes before the newline). */
    private static final int MAX_MAGIC_HEADER_LEN = 128;

    private MagicHeaderParser() {}

    // -----------------------------------------------------------------------
    // Public API
    // -----------------------------------------------------------------------

    /**
     * Parses the magic header from a raw byte array.
     *
     * @param data the file bytes (only the first line is consumed)
     * @return parsed {@link MagicHeader}
     * @throws OsfException.MalformedFile if the header is malformed or
     *         the identifier is unrecognised
     */
    public static MagicHeader parse(byte[] data) {
        try {
            return parse(new ByteArrayInputStream(data));
        } catch (IOException e) {
            // ByteArrayInputStream never throws IOException; re-wrap just in case.
            throw new OsfException.MalformedFile("I/O error reading byte array: " + e.getMessage(), e);
        }
    }

    /**
     * Parses the magic header from an {@link InputStream}.
     *
     * <p>Reads byte-by-byte up to (and including) the terminating newline.
     * After this method returns the stream is positioned immediately after
     * the {@code \n}, so the next read yields the first byte of the metablock.
     *
     * @param in the stream to read from
     * @return parsed {@link MagicHeader}
     * @throws IOException              on underlying I/O failure
     * @throws OsfException.MalformedFile if the header is malformed or
     *         the identifier is unrecognised
     */
    public static MagicHeader parse(InputStream in) throws IOException {
        // Read byte-by-byte until '\n'; collect raw bytes to count headerByteLength.
        byte[] lineBuf = new byte[MAX_MAGIC_HEADER_LEN + 2]; // +2 for '\r' and '\n'
        int lineLen = 0;    // bytes accumulated before '\n' (excluding '\n' itself)
        int totalBytes = 0; // total bytes consumed from stream (incl. '\n')
        boolean newlineFound = false;

        while (true) {
            int b = in.read();
            if (b == -1) {
                throw new OsfException.MalformedFile(
                        "unexpected end of input before newline in magic header");
            }
            totalBytes++;
            if (b == '\n') {
                newlineFound = true;
                break;
            }
            if (lineLen > MAX_MAGIC_HEADER_LEN) {
                throw new OsfException.MalformedFile(
                        "no newline within " + MAX_MAGIC_HEADER_LEN + " bytes — not an OSF file");
            }
            lineBuf[lineLen++] = (byte) b;
        }

        // Strip trailing '\r' (CRLF tolerance).
        int lineEnd = lineLen;
        if (lineEnd > 0 && lineBuf[lineEnd - 1] == '\r') {
            lineEnd--;
        }

        String line = new String(lineBuf, 0, lineEnd, java.nio.charset.StandardCharsets.US_ASCII);
        return parseLine(line, totalBytes);
    }

    // -----------------------------------------------------------------------
    // Internal helpers
    // -----------------------------------------------------------------------

    /**
     * Parses a single trimmed header line (no trailing newline/CR).
     *
     * @param line         the header text
     * @param headerBytes  total bytes consumed from the stream (used for
     *                     {@link MagicHeader#headerByteLength()})
     */
    private static MagicHeader parseLine(String line, int headerBytes) {
        int sep = line.indexOf(' ');
        if (sep < 0) {
            throw new OsfException.MalformedFile(
                    "expected '<identifier> <length>', got: \"" + line + "\"");
        }

        String identifier = line.substring(0, sep);
        String rest = line.substring(sep + 1).trim();

        OsfVersion version = identifierToVersion(identifier);

        if (rest.isEmpty()) {
            throw new OsfException.MalformedFile(
                    "missing metablock length after identifier \"" + identifier + "\"");
        }

        long metablockLength;
        try {
            metablockLength = Long.parseUnsignedLong(rest);
        } catch (NumberFormatException e) {
            throw new OsfException.MalformedFile(
                    "metablock length is not a valid uint64: \"" + rest + "\"");
        }

        return new MagicHeader(version, metablockLength, headerBytes);
    }

    private static OsfVersion identifierToVersion(String identifier) {
        return switch (identifier) {
            case "OSF4", "OCEAN_STREAM_FORMAT4", "OCEAN_STREAMING_FORMAT4" -> OsfVersion.OSF4;
            case "OSF5" -> OsfVersion.OSF5;
            default -> throw new OsfException.MalformedFile(
                    "unknown OSF identifier: \"" + identifier + "\"");
        };
    }
}
