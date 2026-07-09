// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH
//
// inspect — print a summary of an OSF / OSFZ file.
//
// Usage:
//   inspect <file>
//
// Prints file-level metadata (version, creator, createdUtc, compression)
// and a one-line summary per channel.

#include <osf/osf.h>

#include <algorithm>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <string>

namespace {

// ── Helpers ───────────────────────────────────────────────────────────────

std::string dataTypeName(osf::DataType dt) {
    switch (dt) {
        case osf::DataType::Bool:        return "bool";
        case osf::DataType::Int8:        return "int8";
        case osf::DataType::Int16:       return "int16";
        case osf::DataType::Int32:       return "int32";
        case osf::DataType::Int64:       return "int64";
        case osf::DataType::UInt8:       return "uint8";
        case osf::DataType::UInt16:      return "uint16";
        case osf::DataType::UInt32:      return "uint32";
        case osf::DataType::UInt64:      return "uint64";
        case osf::DataType::Float:       return "float";
        case osf::DataType::Double:      return "double";
        case osf::DataType::String:      return "string";
        case osf::DataType::Binary:      return "binary";
        case osf::DataType::ByteArray:   return "binary";
        case osf::DataType::GpsLocation: return "gpsloc";
        default:                         return "?unsupported";
    }
}

std::string channelTypeName(osf::ChannelType ct) {
    switch (ct) {
        case osf::ChannelType::Scalar: return "scalar";
        case osf::ChannelType::Vector: return "vector";
        case osf::ChannelType::Matrix: return "matrix";
        case osf::ChannelType::Binary: return "binary";
        default:                       return "?unsupported";
    }
}

// Truncate a UTF-8 string to at most maxChars characters (by byte for
// simplicity; only the channel-name column is affected).
std::string truncate(std::string const& s, std::size_t maxChars) {
    if (s.size() <= maxChars) return s;
    return s.substr(0, maxChars - 1) + "~";
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: inspect <file>\n";
        return 2;
    }

    std::string const path = argv[1];

    auto result = osf::DataManager::loadFromFile(path);
    if (!result) {
        std::cerr << path << ": " << result.error().message << "\n";
        return 1;
    }

    osf::DataManager const& mgr = *result;
    osf::ReaderStats  const& st  = mgr.stats;
    osf::FileInfo     const& fi  = mgr.meta.fileInfo;

    // ── File-level summary ──────────────────────────────────────────────
    std::cout << "path:           " << path << "\n";

    // Compression
    std::string compressedLabel;
    if (!st.compressed) {
        compressedLabel = "no";
    } else {
        switch (st.compressionFormat) {
            case osf::CompressionFormat::Gzip: compressedLabel = "yes (gzip)"; break;
            case osf::CompressionFormat::Zlib: compressedLabel = "yes (zlib)"; break;
            default:                           compressedLabel = "yes";         break;
        }
    }
    std::cout << "compressed:     " << compressedLabel << "\n";
    std::cout << "version:        " << fi.version << "\n";
    std::cout << "creator:        " << fi.creator.value_or("-") << "\n";
    std::cout << "created_utc:    " << fi.createdUtc.value_or("-") << "\n";

    std::size_t const nchan = mgr.channels().size();
    std::cout << "channels:       " << nchan << "\n";

    // ── Per-channel lines ───────────────────────────────────────────────
    if (nchan > 0) {
        // Determine column width for the name field (capped at 48).
        std::size_t maxName = 0;
        for (osf::DataChannel const& dc : mgr.channels()) {
            maxName = std::max(maxName, osf::channelName(dc).size());
        }
        maxName = std::min(maxName, std::size_t{48});

        for (osf::DataChannel const& dc : mgr.channels()) {
            std::uint16_t const idx   = osf::channelIndex(dc);
            std::string   const name  = truncate(osf::channelName(dc), maxName);
            std::string   const ct    = channelTypeName(osf::channelMeta(dc).channelType);
            std::string   const dt    = dataTypeName(osf::channelDataType(dc));
            std::string   const unit  = osf::channelPhysicalUnit(dc).value_or("-");

            std::cout << "   ["
                      << std::setw(3) << std::right << idx
                      << "] "
                      << std::setw(static_cast<int>(maxName)) << std::left << name
                      << "  "
                      << std::setw(11) << std::left << ct
                      << "  "
                      << std::setw(8) << std::left << dt
                      << "  unit=" << unit
                      << "\n";
        }
    }

    std::cout << "infos:          " << mgr.meta.infos.size() << "\n";
    return 0;
}
