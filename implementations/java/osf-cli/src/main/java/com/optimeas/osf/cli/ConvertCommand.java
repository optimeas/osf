// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH
package com.optimeas.osf.cli;

import com.optimeas.osf.DataChannel;
import com.optimeas.osf.DataManager;
import com.optimeas.osf.DataType;
import com.optimeas.osf.GpsLocation;
import com.optimeas.osf.BlockWriter;
import com.optimeas.osf.StreamingWriter;
import com.optimeas.osf.OsfException;

import picocli.CommandLine;
import picocli.CommandLine.Command;
import picocli.CommandLine.Model.CommandSpec;
import picocli.CommandLine.Option;
import picocli.CommandLine.Parameters;
import picocli.CommandLine.Spec;

import java.io.IOException;
import java.io.OutputStream;
import java.io.PrintWriter;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;
import java.util.zip.GZIPOutputStream;

/**
 * {@code convert} subcommand — reads any OSF file (OSF4 or OSF5, optionally
 * compressed) and writes it as OSF5, with an optional gzip wrapper.
 *
 * <p>Two writer strategies are available via {@code --writer}:
 * <ul>
 *   <li><b>BLOCK</b> (default) — accumulates every sample in memory then emits
 *       in one pass; supports both plain-file and gzip-compressed output via
 *       {@link BlockWriter#writeTo(OutputStream)}.</li>
 *   <li><b>STREAMING</b> — replays channels sample-by-sample through a
 *       {@link StreamingWriter} backed by a {@link java.nio.channels.FileChannel};
 *       supports uncompressed output only. When {@code --compress} is also
 *       requested the command falls back to BLOCK and prints a note.</li>
 * </ul>
 */
@Command(name = "convert", mixinStandardHelpOptions = true,
         description = "Convert an OSF file to OSF5, optionally compressing the output.")
final class ConvertCommand implements Runnable {

    /** Writer back-end strategy. */
    enum WriterMode { BLOCK, STREAMING }

    @Spec CommandSpec spec;

    @Parameters(index = "0", description = "Input OSF file (any version/compression).")
    Path in;

    @Parameters(index = "1", description = "Output OSF file.")
    Path out;

    @Option(names = "--compress",
            description = "Wrap the output in gzip (produces an OSFZ file). "
                        + "Forces BLOCK writer when combined with --writer streaming.")
    boolean compress;

    @Option(names = "--writer",
            defaultValue = "BLOCK",
            description = "Writer back-end: BLOCK (default) or STREAMING. "
                        + "STREAMING does not support --compress; BLOCK is used instead.")
    WriterMode writer;

    @Override
    public void run() {
        PrintWriter out2 = spec.commandLine().getOut();
        PrintWriter err  = spec.commandLine().getErr();

        // Load source
        DataManager mgr;
        try {
            mgr = DataManager.loadFromFile(in);
        } catch (OsfException e) {
            err.println("error: " + e.getMessage());
            throw new CommandLine.ExecutionException(spec.commandLine(),
                    "Failed to load OSF file: " + in, e);
        }

        // Resolve effective writer mode
        WriterMode effectiveWriter = writer;
        if (compress && writer == WriterMode.STREAMING) {
            err.println("note: --compress is not supported with STREAMING writer; using BLOCK.");
            effectiveWriter = WriterMode.BLOCK;
        }

        try {
            if (effectiveWriter == WriterMode.BLOCK) {
                writeBlock(mgr);
            } else {
                writeStreaming(mgr, err);
            }
        } catch (OsfException e) {
            err.println("error: " + e.getMessage());
            throw new CommandLine.ExecutionException(spec.commandLine(),
                    "Failed to write OSF file: " + out, e);
        }

        int channelCount = mgr.channels().size();
        out2.printf("wrote %s (%d channel%s)%n", out, channelCount, channelCount == 1 ? "" : "s");
    }

    // -------------------------------------------------------------------------
    // BLOCK writer

    private void writeBlock(DataManager mgr) {
        BlockWriter bw = BlockWriter.fromManager(mgr);
        if (compress) {
            try (OutputStream os = new GZIPOutputStream(Files.newOutputStream(out))) {
                bw.writeTo(os);
            } catch (IOException e) {
                throw new OsfException("failed to write compressed OSF file " + out
                        + ": " + e.getMessage(), e);
            }
        } else {
            bw.writeToFile(out);
        }
    }

    // -------------------------------------------------------------------------
    // STREAMING writer — replay every channel sample-by-sample

    private void writeStreaming(DataManager mgr, PrintWriter err) {
        List<DataChannel> channels = mgr.channels();

        try (StreamingWriter sw = StreamingWriter.create(out)) {
            // Copy file-level metadata
            for (var entry : mgr.metadata().entrySet()) {
                sw.setMetadata(entry.getKey(), entry.getValue());
            }

            // Declare all channels, collecting their assigned writer indices
            int[] writerIdx = new int[channels.size()];
            for (int i = 0; i < channels.size(); i++) {
                DataChannel dc = channels.get(i);
                String unit = dc.physicalUnit();
                writerIdx[i] = switch (dc.kind()) {
                    case EQUIDISTANT -> {
                        double rate = dc.segments().isEmpty() ? 1.0
                                : dc.segments().get(0).sampleRateHz();
                        yield sw.addEquidistantChannel(dc.name(), dc.dataType(), 2, rate,
                                unit, null);
                    }
                    case TIMESTAMPED, VARIABLE ->
                            sw.addTimestampedChannel(dc.name(), dc.dataType(), 2, unit, null);
                };
            }

            // Replay samples
            for (int i = 0; i < channels.size(); i++) {
                DataChannel dc = channels.get(i);
                int idx = writerIdx[i];
                replayChannel(sw, dc, idx, err);
            }
        }
    }

    /** Replay all samples from {@code dc} into the streaming writer at {@code idx}. */
    private static void replayChannel(StreamingWriter sw, DataChannel dc, int idx,
                                      PrintWriter err) {
        switch (dc.kind()) {
            case EQUIDISTANT -> {
                for (DataChannel.Segment seg : dc.segments()) {
                    int start = seg.startIndex();
                    int count = seg.sampleCount();
                    if (count == 0) continue;
                    if (dc.dataType() == DataType.FLOAT) {
                        double[] allD = dc.asDoubles();
                        float[] slice = new float[count];
                        for (int j = 0; j < count; j++) slice[j] = (float) allD[start + j];
                        sw.startEquidistantSegment(idx, seg.startTimestampNs(), slice);
                    } else {
                        double[] allD = dc.asDoubles();
                        double[] slice = java.util.Arrays.copyOfRange(allD, start, start + count);
                        sw.startEquidistantSegment(idx, seg.startTimestampNs(), slice);
                    }
                }
            }
            case TIMESTAMPED -> {
                long[] ts = dc.timestampsNs();
                DataType dt = dc.dataType();
                if (dt == DataType.GPS_LOCATION) {
                    GpsLocation[] g = dc.asGps();
                    sw.writeSamples(idx, ts, g);
                } else if (dt == DataType.BOOL) {
                    boolean[] v = dc.asBooleans();
                    for (int j = 0; j < ts.length; j++) sw.writeSample(idx, ts[j], v[j]);
                } else if (dt == DataType.FLOAT) {
                    double[] v = dc.asDoubles();
                    for (int j = 0; j < ts.length; j++) sw.writeSample(idx, ts[j], (float) v[j]);
                } else if (dt == DataType.DOUBLE) {
                    sw.writeSamples(idx, ts, dc.asDoubles());
                } else {
                    // All integer types
                    sw.writeSamples(idx, ts, dc.asLongs());
                }
            }
            case VARIABLE -> {
                long[] ts = dc.timestampsNs();
                if (dc.dataType() == DataType.STRING) {
                    String[] v = dc.asStrings();
                    for (int j = 0; j < ts.length; j++) sw.writeSample(idx, ts[j], v[j]);
                } else {
                    byte[][] v = dc.asBinaries();
                    for (int j = 0; j < ts.length; j++) sw.writeSample(idx, ts[j], v[j]);
                }
            }
            default -> err.printf("warning: channel '%s' has unknown kind %s; skipping.%n",
                    dc.name(), dc.kind());
        }
    }
}
