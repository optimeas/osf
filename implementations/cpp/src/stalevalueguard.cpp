// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

#include "osf/stalevalueguard.h"

#include <type_traits>
#include <utility>

namespace osf {

StaleValueGuard::StaleValueGuard(StreamingWriter& writer,
                                 std::int64_t repeatIntervalNs)
    : writer_{writer}, interval_ns_{repeatIntervalNs} {}

std::int64_t StaleValueGuard::repeatIntervalNs() const noexcept {
    return interval_ns_;
}

Result<void> StaleValueGuard::writeTimestampedGpsSample(
        std::uint16_t channel, std::int64_t timestampNs,
        GpsLocation value) {
    auto r = writer_.writeTimestampedGpsSample(channel, timestampNs,
                                                  value);
    if (r) {
        tracked_[channel] = ChannelEntry{timestampNs, CachedValue{value}};
    }
    return r;
}

Result<void> StaleValueGuard::writeTimestampedGpsSamples(
        std::uint16_t channel, std::int64_t const* timestampsNs,
        GpsLocation const* values, std::size_t count) {
    auto r = writer_.writeTimestampedGpsSamples(channel, timestampsNs,
                                                   values, count);
    if (r && count > 0) {
        tracked_[channel] = ChannelEntry{timestampsNs[count - 1],
                                         CachedValue{values[count - 1]}};
    }
    return r;
}

Result<std::size_t> StaleValueGuard::poll(std::int64_t nowNs) {
    std::size_t reemitted = 0;
    for (auto& [channel, entry] : tracked_) {
        if (nowNs - entry.lastActivityNs < interval_ns_) {
            continue;
        }
        if (auto r = reemit(channel, nowNs, entry.value); !r) {
            return tl::make_unexpected(r.error());
        }
        entry.lastActivityNs = nowNs;
        ++reemitted;
    }
    return reemitted;
}

Result<void> StaleValueGuard::reemit(std::uint16_t channel,
                                     std::int64_t nowNs,
                                     CachedValue const& v) {
    return std::visit(
        [&](auto const& value) -> Result<void> {
            using U = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<U, GpsLocation>) {
                return writer_.writeTimestampedGpsSample(channel, nowNs,
                                                            value);
            } else {
                return writer_.writeTimestampedSample<U>(channel, nowNs,
                                                           value);
            }
        },
        v);
}

bool StaleValueGuard::isTracked(std::uint16_t channel) const noexcept {
    return tracked_.find(channel) != tracked_.end();
}

void StaleValueGuard::forget(std::uint16_t channel) {
    tracked_.erase(channel);
}

void StaleValueGuard::clear() noexcept {
    tracked_.clear();
}

}  // namespace osf
