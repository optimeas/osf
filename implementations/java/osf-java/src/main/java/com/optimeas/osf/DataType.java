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
    GPS_LOCATION("gpslocation");

    private final String wireName;
    DataType(String wireName) { this.wireName = wireName; }
    public String wireName() { return wireName; }

    private static final Map<String, DataType> BY_WIRE;
    static {
        var m = new java.util.HashMap<String, DataType>();
        for (DataType t : values()) m.put(t.wireName, t);
        m.put("bytearray", BINARY); // accepted read alias; writer always emits "binary"
        BY_WIRE = Map.copyOf(m);
    }

    /** Types the spec removed — rejected on read with a pointer to the replacement. */
    private static final Set<String> REMOVED = Set.of("pair", "triple", "candata", "gpsdata");

    public static DataType fromWireName(String name) {
        DataType t = BY_WIRE.get(name);
        if (t != null) return t;
        if (REMOVED.contains(name)) {
            throw new OsfException.UnsupportedType(
                "data type '" + name + "' was removed from the OSF spec; "
                + "see docs/en/osf_general.md for the replacement");
        }
        throw new OsfException.UnsupportedType("unknown OSF data type '" + name + "'");
    }
}
