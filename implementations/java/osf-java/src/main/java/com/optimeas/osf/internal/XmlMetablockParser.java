// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH
package com.optimeas.osf.internal;

import com.optimeas.osf.*;

import javax.xml.stream.XMLInputFactory;
import javax.xml.stream.XMLStreamConstants;
import javax.xml.stream.XMLStreamException;
import javax.xml.stream.XMLStreamReader;
import java.io.ByteArrayInputStream;
import java.util.*;

/**
 * Parses an OSF4 XML metablock into a {@link Metablock} using StAX.
 *
 * <p>The wire format is a single {@code <optimeas>} element with file-level
 * attributes and a {@code <channels>} child carrying {@code <channel>}
 * entries:
 * <pre>{@code
 * <?xml version="1.0" encoding="UTF-8"?>
 * <optimeas creator="..." created_utc="..." ...>
 *   <channels count="N">
 *     <channel index="0" name="..." channeltype="scalar"
 *              datatype="double" sizeoflengthvalue="2" .../>
 *   </channels>
 * </optimeas>
 * }</pre>
 *
 * <p>XML grammar confirmed from
 * {@code implementations/rust/osf-core/src/meta_xml.rs}.
 *
 * <p>XXE protection: the factory has external-entity support and DTD
 * processing disabled via {@link XMLInputFactory#IS_SUPPORTING_EXTERNAL_ENTITIES}
 * and {@link XMLInputFactory#SUPPORT_DTD}.
 */
public class XmlMetablockParser {

    // Attribute name constants confirmed from meta_xml.rs
    private static final String A_INDEX             = "index";
    private static final String A_NAME              = "name";
    private static final String A_CHANNELTYPE       = "channeltype";
    private static final String A_DATATYPE          = "datatype";
    private static final String A_SIZEOFLENGTHVALUE = "sizeoflengthvalue";
    private static final String A_TIMEINCREMENT     = "timeincrement";
    private static final String A_PHYSICALUNIT      = "physicalunit";

    // First-class channel attributes (not collected into the attributes map)
    private static final Set<String> CHANNEL_TYPED_ATTRS = Set.of(
            A_INDEX, A_NAME, A_CHANNELTYPE, A_DATATYPE,
            A_SIZEOFLENGTHVALUE, A_TIMEINCREMENT, A_PHYSICALUNIT
    );

    // Known <optimeas>-level metadata attribute names (confirmed from meta_xml.rs)
    private static final Set<String> OPTIMEAS_META_ATTRS = Set.of(
            "creator", "created_utc", "tag", "reason", "comment",
            "namespacesep",
            "created_at_latitude", "created_at_longitude", "created_at_altitude",
            "latitude", "longitude", "altitude"
    );

    /** XXE-safe factory, shared across calls. */
    private static final XMLInputFactory FACTORY;
    static {
        XMLInputFactory f = XMLInputFactory.newInstance();
        // XXE protection: disable external entity resolution and DTD processing
        f.setProperty(XMLInputFactory.IS_SUPPORTING_EXTERNAL_ENTITIES, Boolean.FALSE);
        f.setProperty(XMLInputFactory.SUPPORT_DTD, Boolean.FALSE);
        f.setProperty(XMLInputFactory.IS_REPLACING_ENTITY_REFERENCES, Boolean.FALSE);
        FACTORY = f;
    }

    /**
     * Parse the bytes of an OSF4 metablock body into a {@link Metablock}.
     *
     * @param metablockBytes raw bytes of the metablock (no magic-header line,
     *                       no block-stream bytes)
     * @return the parsed metablock with {@link Metablock#version()} == 4
     * @throws OsfException.MalformedFile   if the bytes are not valid XML, if the
     *                                      root element is not {@code <optimeas>},
     *                                      or if a required channel attribute is
     *                                      missing or has an invalid value
     * @throws OsfException.UnsupportedType if a channel carries a datatype that
     *                                      was removed from the spec
     */
    public Metablock parse(byte[] metablockBytes) {
        XMLStreamReader reader;
        try {
            reader = FACTORY.createXMLStreamReader(
                    new ByteArrayInputStream(metablockBytes));
        } catch (XMLStreamException e) {
            throw new OsfException.MalformedFile(
                    "OSF4 XML metablock: failed to create XML reader: " + e.getMessage(), e);
        }

        try {
            return doParse(reader);
        } catch (XMLStreamException e) {
            throw new OsfException.MalformedFile(
                    "OSF4 XML metablock parse error: " + e.getMessage(), e);
        } finally {
            try { reader.close(); } catch (XMLStreamException ignored) { }
        }
    }

    // -----------------------------------------------------------------------
    // main parse loop
    // -----------------------------------------------------------------------

    private static Metablock doParse(XMLStreamReader r) throws XMLStreamException {
        // advance to first START_ELEMENT — skip declaration, processing instructions, etc.
        while (r.hasNext()) {
            int event = r.next();
            if (event == XMLStreamConstants.START_ELEMENT) {
                break;
            }
        }

        if (!r.isStartElement()) {
            throw new OsfException.MalformedFile(
                    "OSF4 XML metablock: document contains no elements");
        }

        String root = r.getLocalName();
        if (!"optimeas".equals(root)) {
            throw new OsfException.MalformedFile(
                    "OSF4 XML metablock: root element must be <optimeas>, got <" + root + ">");
        }

        // Collect <optimeas ...> attributes → metadata map
        Map<String, String> metadata = collectOptimeasAttrs(r);

        List<ChannelDef> channels = new ArrayList<>();

        // Parse children of <optimeas>
        while (r.hasNext()) {
            int event = r.next();
            if (event == XMLStreamConstants.START_ELEMENT) {
                String elem = r.getLocalName();
                if ("channels".equals(elem)) {
                    parseChannels(r, channels);
                } else {
                    skipElement(r, elem);
                }
            } else if (event == XMLStreamConstants.END_ELEMENT) {
                // </optimeas>
                break;
            }
        }

        return new Metablock(4,
                Collections.unmodifiableMap(metadata),
                Collections.unmodifiableList(channels));
    }

    // -----------------------------------------------------------------------
    // <optimeas> attributes → metadata map
    // -----------------------------------------------------------------------

    private static Map<String, String> collectOptimeasAttrs(XMLStreamReader r) {
        Map<String, String> meta = new LinkedHashMap<>();
        int count = r.getAttributeCount();
        for (int i = 0; i < count; i++) {
            String key   = r.getAttributeLocalName(i);
            String value = r.getAttributeValue(i);
            if (value != null) {
                meta.put(key, value);
            }
        }
        return meta;
    }

    // -----------------------------------------------------------------------
    // <channels> → populate list
    // -----------------------------------------------------------------------

    private static void parseChannels(XMLStreamReader r, List<ChannelDef> channels)
            throws XMLStreamException {
        // cursor is on <channels ...>
        while (r.hasNext()) {
            int event = r.next();
            if (event == XMLStreamConstants.START_ELEMENT
                    || event == XMLStreamConstants.END_ELEMENT) {
                String elem = r.getLocalName();
                if (event == XMLStreamConstants.END_ELEMENT && "channels".equals(elem)) {
                    return;
                }
                if (event == XMLStreamConstants.START_ELEMENT) {
                    if ("channel".equals(elem)) {
                        ChannelDef ch = parseChannel(r, channels.size());
                        channels.add(ch);
                        // channel elements are typically empty (<channel .../>) but may
                        // appear as start+end pairs; either way consume through the end.
                        consumeToEndIfNeeded(r, "channel");
                    } else {
                        skipElement(r, elem);
                    }
                }
            }
        }
    }

    // -----------------------------------------------------------------------
    // <channel .../> → ChannelDef
    // -----------------------------------------------------------------------

    private static ChannelDef parseChannel(XMLStreamReader r, int position) {
        // Collect all attributes from the <channel> element
        Map<String, String> attrs = collectAttrs(r);

        // index — required
        String rawIndex = attrs.get(A_INDEX);
        if (rawIndex == null) {
            throw new OsfException.MalformedFile(
                    "OSF4 <channel> at position " + position
                            + " is missing required attribute \"index\"");
        }
        int index;
        try {
            long idx = Long.parseLong(rawIndex.trim());
            if (idx < 0 || idx > 0xFFFFL) {
                throw new OsfException.MalformedFile(
                        "OSF4 <channel> at position " + position
                                + " has index=" + rawIndex + " out of range 0..65535");
            }
            index = (int) idx;
        } catch (NumberFormatException e) {
            throw new OsfException.MalformedFile(
                    "OSF4 <channel> at position " + position
                            + " has non-numeric index=" + rawIndex);
        }

        // name — required
        String name = attrs.get(A_NAME);
        if (name == null) {
            throw new OsfException.MalformedFile(
                    "OSF4 <channel index=" + index
                            + "> is missing required attribute \"name\"");
        }

        // channeltype — required
        String rawChannelType = attrs.get(A_CHANNELTYPE);
        if (rawChannelType == null) {
            throw new OsfException.MalformedFile(
                    "OSF4 <channel name=\"" + name
                            + "\"> is missing required attribute \"channeltype\"");
        }
        ChannelType channelType = ChannelType.fromWireName(rawChannelType);

        // datatype — required; removed types throw UnsupportedType (propagated);
        // unknown (non-removed) types return DataType.UNSUPPORTED
        String rawDataType = attrs.get(A_DATATYPE);
        if (rawDataType == null) {
            throw new OsfException.MalformedFile(
                    "OSF4 <channel name=\"" + name
                            + "\"> is missing required attribute \"datatype\"");
        }
        DataType dataType = DataType.fromWireName(rawDataType);

        // sizeoflengthvalue — required, must be 2 or 4
        String rawSol = attrs.get(A_SIZEOFLENGTHVALUE);
        if (rawSol == null) {
            throw new OsfException.MalformedFile(
                    "OSF4 <channel name=\"" + name
                            + "\"> is missing required attribute \"sizeoflengthvalue\"");
        }
        int sizeOfLengthValue;
        try {
            long sol = Long.parseLong(rawSol.trim());
            if (sol != 2L && sol != 4L) {
                throw new OsfException.MalformedFile(
                        "OSF4 <channel name=\"" + name
                                + "\"> sizeoflengthvalue must be 2 or 4, got " + sol);
            }
            sizeOfLengthValue = (int) sol;
        } catch (NumberFormatException e) {
            throw new OsfException.MalformedFile(
                    "OSF4 <channel name=\"" + name
                            + "\"> has non-numeric sizeoflengthvalue=" + rawSol);
        }

        // timeincrement — optional; absent or 0 → non-equidistant
        long timeIncrementNs = 0L;
        String rawTi = attrs.get(A_TIMEINCREMENT);
        if (rawTi != null) {
            try {
                timeIncrementNs = Long.parseLong(rawTi.trim());
            } catch (NumberFormatException e) {
                throw new OsfException.MalformedFile(
                        "OSF4 <channel name=\"" + name
                                + "\"> has non-numeric timeincrement=" + rawTi);
            }
        }

        // physicalunit — optional
        String physicalUnit = attrs.get(A_PHYSICALUNIT);

        // attributes — collect remaining string fields not already consumed above
        Map<String, String> attributes = new LinkedHashMap<>();
        for (Map.Entry<String, String> entry : attrs.entrySet()) {
            if (!CHANNEL_TYPED_ATTRS.contains(entry.getKey())) {
                attributes.put(entry.getKey(), entry.getValue());
            }
        }

        // Preserve original wire strings for UNSUPPORTED types
        if (dataType == DataType.UNSUPPORTED) {
            attributes.put(A_DATATYPE, rawDataType);
        }
        if (channelType == ChannelType.UNSUPPORTED) {
            attributes.put(A_CHANNELTYPE, rawChannelType);
        }

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

    // -----------------------------------------------------------------------
    // helpers
    // -----------------------------------------------------------------------

    /** Collect all attributes on the current START_ELEMENT into a map. */
    private static Map<String, String> collectAttrs(XMLStreamReader r) {
        Map<String, String> map = new LinkedHashMap<>();
        int count = r.getAttributeCount();
        for (int i = 0; i < count; i++) {
            map.put(r.getAttributeLocalName(i), r.getAttributeValue(i));
        }
        return map;
    }

    /**
     * If the current element was a START_ELEMENT (not EMPTY_ELEMENT), advance
     * through all content until we hit the matching END_ELEMENT.
     *
     * <p>StAX does not expose a distinct EMPTY_ELEMENT event type — an empty
     * element ({@code <channel .../>}) fires START_ELEMENT immediately followed
     * by END_ELEMENT. The caller has already consumed the START_ELEMENT, so this
     * method just peeks ahead and consumes the END_ELEMENT if the element has
     * no child content (i.e. the next event is END_ELEMENT for the same name).
     * For elements with child content it skips everything until the matching end.
     */
    private static void consumeToEndIfNeeded(XMLStreamReader r, String elemName)
            throws XMLStreamException {
        // The START_ELEMENT has been consumed. Check whether the next event is the
        // matching END_ELEMENT (empty element / self-closing) or something else.
        if (!r.hasNext()) return;
        // peek: if next is END_ELEMENT for our element we just consume it.
        // if it's anything else we fall into the skip loop.
        int event = r.next();
        if (event == XMLStreamConstants.END_ELEMENT
                && elemName.equals(r.getLocalName())) {
            return; // clean end of <channel/>
        }
        // There was unexpected content inside <channel> — skip until </channel>
        int depth = 1;
        while (r.hasNext()) {
            event = r.next();
            if (event == XMLStreamConstants.START_ELEMENT) {
                depth++;
            } else if (event == XMLStreamConstants.END_ELEMENT) {
                depth--;
                if (depth == 0) return;
            }
        }
    }

    /**
     * Skip an element (and all its children) whose START_ELEMENT has just been
     * consumed by the caller.
     */
    private static void skipElement(XMLStreamReader r, String elemName)
            throws XMLStreamException {
        int depth = 1;
        while (r.hasNext() && depth > 0) {
            int event = r.next();
            if (event == XMLStreamConstants.START_ELEMENT) {
                depth++;
            } else if (event == XMLStreamConstants.END_ELEMENT) {
                depth--;
            }
        }
    }
}
