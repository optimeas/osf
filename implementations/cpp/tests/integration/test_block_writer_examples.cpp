// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

#include "roundtrip_helper.hpp"

#include <gtest/gtest.h>

#include <osf/block_writer.hpp>
#include <osf/manager.hpp>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

class BlockWriterExamples : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        auto dir = std::filesystem::path{OSF_EXAMPLES_DIR};
        ASSERT_TRUE(std::filesystem::exists(dir))
            << "OSF_EXAMPLES_DIR does not exist: " << dir;
    }

    static std::filesystem::path examples_dir() {
        return std::filesystem::path{OSF_EXAMPLES_DIR};
    }
};

std::filesystem::path make_bw_temp_path() {
    static std::atomic<std::uint64_t> counter{0};
    auto const n = counter.fetch_add(1) + 1;
    return std::filesystem::temp_directory_path() /
           ("osf_block_writer_xref_" + std::to_string(n) + ".osf");
}

struct TempFileGuard {
    std::filesystem::path path;
    ~TempFileGuard() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
};

}  // namespace

// ── Category A — Round-trip every osf5_*.osf reference file ──────────

/// For each generated osf5_* file: load → write_to(ostream) via BlockWriter
/// → reload → compare channel count + per-channel name / data_type /
/// sample_count / first & last sample values.
TEST_F(BlockWriterExamples, every_osf5_reference_file_roundtrips_via_block_writer) {
    auto generated = examples_dir() / "generated";
    ASSERT_TRUE(std::filesystem::exists(generated));

    int tested = 0;
    for (auto const& entry : std::filesystem::directory_iterator{generated}) {
        if (entry.path().extension() != ".osf") continue;
        auto filename = entry.path().filename().string();
        if (filename.rfind("osf5_", 0) != 0) continue;   // only osf5_* files

        SCOPED_TRACE("file: " + filename);

        // Load.
        auto loaded = osf::DataManager::load_from_file(entry.path());
        ASSERT_TRUE(loaded.has_value())
            << "load failed for " << filename << ": " << loaded.error().message;

        // Write via BlockWriter (free function).
        std::ostringstream oss;
        oss.exceptions(std::ios::failbit | std::ios::badbit);
        auto wr = osf::write_to(*loaded, oss);
        ASSERT_TRUE(wr.has_value())
            << "write_to failed for " << filename << ": " << wr.error().message;

        // Reload from the in-memory bytes.
        std::istringstream iss(oss.str(), std::ios::binary);
        auto reloaded = osf::DataManager::load_from_stream(iss);
        ASSERT_TRUE(reloaded.has_value())
            << "reload failed for " << filename << ": " << reloaded.error().message;

        // Compare both managers.
        EXPECT_TRUE(osf_test::roundtrip_managers_equal(*loaded, *reloaded))
            << "file: " << entry.path();

        ++tested;
    }

    EXPECT_GT(tested, 0) << "no osf5_*.osf files found under " << generated;
}

// ── Category B — write_to_file byte-identical to write_to(ostream) ───

/// Pick any osf5_*.osf file, round-trip it via write_to_file() and
/// write_to(ostream) independently, then assert the two outputs are
/// byte-identical.
TEST_F(BlockWriterExamples, WriteToFileMatchesWriteToOstream) {
    // Use osf5_equidistant.osf as the fixed reference.
    auto path = examples_dir() / "generated" / "osf5_equidistant.osf";
    ASSERT_TRUE(std::filesystem::exists(path))
        << "missing reference file: " << path;

    auto loaded = osf::DataManager::load_from_file(path);
    ASSERT_TRUE(loaded.has_value()) << loaded.error().message;

    // Write to a temp file.
    TempFileGuard g{make_bw_temp_path()};
    {
        auto r = osf::write_to_file(*loaded, g.path);
        ASSERT_TRUE(r.has_value()) << r.error().message;
    }

    // Write to an ostream.
    std::ostringstream oss;
    oss.exceptions(std::ios::failbit | std::ios::badbit);
    {
        auto r = osf::write_to(*loaded, oss);
        ASSERT_TRUE(r.has_value()) << r.error().message;
    }
    std::string const ostream_bytes = oss.str();

    // Read the temp file into memory.
    auto const file_size = std::filesystem::file_size(g.path);
    std::vector<char> file_bytes(file_size);
    {
        std::ifstream fin(g.path, std::ios::binary);
        ASSERT_TRUE(fin.is_open()) << "cannot open temp file: " << g.path;
        fin.read(file_bytes.data(), static_cast<std::streamsize>(file_size));
        ASSERT_EQ(fin.gcount(), static_cast<std::streamsize>(file_size));
    }

    ASSERT_EQ(ostream_bytes.size(), file_size)
        << "byte length mismatch: ostream=" << ostream_bytes.size()
        << " file=" << file_size;

    // Byte-identical comparison.
    bool identical = (std::string(file_bytes.begin(), file_bytes.end()) ==
                      ostream_bytes);
    EXPECT_TRUE(identical)
        << "write_to_file output differs from write_to(ostream) output";
}
