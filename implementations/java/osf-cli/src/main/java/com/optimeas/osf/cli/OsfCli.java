// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH
package com.optimeas.osf.cli;

import picocli.CommandLine;
import picocli.CommandLine.Command;

@Command(name = "osf", mixinStandardHelpOptions = true, version = "osf-cli 0.1.0",
         description = "Inspect, dump and convert OSF files.",
         subcommands = {InfoCommand.class, ChannelsCommand.class,
                        DumpCommand.class, ConvertCommand.class})
public final class OsfCli implements Runnable {
    @Override public void run() { new CommandLine(this).usage(System.out); }
    public static void main(String[] args) {
        System.exit(new CommandLine(new OsfCli()).execute(args));
    }
}
