// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH
package com.optimeas.osf.internal;

import com.optimeas.osf.*;
import org.junit.jupiter.api.Test;

import static org.assertj.core.api.Assertions.*;

/**
 * TDD tests for {@link JsonMetablockParser}.
 *
 * JSON field names are taken verbatim from the wire format confirmed in
 * implementations/rust/osf-core/src/meta_json.rs.
 */
class JsonMetablockParserTest {

    private final JsonMetablockParser parser = new JsonMetablockParser();

    // -----------------------------------------------------------------------
    // helpers
    // -----------------------------------------------------------------------

    private static byte[] utf8(String s) {
        return s.getBytes(java.nio.charset.StandardCharsets.UTF_8);
    }

    // -----------------------------------------------------------------------
    // minimal metablock — top-level structure + one scalar double channel
    // -----------------------------------------------------------------------

    @Test
    void parsesMinimalMetablock() {
        byte[] bytes = utf8("""
                {
                  "osf": {
                    "format": "osf5",
                    "version": 5,
                    "file": { "creator": "test-writer" },
                    "channels": [
                      {
                        "index": 0,
                        "name": "Temperature",
                        "channeltype": "scalar",
                        "datatype": "double",
                        "sizeoflengthvalue": 2
                      }
                    ]
                  }
                }
                """);

        Metablock mb = parser.parse(bytes);

        assertThat(mb.version()).isEqualTo(5);
        assertThat(mb.metadata()).containsEntry("creator", "test-writer");
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
    // equidistant channel — timeincrement present and > 0
    // -----------------------------------------------------------------------

    @Test
    void equidistantChannelHasTimeIncrementAndIsEquidistant() {
        byte[] bytes = utf8("""
                {
                  "osf": {
                    "format": "osf5",
                    "version": 5,
                    "file": {},
                    "channels": [
                      {
                        "index": 0,
                        "name": "Vibration",
                        "channeltype": "equidistant",
                        "datatype": "float",
                        "sizeoflengthvalue": 2,
                        "timeincrement": 100000
                      }
                    ]
                  }
                }
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
                {
                  "osf": {
                    "format": "osf5",
                    "version": 5,
                    "file": {},
                    "channels": [
                      {
                        "index": 0,
                        "name": "Pressure",
                        "channeltype": "scalar",
                        "datatype": "double",
                        "sizeoflengthvalue": 2,
                        "physicalunit": "bar"
                      }
                    ]
                  }
                }
                """);

        Metablock mb = parser.parse(bytes);
        assertThat(mb.channels().get(0).physicalUnit()).isEqualTo("bar");
    }

    // -----------------------------------------------------------------------
    // removed datatype → UnsupportedType
    // -----------------------------------------------------------------------

    @Test
    void removedDatatypeCandata_throwsUnsupportedType() {
        byte[] bytes = utf8("""
                {
                  "osf": {
                    "format": "osf5",
                    "version": 5,
                    "file": {},
                    "channels": [
                      {
                        "index": 0,
                        "name": "CanChannel",
                        "channeltype": "scalar",
                        "datatype": "candata",
                        "sizeoflengthvalue": 4
                      }
                    ]
                  }
                }
                """);

        assertThatThrownBy(() -> parser.parse(bytes))
                .isInstanceOf(OsfException.UnsupportedType.class);
    }

    @Test
    void removedDatatypeGpsdata_throwsUnsupportedType() {
        byte[] bytes = utf8("""
                {
                  "osf": {
                    "format": "osf5",
                    "version": 5,
                    "file": {},
                    "channels": [
                      {
                        "index": 0,
                        "name": "GPS",
                        "channeltype": "scalar",
                        "datatype": "gpsdata",
                        "sizeoflengthvalue": 4
                      }
                    ]
                  }
                }
                """);

        assertThatThrownBy(() -> parser.parse(bytes))
                .isInstanceOf(OsfException.UnsupportedType.class);
    }

    // -----------------------------------------------------------------------
    // malformed JSON → MalformedFile
    // -----------------------------------------------------------------------

    @Test
    void malformedJsonThrowsMalformedFile() {
        byte[] bytes = utf8("{ this is not valid json }");

        assertThatThrownBy(() -> parser.parse(bytes))
                .isInstanceOf(OsfException.MalformedFile.class);
    }

    // -----------------------------------------------------------------------
    // missing "osf" envelope → MalformedFile
    // -----------------------------------------------------------------------

    @Test
    void missingOsfEnvelope_throwsMalformedFile() {
        byte[] bytes = utf8("""
                { "format": "osf5", "version": 5, "channels": [] }
                """);

        assertThatThrownBy(() -> parser.parse(bytes))
                .isInstanceOf(OsfException.MalformedFile.class);
    }

    // -----------------------------------------------------------------------
    // invalid sizeoflengthvalue → MalformedFile
    // -----------------------------------------------------------------------

    @Test
    void invalidSizeOfLengthValue_throwsMalformedFile() {
        byte[] bytes = utf8("""
                {
                  "osf": {
                    "format": "osf5",
                    "version": 5,
                    "file": {},
                    "channels": [
                      {
                        "index": 0,
                        "name": "ch",
                        "channeltype": "scalar",
                        "datatype": "double",
                        "sizeoflengthvalue": 3
                      }
                    ]
                  }
                }
                """);

        assertThatThrownBy(() -> parser.parse(bytes))
                .isInstanceOf(OsfException.MalformedFile.class);
    }

    // -----------------------------------------------------------------------
    // empty channels array is accepted
    // -----------------------------------------------------------------------

    @Test
    void noChannels_parsesOk() {
        byte[] bytes = utf8("""
                {
                  "osf": {
                    "format": "osf5",
                    "version": 5,
                    "file": {},
                    "channels": []
                  }
                }
                """);

        Metablock mb = parser.parse(bytes);
        assertThat(mb.channels()).isEmpty();
    }

    // -----------------------------------------------------------------------
    // "file" block absent — still parses
    // -----------------------------------------------------------------------

    @Test
    void noFileBlock_parsesOk() {
        byte[] bytes = utf8("""
                {
                  "osf": {
                    "format": "osf5",
                    "version": 5,
                    "channels": []
                  }
                }
                """);

        Metablock mb = parser.parse(bytes);
        assertThat(mb.metadata()).isEmpty();
    }

    // -----------------------------------------------------------------------
    // file block metadata fields collected into metadata map
    // -----------------------------------------------------------------------

    @Test
    void fileBlockMetadataIsMapped() {
        byte[] bytes = utf8("""
                {
                  "osf": {
                    "format": "osf5",
                    "version": 5,
                    "file": {
                      "creator": "osftool",
                      "created_utc": "2026-01-01T00:00:00Z",
                      "tag": "test",
                      "reason": "unit-test"
                    },
                    "channels": []
                  }
                }
                """);

        Metablock mb = parser.parse(bytes);
        assertThat(mb.metadata())
                .containsEntry("creator", "osftool")
                .containsEntry("created_utc", "2026-01-01T00:00:00Z")
                .containsEntry("tag", "test")
                .containsEntry("reason", "unit-test");
    }

    // -----------------------------------------------------------------------
    // extra unknown channel fields go into attributes
    // -----------------------------------------------------------------------

    @Test
    void unknownChannelStringFieldsCollectedInAttributes() {
        byte[] bytes = utf8("""
                {
                  "osf": {
                    "format": "osf5",
                    "version": 5,
                    "file": {},
                    "channels": [
                      {
                        "index": 0,
                        "name": "ch",
                        "channeltype": "scalar",
                        "datatype": "double",
                        "sizeoflengthvalue": 2,
                        "displayname": "Channel Display",
                        "comment": "some comment"
                      }
                    ]
                  }
                }
                """);

        Metablock mb = parser.parse(bytes);
        ChannelDef ch = mb.channels().get(0);
        assertThat(ch.attributes())
                .containsEntry("displayname", "Channel Display")
                .containsEntry("comment", "some comment");
    }

    // -----------------------------------------------------------------------
    // multiple channels with different index values
    // -----------------------------------------------------------------------

    @Test
    void multipleChannels_parsedInOrder() {
        byte[] bytes = utf8("""
                {
                  "osf": {
                    "format": "osf5",
                    "version": 5,
                    "file": {},
                    "channels": [
                      { "index": 0, "name": "ch0", "channeltype": "scalar",
                        "datatype": "int32", "sizeoflengthvalue": 2 },
                      { "index": 1, "name": "ch1", "channeltype": "timestamped",
                        "datatype": "string", "sizeoflengthvalue": 4 }
                    ]
                  }
                }
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
    // forward-compat: unknown channeltype → UNSUPPORTED, wire string in attributes
    // -----------------------------------------------------------------------

    @Test
    void unknownChanneltype_parsesAsUnsupportedAndPreservesWireString() {
        byte[] bytes = utf8("""
                {
                  "osf": {
                    "format": "osf5",
                    "version": 5,
                    "file": {},
                    "channels": [
                      {
                        "index": 0,
                        "name": "VectorCh",
                        "channeltype": "vector",
                        "datatype": "double",
                        "sizeoflengthvalue": 2
                      }
                    ]
                  }
                }
                """);

        Metablock mb = parser.parse(bytes);
        ChannelDef ch = mb.channels().get(0);
        assertThat(ch.channelType()).isEqualTo(ChannelType.UNSUPPORTED);
        assertThat(ch.attributes()).containsEntry("channeltype", "vector");
    }

    // -----------------------------------------------------------------------
    // forward-compat: unknown datatype → UNSUPPORTED, wire string in attributes
    // -----------------------------------------------------------------------

    @Test
    void unknownDatatype_parsesAsUnsupportedAndPreservesWireString() {
        byte[] bytes = utf8("""
                {
                  "osf": {
                    "format": "osf5",
                    "version": 5,
                    "file": {},
                    "channels": [
                      {
                        "index": 0,
                        "name": "FutureCh",
                        "channeltype": "scalar",
                        "datatype": "somefuturetype",
                        "sizeoflengthvalue": 2
                      }
                    ]
                  }
                }
                """);

        Metablock mb = parser.parse(bytes);
        ChannelDef ch = mb.channels().get(0);
        assertThat(ch.dataType()).isEqualTo(DataType.UNSUPPORTED);
        assertThat(ch.attributes()).containsEntry("datatype", "somefuturetype");
    }

    // -----------------------------------------------------------------------
    // bytearray alias is normalised to BINARY
    // -----------------------------------------------------------------------

    @Test
    void bytearrayAliasNormalisedToBinary() {
        byte[] bytes = utf8("""
                {
                  "osf": {
                    "format": "osf5",
                    "version": 5,
                    "file": {},
                    "channels": [
                      { "index": 0, "name": "raw", "channeltype": "scalar",
                        "datatype": "bytearray", "sizeoflengthvalue": 4 }
                    ]
                  }
                }
                """);

        Metablock mb = parser.parse(bytes);
        assertThat(mb.channels().get(0).dataType()).isEqualTo(DataType.BINARY);
    }
}
