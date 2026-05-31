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

#include <cstddef>
#include <cstdint>

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

}  // namespace osf::detail

#endif  // OSF_DETAIL_WRITER_COMMON_HPP
