// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH
package com.optimeas.osf;

/**
 * OSF channel type as encoded in the metablock {@code "channeltype"} field.
 *
 * <p>Wire spellings are taken verbatim from the OSF5 spec and confirmed
 * against implementations/rust/osf-core/src/meta.rs:
 * {@code "scalar"}, {@code "equidistant"}, {@code "timestamped"}.
 *
 * <p>Unknown spellings produce {@link OsfException.UnsupportedType}.
 */
public enum ChannelType {
    SCALAR("scalar"),
    EQUIDISTANT("equidistant"),
    TIMESTAMPED("timestamped");

    private final String wireName;

    ChannelType(String wireName) {
        this.wireName = wireName;
    }

    /** Returns the exact wire spelling used in the JSON/XML metablock. */
    public String wireName() {
        return wireName;
    }

    private static final java.util.Map<String, ChannelType> BY_WIRE;
    static {
        var m = new java.util.HashMap<String, ChannelType>();
        for (ChannelType t : values()) m.put(t.wireName, t);
        BY_WIRE = java.util.Map.copyOf(m);
    }

    /**
     * Resolve a wire-format channel-type string.
     *
     * @param name the raw {@code "channeltype"} string from the metablock
     * @return the matching constant
     * @throws OsfException.UnsupportedType for unknown spellings
     */
    public static ChannelType fromWireName(String name) {
        ChannelType t = BY_WIRE.get(name);
        if (t != null) return t;
        throw new OsfException.UnsupportedType(
                "unknown OSF channel type '" + name + "'");
    }
}
