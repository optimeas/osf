// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH
package com.optimeas.osf.cli;

import java.nio.file.Path;
import picocli.CommandLine.Command;
import picocli.CommandLine.Parameters;

@Command(name = "convert", mixinStandardHelpOptions = true,
         description = "Convert an OSF file to a different format or version.")
final class ConvertCommand implements Runnable {
    @Parameters(index = "0", description = "Input OSF file.") Path file;

    @Override
    public void run() {
        // TODO B6
    }
}
