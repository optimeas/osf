// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH
package com.optimeas.osf.cli;

import com.optimeas.osf.DataChannel;
import com.optimeas.osf.DataManager;
import com.optimeas.osf.DataType;
import com.optimeas.osf.OsfException;

import picocli.CommandLine;
import picocli.CommandLine.Command;
import picocli.CommandLine.ITypeConverter;
import picocli.CommandLine.Model.CommandSpec;
import picocli.CommandLine.Option;
import picocli.CommandLine.Parameters;
import picocli.CommandLine.Spec;

import java.io.IOException;
import java.io.PrintWriter;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;

/**
 * {@code dump} subcommand — writes channel data from an OSF file to CSV.
 *
 * <p>By default all chartable (numeric + bool) channels are included, with
 * per-channel CSV output ({@code timestamp,value}).  Use {@code --format
 * unified-csv} for the merged wide-table form and {@code --channel} to
 * restrict which channels are dumped.
 */
@Command(name = "dump", mixinStandardHelpOptions = true,
         description = "Dump channel data from an OSF file to CSV.")
final class DumpCommand implements Runnable {

    // -------------------------------------------------------------------------
    // Format enum + converter

    /** Output format: per-channel narrow CSV or merged wide (unified) CSV. */
    enum Format {
        CSV, UNIFIED_CSV;
    }

    /**
     * Converts the CLI string to {@link Format}, accepting {@code csv} and
     * {@code unified-csv} (case-insensitive).
     */
    static final class FormatConverter implements ITypeConverter<Format> {
        @Override
        public Format convert(String value) {
            return switch (value.toLowerCase()) {
                case "csv"         -> Format.CSV;
                case "unified-csv", "unified_csv" -> Format.UNIFIED_CSV;
                default -> throw new CommandLine.TypeConversionException(
                        "Unknown format '" + value + "'. Use 'csv' or 'unified-csv'.");
            };
        }
    }

    // -------------------------------------------------------------------------
    // Fields

    @Spec CommandSpec spec;

    @Parameters(index = "0", description = "OSF file to dump.")
    Path file;

    @Option(names = "--channel",
            description = "Channel name or integer index to include. "
                        + "Repeatable. Default: all chartable (numeric/bool) channels.")
    List<String> channelSelectors;

    @Option(names = "--format",
            converter = FormatConverter.class,
            defaultValue = "csv",
            description = "Output format: csv (default) or unified-csv.")
    Format format;

    @Option(names = "--timestamp-format",
            defaultValue = "DATETIME",
            description = "Timestamp rendering: DATETIME (default), SECONDS, ISO8601, NANOSECONDS.")
    TimestampFormat timestampFormat;

    @Option(names = "--out",
            description = "Write output to this file instead of stdout.")
    Path out;

    // -------------------------------------------------------------------------
    // Execution

    @Override
    public void run() {
        PrintWriter err = spec.commandLine().getErr();

        DataManager mgr;
        try {
            mgr = DataManager.loadFromFile(file);
        } catch (OsfException e) {
            err.println("error: " + e.getMessage());
            throw new CommandLine.ExecutionException(spec.commandLine(),
                    "Failed to load OSF file: " + file, e);
        }

        List<DataChannel> allChannels = mgr.channels();

        // Resolve which channels to dump
        List<DataChannel> selected;
        if (channelSelectors == null || channelSelectors.isEmpty()) {
            // Default: all chartable channels (numeric + bool; skip string/binary/gps)
            selected = allChannels.stream()
                    .filter(DumpCommand::isChartable)
                    .toList();
        } else {
            selected = resolveSelectors(channelSelectors, allChannels, err);
        }

        if (selected.isEmpty()) {
            err.println("warning: no chartable channels selected; nothing to dump.");
            return;
        }

        // Build CsvWriter.Column per selected channel
        List<CsvWriter.Column> columns = new ArrayList<>(selected.size());
        for (DataChannel ch : selected) {
            if (!isChartable(ch)) {
                // Explicitly requested but not numeric/bool — skip with note
                err.printf("note: channel '%s' (type=%s) is not numeric; skipping.%n",
                        ch.name(), ch.dataType().name().toLowerCase());
                continue;
            }
            double[] values;
            try {
                values = ch.asDoubles();
            } catch (OsfException e) {
                err.printf("warning: could not read channel '%s' as doubles: %s; skipping.%n",
                        ch.name(), e.getMessage());
                continue;
            }
            columns.add(new CsvWriter.Column(ch.name(), ch.timestampsNs(), values));
        }

        if (columns.isEmpty()) {
            err.println("warning: no columns produced; nothing to write.");
            return;
        }

        // Render the CSV
        String csv;
        if (format == Format.UNIFIED_CSV) {
            csv = CsvWriter.unified(columns, timestampFormat);
        } else {
            // Per-channel CSV: concatenate each channel's block separated by a blank line
            // and a comment header so the reader can identify each section.
            StringBuilder sb = new StringBuilder();
            for (CsvWriter.Column col : columns) {
                if (sb.length() > 0) sb.append('\n');
                sb.append("# channel: ").append(col.name()).append('\n');
                sb.append(CsvWriter.perChannel(col, timestampFormat));
            }
            csv = sb.toString();
        }

        // Write output
        if (out != null) {
            try {
                Files.writeString(out, csv);
            } catch (IOException e) {
                err.println("error: cannot write output file: " + e.getMessage());
                throw new CommandLine.ExecutionException(spec.commandLine(),
                        "Cannot write output: " + out, e);
            }
        } else {
            PrintWriter stdout = spec.commandLine().getOut();
            stdout.print(csv);
            stdout.flush();
        }
    }

    // -------------------------------------------------------------------------
    // Helpers

    /**
     * Returns {@code true} for data types that can be widened to {@code double}
     * via {@link DataChannel#asDoubles()}: all numeric types and bool.
     * String, binary, and GPS are excluded.
     */
    private static boolean isChartable(DataChannel ch) {
        return switch (ch.dataType()) {
            case BOOL,
                 INT8, INT16, INT32, INT64,
                 UINT8, UINT16, UINT32, UINT64,
                 FLOAT, DOUBLE -> true;
            default -> false;
        };
    }

    /**
     * Resolve a list of channel selectors (names or integer indices) against the
     * full channel list.  Unrecognised selectors are logged to {@code err}.
     */
    private static List<DataChannel> resolveSelectors(
            List<String> selectors, List<DataChannel> allChannels, PrintWriter err) {
        List<DataChannel> result = new ArrayList<>();
        for (String sel : selectors) {
            DataChannel found = null;
            // Try integer index first
            try {
                int idx = Integer.parseInt(sel.trim());
                for (DataChannel ch : allChannels) {
                    if (ch.index() == idx) { found = ch; break; }
                }
                if (found == null) {
                    err.printf("warning: no channel with index %d; skipping.%n", idx);
                    continue;
                }
            } catch (NumberFormatException ignored) {
                // Not an integer — treat as channel name
                for (DataChannel ch : allChannels) {
                    if (ch.name().equals(sel)) { found = ch; break; }
                }
                if (found == null) {
                    err.printf("warning: channel '%s' not found; skipping.%n", sel);
                    continue;
                }
            }
            result.add(found);
        }
        return result;
    }
}
