// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH
package com.optimeas.osf.internal;

import com.optimeas.osf.ChannelDef;
import com.optimeas.osf.ChannelType;
import com.optimeas.osf.DataType;
import com.optimeas.osf.Metablock;
import org.junit.jupiter.api.Test;

import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

import static org.assertj.core.api.Assertions.assertThat;

/**
 * TDD tests for {@link MetablockBuilder}.
 *
 * <p>The builder's contract is pinned by round-tripping its JSON output through
 * the trusted J3 {@link JsonMetablockParser}: channel names / data types /
 * sizes / equidistant timeincrement survive, and the {@code channeltype} of a
 * non-equidistant channel is normalised to {@code scalar} (the reference
 * convention, confirmed in {@code writer_common.cpp build_metablock} and
 * {@code writer.rs channel_type_to_wire}).
 */
class MetablockBuilderTest {

    private final JsonMetablockParser parser = new JsonMetablockParser();

    private static ChannelDef ch(int index, String name, DataType dt, ChannelType ct,
                                 int sov, long timeIncr) {
        return new ChannelDef(index, name, dt, ct, sov, timeIncr, null, Map.of());
    }

    @Test
    void buildsParseableOsf5MetablockWithChannels() {
        List<ChannelDef> chans = List.of(
                ch(0, "Sensor/Temperature", DataType.DOUBLE, ChannelType.TIMESTAMPED, 2, 0L),
                ch(1, "Sensor/Wave", DataType.FLOAT, ChannelType.EQUIDISTANT, 2, 1_000_000L));

        byte[] json = MetablockBuilder.buildOsf5Json(5,
                Map.of("creator", "osf-java/test", "tag", "default"), chans);

        Metablock mb = parser.parse(json);
        assertThat(mb.version()).isEqualTo(5);
        assertThat(mb.metadata()).containsEntry("creator", "osf-java/test");
        assertThat(mb.channels()).hasSize(2);

        ChannelDef c0 = mb.channels().get(0);
        assertThat(c0.index()).isEqualTo(0);
        assertThat(c0.name()).isEqualTo("Sensor/Temperature");
        assertThat(c0.dataType()).isEqualTo(DataType.DOUBLE);
        assertThat(c0.sizeOfLengthValue()).isEqualTo(2);
    }

    @Test
    void normalisesNonEquidistantChannelTypeToScalar() {
        // TIMESTAMPED and SCALAR inputs both serialise as "scalar".
        List<ChannelDef> chans = List.of(
                ch(0, "a", DataType.DOUBLE, ChannelType.TIMESTAMPED, 2, 0L),
                ch(1, "b", DataType.INT32, ChannelType.SCALAR, 2, 0L));
        byte[] json = MetablockBuilder.buildOsf5Json(5, Map.of(), chans);
        Metablock mb = parser.parse(json);
        assertThat(mb.channels().get(0).channelType()).isEqualTo(ChannelType.SCALAR);
        assertThat(mb.channels().get(1).channelType()).isEqualTo(ChannelType.SCALAR);
    }

    @Test
    void preservesEquidistantChannelTypeAndTimeincrement() {
        List<ChannelDef> chans = List.of(
                ch(0, "wave", DataType.DOUBLE, ChannelType.EQUIDISTANT, 4, 2_000_000L));
        byte[] json = MetablockBuilder.buildOsf5Json(5, Map.of(), chans);
        Metablock mb = parser.parse(json);
        ChannelDef c0 = mb.channels().get(0);
        assertThat(c0.channelType()).isEqualTo(ChannelType.EQUIDISTANT);
        assertThat(c0.timeIncrementNs()).isEqualTo(2_000_000L);
        assertThat(c0.isEquidistant()).isTrue();
        assertThat(c0.sizeOfLengthValue()).isEqualTo(4);
    }

    @Test
    void omitsTimeincrementForNonEquidistant() {
        List<ChannelDef> chans = List.of(
                ch(0, "a", DataType.DOUBLE, ChannelType.SCALAR, 2, 0L));
        byte[] json = MetablockBuilder.buildOsf5Json(5, Map.of(), chans);
        Metablock mb = parser.parse(json);
        // timeincrement absent on the wire → parsed as 0 / non-equidistant.
        assertThat(mb.channels().get(0).timeIncrementNs()).isEqualTo(0L);
        assertThat(mb.channels().get(0).isEquidistant()).isFalse();
    }

    @Test
    void carriesFileMetadataAndPhysicalUnit() {
        Map<String, String> meta = new LinkedHashMap<>();
        meta.put("creator", "osf-java/1.0");
        meta.put("reason", "unit-test");
        List<ChannelDef> chans = List.of(
                new ChannelDef(0, "Temp", DataType.DOUBLE, ChannelType.SCALAR, 2, 0L,
                        "°C", Map.of()));
        byte[] json = MetablockBuilder.buildOsf5Json(5, meta, chans);
        Metablock mb = parser.parse(json);
        assertThat(mb.metadata()).containsEntry("reason", "unit-test");
        assertThat(mb.channels().get(0).physicalUnit()).isEqualTo("°C");
    }

    @Test
    void serialisesEveryNumericAndVariableDataType() {
        DataType[] all = {
                DataType.BOOL, DataType.INT8, DataType.INT16, DataType.INT32, DataType.INT64,
                DataType.UINT8, DataType.UINT16, DataType.UINT32, DataType.UINT64,
                DataType.FLOAT, DataType.DOUBLE, DataType.STRING, DataType.BINARY,
                DataType.GPS_LOCATION,
        };
        List<ChannelDef> chans = new java.util.ArrayList<>();
        for (int i = 0; i < all.length; i++) {
            chans.add(ch(i, "c" + i, all[i], ChannelType.SCALAR, 2, 0L));
        }
        byte[] json = MetablockBuilder.buildOsf5Json(5, Map.of(), chans);
        Metablock mb = parser.parse(json);
        for (int i = 0; i < all.length; i++) {
            assertThat(mb.channels().get(i).dataType()).isEqualTo(all[i]);
        }
    }
}
