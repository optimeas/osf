// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH
package com.optimeas.osf;

import com.optimeas.osf.testutil.ExamplesDir;
import com.optimeas.osf.testutil.ReferenceManifest;
import com.optimeas.osf.testutil.ReferenceManifest.ChannelEntry;
import com.optimeas.osf.testutil.ReferenceManifest.FileEntry;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.MethodSource;

import java.io.IOException;
import java.nio.file.Path;
import java.util.List;
import java.util.Map;
import java.util.stream.Stream;

import static org.assertj.core.api.Assertions.assertThat;

/**
 * Cross-implementation conformance test: asserts that {@link DataManager}
 * decodes every generated reference file to match the expected contents
 * declared in {@code examples/reference_manifest.json}.
 *
 * <p>Each parameterized invocation loads one file from {@code examples/generated/}
 * and verifies:
 * <ul>
 *   <li>The declared manifest version matches the OSF magic header (inferred
 *       from the canonical filename prefix {@code osf4_} / {@code osf5_}).</li>
 *   <li>Channel count matches the manifest.</li>
 *   <li>Per channel (matched by index): name, data type (wire name), sample
 *       count, and storage mode ({@code equidistant} / {@code timestamped} /
 *       {@code variable}).</li>
 * </ul>
 *
 * <p>The test skips cleanly when the corpus or manifest is absent (via
 * {@link ExamplesDir}).
 */
class ConformanceManifestTest {

    /**
     * Provide one argument per manifest entry: the resolved path to
     * {@code examples/generated/<file>}.
     *
     * <p>If the examples directory is absent {@code ExamplesDir.resolve()}
     * causes the assumption to fail and the whole test is skipped. If the
     * directory exists but the generated sub-directory is empty the stream
     * is empty and the parameterized test reports zero invocations.
     */
    static Stream<Path> manifestEntries() throws IOException {
        // ExamplesDir.resolve() skips if corpus absent.
        Path generated = ExamplesDir.resolve().resolve("generated");
        Map<String, FileEntry> manifest = ReferenceManifest.load();
        return manifest.keySet().stream()
                .sorted()
                .map(generated::resolve);
    }

    @ParameterizedTest(name = "{0}")
    @MethodSource("manifestEntries")
    void conformsToManifest(Path file) throws IOException {
        String fileName = file.getFileName().toString();

        // Load manifest (cheap: already parsed, but load is stateless / re-reads).
        Map<String, FileEntry> manifest = ReferenceManifest.load();
        FileEntry expected = manifest.get(fileName);
        assertThat(expected)
                .as("manifest entry for %s", fileName)
                .isNotNull();

        // ── Version check ────────────────────────────────────────────────────
        // DataManager does not expose OsfVersion publicly; we infer the
        // expected format version from the canonical filename prefix and assert
        // it matches the manifest declaration.  Correct decoding (channels /
        // types / counts below) would fail independently if the wrong parser
        // were chosen.
        int versionFromName = fileName.startsWith("osf4_") ? 4 : 5;
        assertThat(expected.version())
                .as("manifest version for %s matches filename prefix", fileName)
                .isEqualTo(versionFromName);

        // ── Load ────────────────────────────────────────────────────────────
        DataManager mgr = DataManager.loadFromFile(file);
        List<DataChannel> channels = mgr.channels();

        // ── Channel count ────────────────────────────────────────────────────
        assertThat(channels)
                .as("channel count in %s", fileName)
                .hasSize(expected.channels().size());

        // ── Per-channel assertions ───────────────────────────────────────────
        for (ChannelEntry ce : expected.channels()) {
            DataChannel ch = channels.stream()
                    .filter(c -> c.index() == ce.index())
                    .findFirst()
                    .orElse(null);

            assertThat(ch)
                    .as("channel at index %d in %s", ce.index(), fileName)
                    .isNotNull();

            assertThat(ch.name())
                    .as("name of channel %d in %s", ce.index(), fileName)
                    .isEqualTo(ce.name());

            assertThat(ch.dataType().wireName())
                    .as("dataType wireName of channel %d (%s) in %s",
                            ce.index(), ce.name(), fileName)
                    .isEqualTo(ce.dataType());

            assertThat(ch.sampleCount())
                    .as("sampleCount of channel %d (%s) in %s",
                            ce.index(), ce.name(), fileName)
                    .isEqualTo(ce.sampleCount());

            String expectedMode = modeFromKind(ch.kind());
            assertThat(expectedMode)
                    .as("mode of channel %d (%s) in %s",
                            ce.index(), ce.name(), fileName)
                    .isEqualTo(ce.mode());
        }
    }

    /**
     * Map {@link DataChannel.Kind} to the manifest's {@code mode} string.
     *
     * @param kind the storage kind
     * @return {@code "equidistant"}, {@code "timestamped"}, or {@code "variable"}
     * @throws IllegalArgumentException for any unrecognised future kind
     */
    private static String modeFromKind(DataChannel.Kind kind) {
        return switch (kind) {
            case EQUIDISTANT -> "equidistant";
            case TIMESTAMPED -> "timestamped";
            case VARIABLE    -> "variable";
        };
    }
}
