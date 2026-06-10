// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH
package com.optimeas.osf.internal;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.optimeas.osf.*;

import java.io.IOException;
import java.util.*;

/**
 * Parses an OSF5 JSON metablock into a {@link Metablock}.
 *
 * <p>The wire format is a JSON object wrapped in an {@code "osf"} envelope:
 * <pre>{@code
 * {"osf":{
 *   "format": "osf5",
 *   "version": 5,
 *   "file": { "creator": "...", "created_utc": "...", ... },
 *   "channels": [ {...}, ... ],
 *   "infos": [ {...}, ... ]
 * }}
 * }</pre>
 *
 * <p>JSON field names mirror the Rust reference implementation
 * ({@code meta_json.rs}) exactly — they form the wire contract and must not
 * be changed.
 *
 * <p>Jackson is used only inside this class; no Jackson types appear in the
 * public model records ({@link Metablock}, {@link ChannelDef}, …).
 */
public class JsonMetablockParser {

    private static final ObjectMapper MAPPER = new ObjectMapper();

    // Wire field names confirmed from implementations/rust/osf-core/src/meta_json.rs
    // and meta.rs.
    private static final String F_OSF               = "osf";
    private static final String F_VERSION           = "version";
    private static final String F_FILE              = "file";
    private static final String F_CHANNELS          = "channels";

    // Per-channel required fields
    private static final String F_INDEX             = "index";
    private static final String F_NAME              = "name";
    private static final String F_CHANNELTYPE       = "channeltype";
    private static final String F_DATATYPE          = "datatype";
    private static final String F_SIZEOFLENGTHVALUE = "sizeoflengthvalue";

    // Per-channel optional fields that receive first-class treatment
    private static final String F_TIMEINCREMENT     = "timeincrement";
    private static final String F_PHYSICALUNIT      = "physicalunit";

    // Known required/typed channel fields — everything else that is a scalar
    // string goes into the attributes map
    private static final Set<String> CHANNEL_TYPED_FIELDS = Set.of(
            F_INDEX, F_NAME, F_CHANNELTYPE, F_DATATYPE,
            F_SIZEOFLENGTHVALUE, F_TIMEINCREMENT, F_PHYSICALUNIT
    );

    /**
     * Parse the bytes of an OSF5 metablock body into a {@link Metablock}.
     *
     * @param metablockBytes raw bytes of the metablock (no magic-header line,
     *                       no block-stream bytes)
     * @return the parsed metablock
     * @throws OsfException.MalformedFile  if the bytes are not valid JSON, if
     *                                     the {@code "osf"} envelope is absent,
     *                                     or if a required channel field is
     *                                     missing or has an invalid value
     * @throws OsfException.UnsupportedType if a channel carries a datatype or
     *                                      channel-type string that was removed
     *                                      from the spec
     */
    public Metablock parse(byte[] metablockBytes) {
        JsonNode root;
        try {
            root = MAPPER.readTree(metablockBytes);
        } catch (IOException e) {
            throw new OsfException.MalformedFile(
                    "OSF5 metablock JSON parse error: " + e.getMessage(), e);
        }

        if (!root.isObject()) {
            throw new OsfException.MalformedFile(
                    "OSF5 root must be a JSON object");
        }

        JsonNode osfNode = root.get(F_OSF);
        if (osfNode == null || !osfNode.isObject()) {
            throw new OsfException.MalformedFile(
                    "OSF5 root is missing the \"osf\" envelope");
        }
        ObjectNode osf = (ObjectNode) osfNode;

        int version = osf.path(F_VERSION).asInt(5);

        Map<String, String> metadata = parseFileBlock(osf);
        List<ChannelDef> channels   = parseChannels(osf);

        return new Metablock(version, Collections.unmodifiableMap(metadata),
                Collections.unmodifiableList(channels));
    }

    // -----------------------------------------------------------------------
    // file block → metadata map
    // -----------------------------------------------------------------------

    private static Map<String, String> parseFileBlock(ObjectNode osf) {
        JsonNode fileNode = osf.get(F_FILE);
        if (fileNode == null || fileNode.isNull() || !fileNode.isObject()) {
            return Map.of();
        }
        ObjectNode file = (ObjectNode) fileNode;
        Map<String, String> meta = new LinkedHashMap<>();
        file.fields().forEachRemaining(entry -> {
            JsonNode v = entry.getValue();
            if (v != null && !v.isNull()) {
                // String fields verbatim; other scalar types rendered as text
                meta.put(entry.getKey(), v.isTextual() ? v.textValue() : v.toString());
            }
        });
        return meta;
    }

    // -----------------------------------------------------------------------
    // channels array
    // -----------------------------------------------------------------------

    private static List<ChannelDef> parseChannels(ObjectNode osf) {
        JsonNode channelsNode = osf.get(F_CHANNELS);
        if (channelsNode == null || channelsNode.isNull()) {
            return List.of();
        }
        if (!channelsNode.isArray()) {
            throw new OsfException.MalformedFile(
                    "OSF5 \"channels\" must be an array");
        }
        List<ChannelDef> result = new ArrayList<>(channelsNode.size());
        for (int i = 0; i < channelsNode.size(); i++) {
            JsonNode entry = channelsNode.get(i);
            if (!entry.isObject()) {
                throw new OsfException.MalformedFile(
                        "OSF5 channels[" + i + "] must be an object");
            }
            result.add(parseChannel((ObjectNode) entry, i));
        }
        return result;
    }

    private static ChannelDef parseChannel(ObjectNode obj, int position) {
        // index — required
        JsonNode idxNode = obj.get(F_INDEX);
        if (idxNode == null || !idxNode.isNumber()) {
            throw new OsfException.MalformedFile(
                    "channel at position " + position
                            + " is missing required field \"index\"");
        }
        long rawIdx = idxNode.longValue();
        if (rawIdx < 0 || rawIdx > 0xFFFFL) {
            throw new OsfException.MalformedFile(
                    "channel at position " + position + " has index=" + rawIdx
                            + " out of range 0..65535");
        }
        int index = (int) rawIdx;

        // name — required
        JsonNode nameNode = obj.get(F_NAME);
        if (nameNode == null || !nameNode.isTextual()) {
            throw new OsfException.MalformedFile(
                    "channel at index " + index
                            + " is missing required field \"name\"");
        }
        String name = nameNode.textValue();

        // channeltype — required
        JsonNode ctNode = obj.get(F_CHANNELTYPE);
        if (ctNode == null || !ctNode.isTextual()) {
            throw new OsfException.MalformedFile(
                    "channel \"" + name + "\" is missing required field \"channeltype\"");
        }
        // fromWireName throws UnsupportedType for unknown values — propagated as-is
        ChannelType channelType = ChannelType.fromWireName(ctNode.textValue());

        // datatype — required; removed types throw UnsupportedType (propagated)
        JsonNode dtNode = obj.get(F_DATATYPE);
        if (dtNode == null || !dtNode.isTextual()) {
            throw new OsfException.MalformedFile(
                    "channel \"" + name + "\" is missing required field \"datatype\"");
        }
        DataType dataType = DataType.fromWireName(dtNode.textValue());

        // sizeoflengthvalue — required, must be 2 or 4
        JsonNode solNode = obj.get(F_SIZEOFLENGTHVALUE);
        if (solNode == null || !solNode.isNumber()) {
            throw new OsfException.MalformedFile(
                    "channel \"" + name + "\" is missing required field \"sizeoflengthvalue\"");
        }
        long rawSol = solNode.longValue();
        if (rawSol != 2L && rawSol != 4L) {
            throw new OsfException.MalformedFile(
                    "channel \"" + name + "\" sizeoflengthvalue must be 2 or 4, got " + rawSol);
        }
        int sizeOfLengthValue = (int) rawSol;

        // timeincrement — optional; absent or 0 → non-equidistant
        long timeIncrementNs = 0L;
        JsonNode tiNode = obj.get(F_TIMEINCREMENT);
        if (tiNode != null && !tiNode.isNull() && tiNode.isNumber()) {
            timeIncrementNs = tiNode.longValue();
        }

        // physicalunit — optional
        String physicalUnit = null;
        JsonNode puNode = obj.get(F_PHYSICALUNIT);
        if (puNode != null && !puNode.isNull() && puNode.isTextual()) {
            physicalUnit = puNode.textValue();
        }

        // attributes — collect remaining scalar string fields
        Map<String, String> attributes = new LinkedHashMap<>();
        obj.fields().forEachRemaining(entry -> {
            String key = entry.getKey();
            if (!CHANNEL_TYPED_FIELDS.contains(key)) {
                JsonNode v = entry.getValue();
                if (v != null && !v.isNull() && !v.isObject() && !v.isArray()) {
                    attributes.put(key, v.isTextual() ? v.textValue() : v.toString());
                }
            }
        });

        return new ChannelDef(
                index,
                name,
                dataType,
                channelType,
                sizeOfLengthValue,
                timeIncrementNs,
                physicalUnit,
                Collections.unmodifiableMap(attributes)
        );
    }
}
