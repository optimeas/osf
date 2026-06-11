// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH
package com.optimeas.osf.cli;

import static org.assertj.core.api.Assertions.assertThat;

import org.junit.jupiter.api.Test;
import picocli.CommandLine;

class OsfCliTest {
    @Test
    void helpListsSubcommands() {
        var sw = new java.io.StringWriter();
        var cmd = new CommandLine(new OsfCli());
        cmd.setOut(new java.io.PrintWriter(sw));
        int code = cmd.execute("--help");
        assertThat(code).isZero();
        assertThat(sw.toString()).contains("info").contains("channels").contains("dump").contains("convert");
    }
}
