// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

#include <osf/stats.h>

#include <cstdio>
#include <ostream>
#include <string>

namespace osf {

void ChannelStats::observeTimestamp(std::int64_t ts) noexcept {
    if (!timeRangeNs) {
        timeRangeNs = std::make_pair(ts, ts);
        return;
    }
    auto& range = *timeRangeNs;
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
std::string fmtDouble(char const* spec, double v) {
    char buf[64];
    int const n = std::snprintf(buf, sizeof(buf), spec, v);
    if (n < 0) return {};
    return std::string{buf, static_cast<std::size_t>(n)};
}

}  // anonymous namespace

std::string formatBytes(std::uint64_t bytes) {
    if (bytes >= GB) {
        return fmtDouble("%.2f GB", static_cast<double>(bytes) /
                                       static_cast<double>(GB));
    }
    if (bytes >= MB) {
        return fmtDouble("%.2f MB", static_cast<double>(bytes) /
                                       static_cast<double>(MB));
    }
    if (bytes >= KB) {
        return fmtDouble("%.2f KB", static_cast<double>(bytes) /
                                       static_cast<double>(KB));
    }
    return std::to_string(bytes) + " B";
}

std::string formatDuration(std::chrono::nanoseconds d) {
    double const secs =
        std::chrono::duration<double>(d).count();
    if (secs < 1.0) {
        return fmtDouble("%.0f ms", secs * 1000.0);
    }
    return fmtDouble("%.2f s", secs);
}

std::string_view compressionFormatName(CompressionFormat f) noexcept {
    switch (f) {
        case CompressionFormat::None: return "none";
        case CompressionFormat::Zlib: return "zlib";
        case CompressionFormat::Gzip: return "gzip";
    }
    return "none";
}

std::ostream& operator<<(std::ostream& os, ReaderStats const& s) {
    os << "File size:       "
       << (s.fileSizeBytes ? formatBytes(*s.fileSizeBytes)
                             : std::string{"(streaming)"})
       << '\n';
    os << "Header:          " << formatBytes(s.headerSizeBytes)    << '\n';
    os << "Metablock:       " << formatBytes(s.metablockSizeBytes) << '\n';
    os << "Data section:    " << formatBytes(s.dataSectionSizeBytes)
       << '\n';
    os << "Read in:         " << formatDuration(s.elapsed) << '\n';
    os << '\n';
    os << "Channels total:        " << s.channelsTotal       << '\n';
    os << "With data:             " << s.channelsWithData   << '\n';
    os << "Unsupported:           " << s.channelsUnsupported << '\n';
    os << '\n';
    os << "Blocks total:          " << s.blocksTotal                       << '\n';
    os << "Read:                  " << s.blocksRead                        << '\n';
    os << "Skipped (unsupp.):     " << s.blocksSkippedUnsupported         << '\n';
    os << "Skipped (deprec.):     " << s.blocksSkippedDeprecatedType     << '\n';
    os << "Skipped (status ev.):  " << s.blocksSkippedStatusEvent        << '\n';
    os << "Skipped (reserved):    " << s.blocksSkippedReservedType       << '\n';
    os << "Skipped (zero-len):    " << s.blocksSkippedZeroLength         << '\n';
    os << "Truncated:             " << s.blocksTruncated                   << '\n';
    if (s.trailerSeen) {
        os << "Trailer block:         present\n";
    }
    if (s.compressed) {
        os << "Compressed:            yes ("
           << compressionFormatName(s.compressionFormat) << ")\n";
    }
    if (s.integrity != IntegrityProfile::None) {
        os << "Integrity:             " << integrityProfileName(s.integrity) << '\n';
        os << "Blocks CRC-failed:     " << s.blocksCrcFailed << '\n';
        if (s.blocksSignatureSkipped > 0) {
            os << "Signature blocks:      " << s.blocksSignatureSkipped
               << " (skipped, unverified)\n";
        }
    }
    return os;
}

std::ostream& operator<<(std::ostream& os, ChannelStats const& s) {
    os << "blocks=" << s.blocksRead << "+" << s.blocksSkipped
       << "skipped samples=" << s.samplesTotal
       << " bytes=" << formatBytes(s.bytesPayload)
       << " segments=" << s.segments
       << " ts=";
    if (s.timeRangeNs) {
        os << s.timeRangeNs->first << ".." << s.timeRangeNs->second;
    } else {
        os << '-';
    }
    return os;
}

}  // namespace osf
