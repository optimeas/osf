// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH
package com.optimeas.osf;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;

import org.junit.jupiter.api.Test;

class DataTypeTest {

    @Test
    void parsesCanonicalWireNames() {
        assertThat(DataType.fromWireName("double")).isEqualTo(DataType.DOUBLE);
        assertThat(DataType.fromWireName("uint64")).isEqualTo(DataType.UINT64);
        assertThat(DataType.fromWireName("gpslocation")).isEqualTo(DataType.GPS_LOCATION);
        assertThat(DataType.fromWireName("binary")).isEqualTo(DataType.BINARY);
    }

    @Test
    void acceptsBytearrayAsAliasForBinary() {
        assertThat(DataType.fromWireName("bytearray")).isEqualTo(DataType.BINARY);
    }

    @Test
    void rejectsRemovedTypesWithClearMessage() {
        assertThatThrownBy(() -> DataType.fromWireName("candata"))
            .isInstanceOf(OsfException.UnsupportedType.class)
            .hasMessageContaining("candata")
            .hasMessageContaining("removed");
    }

    @Test
    void rejectsUnknownType() {
        assertThatThrownBy(() -> DataType.fromWireName("wat"))
            .isInstanceOf(OsfException.UnsupportedType.class);
    }
}
