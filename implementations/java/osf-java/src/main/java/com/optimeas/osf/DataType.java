// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH
package com.optimeas.osf;

import java.util.Map;
import java.util.Set;

/** OSF scalar data types (current spec revision). */
public enum DataType {
    BOOL("bool"),
    INT8("int8"), INT16("int16"), INT32("int32"), INT64("int64"),
    UINT8("uint8"), UINT16("uint16"), UINT32("uint32"), UINT64("uint64"),
    FLOAT("float"), DOUBLE("double"),
    STRING("string"), BINARY("binary"),
    GPS_LOCATION("gpslocation"),
    /**
     * Forward-compatibility sentinel: a datatype string not known to this
     * build that is also not one of the hard-error removed types.
     *
     * <p>The file still loads; only block reads against the affected channel
     * will fail explicitly when attempted. The original wire string is
     * preserved in the channel's {@code attributes} map under the key
     * {@code "datatype"} by the metablock parser.
     */
    UNSUPPORTED("<unsupported>");

    private final String wireName;
    DataType(String wireName) { this.wireName = wireName; }
    public String wireName() { return wireName; }

    private static final Map<String, DataType> BY_WIRE;
    static {
        var m = new java.util.HashMap<String, DataType>();
        // Register all canonical names except UNSUPPORTED (sentinel, never on wire)
        for (DataType t : values()) {
            if (t != UNSUPPORTED) m.put(t.wireName, t);
        }
        m.put("bytearray", BINARY); // accepted read alias; writer always emits "binary"
        BY_WIRE = Map.copyOf(m);
    }

    /** Types the spec removed — rejected on read with a pointer to the replacement. */
    private static final Set<String> REMOVED = Set.of("pair", "triple", "candata", "gpsdata");

    /**
     * Resolve a wire-format datatype string.
     *
     * <ul>
     *   <li>Known canonical name → that constant.</li>
     *   <li>{@code "bytearray"} → {@link #BINARY} (read-side alias).</li>
     *   <li>Removed name ({@code pair}, {@code triple}, {@code candata},
     *       {@code gpsdata}) → throws {@link OsfException.UnsupportedType}.</li>
     *   <li>Any other unknown string → returns {@link #UNSUPPORTED} (forward
     *       compatibility; file still loads).</li>
     * </ul>
     *
     * @throws OsfException.UnsupportedType for removed datatypes only
     */
    public static DataType fromWireName(String name) {
        DataType t = BY_WIRE.get(name);
        if (t != null) return t;
        if (REMOVED.contains(name)) {
            throw new OsfException.UnsupportedType(
                "data type '" + name + "' was removed from the OSF spec; "
                + "see docs/en/osf_general.md for the replacement");
        }
        // Unknown but not removed: forward-compat, file still loads
        return UNSUPPORTED;
    }
}
