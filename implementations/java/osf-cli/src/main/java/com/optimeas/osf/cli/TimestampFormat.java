// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH
package com.optimeas.osf.cli;

import java.time.Instant;
import java.time.ZoneOffset;
import java.time.format.DateTimeFormatter;

/**
 * Render a nanosecond-epoch timestamp as a human-readable or machine-readable string.
 */
public enum TimestampFormat {

    /** Human-readable UTC datetime with millisecond precision: {@code 1970-01-01 00:00:00.000} */
    DATETIME {
        private static final DateTimeFormatter FMT =
                DateTimeFormatter.ofPattern("uuuu-MM-dd HH:mm:ss.SSS").withZone(ZoneOffset.UTC);

        @Override
        public String render(long ns) {
            return FMT.format(instantOf(ns));
        }
    },

    /** Decimal seconds with 9-digit fractional part: {@code 1.500000000} */
    SECONDS {
        @Override
        public String render(long ns) {
            long sec  = Math.floorDiv(ns, 1_000_000_000L);
            long frac = Math.floorMod(ns, 1_000_000_000L);
            return String.format("%d.%09d", sec, frac);
        }
    },

    /** ISO-8601 UTC instant truncated to seconds: {@code 1970-01-01T00:00:00Z} */
    ISO8601 {
        private static final DateTimeFormatter FMT =
                DateTimeFormatter.ofPattern("uuuu-MM-dd'T'HH:mm:ss'Z'").withZone(ZoneOffset.UTC);

        @Override
        public String render(long ns) {
            return FMT.format(instantOf(ns));
        }
    },

    /** Raw nanosecond integer: {@code 1500000000} */
    NANOSECONDS {
        @Override
        public String render(long ns) {
            return Long.toString(ns);
        }
    };

    /** Render the nanosecond-epoch timestamp {@code ns} as a string. */
    public abstract String render(long ns);

    // -------------------------------------------------------------------------
    // Shared helper

    static Instant instantOf(long ns) {
        long sec  = Math.floorDiv(ns, 1_000_000_000L);
        long frac = Math.floorMod(ns, 1_000_000_000L);
        return Instant.ofEpochSecond(sec, frac);
    }
}
