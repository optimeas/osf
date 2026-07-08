// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

#include "crc32c_p.h"

namespace osf::detail {
namespace {

// Reversed CRC-32/ISCSI (Castagnoli) polynomial.
constexpr std::uint32_t kPoly = 0x82F63B78u;

// Slicing-by-8 lookup tables, built once on first use.
struct Tables {
    std::uint32_t t[8][256];

    Tables() noexcept {
        for (std::uint32_t n = 0; n < 256; ++n) {
            std::uint32_t c = n;
            for (int k = 0; k < 8; ++k) {
                c = (c & 1u) != 0u ? (kPoly ^ (c >> 1)) : (c >> 1);
            }
            t[0][n] = c;
        }
        for (std::uint32_t n = 0; n < 256; ++n) {
            std::uint32_t c = t[0][n];
            for (std::size_t k = 1; k < 8; ++k) {
                c = t[0][c & 0xFFu] ^ (c >> 8);
                t[k][n] = c;
            }
        }
    }
};

Tables const& tables() noexcept {
    static Tables const kTables;
    return kTables;
}

// Read four bytes little-endian into a uint32_t (cast-safe under -Wconversion).
inline std::uint32_t rd32(std::uint8_t const* p) noexcept {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

} // namespace

void Crc32c::update(void const* data, std::size_t len) noexcept {
    auto const& t = tables().t;
    auto const* p = static_cast<std::uint8_t const*>(data);
    std::uint32_t crc = m_crc;

    while (len >= 8) {
        crc ^= rd32(p);
        std::uint32_t const hi = rd32(p + 4);
        crc = t[7][crc & 0xFFu] ^ t[6][(crc >> 8) & 0xFFu] ^ t[5][(crc >> 16) & 0xFFu] ^
              t[4][(crc >> 24) & 0xFFu] ^ t[3][hi & 0xFFu] ^ t[2][(hi >> 8) & 0xFFu] ^
              t[1][(hi >> 16) & 0xFFu] ^ t[0][(hi >> 24) & 0xFFu];
        p += 8;
        len -= 8;
    }
    while (len-- != 0) {
        crc = t[0][(crc ^ *p++) & 0xFFu] ^ (crc >> 8);
    }
    m_crc = crc;
}

std::uint32_t Crc32c::value() const noexcept {
    return m_crc ^ 0xFFFFFFFFu;
}

std::uint32_t crc32c(void const* data, std::size_t len) noexcept {
    Crc32c c;
    c.update(data, len);
    return c.value();
}

} // namespace osf::detail
