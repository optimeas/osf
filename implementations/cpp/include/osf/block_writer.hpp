// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

/**
 * @file block_writer.hpp
 * @brief OSF5 analyst-style (block) writer.
 *
 * BlockWriter accumulates all samples in memory and emits the complete
 * OSF5 file at write_to_file() / write_to() time. Contrast StreamingWriter,
 * which fsyncs each block to disk and is suited for embedded / power-loss-safe
 * recording.
 *
 * Two writer classes serve the OSF5 write surface (DECISIONS §7):
 *   - StreamingWriter — embedded; per-block flush via OS fsync.
 *   - BlockWriter (this class) — analyst-style; accumulates samples in
 *     memory, emits the complete file at close.
 *
 * Thread safety: BlockWriter is not thread-safe. All methods must be
 * called from a single thread, or the caller must serialize externally.
 */

#pragma once

#include "osf/binary_sample.hpp"
#include "osf/block.hpp"
#include "osf/error.hpp"
#include "osf/streaming_writer.hpp"
#include "osf/types.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace osf {

// Forward declaration — from_manager is a later task.
class DataManager;

// ── BlockWriter ───────────────────────────────────────────────────────

class BlockWriter {
public:
    // Six special members declared here, defined out-of-line in the .cpp.
    // Required because channel_data_ holds std::vector<ChannelData> and
    // ChannelData is only forward-declared in this header (incomplete type).
    // MSVC will not instantiate vector's special members against an incomplete
    // type, so = default in the header is illegal.
    BlockWriter();
    ~BlockWriter();
    BlockWriter(BlockWriter const&);
    BlockWriter& operator=(BlockWriter const&);
    BlockWriter(BlockWriter&&) noexcept;
    BlockWriter& operator=(BlockWriter&&) noexcept;

    // ── Configuration ─────────────────────────────────────────────────

    void set_creator(std::string value);
    void set_tag(std::string value);
    void set_reason(std::string value);
    void set_namespace_sep(std::string value);
    void set_comment(std::string value);
    void set_location(double latitude, double longitude, double altitude);

    // ── Channel registration ───────────────────────────────────────────

    [[nodiscard]] Result<std::uint16_t> add_channel(ChannelDef def);

    [[nodiscard]] std::size_t channel_count() const noexcept;

    [[nodiscard]] std::optional<std::uint16_t>
    channel_index(std::string_view name) const;

    // ── Equidistant accumulation ───────────────────────────────────────

    [[nodiscard]] Result<void> add_equidistant_segment(
        std::uint16_t channel, std::int64_t start_ts_ns, double rate_hz,
        float const* samples, std::size_t count);
    [[nodiscard]] Result<void> add_equidistant_segment(
        std::uint16_t channel, std::int64_t start_ts_ns, double rate_hz,
        double const* samples, std::size_t count);

    // ── Emit ───────────────────────────────────────────────────────────

    [[nodiscard]] Result<void> write_to_file(std::filesystem::path path) const;
    [[nodiscard]] Result<void> write_to(std::ostream& out) const;

private:
    struct ChannelData;   // fully defined in block_writer.cpp

    // Header-local mirror of detail::FileInfoDraft — avoids pulling the
    // private writer_common.hpp into this public header.
    struct FileInfoFields {
        std::optional<std::string> creator;
        std::optional<std::string> tag;
        std::optional<std::string> reason;
        std::optional<std::string> namespace_sep;
        std::optional<std::string> comment;
        std::optional<double> created_at_latitude;
        std::optional<double> created_at_longitude;
        std::optional<double> created_at_altitude;
    };

    template <typename T>
    [[nodiscard]] Result<void> add_equidistant_segment_impl(
        std::uint16_t channel, std::int64_t start_ts_ns, double rate_hz,
        T const* samples, std::size_t count);

    void autobump_size_of_length_value(std::vector<ChannelDef>& defs) const;

    [[nodiscard]] Result<void> emit_channel(std::ostream& out,
        std::vector<std::uint8_t>& buf, std::uint16_t ci, std::uint8_t sov,
        ChannelData const& cd) const;

    [[nodiscard]] Result<void> write_block_bytes(std::ostream& out,
        std::vector<std::uint8_t> const& buf) const;

    FileInfoFields                                    file_info_;
    std::vector<ChannelDef>                           channels_;
    std::vector<ChannelData>                          channel_data_;
    std::unordered_map<std::string, std::uint16_t>    name_to_index_;
};

}  // namespace osf
