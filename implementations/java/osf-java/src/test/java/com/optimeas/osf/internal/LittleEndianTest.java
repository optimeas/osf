// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH
package com.optimeas.osf.internal;

import static org.assertj.core.api.Assertions.assertThat;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import org.junit.jupiter.api.Test;

class LittleEndianTest {

    @Test
    void readsInt64LittleEndian() {
        byte[] bytes = ByteBuffer.allocate(8).order(ByteOrder.LITTLE_ENDIAN)
            .putLong(1_234_567_890_123L).array();
        ByteBuffer buf = LittleEndian.wrap(bytes);
        assertThat(buf.getLong()).isEqualTo(1_234_567_890_123L);
    }

    @Test
    void wrapEnforcesLittleEndianOrder() {
        ByteBuffer buf = LittleEndian.wrap(new byte[] {0x01, 0x00, 0x00, 0x00});
        assertThat(buf.order()).isEqualTo(ByteOrder.LITTLE_ENDIAN);
        assertThat(buf.getInt()).isEqualTo(1);
    }
}
