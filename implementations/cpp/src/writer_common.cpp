// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

#include "writer_common.hpp"

namespace osf::detail {

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

}  // namespace osf::detail
