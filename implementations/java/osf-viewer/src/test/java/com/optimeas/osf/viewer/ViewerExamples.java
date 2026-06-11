// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH
package com.optimeas.osf.viewer;

import org.junit.jupiter.api.Assumptions;

import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;

/**
 * Locates the reference example corpus for the viewer integration tests.
 *
 * <p>Walks up from the JVM CWD looking for a directory containing
 * {@code examples/generated}. When the corpus is absent the calling test is
 * <em>skipped</em> (JUnit assumption) rather than failed.
 */
final class ViewerExamples {

    private ViewerExamples() {}

    /**
     * Resolve the absolute path to a file inside {@code examples/generated/}.
     * Skips the calling test (via {@link Assumptions#assumeTrue}) when the
     * corpus directory cannot be found.
     *
     * @param name file name inside {@code examples/generated/}
     * @return absolute {@link Path} to the requested file
     */
    static Path generated(String name) {
        Path generatedDir = findGeneratedDir();
        return generatedDir.resolve(name);
    }

    /** Walk parents until we find {@code <ancestor>/examples/generated}. */
    private static Path findGeneratedDir() {
        Path start = Paths.get("").toAbsolutePath().normalize();
        for (Path p = start; p != null; p = p.getParent()) {
            Path candidate = p.resolve("examples").resolve("generated");
            if (Files.isDirectory(candidate)) {
                return candidate.normalize();
            }
        }
        Assumptions.assumeTrue(false,
                "examples/generated not found by walking up from " + start
                + "; set the working directory inside the repository to enable corpus tests");
        // unreachable — assumeTrue throws Assumption failed when false
        throw new IllegalStateException("unreachable");
    }
}
