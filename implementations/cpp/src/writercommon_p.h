// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

/**
 * @file writercommon_p.h
 * @brief Private writer infrastructure shared by StreamingWriter (7b)
 *        and BlockWriter (7c): block-payload chunking math and sizing
 *        constants. C++-specific — the Rust reference inlines this into
 *        writer.rs.
 */

#ifndef OSF_DETAIL_WRITER_COMMON_HPP
#define OSF_DETAIL_WRITER_COMMON_HPP

#include "osf/metablock.h"
#include "osf/streamingwriter.h"   // ChannelDef

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace osf::detail {

// GPS wire-format size per sample: 3 little-endian doubles
// (latitude, longitude, altitude) — see block.h.
constexpr std::size_t GPS_WIRE_SIZE = 24;

// Single-sample variable (string/binary) bcAbsTimeStampData overhead:
// 1 control byte + 8 timestamp bytes (bit-7 = 0, no u32 N-prefix,
// OSF5 no trailing 0x00).
constexpr std::size_t VARIABLE_BLOCK_OVERHEAD_BYTES = 9;

// Maximum payload bytes (control byte + body) for a block whose length
// prefix is `sov` bytes wide. u16 -> 65535; u32 -> soft-capped at
// i32::MAX - 1024 to avoid platform-dependent overflow on body-length
// conversion.
constexpr std::size_t maxPayloadForSov(std::uint8_t sov) noexcept {
    return (sov == 2)
               ? std::size_t{0xFFFFu}
               : static_cast<std::size_t>(0x7FFFFFFFu - 1024u);
}

// bcStartData multi-sample: payload = [u8 ctrl][i64 ts][f64 rate]
// [u32 N][N * valueSize]. Overhead = 21.
std::size_t maxSamplesPerStartBlock(std::size_t valueSize,
                                        std::uint8_t sov) noexcept;

// bcContinuedData multi-sample: payload = [u8 ctrl][u32 N]
// [N * valueSize]. Overhead = 5.
std::size_t maxSamplesPerContinuedBlock(std::size_t valueSize,
                                            std::uint8_t sov) noexcept;

// bcAbsTimeStampData multi-sample: payload = [u8 ctrl][u32 N]
// [N * (8 + valueSize)]. Overhead = 5.
std::size_t maxSamplesPerTimestampedBlock(std::size_t valueSize,
                                              std::uint8_t sov) noexcept;

// Effective max sample size for a single-sample variable block.
std::size_t variableSampleCapacity(std::uint8_t sov) noexcept;

// Writer-controllable file-info fields. `createdUtc` and `version`
// are not carried here — buildMetablock stamps them at assembly time
// (version = 5; createdUtc = current UTC, DECISIONS §13). Unset
// `creator` / `tag` receive their §13 defaults at assembly time, too.
// Must stay field-compatible with BlockWriter::FileInfoFields (blockwriter.h).
struct FileInfoDraft {
    std::optional<std::string> creator;
    std::optional<std::string> tag;
    std::optional<std::string> reason;
    std::optional<double>      createdAtLatitude;
    std::optional<double>      createdAtLongitude;
    std::optional<double>      createdAtAltitude;
    std::optional<std::string> namespaceSep;
    std::optional<std::string> comment;
};

// Assemble an OSF5 (version 5) MetaBlock from writer state. Channel
// indices are assigned sequentially 0..N. channeltype is normalised to
// the Delphi reference convention: `equidistant` for equidistant
// channels, `scalar` for every other kind (timestamped numeric, GPS,
// string, binary). DECISIONS §13 metadata defaults are applied here:
// createdUtc is stamped with the current UTC time, unset creator
// falls back to "osf-cpp/<version>", unset tag to "default".
// Caller serialises via serializeMetablockJson.
MetaBlock buildMetablock(FileInfoDraft const& fi,
                          std::vector<ChannelDef> const& channels);

}  // namespace osf::detail

#endif  // OSF_DETAIL_WRITER_COMMON_HPP
