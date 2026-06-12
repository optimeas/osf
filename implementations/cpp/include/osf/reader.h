// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

/// \file reader.h
/// OSF block-stream reader.
///
/// `BlockReader` consumes an `std::istream` whose cursor is positioned
/// at the first byte after the metablock and yields one `Block` per
/// call to `next()`. The block-stream format is documented in
/// the OSF format specification §"Control byte" / "Data structure per
/// control type".
///
/// Design choices for this layer:
///
/// - **Two consumer patterns.** The primitive is
///   `next() -> std::optional<Result<Block>>`. A range-based for loop
///   is supported via `begin()` / `end()` returning an input iterator
///   plus an end sentinel.
/// - **Best-effort on truncation.** A file that ends mid-block —
///   typical for embedded writers losing power — yields all blocks up
///   to the last complete one and then `nullopt`. The reader bumps
///   `stats().blocks_truncated` from 0 to 1 (capped because no useful
///   block can follow a partial one) and stops.
/// - **Skip on unsupported.** Channels marked `DataType::Unsupported`
///   or `ChannelType::Unsupported` do not abort the iteration; the
///   reader consumes the payload bytes from the stream and emits
///   `Skipped` so downstream code keeps working.
/// - **Skipped payload capture is opt-in.** Default behaviour drops
///   the bytes without allocation; specialists who need to look at
///   deprecated `bcMessageEvent` blocks or unknown future types call
///   `with_capture_skipped_payload(true)` to keep them.

#pragma once

#include <chrono>
#include <cstdint>
#include <iosfwd>
#include <iterator>
#include <optional>
#include <unordered_map>

#include <osf/block.h>
#include <osf/error.h>
#include <osf/header.h>
#include <osf/metablock.h>
#include <osf/stats.h>
#include <osf/types.h>

namespace osf {

/// Best-effort iterator over the block stream of an OSF file.
///
/// The reader borrows an `std::istream&` whose cursor is right after
/// the metablock, plus the parsed `MetaBlock` (so it can resolve
/// channel indices to their per-channel `data_type`, `channel_type`,
/// and `size_of_length_value`). The stream must outlive the reader.
///
/// Construct directly with the two-argument constructor. Optional
/// fluent setters:
/// - `with_capture_skipped_payload(bool)` — keep raw bytes of skipped
///   blocks instead of dropping them.
/// - `with_file_size(u64)` — record the originating file size so the
///   `stats()` output can show it (not used by the reader itself).
class BlockReader {
public:
    /// Construct a reader against an open stream and the parsed
    /// metablock. The metablock is consumed only for its channel
    /// definitions; the reader keeps an internal lookup table indexed
    /// by channel index for fast access during iteration. Per-channel
    /// stats are pre-seeded with the channel name and zero counters.
    BlockReader(std::istream& stream, MetaBlock const& meta);

    // Non-copyable (the stream is borrowed and the iterator carries
    // state); moves are safe.
    BlockReader(BlockReader const&) = delete;
    BlockReader& operator=(BlockReader const&) = delete;
    BlockReader(BlockReader&&) noexcept = default;
    BlockReader& operator=(BlockReader&&) noexcept = default;
    ~BlockReader() = default;

    /// Opt in to capturing the raw payload bytes of skipped blocks.
    /// Default is `false` (zero allocation per skipped block).
    BlockReader& with_capture_skipped_payload(bool enabled) noexcept {
        capture_skipped_ = enabled;
        return *this;
    }

    /// Record the file size that produced this stream so consumers
    /// (e.g. the `stats` example) can show it. The reader does not
    /// use the value internally.
    BlockReader& with_file_size(std::uint64_t file_size_bytes) noexcept {
        stats_.file_size_bytes = file_size_bytes;
        return *this;
    }

    /// Pull one block from the stream.
    ///
    /// Return value:
    /// - `std::nullopt` — end of stream (clean EOF, optional trailer
    ///   consumed, or truncation reached).
    /// - `Result<Block>` with a value — a successfully decoded block.
    /// - `Result<Block>` with an error — a hard failure that stops
    ///   iteration. After an error the reader is finished and further
    ///   calls return `std::nullopt`.
    [[nodiscard]] std::optional<Result<Block>> next();

    /// Read-only snapshot of the running stats. `elapsed` is updated
    /// to the current wall-clock delta and `blocks_total` /
    /// `channels_with_data` are recomputed from per-channel detail.
    [[nodiscard]] ReaderStats stats() const;

    /// Number of blocks the reader could not finish before the stream
    /// ended. Capped at 1 by construction.
    [[nodiscard]] std::uint64_t blocks_truncated() const noexcept {
        return stats_.blocks_truncated;
    }

    /// `true` if the reader consumed the optional `0xFFFF`
    /// info-data block.
    [[nodiscard]] bool trailer_seen() const noexcept {
        return stats_.trailer_seen;
    }

    /// File size that was supplied via `with_file_size`, if any.
    [[nodiscard]] std::optional<std::uint64_t> file_size_bytes() const noexcept {
        return stats_.file_size_bytes;
    }

    // -----------------------------------------------------------------
    // Input-iterator API for range-based for loops.
    // -----------------------------------------------------------------

    /// End sentinel returned by `BlockReader::end()`. Distinct type
    /// (rather than `Iterator{nullptr}`) keeps the equality comparison
    /// explicit and one-directional.
    struct EndSentinel {};

    /// Single-pass input iterator over the reader. `operator++()`
    /// advances the underlying `BlockReader`; the dereferenced
    /// `Result<Block>` is the most recent yield. After
    /// `BlockReader::next()` returns `std::nullopt` the iterator
    /// compares equal to `EndSentinel`.
    class Iterator {
    public:
        using iterator_category = std::input_iterator_tag;
        using value_type        = Result<Block>;
        using difference_type   = std::ptrdiff_t;
        using pointer           = Result<Block>*;
        using reference         = Result<Block>&;

        Iterator() noexcept : reader_(nullptr) {}
        explicit Iterator(BlockReader& reader);

        Iterator& operator++();      // pre-increment
        void operator++(int);        // post-increment (void per InputIt)

        Result<Block>& operator*() { return *current_; }
        Result<Block>* operator->() { return &(*current_); }

        friend bool operator==(Iterator const& a, EndSentinel const&) noexcept {
            return !a.current_.has_value();
        }
        friend bool operator!=(Iterator const& a, EndSentinel const& b) noexcept {
            return !(a == b);
        }
        friend bool operator==(EndSentinel const& s, Iterator const& a) noexcept {
            return a == s;
        }
        friend bool operator!=(EndSentinel const& s, Iterator const& a) noexcept {
            return a != s;
        }

    private:
        BlockReader* reader_;
        std::optional<Result<Block>> current_;
    };

    Iterator    begin() { return Iterator{*this}; }
    EndSentinel end()   { return EndSentinel{}; }

private:
    // Per-channel metadata the reader needs to decode a block: the
    // channel and data types so we know how to route the payload, plus
    // size_of_length_value so we know how wide the length prefix is on
    // the wire.
    struct ChannelInfo {
        ChannelType channel_type = ChannelType::Scalar;
        DataType    data_type    = DataType::Unsupported;
        std::uint8_t size_of_length_value = 0;
    };

    // Tag-only result returned by the I/O helpers — distinguishes a
    // clean EOF (truncation) from a fully-read value.
    template <typename T>
    using IoResult = Result<std::optional<T>>;

    IoResult<std::uint32_t> read_length_field(std::uint8_t sizeof_field);
    IoResult<std::vector<std::uint8_t>> read_payload(std::size_t len);
    Result<bool> drain(std::uint64_t len);
    Result<void> consume_trailer();

    Result<Block> skip_block(std::uint16_t channel_index, std::size_t length,
                             SkipReason reason);
    void record_skip(std::uint16_t channel_index, std::uint32_t length,
                     SkipReason const& reason);

    std::istream* stream_;
    /// OSF file version derived from `meta.file_info.version`. Drives
    /// the version-deterministic null-terminator rule (spec rev
    /// 2026-05-24): OSF4 strips the last byte of every string/binary
    /// AbsTs payload, OSF5 leaves it alone.
    OsfVersion osf_version_ = OsfVersion::Osf5;
    std::unordered_map<std::uint16_t, ChannelInfo> channels_;
    bool finished_ = false;
    bool capture_skipped_ = false;
    std::chrono::steady_clock::time_point started_;
    ReaderStats stats_;
};

}  // namespace osf
