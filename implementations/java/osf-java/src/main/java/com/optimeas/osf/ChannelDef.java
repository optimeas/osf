// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH
package com.optimeas.osf;

import java.util.Map;

/**
 * Definition of a single channel as recorded in the OSF metablock.
 *
 * <p>This record is populated by both the OSF5 JSON parser and the OSF4 XML
 * parser; the wire-to-field mapping is:
 *
 * <table>
 *   <tr><th>Wire field</th><th>Record component</th></tr>
 *   <tr><td>{@code "index"}</td><td>{@link #index()}</td></tr>
 *   <tr><td>{@code "name"}</td><td>{@link #name()}</td></tr>
 *   <tr><td>{@code "datatype"}</td><td>{@link #dataType()}</td></tr>
 *   <tr><td>{@code "channeltype"}</td><td>{@link #channelType()}</td></tr>
 *   <tr><td>{@code "sizeoflengthvalue"}</td><td>{@link #sizeOfLengthValue()}</td></tr>
 *   <tr><td>{@code "timeincrement"}</td><td>{@link #timeIncrementNs()}</td></tr>
 *   <tr><td>{@code "physicalunit"}</td><td>{@link #physicalUnit()}</td></tr>
 *   <tr><td>other string fields</td><td>{@link #attributes()}</td></tr>
 * </table>
 *
 * @param index            stable channel index (0..65535), referenced by the
 *                         block stream
 * @param name             fully-qualified channel name
 * @param dataType         resolved data type
 * @param channelType      resolved channel type
 * @param sizeOfLengthValue width of per-value length prefix in bytes; always
 *                         2 or 4 after validation
 * @param timeIncrementNs  equidistant period in nanoseconds; 0 when absent or
 *                         explicitly zero on the wire (non-equidistant)
 * @param physicalUnit     physical unit string, or {@code null} when absent
 * @param attributes       extra scalar string fields from the channel object
 *                         (e.g. {@code "displayname"}, {@code "comment"},
 *                         {@code "reference"}) keyed by the wire field name
 */
public record ChannelDef(
        int index,
        String name,
        DataType dataType,
        ChannelType channelType,
        int sizeOfLengthValue,
        long timeIncrementNs,
        String physicalUnit,
        Map<String, String> attributes
) {
    /**
     * Returns {@code true} when this is an equidistant channel, i.e. when
     * the metablock carries a positive {@code "timeincrement"}.
     *
     * <p>A zero or absent {@code "timeincrement"} means the channel is
     * timestamped (each sample carries its own timestamp).
     */
    public boolean isEquidistant() {
        return timeIncrementNs > 0;
    }
}
