// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

#include "osf/stale_value_guard.hpp"

#include <type_traits>
#include <utility>

namespace osf {

StaleValueGuard::StaleValueGuard(StreamingWriter& writer,
                                 std::int64_t repeat_interval_ns)
    : writer_{writer}, interval_ns_{repeat_interval_ns} {}

std::int64_t StaleValueGuard::repeat_interval_ns() const noexcept {
    return interval_ns_;
}

Result<void> StaleValueGuard::write_timestamped_gps_sample(
        std::uint16_t channel, std::int64_t timestamp_ns,
        GpsLocation value) {
    auto r = writer_.write_timestamped_gps_sample(channel, timestamp_ns,
                                                  value);
    if (r) {
        tracked_[channel] = ChannelEntry{timestamp_ns, CachedValue{value}};
    }
    return r;
}

Result<void> StaleValueGuard::write_timestamped_gps_samples(
        std::uint16_t channel, std::int64_t const* timestamps_ns,
        GpsLocation const* values, std::size_t count) {
    auto r = writer_.write_timestamped_gps_samples(channel, timestamps_ns,
                                                   values, count);
    if (r && count > 0) {
        tracked_[channel] = ChannelEntry{timestamps_ns[count - 1],
                                         CachedValue{values[count - 1]}};
    }
    return r;
}

Result<std::size_t> StaleValueGuard::poll(std::int64_t now_ns) {
    std::size_t reemitted = 0;
    for (auto& [channel, entry] : tracked_) {
        if (now_ns - entry.last_activity_ns < interval_ns_) {
            continue;
        }
        if (auto r = reemit(channel, now_ns, entry.value); !r) {
            return tl::make_unexpected(r.error());
        }
        entry.last_activity_ns = now_ns;
        ++reemitted;
    }
    return reemitted;
}

Result<void> StaleValueGuard::reemit(std::uint16_t channel,
                                     std::int64_t now_ns,
                                     CachedValue const& v) {
    return std::visit(
        [&](auto const& value) -> Result<void> {
            using U = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<U, GpsLocation>) {
                return writer_.write_timestamped_gps_sample(channel, now_ns,
                                                            value);
            } else {
                return writer_.write_timestamped_sample<U>(channel, now_ns,
                                                           value);
            }
        },
        v);
}

bool StaleValueGuard::is_tracked(std::uint16_t channel) const noexcept {
    return tracked_.find(channel) != tracked_.end();
}

void StaleValueGuard::forget(std::uint16_t channel) {
    tracked_.erase(channel);
}

void StaleValueGuard::clear() noexcept {
    tracked_.clear();
}

}  // namespace osf
