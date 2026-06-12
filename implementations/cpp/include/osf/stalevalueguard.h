// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

/**
 * @file stalevalueguard.h
 * @brief Freshness layer over StreamingWriter that re-emits the last
 *        value of idle timestamped channels.
 *
 * Timestamped (sporadic / event) channels only receive a sample when
 * their value changes. On a time-series trace or in downstream analysis,
 * a channel last sampled long ago is ambiguous: is it still at that value
 * or did the recording stop? The optiMEAS convention bounds this
 * staleness by re-emitting the last known value at most every
 * repeat-interval (default 100 s) so every guarded channel's on-disk
 * trace stays "fresh".
 *
 * StaleValueGuard is a thin write-through wrapper around a caller-owned
 * StreamingWriter (the guard is constructed after the writer's start()).
 * The caller routes timestamped writes through the guard, which forwards
 * each write to the writer and, on success, caches the channel's last
 * (timestamp, value). poll(nowNs) then re-emits the cached value of any
 * channel that has been idle for at least the repeat interval, stamped at
 * nowNs. The guard is decoupled from the writer — it owns no file handle
 * and never touches writer internals.
 *
 * Design notes:
 *   - Pull-based: no internal clock and no background thread. The caller
 *     supplies nowNs to poll() (mirroring StreamingWriter, where the
 *     caller supplies all timestamps). This keeps the guard deterministic,
 *     embedded-friendly, and trivially testable.
 *   - At most one re-emit per channel per poll() — the guard keeps a trace
 *     fresh, it does not backfill the idle gap with intermediate points.
 *   - Numeric (11 types) and GpsLocation channels only. String / binary
 *     channels are intentionally not guarded (re-emitting large blobs is
 *     undesirable).
 *   - Channels auto-track on their first successful write-through; a
 *     channel never written through the guard is never re-emitted.
 *   - Channel-type validation is delegated to the writer: re-emit calls
 *     the same write_timestamped_* methods, so a non-timestamped channel
 *     is rejected there.
 *
 * Thread safety: not thread-safe (same contract as StreamingWriter).
 * Methods must be called from a single thread or serialized externally.
 */

#pragma once

#include "osf/block.h"             // GpsLocation
#include "osf/error.h"
#include "osf/streamingwriter.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <variant>

namespace osf {

class StaleValueGuard {
public:
    /// Default freshness interval: 100 seconds, in nanoseconds.
    static constexpr std::int64_t DEFAULT_REPEAT_INTERVAL_NS =
        100'000'000'000;

    /**
     * @brief Construct a guard over a caller-owned StreamingWriter.
     * @param writer            The writer to forward writes to. Must
     *                          outlive the guard. Typically already
     *                          started; writes are validated by the
     *                          writer regardless.
     * @param repeatIntervalNs Idle threshold after which poll() re-emits
     *                          the last value. Expected to be positive; a
     *                          non-positive value makes every poll() that
     *                          advances now re-emit.
     */
    explicit StaleValueGuard(
        StreamingWriter& writer,
        std::int64_t repeatIntervalNs = DEFAULT_REPEAT_INTERVAL_NS);

    StaleValueGuard(StaleValueGuard const&) = delete;
    StaleValueGuard& operator=(StaleValueGuard const&) = delete;

    /// The configured re-emit interval in nanoseconds.
    [[nodiscard]] std::int64_t repeatIntervalNs() const noexcept;

    // ── Write-through (numeric) — caches last value+ts on success ──────

    /**
     * @brief Forward a single timestamped numeric sample to the writer
     *        and, on success, cache it as the channel's last value.
     */
    template <typename T>
    [[nodiscard]] Result<void> writeTimestampedSample(
        std::uint16_t channel, std::int64_t timestampNs, T value);

    /**
     * @brief Forward a batch of timestamped numeric samples to the writer
     *        and, on success, cache the LAST sample of the batch as the
     *        channel's last value. A zero-count batch forwards as-is and
     *        does not change the cache.
     */
    template <typename T>
    [[nodiscard]] Result<void> writeTimestampedSamples(
        std::uint16_t channel, std::int64_t const* timestampsNs,
        T const* values, std::size_t count);

    // ── Write-through (GPS) ────────────────────────────────────────────

    [[nodiscard]] Result<void> writeTimestampedGpsSample(
        std::uint16_t channel, std::int64_t timestampNs, GpsLocation value);

    [[nodiscard]] Result<void> writeTimestampedGpsSamples(
        std::uint16_t channel, std::int64_t const* timestampsNs,
        GpsLocation const* values, std::size_t count);

    // ── Freshness sweep ────────────────────────────────────────────────

    /**
     * @brief Re-emit the cached value of every tracked channel idle for at
     *        least the repeat interval, stamped at nowNs.
     * @return The number of channels re-emitted, or the first writer error
     *         encountered (remaining channels for that sweep are skipped).
     *
     * A channel is re-emitted when `nowNs - lastActivityNs >= interval`.
     * On a successful re-emit the channel's last-activity timestamp is set
     * to nowNs, so the next re-emit is one interval later. Real writes
     * also advance last-activity, so an actively updated channel never
     * receives a synthetic repeat.
     */
    [[nodiscard]] Result<std::size_t> poll(std::int64_t nowNs);

    // ── Introspection / control ────────────────────────────────────────

    /// Whether the channel has been written through the guard at least once.
    [[nodiscard]] bool isTracked(std::uint16_t channel) const noexcept;

    /// Stop guarding one channel (drops its cached value). No-op if untracked.
    void forget(std::uint16_t channel);

    /// Stop guarding all channels (drops all cached values).
    void clear() noexcept;

private:
    using CachedValue = std::variant<
        bool, std::int8_t, std::int16_t, std::int32_t, std::int64_t,
        std::uint8_t, std::uint16_t, std::uint32_t, std::uint64_t,
        float, double, GpsLocation>;

    struct ChannelEntry {
        std::int64_t lastActivityNs;
        CachedValue value;
    };

    StreamingWriter& writer_;
    std::int64_t interval_ns_;
    std::map<std::uint16_t, ChannelEntry> tracked_;

    // Re-emit a cached value at nowNs by dispatching over the variant to
    // the matching writer method. Defined in the .cpp.
    [[nodiscard]] Result<void> reemit(std::uint16_t channel,
                                      std::int64_t nowNs,
                                      CachedValue const& v);
};

// ── Numeric template bodies — thin forward + cache ────────────────────

template <typename T>
Result<void> StaleValueGuard::writeTimestampedSample(
        std::uint16_t channel, std::int64_t timestampNs, T value) {
    auto r = writer_.writeTimestampedSample<T>(channel, timestampNs,
                                                 value);
    if (r) {
        tracked_[channel] = ChannelEntry{timestampNs, CachedValue{value}};
    }
    return r;
}

template <typename T>
Result<void> StaleValueGuard::writeTimestampedSamples(
        std::uint16_t channel, std::int64_t const* timestampsNs,
        T const* values, std::size_t count) {
    auto r = writer_.writeTimestampedSamples<T>(channel, timestampsNs,
                                                  values, count);
    if (r && count > 0) {
        tracked_[channel] = ChannelEntry{timestampsNs[count - 1],
                                         CachedValue{values[count - 1]}};
    }
    return r;
}

}  // namespace osf
