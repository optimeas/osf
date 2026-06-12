---
title: Cookbook
description: Recipes for typical tasks with the OSF C++ library — from file inspection through CSV export to the embedded recording loop
sidebar_position: 7
image: "/img/om_social_card.png"
keywords:
  - OSF
  - C++
  - Examples
  - Cookbook
  - Recipes
last_update:
  date: 2026-06-12
  author: Optimeas GmbH
---

# Cookbook

Compact, copy-ready recipes. Four of them exist as runnable programs in
the library's `examples/` directory (`inspect`, `dump`, `write`,
`copy`) — on any discrepancy the shipped example code wins.

All recipes assume `#include <osf/osf.h>` unless stated otherwise; error
handling is trimmed to the minimum.

## Inspect a file (metadata + channel list)

```cpp
auto r = osf::DataManager::loadFromFile(path);         // .osf or .osfz
if (!r) { std::cerr << r.error().message << "\n"; return 1; }
auto const& mgr = *r;

std::cout << "version:  " << mgr.meta.fileInfo.version << "\n"
          << "creator:  " << mgr.meta.fileInfo.creator.value_or("-") << "\n"
          << "created:  " << mgr.meta.fileInfo.createdUtc.value_or("-") << "\n"
          << "channels: " << mgr.channels().size() << "\n";

for (osf::DataChannel const& ch : mgr.channels()) {
    std::cout << "  [" << osf::channelIndex(ch) << "] "
              << osf::channelName(ch)
              << "  (" << osf::channelSampleCount(ch) << " samples, unit: "
              << osf::channelPhysicalUnit(ch).value_or("-") << ")\n";
}
```

## Get a channel as `double` values with timestamps

```cpp
osf::DataChannel const* ch = mgr.channel("Motor.Speed");
if (!ch) { /* channel does not exist */ }

// Timestamped channel -> (ts, value) pairs:
if (auto const* ts = std::get_if<osf::TimestampedChannel>(ch)) {
    if (auto pairs = osf::asDoublesFlat(*ts)) {
        for (auto const& [tNs, v] : *pairs) { /* … */ }
    }
}

// Equidistant channel -> values + time from segments:
if (auto const* eq = std::get_if<osf::EquidistantChannel>(ch)) {
    for (auto const& s : eq->samplesVector()) {        // reconstructs timestamps
        double v = std::get<double>(s.value);
        /* s.timestampNs, v */
    }
}
```

## Iterate generically over mixed data types

`std::visit` over the value variant makes the code data-type-agnostic
(handy for export tools):

```cpp
auto const& tc = std::get<osf::TimestampedChannel>(*ch);
for (auto const& s : tc.samplesVector()) {
    std::visit([&](auto const& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, osf::GpsLocation>) {
            csv << s.timestampNs << ";" << value.latitude << ";" << value.longitude << "\n";
        } else if constexpr (std::is_arithmetic_v<T>) {
            csv << s.timestampNs << ";" << +value << "\n";   // +value: print int8 as a number
        }
    }, s.value);
}
```

## Minimal CSV export

```cpp
#include <fstream>

std::ofstream csv("channel.csv");
csv << "timestamp_ns;value\n";

auto const* tc = std::get_if<osf::TimestampedChannel>(mgr.channel(name));
if (!tc) return;
auto pairs = osf::asDoublesFlat(*tc);
if (!pairs) return;                                  // DataTypeMismatch etc.
for (auto const& [t, v] : *pairs) csv << t << ";" << v << "\n";
```

## Convert OSF4 → OSF5 (OSFZ input too)

```cpp
auto mgr = osf::DataManager::loadFromFile("old_osf4.osf");
if (!mgr) { /* … */ }
if (auto r = osf::writeToFile(*mgr, "new_osf5.osf"); !r) { /* … */ }
// samples preserved bit-exact; output always OSF5
```

The example program `copy` does exactly this and verifies by reload.

## Write a new file with analysis data

```cpp
osf::BlockWriter w;
w.setCreator("my-tool/1.0");

osf::ChannelDef def;
def.name        = "result.fft_peak";
def.dataType    = osf::DataType::Double;
def.channelType = osf::ChannelType::Scalar;
def.physicalUnit = "Hz";
auto ch = w.addChannel(def);
if (!ch) { /* … */ }

for (auto const& [tsNs, peak] : results)
    if (auto r = w.addTimestampedSample<double>(*ch, tsNs, peak); !r) { /* … */ }

if (auto r = w.writeToFile("result.osf"); !r) { /* … */ }
```

## Embedded recording loop (power-loss safe, with freshness guarantee)

```cpp
#include <osf/streamingwriter.h>
#include <osf/stalevalueguard.h>

osf::StreamingWriter w("/data/rec_0001.osf");
w.setCreator("logger-fw/3.2");

auto temp = w.addChannel(tempDef);       // timestamped double
auto door = w.addChannel(doorDef);       // timestamped bool (event channel)
if (!temp || !door) { /* … */ }
if (auto r = w.start(); !r) { /* … */ }

osf::StaleValueGuard guard(w);            // 100 s default

while (running) {
    std::int64_t const now = nowNs();

    if (newReading)
        if (auto r = guard.writeTimestampedSample<double>(*temp, now, value); !r) break;
    if (doorChanged)
        if (auto r = guard.writeTimestampedSample<bool>(*door, now, open); !r) break;

    if (auto r = guard.poll(now); !r) break;   // re-emits stale channels
    waitForNextTick();
}
if (auto r = w.close(); !r) { /* log the original or close error */ }
```

Every acknowledged write is fsynced — after power loss the
`DataManager` reads the file up to the last complete block.

## Images/blobs as a binary channel

```cpp
// Writing (StreamingWriter: declare sov=4 — samples > ~64 KB!):
osf::ChannelDef cam;
cam.name             = "camera.snapshots";
cam.dataType         = osf::DataType::Binary;
cam.channelType      = osf::ChannelType::Scalar;
cam.sizeOfLengthValue = 4;
cam.mimeType         = "image/jpeg";
auto camCh = w.addChannel(cam);
/* … start() … */
w.writeTimestampedBinary(*camCh, tsNs, osf::BinarySample::fromVector(jpeg));

// Reading:
auto const& vc = std::get<osf::VariableChannel>(*mgr.channel("camera.snapshots"));
if (auto bins = vc.asBinaries()) {
    for (std::size_t i = 0; i < (*bins)->size(); ++i) {
        std::int64_t ts = vc.timestampsNs[i];
        std::vector<std::uint8_t> const& jpeg = (**bins)[i];
        /* … */
    }
}
```

## Aggregate a huge file in streaming mode (without DataManager)

```cpp
#include <fstream>

std::ifstream raw(path, std::ios::binary);
osf::DecompressingIStream in(raw);                       // OSFZ transparent

auto hdr = osf::parseMagicHeader(in);
if (!hdr) { /* … */ }
std::vector<std::uint8_t> mb(hdr->metablockLen);
in.read(reinterpret_cast<char*>(mb.data()), static_cast<std::streamsize>(mb.size()));
auto meta = (hdr->version == osf::OsfVersion::Osf5)
    ? osf::parseMetablockJson(mb.data(), mb.size())
    : osf::parseMetablockXml(mb.data(), mb.size());
if (!meta) { /* … */ }

double sum = 0; std::uint64_t n = 0;
osf::BlockReader reader(in, *meta);
for (auto& blk : reader) {
    if (!blk) break;                                     // hard error
    if (auto const* abs = std::get_if<osf::AbsTimestampData>(&blk->kind)) {
        if (auto const* v = std::get_if<std::vector<std::pair<std::int64_t,double>>>(&abs->samples)) {
            for (auto const& [t, x] : *v) { sum += x; ++n; }
        }
    }
}
// Constant memory regardless of file size.
```

## Print read statistics

```cpp
std::cout << mgr.stats;                                  // multi-line summary
if (mgr.stats.blocksTruncated) std::cout << "WARNING: file was truncated\n";
if (mgr.stats.compressed)
    std::cout << "source was OSFZ ("
              << osf::compressionFormatName(mgr.stats.compressionFormat) << ")\n";
```

## Exceptions instead of Result (app outer edge)

```cpp
#include <osf/throwing.h>

int main(int argc, char** argv) try {
    auto mgr = osf::throwing::load(argv[1]);
    osf::throwing::writeToFile(mgr, argv[2]);
    return 0;
} catch (osf::Exception const& e) {
    std::cerr << "OSF error [" << osf::errorCategoryName(e.code()) << "]: "
              << e.what() << "\n";
    return 1;
}
```

## Consume from C

See the complete example on the [C ABI](c-abi.md) page — load, list
channels, copy-out readers, `osf_write_to_file`.
