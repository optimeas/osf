// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

/**
 * @file block_writer.hpp
 * @brief OSF5 analyst-style (block) writer.
 *
 * BlockWriter accumulates all samples in memory and emits the complete
 * OSF5 file at write_to_file() / write_to() time. Contrast StreamingWriter,
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

#include "osf/binary_sample.hpp"
#include "osf/block.hpp"
#include "osf/error.hpp"
#include "osf/streaming_writer.hpp"
#include "osf/types.hpp"

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

// Forward declaration for the from_manager parameter and the free
// osf::write_to / write_to_file(DataManager const&, …) convenience functions.
class DataManager;

// ── BlockWriter ───────────────────────────────────────────────────────

class BlockWriter {
public:
    // Six special members declared here, defined out-of-line in the .cpp.
    // Required because channel_data_ holds std::vector<ChannelData> and
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
    // time before write_to() / write_to_file(). Unset fields receive
    // the same library defaults as in StreamingWriter (created_utc
    // always stamped; creator/tag fallbacks).

    /// Name of the writing device or application (`creator` field).
    void set_creator(std::string value);
    /// Free-form tag (`tag` field; defaults to `"default"` when unset).
    void set_tag(std::string value);
    /// Free-form text describing why the recording was made.
    void set_reason(std::string value);
    /// Separator between path components in channel names (default `.`).
    void set_namespace_sep(std::string value);
    /// Free-form file comment.
    void set_comment(std::string value);
    /// Geolocation of the recording (`created_at_latitude` /
    /// `_longitude` / `_altitude`); decimal degrees and meters.
    void set_location(double latitude, double longitude, double altitude);

    // ── Channel registration ───────────────────────────────────────────

    /**
     * @brief Declare a channel; returns the index used by all add_*
     *        calls. Indices are assigned sequentially starting at 0.
     *
     * Fails with `InvalidArgument` when `def.size_of_length_value` is
     * neither 2 nor 4, `def.data_type` / `def.channel_type` is
     * `Unsupported`, or 65535 channels are already declared. Unlike
     * StreamingWriter, a declared `size_of_length_value` of 2 is only a
     * starting point for variable channels — emit auto-bumps it to 4
     * when an accumulated sample needs it.
     */
    [[nodiscard]] Result<std::uint16_t> add_channel(ChannelDef def);

    /// Number of channels declared so far.
    [[nodiscard]] std::size_t channel_count() const noexcept;

    /// Index of the channel registered under @p name, or `std::nullopt`
    /// if no channel has that name. With duplicate names the first
    /// registration wins.
    [[nodiscard]] std::optional<std::uint16_t>
    channel_index(std::string_view name) const;

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
    [[nodiscard]] Result<void> add_equidistant_segment(
        std::uint16_t channel, std::int64_t start_ts_ns, double rate_hz,
        float const* samples, std::size_t count);
    /// `double` overload — same semantics as the `float` form above.
    [[nodiscard]] Result<void> add_equidistant_segment(
        std::uint16_t channel, std::int64_t start_ts_ns, double rate_hz,
        double const* samples, std::size_t count);

    // ── Timestamped numeric accumulation — template, 11 instantiations ─

    /// Accumulate one `(timestamp, value)` sample. T must be one of the
    /// 11 numeric types (bool, intN, uintN, float, double) — enforced
    /// by static_assert. Timestamps are nanoseconds since the Unix
    /// epoch (UTC); monotonicity is not validated.
    template <typename T>
    [[nodiscard]] Result<void> add_timestamped_sample(
        std::uint16_t channel, std::int64_t timestamp_ns, T value);

    /// Accumulate @p count `(timestamp, value)` pairs from parallel
    /// arrays. Chunking into spec-sized `bcAbsTimeStampData` blocks
    /// happens at emit time.
    template <typename T>
    [[nodiscard]] Result<void> add_timestamped_samples(
        std::uint16_t channel, std::int64_t const* timestamps_ns,
        T const* values, std::size_t count);

    // ── Timestamped GPS accumulation ───────────────────────────────────

    /// GPS variant of add_timestamped_sample (24-byte
    /// latitude/longitude/altitude wire format).
    [[nodiscard]] Result<void> add_timestamped_gps_sample(
        std::uint16_t channel, std::int64_t timestamp_ns, GpsLocation value);
    /// GPS variant of add_timestamped_samples (parallel arrays).
    [[nodiscard]] Result<void> add_timestamped_gps_samples(
        std::uint16_t channel, std::int64_t const* timestamps_ns,
        GpsLocation const* values, std::size_t count);

    // ── Variable accumulation (string + binary) ────────────────────────
    //
    // One sample per block per spec; emit writes each sample as its own
    // single-sample bcAbsTimeStampData block, with no trailing 0x00
    // (OSF5, spec rev 2026-05-24). Samples larger than the declared
    // size_of_length_value allows trigger the automatic 2 -> 4 bump at
    // emit time, so unlike StreamingWriter there is no hard size limit
    // below the 4-byte length-field capacity.

    /// Accumulate one string sample (copied into the writer).
    [[nodiscard]] Result<void> add_string_sample(
        std::uint16_t channel, std::int64_t timestamp_ns,
        std::string_view value);
    /// Accumulate @p count string samples from parallel arrays.
    [[nodiscard]] Result<void> add_string_samples(
        std::uint16_t channel, std::int64_t const* timestamps_ns,
        std::string_view const* values, std::size_t count);

    /// Accumulate one binary sample. @p value is a non-owning view —
    /// the bytes are copied into the writer before the call returns.
    [[nodiscard]] Result<void> add_binary_sample(
        std::uint16_t channel, std::int64_t timestamp_ns,
        BinarySample value);
    /// Accumulate @p count binary samples from parallel arrays.
    [[nodiscard]] Result<void> add_binary_samples(
        std::uint16_t channel, std::int64_t const* timestamps_ns,
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
    [[nodiscard]] Result<void> write_to_file(std::filesystem::path path) const;
    /// Stream variant of write_to_file — works against any
    /// `std::ostream` (memory, socket, …).
    [[nodiscard]] Result<void> write_to(std::ostream& out) const;

    // ── Round-trip / copy ──────────────────────────────────────────────

    /// Build a BlockWriter from a loaded DataManager: copies file-info
    /// (creator, tag, reason, location, namespace_sep, comment) and every
    /// typed channel (equidistant segments, timestamped numeric/GPS,
    /// string/binary). Always emits OSF5 (the library writes OSF5
    /// only), even when the source manager came from an OSF4 file.
    [[nodiscard]] static Result<BlockWriter> from_manager(DataManager const& mgr);

private:
    struct ChannelData;   // fully defined in block_writer.cpp

    // Type trait: which T are supported by add_timestamped_*<T>.
    template <typename T>
    struct IsTimestampedNumeric : std::false_type {};

    // Header-local mirror of detail::FileInfoDraft — avoids pulling the
    // private writer_common.hpp into this public header.
    struct FileInfoFields {
        std::optional<std::string> creator;
        std::optional<std::string> tag;
        std::optional<std::string> reason;
        std::optional<std::string> namespace_sep;
        std::optional<std::string> comment;
        std::optional<double> created_at_latitude;
        std::optional<double> created_at_longitude;
        std::optional<double> created_at_altitude;
    };

    template <typename T>
    [[nodiscard]] Result<void> add_equidistant_segment_impl(
        std::uint16_t channel, std::int64_t start_ts_ns, double rate_hz,
        T const* samples, std::size_t count);

    // Private template — definition + explicit instantiations live
    // in the .cpp; this declaration is here so the public template
    // body can forward to it.
    template <typename T>
    [[nodiscard]] Result<void> add_timestamped_samples_impl(
        std::uint16_t channel, std::int64_t const* timestamps_ns,
        T const* values, std::size_t count);

    void autobump_size_of_length_value(std::vector<ChannelDef>& defs) const;

    [[nodiscard]] Result<void> emit_channel(std::ostream& out,
        std::vector<std::uint8_t>& buf, std::uint16_t ci, std::uint8_t sov,
        ChannelData const& cd) const;

    [[nodiscard]] Result<void> write_block_bytes(std::ostream& out,
        std::vector<std::uint8_t> const& buf) const;

    FileInfoFields                                    file_info_;
    std::vector<ChannelDef>                           channels_;
    std::vector<ChannelData>                          channel_data_;
    std::unordered_map<std::string, std::uint16_t>    name_to_index_;
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
Result<void> BlockWriter::add_timestamped_sample(
        std::uint16_t channel, std::int64_t timestamp_ns, T value) {
    static_assert(IsTimestampedNumeric<T>::value,
                  "T must be one of: bool, int8_t..int64_t, "
                  "uint8_t..uint64_t, float, double. Use "
                  "add_timestamped_gps_sample for GpsLocation, "
                  "add_string/binary for variable-length data.");
    return add_timestamped_samples_impl<T>(channel, &timestamp_ns, &value, 1);
}

template <typename T>
Result<void> BlockWriter::add_timestamped_samples(
        std::uint16_t channel, std::int64_t const* timestamps_ns,
        T const* values, std::size_t count) {
    static_assert(IsTimestampedNumeric<T>::value,
                  "T must be one of: bool, int8_t..int64_t, "
                  "uint8_t..uint64_t, float, double. Use "
                  "add_timestamped_gps_samples for GpsLocation, "
                  "add_timestamped_string/binary for variable-length data.");
    return add_timestamped_samples_impl<T>(channel, timestamps_ns, values, count);
}

// ── Free convenience functions ────────────────────────────────────────
// Both build a BlockWriter via BlockWriter::from_manager and immediately
// emit. Always writes OSF5.

/// Load \p mgr into a BlockWriter and write the result to \p path.
[[nodiscard]] Result<void> write_to_file(DataManager const& mgr,
                                         std::filesystem::path path);

/// Load \p mgr into a BlockWriter and write the result to \p out.
[[nodiscard]] Result<void> write_to(DataManager const& mgr,
                                    std::ostream& out);

}  // namespace osf
