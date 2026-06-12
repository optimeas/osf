// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

/**
 * @file streamingwriter.h
 * @brief OSF5 streaming writer for embedded power-loss-safe recording.
 *
 * StreamingWriter writes raw OSF5 files sample by sample (or in small
 * chunks), fsync'ing each completed block to disk. The result is a file
 * that remains readable up to the last successfully fsync'd block even
 * after a sudden power loss or stream abort. The library reader is
 * already best-effort on truncation (BlockReader bumps blocksTruncated
 * and yields cleanly at a partial block).
 *
 * Two writer classes serve the OSF5 write surface:
 *   - StreamingWriter (this class) — embedded; per-block flush via OS
 *     fsync. Constant memory footprint regardless of recording length.
 *     Compression is intentionally out of scope.
 *   - BlockWriter — analyst-style; accumulates samples in memory,
 *     emits the complete file at writeTo() / writeToFile().
 *     Path or memory (std::ostream) sink.
 *
 * Compression is intentionally out of scope. The StreamingWriter writes
 * raw .osf files. Compression to .osfz (gzip) is left to the
 * application layer, typically as a separate post-processing step
 * after close() — write-time and compress-time failure modes stay
 * decoupled that way.
 *
 * Thread safety: StreamingWriter is not thread-safe. Methods must be
 * called from a single thread, or the caller must serialize access
 * externally (e.g., with std::mutex). Different writer instances on
 * different files may run concurrently. Concurrent writers on the same
 * file path produce undefined results.
 *
 * Memory implications: the scratch buffer grows to accommodate the
 * largest single block ever written and is not shrunk until the writer
 * is destroyed. If memory recovery between large blocks is critical,
 * close the writer and construct a new one for the next recording.
 *
 * Streaming guarantees are file-system-specific. For memory/socket
 * sinks, BlockWriter is the right class.
 */

#pragma once

#include "osf/binarysample.h"
#include "osf/block.h"
#include "osf/error.h"
#include "osf/types.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace osf {

namespace detail {
class DurableFile;       // forward decl — definition in src/durablefile_p.h
struct ChannelState;     // forward decl — definition in src/streamingwriter.cpp
}

// ── ChannelDef — user-supplied channel description ────────────────────
//
// Distinct from osf::Channel (read-side metablock entry) because the
// writer assigns the channel index — the user must not specify it.

/**
 * @brief Channel description passed to StreamingWriter::addChannel.
 *
 * For variable-length channels (DataType::String, DataType::Binary)
 * that may carry large samples (images, audio, large structured blobs),
 * declare sizeOfLengthValue = 4 at addChannel() time. For
 * variable-length channels with small payloads — typically under 1 KB
 * per sample — the default of 2 is appropriate.
 *
 * For high sample-rate equidistant or timestamped numeric channels,
 * declare sizeOfLengthValue = 4 to keep large append buffers in a
 * single block (one fsync) rather than chunked across multiple blocks
 * (one fsync per chunk). Example: a 100k-sample double append into a
 * channel with sizeoflengthvalue=2 produces ~13 bcContinuedData blocks
 * and ~13 fsyncs.
 */
struct ChannelDef {
    std::string name;                                       // mandatory
    DataType dataType = DataType::Double;                  // mandatory
    ChannelType channelType = ChannelType::Scalar;         // mandatory
    std::uint8_t sizeOfLengthValue = 2;                  // 2 (default) or 4
    std::optional<std::string> physicalUnit;
    std::optional<std::string> physicalDimension;
    std::optional<std::string> displayName;
    std::optional<std::string> mimeType;
    std::optional<std::string> reference;
    std::optional<std::string> comment;
    std::optional<std::int64_t> timeIncrementNs;
};

// ── StreamingWriter ───────────────────────────────────────────────────

class StreamingWriter {
public:
    explicit StreamingWriter(std::filesystem::path path);
    ~StreamingWriter();

    StreamingWriter(StreamingWriter const&) = delete;
    StreamingWriter& operator=(StreamingWriter const&) = delete;

    /**
     * @brief Move ctor: transfers ownership of the file handle, channel
     *        state, and scratch buffer. The moved-from writer is in
     *        the Closed state and all its methods return Closed-state
     *        errors.
     */
    StreamingWriter(StreamingWriter&&) noexcept;

    /**
     * @brief Move assignment: closes the file handle of *this if open
     *        (best-effort, errors ignored), then transfers ownership
     *        from the source. After move assignment, *this is in the
     *        same state as the source was, and the source is in the
     *        Closed state.
     */
    StreamingWriter& operator=(StreamingWriter&&) noexcept;

    // ── Configuration phase (before start()) ──────────────────────────
    //
    // File-info setters populate the OSF5 metablock's `file` object.
    // They take effect only when called BEFORE start() — the metablock
    // is written to disk by start() and never rewritten. Calls after
    // start() are silently ignored for the current file. All fields are
    // optional; the library applies defaults at metablock assembly:
    // createdUtc is always stamped automatically, an unset creator
    // falls back to "osf-cpp/<version>", an unset tag to "default".

    /// Name of the writing device or application (`creator` field).
    void setCreator(std::string value);
    /// Free-form tag (`tag` field; defaults to `"default"` when unset).
    void setTag(std::string value);
    /// Free-form text describing why the recording was made.
    void setReason(std::string value);
    /// Geolocation of the recording (`createdAtLatitude` /
    /// `_longitude` / `_altitude`); decimal degrees and meters.
    void setLocation(double latitude, double longitude, double altitude);
    /// Separator between path components in channel names (default `.`).
    void setNamespaceSep(std::string value);
    /// Free-form file comment.
    void setComment(std::string value);

    /**
     * @brief Declare a channel; returns the index used by all write
     *        calls. Indices are assigned sequentially starting at 0.
     *
     * Allowed only in the Configure phase (before start()).
     * Fails with `InvalidArgument` when the writer is past Configure,
     * `def.sizeOfLengthValue` is neither 2 nor 4, `def.dataType` /
     * `def.channelType` is `Unsupported`, or 65535 channels are
     * already declared. Duplicate names are not rejected — OSF allows
     * them, but `DataManager::channel(name)` on the read side will only
     * find the first.
     */
    [[nodiscard]] Result<std::uint16_t> addChannel(ChannelDef def);

    // ── Lifecycle ─────────────────────────────────────────────────────

    /**
     * @brief Open the file, write the magic header and the metablock,
     *        fsync. After successful return, the writer is in the
     *        Streaming state.
     *
     * Thread-safe access pattern:
     *
     *     std::mutex writerMutex;
     *     osf::StreamingWriter writer(path);
     *     // ... configure ...
     *     {
     *         std::lock_guard lock(writerMutex);
     *         if (auto r = writer.start(); !r) { ... }
     *     }
     */
    [[nodiscard]] Result<void> start();

    /**
     * @brief File-close. All data is already durable from the per-block
     *        fsync in each write; close() does not re-flush.
     *
     * Safe from any state. From Broken returns the original sticky
     * error after best-effort file-close. After close(), all write*
     * return an error.
     */
    [[nodiscard]] Result<void> close();

    // ── Equidistant writes (float / double only per spec) ────────────

    /**
     * @brief Open a new equidistant segment on @p channel — emits a
     *        `bcStartData` block carrying @p startTimestampNs,
     *        @p sampleRateHz, and the first samples.
     *
     * Each call opens a NEW segment (spec rev 2026-05-04: multiple
     * `bcStartData` per channel are explicit; gaps between segments
     * are not interpolated). Sample counts exceeding the channel's
     * block capacity are chunked automatically into `bcStartData` +
     * `bcContinuedData` blocks — each chunk is one fsync.
     *
     * Fails with `InvalidArgument` when `count == 0`, the rate is not
     * a positive finite double, or the channel is not an equidistant
     * float/double channel of matching type.
     */
    [[nodiscard]] Result<void> startEquidistantSegment(
        std::uint16_t channel, std::int64_t startTimestampNs,
        double sampleRateHz, float const* samples, std::size_t count);
    /// `double` overload — same semantics as the `float` form above.
    [[nodiscard]] Result<void> startEquidistantSegment(
        std::uint16_t channel, std::int64_t startTimestampNs,
        double sampleRateHz, double const* samples, std::size_t count);

    /**
     * @brief Extend the channel's CURRENT segment — emits
     *        `bcContinuedData` blocks (chunked when needed).
     *
     * Requires an open segment: fails with `InvalidBlock` ("append
     * without start") when no `startEquidistantSegment` preceded it.
     * Timing continues seamlessly at `1 / sampleRateHz` per sample.
     */
    [[nodiscard]] Result<void> appendEquidistantSamples(
        std::uint16_t channel, float const* samples, std::size_t count);
    /// `double` overload — same semantics as the `float` form above.
    [[nodiscard]] Result<void> appendEquidistantSamples(
        std::uint16_t channel, double const* samples, std::size_t count);

    // ── Timestamped writes (numeric) — template, 11 instantiations ────

    /**
     * @brief Write one `(timestamp, value)` sample as a
     *        `bcAbsTimeStampData` block (single-sample form, bit 7 = 0).
     *
     * T must be one of the 11 numeric types (bool, intN, uintN, float,
     * double) — enforced by static_assert. The timestamp is nanoseconds
     * since the Unix epoch (UTC); the writer does not validate
     * monotonicity (the spec does not require it).
     */
    template <typename T>
    [[nodiscard]] Result<void> writeTimestampedSample(
        std::uint16_t channel, std::int64_t timestampNs, T value);

    /**
     * @brief Write @p count `(timestamp, value)` pairs as multi-sample
     *        `bcAbsTimeStampData` blocks, chunked to the channel's
     *        block capacity (one fsync per block).
     *
     * @p timestampsNs and @p values are parallel arrays of length
     * @p count. Fails with `InvalidArgument` for `count == 0` or a
     * channel/data-type mismatch.
     */
    template <typename T>
    [[nodiscard]] Result<void> writeTimestampedSamples(
        std::uint16_t channel, std::int64_t const* timestampsNs,
        T const* values, std::size_t count);

    // ── Timestamped writes (GPS) — separate non-template symbols ─────

    /// GPS variant of writeTimestampedSample (24-byte
    /// latitude/longitude/altitude wire format).
    [[nodiscard]] Result<void> writeTimestampedGpsSample(
        std::uint16_t channel, std::int64_t timestampNs,
        GpsLocation value);

    /// GPS variant of writeTimestampedSamples (chunked, parallel
    /// arrays).
    [[nodiscard]] Result<void> writeTimestampedGpsSamples(
        std::uint16_t channel, std::int64_t const* timestampsNs,
        GpsLocation const* values, std::size_t count);

    // ── Timestamped writes (variable, single-sample only per spec) ───

    /**
     * @brief Write one string sample as a single-sample
     *        `bcAbsTimeStampData` block. OSF5: no trailing `0x00` is
     *        appended (spec rev 2026-05-24).
     *
     * One sample per block per spec — there is no multi-sample string
     * write. Fails with `InvalidBlock` when the sample exceeds the
     * single-block payload capacity of the channel's
     * `sizeOfLengthValue` (~64 KB for sov=2); declare
     * `sizeOfLengthValue = 4` at addChannel() time for channels
     * that may carry larger payloads (the streaming writer cannot
     * auto-bump — the metablock is already on disk).
     */
    [[nodiscard]] Result<void> writeTimestampedString(
        std::uint16_t channel, std::int64_t timestampNs,
        std::string_view value);

    /// Binary variant of writeTimestampedString. @p value is a
    /// non-owning view — the bytes are copied into the block before
    /// the call returns.
    [[nodiscard]] Result<void> writeTimestampedBinary(
        std::uint16_t channel, std::int64_t timestampNs,
        BinarySample value);

private:
    // Type trait: which T are supported by write_timestamped_*<T>.
    template <typename T>
    struct IsTimestampedNumeric : std::false_type {};

    // Lifecycle state — definition in the .cpp.
    enum class State;

    State state_;
    std::filesystem::path path_;
    std::unique_ptr<detail::DurableFile> durable_file_;
    std::vector<ChannelDef> channels_;
    std::vector<detail::ChannelState> channel_states_;
    std::vector<std::uint8_t> scratch_buffer_;
    std::optional<Error> sticky_error_;

    // File-info setters write into these private fields.
    std::optional<std::string> creator_;
    std::optional<std::string> tag_;
    std::optional<std::string> reason_;
    std::optional<double>      created_at_latitude_;
    std::optional<double>      created_at_longitude_;
    std::optional<double>      created_at_altitude_;
    std::optional<std::string> namespace_sep_;
    std::optional<std::string> comment_;

    // ── Type-agnostic helpers (defined in streamingwriter.cpp) ────────
    [[nodiscard]] Result<void> doWriteBlock(std::uint8_t const* data,
                                              std::size_t size);
    [[nodiscard]] std::uint8_t sovFor(std::uint16_t channel) const noexcept;
    [[nodiscard]] std::optional<Error> requireStreamingState() const;
    [[nodiscard]] std::optional<Error> requireEquidistantChannel(
        std::uint16_t channel, DataType expected);
    [[nodiscard]] std::optional<Error> requireTimestampedChannel(
        std::uint16_t channel, DataType expected);
    [[nodiscard]] std::optional<Error> requireVariableChannel(
        std::uint16_t channel, DataType expected);

    // Private equidistant impl templates — definitions + explicit
    // instantiations (for float and double) live in the .cpp.
    template <typename T>
    [[nodiscard]] Result<void> startEquidistantSegmentImpl(
        std::uint16_t channel, std::int64_t startTimestampNs,
        double sampleRateHz, T const* samples, std::size_t count);

    template <typename T>
    [[nodiscard]] Result<void> appendEquidistantSamplesImpl(
        std::uint16_t channel, T const* samples, std::size_t count);

    // Private template — definition + explicit instantiations live
    // in the .cpp; this declaration is here so the public template
    // body can forward to it.
    template <typename T>
    [[nodiscard]] Result<void> writeTimestampedSamplesImpl(
        std::uint16_t channel, std::int64_t const* timestampsNs,
        T const* values, std::size_t count);
};

// ── IsTimestampedNumeric specializations ─────────────────────────────

template <> struct StreamingWriter::IsTimestampedNumeric<bool>          : std::true_type {};
template <> struct StreamingWriter::IsTimestampedNumeric<std::int8_t>   : std::true_type {};
template <> struct StreamingWriter::IsTimestampedNumeric<std::int16_t>  : std::true_type {};
template <> struct StreamingWriter::IsTimestampedNumeric<std::int32_t>  : std::true_type {};
template <> struct StreamingWriter::IsTimestampedNumeric<std::int64_t>  : std::true_type {};
template <> struct StreamingWriter::IsTimestampedNumeric<std::uint8_t>  : std::true_type {};
template <> struct StreamingWriter::IsTimestampedNumeric<std::uint16_t> : std::true_type {};
template <> struct StreamingWriter::IsTimestampedNumeric<std::uint32_t> : std::true_type {};
template <> struct StreamingWriter::IsTimestampedNumeric<std::uint64_t> : std::true_type {};
template <> struct StreamingWriter::IsTimestampedNumeric<float>         : std::true_type {};
template <> struct StreamingWriter::IsTimestampedNumeric<double>        : std::true_type {};

// ── Public template bodies — thin forward to the private _impl ───────

template <typename T>
Result<void> StreamingWriter::writeTimestampedSample(
        std::uint16_t channel, std::int64_t timestampNs, T value) {
    static_assert(IsTimestampedNumeric<T>::value,
                  "T must be one of: bool, int8_t..int64_t, "
                  "uint8_t..uint64_t, float, double. Use "
                  "writeTimestampedGpsSample for GpsLocation, "
                  "writeTimestampedString/binary for variable-length data.");
    return writeTimestampedSamplesImpl<T>(channel, &timestampNs,
                                             &value, 1);
}

template <typename T>
Result<void> StreamingWriter::writeTimestampedSamples(
        std::uint16_t channel, std::int64_t const* timestampsNs,
        T const* values, std::size_t count) {
    static_assert(IsTimestampedNumeric<T>::value,
                  "T must be one of: bool, int8_t..int64_t, "
                  "uint8_t..uint64_t, float, double. Use "
                  "writeTimestampedGpsSamples for GpsLocation, "
                  "writeTimestampedString/binary for variable-length data.");
    return writeTimestampedSamplesImpl<T>(channel, timestampsNs,
                                             values, count);
}

}  // namespace osf
