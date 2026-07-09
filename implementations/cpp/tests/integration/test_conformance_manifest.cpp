// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

// Manifest-driven cross-implementation conformance test. Reads the shared
// examples/reference_manifest.json — the single source of truth for the
// expected decoded contents of every reference file — and asserts that
// DataManager decodes each listed file to match: version (from the filename
// prefix), channel count, per-channel index/name/dataType/sampleCount/mode,
// plus the integrity profile for entries that declare one.
//
// Manifest keys may be sub-paths (e.g. integrity/osf5_crc_equidistant.osf);
// they resolve under examples/generated/. Sharing this list with the
// Java/Rust/Delphi conformance tests is what makes it a real cross-language
// contract — the file list lives only in the manifest.

#include <gtest/gtest.h>

#include <osf/datachannel.h>
#include <osf/manager.h>
#include <osf/types.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <variant>

namespace {

std::filesystem::path examplesDir() { return std::filesystem::path{OSF_EXAMPLES_DIR}; }

std::string modeOf(osf::DataChannel const& ch) {
    if (std::holds_alternative<osf::EquidistantChannel>(ch)) return "equidistant";
    if (std::holds_alternative<osf::TimestampedChannel>(ch)) return "timestamped";
    return "variable";
}

osf::IntegrityProfile integrityOf(std::string const& token) {
    if (token == "crc32c") return osf::IntegrityProfile::Crc32c;
    if (token == "ed25519") return osf::IntegrityProfile::Ed25519;
    return osf::IntegrityProfile::None;
}

}  // namespace

TEST(CppConformanceManifest, conformsToReferenceManifest) {
    auto const root = examplesDir();
    std::ifstream in(root / "reference_manifest.json");
    ASSERT_TRUE(in.good()) << "cannot open reference_manifest.json";
    nlohmann::json const manifest = nlohmann::json::parse(in);
    ASSERT_FALSE(manifest.empty());

    for (auto const& [key, entry] : manifest.items()) {
        auto const path = root / "generated" / key;
        auto mgr = osf::DataManager::loadFromFile(path);
        ASSERT_TRUE(mgr.has_value()) << key << ": " << mgr.error().message;

        std::string const base = std::filesystem::path{key}.filename().string();
        int const wantVersion = base.rfind("osf4_", 0) == 0 ? 4 : 5;
        EXPECT_EQ(entry.at("version").get<int>(), wantVersion) << key;

        if (entry.contains("integrity")) {
            EXPECT_EQ(mgr->stats.integrity,
                      integrityOf(entry.at("integrity").get<std::string>())) << key;
            EXPECT_EQ(mgr->stats.blocksCrcFailed, 0u) << key;
        }

        auto const& channels = mgr->channels();
        auto const& chEntries = entry.at("channels");
        EXPECT_EQ(channels.size(), chEntries.size()) << key;

        for (auto const& ce : chEntries) {
            auto const idx = ce.at("index").get<std::uint16_t>();
            auto it = std::find_if(channels.begin(), channels.end(),
                [&](osf::DataChannel const& c) { return osf::channelIndex(c) == idx; });
            ASSERT_NE(it, channels.end()) << key << " index " << idx;
            EXPECT_EQ(osf::channelName(*it), ce.at("name").get<std::string>()) << key;
            auto const wantDt = osf::parseDataType(ce.at("dataType").get<std::string>());
            ASSERT_TRUE(wantDt.has_value()) << key << ": bad manifest dataType";
            EXPECT_EQ(osf::channelDataType(*it), *wantDt) << key;
            EXPECT_EQ(osf::channelSampleCount(*it),
                      ce.at("sampleCount").get<std::size_t>()) << key;
            EXPECT_EQ(modeOf(*it), ce.at("mode").get<std::string>()) << key;
        }
    }
}
