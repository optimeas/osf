// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

#include "writer_common.hpp"

#include "osf/types.hpp"
#include "osf/version.hpp"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <string>

namespace osf::detail {

namespace {

// Current UTC time as ISO-8601 `YYYY-MM-DDTHH:MM:SSZ`, matching the
// Rust writer's format_utc_now (sub-second precision intentionally
// dropped — same rationale: the spec never needed it; extend
// additively if a future revision does).
std::string now_utc_iso8601() {
    auto const now = std::chrono::system_clock::now();
    std::time_t const t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    // 80 bytes covers GCC's -Wformat-truncation worst case (six full-range
    // ints + separators); the real output is always 20 chars + NUL.
    char buf[80];
    std::snprintf(buf, sizeof buf, "%04d-%02d-%02dT%02d:%02d:%02dZ",
                  tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                  tm.tm_hour, tm.tm_min, tm.tm_sec);
    return std::string{buf};
}

}  // namespace

std::size_t max_samples_per_start_block(std::size_t value_size,
                                        std::uint8_t sov) noexcept {
    constexpr std::size_t OVERHEAD = 1u + 8u + 8u + 4u;
    std::size_t const max_payload = max_payload_for_sov(sov);
    if (max_payload <= OVERHEAD) return 1;
    std::size_t const samples = (max_payload - OVERHEAD) / value_size;
    return (samples == 0) ? 1u : samples;
}

std::size_t max_samples_per_continued_block(std::size_t value_size,
                                            std::uint8_t sov) noexcept {
    constexpr std::size_t OVERHEAD = 1u + 4u;
    std::size_t const max_payload = max_payload_for_sov(sov);
    if (max_payload <= OVERHEAD) return 1;
    std::size_t const samples = (max_payload - OVERHEAD) / value_size;
    return (samples == 0) ? 1u : samples;
}

std::size_t max_samples_per_timestamped_block(std::size_t value_size,
                                              std::uint8_t sov) noexcept {
    constexpr std::size_t OVERHEAD = 1u + 4u;
    std::size_t const per_sample = 8u + value_size;
    std::size_t const max_payload = max_payload_for_sov(sov);
    if (max_payload <= OVERHEAD) return 1;
    std::size_t const samples = (max_payload - OVERHEAD) / per_sample;
    return (samples == 0) ? 1u : samples;
}

std::size_t variable_sample_capacity(std::uint8_t sov) noexcept {
    std::size_t const max_payload = max_payload_for_sov(sov);
    return (max_payload <= VARIABLE_BLOCK_OVERHEAD_BYTES)
               ? 0u
               : (max_payload - VARIABLE_BLOCK_OVERHEAD_BYTES);
}

MetaBlock build_metablock(FileInfoDraft const& fi,
                          std::vector<ChannelDef> const& channels) {
    MetaBlock meta;
    meta.file_info.version              = 5;
    // DECISIONS §13 metadata defaults (parity with the Rust writer):
    // created_utc is always stamped at assembly time; creator falls
    // back to "osf-cpp/<library-version>"; tag falls back to
    // "default". reason and the created_at_* triple stay omitted when
    // unset (not written as null).
    meta.file_info.created_utc          = now_utc_iso8601();
    meta.file_info.creator              = fi.creator.has_value()
        ? fi.creator
        : std::optional<std::string>{"osf-cpp/" + std::string{version()}};
    meta.file_info.tag                  = fi.tag.has_value()
        ? fi.tag
        : std::optional<std::string>{"default"};
    meta.file_info.reason              = fi.reason;
    meta.file_info.created_at_latitude  = fi.created_at_latitude;
    meta.file_info.created_at_longitude = fi.created_at_longitude;
    meta.file_info.created_at_altitude  = fi.created_at_altitude;
    meta.file_info.namespace_sep       = fi.namespace_sep;
    meta.file_info.comment             = fi.comment;

    for (std::size_t i = 0; i < channels.size(); ++i) {
        ChannelDef const& d = channels[i];
        Channel chx;
        chx.index = static_cast<std::uint16_t>(i);
        chx.name  = d.name;
        chx.data_type = d.data_type;
        // Normalise: equidistant stays equidistant; everything else is
        // scalar (Delphi reference convention; BACKLOG Task-7 #2).
        chx.channel_type = (d.channel_type == ChannelType::Equidistant)
                               ? ChannelType::Equidistant
                               : ChannelType::Scalar;
        chx.size_of_length_value = d.size_of_length_value;
        chx.time_increment_ns = d.time_increment_ns;
        chx.physical_unit = d.physical_unit;
        chx.physical_dimension = d.physical_dimension;
        chx.display_name = d.display_name;
        chx.mime_type = d.mime_type;
        chx.reference = d.reference;
        chx.comment = d.comment;
        meta.channels.push_back(std::move(chx));
    }
    return meta;
}

}  // namespace osf::detail
