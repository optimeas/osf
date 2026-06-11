// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH
package com.optimeas.osf.cli;

import static org.assertj.core.api.Assertions.assertThat;

import org.junit.jupiter.api.Test;
import picocli.CommandLine;

class InfoCommandTest {
    @Test void infoReportsChannelsAndVersion() {
        var sw = new java.io.StringWriter();
        var cmd = new CommandLine(new OsfCli());
        cmd.setOut(new java.io.PrintWriter(sw, true));
        int code = cmd.execute("info", CliExamples.generated("osf5_equidistant.osf").toString());
        assertThat(code).isZero();
        String out = sw.toString();
        assertThat(out).contains("OSF5").contains("channels: 3");
    }
}
