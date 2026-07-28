// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH
package com.optimeas.osf.testutil;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;

import java.io.IOException;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Collections;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/**
 * Parses {@code examples/reference_manifest.json} into an in-memory map of
 * expected decoded file contents.
 *
 * <p>Uses Jackson's tree (JsonNode) API so the test package does not need to
 * be opened to the Jackson module — only read operations on the JSON tree are
 * required.
 *
 * <p>Used by {@code ConformanceManifestTest} to drive the parameterized
 * cross-implementation conformance assertions.  The manifest file sits at
 * {@code <examples-root>/reference_manifest.json} and is resolved via
 * {@link ExamplesDir}.
 */
public final class ReferenceManifest {

    /** Per-file entry from the manifest. */
    public static final class FileEntry {
        private final int version;
        private final List<ChannelEntry> channels;
        private final String integrity;
        private final Map<String, Long> anomalies;

        FileEntry(int version, List<ChannelEntry> channels, String integrity,
                  Map<String, Long> anomalies) {
            this.version = version;
            this.channels = List.copyOf(channels);
            this.integrity = integrity;
            this.anomalies = Map.copyOf(anomalies);
        }

        /** OSF format version: 4 or 5. */
        public int version() { return version; }

        /** Channel entries in index order. */
        public List<ChannelEntry> channels() { return channels; }

        /**
         * Declared integrity-profile wire token ({@code "crc32c"} /
         * {@code "ed25519"}), or {@code null} when the file carries no integrity
         * profile. Optional field — absent for the plain-file entries.
         */
        public String integrity() { return integrity; }

        /**
         * Deliberate non-conformances this corpus file carries, from the
         * optional {@code anomalies} manifest object. Empty for well-formed
         * files, which must therefore report zero for every kind.
         */
        public Map<String, Long> anomalies() { return anomalies; }
    }

    /** Per-channel entry within a file entry. */
    public static final class ChannelEntry {
        private final int index;
        private final String name;
        private final String dataType;
        private final long sampleCount;
        private final String mode;

        ChannelEntry(int index, String name, String dataType,
                     long sampleCount, String mode) {
            this.index = index;
            this.name = name;
            this.dataType = dataType;
            this.sampleCount = sampleCount;
            this.mode = mode;
        }

        /** On-disk channel index (metablock {@code "index"} attribute). */
        public int index() { return index; }

        /** Fully-qualified channel name. */
        public String name() { return name; }

        /**
         * OSF wire-name for the data type (lower-case), e.g. {@code "double"},
         * {@code "int32"}, {@code "gpslocation"}.
         */
        public String dataType() { return dataType; }

        /** Expected sample count. */
        public long sampleCount() { return sampleCount; }

        /**
         * Storage mode: {@code "equidistant"}, {@code "timestamped"}, or
         * {@code "variable"}.
         */
        public String mode() { return mode; }
    }

    private ReferenceManifest() {}

    /**
     * Load and parse the manifest from the examples directory resolved by
     * {@link ExamplesDir#resolve()}.
     *
     * <p>If the examples directory is absent the calling test is skipped (via
     * the assumption inside {@code ExamplesDir.resolve()}). If the manifest
     * file itself is not found this method throws {@link IOException} so the
     * test fails rather than silently skipping — that would indicate a broken
     * corpus checkout (directory present but manifest missing).
     *
     * @return unmodifiable map from filename (e.g. {@code "osf5_equidistant.osf"})
     *         to its {@link FileEntry}
     * @throws IOException if the manifest file cannot be read or parsed
     */
    public static Map<String, FileEntry> load() throws IOException {
        Path examplesDir = ExamplesDir.resolve();
        Path manifestPath = examplesDir.resolve("reference_manifest.json");

        ObjectMapper mapper = new ObjectMapper();
        JsonNode root = mapper.readTree(manifestPath.toFile());

        Map<String, FileEntry> result = new LinkedHashMap<>();
        root.fields().forEachRemaining(e -> {
            String fileName = e.getKey();
            JsonNode fileNode = e.getValue();
            int version = fileNode.get("version").asInt();
            String integrity = fileNode.hasNonNull("integrity")
                    ? fileNode.get("integrity").asText() : null;
            List<ChannelEntry> channels = new ArrayList<>();
            for (JsonNode chNode : fileNode.get("channels")) {
                channels.add(new ChannelEntry(
                        chNode.get("index").asInt(),
                        chNode.get("name").asText(),
                        chNode.get("dataType").asText(),
                        chNode.get("sampleCount").asLong(),
                        chNode.get("mode").asText()));
            }
            // Optional "anomalies" object: deliberate non-conformances this
            // corpus file carries. Non-defaulting lookup by design - a
            // mis-spelled key must fail loudly rather than silently reading
            // as absent (0 == 0 across every entry would mask the bug).
            Map<String, Long> anomalies = new LinkedHashMap<>();
            JsonNode anomaliesNode = fileNode.get("anomalies");
            if (anomaliesNode != null) {
                anomaliesNode.fields().forEachRemaining(a ->
                        anomalies.put(a.getKey(), a.getValue().asLong()));
            }
            result.put(fileName, new FileEntry(version, channels, integrity, anomalies));
        });
        return Collections.unmodifiableMap(result);
    }
}
