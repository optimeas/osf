// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

/**
 * @file block_encode.hpp
 * @brief Private OSF block-encoder primitives shared by BlockWriter and
 *        StreamingWriter.
 *
 * This module is C++-specific. The Rust reference embeds encode logic
 * directly into writer.rs; the C++ split into a private encoder layer
 * serves the two-writer-classes architecture documented in
 * DECISIONS.md §7 (StreamingWriter + BlockWriter). Both writers compose
 * the same stateless encoder functions.
 *
 * All functions append to an out-parameter byte buffer and return
 * Result<void>. Three error conditions are reported:
 *   - InvalidBlock     payload too large for sizeoflengthvalue (Writer
 *                      reacts with auto-bump 2 → 4)
 *   - InvalidArgument  count == 0 (numeric / GPS) — caller violated API
 *   - InvalidArgument  sizeoflengthvalue not in {2, 4} — defensive
 *
 * Spec rev 2026-05-24 — encoder always emits OSF5 conformance:
 *   - Bit 7 = 0 when count == 1 (no uint32 N-prefix; 4 bytes saved)
 *   - Bit 7 = 1 when count > 1 (uint32 N-prefix)
 *   - String/binary payloads in bcAbsTimeStampData: NO trailing 0x00
 *   - Multi-sample variable-length blocks not produced (single-sample
 *     signature; callers split N>1 sequences themselves)
 */

#ifndef OSF_DETAIL_BLOCK_ENCODE_HPP
#define OSF_DETAIL_BLOCK_ENCODE_HPP

#include "osf/binary_sample.hpp"
#include "osf/block.hpp"      // GpsLocation
#include "osf/error.hpp"      // Result<void>

#include <cstdint>
#include <cstddef>
#include <string_view>
#include <vector>

namespace osf::detail {

using osf::BinarySample;

// ── Equidistant numeric (float / double only per spec) ─────────────

template <typename T>
Result<void> encode_start_data(std::vector<std::uint8_t>& out,
                               std::uint16_t channel_index,
                               std::uint8_t sizeoflengthvalue,
                               std::int64_t start_timestamp_ns,
                               double sample_rate_hz,
                               T const* samples,
                               std::size_t count);

template <typename T>
Result<void> encode_continued_data(std::vector<std::uint8_t>& out,
                                   std::uint16_t channel_index,
                                   std::uint8_t sizeoflengthvalue,
                                   T const* samples,
                                   std::size_t count);

// ── Timestamped numeric ────────────────────────────────────────────

template <typename T>
Result<void> encode_abs_timestamp_data(std::vector<std::uint8_t>& out,
                                       std::uint16_t channel_index,
                                       std::uint8_t sizeoflengthvalue,
                                       std::int64_t const* timestamps_ns,
                                       T const* samples,
                                       std::size_t count);

// ── Timestamped GPS ────────────────────────────────────────────────

Result<void> encode_abs_timestamp_data_gps(std::vector<std::uint8_t>& out,
                                          std::uint16_t channel_index,
                                          std::uint8_t sizeoflengthvalue,
                                          std::int64_t const* timestamps_ns,
                                          GpsLocation const* samples,
                                          std::size_t count);

// ── Timestamped variable-length (single-sample only per spec) ──────

Result<void> encode_abs_timestamp_data(std::vector<std::uint8_t>& out,
                                       std::uint16_t channel_index,
                                       std::uint8_t sizeoflengthvalue,
                                       std::int64_t timestamp_ns,
                                       std::string_view sample);

Result<void> encode_abs_timestamp_data(std::vector<std::uint8_t>& out,
                                       std::uint16_t channel_index,
                                       std::uint8_t sizeoflengthvalue,
                                       std::int64_t timestamp_ns,
                                       BinarySample sample);

}  // namespace osf::detail

#endif  // OSF_DETAIL_BLOCK_ENCODE_HPP
