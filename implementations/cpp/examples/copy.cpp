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

    std::string const inPath  = argv[1];
    std::string const outPath = argv[2];

    // ── Load source ───────────────────────────────────────────────────────
    auto loadRes = osf::DataManager::loadFromFile(inPath);
    if (!loadRes) {
        std::cerr << inPath << ": " << loadRes.error().message << "\n";
        return 1;
    }
    osf::DataManager const& src = *loadRes;
    std::size_t const srcChannels = src.channels().size();

    // ── Write destination ─────────────────────────────────────────────────
    auto writeRes = osf::writeToFile(src, outPath);
    if (!writeRes) {
        std::cerr << outPath << ": " << writeRes.error().message << "\n";
        return 1;
    }

    // ── Reload and verify ─────────────────────────────────────────────────
    auto reloadRes = osf::DataManager::loadFromFile(outPath);
    if (!reloadRes) {
        std::cerr << outPath << " (reload): " << reloadRes.error().message << "\n";
        return 1;
    }
    osf::DataManager const& dst = *reloadRes;
    std::size_t const dstChannels = dst.channels().size();

    std::error_code ecIn;
    std::error_code ecOut;
    std::uintmax_t const inSz  = std::filesystem::file_size(inPath,  ecIn);
    std::uintmax_t const outSz = std::filesystem::file_size(outPath, ecOut);

    std::cout << "in:       " << inPath
              << " (" << (ecIn ? std::uintmax_t{0} : inSz) << " bytes, "
              << srcChannels << " channels)\n";
    std::cout << "out:      " << outPath
              << " (" << (ecOut ? std::uintmax_t{0} : outSz) << " bytes, "
              << dstChannels << " channels)\n";

    if (srcChannels != dstChannels) {
        std::cerr << "MISMATCH: channel count in=" << srcChannels
                  << " out=" << dstChannels << "\n";
        return 1;
    }

    std::cout << "ok: channel count matches (" << srcChannels << ")\n";
    return 0;
}
