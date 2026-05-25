// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

#include "block_encode.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

namespace {

using osf::detail::BinarySample;

TEST(BlockEncodeSmoke, BinarySampleFromPointer) {
    std::uint8_t buf[] = {0xDE, 0xAD, 0xBE, 0xEF};
    BinarySample s{buf, 4};
    EXPECT_EQ(s.data, buf);
    EXPECT_EQ(s.size, std::size_t{4});
}

TEST(BlockEncodeSmoke, BinarySampleFromVector) {
    std::vector<std::uint8_t> v = {0x01, 0x02, 0x03};
    auto s = BinarySample::from_vector(v);
    EXPECT_EQ(s.data, v.data());
    EXPECT_EQ(s.size, std::size_t{3});
}

}  // namespace
