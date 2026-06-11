// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH
package com.optimeas.osf.cli;

import java.nio.file.Path;
import picocli.CommandLine.Command;
import picocli.CommandLine.Parameters;

@Command(name = "info", mixinStandardHelpOptions = true,
         description = "Print metadata and channel summary for an OSF file.")
final class InfoCommand implements Runnable {
    @Parameters(index = "0", description = "OSF file to inspect.") Path file;

    @Override
    public void run() {
        // TODO B3
    }
}
