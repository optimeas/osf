// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH
package com.optimeas.osf;

import com.optimeas.osf.testutil.ExamplesDir;
import org.junit.jupiter.api.Assumptions;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.MethodSource;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;
import java.util.stream.Stream;

import static org.assertj.core.api.Assertions.assertThat;

/**
 * Integration tests driving the full {@link DataManager} read pipeline against
 * the real reference corpus under {@code examples/}.
 *
 * <p>This is the first end-to-end validation of the read path (magic header →
 * metablock → block reader → channel assembly) on actual generated files and on
 * the larger captured field samples. The tests are guarded via
 * {@link ExamplesDir}: a checkout without the corpus skips them rather than
 * failing.
 */
class ManagerExamplesTest {

    /**
     * Every {@code *.osf} file in {@code <examples>/generated}. Resolved at
     * argument-provision time; if the corpus is absent the stream is empty and
     * the parameterized test reports no invocations (the directory check below
     * also guards explicitly).
     */
    static Stream<Path> generatedOsfFiles() throws IOException {
        Path generated = ExamplesDir.resolve().resolve("generated");
        if (!Files.isDirectory(generated)) {
            return Stream.empty();
        }
        try (Stream<Path> entries = Files.list(generated)) {
            return entries
                    .filter(p -> p.getFileName().toString().endsWith(".osf"))
                    .sorted()
                    .toList()
                    .stream();
        }
    }

    @ParameterizedTest(name = "{0}")
    @MethodSource("generatedOsfFiles")
    void loadsEveryGeneratedFile(Path file) {
        DataManager mgr = DataManager.loadFromFile(file);

        List<DataChannel> channels = mgr.channels();
        assertThat(channels)
                .as("channels in %s", file.getFileName())
                .isNotEmpty();

        boolean anyWithSamples = channels.stream().anyMatch(c -> c.sampleCount() > 0);
        assertThat(anyWithSamples)
                .as("at least one channel with samples in %s", file.getFileName())
                .isTrue();
    }

    @Test
    void loadsMotorbikeFieldSample() {
        assertFieldSampleLoads("motorbike.osf");
    }

    @Test
    void loadsSteamLocoFieldSample() {
        assertFieldSampleLoads("steam_loco.osf");
    }

    private static void assertFieldSampleLoads(String fileName) {
        Path file = ExamplesDir.resolve().resolve(fileName);
        Assumptions.assumeTrue(Files.isRegularFile(file),
                "field sample " + file + " not present — skipping");

        DataManager mgr = DataManager.loadFromFile(file);
        assertThat(mgr.channels())
                .as("channels in %s", fileName)
                .isNotEmpty();
        boolean anyWithSamples = mgr.channels().stream().anyMatch(c -> c.sampleCount() > 0);
        assertThat(anyWithSamples)
                .as("at least one channel with samples in %s", fileName)
                .isTrue();
    }
}
