// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

#include "osf/stalevalueguard.h"
#include "osf/datachannel.h"
#include "osf/error.h"
#include "osf/manager.h"
#include "osf/streamingwriter.h"

#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>

namespace {

std::filesystem::path make_temp_path() {
    static std::atomic<std::uint64_t> counter{0};
    auto const n = counter.fetch_add(1) + 1;
    auto const filename =
        "osf_stale_value_guard_test_" + std::to_string(n) + ".osf";
    return std::filesystem::temp_directory_path() / filename;
}

struct TempFileGuard {
    std::filesystem::path path;
    ~TempFileGuard() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
};

osf::ChannelDef make_ts_channel(std::string name, osf::DataType type) {
    osf::ChannelDef d;
    d.name = std::move(name);
    d.dataType = type;
    d.channelType = osf::ChannelType::Timestamped;
    d.sizeOfLengthValue = 2;
    return d;
}

// The default 100-second repeat interval, in nanoseconds.
constexpr std::int64_t kInterval =
    osf::StaleValueGuard::DEFAULT_REPEAT_INTERVAL_NS;

}  // namespace

// ── 1. No repeat before the interval elapses ─────────────────────────

TEST(StaleValueGuard, no_repeat_before_interval) {
    TempFileGuard g{make_temp_path()};
    {
        osf::StreamingWriter w{g.path};
        ASSERT_TRUE(w.addChannel(
            make_ts_channel("a", osf::DataType::Double)).has_value());
        ASSERT_TRUE(w.start().has_value());

        osf::StaleValueGuard guard{w};
        ASSERT_TRUE(guard.writeTimestampedSample<double>(
            0, /*ts=*/1000, /*v=*/42.0).has_value());

        auto polled = guard.poll(1000 + kInterval - 1);
        ASSERT_TRUE(polled.has_value());
        EXPECT_EQ(*polled, 0u);

        ASSERT_TRUE(w.close().has_value());
    }
    auto mgr = osf::DataManager::loadFromFile(g.path);
    ASSERT_TRUE(mgr.has_value());
    auto const* ch =
        std::get_if<osf::TimestampedChannel>(mgr->channel("a"));
    ASSERT_NE(ch, nullptr);
    auto const pairs = osf::asDoublesFlat(*ch);
    ASSERT_TRUE(pairs.has_value());
    ASSERT_EQ(pairs->size(), 1u);
    EXPECT_EQ((*pairs)[0].first, 1000);
    EXPECT_DOUBLE_EQ((*pairs)[0].second, 42.0);
}

// ── 2. One repeat once the interval has elapsed ──────────────────────

TEST(StaleValueGuard, repeat_once_after_interval) {
    TempFileGuard g{make_temp_path()};
    {
        osf::StreamingWriter w{g.path};
        ASSERT_TRUE(w.addChannel(
            make_ts_channel("a", osf::DataType::Double)).has_value());
        ASSERT_TRUE(w.start().has_value());

        osf::StaleValueGuard guard{w};
        ASSERT_TRUE(guard.writeTimestampedSample<double>(
            0, 1000, 42.0).has_value());

        auto polled = guard.poll(1000 + kInterval);
        ASSERT_TRUE(polled.has_value());
        EXPECT_EQ(*polled, 1u);

        ASSERT_TRUE(w.close().has_value());
    }
    auto mgr = osf::DataManager::loadFromFile(g.path);
    ASSERT_TRUE(mgr.has_value());
    auto const* ch =
        std::get_if<osf::TimestampedChannel>(mgr->channel("a"));
    ASSERT_NE(ch, nullptr);
    auto const pairs = osf::asDoublesFlat(*ch);
    ASSERT_TRUE(pairs.has_value());
    ASSERT_EQ(pairs->size(), 2u);
    EXPECT_EQ((*pairs)[0].first, 1000);
    EXPECT_EQ((*pairs)[1].first, 1000 + kInterval);
    EXPECT_DOUBLE_EQ((*pairs)[1].second, 42.0);  // last value repeated
}

// ── 3. A real write resets staleness ─────────────────────────────────

TEST(StaleValueGuard, real_write_resets_staleness) {
    TempFileGuard g{make_temp_path()};
    {
        osf::StreamingWriter w{g.path};
        ASSERT_TRUE(w.addChannel(
            make_ts_channel("a", osf::DataType::Double)).has_value());
        ASSERT_TRUE(w.start().has_value());

        osf::StaleValueGuard guard{w};
        ASSERT_TRUE(guard.writeTimestampedSample<double>(
            0, 1000, 42.0).has_value());
        // A fresh real sample just before the interval would expire.
        ASSERT_TRUE(guard.writeTimestampedSample<double>(
            0, 1000 + kInterval - 1, 43.0).has_value());

        // now - last_activity = 1 < interval → no repeat.
        auto polled = guard.poll(1000 + kInterval);
        ASSERT_TRUE(polled.has_value());
        EXPECT_EQ(*polled, 0u);

        ASSERT_TRUE(w.close().has_value());
    }
    auto mgr = osf::DataManager::loadFromFile(g.path);
    ASSERT_TRUE(mgr.has_value());
    auto const* ch =
        std::get_if<osf::TimestampedChannel>(mgr->channel("a"));
    ASSERT_NE(ch, nullptr);
    auto const pairs = osf::asDoublesFlat(*ch);
    ASSERT_TRUE(pairs.has_value());
    ASSERT_EQ(pairs->size(), 2u);  // two real samples, no synthetic one
    EXPECT_DOUBLE_EQ((*pairs)[1].second, 43.0);
}

// ── 4. At most one repeat per poll (no backfill) ─────────────────────

TEST(StaleValueGuard, at_most_one_repeat_per_poll) {
    TempFileGuard g{make_temp_path()};
    {
        osf::StreamingWriter w{g.path};
        ASSERT_TRUE(w.addChannel(
            make_ts_channel("a", osf::DataType::Double)).has_value());
        ASSERT_TRUE(w.start().has_value());

        osf::StaleValueGuard guard{w};
        ASSERT_TRUE(guard.writeTimestampedSample<double>(
            0, 1000, 42.0).has_value());

        // Three intervals idle, but a single poll re-emits exactly once.
        auto polled = guard.poll(1000 + 3 * kInterval);
        ASSERT_TRUE(polled.has_value());
        EXPECT_EQ(*polled, 1u);

        ASSERT_TRUE(w.close().has_value());
    }
    auto mgr = osf::DataManager::loadFromFile(g.path);
    ASSERT_TRUE(mgr.has_value());
    auto const* ch =
        std::get_if<osf::TimestampedChannel>(mgr->channel("a"));
    ASSERT_NE(ch, nullptr);
    auto const pairs = osf::asDoublesFlat(*ch);
    ASSERT_TRUE(pairs.has_value());
    ASSERT_EQ(pairs->size(), 2u);
    EXPECT_EQ((*pairs)[1].first, 1000 + 3 * kInterval);
}

// ── 5. Repeated polls keep re-emitting (interval from last activity) ─

TEST(StaleValueGuard, repeated_poll_keeps_reemitting) {
    TempFileGuard g{make_temp_path()};
    {
        osf::StreamingWriter w{g.path};
        ASSERT_TRUE(w.addChannel(
            make_ts_channel("a", osf::DataType::Double)).has_value());
        ASSERT_TRUE(w.start().has_value());

        osf::StaleValueGuard guard{w};
        ASSERT_TRUE(guard.writeTimestampedSample<double>(
            0, 1000, 42.0).has_value());

        auto p1 = guard.poll(1000 + kInterval);
        ASSERT_TRUE(p1.has_value());
        EXPECT_EQ(*p1, 1u);
        // last_activity is now 1000 + kInterval → next repeat one interval on.
        auto p2 = guard.poll(1000 + 2 * kInterval);
        ASSERT_TRUE(p2.has_value());
        EXPECT_EQ(*p2, 1u);

        ASSERT_TRUE(w.close().has_value());
    }
    auto mgr = osf::DataManager::loadFromFile(g.path);
    ASSERT_TRUE(mgr.has_value());
    auto const* ch =
        std::get_if<osf::TimestampedChannel>(mgr->channel("a"));
    ASSERT_NE(ch, nullptr);
    auto const pairs = osf::asDoublesFlat(*ch);
    ASSERT_TRUE(pairs.has_value());
    ASSERT_EQ(pairs->size(), 3u);
    EXPECT_EQ((*pairs)[2].first, 1000 + 2 * kInterval);
}

// ── 6. Multiple channels, mixed numeric types + GPS ──────────────────

TEST(StaleValueGuard, multiple_channels_mixed_types) {
    TempFileGuard g{make_temp_path()};
    {
        osf::StreamingWriter w{g.path};
        ASSERT_TRUE(w.addChannel(
            make_ts_channel("d", osf::DataType::Double)).has_value());
        ASSERT_TRUE(w.addChannel(
            make_ts_channel("i", osf::DataType::Int32)).has_value());
        ASSERT_TRUE(w.addChannel(
            make_ts_channel("b", osf::DataType::Bool)).has_value());
        ASSERT_TRUE(w.addChannel(
            make_ts_channel("g", osf::DataType::GpsLocation)).has_value());
        ASSERT_TRUE(w.start().has_value());

        osf::StaleValueGuard guard{w};
        ASSERT_TRUE(guard.writeTimestampedSample<double>(
            0, 1000, 1.5).has_value());
        ASSERT_TRUE(guard.writeTimestampedSample<std::int32_t>(
            1, 1000, -7).has_value());
        ASSERT_TRUE(guard.writeTimestampedSample<bool>(
            2, 1000, true).has_value());
        ASSERT_TRUE(guard.writeTimestampedGpsSample(
            3, 1000, osf::GpsLocation{48.1, 11.6, 520.0}).has_value());

        auto polled = guard.poll(1000 + kInterval);
        ASSERT_TRUE(polled.has_value());
        EXPECT_EQ(*polled, 4u);

        ASSERT_TRUE(w.close().has_value());
    }
    auto mgr = osf::DataManager::loadFromFile(g.path);
    ASSERT_TRUE(mgr.has_value());

    auto const d = osf::asDoublesFlat(
        *std::get_if<osf::TimestampedChannel>(mgr->channel("d")));
    ASSERT_TRUE(d.has_value());
    ASSERT_EQ(d->size(), 2u);
    EXPECT_DOUBLE_EQ((*d)[1].second, 1.5);

    auto const i = osf::asInt32Flat(
        *std::get_if<osf::TimestampedChannel>(mgr->channel("i")));
    ASSERT_TRUE(i.has_value());
    ASSERT_EQ(i->size(), 2u);
    EXPECT_EQ((*i)[1].second, -7);

    auto const b = osf::asBoolsFlat(
        *std::get_if<osf::TimestampedChannel>(mgr->channel("b")));
    ASSERT_TRUE(b.has_value());
    ASSERT_EQ(b->size(), 2u);
    EXPECT_EQ((*b)[1].second, true);

    auto const gps = osf::asGpsFlat(
        *std::get_if<osf::TimestampedChannel>(mgr->channel("g")));
    ASSERT_TRUE(gps.has_value());
    ASSERT_EQ(gps->size(), 2u);
    EXPECT_EQ((*gps)[1].second, (osf::GpsLocation{48.1, 11.6, 520.0}));
    EXPECT_EQ((*gps)[1].first, 1000 + kInterval);
}

// ── 7. Batch write caches the last sample of the batch ───────────────

TEST(StaleValueGuard, batch_write_caches_last_sample) {
    TempFileGuard g{make_temp_path()};
    {
        osf::StreamingWriter w{g.path};
        ASSERT_TRUE(w.addChannel(
            make_ts_channel("a", osf::DataType::Double)).has_value());
        ASSERT_TRUE(w.start().has_value());

        osf::StaleValueGuard guard{w};
        std::int64_t const ts[] = {1000, 2000, 3000};
        double const vs[] = {1.0, 2.0, 3.0};
        ASSERT_TRUE(guard.writeTimestampedSamples<double>(
            0, ts, vs, 3).has_value());

        // Staleness measured from the last sample's timestamp (3000).
        auto polled = guard.poll(3000 + kInterval);
        ASSERT_TRUE(polled.has_value());
        EXPECT_EQ(*polled, 1u);

        ASSERT_TRUE(w.close().has_value());
    }
    auto mgr = osf::DataManager::loadFromFile(g.path);
    ASSERT_TRUE(mgr.has_value());
    auto const pairs = osf::asDoublesFlat(
        *std::get_if<osf::TimestampedChannel>(mgr->channel("a")));
    ASSERT_TRUE(pairs.has_value());
    ASSERT_EQ(pairs->size(), 4u);
    EXPECT_EQ((*pairs)[3].first, 3000 + kInterval);
    EXPECT_DOUBLE_EQ((*pairs)[3].second, 3.0);  // last of the batch
}

// ── 8. Custom interval is honoured ───────────────────────────────────

TEST(StaleValueGuard, custom_interval) {
    TempFileGuard g{make_temp_path()};
    osf::StreamingWriter w{g.path};
    ASSERT_TRUE(w.addChannel(
        make_ts_channel("a", osf::DataType::Double)).has_value());
    ASSERT_TRUE(w.start().has_value());

    osf::StaleValueGuard guard{w, /*repeatIntervalNs=*/5000};
    EXPECT_EQ(guard.repeatIntervalNs(), 5000);

    ASSERT_TRUE(guard.writeTimestampedSample<double>(0, 0, 1.0).has_value());
    auto before = guard.poll(4999);
    ASSERT_TRUE(before.has_value());
    EXPECT_EQ(*before, 0u);
    auto at = guard.poll(5000);
    ASSERT_TRUE(at.has_value());
    EXPECT_EQ(*at, 1u);

    ASSERT_TRUE(w.close().has_value());
}

// ── 9. poll with un-advanced now re-emits nothing ────────────────────

TEST(StaleValueGuard, poll_without_advancing_now) {
    TempFileGuard g{make_temp_path()};
    osf::StreamingWriter w{g.path};
    ASSERT_TRUE(w.addChannel(
        make_ts_channel("a", osf::DataType::Double)).has_value());
    ASSERT_TRUE(w.start().has_value());

    osf::StaleValueGuard guard{w};
    ASSERT_TRUE(guard.writeTimestampedSample<double>(0, 1000, 1.0).has_value());
    auto polled = guard.poll(1000);  // now == last activity
    ASSERT_TRUE(polled.has_value());
    EXPECT_EQ(*polled, 0u);

    ASSERT_TRUE(w.close().has_value());
}

// ── 10. A channel never written through the guard is not re-emitted ──

TEST(StaleValueGuard, untracked_channel_ignored) {
    TempFileGuard g{make_temp_path()};
    {
        osf::StreamingWriter w{g.path};
        ASSERT_TRUE(w.addChannel(
            make_ts_channel("a", osf::DataType::Double)).has_value());
        ASSERT_TRUE(w.addChannel(
            make_ts_channel("b", osf::DataType::Double)).has_value());
        ASSERT_TRUE(w.start().has_value());

        osf::StaleValueGuard guard{w};
        ASSERT_TRUE(guard.writeTimestampedSample<double>(
            0, 1000, 1.0).has_value());  // only channel 0
        EXPECT_TRUE(guard.isTracked(0));
        EXPECT_FALSE(guard.isTracked(1));

        auto polled = guard.poll(1000 + kInterval);
        ASSERT_TRUE(polled.has_value());
        EXPECT_EQ(*polled, 1u);  // only channel 0 re-emits

        ASSERT_TRUE(w.close().has_value());
    }
    auto mgr = osf::DataManager::loadFromFile(g.path);
    ASSERT_TRUE(mgr.has_value());
    // Channel "b" was declared but never written through the guard, so it
    // carries no samples (a numeric channel with no blocks finalizes as an
    // empty EquidistantChannel — the variant alternative is irrelevant here).
    auto const* b = mgr->channel("b");
    ASSERT_NE(b, nullptr);
    EXPECT_TRUE(osf::channelIsEmpty(*b));
}

// ── 11. isTracked / forget / clear ──────────────────────────────────

TEST(StaleValueGuard, is_tracked_forget_clear) {
    TempFileGuard g{make_temp_path()};
    osf::StreamingWriter w{g.path};
    ASSERT_TRUE(w.addChannel(
        make_ts_channel("a", osf::DataType::Double)).has_value());
    ASSERT_TRUE(w.addChannel(
        make_ts_channel("b", osf::DataType::Double)).has_value());
    ASSERT_TRUE(w.start().has_value());

    osf::StaleValueGuard guard{w};
    ASSERT_TRUE(guard.writeTimestampedSample<double>(0, 1000, 1.0).has_value());
    ASSERT_TRUE(guard.writeTimestampedSample<double>(1, 1000, 2.0).has_value());
    EXPECT_TRUE(guard.isTracked(0));
    EXPECT_TRUE(guard.isTracked(1));

    guard.forget(0);
    EXPECT_FALSE(guard.isTracked(0));
    auto after_forget = guard.poll(1000 + kInterval);
    ASSERT_TRUE(after_forget.has_value());
    EXPECT_EQ(*after_forget, 1u);  // only channel 1 remains

    guard.clear();
    EXPECT_FALSE(guard.isTracked(1));
    auto after_clear = guard.poll(1000 + 2 * kInterval);
    ASSERT_TRUE(after_clear.has_value());
    EXPECT_EQ(*after_clear, 0u);

    ASSERT_TRUE(w.close().has_value());
}

// ── 12. A writer error during re-emit propagates out of poll ─────────

TEST(StaleValueGuard, writer_error_propagates) {
    TempFileGuard g{make_temp_path()};
    osf::StreamingWriter w{g.path};
    ASSERT_TRUE(w.addChannel(
        make_ts_channel("a", osf::DataType::Double)).has_value());
    ASSERT_TRUE(w.start().has_value());

    osf::StaleValueGuard guard{w};
    ASSERT_TRUE(guard.writeTimestampedSample<double>(0, 1000, 1.0).has_value());

    ASSERT_TRUE(w.close().has_value());  // writer no longer accepts writes

    auto polled = guard.poll(1000 + kInterval);
    ASSERT_FALSE(polled.has_value());  // re-emit hit the closed writer
    EXPECT_EQ(polled.error().code, osf::Error::Code::InvalidArgument);
}
