// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

#include "crc32c_p.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <string_view>

using osf::detail::Crc32c;
using osf::detail::crc32c;

namespace {

std::uint32_t crc(std::string_view s) {
    return crc32c(s.data(), s.size());
}

} // namespace

TEST(Crc32c, CanonicalCheckValue) {
    // CRC-32/ISCSI (Castagnoli) check value for "123456789".
    EXPECT_EQ(crc("123456789"), 0xE306'9283u);
}

TEST(Crc32c, EmptyIsZero) {
    EXPECT_EQ(crc32c(nullptr, 0), 0u);
}

TEST(Crc32c, Rfc3720ThirtyTwoZeroBytes) {
    std::array<std::uint8_t, 32> zeros{};
    EXPECT_EQ(crc32c(zeros.data(), zeros.size()), 0x8A91'36AAu);
}

TEST(Crc32c, Rfc3720ThirtyTwoOneBytes) {
    std::array<std::uint8_t, 32> ones{};
    ones.fill(0xFF);
    EXPECT_EQ(crc32c(ones.data(), ones.size()), 0x62A8'AB43u);
}

TEST(Crc32c, IncrementalMatchesOneShot) {
    std::string_view const s = "123456789";
    Crc32c c;
    c.update(s.data(), 4);
    c.update(s.data() + 4, s.size() - 4);
    EXPECT_EQ(c.value(), crc(s));
}

TEST(Crc32c, ChunkBoundaryAtEight) {
    // Exercise the slicing-by-8 fast path plus the byte tail across a chunk
    // boundary: a single-shot 20-byte CRC must equal a split at byte 8.
    std::array<std::uint8_t, 20> data{};
    for (std::size_t i = 0; i < data.size(); ++i) {
        data[i] = static_cast<std::uint8_t>(i * 7 + 1);
    }
    Crc32c split;
    split.update(data.data(), 8);
    split.update(data.data() + 8, data.size() - 8);
    EXPECT_EQ(split.value(), crc32c(data.data(), data.size()));
}
