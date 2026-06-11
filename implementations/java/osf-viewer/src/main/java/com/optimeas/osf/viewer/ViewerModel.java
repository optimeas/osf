// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH
package com.optimeas.osf.viewer;

import com.optimeas.osf.DataChannel;
import com.optimeas.osf.DataManager;
import com.optimeas.osf.DataType;

import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;

/**
 * UI-free view state for the OSF viewer.
 *
 * <p>Holds the loaded {@link DataManager}, the full channel list, the
 * currently selected (chartable) channels, the visible time range, and the
 * per-channel cached sample data + Y autoscale range. No JavaFX imports —
 * fully unit-testable without a JavaFX runtime.
 *
 * <p>Change notifications are delivered synchronously via a plain
 * {@link Runnable} registered with {@link #setOnChange(Runnable)}.
 */
public final class ViewerModel {

    /**
     * Min/max Y extent for one chartable channel's values.
     *
     * <p>{@code max >= min} for any channel with at least one sample.
     * For a channel with a single constant value, {@code min == max}.
     */
    public record YRange(double min, double max) {}

    // -----------------------------------------------------------------------
    // Per-channel cache entry (chartable channels only)
    // -----------------------------------------------------------------------
    private record ChannelCache(long[] timestampsNs, double[] values, YRange yRange) {}

    // -----------------------------------------------------------------------
    // State
    // -----------------------------------------------------------------------

    private List<DataChannel> channels = List.of();
    private final Set<String> selectedNames = new LinkedHashSet<>();
    private long visibleT0;
    private long visibleT1;
    private final Map<String, ChannelCache> cache = new HashMap<>();
    private Runnable onChange;

    // -----------------------------------------------------------------------
    // Data loading
    // -----------------------------------------------------------------------

    /**
     * Load a {@link DataManager} into the model.
     *
     * <p>This method:
     * <ol>
     *   <li>Stores the channel list (all channels from the manager).</li>
     *   <li>Clears the selection.</li>
     *   <li>For every <em>chartable</em> channel (numeric DataType, at least one
     *       sample) caches its {@code timestampsNs()} + {@code asDoubles()} and
     *       computes the value min/max ({@link YRange}).</li>
     *   <li>Initialises the visible range to the union of all chartable channels'
     *       timestamp extents: {@code visibleT0 = min(first timestamps)},
     *       {@code visibleT1 = max(last timestamps)}.</li>
     * </ol>
     *
     * @param mgr the loaded manager (must not be {@code null})
     */
    public void setData(DataManager mgr) {
        channels = mgr.channels();
        selectedNames.clear();
        cache.clear();

        long globalMin = Long.MAX_VALUE;
        long globalMax = Long.MIN_VALUE;

        for (DataChannel ch : channels) {
            if (!isChartable(ch)) continue;
            long[] ts = ch.timestampsNs();
            if (ts == null || ts.length == 0) continue;

            double[] vals = ch.asDoubles();

            // Compute Y range
            double mn = vals[0], mx = vals[0];
            for (int i = 1; i < vals.length; i++) {
                if (vals[i] < mn) mn = vals[i];
                if (vals[i] > mx) mx = vals[i];
            }

            cache.put(ch.name(), new ChannelCache(ts, vals, new YRange(mn, mx)));

            // Accumulate global time extent
            if (ts[0] < globalMin) globalMin = ts[0];
            if (ts[ts.length - 1] > globalMax) globalMax = ts[ts.length - 1];
        }

        // Fall back to zero range if no chartable channels had samples
        visibleT0 = (globalMin == Long.MAX_VALUE) ? 0L : globalMin;
        visibleT1 = (globalMax == Long.MIN_VALUE) ? 0L : globalMax;

        notifyChange();
    }

    // -----------------------------------------------------------------------
    // Channel list
    // -----------------------------------------------------------------------

    /**
     * All channels in metablock order. Empty before {@link #setData} is called.
     */
    public List<DataChannel> channels() {
        return channels;
    }

    // -----------------------------------------------------------------------
    // Selection
    // -----------------------------------------------------------------------

    /**
     * Select or deselect a channel by name.
     *
     * <p>Selecting a non-chartable channel (string, binary, GPS) is a no-op —
     * it will not appear in {@link #selectedChannels()}.
     *
     * @param name the channel's fully-qualified name
     * @param on   {@code true} to select, {@code false} to deselect
     */
    public void setSelected(String name, boolean on) {
        if (on) {
            // Only add if the channel is chartable (has a cache entry)
            if (cache.containsKey(name)) {
                selectedNames.add(name);
                notifyChange();
            }
        } else {
            if (selectedNames.remove(name)) {
                notifyChange();
            }
        }
    }

    /**
     * The currently selected chartable channels, in selection order.
     * Never contains string/binary/GPS channels.
     */
    public List<DataChannel> selectedChannels() {
        if (selectedNames.isEmpty()) return List.of();
        List<DataChannel> result = new ArrayList<>();
        for (DataChannel ch : channels) {
            if (selectedNames.contains(ch.name())) {
                result.add(ch);
            }
        }
        return Collections.unmodifiableList(result);
    }

    // -----------------------------------------------------------------------
    // Visible time range
    // -----------------------------------------------------------------------

    /**
     * Start of the visible time range (inclusive), in nanoseconds.
     * Initialised to the minimum first-timestamp across all chartable channels.
     */
    public long visibleT0() {
        return visibleT0;
    }

    /**
     * End of the visible time range (inclusive), in nanoseconds.
     * Initialised to the maximum last-timestamp across all chartable channels.
     * Greater than {@link #visibleT0()} after {@link #setData} on a non-empty file.
     */
    public long visibleT1() {
        return visibleT1;
    }

    /**
     * Pan the visible range by shifting both endpoints by {@code deltaNs}.
     * Does not change the span.
     */
    public void pan(long deltaNs) {
        visibleT0 += deltaNs;
        visibleT1 += deltaNs;
        notifyChange();
    }

    /**
     * Set an explicit visible time range.
     *
     * @param t0 start (inclusive, ns)
     * @param t1 end (inclusive, ns); must be &gt; t0
     */
    public void setVisibleRange(long t0, long t1) {
        if (t1 <= t0) throw new IllegalArgumentException("t1 must be > t0");
        visibleT0 = t0;
        visibleT1 = t1;
        notifyChange();
    }

    // -----------------------------------------------------------------------
    // Per-channel Y autoscale
    // -----------------------------------------------------------------------

    /**
     * The cached Y range (value min/max) for a chartable channel.
     *
     * @param name the channel's fully-qualified name
     * @return the cached {@link YRange}; {@code max >= min} for any channel with data
     * @throws IllegalArgumentException if the channel name is unknown or not chartable
     */
    public YRange yRangeFor(String name) {
        ChannelCache entry = cache.get(name);
        if (entry == null) {
            throw new IllegalArgumentException(
                    "no chartable channel with name '" + name + "' in this model");
        }
        return entry.yRange();
    }

    /**
     * The cached timestamps (ns) for a chartable channel.
     * Returns the channel's own backing array — callers must not mutate it.
     *
     * @param name the channel's fully-qualified name
     * @throws IllegalArgumentException if unknown or not chartable
     */
    public long[] timestampsNsFor(String name) {
        ChannelCache entry = cache.get(name);
        if (entry == null) {
            throw new IllegalArgumentException(
                    "no chartable channel with name '" + name + "'");
        }
        return entry.timestampsNs();
    }

    /**
     * The cached double values for a chartable channel.
     * Returns a direct reference — callers must not mutate it.
     *
     * @param name the channel's fully-qualified name
     * @throws IllegalArgumentException if unknown or not chartable
     */
    public double[] valuesFor(String name) {
        ChannelCache entry = cache.get(name);
        if (entry == null) {
            throw new IllegalArgumentException(
                    "no chartable channel with name '" + name + "'");
        }
        return entry.values();
    }

    // -----------------------------------------------------------------------
    // Change callback
    // -----------------------------------------------------------------------

    /**
     * Register a callback invoked synchronously whenever the model's observable
     * state changes (selection, visible range). No JavaFX bindings — plain
     * {@link Runnable}.
     *
     * @param r the callback, or {@code null} to clear
     */
    public void setOnChange(Runnable r) {
        this.onChange = r;
    }

    private void notifyChange() {
        if (onChange != null) onChange.run();
    }

    // -----------------------------------------------------------------------
    // Chartability predicate
    // -----------------------------------------------------------------------

    /**
     * A channel is <em>chartable</em> when its DataType supports
     * {@link DataChannel#asDoubles()}. That covers all boolean, integer, and
     * floating-point types. String, binary, GPS, and unsupported types are
     * excluded.
     */
    static boolean isChartable(DataChannel ch) {
        return switch (ch.dataType()) {
            case BOOL,
                 INT8, INT16, INT32, INT64,
                 UINT8, UINT16, UINT32, UINT64,
                 FLOAT, DOUBLE -> true;
            default -> false; // STRING, BINARY, GPS_LOCATION, UNSUPPORTED
        };
    }
}
