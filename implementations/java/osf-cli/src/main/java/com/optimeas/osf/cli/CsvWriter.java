// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH
package com.optimeas.osf.cli;

import java.util.Arrays;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.TreeSet;

/**
 * Lightweight CSV serialiser for OSF channel data.
 *
 * <p>Two modes:
 * <ul>
 *   <li>{@link #unified} — wide table: one row per distinct timestamp across all columns,
 *       blank cells where a column has no sample at that timestamp.</li>
 *   <li>{@link #perChannel} — narrow table: {@code timestamp,value} rows for one column.</li>
 * </ul>
 *
 * <p>Double values that are mathematically integral (and finite) are rendered without a
 * decimal point (e.g. {@code 1} not {@code 1.0}).
 */
public final class CsvWriter {

    private CsvWriter() {}

    // -------------------------------------------------------------------------
    // Public API

    /**
     * A named channel with parallel {@code timestampsNs} / {@code values} arrays.
     */
    public record Column(String name, long[] timestampsNs, double[] values) {}

    /**
     * Build a wide (unified) CSV: header {@code timestamp,<name1>,<name2>,...} followed by
     * one data row per distinct timestamp across all columns. Cells are blank where a column
     * carries no sample at that timestamp.
     */
    public static String unified(List<Column> cols, TimestampFormat tf) {
        // Index each column: timestamp → value
        List<Map<Long, Double>> indexes = cols.stream()
                .map(CsvWriter::buildIndex)
                .toList();

        // Collect all distinct timestamps, sorted
        TreeSet<Long> allTs = new TreeSet<>();
        for (Column c : cols) {
            for (long t : c.timestampsNs()) allTs.add(t);
        }

        // Header
        StringBuilder sb = new StringBuilder();
        sb.append("timestamp");
        for (Column c : cols) {
            sb.append(',');
            sb.append(csvEscape(c.name()));
        }
        sb.append('\n');

        // Rows
        for (long ts : allTs) {
            sb.append(tf.render(ts));
            for (Map<Long, Double> idx : indexes) {
                sb.append(',');
                if (idx.containsKey(ts)) {
                    sb.append(renderDouble(idx.get(ts)));
                }
                // else blank
            }
            sb.append('\n');
        }

        return sb.toString();
    }

    /**
     * Build a narrow (per-channel) CSV: header {@code timestamp,value} followed by one row
     * per sample.
     */
    public static String perChannel(Column c, TimestampFormat tf) {
        StringBuilder sb = new StringBuilder();
        sb.append("timestamp,value\n");
        long[] ts = c.timestampsNs();
        double[] vs = c.values();
        for (int i = 0; i < ts.length; i++) {
            sb.append(tf.render(ts[i]));
            sb.append(',');
            sb.append(renderDouble(vs[i]));
            sb.append('\n');
        }
        return sb.toString();
    }

    // -------------------------------------------------------------------------
    // Helpers

    /** Build a timestamp→value lookup map for a column. */
    private static Map<Long, Double> buildIndex(Column c) {
        Map<Long, Double> map = new HashMap<>(c.timestampsNs().length * 2);
        for (int i = 0; i < c.timestampsNs().length; i++) {
            map.put(c.timestampsNs()[i], c.values()[i]);
        }
        return map;
    }

    /**
     * Render a double: if it is finite and mathematically integral, omit the decimal point.
     * Otherwise delegate to {@link Double#toString(double)}.
     */
    static String renderDouble(double v) {
        if (!Double.isInfinite(v) && !Double.isNaN(v) && v == Math.rint(v)) {
            return Long.toString((long) v);
        }
        return Double.toString(v);
    }

    /**
     * CSV-escape a cell value: wraps in double-quotes and doubles any internal quotes if the
     * value contains a comma, double-quote, or newline; otherwise returns it unchanged.
     */
    static String csvEscape(String value) {
        if (value.indexOf(',') < 0 && value.indexOf('"') < 0
                && value.indexOf('\n') < 0 && value.indexOf('\r') < 0) {
            return value;
        }
        return '"' + value.replace("\"", "\"\"") + '"';
    }
}
