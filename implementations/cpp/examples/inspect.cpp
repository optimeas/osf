// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH
//
// inspect — print a summary of an OSF / OSFZ file.
//
// Usage:
//   inspect <file>
//
// Prints file-level metadata (version, creator, created_utc, compression)
// and a one-line summary per channel. Mirrors the column layout of the
// Rust `inspect` example (implementations/rust/osf-core/examples/inspect.rs).

#include <osf/osf.hpp>

#include <algorithm>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <string>

namespace {

// ── Helpers ───────────────────────────────────────────────────────────────

std::string data_type_name(osf::DataType dt) {
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

std::string channel_type_name(osf::ChannelType ct) {
    switch (ct) {
        case osf::ChannelType::Scalar:      return "scalar";
        case osf::ChannelType::Equidistant: return "equidistant";
        case osf::ChannelType::Timestamped: return "timestamped";
        default:                            return "?unsupported";
    }
}

// Truncate a UTF-8 string to at most max_chars characters (by byte for
// simplicity; only the channel-name column is affected).
std::string truncate(std::string const& s, std::size_t max_chars) {
    if (s.size() <= max_chars) return s;
    return s.substr(0, max_chars - 1) + "~";
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: inspect <file>\n";
        return 2;
    }

    std::string const path = argv[1];

    auto result = osf::DataManager::load_from_file(path);
    if (!result) {
        std::cerr << path << ": " << result.error().message << "\n";
        return 1;
    }

    osf::DataManager const& mgr = *result;
    osf::ReaderStats  const& st  = mgr.stats;
    osf::FileInfo     const& fi  = mgr.meta.file_info;

    // ── File-level summary ──────────────────────────────────────────────
    std::cout << "path:           " << path << "\n";

    // Compression
    std::string compressed_label;
    if (!st.compressed) {
        compressed_label = "no";
    } else {
        switch (st.compression_format) {
            case osf::CompressionFormat::Gzip: compressed_label = "yes (gzip)"; break;
            case osf::CompressionFormat::Zlib: compressed_label = "yes (zlib)"; break;
            default:                           compressed_label = "yes";         break;
        }
    }
    std::cout << "compressed:     " << compressed_label << "\n";
    std::cout << "version:        " << fi.version << "\n";
    std::cout << "creator:        " << fi.creator.value_or("-") << "\n";
    std::cout << "created_utc:    " << fi.created_utc.value_or("-") << "\n";

    std::size_t const nchan = mgr.channels().size();
    std::cout << "channels:       " << nchan << "\n";

    // ── Per-channel lines ───────────────────────────────────────────────
    if (nchan > 0) {
        // Determine column width for the name field (capped at 48).
        std::size_t max_name = 0;
        for (osf::DataChannel const& dc : mgr.channels()) {
            max_name = std::max(max_name, osf::channel_name(dc).size());
        }
        max_name = std::min(max_name, std::size_t{48});

        for (osf::DataChannel const& dc : mgr.channels()) {
            std::uint16_t const idx   = osf::channel_index(dc);
            std::string   const name  = truncate(osf::channel_name(dc), max_name);
            std::string   const ct    = channel_type_name(osf::channel_meta(dc).channel_type);
            std::string   const dt    = data_type_name(osf::channel_data_type(dc));
            std::string   const unit  = osf::channel_physical_unit(dc).value_or("-");

            std::cout << "   ["
                      << std::setw(3) << std::right << idx
                      << "] "
                      << std::setw(static_cast<int>(max_name)) << std::left << name
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
