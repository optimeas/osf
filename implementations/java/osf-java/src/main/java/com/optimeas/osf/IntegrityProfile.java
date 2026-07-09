// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH
package com.optimeas.osf;

/**
 * OSF5 integrity profile level declared by the magic-header tokens.
 *
 * <p>The levels are strictly ordered ({@code NONE ⊂ CRC32C ⊂ ED25519}): a
 * {@code crc32c} token raises the level to {@link #CRC32C}; a following
 * {@code ed25519} token raises it to {@link #ED25519}. A plain OSF5 file (or any
 * OSF4 file) carries no integrity token and is {@link #NONE}.
 *
 * <p>This library implements level {@code crc} (metablock + per-block CRC32C).
 * Signed files ({@link #ED25519}) are read through transparently — signature
 * blocks are skipped and counted — but the signatures are not verified.
 */
public enum IntegrityProfile {
    /** No integrity token; a plain OSF file. */
    NONE,
    /** Level {@code crc}: metablock CRC + per-block frame CRC32C. */
    CRC32C,
    /** Level {@code signed}: an Ed25519 signature chain (not verified here). */
    ED25519
}
