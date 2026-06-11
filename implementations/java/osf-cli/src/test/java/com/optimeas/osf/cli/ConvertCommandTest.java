// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH
package com.optimeas.osf.cli;

import static org.assertj.core.api.Assertions.assertThat;

import com.optimeas.osf.DataManager;

import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;
import picocli.CommandLine;

import java.nio.file.Path;

/**
 * Integration tests for the {@code convert} subcommand.
 *
 * <p>All tests use the reference corpus under {@code examples/generated/}
 * (resolved via {@link CliExamples}); they are skipped when that directory is
 * absent (e.g. in a minimal CI checkout without examples).
 */
class ConvertCommandTest {

    // -----------------------------------------------------------------------
    // BLOCK (default): OSF4 → OSF5 round-trip

    @Test
    void blockRoundTripOsf4MixedPreservesChannelCountAndSpotValues(@TempDir Path tmp) {
        Path src = CliExamples.generated("osf4_mixed.osf");
        Path dst = tmp.resolve("out.osf");

        var cmd = new CommandLine(new OsfCli());
        cmd.setOut(new java.io.PrintWriter(new java.io.StringWriter())); // suppress stdout
        int code = cmd.execute("convert", src.toString(), dst.toString());
        assertThat(code).isZero();

        // Verify output can be reloaded
        DataManager srcMgr = DataManager.loadFromFile(src);
        DataManager dstMgr = DataManager.loadFromFile(dst);

        // Channel count must be preserved
        assertThat(dstMgr.channels()).hasSize(srcMgr.channels().size());

        // Spot-check: find the first numeric timestamped channel and compare
        // sample counts and first/last values across the round-trip.
        srcMgr.channels().stream()
                .filter(ch -> isNumeric(ch.dataType()))
                .filter(ch -> ch.kind() == com.optimeas.osf.DataChannel.Kind.TIMESTAMPED
                           || ch.kind() == com.optimeas.osf.DataChannel.Kind.EQUIDISTANT)
                .findFirst()
                .ifPresent(srcCh -> {
                    var dstCh = dstMgr.channelByName(srcCh.name()).orElseThrow();
                    assertThat(dstCh.sampleCount()).isEqualTo(srcCh.sampleCount());
                    if (srcCh.sampleCount() > 0) {
                        double[] srcV = srcCh.asDoubles();
                        double[] dstV = dstCh.asDoubles();
                        assertThat(dstV[0]).isEqualTo(srcV[0]);
                        assertThat(dstV[srcV.length - 1]).isEqualTo(srcV[srcV.length - 1]);
                    }
                });
    }

    // -----------------------------------------------------------------------
    // BLOCK + --compress: OSF5 → OSFZ

    @Test
    void blockCompressProducesReadableOsfzWithMatchingChannelCount(@TempDir Path tmp) {
        Path src = CliExamples.generated("osf5_mixed.osf");
        Path dst = tmp.resolve("out.osfz");

        var sw = new java.io.StringWriter();
        var cmd = new CommandLine(new OsfCli());
        cmd.setOut(new java.io.PrintWriter(sw, true));
        int code = cmd.execute("convert", "--compress", src.toString(), dst.toString());
        assertThat(code).isZero();

        // Output must be loadable
        DataManager srcMgr = DataManager.loadFromFile(src);
        DataManager dstMgr = DataManager.loadFromFile(dst);

        // Compression flag must be set
        assertThat(dstMgr.stats().compressed()).isTrue();

        // Channel count must be preserved
        assertThat(dstMgr.channels()).hasSize(srcMgr.channels().size());
    }

    // -----------------------------------------------------------------------
    // Confirmation message

    @Test
    void conversionPrintsConfirmationLine(@TempDir Path tmp) {
        Path src = CliExamples.generated("osf5_scalar_numeric.osf");
        Path dst = tmp.resolve("out.osf");

        var sw = new java.io.StringWriter();
        var cmd = new CommandLine(new OsfCli());
        cmd.setOut(new java.io.PrintWriter(sw, true));
        int code = cmd.execute("convert", src.toString(), dst.toString());

        assertThat(code).isZero();
        // "wrote <path> (<N> channel(s))" line must appear
        assertThat(sw.toString()).contains("wrote").contains("channel");
    }

    // -----------------------------------------------------------------------
    // STREAMING (uncompressed): round-trip equivalence

    @Test
    void streamingRoundTripPreservesChannelCount(@TempDir Path tmp) {
        Path src = CliExamples.generated("osf5_scalar_numeric.osf");
        Path dst = tmp.resolve("out_streaming.osf");

        var cmd = new CommandLine(new OsfCli());
        cmd.setOut(new java.io.PrintWriter(new java.io.StringWriter()));
        int code = cmd.execute("convert", "--writer", "STREAMING", src.toString(), dst.toString());
        assertThat(code).isZero();

        DataManager srcMgr = DataManager.loadFromFile(src);
        DataManager dstMgr = DataManager.loadFromFile(dst);
        assertThat(dstMgr.channels()).hasSize(srcMgr.channels().size());

        // Spot-check first numeric channel's sample count
        srcMgr.channels().stream()
                .filter(ch -> isNumeric(ch.dataType()))
                .findFirst()
                .ifPresent(srcCh -> {
                    var dstCh = dstMgr.channelByName(srcCh.name()).orElseThrow();
                    assertThat(dstCh.sampleCount()).isEqualTo(srcCh.sampleCount());
                });
    }

    // -----------------------------------------------------------------------
    // STREAMING + --compress falls back to BLOCK (no error, compressed output)

    @Test
    void streamingPlusCompressFallsBackToBlockAndProducesCompressedOutput(@TempDir Path tmp) {
        Path src = CliExamples.generated("osf5_mixed.osf");
        Path dst = tmp.resolve("fallback.osfz");

        var errSw = new java.io.StringWriter();
        var cmd = new CommandLine(new OsfCli());
        cmd.setOut(new java.io.PrintWriter(new java.io.StringWriter()));
        cmd.setErr(new java.io.PrintWriter(errSw, true));
        int code = cmd.execute("convert", "--compress", "--writer", "STREAMING",
                src.toString(), dst.toString());

        assertThat(code).isZero();
        // The fallback note must appear
        assertThat(errSw.toString()).contains("BLOCK");

        // Output must still be a valid compressed OSF5 file
        DataManager dstMgr = DataManager.loadFromFile(dst);
        assertThat(dstMgr.stats().compressed()).isTrue();
        assertThat(dstMgr.channels()).hasSize(DataManager.loadFromFile(src).channels().size());
    }

    // -----------------------------------------------------------------------
    // Helpers

    private static boolean isNumeric(com.optimeas.osf.DataType dt) {
        return switch (dt) {
            case BOOL, INT8, INT16, INT32, INT64, UINT8, UINT16, UINT32, UINT64,
                 FLOAT, DOUBLE -> true;
            default -> false;
        };
    }
}
