// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH
package com.optimeas.osf.cli;

import java.nio.file.Path;
import picocli.CommandLine.Command;
import picocli.CommandLine.Parameters;

@Command(name = "dump", mixinStandardHelpOptions = true,
         description = "Dump channel data from an OSF file to CSV.")
final class DumpCommand implements Runnable {
    @Parameters(index = "0", description = "OSF file to dump.") Path file;

    @Override
    public void run() {
        // TODO B5
    }
}
