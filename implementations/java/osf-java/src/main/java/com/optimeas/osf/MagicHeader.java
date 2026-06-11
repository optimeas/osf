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
 */
public record MagicHeader(OsfVersion version, long metablockLength, int headerByteLength) {
}
