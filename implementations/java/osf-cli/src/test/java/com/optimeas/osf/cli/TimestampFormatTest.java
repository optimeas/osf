// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH
package com.optimeas.osf.cli;

import static org.assertj.core.api.Assertions.assertThat;
import org.junit.jupiter.api.Test;

class TimestampFormatTest {
    @Test void seconds() { assertThat(TimestampFormat.SECONDS.render(1_500_000_000L)).isEqualTo("1.500000000"); }
    @Test void nanoseconds() { assertThat(TimestampFormat.NANOSECONDS.render(1_500_000_000L)).isEqualTo("1500000000"); }
    @Test void iso8601() { assertThat(TimestampFormat.ISO8601.render(0L)).isEqualTo("1970-01-01T00:00:00Z"); }
    @Test void datetime() { assertThat(TimestampFormat.DATETIME.render(0L)).isEqualTo("1970-01-01 00:00:00.000"); }
}
