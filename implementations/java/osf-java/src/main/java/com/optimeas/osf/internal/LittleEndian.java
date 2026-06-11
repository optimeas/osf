// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH
package com.optimeas.osf.internal;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;

/** Central little-endian ByteBuffer helpers — the single point of byte-order policy. */
public final class LittleEndian {
    private LittleEndian() {}

    public static ByteBuffer wrap(byte[] bytes) {
        return ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN);
    }

    public static ByteBuffer allocate(int capacity) {
        return ByteBuffer.allocate(capacity).order(ByteOrder.LITTLE_ENDIAN);
    }
}
