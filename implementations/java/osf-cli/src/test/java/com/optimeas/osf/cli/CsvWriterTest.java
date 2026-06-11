// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH
package com.optimeas.osf.cli;

import static org.assertj.core.api.Assertions.assertThat;
import org.junit.jupiter.api.Test;

class CsvWriterTest {
    @Test void unifiedMergesTimestampsAndBlanksMissing() {
        // two channels with partially overlapping timestamps
        long[] tA = {10, 20, 30}; double[] vA = {1, 2, 3};
        long[] tB = {20, 40};     double[] vB = {9, 8};
        String csv = CsvWriter.unified(
            java.util.List.of(new CsvWriter.Column("A", tA, vA), new CsvWriter.Column("B", tB, vB)),
            TimestampFormat.NANOSECONDS);
        // header + 4 rows (10,20,30,40); B blank at 10/30, A blank at 40
        assertThat(csv).startsWith("timestamp,A,B");
        assertThat(csv).contains("\n10,1,\n").contains("\n20,2,9\n").contains("\n40,,8");
    }
}
