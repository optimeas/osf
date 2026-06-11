// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH
package com.optimeas.osf.viewer;

import static org.assertj.core.api.Assertions.assertThat;
import org.junit.jupiter.api.Test;

class DecimatorTest {

    @Test
    void spikeBetweenSamplesSurvivesInPixelColumn() {
        // 1000 samples in [0,1000) ns; a sharp spike at index 500 (value 100), else ~0.
        long[] ts = new long[1000]; double[] v = new double[1000];
        for (int i = 0; i < 1000; i++) { ts[i] = i; v[i] = 0.0; }
        v[500] = 100.0;
        // Render to 10 pixels over the full range: the spike falls in column 5.
        Decimator.PixelColumn[] cols = Decimator.reduce(ts, v, 0, 1000, 10);
        assertThat(cols).hasSize(10);
        assertThat(cols[5].hasData()).isTrue();
        assertThat(cols[5].maxY()).isEqualTo(100.0); // spike preserved — NOT averaged/skipped away
        assertThat(cols[0].maxY()).isEqualTo(0.0);
    }

    @Test
    void emptyColumnsFlaggedNoData() {
        long[] ts = {0, 1000}; double[] v = {1.0, 2.0};
        // 10 pixels over [0,1000): only the first and last buckets have a sample.
        Decimator.PixelColumn[] cols = Decimator.reduce(ts, v, 0, 1000, 10);
        assertThat(cols[5].hasData()).isFalse();
    }

    @Test
    void singleSamplePerPixelDegeneratesToExactValues() {
        long[] ts = {0, 1, 2, 3}; double[] v = {5, 6, 7, 8};
        Decimator.PixelColumn[] cols = Decimator.reduce(ts, v, 0, 4, 4);
        assertThat(cols[0].minY()).isEqualTo(5.0);
        assertThat(cols[0].maxY()).isEqualTo(5.0);
        assertThat(cols[3].minY()).isEqualTo(8.0);
    }
}
