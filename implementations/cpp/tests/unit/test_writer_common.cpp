// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

#include "writer_common.hpp"

#include "osf/metablock.hpp"
#include "osf/streaming_writer.hpp"   // ChannelDef

#include <gtest/gtest.h>

namespace {

osf::ChannelDef ch(std::string name, osf::DataType dt, osf::ChannelType ct) {
    osf::ChannelDef d;
    d.name = std::move(name);
    d.data_type = dt;
    d.channel_type = ct;
    d.size_of_length_value = 2;
    return d;
}

TEST(WriterCommon, BuildMetablockNormalisesNonEquidistantToScalar) {
    osf::detail::FileInfoDraft fi;
    fi.creator = "test:1";
    std::vector<osf::ChannelDef> channels{
        ch("eq", osf::DataType::Double, osf::ChannelType::Equidistant),
        ch("ts", osf::DataType::Int32, osf::ChannelType::Timestamped),
        ch("var", osf::DataType::String, osf::ChannelType::Scalar),
    };

    osf::MetaBlock meta = osf::detail::build_metablock(fi, channels);

    ASSERT_EQ(meta.channels.size(), 3u);
    EXPECT_EQ(meta.channels[0].channel_type, osf::ChannelType::Equidistant);
    EXPECT_EQ(meta.channels[1].channel_type, osf::ChannelType::Scalar);  // was Timestamped
    EXPECT_EQ(meta.channels[2].channel_type, osf::ChannelType::Scalar);
    EXPECT_EQ(meta.file_info.version, 5u);
    EXPECT_EQ(meta.file_info.creator, "test:1");
    EXPECT_EQ(meta.channels[0].index, 0u);
    EXPECT_EQ(meta.channels[1].index, 1u);
}

}  // namespace
