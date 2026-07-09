// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH
package com.optimeas.osf;

/**
 * Parsed contents of the OSF magic-header line.
 *
 * <p>The header line has the form:
 * <pre>{@code <IDENTIFIER> <metablock_length>\n}</pre>
 *
 * @param version          the OSF format version detected from the identifier.
 * @param metablockLength  byte length of the metablock that immediately follows
 *                         the terminating newline.
 * @param headerByteLength number of bytes consumed by the magic-header line
 *                         including its terminator (LF or CRLF), so that
 *                         callers know the byte offset at which the metablock
 *                         begins.
 * @param integrity        the integrity profile declared by the header tokens
 *                         ({@link IntegrityProfile#NONE} when the line carries
 *                         no integrity token).
 * @param metablockCrc     the CRC32C of the raw metablock bytes carried by the
 *                         {@code crc32c} token as an unsigned 32-bit value
 *                         ({@code 0..2^32-1}), or {@code null} when no
 *                         {@code crc32c} token is present.
 */
public record MagicHeader(OsfVersion version, long metablockLength, int headerByteLength,
                          IntegrityProfile integrity, Long metablockCrc) {
}
