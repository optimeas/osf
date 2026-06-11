// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH
package com.optimeas.osf.cli;

import static org.assertj.core.api.Assertions.assertThat;

import org.junit.jupiter.api.Test;
import picocli.CommandLine;

class DumpCommandTest {

    // -----------------------------------------------------------------------
    // unified-csv: single channel, explicit --format unified-csv

    @Test
    void unifiedCsvSingleChannelHasHeaderAndHundredRows() {
        var sw = new java.io.StringWriter();
        var cmd = new CommandLine(new OsfCli());
        cmd.setOut(new java.io.PrintWriter(sw, true));

        int code = cmd.execute(
                "dump",
                "--format", "unified-csv",
                "--channel", "Sensor/Vibration100Hz",
                CliExamples.generated("osf5_equidistant.osf").toString());

        assertThat(code).isZero();
        String out = sw.toString();

        // Header must be "timestamp,Sensor/Vibration100Hz"
        assertThat(out).startsWith("timestamp,Sensor/Vibration100Hz");

        // Exactly 100 non-empty, non-header lines (data rows)
        String[] lines = out.split("\n", -1);
        long dataRows = java.util.Arrays.stream(lines)
                .skip(1)                        // skip the header line
                .filter(l -> !l.isBlank())
                .count();
        assertThat(dataRows).isEqualTo(100);
    }

    // -----------------------------------------------------------------------
    // default csv (per-channel): single channel, default format

    @Test
    void defaultCsvSingleChannelHasTimestampValueHeaderAndHundredRows() {
        var sw = new java.io.StringWriter();
        var cmd = new CommandLine(new OsfCli());
        cmd.setOut(new java.io.PrintWriter(sw, true));

        int code = cmd.execute(
                "dump",
                "--channel", "Sensor/Vibration100Hz",
                CliExamples.generated("osf5_equidistant.osf").toString());

        assertThat(code).isZero();
        String out = sw.toString();

        // Per-channel format emits a "timestamp,value" header
        assertThat(out).contains("timestamp,value");

        // Exactly 100 data rows (skip the comment line, the header line, blank lines)
        String[] lines = out.split("\n", -1);
        long dataRows = java.util.Arrays.stream(lines)
                .filter(l -> !l.isBlank())
                .filter(l -> !l.startsWith("#"))   // strip "# channel: ..." comment lines
                .filter(l -> !l.startsWith("timestamp")) // strip header
                .count();
        assertThat(dataRows).isEqualTo(100);
    }

    // -----------------------------------------------------------------------
    // unified-csv: all three equidistant channels (default channel selection)

    @Test
    void unifiedCsvAllChannelsDefaultSelectionThreeColumnHeader() {
        var sw = new java.io.StringWriter();
        var cmd = new CommandLine(new OsfCli());
        cmd.setOut(new java.io.PrintWriter(sw, true));

        int code = cmd.execute(
                "dump",
                "--format", "unified-csv",
                CliExamples.generated("osf5_equidistant.osf").toString());

        assertThat(code).isZero();
        String out = sw.toString();

        // All three channels appear in the header
        assertThat(out).startsWith("timestamp,");
        assertThat(out).contains("Sensor/Vibration100Hz")
                       .contains("Sensor/Vibration1kHz")
                       .contains("Sensor/Vibration10kHz");
    }

    // -----------------------------------------------------------------------
    // channel selection by integer index

    @Test
    void channelSelectedByIndexProducesOutput() {
        var sw = new java.io.StringWriter();
        var cmd = new CommandLine(new OsfCli());
        cmd.setOut(new java.io.PrintWriter(sw, true));

        // index 0 = Sensor/Vibration100Hz in osf5_equidistant.osf
        int code = cmd.execute(
                "dump",
                "--format", "unified-csv",
                "--channel", "0",
                CliExamples.generated("osf5_equidistant.osf").toString());

        assertThat(code).isZero();
        String out = sw.toString();
        assertThat(out).startsWith("timestamp,Sensor/Vibration100Hz");
    }

    // -----------------------------------------------------------------------
    // timestamp-format option

    @Test
    void nanosecondTimestampFormatProducesIntegerTimestamps() {
        var sw = new java.io.StringWriter();
        var cmd = new CommandLine(new OsfCli());
        cmd.setOut(new java.io.PrintWriter(sw, true));

        int code = cmd.execute(
                "dump",
                "--format", "unified-csv",
                "--timestamp-format", "NANOSECONDS",
                "--channel", "Sensor/Vibration100Hz",
                CliExamples.generated("osf5_equidistant.osf").toString());

        assertThat(code).isZero();
        String out = sw.toString();

        // The first data row's timestamp cell should be a plain integer (no dots)
        String[] lines = out.split("\n", -1);
        assertThat(lines.length).isGreaterThan(1);
        String firstDataRow = lines[1];
        String timestampCell = firstDataRow.split(",")[0];
        // Must be parseable as long and contain no decimal point
        assertThat(timestampCell).doesNotContain(".");
        assertThat(Long.parseLong(timestampCell)).isGreaterThanOrEqualTo(0);
    }
}
