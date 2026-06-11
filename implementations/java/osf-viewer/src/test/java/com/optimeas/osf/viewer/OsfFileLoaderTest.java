// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH
package com.optimeas.osf.viewer;

import org.junit.jupiter.api.Test;

import static org.assertj.core.api.Assertions.assertThat;

/**
 * Non-GUI smoke test for {@link OsfFileLoader#loadInto(java.nio.file.Path)}.
 *
 * <p>Uses the {@link ViewerExamples} corpus resolver — the test is
 * automatically skipped (JUnit assumption) when the {@code examples/generated}
 * directory cannot be found, so it is safe to run in CI environments that do
 * not include the corpus.
 *
 * <p>No JavaFX runtime is required: {@link OsfFileLoader#loadInto} is a plain
 * synchronous method that reads the file and populates a {@link ViewerModel}
 * without touching any FX scene-graph classes.
 */
class OsfFileLoaderTest {

    /**
     * Loading {@code osf5_mixed.osf} (which contains 4 channels) must
     * produce a model with exactly 4 entries in {@link ViewerModel#channels()}.
     */
    @Test
    void loadsOsf5MixedWithFourChannels() {
        // Skip if the corpus is absent (e.g. shallow CI clone without examples/).
        var path = ViewerExamples.generated("osf5_mixed.osf");

        ViewerModel m = OsfFileLoader.loadInto(path);

        assertThat(m.channels())
                .as("osf5_mixed.osf must expose 4 channels")
                .hasSize(4);
    }
}
