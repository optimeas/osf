// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH
package com.optimeas.osf.internal;

import java.util.zip.CRC32C;

/**
 * Small write-side helpers shared by both writers for the OSF5 integrity
 * profile level crc: building the magic-header line (optionally with the
 * {@code crc32c} token carrying the metablock CRC).
 */
public final class Integrity {

    private Integrity() {}

    /**
     * Build the {@code OSF5 <len>[ crc32c:<HEX8>]\n} magic-header line for the
     * given metablock. When {@code frameCrc} is set the CRC32C of the metablock
     * bytes is appended as a {@code crc32c} token (8 uppercase hex digits),
     * declaring level crc.
     *
     * @param metablock the metablock bytes that will follow the header
     * @param frameCrc  whether the integrity profile is active
     * @return the ASCII magic-header line including the trailing newline
     */
    public static String magicLine(byte[] metablock, boolean frameCrc) {
        StringBuilder sb = new StringBuilder("OSF5 ").append(metablock.length);
        if (frameCrc) {
            CRC32C crc = new CRC32C();
            crc.update(metablock, 0, metablock.length);
            sb.append(" crc32c:").append(hex8(crc.getValue()));
        }
        return sb.append('\n').toString();
    }

    /** Format an unsigned 32-bit value as exactly 8 uppercase hex digits. */
    public static String hex8(long value) {
        String h = Long.toHexString(value & 0xFFFF_FFFFL).toUpperCase(java.util.Locale.ROOT);
        return "00000000".substring(h.length()) + h;
    }
}
