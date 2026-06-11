// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH
package com.optimeas.osf;

import java.util.List;
import java.util.Map;

/**
 * Parsed contents of an OSF metablock — version-independent.
 *
 * <p>Both the OSF5 JSON parser and the OSF4 XML parser produce this record.
 * The format split ends here: all downstream consumers (block readers,
 * writers, the public API) see only {@code Metablock}.
 *
 * @param version   on-disk format version (4 or 5), taken from the
 *                  {@code "version"} field inside the {@code "osf"} envelope
 * @param metadata  file-level metadata from the {@code "file"} block,
 *                  collected as a {@code String → String} map; keys match the
 *                  wire field names exactly (e.g. {@code "creator"},
 *                  {@code "created_utc"}, {@code "tag"}, {@code "reason"})
 * @param channels  channel definitions in the order they appear on disk
 */
public record Metablock(
        int version,
        Map<String, String> metadata,
        List<ChannelDef> channels
) {}
