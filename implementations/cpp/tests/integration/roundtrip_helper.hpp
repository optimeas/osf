// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

/// \file roundtrip_helper.hpp
/// Shared DataManager equality helper for integration round-trip tests.
///
/// Compares two `osf::DataManager` instances channel-by-channel:
///   - equal channel count
///   - per-channel: name, data_type, sample_count
///   - per-channel: first AND last materialised sample value (skipped for
///     empty channels)
///
/// Used by both the BlockWriter and StreamingWriter example round-trip suites.

#pragma once

#include <gtest/gtest.h>

#include <osf/data_channel.hpp>
#include <osf/manager.hpp>

#include <cstddef>
#include <sstream>
#include <string>
#include <variant>

namespace osf_test {

// ── Internal helpers ──────────────────────────────────────────────────

namespace detail {

/// Format a DataType as a short string for failure messages.
inline std::string dt_str(osf::DataType dt) {
    switch (dt) {
        case osf::DataType::Bool:        return "Bool";
        case osf::DataType::Int8:        return "Int8";
        case osf::DataType::Int16:       return "Int16";
        case osf::DataType::Int32:       return "Int32";
        case osf::DataType::Int64:       return "Int64";
        case osf::DataType::UInt8:       return "UInt8";
        case osf::DataType::UInt16:      return "UInt16";
        case osf::DataType::UInt32:      return "UInt32";
        case osf::DataType::UInt64:      return "UInt64";
        case osf::DataType::Float:       return "Float";
        case osf::DataType::Double:      return "Double";
        case osf::DataType::GpsLocation: return "GpsLocation";
        case osf::DataType::String:      return "String";
        case osf::DataType::Binary:      return "Binary";
        default:                         return "Unsupported";
    }
}

// ── Equidistant first/last value comparison ───────────────────────────

#define OSF_CMP_EQ_FLAT(SUFFIX)                                                \
    if (auto va = osf::as_##SUFFIX##_flat(a); va.has_value()) {               \
        auto vb = osf::as_##SUFFIX##_flat(b);                                  \
        if (!vb.has_value()) {                                                 \
            return ::testing::AssertionFailure()                               \
                << "channel '" << name                                         \
                << "': as_" #SUFFIX "_flat succeeded on A but failed on B: "  \
                << vb.error().message;                                         \
        }                                                                      \
        auto const& fa = *va;                                                  \
        auto const& fb = *vb;                                                  \
        if (fa.size() != fb.size()) {                                          \
            return ::testing::AssertionFailure()                               \
                << "channel '" << name << "': flat size mismatch: "           \
                << fa.size() << " vs " << fb.size();                          \
        }                                                                      \
        if (fa.empty()) return ::testing::AssertionSuccess();                  \
        if (fa.front() != fb.front()) {                                        \
            return ::testing::AssertionFailure()                               \
                << "channel '" << name << "': first value mismatch";          \
        }                                                                      \
        if (fa.back() != fb.back()) {                                          \
            return ::testing::AssertionFailure()                               \
                << "channel '" << name << "': last value mismatch";           \
        }                                                                      \
        return ::testing::AssertionSuccess();                                  \
    }

inline ::testing::AssertionResult compare_equidistant(
        std::string const& name,
        osf::EquidistantChannel const& a,
        osf::EquidistantChannel const& b) {
    OSF_CMP_EQ_FLAT(doubles)
    OSF_CMP_EQ_FLAT(floats)
    // Equidistant GPS is rare but theoretically valid.
    // as_gps_flat(EquidistantChannel) returns Result<vector<GpsLocation>>
    // (no timestamp pairs — equidistant channels use segment start_ts).
    if (auto va = osf::as_gps_flat(a); va.has_value()) {
        auto vb = osf::as_gps_flat(b);
        if (!vb.has_value()) {
            return ::testing::AssertionFailure()
                << "channel '" << name
                << "': as_gps_flat succeeded on A but failed on B: "
                << vb.error().message;
        }
        auto const& fa = *va;
        auto const& fb = *vb;
        if (fa.size() != fb.size())
            return ::testing::AssertionFailure()
                << "channel '" << name << "': GPS flat size mismatch: "
                << fa.size() << " vs " << fb.size();
        if (fa.empty()) return ::testing::AssertionSuccess();
        if (fa.front().latitude  != fb.front().latitude  ||
            fa.front().longitude != fb.front().longitude ||
            fa.front().altitude  != fb.front().altitude) {
            return ::testing::AssertionFailure()
                << "channel '" << name << "': first GPS value mismatch";
        }
        if (fa.back().latitude  != fb.back().latitude  ||
            fa.back().longitude != fb.back().longitude ||
            fa.back().altitude  != fb.back().altitude) {
            return ::testing::AssertionFailure()
                << "channel '" << name << "': last GPS value mismatch";
        }
        return ::testing::AssertionSuccess();
    }
    return ::testing::AssertionFailure()
        << "channel '" << name << "': unrecognised equidistant data type: "
        << dt_str(a.data_type);
}

#undef OSF_CMP_EQ_FLAT

// ── Timestamped first/last value comparison ───────────────────────────

/// Compare first/last of a `Result<vector<pair<int64_t, T>>>`.
#define OSF_CMP_TS_FLAT(SUFFIX, TYPE)                                          \
    if (auto va = osf::as_##SUFFIX##_flat(a); va.has_value()) {               \
        auto vb = osf::as_##SUFFIX##_flat(b);                                  \
        if (!vb.has_value()) {                                                 \
            return ::testing::AssertionFailure()                               \
                << "channel '" << name                                         \
                << "': as_" #SUFFIX "_flat succeeded on A but failed on B: "  \
                << vb.error().message;                                         \
        }                                                                      \
        auto const& fa = *va;                                                  \
        auto const& fb = *vb;                                                  \
        if (fa.size() != fb.size()) {                                          \
            return ::testing::AssertionFailure()                               \
                << "channel '" << name << "': flat size mismatch: "           \
                << fa.size() << " vs " << fb.size();                          \
        }                                                                      \
        if (fa.empty()) return ::testing::AssertionSuccess();                  \
        if (fa.front().first  != fb.front().first)                            \
            return ::testing::AssertionFailure()                               \
                << "channel '" << name << "': first timestamp mismatch: "     \
                << fa.front().first << " vs " << fb.front().first;            \
        if (fa.front().second != fb.front().second)                           \
            return ::testing::AssertionFailure()                               \
                << "channel '" << name << "': first value mismatch";          \
        if (fa.back().first  != fb.back().first)                              \
            return ::testing::AssertionFailure()                               \
                << "channel '" << name << "': last timestamp mismatch: "      \
                << fa.back().first << " vs " << fb.back().first;              \
        if (fa.back().second != fb.back().second)                             \
            return ::testing::AssertionFailure()                               \
                << "channel '" << name << "': last value mismatch";           \
        return ::testing::AssertionSuccess();                                  \
    }

inline ::testing::AssertionResult compare_timestamped(
        std::string const& name,
        osf::TimestampedChannel const& a,
        osf::TimestampedChannel const& b) {
    OSF_CMP_TS_FLAT(bools,   bool)
    OSF_CMP_TS_FLAT(int8,    std::int8_t)
    OSF_CMP_TS_FLAT(int16,   std::int16_t)
    OSF_CMP_TS_FLAT(int32,   std::int32_t)
    OSF_CMP_TS_FLAT(int64,   std::int64_t)
    OSF_CMP_TS_FLAT(uint8,   std::uint8_t)
    OSF_CMP_TS_FLAT(uint16,  std::uint16_t)
    OSF_CMP_TS_FLAT(uint32,  std::uint32_t)
    OSF_CMP_TS_FLAT(uint64,  std::uint64_t)
    OSF_CMP_TS_FLAT(floats,  float)
    OSF_CMP_TS_FLAT(doubles, double)
    // GPS timestamped
    if (auto va = osf::as_gps_flat(a); va.has_value()) {
        auto vb = osf::as_gps_flat(b);
        if (!vb.has_value()) {
            return ::testing::AssertionFailure()
                << "channel '" << name
                << "': as_gps_flat succeeded on A but failed on B: "
                << vb.error().message;
        }
        auto const& fa = *va;
        auto const& fb = *vb;
        if (fa.size() != fb.size()) {
            return ::testing::AssertionFailure()
                << "channel '" << name << "': GPS flat size mismatch: "
                << fa.size() << " vs " << fb.size();
        }
        if (fa.empty()) return ::testing::AssertionSuccess();
        if (fa.front().first != fb.front().first)
            return ::testing::AssertionFailure()
                << "channel '" << name << "': first GPS timestamp mismatch";
        auto const& ga = fa.front().second;
        auto const& gb = fb.front().second;
        if (ga.latitude != gb.latitude || ga.longitude != gb.longitude ||
            ga.altitude != gb.altitude)
            return ::testing::AssertionFailure()
                << "channel '" << name << "': first GPS value mismatch";
        if (fa.back().first != fb.back().first)
            return ::testing::AssertionFailure()
                << "channel '" << name << "': last GPS timestamp mismatch";
        auto const& la = fa.back().second;
        auto const& lb = fb.back().second;
        if (la.latitude != lb.latitude || la.longitude != lb.longitude ||
            la.altitude != lb.altitude)
            return ::testing::AssertionFailure()
                << "channel '" << name << "': last GPS value mismatch";
        return ::testing::AssertionSuccess();
    }
    return ::testing::AssertionFailure()
        << "channel '" << name << "': unrecognised timestamped data type: "
        << dt_str(a.data_type);
}

#undef OSF_CMP_TS_FLAT

// ── Variable first/last value comparison ─────────────────────────────

inline ::testing::AssertionResult compare_variable(
        std::string const& name,
        osf::VariableChannel const& a,
        osf::VariableChannel const& b) {
    if (a.data_type == osf::DataType::String) {
        auto sa = a.as_strings();
        auto sb = b.as_strings();
        if (!sa.has_value())
            return ::testing::AssertionFailure()
                << "channel '" << name
                << "': as_strings() failed on A: " << sa.error().message;
        if (!sb.has_value())
            return ::testing::AssertionFailure()
                << "channel '" << name
                << "': as_strings() failed on B: " << sb.error().message;
        auto const& va = **sa;
        auto const& vb = **sb;
        if (va.size() != vb.size())
            return ::testing::AssertionFailure()
                << "channel '" << name << "': string count mismatch: "
                << va.size() << " vs " << vb.size();
        if (va.empty()) return ::testing::AssertionSuccess();
        if (va.front() != vb.front())
            return ::testing::AssertionFailure()
                << "channel '" << name << "': first string value mismatch: '"
                << va.front() << "' vs '" << vb.front() << "'";
        if (va.back() != vb.back())
            return ::testing::AssertionFailure()
                << "channel '" << name << "': last string value mismatch: '"
                << va.back() << "' vs '" << vb.back() << "'";
        return ::testing::AssertionSuccess();
    }
    if (a.data_type == osf::DataType::Binary) {
        auto ba = a.as_binaries();
        auto bb = b.as_binaries();
        if (!ba.has_value())
            return ::testing::AssertionFailure()
                << "channel '" << name
                << "': as_binaries() failed on A: " << ba.error().message;
        if (!bb.has_value())
            return ::testing::AssertionFailure()
                << "channel '" << name
                << "': as_binaries() failed on B: " << bb.error().message;
        auto const& va = **ba;
        auto const& vb = **bb;
        if (va.size() != vb.size())
            return ::testing::AssertionFailure()
                << "channel '" << name << "': binary count mismatch: "
                << va.size() << " vs " << vb.size();
        if (va.empty()) return ::testing::AssertionSuccess();
        if (va.front() != vb.front())
            return ::testing::AssertionFailure()
                << "channel '" << name << "': first binary value mismatch";
        if (va.back() != vb.back())
            return ::testing::AssertionFailure()
                << "channel '" << name << "': last binary value mismatch";
        return ::testing::AssertionSuccess();
    }
    return ::testing::AssertionFailure()
        << "channel '" << name << "': unrecognised variable data type: "
        << dt_str(a.data_type);
}

}  // namespace detail

// ── Public API ────────────────────────────────────────────────────────

/// Compare two `DataManager` instances for round-trip equality:
///   - equal channel count
///   - per-channel: name + data_type + sample_count (identical)
///   - per-channel: first AND last materialised sample value (for non-empty
///     channels), including timestamp comparison for timestamped channels
///
/// Returns `AssertionSuccess()` when all checks pass; otherwise
/// `AssertionFailure()` with a message identifying the first deviation.
inline ::testing::AssertionResult roundtrip_managers_equal(
        osf::DataManager const& a, osf::DataManager const& b) {
    auto const& ach = a.channels();
    auto const& bch = b.channels();

    if (ach.size() != bch.size()) {
        return ::testing::AssertionFailure()
            << "channel count mismatch: " << ach.size()
            << " vs " << bch.size();
    }

    for (std::size_t i = 0; i < ach.size(); ++i) {
        auto const& ca = ach[i];
        std::string const& nm = osf::channel_name(ca);

        // Look up by name in B — matches the streaming-writer approach.
        auto const* cb_ptr = b.channel(nm);
        if (!cb_ptr) {
            return ::testing::AssertionFailure()
                << "channel '" << nm << "' (index " << i
                << ") missing in B";
        }
        auto const& cb = *cb_ptr;

        // Data type must match.
        if (osf::channel_data_type(ca) != osf::channel_data_type(cb)) {
            return ::testing::AssertionFailure()
                << "channel '" << nm << "': data_type mismatch: "
                << detail::dt_str(osf::channel_data_type(ca)) << " vs "
                << detail::dt_str(osf::channel_data_type(cb));
        }

        // Sample count must match.
        auto const cnt_a = osf::channel_sample_count(ca);
        auto const cnt_b = osf::channel_sample_count(cb);
        if (cnt_a != cnt_b) {
            return ::testing::AssertionFailure()
                << "channel '" << nm << "': sample count mismatch: "
                << cnt_a << " vs " << cnt_b;
        }

        // Skip value comparison for genuinely empty channels.
        if (cnt_a == 0) continue;

        // Dispatch on the DataChannel variant.
        if (auto const* ea = std::get_if<osf::EquidistantChannel>(&ca)) {
            auto const* eb = std::get_if<osf::EquidistantChannel>(&cb);
            if (!eb)
                return ::testing::AssertionFailure()
                    << "channel '" << nm
                    << "': variant mismatch (A is Equidistant, B is not)";
            auto r = detail::compare_equidistant(nm, *ea, *eb);
            if (!r) return r;
        } else if (auto const* ta = std::get_if<osf::TimestampedChannel>(&ca)) {
            auto const* tb = std::get_if<osf::TimestampedChannel>(&cb);
            if (!tb)
                return ::testing::AssertionFailure()
                    << "channel '" << nm
                    << "': variant mismatch (A is Timestamped, B is not)";
            auto r = detail::compare_timestamped(nm, *ta, *tb);
            if (!r) return r;
        } else if (auto const* va = std::get_if<osf::VariableChannel>(&ca)) {
            auto const* vb = std::get_if<osf::VariableChannel>(&cb);
            if (!vb)
                return ::testing::AssertionFailure()
                    << "channel '" << nm
                    << "': variant mismatch (A is Variable, B is not)";
            auto r = detail::compare_variable(nm, *va, *vb);
            if (!r) return r;
        } else {
            return ::testing::AssertionFailure()
                << "channel '" << nm
                << "': unknown DataChannel variant at index " << i;
        }
    }

    return ::testing::AssertionSuccess();
}

}  // namespace osf_test
