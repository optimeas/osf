// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH
package com.optimeas.osf.internal;

import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.optimeas.osf.ChannelDef;
import com.optimeas.osf.ChannelType;
import com.optimeas.osf.DataType;
import com.optimeas.osf.OsfException;

import java.util.List;
import java.util.Map;

/**
 * Internal write-side mirror of {@link JsonMetablockParser}: builds the OSF5
 * JSON metablock body from file metadata and channel definitions.
 *
 * <p>The field names match exactly what {@link JsonMetablockParser} reads (they
 * are the wire contract) — {@code osf.version}, {@code osf.format},
 * {@code osf.file} (the metadata map verbatim), and {@code osf.channels[]} with
 * {@code index / name / channeltype / datatype / sizeoflengthvalue /
 * timeincrement / physicalunit}. The mapping mirrors the Rust reference
 * {@code writer.rs build_metablock_json / channel_def_to_json} and the C++
 * {@code writer_common.cpp build_metablock}.
 *
 * <h2>channeltype normalisation</h2>
 * Non-equidistant channels are normalised to {@code "scalar"} (the Delphi /
 * reference convention; only {@code equidistant} is preserved). Confirmed in
 * {@code writer_common.cpp build_metablock} and {@code writer.rs
 * channel_type_to_wire}.
 *
 * <p>{@code timeincrement} is emitted only for equidistant channels.
 *
 * <p>The returned bytes are the metablock body alone; the writers prepend the
 * magic-header line ({@code OSF5 <len>\n}).
 */
public final class MetablockBuilder {

    private static final ObjectMapper MAPPER = new ObjectMapper();

    // Wire field names — must match JsonMetablockParser.
    private static final String F_OSF = "osf";
    private static final String F_FORMAT = "format";
    private static final String F_VERSION = "version";
    private static final String F_FILE = "file";
    private static final String F_CHANNELS = "channels";
    private static final String F_INDEX = "index";
    private static final String F_NAME = "name";
    private static final String F_CHANNELTYPE = "channeltype";
    private static final String F_DATATYPE = "datatype";
    private static final String F_SIZEOFLENGTHVALUE = "sizeoflengthvalue";
    private static final String F_TIMEINCREMENT = "timeincrement";
    private static final String F_PHYSICALUNIT = "physicalunit";

    private MetablockBuilder() {}

    /**
     * Build the OSF5 JSON metablock body.
     *
     * @param version  on-disk format version (always 5 for OSF5 output)
     * @param metadata file-level metadata, written verbatim into {@code osf.file}
     *                 (keys are wire field names, e.g. {@code creator}, {@code tag},
     *                 {@code created_utc})
     * @param channels channel definitions in stream order; their {@code index}
     *                 is re-derived from list position to stay consistent
     * @return the JSON bytes of the metablock body (no magic-header line)
     */
    public static byte[] buildOsf5Json(int version, Map<String, String> metadata,
                                       List<ChannelDef> channels) {
        ObjectNode root = MAPPER.createObjectNode();
        ObjectNode osf = root.putObject(F_OSF);
        osf.put(F_FORMAT, "osf5");
        osf.put(F_VERSION, version);

        ObjectNode file = osf.putObject(F_FILE);
        if (metadata != null) {
            for (Map.Entry<String, String> e : metadata.entrySet()) {
                file.put(e.getKey(), e.getValue());
            }
        }

        ArrayNode channelsArr = osf.putArray(F_CHANNELS);
        for (int i = 0; i < channels.size(); i++) {
            channelsArr.add(channelToJson(i, channels.get(i)));
        }

        try {
            return MAPPER.writerWithDefaultPrettyPrinter().writeValueAsBytes(root);
        } catch (JsonProcessingException e) {
            throw new OsfException.MalformedFile(
                    "failed to serialise OSF5 metablock JSON: " + e.getMessage(), e);
        }
    }

    private static ObjectNode channelToJson(int index, ChannelDef def) {
        ObjectNode obj = MAPPER.createObjectNode();
        obj.put(F_INDEX, index);
        obj.put(F_NAME, def.name());
        obj.put(F_CHANNELTYPE, channelTypeToWire(def.channelType()));
        obj.put(F_DATATYPE, dataTypeToWire(def.dataType()));
        obj.put(F_SIZEOFLENGTHVALUE, def.sizeOfLengthValue());

        // timeincrement is written whenever the channel has one (equidistant
        // channels); it is independent of channeltype (the data shape).
        if (def.timeIncrementNs() > 0) {
            obj.put(F_TIMEINCREMENT, def.timeIncrementNs());
        }
        if (def.physicalUnit() != null) {
            obj.put(F_PHYSICALUNIT, def.physicalUnit());
        }
        // Carry any extra scalar string attributes (displayname, comment, …).
        if (def.attributes() != null) {
            for (Map.Entry<String, String> e : def.attributes().entrySet()) {
                if (!obj.has(e.getKey())) {
                    obj.put(e.getKey(), e.getValue());
                }
            }
        }
        return obj;
    }

    /**
     * The channel's data-shape channeltype on the wire
     * ({@code scalar}/{@code vector}/{@code matrix}/{@code binary}). Unknown
     * shapes fall back to {@code "scalar"}.
     */
    private static String channelTypeToWire(ChannelType ct) {
        return switch (ct) {
            case SCALAR -> "scalar";
            case VECTOR -> "vector";
            case MATRIX -> "matrix";
            case BINARY -> "binary";
            case UNSUPPORTED -> "scalar";
        };
    }

    private static String dataTypeToWire(DataType dt) {
        if (dt == DataType.UNSUPPORTED) {
            throw new OsfException.MalformedFile(
                    "cannot serialise channel with UNSUPPORTED data type");
        }
        return dt.wireName();
    }
}
