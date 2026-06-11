// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH
package com.optimeas.osf.viewer;

/**
 * Pure coordinate mapping between time/value space and pixel space.
 * No JavaFX imports — fully testable without a JavaFX runtime.
 *
 * <p>Time axis: t0..t1 maps linearly to x=0..widthPx.
 * Value axis: yMin..yMax maps to y=heightPx..0 (INVERTED — y=0 is the top of
 * the screen, yMax maps to y=0, yMin maps to y=heightPx).
 */
public final class AxisTransform {

    private final long t0;
    private final long t1;
    private final double yMin;
    private final double yMax;
    private final double widthPx;
    private final double heightPx;

    /** Precomputed spans (clamped to 1 to avoid division by zero). */
    private final double timeSpan;   // t1 - t0, clamped
    private final double valueSpan;  // yMax - yMin, clamped

    /**
     * @param t0       start time (ns or any monotonic unit)
     * @param t1       end time (same unit as t0)
     * @param yMin     minimum data value (maps to y=heightPx, screen bottom)
     * @param yMax     maximum data value (maps to y=0, screen top)
     * @param widthPx  canvas width in pixels
     * @param heightPx canvas height in pixels
     */
    public AxisTransform(long t0, long t1,
                         double yMin, double yMax,
                         double widthPx, double heightPx) {
        this.t0       = t0;
        this.t1       = t1;
        this.yMin     = yMin;
        this.yMax     = yMax;
        this.widthPx  = widthPx;
        this.heightPx = heightPx;
        // Guard zero spans: if start==end we avoid divide-by-zero by treating
        // the span as 1, which causes all mappings to return 0.
        this.timeSpan  = (t1 == t0)       ? 1.0 : (double)(t1 - t0);
        this.valueSpan = (yMax == yMin)    ? 1.0 : (yMax - yMin);
    }

    // -------------------------------------------------------------------------
    // Time <-> X
    // -------------------------------------------------------------------------

    /** Maps a timestamp to a screen x-coordinate in [0, widthPx]. */
    public double timeToX(long t) {
        return (t - t0) / timeSpan * widthPx;
    }

    /**
     * Maps a screen x-coordinate back to the nearest timestamp.
     * Uses {@link Math#round} so the round-trip {@code xToTime(timeToX(t)) == t}
     * holds for integer-nanosecond timestamps.
     */
    public long xToTime(double x) {
        return t0 + Math.round(x / widthPx * timeSpan);
    }

    // -------------------------------------------------------------------------
    // Value <-> Y  (INVERTED: y=0 is top of screen = yMax)
    // -------------------------------------------------------------------------

    /**
     * Maps a data value to a screen y-coordinate.
     * y=0 corresponds to yMax (top), y=heightPx corresponds to yMin (bottom).
     */
    public double valueToY(double v) {
        return (yMax - v) / valueSpan * heightPx;
    }

    /** Maps a screen y-coordinate back to a data value. */
    public double yToValue(double y) {
        return yMax - (y / heightPx * valueSpan);
    }

    // -------------------------------------------------------------------------
    // Accessors (useful for renderers that need the raw parameters)
    // -------------------------------------------------------------------------

    public long t0()         { return t0; }
    public long t1()         { return t1; }
    public double yMin()     { return yMin; }
    public double yMax()     { return yMax; }
    public double widthPx()  { return widthPx; }
    public double heightPx() { return heightPx; }
}
