// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

#include "osf/stalevalueguard.h"

#include <type_traits>
#include <utility>

namespace osf {

StaleValueGuard::StaleValueGuard(StreamingWriter& writer,
                                 std::int64_t repeatIntervalNs)
    : m_writer{writer}, m_intervalNs{repeatIntervalNs} {}

std::int64_t StaleValueGuard::repeatIntervalNs() const noexcept {
    return m_intervalNs;
}

Result<void> StaleValueGuard::writeTimestampedGpsSample(
        std::uint16_t channel, std::int64_t timestampNs,
        GpsLocation value) {
    auto r = m_writer.writeTimestampedGpsSample(channel, timestampNs,
                                                  value);
    if (r) {
        m_tracked[channel] = ChannelEntry{timestampNs, CachedValue{value}};
    }
    return r;
}

Result<void> StaleValueGuard::writeTimestampedGpsSamples(
        std::uint16_t channel, std::int64_t const* timestampsNs,
        GpsLocation const* values, std::size_t count) {
    auto r = m_writer.writeTimestampedGpsSamples(channel, timestampsNs,
                                                   values, count);
    if (r && count > 0) {
        m_tracked[channel] = ChannelEntry{timestampsNs[count - 1],
                                         CachedValue{values[count - 1]}};
    }
    return r;
}

Result<std::size_t> StaleValueGuard::poll(std::int64_t nowNs) {
    std::size_t reemitted = 0;
    for (auto& [channel, entry] : m_tracked) {
        if (nowNs - entry.lastActivityNs < m_intervalNs) {
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
                return m_writer.writeTimestampedGpsSample(channel, nowNs,
                                                            value);
            } else {
                return m_writer.writeTimestampedSample<U>(channel, nowNs,
                                                           value);
            }
        },
        v);
}

bool StaleValueGuard::isTracked(std::uint16_t channel) const noexcept {
    return m_tracked.find(channel) != m_tracked.end();
}

void StaleValueGuard::forget(std::uint16_t channel) {
    m_tracked.erase(channel);
}

void StaleValueGuard::clear() noexcept {
    m_tracked.clear();
}

}  // namespace osf
