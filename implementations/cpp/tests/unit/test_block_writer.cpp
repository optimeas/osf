// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

#include "osf/block_writer.hpp"

#include <optional>
#include <sstream>

#include <gtest/gtest.h>

namespace {

osf::ChannelDef dbl(std::string name) {
    osf::ChannelDef d;
    d.name = std::move(name);
    d.data_type = osf::DataType::Double;
    d.channel_type = osf::ChannelType::Scalar;
    d.size_of_length_value = 2;
    return d;
}

TEST(BlockWriter, AddChannelReturnsIndexInOrder) {
    osf::BlockWriter w;
    auto i0 = w.add_channel(dbl("a"));
    auto i1 = w.add_channel(dbl("b"));
    ASSERT_TRUE(i0.has_value());
    ASSERT_TRUE(i1.has_value());
    EXPECT_EQ(*i0, 0u);
    EXPECT_EQ(*i1, 1u);
    EXPECT_EQ(w.channel_count(), 2u);
    EXPECT_EQ(w.channel_index("a"), std::optional<std::uint16_t>{0u});
    EXPECT_EQ(w.channel_index("missing"), std::nullopt);
}

TEST(BlockWriter, AddChannelRejectsBadSizeOfLengthValue) {
    osf::BlockWriter w;
    auto d = dbl("a");
    d.size_of_length_value = 3;
    auto r = w.add_channel(d);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, osf::Error::Code::InvalidArgument);
}

TEST(BlockWriter, AddChannelRejectsUnsupportedDataType) {
    osf::BlockWriter w;
    auto d = dbl("a");
    d.data_type = osf::DataType::Unsupported;
    auto r = w.add_channel(d);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, osf::Error::Code::InvalidArgument);
}

TEST(BlockWriter, EmptyBuilderWriteReturnsInvalidArgument) {
    osf::BlockWriter w;
    std::ostringstream out;
    auto r = w.write_to(out);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, osf::Error::Code::InvalidArgument);
}

}  // namespace
