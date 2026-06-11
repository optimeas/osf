// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH
package com.optimeas.osf.viewer;

import com.optimeas.osf.DataChannel;
import javafx.beans.value.ChangeListener;
import javafx.scene.canvas.Canvas;
import javafx.scene.canvas.GraphicsContext;
import javafx.scene.paint.Color;
import javafx.scene.text.Font;

import java.time.Instant;
import java.util.Arrays;
import java.util.List;
import java.util.function.Consumer;

/**
 * JavaFX {@link Canvas} that renders the multi-channel min/max overlay with
 * pan and zoom.
 *
 * <p>The canvas is resizable: it overrides {@link #isResizable()} and
 * re-renders whenever the JavaFX layout engine changes its width or height.
 * Registered {@link #setCursorListener} receives a human-readable readout
 * string on every mouse-move event.
 *
 * <p>Coordinate mapping is delegated entirely to {@link AxisTransform};
 * data reduction to {@link Decimator}.  The model supplies all data and
 * fires the change callback, which triggers {@link #render()}.
 */
public final class PlotCanvas extends Canvas {

    // ------------------------------------------------------------------
    // Channel colour palette (~6 distinct colours)
    // ------------------------------------------------------------------
    private static final Color[] PALETTE = {
        Color.rgb(31,  119, 180),   // blue
        Color.rgb(255, 127,  14),   // orange
        Color.rgb(44,  160,  44),   // green
        Color.rgb(214,  39,  40),   // red
        Color.rgb(148, 103, 189),   // purple
        Color.rgb(140,  86,  75),   // brown
    };

    // ------------------------------------------------------------------
    // Layout constants
    // ------------------------------------------------------------------
    private static final double MARGIN_LEFT   = 8.0;
    private static final double MARGIN_RIGHT  = 8.0;
    private static final double MARGIN_TOP    = 8.0;
    private static final double MARGIN_BOTTOM = 24.0;  // space for time labels

    private static final Color BACKGROUND_COLOR  = Color.rgb(248, 248, 252);
    private static final Color GRID_COLOR        = Color.rgb(210, 210, 220);
    private static final Color BORDER_COLOR      = Color.rgb(100, 100, 120);
    private static final int   GRID_LINES_X      = 5;
    private static final int   GRID_LINES_Y      = 5;

    // ------------------------------------------------------------------
    // State
    // ------------------------------------------------------------------
    private final ViewerModel model;

    /** X position where a drag started (screen pixels). */
    private double dragStartX = Double.NaN;
    /** Snapshot of model.visibleT0() at the time the drag started. */
    private long dragStartT0;
    /** Snapshot of model.visibleT1() at the time the drag started. */
    private long dragStartT1;

    private Consumer<String> cursorListener;

    // ------------------------------------------------------------------
    // Constructor
    // ------------------------------------------------------------------

    /**
     * Create a PlotCanvas bound to {@code model}.
     *
     * @param model the viewer model (must not be {@code null})
     */
    public PlotCanvas(ViewerModel model) {
        this.model = model;

        // Trigger a re-render whenever the model's observable state changes.
        model.setOnChange(this::render);

        // Re-render on size changes driven by the JavaFX layout engine.
        ChangeListener<Number> sizeListener = (obs, oldVal, newVal) -> render();
        widthProperty().addListener(sizeListener);
        heightProperty().addListener(sizeListener);

        // ----------------------------------------------------------------
        // Mouse drag — pan
        // ----------------------------------------------------------------
        setOnMousePressed(e -> {
            dragStartX  = e.getX();
            dragStartT0 = model.visibleT0();
            dragStartT1 = model.visibleT1();
        });

        setOnMouseDragged(e -> {
            if (Double.isNaN(dragStartX)) return;
            double w = getWidth();
            if (w <= 0) return;

            long span     = dragStartT1 - dragStartT0;
            double dx     = e.getX() - dragStartX;          // pixels moved
            long deltaNs  = -(long) (dx / w * span);        // dragging right = pan backward in time

            long newT0 = dragStartT0 + deltaNs;
            long newT1 = dragStartT1 + deltaNs;
            // Use setVisibleRange so the model enforces t1 > t0.
            if (newT1 > newT0) {
                model.setVisibleRange(newT0, newT1);
            }
        });

        setOnMouseReleased(e -> dragStartX = Double.NaN);

        // ----------------------------------------------------------------
        // Scroll wheel — zoom about cursor
        // ----------------------------------------------------------------
        setOnScroll(e -> {
            double w = getWidth();
            if (w <= 0) return;

            long t0   = model.visibleT0();
            long t1   = model.visibleT1();
            long span = t1 - t0;

            // Build a time-axis-only AxisTransform to convert cursor x → time.
            // We use dummy Y values because we only need xToTime here.
            AxisTransform ax = new AxisTransform(t0, t1, 0.0, 1.0, w, 1.0);
            long tc = ax.xToTime(e.getX());

            // Zoom factor: scroll up → zoom in (smaller span).
            double factor = e.getDeltaY() > 0 ? 0.8 : 1.25;
            long newSpan  = Math.max(1L, (long) (span * factor));

            // Keep cursor time stationary: tc stays at the same fraction within
            // the new window.
            long newT0 = tc - (long) ((tc - t0) * factor);
            long newT1 = newT0 + newSpan;

            if (newT1 > newT0) {
                model.setVisibleRange(newT0, newT1);
            }
        });

        // ----------------------------------------------------------------
        // Mouse move — cursor readout
        // ----------------------------------------------------------------
        setOnMouseMoved(e -> fireCursorReadout(e.getX()));
        // Also fire while dragging so the readout stays live.
        setOnMouseDragged(setOnMouseDragged -> {
            // Already wired above for pan — replace with a combined handler.
        });
        // Combine pan + readout in a single dragged handler.
        setOnMouseDragged(e -> {
            // Pan logic (same as above).
            if (!Double.isNaN(dragStartX)) {
                double w = getWidth();
                if (w > 0) {
                    long span     = dragStartT1 - dragStartT0;
                    double dx     = e.getX() - dragStartX;
                    long deltaNs  = -(long) (dx / w * span);
                    long newT0    = dragStartT0 + deltaNs;
                    long newT1    = dragStartT1 + deltaNs;
                    if (newT1 > newT0) {
                        model.setVisibleRange(newT0, newT1);
                    }
                }
            }
            fireCursorReadout(e.getX());
        });
    }

    // ------------------------------------------------------------------
    // Resizable Canvas contract
    // ------------------------------------------------------------------

    @Override public boolean isResizable()            { return true; }
    @Override public double  minWidth(double height)  { return 0; }
    @Override public double  minHeight(double width)  { return 0; }
    @Override public double  maxWidth(double height)  { return Double.MAX_VALUE; }
    @Override public double  maxHeight(double width)  { return Double.MAX_VALUE; }
    @Override public double  prefWidth(double height) { return 600; }
    @Override public double  prefHeight(double width) { return 300; }

    /**
     * Called by the JavaFX layout engine to give us a concrete size.
     * We honour it directly (Canvas does not do layout itself) and re-render.
     */
    @Override
    public void resize(double width, double height) {
        setWidth(width);
        setHeight(height);
        // The property listeners registered in the constructor will trigger render().
    }

    // ------------------------------------------------------------------
    // Rendering
    // ------------------------------------------------------------------

    /**
     * Clear and repaint the entire canvas.
     * Must be called on the JavaFX Application Thread.
     */
    public void render() {
        double w = getWidth();
        double h = getHeight();
        if (w <= 0 || h <= 0) return;

        GraphicsContext gc = getGraphicsContext2D();
        gc.clearRect(0, 0, w, h);

        // ----------------------------------------------------------------
        // Background
        // ----------------------------------------------------------------
        gc.setFill(BACKGROUND_COLOR);
        gc.fillRect(0, 0, w, h);

        // Compute the plot area (inside margins).
        double plotX = MARGIN_LEFT;
        double plotY = MARGIN_TOP;
        double plotW = Math.max(1, w - MARGIN_LEFT - MARGIN_RIGHT);
        double plotH = Math.max(1, h - MARGIN_TOP  - MARGIN_BOTTOM);

        // ----------------------------------------------------------------
        // Grid lines
        // ----------------------------------------------------------------
        gc.setStroke(GRID_COLOR);
        gc.setLineWidth(1.0);

        // Vertical grid lines (time axis)
        for (int i = 1; i < GRID_LINES_X; i++) {
            double x = plotX + i * plotW / GRID_LINES_X;
            gc.strokeLine(x, plotY, x, plotY + plotH);
        }
        // Horizontal grid lines (value axis)
        for (int i = 1; i < GRID_LINES_Y; i++) {
            double y = plotY + i * plotH / GRID_LINES_Y;
            gc.strokeLine(plotX, y, plotX + plotW, y);
        }

        // ----------------------------------------------------------------
        // Axis border
        // ----------------------------------------------------------------
        gc.setStroke(BORDER_COLOR);
        gc.setLineWidth(1.5);
        gc.strokeRect(plotX, plotY, plotW, plotH);

        // ----------------------------------------------------------------
        // Channel data
        // ----------------------------------------------------------------
        List<DataChannel> selected = model.selectedChannels();
        long t0 = model.visibleT0();
        long t1 = model.visibleT1();

        for (int idx = 0; idx < selected.size(); idx++) {
            DataChannel ch    = selected.get(idx);
            Color       color = PALETTE[idx % PALETTE.length];
            String      name  = ch.name();

            long[]   ts   = model.timestampsNsFor(name);
            double[] vals = model.valuesFor(name);
            ViewerModel.YRange yr = model.yRangeFor(name);

            // Guard: if min == max, pad by ±1 so the horizontal line is visible.
            double yMin = yr.min();
            double yMax = yr.max();
            if (yMin == yMax) {
                yMin -= 1.0;
                yMax += 1.0;
            }

            int widthInt = (int) Math.max(1, plotW);
            Decimator.PixelColumn[] cols = Decimator.reduce(ts, vals, t0, t1, widthInt);

            AxisTransform ax = new AxisTransform(t0, t1, yMin, yMax, plotW, plotH);

            gc.setStroke(color);
            gc.setLineWidth(1.0);

            for (int px = 0; px < cols.length; px++) {
                Decimator.PixelColumn col = cols[px];
                if (!col.hasData()) continue;

                double screenX = plotX + px;
                double yTop    = plotY + ax.valueToY(col.maxY());
                double yBottom = plotY + ax.valueToY(col.minY());

                // Clamp to plot area to avoid drawing into the margins.
                yTop    = Math.max(plotY,         Math.min(plotY + plotH, yTop));
                yBottom = Math.max(plotY,         Math.min(plotY + plotH, yBottom));

                // Ensure at least 1 pixel is visible even when min==max.
                if (yTop == yBottom) yBottom = yTop + 1;

                gc.strokeLine(screenX, yTop, screenX, yBottom);
            }
        }

        // ----------------------------------------------------------------
        // Time-axis labels (start / end)
        // ----------------------------------------------------------------
        gc.setFill(BORDER_COLOR);
        gc.setFont(Font.font("Monospaced", 10));
        String labelT0 = formatNs(t0);
        String labelT1 = formatNs(t1);
        gc.fillText(labelT0, plotX,             plotY + plotH + MARGIN_BOTTOM - 6);
        // Right-align the end label (approximate: 7 px per character).
        double t1LabelX = Math.max(plotX, plotX + plotW - labelT1.length() * 7.0);
        gc.fillText(labelT1, t1LabelX,           plotY + plotH + MARGIN_BOTTOM - 6);
    }

    // ------------------------------------------------------------------
    // Cursor readout
    // ------------------------------------------------------------------

    /**
     * Register a listener that receives a human-readable cursor readout string
     * on every mouse-move / mouse-drag event.
     *
     * <p>The readout contains the cursor time and, for each selected channel,
     * the nearest sample value at that time.
     *
     * @param listener the consumer, or {@code null} to clear
     */
    public void setCursorListener(Consumer<String> listener) {
        this.cursorListener = listener;
    }

    private void fireCursorReadout(double mouseX) {
        if (cursorListener == null) return;

        double w = getWidth();
        if (w <= 0) return;

        long t0 = model.visibleT0();
        long t1 = model.visibleT1();

        // Use a time-axis-only AxisTransform to map the cursor x to a time.
        AxisTransform ax = new AxisTransform(t0, t1, 0.0, 1.0, w, 1.0);
        long cursorTime  = ax.xToTime(mouseX);

        StringBuilder sb = new StringBuilder();
        sb.append("t=").append(formatNs(cursorTime));

        List<DataChannel> selected = model.selectedChannels();
        for (DataChannel ch : selected) {
            String   name = ch.name();
            long[]   ts   = model.timestampsNsFor(name);
            double[] vals = model.valuesFor(name);

            if (ts.length == 0) continue;

            // Binary-search for the nearest sample.
            int idx = Arrays.binarySearch(ts, cursorTime);
            if (idx < 0) {
                // insertion point
                int ip = -(idx + 1);
                // Choose the closer of ip-1 and ip.
                if (ip == 0) {
                    idx = 0;
                } else if (ip >= ts.length) {
                    idx = ts.length - 1;
                } else {
                    long diffBefore = Math.abs(cursorTime - ts[ip - 1]);
                    long diffAfter  = Math.abs(ts[ip]     - cursorTime);
                    idx = (diffBefore <= diffAfter) ? (ip - 1) : ip;
                }
            }
            sb.append("  ").append(name).append("=").append(vals[idx]);
        }

        cursorListener.accept(sb.toString());
    }

    // ------------------------------------------------------------------
    // Helpers
    // ------------------------------------------------------------------

    /**
     * Format a nanosecond timestamp as a short human-readable string.
     * Uses epoch-second + millisecond fraction to stay compact.
     */
    private static String formatNs(long ns) {
        long epochSec = ns / 1_000_000_000L;
        long ms       = (ns % 1_000_000_000L) / 1_000_000L;
        // Handle negative remainder for pre-epoch timestamps.
        if (ms < 0) { epochSec--; ms += 1000; }
        Instant instant = Instant.ofEpochSecond(epochSec);
        // Format as "YYYY-MM-DDThh:mm:ss.mmm" (truncated ISO-8601).
        return instant.toString().replace("Z", "") + String.format(".%03dZ", ms);
    }
}
