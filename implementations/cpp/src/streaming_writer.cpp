// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

#include "osf/streaming_writer.hpp"

#include "block_encode.hpp"           // osf::detail::encode_*
#include "durable_file.hpp"           // osf::detail::DurableFile
#include "osf/metablock.hpp"          // FileInfo, Channel, MetaBlock, serialize_metablock_json

#include <algorithm>
#include <cmath>
#include <cstring>
#include <sstream>
#include <type_traits>
#include <utility>

namespace osf {

// ── State enum (definition; declared in the header) ──────────────────

enum class StreamingWriter::State {
    Configure,
    Streaming,
    Broken,
    Closed
};

namespace detail {

struct ChannelState {
    enum class BlockKindLock {
        Unset, Equidistant, Timestamped, Variable
    };
    BlockKindLock kind_lock = BlockKindLock::Unset;
    DataType      datatype_lock = DataType::Unsupported;   // copied from ChannelDef.data_type
    bool          segment_open = false;                     // equidistant only
};

}  // namespace detail

namespace {

Error make_error(Error::Code code, std::string msg) {
    return Error{code, std::move(msg)};
}

// Map a supported template T to its DataType enum. Compile-time
// dispatch; used by require_timestamped_channel in Tasks 4–6.
template <typename T>
constexpr DataType data_type_for() noexcept {
    if constexpr (std::is_same_v<T, bool>)               return DataType::Bool;
    else if constexpr (std::is_same_v<T, std::int8_t>)   return DataType::Int8;
    else if constexpr (std::is_same_v<T, std::int16_t>)  return DataType::Int16;
    else if constexpr (std::is_same_v<T, std::int32_t>)  return DataType::Int32;
    else if constexpr (std::is_same_v<T, std::int64_t>)  return DataType::Int64;
    else if constexpr (std::is_same_v<T, std::uint8_t>)  return DataType::UInt8;
    else if constexpr (std::is_same_v<T, std::uint16_t>) return DataType::UInt16;
    else if constexpr (std::is_same_v<T, std::uint32_t>) return DataType::UInt32;
    else if constexpr (std::is_same_v<T, std::uint64_t>) return DataType::UInt64;
    else if constexpr (std::is_same_v<T, float>)         return DataType::Float;
    else if constexpr (std::is_same_v<T, double>)        return DataType::Double;
    else { static_assert(sizeof(T) == 0, "unsupported T"); return DataType::Unsupported; }
}

constexpr std::size_t MAX_PAYLOAD_FOR_SOV(std::uint8_t sov) noexcept {
    // u16 length field: max payload = 65535
    // u32 length field: soft-capped at i32::MAX - 1024 to avoid
    // platform-dependent overflow on body-length conversion.
    return (sov == 2)
               ? std::size_t{0xFFFFu}
               : static_cast<std::size_t>(0x7FFFFFFFu - 1024u);
}

// bcStartData multi-sample: payload = [u8 ctrl][i64 ts][f64 rate]
// [u32 N][N * value_size]. Overhead = 21.
std::size_t max_samples_per_start_block(std::size_t value_size,
                                        std::uint8_t sov) noexcept {
    constexpr std::size_t OVERHEAD = 1u + 8u + 8u + 4u;
    std::size_t const max_payload = MAX_PAYLOAD_FOR_SOV(sov);
    if (max_payload <= OVERHEAD) return 1;
    std::size_t const samples = (max_payload - OVERHEAD) / value_size;
    return (samples == 0) ? 1u : samples;
}

// bcContinuedData multi-sample: payload = [u8 ctrl][u32 N]
// [N * value_size]. Overhead = 5.
std::size_t max_samples_per_continued_block(std::size_t value_size,
                                            std::uint8_t sov) noexcept {
    constexpr std::size_t OVERHEAD = 1u + 4u;
    std::size_t const max_payload = MAX_PAYLOAD_FOR_SOV(sov);
    if (max_payload <= OVERHEAD) return 1;
    std::size_t const samples = (max_payload - OVERHEAD) / value_size;
    return (samples == 0) ? 1u : samples;
}

// bcAbsTimeStampData multi-sample: payload = [u8 ctrl][u32 N]
// [N * (8 + value_size)]. Used by Task 5.
std::size_t max_samples_per_timestamped_block(std::size_t value_size,
                                              std::uint8_t sov) noexcept {
    constexpr std::size_t OVERHEAD = 1u + 4u;
    std::size_t const per_sample = 8u + value_size;
    std::size_t const max_payload = MAX_PAYLOAD_FOR_SOV(sov);
    if (max_payload <= OVERHEAD) return 1;
    std::size_t const samples = (max_payload - OVERHEAD) / per_sample;
    return (samples == 0) ? 1u : samples;
}

}  // namespace

// ── Ctor / dtor / move ───────────────────────────────────────────────

StreamingWriter::StreamingWriter(std::filesystem::path path)
    : state_{State::Configure},
      path_{std::move(path)} {
    // scratch_buffer_ default-constructed; allocation happens in start().
}

StreamingWriter::~StreamingWriter() {
    (void) close();   // best-effort close; errors silently dropped per dtor convention
}

StreamingWriter::StreamingWriter(StreamingWriter&& other) noexcept
    : state_{other.state_},
      path_{std::move(other.path_)},
      durable_file_{std::move(other.durable_file_)},
      channels_{std::move(other.channels_)},
      channel_states_{std::move(other.channel_states_)},
      scratch_buffer_{std::move(other.scratch_buffer_)},
      sticky_error_{std::move(other.sticky_error_)},
      creator_{std::move(other.creator_)},
      tag_{std::move(other.tag_)},
      reason_{std::move(other.reason_)},
      created_at_latitude_{other.created_at_latitude_},
      created_at_longitude_{other.created_at_longitude_},
      created_at_altitude_{other.created_at_altitude_},
      namespace_sep_{std::move(other.namespace_sep_)},
      comment_{std::move(other.comment_)} {
    other.state_ = State::Closed;
}

StreamingWriter& StreamingWriter::operator=(StreamingWriter&& other) noexcept {
    if (this != &other) {
        (void) close();
        state_                 = other.state_;
        path_                  = std::move(other.path_);
        durable_file_          = std::move(other.durable_file_);
        channels_              = std::move(other.channels_);
        channel_states_        = std::move(other.channel_states_);
        scratch_buffer_        = std::move(other.scratch_buffer_);
        sticky_error_          = std::move(other.sticky_error_);
        creator_               = std::move(other.creator_);
        tag_                   = std::move(other.tag_);
        reason_                = std::move(other.reason_);
        created_at_latitude_   = other.created_at_latitude_;
        created_at_longitude_  = other.created_at_longitude_;
        created_at_altitude_   = other.created_at_altitude_;
        namespace_sep_         = std::move(other.namespace_sep_);
        comment_               = std::move(other.comment_);
        other.state_ = State::Closed;
    }
    return *this;
}

// ── File-info setters ────────────────────────────────────────────────

void StreamingWriter::set_creator(std::string value)       { creator_   = std::move(value); }
void StreamingWriter::set_tag(std::string value)           { tag_       = std::move(value); }
void StreamingWriter::set_reason(std::string value)        { reason_    = std::move(value); }
void StreamingWriter::set_namespace_sep(std::string value) { namespace_sep_ = std::move(value); }
void StreamingWriter::set_comment(std::string value)       { comment_   = std::move(value); }

void StreamingWriter::set_location(double latitude, double longitude,
                                   double altitude) {
    created_at_latitude_  = latitude;
    created_at_longitude_ = longitude;
    created_at_altitude_  = altitude;
}

// ── add_channel ──────────────────────────────────────────────────────

Result<std::uint16_t> StreamingWriter::add_channel(ChannelDef def) {
    if (state_ != State::Configure) {
        return tl::make_unexpected(make_error(
            Error::Code::InvalidArgument,
            "add_channel: writer is past the Configure phase"));
    }
    if (def.size_of_length_value != 2 && def.size_of_length_value != 4) {
        return tl::make_unexpected(make_error(
            Error::Code::InvalidArgument,
            "add_channel: size_of_length_value must be 2 or 4"));
    }
    if (def.data_type == DataType::Unsupported) {
        return tl::make_unexpected(make_error(
            Error::Code::InvalidArgument,
            "add_channel: data_type Unsupported is not writeable"));
    }
    if (def.channel_type == ChannelType::Unsupported) {
        return tl::make_unexpected(make_error(
            Error::Code::InvalidArgument,
            "add_channel: channel_type Unsupported is not writeable"));
    }
    if (channels_.size() >= 0xFFFF) {
        return tl::make_unexpected(make_error(
            Error::Code::InvalidArgument,
            "add_channel: too many channels (max 65535)"));
    }

    auto const idx = static_cast<std::uint16_t>(channels_.size());
    detail::ChannelState st;
    st.datatype_lock = def.data_type;
    channels_.push_back(std::move(def));
    channel_states_.push_back(st);
    return idx;
}

// ── start / close ────────────────────────────────────────────────────

Result<void> StreamingWriter::start() {
    if (state_ != State::Configure) {
        return tl::make_unexpected(make_error(
            Error::Code::InvalidArgument,
            "start: writer is past the Configure phase"));
    }
    if (channels_.empty()) {
        return tl::make_unexpected(make_error(
            Error::Code::InvalidArgument,
            "start: no channels declared"));
    }

    // Open the file via DurableFile.
    auto df = detail::DurableFile::create(path_);
    if (!df) {
        return tl::make_unexpected(df.error());
    }
    durable_file_ = std::make_unique<detail::DurableFile>(std::move(*df));

    // Build the MetaBlock from configuration state.
    MetaBlock meta;
    meta.file_info.version = 5;
    meta.file_info.creator             = creator_;
    meta.file_info.tag                 = tag_;
    meta.file_info.reason              = reason_;
    meta.file_info.created_at_latitude  = created_at_latitude_;
    meta.file_info.created_at_longitude = created_at_longitude_;
    meta.file_info.created_at_altitude  = created_at_altitude_;
    meta.file_info.namespace_sep       = namespace_sep_;
    meta.file_info.comment             = comment_;
    for (std::size_t i = 0; i < channels_.size(); ++i) {
        ChannelDef const& d = channels_[i];
        Channel ch;
        ch.index = static_cast<std::uint16_t>(i);
        ch.name  = d.name;
        ch.data_type = d.data_type;
        ch.channel_type = d.channel_type;
        ch.size_of_length_value = d.size_of_length_value;
        ch.time_increment_ns = d.time_increment_ns;
        ch.physical_unit = d.physical_unit;
        ch.physical_dimension = d.physical_dimension;
        ch.display_name = d.display_name;
        ch.mime_type = d.mime_type;
        ch.reference = d.reference;
        ch.comment = d.comment;
        meta.channels.push_back(std::move(ch));
    }

    // Serialize the metablock and build the magic-header line.
    std::string const json_body = serialize_metablock_json(meta);
    std::string const magic_line =
        "OSF5 " + std::to_string(json_body.size()) + "\n";

    // Helper for best-effort unlink + failed-start cleanup.
    auto fail_with_unlink = [&](Error err) -> Result<void> {
        (void) durable_file_->close();
        durable_file_.reset();
        std::error_code ec;
        std::filesystem::remove(path_, ec);
        return tl::make_unexpected(std::move(err));
    };

    // Write magic header.
    if (auto wr = durable_file_->write(
            reinterpret_cast<std::uint8_t const*>(magic_line.data()),
            magic_line.size()); !wr) {
        return fail_with_unlink(wr.error());
    }
    // Write metablock JSON body.
    if (auto wr = durable_file_->write(
            reinterpret_cast<std::uint8_t const*>(json_body.data()),
            json_body.size()); !wr) {
        return fail_with_unlink(wr.error());
    }
    // fsync.
    if (auto sync = durable_file_->force(); !sync) {
        return fail_with_unlink(sync.error());
    }

    scratch_buffer_.reserve(4096);
    state_ = State::Streaming;
    return {};
}

Result<void> StreamingWriter::close() {
    if (state_ == State::Closed) {
        return tl::make_unexpected(make_error(
            Error::Code::InvalidArgument, "close: writer already closed"));
    }

    Result<void> result{};
    if (state_ == State::Broken) {
        // Sticky error is the result regardless of file-close outcome.
        result = tl::make_unexpected(*sticky_error_);
    }

    if (durable_file_) {
        if (auto cr = durable_file_->close(); !cr && state_ != State::Broken) {
            result = tl::make_unexpected(cr.error());
        }
        durable_file_.reset();
    }

    state_ = State::Closed;
    return result;
}

// ── do_write_block — the I/O gate ────────────────────────────────────

Result<void> StreamingWriter::do_write_block(std::uint8_t const* data,
                                             std::size_t size) {
    if (state_ == State::Broken) {
        return tl::make_unexpected(*sticky_error_);
    }
    if (state_ != State::Streaming) {
        return tl::make_unexpected(make_error(
            Error::Code::InvalidArgument,
            "do_write_block: writer not in Streaming state"));
    }
    if (auto wr = durable_file_->write(data, size); !wr) {
        state_ = State::Broken;
        sticky_error_ = wr.error();
        return tl::make_unexpected(*sticky_error_);
    }
    if (auto sync = durable_file_->force(); !sync) {
        state_ = State::Broken;
        sticky_error_ = sync.error();
        return tl::make_unexpected(*sticky_error_);
    }
    return {};
}

// ── require_* helpers ────────────────────────────────────────────────

std::uint8_t StreamingWriter::sov_for(std::uint16_t channel) const noexcept {
    return (channel < channels_.size())
               ? channels_[channel].size_of_length_value
               : std::uint8_t{2};
}

std::optional<Error> StreamingWriter::require_streaming_state() const {
    if (state_ == State::Broken) return *sticky_error_;
    if (state_ == State::Closed) {
        return make_error(Error::Code::InvalidArgument, "writer is closed");
    }
    if (state_ == State::Configure) {
        return make_error(Error::Code::InvalidArgument,
                          "call start() before write_*");
    }
    return std::nullopt;
}

std::optional<Error> StreamingWriter::require_equidistant_channel(
        std::uint16_t channel, DataType expected) {
    if (auto err = require_streaming_state()) return err;
    if (channel >= channels_.size()) {
        return make_error(Error::Code::InvalidArgument,
                          "channel index out of range");
    }
    auto& st = channel_states_[channel];
    if (st.kind_lock != detail::ChannelState::BlockKindLock::Unset &&
        st.kind_lock != detail::ChannelState::BlockKindLock::Equidistant) {
        return make_error(Error::Code::InvalidBlock,
                          "channel " + std::to_string(channel) +
                              ": mixed block types");
    }
    if (st.datatype_lock != expected) {
        return make_error(Error::Code::DataTypeMismatch,
                          "channel " + std::to_string(channel) +
                              ": datatype mismatch");
    }
    st.kind_lock = detail::ChannelState::BlockKindLock::Equidistant;
    return std::nullopt;
}

std::optional<Error> StreamingWriter::require_timestamped_channel(
        std::uint16_t channel, DataType expected) {
    if (auto err = require_streaming_state()) return err;
    if (channel >= channels_.size()) {
        return make_error(Error::Code::InvalidArgument,
                          "channel index out of range");
    }
    auto& st = channel_states_[channel];
    if (st.kind_lock != detail::ChannelState::BlockKindLock::Unset &&
        st.kind_lock != detail::ChannelState::BlockKindLock::Timestamped) {
        return make_error(Error::Code::InvalidBlock,
                          "channel " + std::to_string(channel) +
                              ": mixed block types");
    }
    if (st.datatype_lock != expected) {
        return make_error(Error::Code::DataTypeMismatch,
                          "channel " + std::to_string(channel) +
                              ": datatype mismatch");
    }
    st.kind_lock = detail::ChannelState::BlockKindLock::Timestamped;
    return std::nullopt;
}

std::optional<Error> StreamingWriter::require_variable_channel(
        std::uint16_t channel, DataType expected) {
    if (auto err = require_streaming_state()) return err;
    if (channel >= channels_.size()) {
        return make_error(Error::Code::InvalidArgument,
                          "channel index out of range");
    }
    auto& st = channel_states_[channel];
    if (st.kind_lock != detail::ChannelState::BlockKindLock::Unset &&
        st.kind_lock != detail::ChannelState::BlockKindLock::Variable) {
        return make_error(Error::Code::InvalidBlock,
                          "channel " + std::to_string(channel) +
                              ": mixed block types");
    }
    if (st.datatype_lock != expected) {
        return make_error(Error::Code::DataTypeMismatch,
                          "channel " + std::to_string(channel) +
                              ": datatype mismatch");
    }
    st.kind_lock = detail::ChannelState::BlockKindLock::Variable;
    return std::nullopt;
}

// ── write_* method stubs — filled in by Tasks 4-6 ────────────────────

#define OSF_STUB_NOT_IMPLEMENTED(method)                                      \
    return tl::make_unexpected(make_error(                                    \
        Error::Code::InvalidArgument, #method ": not implemented yet"))

Result<void> StreamingWriter::start_equidistant_segment(
        std::uint16_t channel, std::int64_t start_ts, double rate,
        float const* samples, std::size_t count) {
    return start_equidistant_segment_impl<float>(channel, start_ts, rate,
                                                  samples, count);
}
Result<void> StreamingWriter::start_equidistant_segment(
        std::uint16_t channel, std::int64_t start_ts, double rate,
        double const* samples, std::size_t count) {
    return start_equidistant_segment_impl<double>(channel, start_ts, rate,
                                                   samples, count);
}
Result<void> StreamingWriter::append_equidistant_samples(
        std::uint16_t channel, float const* samples, std::size_t count) {
    return append_equidistant_samples_impl<float>(channel, samples, count);
}
Result<void> StreamingWriter::append_equidistant_samples(
        std::uint16_t channel, double const* samples, std::size_t count) {
    return append_equidistant_samples_impl<double>(channel, samples, count);
}

Result<void> StreamingWriter::write_timestamped_gps_sample(
        std::uint16_t, std::int64_t, GpsLocation) {
    OSF_STUB_NOT_IMPLEMENTED(write_timestamped_gps_sample);
}
Result<void> StreamingWriter::write_timestamped_gps_samples(
        std::uint16_t, std::int64_t const*, GpsLocation const*, std::size_t) {
    OSF_STUB_NOT_IMPLEMENTED(write_timestamped_gps_samples);
}
Result<void> StreamingWriter::write_timestamped_string(
        std::uint16_t, std::int64_t, std::string_view) {
    OSF_STUB_NOT_IMPLEMENTED(write_timestamped_string);
}
Result<void> StreamingWriter::write_timestamped_binary(
        std::uint16_t, std::int64_t, BinarySample) {
    OSF_STUB_NOT_IMPLEMENTED(write_timestamped_binary);
}

template <typename T>
Result<void> StreamingWriter::write_timestamped_samples_impl(
        std::uint16_t, std::int64_t const*, T const*, std::size_t) {
    OSF_STUB_NOT_IMPLEMENTED(write_timestamped_samples_impl);
}

#undef OSF_STUB_NOT_IMPLEMENTED

// ── start_equidistant_segment_impl<T> / append_equidistant_samples_impl<T> ───

template <typename T>
Result<void> StreamingWriter::start_equidistant_segment_impl(
        std::uint16_t channel, std::int64_t start_ts, double rate,
        T const* samples, std::size_t count) {
    if (count == 0) {
        return tl::make_unexpected(make_error(
            Error::Code::InvalidArgument,
            "start_equidistant_segment: count must be > 0"));
    }
    if (!(rate > 0.0) || !std::isfinite(rate)) {
        return tl::make_unexpected(make_error(
            Error::Code::InvalidArgument,
            "start_equidistant_segment: sample_rate_hz must be a "
            "positive finite double"));
    }
    if (auto err = require_equidistant_channel(
            channel, data_type_for<T>())) {
        return tl::make_unexpected(*err);
    }

    auto const sov = sov_for(channel);
    std::size_t const max_first =
        max_samples_per_start_block(sizeof(T), sov);
    std::size_t const max_cont =
        max_samples_per_continued_block(sizeof(T), sov);

    // First chunk as bcStartData.
    std::size_t const first = std::min(count, max_first);
    scratch_buffer_.clear();
    if (auto enc = osf::detail::encode_start_data<T>(
            scratch_buffer_, channel, sov, start_ts, rate,
            samples, first); !enc) {
        return enc;
    }
    if (auto wr = do_write_block(scratch_buffer_.data(),
                                  scratch_buffer_.size()); !wr) {
        return wr;
    }
    channel_states_[channel].segment_open = true;

    // Remaining chunks as bcContinuedData.
    std::size_t written = first;
    while (written < count) {
        std::size_t const chunk =
            std::min(count - written, max_cont);
        scratch_buffer_.clear();
        if (auto enc = osf::detail::encode_continued_data<T>(
                scratch_buffer_, channel, sov,
                samples + written, chunk); !enc) {
            return enc;
        }
        if (auto wr = do_write_block(scratch_buffer_.data(),
                                      scratch_buffer_.size()); !wr) {
            return wr;
        }
        written += chunk;
    }
    return {};
}

template <typename T>
Result<void> StreamingWriter::append_equidistant_samples_impl(
        std::uint16_t channel, T const* samples, std::size_t count) {
    if (count == 0) {
        return tl::make_unexpected(make_error(
            Error::Code::InvalidArgument,
            "append_equidistant_samples: count must be > 0"));
    }
    if (auto err = require_equidistant_channel(
            channel, data_type_for<T>())) {
        return tl::make_unexpected(*err);
    }
    if (!channel_states_[channel].segment_open) {
        return tl::make_unexpected(make_error(
            Error::Code::InvalidBlock,
            "channel " + std::to_string(channel) +
                ": append without start"));
    }

    auto const sov = sov_for(channel);
    std::size_t const max_cont =
        max_samples_per_continued_block(sizeof(T), sov);

    std::size_t written = 0;
    while (written < count) {
        std::size_t const chunk =
            std::min(count - written, max_cont);
        scratch_buffer_.clear();
        if (auto enc = osf::detail::encode_continued_data<T>(
                scratch_buffer_, channel, sov,
                samples + written, chunk); !enc) {
            return enc;
        }
        if (auto wr = do_write_block(scratch_buffer_.data(),
                                      scratch_buffer_.size()); !wr) {
            return wr;
        }
        written += chunk;
    }
    return {};
}

// Explicit instantiations — float + double only per spec rev
// 2026-05-04 equidistant restriction.
template Result<void>
StreamingWriter::start_equidistant_segment_impl<float>(
    std::uint16_t, std::int64_t, double, float const*, std::size_t);
template Result<void>
StreamingWriter::start_equidistant_segment_impl<double>(
    std::uint16_t, std::int64_t, double, double const*, std::size_t);
template Result<void>
StreamingWriter::append_equidistant_samples_impl<float>(
    std::uint16_t, float const*, std::size_t);
template Result<void>
StreamingWriter::append_equidistant_samples_impl<double>(
    std::uint16_t, double const*, std::size_t);

// ── Explicit instantiations of write_timestamped_samples_impl<T> ─────
// Task 5 will replace the stub above with the real body. This block
// stays — the explicit-instantiation list is the same.

#define OSF_INSTANTIATE_TIMESTAMPED_IMPL(T)                                  \
    template Result<void>                                                    \
    StreamingWriter::write_timestamped_samples_impl<T>(                      \
        std::uint16_t, std::int64_t const*, T const*, std::size_t)

OSF_INSTANTIATE_TIMESTAMPED_IMPL(bool);
OSF_INSTANTIATE_TIMESTAMPED_IMPL(std::int8_t);
OSF_INSTANTIATE_TIMESTAMPED_IMPL(std::int16_t);
OSF_INSTANTIATE_TIMESTAMPED_IMPL(std::int32_t);
OSF_INSTANTIATE_TIMESTAMPED_IMPL(std::int64_t);
OSF_INSTANTIATE_TIMESTAMPED_IMPL(std::uint8_t);
OSF_INSTANTIATE_TIMESTAMPED_IMPL(std::uint16_t);
OSF_INSTANTIATE_TIMESTAMPED_IMPL(std::uint32_t);
OSF_INSTANTIATE_TIMESTAMPED_IMPL(std::uint64_t);
OSF_INSTANTIATE_TIMESTAMPED_IMPL(float);
OSF_INSTANTIATE_TIMESTAMPED_IMPL(double);

#undef OSF_INSTANTIATE_TIMESTAMPED_IMPL

}  // namespace osf
