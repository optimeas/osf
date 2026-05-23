// SPDX-License-Identifier: MIT

#include <osf/stats.hpp>

#include <cstdio>
#include <ostream>
#include <string>

namespace osf {

void ChannelStats::observe_timestamp(std::int64_t ts) noexcept {
    if (!time_range_ns) {
        time_range_ns = std::make_pair(ts, ts);
        return;
    }
    auto& range = *time_range_ns;
    if (ts < range.first)  range.first = ts;
    if (ts > range.second) range.second = ts;
}

namespace {

constexpr std::uint64_t KB = 1024;
constexpr std::uint64_t MB = 1024 * KB;
constexpr std::uint64_t GB = 1024 * MB;

// Tiny snprintf-based formatter that does not pull <iomanip> /
// <sstream> into the public header. Buffer of 64 is more than enough
// for the two-digit-fraction formats we emit.
std::string fmt_double(char const* spec, double v) {
    char buf[64];
    int const n = std::snprintf(buf, sizeof(buf), spec, v);
    if (n < 0) return {};
    return std::string{buf, static_cast<std::size_t>(n)};
}

}  // anonymous namespace

std::string format_bytes(std::uint64_t bytes) {
    if (bytes >= GB) {
        return fmt_double("%.2f GB", static_cast<double>(bytes) /
                                       static_cast<double>(GB));
    }
    if (bytes >= MB) {
        return fmt_double("%.2f MB", static_cast<double>(bytes) /
                                       static_cast<double>(MB));
    }
    if (bytes >= KB) {
        return fmt_double("%.2f KB", static_cast<double>(bytes) /
                                       static_cast<double>(KB));
    }
    return std::to_string(bytes) + " B";
}

std::string format_duration(std::chrono::nanoseconds d) {
    double const secs =
        std::chrono::duration<double>(d).count();
    if (secs < 1.0) {
        return fmt_double("%.0f ms", secs * 1000.0);
    }
    return fmt_double("%.2f s", secs);
}

std::string_view compression_format_name(CompressionFormat f) noexcept {
    switch (f) {
        case CompressionFormat::None: return "none";
        case CompressionFormat::Zlib: return "zlib";
        case CompressionFormat::Gzip: return "gzip";
    }
    return "none";
}

std::ostream& operator<<(std::ostream& os, ReaderStats const& s) {
    os << "File size:       "
       << (s.file_size_bytes ? format_bytes(*s.file_size_bytes)
                             : std::string{"(streaming)"})
       << '\n';
    os << "Header:          " << format_bytes(s.header_size_bytes)    << '\n';
    os << "Metablock:       " << format_bytes(s.metablock_size_bytes) << '\n';
    os << "Data section:    " << format_bytes(s.data_section_size_bytes)
       << '\n';
    os << "Read in:         " << format_duration(s.elapsed) << '\n';
    os << '\n';
    os << "Channels total:        " << s.channels_total       << '\n';
    os << "With data:             " << s.channels_with_data   << '\n';
    os << "Unsupported:           " << s.channels_unsupported << '\n';
    os << '\n';
    os << "Blocks total:          " << s.blocks_total                       << '\n';
    os << "Read:                  " << s.blocks_read                        << '\n';
    os << "Skipped (unsupp.):     " << s.blocks_skipped_unsupported         << '\n';
    os << "Skipped (deprec.):     " << s.blocks_skipped_deprecated_type     << '\n';
    os << "Skipped (reserved):    " << s.blocks_skipped_reserved_type       << '\n';
    os << "Truncated:             " << s.blocks_truncated                   << '\n';
    if (s.trailer_seen) {
        os << "Trailer block:         present\n";
    }
    if (s.compressed) {
        os << "Compressed:            yes ("
           << compression_format_name(s.compression_format) << ")\n";
    }
    return os;
}

std::ostream& operator<<(std::ostream& os, ChannelStats const& s) {
    os << "blocks=" << s.blocks_read << "+" << s.blocks_skipped
       << "skipped samples=" << s.samples_total
       << " bytes=" << format_bytes(s.bytes_payload)
       << " segments=" << s.segments
       << " ts=";
    if (s.time_range_ns) {
        os << s.time_range_ns->first << ".." << s.time_range_ns->second;
    } else {
        os << '-';
    }
    return os;
}

}  // namespace osf
