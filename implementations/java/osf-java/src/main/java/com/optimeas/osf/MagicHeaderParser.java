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

        while (true) {
            int b = in.read();
            if (b == -1) {
                throw new OsfException.MalformedFile(
                        "unexpected end of input before newline in magic header");
            }
            totalBytes++;
            if (b == '\n') {
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
     * <p>The header grammar is
     * <pre>{@code <identifier> SP <metablock-length> *(SP <token>) }</pre>
     * with exactly one space between fields and no trailing space. Splitting on
     * a single {@code ' '} therefore turns a double or trailing space into an
     * empty field, which is rejected. Tokens are {@code key:value} and are an
     * OSF5-only feature (declaring the integrity profile); an OSF4 identifier
     * must not carry any. This mirrors the Rust {@code header.rs} grammar.
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
        String rest = line.substring(sep + 1);

        OsfVersion version = identifierToVersion(identifier);

        // Split on a single space; a double/trailing space yields an empty field.
        String[] fields = rest.split(" ", -1);

        String lenField = fields[0];
        if (lenField.isEmpty()) {
            throw new OsfException.MalformedFile(
                    "missing metablock length after identifier \"" + identifier + "\"");
        }
        long metablockLength;
        try {
            metablockLength = Long.parseUnsignedLong(lenField);
        } catch (NumberFormatException e) {
            throw new OsfException.MalformedFile(
                    "metablock length is not a valid uint64: \"" + lenField + "\"");
        }

        IntegrityProfile integrity = IntegrityProfile.NONE;
        Long metablockCrc = null;
        boolean sawCrc = false;

        for (int i = 1; i < fields.length; i++) {
            String token = fields[i];
            if (token.isEmpty()) {
                throw new OsfException.MalformedFile(
                        "malformed magic header: fields must be separated by a single "
                        + "space with no trailing space");
            }
            // Tokens are an OSF5-only feature; OSF4 identifiers must not carry them.
            if (version != OsfVersion.OSF5) {
                throw new OsfException.MalformedFile(
                        "header tokens are only allowed for OSF5; found \"" + token
                        + "\" after an OSF4 identifier");
            }
            int colon = token.indexOf(':');
            if (colon < 0) {
                throw new OsfException.MalformedFile(
                        "malformed header token, expected 'key:value': \"" + token + "\"");
            }
            String key = token.substring(0, colon);
            String value = token.substring(colon + 1);
            if (!isValidTokenKey(key)) {
                throw new OsfException.MalformedFile(
                        "malformed header token key (lowercase a-z, 0-9, '-' only): \""
                        + key + "\"");
            }
            switch (key) {
                case "crc32c" -> {
                    metablockCrc = parseCrc32cToken(value);
                    integrity = IntegrityProfile.CRC32C;
                    sawCrc = true;
                }
                case "ed25519" -> {
                    validateEd25519KeyId(value);
                    if (!sawCrc) {
                        throw new OsfException.MalformedFile(
                                "ed25519 token is only valid after a crc32c token "
                                + "(crc32c first)");
                    }
                    integrity = IntegrityProfile.ED25519;
                }
                default -> throw new OsfException.UnknownHeaderToken(
                        "unknown header token '" + key + "'");
            }
        }

        return new MagicHeader(version, metablockLength, headerBytes, integrity, metablockCrc);
    }

    /** Token key charset: lowercase a-z, digits, and '-'. */
    private static boolean isValidTokenKey(String key) {
        if (key.isEmpty()) {
            return false;
        }
        for (int i = 0; i < key.length(); i++) {
            char c = key.charAt(i);
            if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) {
                return false;
            }
        }
        return true;
    }

    /** Parse a {@code crc32c} value: exactly 8 uppercase hex digits → u32. */
    private static long parseCrc32cToken(String value) {
        if (value.length() != 8 || !isHex(value, true)) {
            throw new OsfException.MalformedFile(
                    "crc32c token value must be 8 uppercase hex digits, got \"" + value + "\"");
        }
        return Long.parseLong(value, 16);
    }

    /** Validate an {@code ed25519} keyid: exactly 16 lowercase hex digits (syntactic). */
    private static void validateEd25519KeyId(String value) {
        if (value.length() != 16 || !isHex(value, false)) {
            throw new OsfException.MalformedFile(
                    "ed25519 keyid must be 16 lowercase hex digits, got \"" + value + "\"");
        }
    }

    private static boolean isHex(String s, boolean upper) {
        for (int i = 0; i < s.length(); i++) {
            char c = s.charAt(i);
            boolean digit = c >= '0' && c <= '9';
            boolean letter = upper ? (c >= 'A' && c <= 'F') : (c >= 'a' && c <= 'f');
            if (!digit && !letter) {
                return false;
            }
        }
        return true;
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
