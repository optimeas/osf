// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH
package com.optimeas.osf.testutil;

import org.junit.jupiter.api.Assumptions;

import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;

/**
 * Locates the reference example corpus for the integration tests.
 *
 * <p>The directory is taken from the {@code osf.examples.dir} system property,
 * defaulting to {@code ../../examples} relative to the {@code osf-java} module
 * (i.e. the repository-root {@code examples/} directory). When the resolved path
 * is not a directory the calling test is <em>skipped</em> (JUnit assumption)
 * rather than failed, so the unit suite still passes on a checkout without the
 * corpus.
 */
public final class ExamplesDir {

    /** System property naming the examples directory. */
    public static final String PROPERTY = "osf.examples.dir";

    private ExamplesDir() {}

    /**
     * Resolve the examples directory, skipping the test when it is absent.
     *
     * <p>Resolution order:
     * <ol>
     *   <li>The {@code osf.examples.dir} system property, if set and a
     *       directory (relative paths are resolved against the JVM CWD, which is
     *       the {@code osf-java} module directory under Surefire).</li>
     *   <li>Otherwise, walk up from the JVM CWD looking for a directory named
     *       {@code examples} that contains a {@code generated} subdirectory.
     *       This makes the corpus reachable regardless of whether Maven runs
     *       from the reactor root or the module directory.</li>
     * </ol>
     *
     * @return the normalized absolute path to the examples directory
     */
    public static Path resolve() {
        String configured = System.getProperty(PROPERTY);
        if (configured != null) {
            Path dir = Paths.get(configured).toAbsolutePath().normalize();
            Assumptions.assumeTrue(Files.isDirectory(dir),
                    "examples directory not found at " + dir
                    + " (from -D" + PROPERTY + "=" + configured + ")");
            return dir;
        }

        Path found = searchUpward(Paths.get("").toAbsolutePath().normalize());
        Assumptions.assumeTrue(found != null,
                "examples directory not found by walking up from "
                + Paths.get("").toAbsolutePath().normalize()
                + " (set -D" + PROPERTY + "=<path> to enable corpus tests)");
        return found;
    }

    /** Walk parents looking for {@code <ancestor>/examples/generated}. */
    private static Path searchUpward(Path start) {
        for (Path p = start; p != null; p = p.getParent()) {
            Path candidate = p.resolve("examples");
            if (Files.isDirectory(candidate)
                    && Files.isDirectory(candidate.resolve("generated"))) {
                return candidate.normalize();
            }
        }
        return null;
    }
}
