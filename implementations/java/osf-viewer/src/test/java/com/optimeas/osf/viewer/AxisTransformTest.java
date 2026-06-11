// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH
package com.optimeas.osf.viewer;

import static org.assertj.core.api.Assertions.assertThat;
import org.junit.jupiter.api.Test;

class AxisTransformTest {
    @Test void timeToXMapsRangeToWidth() {
        var ax = new AxisTransform(1000L, 2000L, 0.0, 100.0, 800, 600);
        assertThat(ax.timeToX(1000L)).isEqualTo(0.0);
        assertThat(ax.timeToX(2000L)).isEqualTo(800.0);
        assertThat(ax.timeToX(1500L)).isEqualTo(400.0);
    }
    @Test void valueToYIsInvertedToScreen() {
        var ax = new AxisTransform(0L, 1L, 0.0, 100.0, 800, 600);
        assertThat(ax.valueToY(100.0)).isEqualTo(0.0);    // top
        assertThat(ax.valueToY(0.0)).isEqualTo(600.0);     // bottom
    }
    @Test void xToTimeRoundTrips() {
        var ax = new AxisTransform(1000L, 2000L, 0.0, 100.0, 800, 600);
        assertThat(ax.xToTime(ax.timeToX(1700L))).isEqualTo(1700L);
    }
}
