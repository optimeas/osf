// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH
package com.optimeas.osf.internal;

import com.optimeas.osf.*;
import org.junit.jupiter.api.Test;

import static org.assertj.core.api.Assertions.*;

/**
 * TDD tests for {@link XmlMetablockParser}.
 *
 * <p>XML grammar confirmed from
 * {@code implementations/rust/osf-core/src/meta_xml.rs}:
 * <ul>
 *   <li>Root: {@code <optimeas>} with attrs {@code creator}, {@code created_utc}, etc.</li>
 *   <li>Channels container: {@code <channels count="N">}</li>
 *   <li>Per-channel: {@code <channel index="..." name="..." channeltype="..."
 *       datatype="..." sizeoflengthvalue="..." timeincrement="..."
 *       physicalunit="..."/>}</li>
 * </ul>
 */
class XmlMetablockParserTest {

    private final XmlMetablockParser parser = new XmlMetablockParser();

    private static byte[] utf8(String s) {
        return s.getBytes(java.nio.charset.StandardCharsets.UTF_8);
    }

    // -----------------------------------------------------------------------
    // minimal metablock — root + one scalar double channel
    // -----------------------------------------------------------------------

    @Test
    void parsesMinimalMetablock() {
        byte[] bytes = utf8("""
                <?xml version="1.0" encoding="UTF-8"?>
                <optimeas creator="test-writer" created_utc="2026-01-01T00:00:00Z">
                  <channels count="1">
                    <channel index="0" name="Temperature" channeltype="scalar"
                             datatype="double" sizeoflengthvalue="2"/>
                  </channels>
                </optimeas>
                """);

        Metablock mb = parser.parse(bytes);

        assertThat(mb.version()).isEqualTo(4);
        assertThat(mb.metadata()).containsEntry("creator", "test-writer");
        assertThat(mb.metadata()).containsEntry("created_utc", "2026-01-01T00:00:00Z");
        assertThat(mb.channels()).hasSize(1);

        ChannelDef ch = mb.channels().get(0);
        assertThat(ch.index()).isEqualTo(0);
        assertThat(ch.name()).isEqualTo("Temperature");
        assertThat(ch.dataType()).isEqualTo(DataType.DOUBLE);
        assertThat(ch.channelType()).isEqualTo(ChannelType.SCALAR);
        assertThat(ch.sizeOfLengthValue()).isEqualTo(2);
        assertThat(ch.timeIncrementNs()).isEqualTo(0L);
        assertThat(ch.isEquidistant()).isFalse();
    }

    // -----------------------------------------------------------------------
    // equidistant channel — timeincrement present
    // -----------------------------------------------------------------------

    @Test
    void equidistantChannelHasTimeIncrementAndIsEquidistant() {
        byte[] bytes = utf8("""
                <?xml version="1.0"?>
                <optimeas>
                  <channels count="1">
                    <channel index="0" name="Vibration" channeltype="equidistant"
                             datatype="float" sizeoflengthvalue="2" timeincrement="100000"/>
                  </channels>
                </optimeas>
                """);

        Metablock mb = parser.parse(bytes);
        ChannelDef ch = mb.channels().get(0);
        assertThat(ch.channelType()).isEqualTo(ChannelType.EQUIDISTANT);
        assertThat(ch.timeIncrementNs()).isEqualTo(100_000L);
        assertThat(ch.isEquidistant()).isTrue();
    }

    // -----------------------------------------------------------------------
    // physicalunit is captured in ChannelDef
    // -----------------------------------------------------------------------

    @Test
    void channelPhysicalUnitIsParsed() {
        byte[] bytes = utf8("""
                <?xml version="1.0"?>
                <optimeas>
                  <channels count="1">
                    <channel index="0" name="Pressure" channeltype="scalar"
                             datatype="double" sizeoflengthvalue="2" physicalunit="bar"/>
                  </channels>
                </optimeas>
                """);

        Metablock mb = parser.parse(bytes);
        assertThat(mb.channels().get(0).physicalUnit()).isEqualTo("bar");
    }

    // -----------------------------------------------------------------------
    // removed datatype (candata) → UnsupportedType
    // -----------------------------------------------------------------------

    @Test
    void removedDatatypeCandata_throwsUnsupportedType() {
        byte[] bytes = utf8("""
                <?xml version="1.0"?>
                <optimeas>
                  <channels count="1">
                    <channel index="0" name="CanChannel" channeltype="scalar"
                             datatype="candata" sizeoflengthvalue="4"/>
                  </channels>
                </optimeas>
                """);

        assertThatThrownBy(() -> parser.parse(bytes))
                .isInstanceOf(OsfException.UnsupportedType.class);
    }

    // -----------------------------------------------------------------------
    // removed datatype (gpsdata) → UnsupportedType
    // -----------------------------------------------------------------------

    @Test
    void removedDatatypeGpsdata_throwsUnsupportedType() {
        byte[] bytes = utf8("""
                <?xml version="1.0"?>
                <optimeas>
                  <channels count="1">
                    <channel index="0" name="GPS" channeltype="scalar"
                             datatype="gpsdata" sizeoflengthvalue="4"/>
                  </channels>
                </optimeas>
                """);

        assertThatThrownBy(() -> parser.parse(bytes))
                .isInstanceOf(OsfException.UnsupportedType.class);
    }

    // -----------------------------------------------------------------------
    // unknown channeltype → UNSUPPORTED + wire string preserved in attributes
    // -----------------------------------------------------------------------

    @Test
    void unknownChanneltype_parsesAsUnsupportedAndPreservesWireString() {
        byte[] bytes = utf8("""
                <?xml version="1.0"?>
                <optimeas>
                  <channels count="1">
                    <channel index="0" name="VectorCh" channeltype="vector"
                             datatype="double" sizeoflengthvalue="2"/>
                  </channels>
                </optimeas>
                """);

        Metablock mb = parser.parse(bytes);
        ChannelDef ch = mb.channels().get(0);
        assertThat(ch.channelType()).isEqualTo(ChannelType.UNSUPPORTED);
        assertThat(ch.attributes()).containsEntry("channeltype", "vector");
    }

    // -----------------------------------------------------------------------
    // malformed XML → MalformedFile
    // -----------------------------------------------------------------------

    @Test
    void malformedXmlThrowsMalformedFile() {
        byte[] bytes = utf8("<optimeas><channels count=\"1\"><channel index");

        assertThatThrownBy(() -> parser.parse(bytes))
                .isInstanceOf(OsfException.MalformedFile.class);
    }

    // -----------------------------------------------------------------------
    // wrong root element → MalformedFile
    // -----------------------------------------------------------------------

    @Test
    void wrongRootElement_throwsMalformedFile() {
        byte[] bytes = utf8("""
                <?xml version="1.0"?>
                <notoptimeas>
                  <channels count="0"/>
                </notoptimeas>
                """);

        assertThatThrownBy(() -> parser.parse(bytes))
                .isInstanceOf(OsfException.MalformedFile.class);
    }

    // -----------------------------------------------------------------------
    // metadata attributes collected into metadata map
    // -----------------------------------------------------------------------

    @Test
    void optimeasAttributesCollectedAsMetadata() {
        byte[] bytes = utf8("""
                <?xml version="1.0"?>
                <optimeas creator="osftool" created_utc="2026-06-01T00:00:00Z"
                          tag="test" reason="unit-test">
                  <channels count="0"/>
                </optimeas>
                """);

        Metablock mb = parser.parse(bytes);
        assertThat(mb.metadata())
                .containsEntry("creator", "osftool")
                .containsEntry("created_utc", "2026-06-01T00:00:00Z")
                .containsEntry("tag", "test")
                .containsEntry("reason", "unit-test");
        assertThat(mb.channels()).isEmpty();
    }

    // -----------------------------------------------------------------------
    // multiple channels parsed in order
    // -----------------------------------------------------------------------

    @Test
    void multipleChannels_parsedInOrder() {
        byte[] bytes = utf8("""
                <?xml version="1.0"?>
                <optimeas>
                  <channels count="2">
                    <channel index="0" name="ch0" channeltype="scalar"
                             datatype="int32" sizeoflengthvalue="2"/>
                    <channel index="1" name="ch1" channeltype="timestamped"
                             datatype="string" sizeoflengthvalue="4"/>
                  </channels>
                </optimeas>
                """);

        Metablock mb = parser.parse(bytes);
        assertThat(mb.channels()).hasSize(2);
        assertThat(mb.channels().get(0).name()).isEqualTo("ch0");
        assertThat(mb.channels().get(0).dataType()).isEqualTo(DataType.INT32);
        assertThat(mb.channels().get(1).name()).isEqualTo("ch1");
        assertThat(mb.channels().get(1).channelType()).isEqualTo(ChannelType.TIMESTAMPED);
        assertThat(mb.channels().get(1).sizeOfLengthValue()).isEqualTo(4);
    }

    // -----------------------------------------------------------------------
    // extra unknown channel attributes go into attributes map
    // -----------------------------------------------------------------------

    @Test
    void unknownChannelAttributesCollectedInAttributes() {
        byte[] bytes = utf8("""
                <?xml version="1.0"?>
                <optimeas>
                  <channels count="1">
                    <channel index="0" name="ch" channeltype="scalar"
                             datatype="double" sizeoflengthvalue="2"
                             displayname="Channel Display" comment="some comment"/>
                  </channels>
                </optimeas>
                """);

        Metablock mb = parser.parse(bytes);
        ChannelDef ch = mb.channels().get(0);
        assertThat(ch.attributes())
                .containsEntry("displayname", "Channel Display")
                .containsEntry("comment", "some comment");
    }

    // -----------------------------------------------------------------------
    // invalid sizeoflengthvalue → MalformedFile
    // -----------------------------------------------------------------------

    @Test
    void invalidSizeOfLengthValue_throwsMalformedFile() {
        byte[] bytes = utf8("""
                <?xml version="1.0"?>
                <optimeas>
                  <channels count="1">
                    <channel index="0" name="ch" channeltype="scalar"
                             datatype="double" sizeoflengthvalue="3"/>
                  </channels>
                </optimeas>
                """);

        assertThatThrownBy(() -> parser.parse(bytes))
                .isInstanceOf(OsfException.MalformedFile.class);
    }

    // -----------------------------------------------------------------------
    // bytearray alias is normalised to BINARY
    // -----------------------------------------------------------------------

    @Test
    void bytearrayAliasNormalisedToBinary() {
        byte[] bytes = utf8("""
                <?xml version="1.0"?>
                <optimeas>
                  <channels count="1">
                    <channel index="0" name="raw" channeltype="scalar"
                             datatype="bytearray" sizeoflengthvalue="4"/>
                  </channels>
                </optimeas>
                """);

        Metablock mb = parser.parse(bytes);
        assertThat(mb.channels().get(0).dataType()).isEqualTo(DataType.BINARY);
    }

    // -----------------------------------------------------------------------
    // unknown datatype → UNSUPPORTED + wire string preserved in attributes
    // -----------------------------------------------------------------------

    @Test
    void unknownDatatype_parsesAsUnsupportedAndPreservesWireString() {
        byte[] bytes = utf8("""
                <?xml version="1.0"?>
                <optimeas>
                  <channels count="1">
                    <channel index="0" name="FutureCh" channeltype="scalar"
                             datatype="somefuturetype" sizeoflengthvalue="2"/>
                  </channels>
                </optimeas>
                """);

        Metablock mb = parser.parse(bytes);
        ChannelDef ch = mb.channels().get(0);
        assertThat(ch.dataType()).isEqualTo(DataType.UNSUPPORTED);
        assertThat(ch.attributes()).containsEntry("datatype", "somefuturetype");
    }

    // -----------------------------------------------------------------------
    // no channels element → empty list
    // -----------------------------------------------------------------------

    @Test
    void noChannelsElement_returnsEmptyList() {
        byte[] bytes = utf8("""
                <?xml version="1.0"?>
                <optimeas creator="x"/>
                """);

        Metablock mb = parser.parse(bytes);
        assertThat(mb.channels()).isEmpty();
    }
}
