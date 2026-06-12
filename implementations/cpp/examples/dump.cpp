// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH
//
// dump — print the first N samples of a channel from an OSF / OSFZ file.
//
// Usage:
//   dump <file> [channel-name] [max-samples]
//
// If channel-name is omitted the first channel is used.
// max-samples defaults to 20.  String/binary channels are noted but not
// printed sample-by-sample (they display a count + first/last timestamp).

#include <osf/osf.h>

#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string>
#include <variant>

namespace {

// Human-readable data-type name.
std::string dt_name(osf::DataType dt) {
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
        default:                         return "?";
    }
}

// Print a NumericValueRef as a plain number.
void print_value(osf::NumericValueRef const& v) {
    std::visit([](auto const& val) {
        using T = std::decay_t<decltype(val)>;
        if constexpr (std::is_same_v<T, bool>) {
            std::cout << (val ? "true" : "false");
        } else if constexpr (std::is_same_v<T, osf::GpsLocation>) {
            std::cout << std::fixed << std::setprecision(6)
                      << val.latitude << "," << val.longitude << "," << val.altitude;
        } else {
            std::cout << val;
        }
    }, v);
}

// Dump up to max_samples from an EquidistantChannel.
void dump_equidistant(osf::EquidistantChannel const& ch, std::size_t max_samples) {
    auto sv = ch.samples_vector();
    std::size_t const total = sv.size();
    std::size_t const limit = std::min(total, max_samples);

    std::cout << "channel: " << ch.name
              << "  type=equidistant"
              << "  dtype=" << dt_name(ch.data_type)
              << "  samples=" << total;
    if (ch.physical_unit.has_value()) {
        std::cout << "  unit=" << *ch.physical_unit;
    }
    std::cout << "\n";

    for (std::size_t i = 0; i < limit; ++i) {
        std::cout << "  [" << std::setw(6) << i << "]  ts="
                  << std::setw(20) << sv[i].timestamp_ns
                  << "  val=";
        print_value(sv[i].value);
        std::cout << "\n";
    }
    if (total > limit) {
        std::cout << "  ... (" << (total - limit) << " more)\n";
    }
}

// Dump up to max_samples from a TimestampedChannel.
void dump_timestamped(osf::TimestampedChannel const& ch, std::size_t max_samples) {
    auto sv = ch.samples_vector();
    std::size_t const total = sv.size();
    std::size_t const limit = std::min(total, max_samples);

    std::cout << "channel: " << ch.name
              << "  type=timestamped"
              << "  samples=" << total;
    if (ch.physical_unit.has_value()) {
        std::cout << "  unit=" << *ch.physical_unit;
    }
    std::cout << "\n";

    for (std::size_t i = 0; i < limit; ++i) {
        std::cout << "  [" << std::setw(6) << i << "]  ts="
                  << std::setw(20) << sv[i].timestamp_ns
                  << "  val=";
        print_value(sv[i].value);
        std::cout << "\n";
    }
    if (total > limit) {
        std::cout << "  ... (" << (total - limit) << " more)\n";
    }
}

// Dump a VariableChannel (string or binary) — show count and timestamps only.
void dump_variable(osf::VariableChannel const& ch, std::size_t max_samples) {
    std::size_t const total = ch.timestamps_ns.size();
    std::size_t const limit = std::min(total, max_samples);

    std::string const kind =
        (ch.data_type == osf::DataType::String) ? "string" : "binary";

    std::cout << "channel: " << ch.name
              << "  type=variable  dtype=" << kind
              << "  samples=" << total << "\n";

    for (std::size_t i = 0; i < limit; ++i) {
        std::cout << "  [" << std::setw(6) << i << "]  ts="
                  << std::setw(20) << ch.timestamps_ns[i];
        if (ch.data_type == osf::DataType::String && ch.string_values.has_value()) {
            std::string const& s = (*ch.string_values)[i];
            // Print at most 60 chars to keep output tidy.
            if (s.size() <= 60) {
                std::cout << "  val=\"" << s << "\"";
            } else {
                std::cout << "  val=\"" << s.substr(0, 57) << "...\"";
            }
        } else {
            // Binary: just print byte count.
            if (ch.binary_values.has_value()) {
                std::cout << "  " << (*ch.binary_values)[i].size() << " bytes";
            }
        }
        std::cout << "\n";
    }
    if (total > limit) {
        std::cout << "  ... (" << (total - limit) << " more)\n";
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: dump <file> [channel-name] [max-samples]\n";
        return 2;
    }

    std::string const path         = argv[1];
    std::string const channel_name = (argc >= 3) ? argv[2] : "";
    std::size_t       max_samples  = 20;

    if (argc >= 4) {
        char* end = nullptr;
        long const n = std::strtol(argv[3], &end, 10);
        if (end == argv[3] || n < 0) {
            std::cerr << "dump: max-samples must be a non-negative integer\n";
            return 2;
        }
        max_samples = static_cast<std::size_t>(n);
    }

    auto result = osf::DataManager::load_from_file(path);
    if (!result) {
        std::cerr << path << ": " << result.error().message << "\n";
        return 1;
    }

    osf::DataManager const& mgr = *result;

    if (mgr.channels().empty()) {
        std::cerr << path << ": file contains no channels\n";
        return 1;
    }

    // Select channel.
    osf::DataChannel const* dc = nullptr;
    if (channel_name.empty()) {
        dc = &mgr.channels()[0];
    } else {
        dc = mgr.channel(channel_name);
        if (!dc) {
            std::cerr << path << ": channel not found: " << channel_name << "\n";
            return 1;
        }
    }

    std::visit([&](auto const& ch) {
        using T = std::decay_t<decltype(ch)>;
        if constexpr (std::is_same_v<T, osf::EquidistantChannel>) {
            dump_equidistant(ch, max_samples);
        } else if constexpr (std::is_same_v<T, osf::TimestampedChannel>) {
            dump_timestamped(ch, max_samples);
        } else {
            dump_variable(ch, max_samples);
        }
    }, *dc);

    return 0;
}
