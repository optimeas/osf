// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

#pragma once

/// \file
/// Internal CRC32C (Castagnoli, CRC-32/ISCSI) used by the OSF5 integrity
/// profile for the metablock checksum and the per-block frame CRC.
///
/// Table-based (slicing-by-8), dependency-free. The check value for the ASCII
/// string "123456789" is 0xE3069283, matching the other OSF implementations
/// byte for byte.

#include <cstddef>
#include <cstdint>

namespace osf::detail {

/// Incremental CRC32C accumulator.
class Crc32c {
public:
    /// Fold \p len bytes at \p data into the running CRC.
    void update(void const* data, std::size_t len) noexcept;

    /// Finalise and return the CRC32C value (does not modify the accumulator).
    [[nodiscard]] std::uint32_t value() const noexcept;

private:
    std::uint32_t m_crc = 0xFFFFFFFFu;
};

/// One-shot CRC32C over \p len bytes at \p data.
[[nodiscard]] std::uint32_t crc32c(void const* data, std::size_t len) noexcept;

} // namespace osf::detail
