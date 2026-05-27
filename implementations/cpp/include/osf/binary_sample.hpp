// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

/**
 * @file binary_sample.hpp
 * @brief Non-owning view over a binary payload, C++17 substitute for
 *        std::span<std::uint8_t const>.
 *
 * Promoted from osf::detail to osf:: in Phase 7b — the StreamingWriter
 * public API takes BinarySample as a parameter of write_timestamped_binary.
 * Phase 7a had this type in src/block_encode.hpp because the encoder was
 * the only consumer; with a public-API call site it belongs in include/osf/.
 *
 * Explicit construction only; no implicit conversion from
 * std::vector<std::uint8_t> to prevent the lifetime trap where a temporary
 * vector would die at statement end.
 */

#ifndef OSF_BINARY_SAMPLE_HPP
#define OSF_BINARY_SAMPLE_HPP

#include <cstddef>
#include <cstdint>
#include <vector>

namespace osf {

struct BinarySample {
    std::uint8_t const* data;
    std::size_t         size;

    constexpr BinarySample(std::uint8_t const* d, std::size_t s) noexcept
        : data{d}, size{s} {}

    /// Ergonomic factory for the common case of an owning vector.
    static BinarySample from_vector(std::vector<std::uint8_t> const& v) noexcept {
        return BinarySample{v.data(), v.size()};
    }
};

}  // namespace osf

#endif  // OSF_BINARY_SAMPLE_HPP
