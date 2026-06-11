// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH
package com.optimeas.osf.cli;

import java.nio.file.Path;
import picocli.CommandLine.Command;
import picocli.CommandLine.Parameters;

@Command(name = "channels", mixinStandardHelpOptions = true,
         description = "List all channels in an OSF file.")
final class ChannelsCommand implements Runnable {
    @Parameters(index = "0", description = "OSF file to inspect.") Path file;

    @Override
    public void run() {
        // TODO B4
    }
}
