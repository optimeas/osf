// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH
package com.optimeas.osf.cli;

import com.optimeas.osf.DataChannel;
import com.optimeas.osf.DataManager;
import com.optimeas.osf.OsfException;

import picocli.CommandLine;
import picocli.CommandLine.Command;
import picocli.CommandLine.Model.CommandSpec;
import picocli.CommandLine.Parameters;
import picocli.CommandLine.Spec;

import java.io.PrintWriter;
import java.nio.file.Path;
import java.util.List;
import java.util.Map;

/**
 * {@code info} subcommand — prints format version, file metadata,
 * compression status, and a per-channel summary for an OSF file.
 */
@Command(name = "info", mixinStandardHelpOptions = true,
         description = "Print metadata and channel summary for an OSF file.")
final class InfoCommand implements Runnable {

    @Spec CommandSpec spec;

    @Parameters(index = "0", description = "OSF file to inspect.") Path file;

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

        // Format / version line — mgr.version() is OsfVersion.OSF4 or OSF5
        out.println("format: " + mgr.version().name());

        // File-level metadata (creator, created_utc, location, etc.)
        Map<String, String> meta = mgr.metadata();
        if (!meta.isEmpty()) {
            for (Map.Entry<String, String> entry : meta.entrySet()) {
                out.println(entry.getKey() + ": " + entry.getValue());
            }
        }

        // Compression
        boolean compressed = mgr.stats().compressed();
        String comprFormat = mgr.stats().compressionFormat();
        out.println("compressed: " + compressed
                + (compressed ? " (" + comprFormat + ")" : ""));

        // Channel count and per-channel summary
        List<DataChannel> channels = mgr.channels();
        out.println("channels: " + channels.size());

        for (DataChannel ch : channels) {
            String unit = ch.physicalUnit() != null ? ch.physicalUnit() : "";
            out.printf("  [%d] %s  type=%s  mode=%s  samples=%d  unit=%s%n",
                    ch.index(),
                    ch.name(),
                    ch.dataType().name().toLowerCase(),
                    ch.kind().name().toLowerCase(),
                    ch.sampleCount(),
                    unit);
        }
    }
}
