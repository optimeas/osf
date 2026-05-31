// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

#include "osf/block_writer.hpp"
#include "osf/data_channel.hpp"
#include "osf/manager.hpp"

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

TEST(BlockWriter, EquidistantDoubleRoundtripsThroughOstream) {
    osf::BlockWriter w;
    w.set_creator("test:1");
    auto idx = w.add_channel([] {
        osf::ChannelDef d;
        d.name = "eq";
        d.data_type = osf::DataType::Double;
        d.channel_type = osf::ChannelType::Equidistant;
        d.size_of_length_value = 2;
        return d;
    }());
    ASSERT_TRUE(idx.has_value());

    std::vector<double> samples{1.0, 2.0, 3.0, 4.0, 5.0};
    ASSERT_TRUE(w.add_equidistant_segment(*idx, /*start_ns=*/1000,
        /*rate_hz=*/100.0, samples.data(), samples.size()).has_value());

    std::ostringstream out;
    ASSERT_TRUE(w.write_to(out).has_value());

    std::string bytes = out.str();
    std::istringstream in(bytes, std::ios::binary);
    auto mgr = osf::DataManager::load_from_stream(in);
    ASSERT_TRUE(mgr.has_value());
    auto const* ch = mgr->channel("eq");
    ASSERT_NE(ch, nullptr);
    auto const* eq = std::get_if<osf::EquidistantChannel>(ch);
    ASSERT_NE(eq, nullptr);
    auto flat = osf::as_doubles_flat(*eq);
    ASSERT_TRUE(flat.has_value());
    ASSERT_EQ(flat->size(), 5u);
    EXPECT_DOUBLE_EQ((*flat)[0], 1.0);
    EXPECT_DOUBLE_EQ((*flat)[4], 5.0);
}

TEST(BlockWriter, AddEquidistantRejectsNonPositiveRate) {
    osf::BlockWriter w;
    auto idx = w.add_channel([] {
        osf::ChannelDef d; d.name = "eq"; d.data_type = osf::DataType::Double;
        d.channel_type = osf::ChannelType::Equidistant; d.size_of_length_value = 2;
        return d;
    }());
    double s = 1.0;
    auto r = w.add_equidistant_segment(*idx, 0, 0.0, &s, 1);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, osf::Error::Code::InvalidArgument);
}

}  // namespace
