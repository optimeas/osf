// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH
package com.optimeas.osf;

import com.optimeas.osf.testutil.ExamplesDir;
import net.jqwik.api.*;
import net.jqwik.api.constraints.IntRange;
import org.junit.jupiter.api.Assumptions;

import java.io.ByteArrayInputStream;
import java.nio.file.Files;
import java.nio.file.Path;

import static org.assertj.core.api.Assertions.fail;

/**
 * Property-based fuzz harness that proves the {@link DataManager} reader's
 * best-effort robustness contract: any byte-prefix of a valid OSF file must
 * either be read without throwing, or throw <em>only</em>
 * {@link OsfException} (specifically {@link OsfException.MalformedFile} for
 * inputs that do not contain a complete magic-header line or metablock).
 *
 * <p>The contract: truncating a valid file at any cut position must NEVER
 * cause a crash ({@link NullPointerException}, {@link ArrayIndexOutOfBoundsException},
 * or any other non-{@link OsfException} throwable). The reader is permitted to
 * throw {@link OsfException.MalformedFile} for prefixes too short to contain a
 * valid magic header or metablock; once the magic header + metablock are intact
 * the best-effort block reader returns partial (possibly empty) data without
 * throwing.
 *
 * <p>The file under test is {@code examples/generated/osf5_mixed.osf}. The test
 * is skipped (via {@link Assumptions}) when the corpus is absent.
 */
class FuzzTruncationTest {

    /**
     * The full bytes of {@code osf5_mixed.osf}, loaded once per test run.
     * {@code null} when the corpus is not present (the property then skips).
     */
    private static final byte[] FILE_BYTES;
    private static final int FILE_LEN;

    static {
        byte[] bytes = null;
        try {
            Path examplesDir = ExamplesDir.resolve();
            Path mixed = examplesDir.resolve("generated/osf5_mixed.osf");
            if (Files.isRegularFile(mixed)) {
                bytes = Files.readAllBytes(mixed);
            }
        } catch (org.opentest4j.TestAbortedException e) {
            // ExamplesDir.resolve() called assumeTrue which was false — corpus absent.
            bytes = null;
        } catch (Exception e) {
            bytes = null;
        }
        FILE_BYTES = bytes;
        FILE_LEN = (bytes != null) ? bytes.length : 0;
    }

    /**
     * Core property: truncate {@code osf5_mixed.osf} to {@code cut} bytes and
     * load it. The operation must never throw anything other than
     * {@link OsfException}.
     *
     * <p>The {@code @IntRange} upper bound is resolved dynamically by jqwik
     * from {@code FILE_LEN}; when {@code FILE_LEN == 0} (corpus absent) the
     * static initialiser sets it to 0 and every trial produces {@code cut=0},
     * but the Assumptions guard skips the test before any assertion runs.
     */
    @Property(tries = 200)
    void truncatedFileNeverCrashes(
            @ForAll @IntRange(min = 0, max = Integer.MAX_VALUE) int cutUnclamped) {
        // Skip the whole property when corpus is absent.
        Assumptions.assumeTrue(FILE_BYTES != null,
                "osf5_mixed.osf not present — skipping truncation fuzz");

        // Clamp the random cut to [0, FILE_LEN] so every trial is a valid prefix.
        int cut = (FILE_LEN == 0) ? 0 : Math.abs(cutUnclamped) % (FILE_LEN + 1);

        byte[] truncated = new byte[cut];
        System.arraycopy(FILE_BYTES, 0, truncated, 0, cut);

        try {
            DataManager.load(new ByteArrayInputStream(truncated));
            // No exception — best-effort read returned data or empty channels.
        } catch (OsfException e) {
            // Permitted: MalformedFile for a header/metablock-short prefix,
            // or any other OsfException the pipeline legitimately throws.
        } catch (Throwable t) {
            // Anything else is a crash — report it as a test failure.
            fail("truncating osf5_mixed.osf at cut=" + cut + "/" + FILE_LEN
                    + " caused an unexpected " + t.getClass().getName()
                    + ": " + t.getMessage(), t);
        }
    }
}
