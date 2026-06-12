// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

/**
 * @file blockwriter.h
 * @brief OSF5 analyst-style (block) writer.
 *
 * BlockWriter accumulates all samples in memory and emits the complete
 * OSF5 file at writeToFile() / writeTo() time. Contrast StreamingWriter,
 * which fsyncs each block to disk and is suited for embedded / power-loss-safe
 * recording.
 *
 * Two writer classes serve the OSF5 write surface:
 *   - StreamingWriter — embedded; per-block flush via OS fsync.
 *   - BlockWriter (this class) — analyst-style; accumulates samples in
 *     memory, emits the complete file at close.
 *
 * Thread safety: BlockWriter is not thread-safe. All methods must be
 * called from a single thread, or the caller must serialize externally.
 */

#pragma once

#include "osf/binarysample.h"
#include "osf/block.h"
#include "osf/error.h"
#include "osf/streamingwriter.h"
#include "osf/types.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace osf {

// Forward declaration for the fromManager parameter and the free
// osf::writeTo / writeToFile(DataManager const&, …) convenience functions.
class DataManager;

// ── BlockWriter ───────────────────────────────────────────────────────

class BlockWriter {
public:
    // Six special members declared here, defined out-of-line in the .cpp.
    // Required because m_channelData holds std::vector<ChannelData> and
    // ChannelData is only forward-declared in this header (incomplete type).
    // MSVC will not instantiate vector's special members against an incomplete
    // type, so = default in the header is illegal.
    BlockWriter();
    ~BlockWriter();
    BlockWriter(BlockWriter const&);
    BlockWriter& operator=(BlockWriter const&);
    BlockWriter(BlockWriter&&) noexcept;
    BlockWriter& operator=(BlockWriter&&) noexcept;

    // ── Configuration ─────────────────────────────────────────────────
    //
    // File-info setters populate the OSF5 metablock's `file` object.
    // Unlike StreamingWriter there is no phase restriction: the
    // metablock is assembled at emit time, so setters may be called any
    // time before writeTo() / writeToFile(). Unset fields receive
    // the same library defaults as in StreamingWriter (createdUtc
    // always stamped; creator/tag fallbacks).

    /// Name of the writing device or application (`creator` field).
    void setCreator(std::string value);
    /// Free-form tag (`tag` field; defaults to `"default"` when unset).
    void setTag(std::string value);
    /// Free-form text describing why the recording was made.
    void setReason(std::string value);
    /// Separator between path components in channel names (default `.`).
    void setNamespaceSep(std::string value);
    /// Free-form file comment.
    void setComment(std::string value);
    /// Geolocation of the recording (`createdAtLatitude` /
    /// `_longitude` / `_altitude`); decimal degrees and meters.
    void setLocation(double latitude, double longitude, double altitude);

    // ── Channel registration ───────────────────────────────────────────

    /**
     * @brief Declare a channel; returns the index used by all add*
     *        calls. Indices are assigned sequentially starting at 0.
     *
     * Fails with `InvalidArgument` when `def.sizeOfLengthValue` is
     * neither 2 nor 4, `def.dataType` / `def.channelType` is
     * `Unsupported`, or 65535 channels are already declared. Unlike
     * StreamingWriter, a declared `sizeOfLengthValue` of 2 is only a
     * starting point for variable channels — emit auto-bumps it to 4
     * when an accumulated sample needs it.
     */
    [[nodiscard]] Result<std::uint16_t> addChannel(ChannelDef def);

    /// Number of channels declared so far.
    [[nodiscard]] std::size_t channelCount() const noexcept;

    /// Index of the channel registered under @p name, or `std::nullopt`
    /// if no channel has that name. With duplicate names the first
    /// registration wins.
    [[nodiscard]] std::optional<std::uint16_t>
    channelIndex(std::string_view name) const;

    // ── Equidistant accumulation ───────────────────────────────────────

    /**
     * @brief Accumulate a new equidistant segment on @p channel
     *        (in-memory; nothing is written until emit).
     *
     * Each call opens a NEW segment with its own start timestamp and
     * sample rate (spec rev 2026-05-04). At emit time the segment
     * becomes one `bcStartData` block, chunked into `bcContinuedData`
     * follow-ups when it exceeds the channel's block capacity.
     *
     * Fails with `InvalidArgument` when `count == 0`, the rate is not
     * a positive finite double, or the channel is not an equidistant
     * float/double channel of matching type.
     */
    [[nodiscard]] Result<void> addEquidistantSegment(
        std::uint16_t channel, std::int64_t startTsNs, double rateHz,
        float const* samples, std::size_t count);
    /// `double` overload — same semantics as the `float` form above.
    [[nodiscard]] Result<void> addEquidistantSegment(
        std::uint16_t channel, std::int64_t startTsNs, double rateHz,
        double const* samples, std::size_t count);

    // ── Timestamped numeric accumulation — template, 11 instantiations ─

    /// Accumulate one `(timestamp, value)` sample. T must be one of the
    /// 11 numeric types (bool, intN, uintN, float, double) — enforced
    /// by static_assert. Timestamps are nanoseconds since the Unix
    /// epoch (UTC); monotonicity is not validated.
    template <typename T>
    [[nodiscard]] Result<void> addTimestampedSample(
        std::uint16_t channel, std::int64_t timestampNs, T value);

    /// Accumulate @p count `(timestamp, value)` pairs from parallel
    /// arrays. Chunking into spec-sized `bcAbsTimeStampData` blocks
    /// happens at emit time.
    template <typename T>
    [[nodiscard]] Result<void> addTimestampedSamples(
        std::uint16_t channel, std::int64_t const* timestampsNs,
        T const* values, std::size_t count);

    // ── Timestamped GPS accumulation ───────────────────────────────────

    /// GPS variant of addTimestampedSample (24-byte
    /// latitude/longitude/altitude wire format).
    [[nodiscard]] Result<void> addTimestampedGpsSample(
        std::uint16_t channel, std::int64_t timestampNs, GpsLocation value);
    /// GPS variant of addTimestampedSamples (parallel arrays).
    [[nodiscard]] Result<void> addTimestampedGpsSamples(
        std::uint16_t channel, std::int64_t const* timestampsNs,
        GpsLocation const* values, std::size_t count);

    // ── Variable accumulation (string + binary) ────────────────────────
    //
    // One sample per block per spec; emit writes each sample as its own
    // single-sample bcAbsTimeStampData block, with no trailing 0x00
    // (OSF5, spec rev 2026-05-24). Samples larger than the declared
    // sizeOfLengthValue allows trigger the automatic 2 -> 4 bump at
    // emit time, so unlike StreamingWriter there is no hard size limit
    // below the 4-byte length-field capacity.

    /// Accumulate one string sample (copied into the writer).
    [[nodiscard]] Result<void> addStringSample(
        std::uint16_t channel, std::int64_t timestampNs,
        std::string_view value);
    /// Accumulate @p count string samples from parallel arrays.
    [[nodiscard]] Result<void> addStringSamples(
        std::uint16_t channel, std::int64_t const* timestampsNs,
        std::string_view const* values, std::size_t count);

    /// Accumulate one binary sample. @p value is a non-owning view —
    /// the bytes are copied into the writer before the call returns.
    [[nodiscard]] Result<void> addBinarySample(
        std::uint16_t channel, std::int64_t timestampNs,
        BinarySample value);
    /// Accumulate @p count binary samples from parallel arrays.
    [[nodiscard]] Result<void> addBinarySamples(
        std::uint16_t channel, std::int64_t const* timestampsNs,
        BinarySample const* values, std::size_t count);

    // ── Emit ───────────────────────────────────────────────────────────

    /**
     * @brief Serialise the accumulated state as a complete OSF5 file at
     *        @p path (magic header + metablock + all blocks).
     *
     * `const` — emitting does not consume the writer; the same writer
     * may be emitted multiple times (e.g. to a file and to a stream).
     * Variable channels whose largest sample exceeds the u16 length
     * field get their `sizeoflengthvalue` bumped 2 -> 4 for this emit.
     * No fsync — durability is the caller's concern (contrast
     * StreamingWriter). Fails with `IoError` when the file cannot be
     * created or a write fails.
     */
    [[nodiscard]] Result<void> writeToFile(std::filesystem::path path) const;
    /// Stream variant of writeToFile — works against any
    /// `std::ostream` (memory, socket, …).
    [[nodiscard]] Result<void> writeTo(std::ostream& out) const;

    // ── Round-trip / copy ──────────────────────────────────────────────

    /// Build a BlockWriter from a loaded DataManager: copies file-info
    /// (creator, tag, reason, location, namespaceSep, comment) and every
    /// typed channel (equidistant segments, timestamped numeric/GPS,
    /// string/binary). Always emits OSF5 (the library writes OSF5
    /// only), even when the source manager came from an OSF4 file.
    [[nodiscard]] static Result<BlockWriter> fromManager(DataManager const& mgr);

private:
    struct ChannelData;   // fully defined in blockwriter.cpp

    // Type trait: which T are supported by addTimestamped*<T>.
    template <typename T>
    struct IsTimestampedNumeric : std::false_type {};

    // Header-local mirror of detail::FileInfoDraft — avoids pulling the
    // private writercommon_p.h into this public header.
    struct FileInfoFields {
        std::optional<std::string> creator;
        std::optional<std::string> tag;
        std::optional<std::string> reason;
        std::optional<std::string> namespaceSep;
        std::optional<std::string> comment;
        std::optional<double> createdAtLatitude;
        std::optional<double> createdAtLongitude;
        std::optional<double> createdAtAltitude;
    };

    template <typename T>
    [[nodiscard]] Result<void> addEquidistantSegmentImpl(
        std::uint16_t channel, std::int64_t startTsNs, double rateHz,
        T const* samples, std::size_t count);

    // Private template — definition + explicit instantiations live
    // in the .cpp; this declaration is here so the public template
    // body can forward to it.
    template <typename T>
    [[nodiscard]] Result<void> addTimestampedSamplesImpl(
        std::uint16_t channel, std::int64_t const* timestampsNs,
        T const* values, std::size_t count);

    void autobumpSizeOfLengthValue(std::vector<ChannelDef>& defs) const;

    [[nodiscard]] Result<void> emitChannel(std::ostream& out,
        std::vector<std::uint8_t>& buf, std::uint16_t ci, std::uint8_t sov,
        ChannelData const& cd) const;

    [[nodiscard]] Result<void> writeBlockBytes(std::ostream& out,
        std::vector<std::uint8_t> const& buf) const;

    FileInfoFields                                    m_fileInfo;
    std::vector<ChannelDef>                           m_channels;
    std::vector<ChannelData>                          m_channelData;
    std::unordered_map<std::string, std::uint16_t>    m_nameToIndex;
};

// ── IsTimestampedNumeric specializations ─────────────────────────────

template <> struct BlockWriter::IsTimestampedNumeric<bool>          : std::true_type {};
template <> struct BlockWriter::IsTimestampedNumeric<std::int8_t>   : std::true_type {};
template <> struct BlockWriter::IsTimestampedNumeric<std::int16_t>  : std::true_type {};
template <> struct BlockWriter::IsTimestampedNumeric<std::int32_t>  : std::true_type {};
template <> struct BlockWriter::IsTimestampedNumeric<std::int64_t>  : std::true_type {};
template <> struct BlockWriter::IsTimestampedNumeric<std::uint8_t>  : std::true_type {};
template <> struct BlockWriter::IsTimestampedNumeric<std::uint16_t> : std::true_type {};
template <> struct BlockWriter::IsTimestampedNumeric<std::uint32_t> : std::true_type {};
template <> struct BlockWriter::IsTimestampedNumeric<std::uint64_t> : std::true_type {};
template <> struct BlockWriter::IsTimestampedNumeric<float>         : std::true_type {};
template <> struct BlockWriter::IsTimestampedNumeric<double>        : std::true_type {};

// ── Public template bodies — thin forward to the private _impl ───────

template <typename T>
Result<void> BlockWriter::addTimestampedSample(
        std::uint16_t channel, std::int64_t timestampNs, T value) {
    static_assert(IsTimestampedNumeric<T>::value,
                  "T must be one of: bool, int8_t..int64_t, "
                  "uint8_t..uint64_t, float, double. Use "
                  "addTimestampedGpsSample for GpsLocation, "
                  "addStringSample / addBinarySample for variable-length data.");
    return addTimestampedSamplesImpl<T>(channel, &timestampNs, &value, 1);
}

template <typename T>
Result<void> BlockWriter::addTimestampedSamples(
        std::uint16_t channel, std::int64_t const* timestampsNs,
        T const* values, std::size_t count) {
    static_assert(IsTimestampedNumeric<T>::value,
                  "T must be one of: bool, int8_t..int64_t, "
                  "uint8_t..uint64_t, float, double. Use "
                  "addTimestampedGpsSamples for GpsLocation, "
                  "addStringSamples / addBinarySamples for variable-length data.");
    return addTimestampedSamplesImpl<T>(channel, timestampsNs, values, count);
}

// ── Free convenience functions ────────────────────────────────────────
// Both build a BlockWriter via BlockWriter::fromManager and immediately
// emit. Always writes OSF5.

/// Load \p mgr into a BlockWriter and write the result to \p path.
[[nodiscard]] Result<void> writeToFile(DataManager const& mgr,
                                         std::filesystem::path path);

/// Load \p mgr into a BlockWriter and write the result to \p out.
[[nodiscard]] Result<void> writeTo(DataManager const& mgr,
                                    std::ostream& out);

}  // namespace osf
