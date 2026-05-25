// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

/// \file manager.hpp
/// High-level OSF reader: assembles typed in-memory channels from the
/// block stream.
///
/// `DataManager` is the second API tier on top of `BlockReader`. Where
/// the reader yields per-block raw views, the manager groups blocks by
/// channel and produces `osf::Channel` values that hide the on-disk
/// block boundaries: equidistant samples flat with their segments,
/// timestamped samples with parallel timestamp / value vectors,
/// string / binary samples as `(timestamp, value)` pairs.
///
/// Channel-by-name lookup is the documented entry point per
/// DECISIONS §10; lookup by index is provided as an optional
/// convenience.

#pragma once

#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <osf/data_channel.hpp>
#include <osf/error.hpp>
#include <osf/metablock.hpp>
#include <osf/stats.hpp>

namespace osf {

/// High-level read-only view of an OSF file: parsed metablock,
/// `ReaderStats`, and the typed channel list.
class DataManager {
public:
    /// Parsed metablock; kept so applications can read file-level
    /// metadata (creator, created_utc, infos, …) without re-opening
    /// the file.
    MetaBlock meta;
    /// Telemetry from the underlying `BlockReader` — file/section
    /// sizes, elapsed time, per-channel sample counts and timing.
    ReaderStats stats;

    /// Open `path`, parse the magic header and metablock, drive a
    /// `BlockReader` to completion, and assemble the typed channel
    /// list.
    ///
    /// OSFZ-compressed input is detected by leading-byte magic (gzip
    /// `0x1F 0x8B` or zlib `0x78 …`) and currently rejected with an
    /// error pointing to Phase 8; transparent decompression lands
    /// there.
    [[nodiscard]] static Result<DataManager> load_from_file(
        std::filesystem::path const& path);

    /// Construct from any `std::istream` positioned at the start of
    /// the OSF file. The stream must outlive the parse — the
    /// constructor reads it to EOF.
    [[nodiscard]] static Result<DataManager> load_from_stream(
        std::istream& stream);

    /// Read-only view of all channels in metablock order.
    [[nodiscard]] std::vector<DataChannel> const& channels() const noexcept {
        return channels_;
    }

    /// Look up a channel by its fully qualified name.
    ///
    /// **Mandatory access form** per DECISIONS §10. Returns `nullptr`
    /// when no channel with that name exists.
    [[nodiscard]] DataChannel const* channel(std::string_view name) const;

    /// Look up a channel by its on-disk index (the integer `index`
    /// attribute from the metablock). Returns `nullptr` when no
    /// channel has that index — optional access form per
    /// DECISIONS §10.
    [[nodiscard]] DataChannel const* channel_by_index(std::uint16_t index) const;

private:
    std::vector<DataChannel> channels_;
    std::unordered_map<std::string, std::size_t> by_name_;
    std::unordered_map<std::uint16_t, std::size_t> by_index_;

    DataManager() = default;

    friend Result<DataManager> build_from_stream_impl(std::istream& stream,
                                                     std::uint64_t file_size,
                                                     bool have_file_size);
};

}  // namespace osf
