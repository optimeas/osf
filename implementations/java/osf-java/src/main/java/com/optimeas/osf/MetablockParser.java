// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH
package com.optimeas.osf;

import com.optimeas.osf.internal.JsonMetablockParser;
import com.optimeas.osf.internal.XmlMetablockParser;

/**
 * Version-dispatching entry point for OSF metablock parsing.
 *
 * <p>Routes the metablock bytes to the correct format-specific parser:
 * <ul>
 *   <li>{@link OsfVersion#OSF5} → {@code internal.JsonMetablockParser} (JSON)</li>
 *   <li>{@link OsfVersion#OSF4} → {@code internal.XmlMetablockParser} (StAX XML)</li>
 * </ul>
 *
 * <p>Both parsers produce an identical {@link Metablock} model, so callers
 * downstream never need to distinguish between OSF4 and OSF5.
 */
public final class MetablockParser {

    private static final JsonMetablockParser JSON_PARSER = new JsonMetablockParser();
    private static final XmlMetablockParser  XML_PARSER  = new XmlMetablockParser();

    private MetablockParser() {}

    /**
     * Parse the bytes of an OSF metablock into a {@link Metablock}.
     *
     * @param version        the on-disk format version (from the magic header)
     * @param metablockBytes raw bytes of the metablock (no magic-header line,
     *                       no block-stream bytes)
     * @return the parsed metablock
     * @throws OsfException.MalformedFile   if the bytes do not conform to the
     *                                      format specified by {@code version}
     * @throws OsfException.UnsupportedType if a channel carries a datatype that
     *                                      was removed from the OSF spec
     */
    public static Metablock parse(OsfVersion version, byte[] metablockBytes) {
        return switch (version) {
            case OSF5 -> JSON_PARSER.parse(metablockBytes);
            case OSF4 -> XML_PARSER.parse(metablockBytes);
        };
    }
}
