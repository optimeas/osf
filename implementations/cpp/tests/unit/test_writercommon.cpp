// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

#include "writercommon_p.h"

#include "osf/metablock.h"
#include "osf/streamingwriter.h"   // ChannelDef
#include "osf/version.h"

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

// DECISIONS §13: created_utc is stamped automatically; unset creator /
// tag fall back to their defaults; explicit values win.
TEST(WriterCommon, BuildMetablockAppliesDecisions13Defaults) {
    osf::detail::FileInfoDraft fi;   // everything unset
    std::vector<osf::ChannelDef> channels{
        ch("c", osf::DataType::Double, osf::ChannelType::Timestamped),
    };

    osf::MetaBlock meta = osf::detail::build_metablock(fi, channels);

    // created_utc: always present, ISO-8601 "YYYY-MM-DDTHH:MM:SSZ".
    ASSERT_TRUE(meta.file_info.created_utc.has_value());
    std::string const& ts = *meta.file_info.created_utc;
    ASSERT_EQ(ts.size(), 20u);
    EXPECT_EQ(ts[4], '-');
    EXPECT_EQ(ts[7], '-');
    EXPECT_EQ(ts[10], 'T');
    EXPECT_EQ(ts[13], ':');
    EXPECT_EQ(ts[16], ':');
    EXPECT_EQ(ts[19], 'Z');

    // creator default: "osf-cpp/<library version>".
    ASSERT_TRUE(meta.file_info.creator.has_value());
    EXPECT_EQ(*meta.file_info.creator,
              "osf-cpp/" + std::string{osf::version()});

    // tag default: "default".
    ASSERT_TRUE(meta.file_info.tag.has_value());
    EXPECT_EQ(*meta.file_info.tag, "default");

    // reason stays omitted when unset (not defaulted, not null).
    EXPECT_FALSE(meta.file_info.reason.has_value());
}

TEST(WriterCommon, BuildMetablockExplicitCreatorTagWin) {
    osf::detail::FileInfoDraft fi;
    fi.creator = "my-app/2.0";
    fi.tag     = "calibration";
    std::vector<osf::ChannelDef> channels{
        ch("c", osf::DataType::Double, osf::ChannelType::Timestamped),
    };

    osf::MetaBlock meta = osf::detail::build_metablock(fi, channels);

    EXPECT_EQ(*meta.file_info.creator, "my-app/2.0");
    EXPECT_EQ(*meta.file_info.tag, "calibration");
}

}  // namespace
