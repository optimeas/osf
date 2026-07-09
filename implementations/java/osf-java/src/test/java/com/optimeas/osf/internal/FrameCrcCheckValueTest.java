// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH
package com.optimeas.osf.internal;

import org.junit.jupiter.api.Test;

import java.nio.charset.StandardCharsets;
import java.util.zip.CRC32C;

import static org.assertj.core.api.Assertions.*;

/**
 * Documents that the CRC32C used for the integrity profile is byte-identical to
 * the Rust / C++ / Delphi implementations. The canonical CRC-32/ISCSI
 * (Castagnoli) check value over {@code "123456789"} is {@code 0xE3069283}; the
 * frame/metablock CRC compatibility with the Rust writer is additionally proven
 * transitively by {@code IntegrityReaderTest} reading the Rust reference files
 * with zero CRC failures.
 */
class FrameCrcCheckValueTest {

    @Test
    void crc32cCanonicalCheckValueMatchesReference() {
        CRC32C crc = new CRC32C();
        byte[] data = "123456789".getBytes(StandardCharsets.US_ASCII);
        crc.update(data, 0, data.length);
        assertThat(crc.getValue()).isEqualTo(0xE3069283L);
    }

    @Test
    void applyFrameCrcPatchesLengthAndAppendsCrc() {
        // A minimal frame [u16 ci=0][u16 len=1][control 0x00], sov=2.
        byte[] frame = {0x00, 0x00, 0x01, 0x00, 0x00};
        byte[] out = BlockEncoder.applyFrameCrc(frame, 2);
        // 4 CRC bytes appended.
        assertThat(out).hasSize(frame.length + 4);
        // Length field bumped from 1 to 5 (counts the CRC).
        int len = (out[2] & 0xFF) | ((out[3] & 0xFF) << 8);
        assertThat(len).isEqualTo(5);
        // The appended CRC is the CRC32C over the patched frame.
        CRC32C crc = new CRC32C();
        crc.update(out, 0, frame.length);
        long expected = crc.getValue();
        long stored = (out[5] & 0xFFL) | ((out[6] & 0xFFL) << 8)
                | ((out[7] & 0xFFL) << 16) | ((out[8] & 0xFFL) << 24);
        assertThat(stored).isEqualTo(expected);
    }
}
