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
    constexpr double k_pi = 3.14159265358979323846;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: write <out.osf>\n";
        return 2;
    }

    std::string const out_path = argv[1];

    // ── Build the writer ──────────────────────────────────────────────────
    osf::BlockWriter writer;
    writer.set_creator("osf-cpp example write");

    // Channel 1: equidistant float — "signals/sine"
    osf::ChannelDef sine_def;
    sine_def.name          = "signals/sine";
    sine_def.data_type     = osf::DataType::Float;
    sine_def.channel_type  = osf::ChannelType::Scalar;
    sine_def.physical_unit = "V";
    sine_def.display_name  = "Sine signal";

    auto sine_idx_res = writer.add_channel(sine_def);
    if (!sine_idx_res) {
        std::cerr << "add_channel (sine): " << sine_idx_res.error().message << "\n";
        return 1;
    }
    std::uint16_t const sine_idx = *sine_idx_res;

    // Channel 2: timestamped double — "events/counter"
    osf::ChannelDef ctr_def;
    ctr_def.name          = "events/counter";
    ctr_def.data_type     = osf::DataType::Double;
    ctr_def.channel_type  = osf::ChannelType::Scalar;
    ctr_def.physical_unit = "count";
    ctr_def.display_name  = "Event counter";

    auto ctr_idx_res = writer.add_channel(ctr_def);
    if (!ctr_idx_res) {
        std::cerr << "add_channel (counter): " << ctr_idx_res.error().message << "\n";
        return 1;
    }
    std::uint16_t const ctr_idx = *ctr_idx_res;

    // ── Generate data ─────────────────────────────────────────────────────

    // Equidistant sine: 50 samples at 100 Hz starting at t = 0 ns.
    constexpr int    n_sine    = 50;
    constexpr double rate_hz   = 100.0;
    constexpr std::int64_t t0_ns = 0;

    std::vector<float> sine_samples(n_sine);
    for (int i = 0; i < n_sine; ++i) {
        sine_samples[static_cast<std::size_t>(i)] =
            static_cast<float>(std::sin(2.0 * k_pi * static_cast<double>(i) / n_sine));
    }

    auto r1 = writer.add_equidistant_segment(
        sine_idx, t0_ns, rate_hz,
        sine_samples.data(), sine_samples.size());
    if (!r1) {
        std::cerr << "add_equidistant_segment: " << r1.error().message << "\n";
        return 1;
    }

    // Timestamped counter: 5 samples, each 1 second apart.
    constexpr int           n_ctr        = 5;
    constexpr std::int64_t  ns_per_sec   = 1'000'000'000LL;

    for (int i = 0; i < n_ctr; ++i) {
        std::int64_t const ts = static_cast<std::int64_t>(i) * ns_per_sec;
        double       const v  = static_cast<double>(i + 1) * 10.0;

        auto r2 = writer.add_timestamped_sample<double>(ctr_idx, ts, v);
        if (!r2) {
            std::cerr << "add_timestamped_sample: " << r2.error().message << "\n";
            return 1;
        }
    }

    // ── Emit ──────────────────────────────────────────────────────────────
    auto r3 = writer.write_to_file(out_path);
    if (!r3) {
        std::cerr << out_path << ": " << r3.error().message << "\n";
        return 1;
    }

    std::error_code ec;
    std::uintmax_t const sz = std::filesystem::file_size(out_path, ec);
    if (ec) {
        std::cout << "written: " << out_path << " (size unknown)\n";
    } else {
        std::cout << "written: " << out_path
                  << " (" << sz << " bytes, "
                  << n_sine << " equidistant + " << n_ctr << " timestamped samples)\n";
    }
    return 0;
}
