// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH
package com.optimeas.osf;

/**
 * On-disk OSF format version, derived from the magic header.
 *
 * <p>The magic-header line uses one of four accepted identifiers:
 * <ul>
 *   <li>{@code OSF4}, {@code OCEAN_STREAM_FORMAT4}, {@code OCEAN_STREAMING_FORMAT4}
 *       → {@link #OSF4}
 *   <li>{@code OSF5} → {@link #OSF5}
 * </ul>
 */
public enum OsfVersion {
    /** OSF4: XML metablock, classic control-byte set, file trailer. */
    OSF4,
    /** OSF5: JSON metablock, simplified control byte, no trailer. */
    OSF5,
}
