// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH
package com.optimeas.osf.viewer;

import static org.assertj.core.api.Assertions.assertThat;
import com.optimeas.osf.DataManager;
import org.junit.jupiter.api.Test;

class ViewerModelTest {
    @Test void loadsChannelsAndComputesFullRange() {
        var mgr = DataManager.loadFromFile(ViewerExamples.generated("osf5_equidistant.osf"));
        var model = new ViewerModel();
        model.setData(mgr);
        assertThat(model.channels()).hasSize(3);
        model.setSelected("Sensor/Vibration100Hz", true);
        assertThat(model.selectedChannels()).hasSize(1);
        // full visible range initialized to the data extent
        assertThat(model.visibleT1()).isGreaterThan(model.visibleT0());
        // per-channel Y autoscale present for the selected channel
        var yr = model.yRangeFor("Sensor/Vibration100Hz");
        assertThat(yr.max()).isGreaterThanOrEqualTo(yr.min());
    }
}
