// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH
package com.optimeas.osf;

import org.junit.jupiter.api.Test;

import java.io.ByteArrayInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;

import static org.assertj.core.api.Assertions.*;

class MagicHeaderParserTest {

    // -----------------------------------------------------------------------
    // Helpers
    // -----------------------------------------------------------------------

    private static byte[] line(String text) {
        // Appends the metablock content right after the header line so we can
        // verify the stream is left at the right position.
        return (text + "\n{\"metablock\":true}").getBytes(StandardCharsets.US_ASCII);
    }

    private static byte[] lineOnly(String text) {
        return (text + "\n").getBytes(StandardCharsets.US_ASCII);
    }

    // -----------------------------------------------------------------------
    // OSF5 detection
    // -----------------------------------------------------------------------

    @Test
    void parsesOsf5FromByteArray() {
        MagicHeader hdr = MagicHeaderParser.parse(line("OSF5 895"));
        assertThat(hdr.version()).isEqualTo(OsfVersion.OSF5);
        assertThat(hdr.metablockLength()).isEqualTo(895L);
    }

    @Test
    void parsesOsf5FromInputStream() throws IOException {
        byte[] bytes = line("OSF5 7");
        InputStream in = new ByteArrayInputStream(bytes);
        MagicHeader hdr = MagicHeaderParser.parse(in);
        assertThat(hdr.version()).isEqualTo(OsfVersion.OSF5);
        assertThat(hdr.metablockLength()).isEqualTo(7L);
        // Stream must be positioned at the first metablock byte, not consumed further.
        byte[] rest = in.readAllBytes();
        assertThat(new String(rest, StandardCharsets.US_ASCII)).isEqualTo("{\"metablock\":true}");
    }

    // -----------------------------------------------------------------------
    // OSF4 detection — all three identifiers
    // -----------------------------------------------------------------------

    @Test
    void parsesOsf4ShortIdentifier() {
        MagicHeader hdr = MagicHeaderParser.parse(lineOnly("OSF4 928"));
        assertThat(hdr.version()).isEqualTo(OsfVersion.OSF4);
        assertThat(hdr.metablockLength()).isEqualTo(928L);
    }

    @Test
    void parsesOsf4OceanStreamFormat4() {
        MagicHeader hdr = MagicHeaderParser.parse(lineOnly("OCEAN_STREAM_FORMAT4 26279"));
        assertThat(hdr.version()).isEqualTo(OsfVersion.OSF4);
        assertThat(hdr.metablockLength()).isEqualTo(26279L);
    }

    @Test
    void parsesOsf4OceanStreamingFormat4() {
        MagicHeader hdr = MagicHeaderParser.parse(lineOnly("OCEAN_STREAMING_FORMAT4 12345"));
        assertThat(hdr.version()).isEqualTo(OsfVersion.OSF4);
        assertThat(hdr.metablockLength()).isEqualTo(12345L);
    }

    // -----------------------------------------------------------------------
    // headerByteLength — must equal number of bytes consumed (incl. '\n')
    // -----------------------------------------------------------------------

    @Test
    void headerByteLengthEqualsConsumedBytes() {
        // "OSF5 42\n" = 8 bytes
        byte[] data = lineOnly("OSF5 42");
        MagicHeader hdr = MagicHeaderParser.parse(data);
        assertThat(hdr.headerByteLength()).isEqualTo("OSF5 42\n".length());
    }

    @Test
    void headerByteLengthCrLf() {
        // "OSF5 42\r\n" = 9 bytes consumed
        byte[] data = ("OSF5 42\r\n").getBytes(StandardCharsets.US_ASCII);
        MagicHeader hdr = MagicHeaderParser.parse(data);
        assertThat(hdr.headerByteLength()).isEqualTo(9);
    }

    // -----------------------------------------------------------------------
    // CRLF tolerance
    // -----------------------------------------------------------------------

    @Test
    void toleratesCrLfTerminator() {
        byte[] data = ("OSF5 42\r\n").getBytes(StandardCharsets.US_ASCII);
        MagicHeader hdr = MagicHeaderParser.parse(data);
        assertThat(hdr.version()).isEqualTo(OsfVersion.OSF5);
        assertThat(hdr.metablockLength()).isEqualTo(42L);
    }

    // -----------------------------------------------------------------------
    // Stream positioned exactly at metablock start
    // -----------------------------------------------------------------------

    @Test
    void streamIsPositionedAfterHeaderLine() throws IOException {
        // Header = "OSF5 7\n" (7 bytes), followed by "METABLOCK"
        byte[] bytes = ("OSF5 7\nMETABLOCK").getBytes(StandardCharsets.US_ASCII);
        InputStream in = new ByteArrayInputStream(bytes);
        MagicHeaderParser.parse(in);
        byte[] rest = in.readAllBytes();
        assertThat(new String(rest, StandardCharsets.US_ASCII)).isEqualTo("METABLOCK");
    }

    // -----------------------------------------------------------------------
    // metablockLength matches declared length
    // -----------------------------------------------------------------------

    @Test
    void metablockLengthMatchesDeclared() {
        long declared = 123456789L;
        MagicHeader hdr = MagicHeaderParser.parse(lineOnly("OSF5 " + declared));
        assertThat(hdr.metablockLength()).isEqualTo(declared);
    }

    @Test
    void metablockLengthZeroIsValid() {
        MagicHeader hdr = MagicHeaderParser.parse(lineOnly("OSF5 0"));
        assertThat(hdr.metablockLength()).isEqualTo(0L);
    }

    // -----------------------------------------------------------------------
    // Error cases — MalformedFile
    // -----------------------------------------------------------------------

    @Test
    void rejectsUnknownIdentifier() {
        byte[] data = lineOnly("OSF99 100");
        assertThatThrownBy(() -> MagicHeaderParser.parse(data))
                .isInstanceOf(OsfException.MalformedFile.class);
    }

    @Test
    void rejectsGarbage() {
        byte[] data = "not an OSF file at all\n".getBytes(StandardCharsets.US_ASCII);
        assertThatThrownBy(() -> MagicHeaderParser.parse(data))
                .isInstanceOf(OsfException.MalformedFile.class);
    }

    @Test
    void rejectsMissingLength() {
        byte[] data = "OSF5\n".getBytes(StandardCharsets.US_ASCII);
        assertThatThrownBy(() -> MagicHeaderParser.parse(data))
                .isInstanceOf(OsfException.MalformedFile.class);
    }

    @Test
    void rejectsNonNumericLength() {
        byte[] data = lineOnly("OSF5 abc");
        assertThatThrownBy(() -> MagicHeaderParser.parse(data))
                .isInstanceOf(OsfException.MalformedFile.class);
    }

    @Test
    void rejectsTruncatedInput() {
        byte[] data = "OSF5 895".getBytes(StandardCharsets.US_ASCII); // no '\n'
        assertThatThrownBy(() -> MagicHeaderParser.parse(data))
                .isInstanceOf(OsfException.MalformedFile.class);
    }

    @Test
    void rejectsRunawayInputWithoutNewline() {
        // > 128 bytes, no newline — should trigger the length cap
        byte[] data = new byte[140];
        java.util.Arrays.fill(data, (byte) 'X');
        assertThatThrownBy(() -> MagicHeaderParser.parse(data))
                .isInstanceOf(OsfException.MalformedFile.class);
    }

    @Test
    void rejectsEmptyInput() {
        byte[] data = new byte[0];
        assertThatThrownBy(() -> MagicHeaderParser.parse(data))
                .isInstanceOf(OsfException.MalformedFile.class);
    }
}
