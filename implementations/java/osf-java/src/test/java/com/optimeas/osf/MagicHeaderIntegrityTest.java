// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH
package com.optimeas.osf;

import org.junit.jupiter.api.Test;

import java.nio.charset.StandardCharsets;

import static org.assertj.core.api.Assertions.*;

/**
 * Magic-header integrity-token tokenizer (OSF5 integrity profile level crc).
 * Mirrors the Rust {@code header.rs} token grammar and the C++ tokenizer.
 */
class MagicHeaderIntegrityTest {

    private static byte[] lineOnly(String text) {
        return (text + "\n").getBytes(StandardCharsets.US_ASCII);
    }

    @Test
    void plainOsf5HasNoIntegrity() {
        MagicHeader hdr = MagicHeaderParser.parse(lineOnly("OSF5 895"));
        assertThat(hdr.integrity()).isEqualTo(IntegrityProfile.NONE);
        assertThat(hdr.metablockCrc()).isNull();
    }

    @Test
    void parsesCrc32cToken() {
        MagicHeader hdr = MagicHeaderParser.parse(lineOnly("OSF5 727 crc32c:DFDB870F"));
        assertThat(hdr.version()).isEqualTo(OsfVersion.OSF5);
        assertThat(hdr.metablockLength()).isEqualTo(727L);
        assertThat(hdr.integrity()).isEqualTo(IntegrityProfile.CRC32C);
        assertThat(hdr.metablockCrc()).isEqualTo(0xDFDB870FL);
    }

    @Test
    void rejectsUnknownHeaderTokenWithDedicatedException() {
        assertThatThrownBy(() -> MagicHeaderParser.parse(lineOnly("OSF5 100 zz:01")))
                .isInstanceOf(OsfException.UnknownHeaderToken.class)
                .hasMessageContaining("unknown header token 'zz'");
    }

    @Test
    void unknownHeaderTokenIsAnOsfException() {
        // The dedicated exception must remain an OsfException so callers that
        // catch the base type still work.
        assertThatThrownBy(() -> MagicHeaderParser.parse(lineOnly("OSF5 100 bogus:ff")))
                .isInstanceOf(OsfException.class);
    }

    @Test
    void rejectsTokenOnOsf4Identifier() {
        assertThatThrownBy(() -> MagicHeaderParser.parse(lineOnly("OSF4 100 crc32c:DEADBEEF")))
                .isInstanceOf(OsfException.MalformedFile.class);
    }

    @Test
    void rejectsCrc32cLowercaseHex() {
        assertThatThrownBy(() -> MagicHeaderParser.parse(lineOnly("OSF5 100 crc32c:dfdb870f")))
                .isInstanceOf(OsfException.MalformedFile.class);
    }

    @Test
    void rejectsCrc32cWrongLength() {
        assertThatThrownBy(() -> MagicHeaderParser.parse(lineOnly("OSF5 100 crc32c:ABCD")))
                .isInstanceOf(OsfException.MalformedFile.class);
    }

    @Test
    void rejectsTrailingSpace() {
        assertThatThrownBy(() -> MagicHeaderParser.parse(lineOnly("OSF5 100 ")))
                .isInstanceOf(OsfException.MalformedFile.class);
    }

    @Test
    void rejectsDoubleSpace() {
        assertThatThrownBy(() -> MagicHeaderParser.parse(lineOnly("OSF5 100  crc32c:DFDB870F")))
                .isInstanceOf(OsfException.MalformedFile.class);
    }

    @Test
    void parsesCrc32cThenEd25519() {
        MagicHeader hdr = MagicHeaderParser.parse(
                lineOnly("OSF5 100 crc32c:DFDB870F ed25519:0123456789abcdef"));
        assertThat(hdr.integrity()).isEqualTo(IntegrityProfile.ED25519);
        assertThat(hdr.metablockCrc()).isEqualTo(0xDFDB870FL);
    }

    @Test
    void rejectsEd25519WithoutCrc32c() {
        assertThatThrownBy(() -> MagicHeaderParser.parse(
                lineOnly("OSF5 100 ed25519:0123456789abcdef")))
                .isInstanceOf(OsfException.MalformedFile.class);
    }

    @Test
    void rejectsEd25519WrongFormat() {
        assertThatThrownBy(() -> MagicHeaderParser.parse(
                lineOnly("OSF5 100 crc32c:DFDB870F ed25519:XYZ")))
                .isInstanceOf(OsfException.MalformedFile.class);
    }
}
