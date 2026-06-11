// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH
package com.optimeas.osf.viewer;

/** Pure min/max-per-pixel reduction — no data is hidden: each pixel column
 *  carries the min AND max of all samples falling in it. */
public final class Decimator {
    private Decimator() {}

    public record PixelColumn(double minY, double maxY, boolean hasData) {}

    /** timestampsNs MUST be ascending. Maps [t0,t1) to widthPx columns. */
    public static PixelColumn[] reduce(long[] timestampsNs, double[] values,
                                       long t0, long t1, int widthPx) {
        PixelColumn[] out = new PixelColumn[widthPx];
        long span = Math.max(1, t1 - t0);
        for (int px = 0; px < widthPx; px++) {
            long cStart = t0 + (long) ((double) px * span / widthPx);
            long cEnd   = t0 + (long) ((double) (px + 1) * span / widthPx);
            int lo = lowerBound(timestampsNs, cStart);
            int hi = lowerBound(timestampsNs, cEnd); // exclusive
            if (lo >= hi) { out[px] = new PixelColumn(0, 0, false); continue; }
            double mn = values[lo], mx = values[lo];
            for (int i = lo + 1; i < hi; i++) {
                double y = values[i];
                if (y < mn) mn = y;
                if (y > mx) mx = y;
            }
            out[px] = new PixelColumn(mn, mx, true);
        }
        return out;
    }

    /** first index with timestampsNs[index] >= key. */
    private static int lowerBound(long[] a, long key) {
        int lo = 0, hi = a.length;
        while (lo < hi) { int mid = (lo + hi) >>> 1; if (a[mid] < key) lo = mid + 1; else hi = mid; }
        return lo;
    }
}
