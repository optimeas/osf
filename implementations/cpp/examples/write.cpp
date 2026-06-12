// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH
//
// write — create a small OSF5 file from generated data.
//
// Usage:
//   write <out.osf>
//
// Creates two channels:
//   1. "signals/sine"   — equidistant float, 100 Hz, 50 samples (a sine ramp)
//   2. "events/counter" — timestamped double, 5 samples spaced 1 second apart
//
// Prints the file size on success.

#include <osf/osf.h>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>
#include <system_error>
#include <vector>

namespace {
    constexpr double kPi = 3.14159265358979323846;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: write <out.osf>\n";
        return 2;
    }

    std::string const outPath = argv[1];

    // ── Build the writer ──────────────────────────────────────────────────
    osf::BlockWriter writer;
    writer.setCreator("osf-cpp example write");

    // Channel 1: equidistant float — "signals/sine"
    osf::ChannelDef sineDef;
    sineDef.name          = "signals/sine";
    sineDef.dataType     = osf::DataType::Float;
    sineDef.channelType  = osf::ChannelType::Scalar;
    sineDef.physicalUnit = "V";
    sineDef.displayName  = "Sine signal";

    auto sineIdxRes = writer.addChannel(sineDef);
    if (!sineIdxRes) {
        std::cerr << "addChannel (sine): " << sineIdxRes.error().message << "\n";
        return 1;
    }
    std::uint16_t const sineIdx = *sineIdxRes;

    // Channel 2: timestamped double — "events/counter"
    osf::ChannelDef ctrDef;
    ctrDef.name          = "events/counter";
    ctrDef.dataType     = osf::DataType::Double;
    ctrDef.channelType  = osf::ChannelType::Scalar;
    ctrDef.physicalUnit = "count";
    ctrDef.displayName  = "Event counter";

    auto ctrIdxRes = writer.addChannel(ctrDef);
    if (!ctrIdxRes) {
        std::cerr << "addChannel (counter): " << ctrIdxRes.error().message << "\n";
        return 1;
    }
    std::uint16_t const ctrIdx = *ctrIdxRes;

    // ── Generate data ─────────────────────────────────────────────────────

    // Equidistant sine: 50 samples at 100 Hz starting at t = 0 ns.
    constexpr int    nSine    = 50;
    constexpr double rateHz   = 100.0;
    constexpr std::int64_t t0Ns = 0;

    std::vector<float> sineSamples(nSine);
    for (int i = 0; i < nSine; ++i) {
        sineSamples[static_cast<std::size_t>(i)] =
            static_cast<float>(std::sin(2.0 * kPi * static_cast<double>(i) / nSine));
    }

    auto r1 = writer.addEquidistantSegment(
        sineIdx, t0Ns, rateHz,
        sineSamples.data(), sineSamples.size());
    if (!r1) {
        std::cerr << "addEquidistantSegment: " << r1.error().message << "\n";
        return 1;
    }

    // Timestamped counter: 5 samples, each 1 second apart.
    constexpr int           nCtr        = 5;
    constexpr std::int64_t  nsPerSec   = 1'000'000'000LL;

    for (int i = 0; i < nCtr; ++i) {
        std::int64_t const ts = static_cast<std::int64_t>(i) * nsPerSec;
        double       const v  = static_cast<double>(i + 1) * 10.0;

        auto r2 = writer.addTimestampedSample<double>(ctrIdx, ts, v);
        if (!r2) {
            std::cerr << "addTimestampedSample: " << r2.error().message << "\n";
            return 1;
        }
    }

    // ── Emit ──────────────────────────────────────────────────────────────
    auto r3 = writer.writeToFile(outPath);
    if (!r3) {
        std::cerr << outPath << ": " << r3.error().message << "\n";
        return 1;
    }

    std::error_code ec;
    std::uintmax_t const sz = std::filesystem::file_size(outPath, ec);
    if (ec) {
        std::cout << "written: " << outPath << " (size unknown)\n";
    } else {
        std::cout << "written: " << outPath
                  << " (" << sz << " bytes, "
                  << nSine << " equidistant + " << nCtr << " timestamped samples)\n";
    }
    return 0;
}
