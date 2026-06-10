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
 * <p>Unknown spellings produce {@link #UNSUPPORTED} (forward compatibility;
 * there are no removed channel-type strings in the current spec revision).
 */
public enum ChannelType {
    SCALAR("scalar"),
    EQUIDISTANT("equidistant"),
    TIMESTAMPED("timestamped"),
    /**
     * Forward-compatibility sentinel: a channeltype string not known to this
     * build.
     *
     * <p>The file still loads; only block reads against the affected channel
     * will fail explicitly when attempted. The original wire string is
     * preserved in the channel's {@code attributes} map under the key
     * {@code "channeltype"} by the metablock parser.
     */
    UNSUPPORTED("<unsupported>");

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
        // Register known types; UNSUPPORTED is a sentinel and never on wire
        for (ChannelType t : values()) {
            if (t != UNSUPPORTED) m.put(t.wireName, t);
        }
        BY_WIRE = java.util.Map.copyOf(m);
    }

    /**
     * Resolve a wire-format channel-type string.
     *
     * <p>Known spellings ({@code scalar}, {@code equidistant},
     * {@code timestamped}) → that constant. Any other string → {@link #UNSUPPORTED}
     * (forward-compat; file still loads). There are no removed channel-type
     * strings in the current spec revision, so this method is infallible.
     *
     * @param name the raw {@code "channeltype"} string from the metablock
     * @return the matching constant, or {@link #UNSUPPORTED} for unknown spellings
     */
    public static ChannelType fromWireName(String name) {
        ChannelType t = BY_WIRE.get(name);
        if (t != null) return t;
        // Unknown but not removed: forward-compat, file still loads
        return UNSUPPORTED;
    }
}
