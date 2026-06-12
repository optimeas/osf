// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH
//
// copy — round-trip an OSF / OSFZ file through the C++ library.
//
// Usage:
//   copy <in> <out>
//
// Loads <in> with DataManager::loadFromFile (transparent OSFZ),
// writes a fresh OSF5 file to <out> via osf::writeToFile,
// reloads <out> and confirms that channel counts match.

#include <osf/osf.h>

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>
#include <system_error>

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: copy <in> <out>\n";
        return 2;
    }

    std::string const in_path  = argv[1];
    std::string const out_path = argv[2];

    // ── Load source ───────────────────────────────────────────────────────
    auto load_res = osf::DataManager::loadFromFile(in_path);
    if (!load_res) {
        std::cerr << in_path << ": " << load_res.error().message << "\n";
        return 1;
    }
    osf::DataManager const& src = *load_res;
    std::size_t const src_channels = src.channels().size();

    // ── Write destination ─────────────────────────────────────────────────
    auto write_res = osf::writeToFile(src, out_path);
    if (!write_res) {
        std::cerr << out_path << ": " << write_res.error().message << "\n";
        return 1;
    }

    // ── Reload and verify ─────────────────────────────────────────────────
    auto reload_res = osf::DataManager::loadFromFile(out_path);
    if (!reload_res) {
        std::cerr << out_path << " (reload): " << reload_res.error().message << "\n";
        return 1;
    }
    osf::DataManager const& dst = *reload_res;
    std::size_t const dst_channels = dst.channels().size();

    std::error_code ec_in;
    std::error_code ec_out;
    std::uintmax_t const in_sz  = std::filesystem::file_size(in_path,  ec_in);
    std::uintmax_t const out_sz = std::filesystem::file_size(out_path, ec_out);

    std::cout << "in:       " << in_path
              << " (" << (ec_in ? std::uintmax_t{0} : in_sz) << " bytes, "
              << src_channels << " channels)\n";
    std::cout << "out:      " << out_path
              << " (" << (ec_out ? std::uintmax_t{0} : out_sz) << " bytes, "
              << dst_channels << " channels)\n";

    if (src_channels != dst_channels) {
        std::cerr << "MISMATCH: channel count in=" << src_channels
                  << " out=" << dst_channels << "\n";
        return 1;
    }

    std::cout << "ok: channel count matches (" << src_channels << ")\n";
    return 0;
}
