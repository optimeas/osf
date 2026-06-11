// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH
package com.optimeas.osf.cli;

import static org.assertj.core.api.Assertions.assertThat;

import org.junit.jupiter.api.Test;
import picocli.CommandLine;

class ChannelsCommandTest {

    @Test
    void channelsListsSixChannelsWithHeader() {
        var sw = new java.io.StringWriter();
        var cmd = new CommandLine(new OsfCli());
        cmd.setOut(new java.io.PrintWriter(sw, true));

        int code = cmd.execute("channels",
                CliExamples.generated("osf5_scalar_numeric.osf").toString());

        assertThat(code).isZero();
        String out = sw.toString();

        // Header must be present
        assertThat(out).contains("datatype");
        assertThat(out).contains("index");
        assertThat(out).contains("samples");

        // All six channel names from reference_manifest.json
        assertThat(out).contains("Sensor/Double");
        assertThat(out).contains("Sensor/Float");
        assertThat(out).contains("Sensor/Int32");
        assertThat(out).contains("Sensor/Int16");
        assertThat(out).contains("Sensor/Int8");
        assertThat(out).contains("Sensor/Bool");

        // Exactly 6 data rows (each line contains "Sensor/")
        long dataRows = out.lines()
                .filter(line -> line.contains("Sensor/"))
                .count();
        assertThat(dataRows).isEqualTo(6);

        // Data types present
        assertThat(out).contains("double");
        assertThat(out).contains("float");
        assertThat(out).contains("int32");
        assertThat(out).contains("bool");
    }

    @Test
    void channelsSortByNameReordersOutput() {
        var sw = new java.io.StringWriter();
        var cmd = new CommandLine(new OsfCli());
        cmd.setOut(new java.io.PrintWriter(sw, true));

        int code = cmd.execute("channels", "--sort", "NAME",
                CliExamples.generated("osf5_scalar_numeric.osf").toString());

        assertThat(code).isZero();
        String out = sw.toString();

        // With NAME sort: Bool < Double < Float < Int16 < Int32 < Int8 (alphabetical)
        // Verify Bool appears before Double in the output
        int boolPos   = out.indexOf("Sensor/Bool");
        int doublePos = out.indexOf("Sensor/Double");
        assertThat(boolPos).isLessThan(doublePos);
    }
}
