// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH
package com.optimeas.osf.cli;

import com.optimeas.osf.DataChannel;
import com.optimeas.osf.DataManager;
import com.optimeas.osf.OsfException;

import picocli.CommandLine;
import picocli.CommandLine.Command;
import picocli.CommandLine.Model.CommandSpec;
import picocli.CommandLine.Option;
import picocli.CommandLine.Parameters;
import picocli.CommandLine.Spec;

import java.io.PrintWriter;
import java.nio.file.Path;
import java.util.Comparator;
import java.util.List;

/**
 * {@code channels} subcommand — prints a tabular listing of all channels
 * in an OSF file (index, name, data type, mode, sample count, unit).
 */
@Command(name = "channels", mixinStandardHelpOptions = true,
         description = "List all channels in an OSF file.")
final class ChannelsCommand implements Runnable {

    /** Sort keys for the channel table. */
    enum SortKey { INDEX, NAME }

    @Spec CommandSpec spec;

    @Parameters(index = "0", description = "OSF file to inspect.") Path file;

    @Option(names = "--sort",
            description = "Sort channels by INDEX (default) or NAME.",
            defaultValue = "INDEX")
    SortKey sort;

    @Override
    public void run() {
        PrintWriter out = spec.commandLine().getOut();
        PrintWriter err = spec.commandLine().getErr();

        DataManager mgr;
        try {
            mgr = DataManager.loadFromFile(file);
        } catch (OsfException e) {
            err.println("error: " + e.getMessage());
            throw new CommandLine.ExecutionException(spec.commandLine(),
                    "Failed to load OSF file: " + file, e);
        }

        List<DataChannel> channels = mgr.channels();

        // Apply sort
        Comparator<DataChannel> cmp = switch (sort) {
            case NAME  -> Comparator.comparing(DataChannel::name);
            case INDEX -> Comparator.comparingInt(DataChannel::index);
        };
        List<DataChannel> sorted = channels.stream().sorted(cmp).toList();

        // Header
        out.printf("%-6s  %-40s  %-12s  %-12s  %-8s  %s%n",
                "index", "name", "datatype", "mode", "samples", "unit");
        out.println("-".repeat(90));

        // One row per channel
        for (DataChannel ch : sorted) {
            String unit = ch.physicalUnit() != null ? ch.physicalUnit() : "";
            String datatype = ch.dataType().name().toLowerCase();
            String mode = ch.kind().name().toLowerCase();
            out.printf("%-6d  %-40s  %-12s  %-12s  %-8d  %s%n",
                    ch.index(),
                    ch.name(),
                    datatype,
                    mode,
                    ch.sampleCount(),
                    unit);
        }
    }
}
