// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH
package com.optimeas.osf;

/**
 * The channel's logical <b>data shape</b> — the OSF metablock
 * {@code "channeltype"} field.
 *
 * <p>This is the channel's structure, NOT its storage mode: whether a channel
 * is equidistant or timestamped is derived at read time from the block control
 * byte ({@code bcStartData}/{@code bcContinuedData} ⇒ equidistant;
 * {@code bcAbsTimeStampData} ⇒ per-sample timestamps) plus {@code timeincrement}
 * — never from {@code channeltype}. The strings {@code equidistant}/
 * {@code timestamped} are therefore NOT channeltypes and never appear in a
 * conformant file.
 *
 * <p>The spec value set is {@code scalar}, {@code vector}, {@code matrix},
 * {@code binary} (docs/de/osf_general.md channel-field reference + "Kanaltypen";
 * osf4.md). Unknown spellings produce {@link #UNSUPPORTED} but do NOT drop the
 * channel — readability is governed by the datatype and block types.
 */
public enum ChannelType {
    /** One value per point in time (the most common shape; the default). */
    SCALAR("scalar"),
    /** A sequence of values per block (e.g. an FFT spectrum). */
    VECTOR("vector"),
    /** A two-dimensional structure per timestamp (e.g. a rainflow matrix). */
    MATRIX("matrix"),
    /** Arbitrary binary blocks — one blob per point in time (with a mimetype). */
    BINARY("binary"),
    /**
     * Forward-compatibility sentinel: a channeltype string not in the spec set.
     *
     * <p>The file still loads and the channel is kept; the original wire string
     * is preserved in the channel's {@code attributes} map under the key
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
     * <p>Known spellings ({@code scalar}, {@code vector}, {@code matrix},
     * {@code binary}) → that constant. Any other string → {@link #UNSUPPORTED}
     * (forward-compat; file still loads and the channel is kept), so this method
     * is infallible.
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
