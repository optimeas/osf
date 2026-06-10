// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

/**
 * @file streaming_writer.hpp
 * @brief OSF5 streaming writer for embedded power-loss-safe recording.
 *
 * StreamingWriter writes raw OSF5 files sample by sample (or in small
 * chunks), fsync'ing each completed block to disk. The result is a file
 * that remains readable up to the last successfully fsync'd block even
 * after a sudden power loss or stream abort. The library reader is
 * already best-effort on truncation (BlockReader bumps blocks_truncated
 * and yields cleanly at a partial block).
 *
 * Two writer classes serve the OSF5 write surface (DECISIONS §7):
 *   - StreamingWriter (this class) — embedded; per-block flush via OS
 *     fsync. Constant memory footprint regardless of recording length.
 *     Compression is intentionally out of scope.
 *   - BlockWriter — analyst-style; accumulates samples in memory,
 *     emits the complete file at write_to() / write_to_file().
 *     Path or memory (std::ostream) sink.
 *
 * Compression is intentionally out of scope. The StreamingWriter writes
 * raw .osf files. Compression to .osfz (gzip) is left to the
 * application layer, typically as a separate post-processing step
 * after close(). See the design spec §3.1 for the full rationale
 * (decoupling of write-time and compress-time failure modes).
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

#include "osf/binary_sample.hpp"
#include "osf/block.hpp"
#include "osf/error.hpp"
#include "osf/types.hpp"

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
class DurableFile;       // forward decl — definition in src/durable_file.hpp
struct ChannelState;     // forward decl — definition in src/streaming_writer.cpp
}

// ── ChannelDef — user-supplied channel description ────────────────────
//
// Distinct from osf::Channel (read-side metablock entry) because the
// writer assigns the channel index — the user must not specify it.

/**
 * @brief Channel description passed to StreamingWriter::add_channel.
 *
 * For variable-length channels (DataType::String, DataType::Binary)
 * that may carry large samples (images, audio, large structured blobs),
 * declare size_of_length_value = 4 at add_channel() time. For
 * variable-length channels with small payloads — typically under 1 KB
 * per sample — the default of 2 is appropriate.
 *
 * For high sample-rate equidistant or timestamped numeric channels,
 * declare size_of_length_value = 4 to keep large append buffers in a
 * single block (one fsync) rather than chunked across multiple blocks
 * (one fsync per chunk). Example: a 100k-sample double append into a
 * channel with sizeoflengthvalue=2 produces ~13 bcContinuedData blocks
 * and ~13 fsyncs.
 */
struct ChannelDef {
    std::string name;                                       // mandatory
    DataType data_type = DataType::Double;                  // mandatory
    ChannelType channel_type = ChannelType::Scalar;         // mandatory
    std::uint8_t size_of_length_value = 2;                  // 2 (default) or 4
    std::optional<std::string> physical_unit;
    std::optional<std::string> physical_dimension;
    std::optional<std::string> display_name;
    std::optional<std::string> mime_type;
    std::optional<std::string> reference;
    std::optional<std::string> comment;
    std::optional<std::int64_t> time_increment_ns;
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

    void set_creator(std::string value);
    void set_tag(std::string value);
    void set_reason(std::string value);
    void set_location(double latitude, double longitude, double altitude);
    void set_namespace_sep(std::string value);
    void set_comment(std::string value);

    [[nodiscard]] Result<std::uint16_t> add_channel(ChannelDef def);

    // ── Lifecycle ─────────────────────────────────────────────────────

    /**
     * @brief Open the file, write the magic header and the metablock,
     *        fsync. After successful return, the writer is in the
     *        Streaming state.
     *
     * Thread-safe access pattern:
     *
     *     std::mutex writer_mutex;
     *     osf::StreamingWriter writer(path);
     *     // ... configure ...
     *     {
     *         std::lock_guard lock(writer_mutex);
     *         if (auto r = writer.start(); !r) { ... }
     *     }
     */
    [[nodiscard]] Result<void> start();

    /**
     * @brief File-close. All data is already durable from the per-block
     *        fsync in each write; close() does not re-flush.
     *
     * Safe from any state. From Broken returns the original sticky
     * error after best-effort file-close. After close(), all write_*
     * return an error.
     */
    [[nodiscard]] Result<void> close();

    // ── Equidistant writes (float / double only per spec) ────────────

    [[nodiscard]] Result<void> start_equidistant_segment(
        std::uint16_t channel, std::int64_t start_timestamp_ns,
        double sample_rate_hz, float const* samples, std::size_t count);
    [[nodiscard]] Result<void> start_equidistant_segment(
        std::uint16_t channel, std::int64_t start_timestamp_ns,
        double sample_rate_hz, double const* samples, std::size_t count);

    [[nodiscard]] Result<void> append_equidistant_samples(
        std::uint16_t channel, float const* samples, std::size_t count);
    [[nodiscard]] Result<void> append_equidistant_samples(
        std::uint16_t channel, double const* samples, std::size_t count);

    // ── Timestamped writes (numeric) — template, 11 instantiations ────

    template <typename T>
    [[nodiscard]] Result<void> write_timestamped_sample(
        std::uint16_t channel, std::int64_t timestamp_ns, T value);

    template <typename T>
    [[nodiscard]] Result<void> write_timestamped_samples(
        std::uint16_t channel, std::int64_t const* timestamps_ns,
        T const* values, std::size_t count);

    // ── Timestamped writes (GPS) — separate non-template symbols ─────

    [[nodiscard]] Result<void> write_timestamped_gps_sample(
        std::uint16_t channel, std::int64_t timestamp_ns,
        GpsLocation value);

    [[nodiscard]] Result<void> write_timestamped_gps_samples(
        std::uint16_t channel, std::int64_t const* timestamps_ns,
        GpsLocation const* values, std::size_t count);

    // ── Timestamped writes (variable, single-sample only per spec) ───

    [[nodiscard]] Result<void> write_timestamped_string(
        std::uint16_t channel, std::int64_t timestamp_ns,
        std::string_view value);

    [[nodiscard]] Result<void> write_timestamped_binary(
        std::uint16_t channel, std::int64_t timestamp_ns,
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

    // ── Type-agnostic helpers (defined in streaming_writer.cpp) ────────
    [[nodiscard]] Result<void> do_write_block(std::uint8_t const* data,
                                              std::size_t size);
    [[nodiscard]] std::uint8_t sov_for(std::uint16_t channel) const noexcept;
    [[nodiscard]] std::optional<Error> require_streaming_state() const;
    [[nodiscard]] std::optional<Error> require_equidistant_channel(
        std::uint16_t channel, DataType expected);
    [[nodiscard]] std::optional<Error> require_timestamped_channel(
        std::uint16_t channel, DataType expected);
    [[nodiscard]] std::optional<Error> require_variable_channel(
        std::uint16_t channel, DataType expected);

    // Private equidistant impl templates — definitions + explicit
    // instantiations (for float and double) live in the .cpp.
    template <typename T>
    [[nodiscard]] Result<void> start_equidistant_segment_impl(
        std::uint16_t channel, std::int64_t start_timestamp_ns,
        double sample_rate_hz, T const* samples, std::size_t count);

    template <typename T>
    [[nodiscard]] Result<void> append_equidistant_samples_impl(
        std::uint16_t channel, T const* samples, std::size_t count);

    // Private template — definition + explicit instantiations live
    // in the .cpp; this declaration is here so the public template
    // body can forward to it.
    template <typename T>
    [[nodiscard]] Result<void> write_timestamped_samples_impl(
        std::uint16_t channel, std::int64_t const* timestamps_ns,
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
Result<void> StreamingWriter::write_timestamped_sample(
        std::uint16_t channel, std::int64_t timestamp_ns, T value) {
    static_assert(IsTimestampedNumeric<T>::value,
                  "T must be one of: bool, int8_t..int64_t, "
                  "uint8_t..uint64_t, float, double. Use "
                  "write_timestamped_gps_sample for GpsLocation, "
                  "write_timestamped_string/binary for variable-length data.");
    return write_timestamped_samples_impl<T>(channel, &timestamp_ns,
                                             &value, 1);
}

template <typename T>
Result<void> StreamingWriter::write_timestamped_samples(
        std::uint16_t channel, std::int64_t const* timestamps_ns,
        T const* values, std::size_t count) {
    static_assert(IsTimestampedNumeric<T>::value,
                  "T must be one of: bool, int8_t..int64_t, "
                  "uint8_t..uint64_t, float, double. Use "
                  "write_timestamped_gps_samples for GpsLocation, "
                  "write_timestamped_string/binary for variable-length data.");
    return write_timestamped_samples_impl<T>(channel, timestamps_ns,
                                             values, count);
}

}  // namespace osf
