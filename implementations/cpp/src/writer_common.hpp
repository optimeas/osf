// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

/**
 * @file writer_common.hpp
 * @brief Private writer infrastructure shared by StreamingWriter (7b)
 *        and BlockWriter (7c): block-payload chunking math and sizing
 *        constants. C++-specific — the Rust reference inlines this into
 *        writer.rs.
 */

#ifndef OSF_DETAIL_WRITER_COMMON_HPP
#define OSF_DETAIL_WRITER_COMMON_HPP

#include "osf/metablock.hpp"
#include "osf/streaming_writer.hpp"   // ChannelDef

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace osf::detail {

// GPS wire-format size per sample: 3 little-endian doubles
// (latitude, longitude, altitude) — see block.hpp.
constexpr std::size_t GPS_WIRE_SIZE = 24;

// Single-sample variable (string/binary) bcAbsTimeStampData overhead:
// 1 control byte + 8 timestamp bytes (bit-7 = 0, no u32 N-prefix,
// OSF5 no trailing 0x00).
constexpr std::size_t VARIABLE_BLOCK_OVERHEAD_BYTES = 9;

// Maximum payload bytes (control byte + body) for a block whose length
// prefix is `sov` bytes wide. u16 -> 65535; u32 -> soft-capped at
// i32::MAX - 1024 to avoid platform-dependent overflow on body-length
// conversion.
constexpr std::size_t max_payload_for_sov(std::uint8_t sov) noexcept {
    return (sov == 2)
               ? std::size_t{0xFFFFu}
               : static_cast<std::size_t>(0x7FFFFFFFu - 1024u);
}

// bcStartData multi-sample: payload = [u8 ctrl][i64 ts][f64 rate]
// [u32 N][N * value_size]. Overhead = 21.
std::size_t max_samples_per_start_block(std::size_t value_size,
                                        std::uint8_t sov) noexcept;

// bcContinuedData multi-sample: payload = [u8 ctrl][u32 N]
// [N * value_size]. Overhead = 5.
std::size_t max_samples_per_continued_block(std::size_t value_size,
                                            std::uint8_t sov) noexcept;

// bcAbsTimeStampData multi-sample: payload = [u8 ctrl][u32 N]
// [N * (8 + value_size)]. Overhead = 5.
std::size_t max_samples_per_timestamped_block(std::size_t value_size,
                                              std::uint8_t sov) noexcept;

// Effective max sample size for a single-sample variable block.
std::size_t variable_sample_capacity(std::uint8_t sov) noexcept;

// Writer-controllable file-info fields. `created_utc` and `version`
// are not carried here — build_metablock stamps them at assembly time
// (version = 5; created_utc = current UTC, DECISIONS §13). Unset
// `creator` / `tag` receive their §13 defaults at assembly time, too.
// Must stay field-compatible with BlockWriter::FileInfoFields (block_writer.hpp).
struct FileInfoDraft {
    std::optional<std::string> creator;
    std::optional<std::string> tag;
    std::optional<std::string> reason;
    std::optional<double>      created_at_latitude;
    std::optional<double>      created_at_longitude;
    std::optional<double>      created_at_altitude;
    std::optional<std::string> namespace_sep;
    std::optional<std::string> comment;
};

// Assemble an OSF5 (version 5) MetaBlock from writer state. Channel
// indices are assigned sequentially 0..N. channeltype is normalised to
// the Delphi reference convention: `equidistant` for equidistant
// channels, `scalar` for every other kind (timestamped numeric, GPS,
// string, binary). DECISIONS §13 metadata defaults are applied here:
// created_utc is stamped with the current UTC time, unset creator
// falls back to "osf-cpp/<version>", unset tag to "default".
// Caller serialises via serialize_metablock_json.
MetaBlock build_metablock(FileInfoDraft const& fi,
                          std::vector<ChannelDef> const& channels);

}  // namespace osf::detail

#endif  // OSF_DETAIL_WRITER_COMMON_HPP
