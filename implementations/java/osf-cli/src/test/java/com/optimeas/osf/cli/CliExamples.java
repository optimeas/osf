// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH
package com.optimeas.osf.cli;

import java.nio.file.Files;
import java.nio.file.Path;
import org.junit.jupiter.api.Assumptions;

/** Resolves the repo-root examples dir; skips the test when absent. */
final class CliExamples {
    private CliExamples() {}
    static Path dir() {
        // Surefire CWD is the module dir; walk up to a dir containing examples/generated.
        Path p = Path.of("").toAbsolutePath();
        for (int i = 0; i < 6 && p != null; i++, p = p.getParent()) {
            Path ex = p.resolve("examples");
            if (Files.isDirectory(ex.resolve("generated"))) return ex;
        }
        Assumptions.assumeTrue(false, "examples/ not found; skipping CLI corpus test");
        return null; // unreachable
    }
    static Path generated(String name) { return dir().resolve("generated").resolve(name); }
}
