// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

#include "osf/streamingwriter.h"

#include "blockencode_p.h"           // osf::detail::encode_*
#include "durablefile_p.h"           // osf::detail::DurableFile
#include "writercommon_p.h"          // osf::detail chunking helpers + constants
#include "osf/metablock.h"          // FileInfo, Channel, MetaBlock, serializeMetablockJson

#include <algorithm>
#include <cassert>
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
    DataType      datatype_lock = DataType::Unsupported;   // copied from ChannelDef.dataType
    bool          segment_open = false;                     // equidistant only
};

}  // namespace detail

namespace {

Error make_error(Error::Code code, std::string msg) {
    return Error{code, std::move(msg)};
}

// Map a supported template T to its DataType enum. Compile-time
// dispatch; used by requireTimestampedChannel in Tasks 4–6.
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

void StreamingWriter::setCreator(std::string value)       { creator_   = std::move(value); }
void StreamingWriter::setTag(std::string value)           { tag_       = std::move(value); }
void StreamingWriter::setReason(std::string value)        { reason_    = std::move(value); }
void StreamingWriter::setNamespaceSep(std::string value) { namespace_sep_ = std::move(value); }
void StreamingWriter::setComment(std::string value)       { comment_   = std::move(value); }

void StreamingWriter::setLocation(double latitude, double longitude,
                                   double altitude) {
    created_at_latitude_  = latitude;
    created_at_longitude_ = longitude;
    created_at_altitude_  = altitude;
}

// ── addChannel ──────────────────────────────────────────────────────

Result<std::uint16_t> StreamingWriter::addChannel(ChannelDef def) {
    if (state_ != State::Configure) {
        return tl::make_unexpected(make_error(
            Error::Code::InvalidArgument,
            "addChannel: writer is past the Configure phase"));
    }
    if (def.sizeOfLengthValue != 2 && def.sizeOfLengthValue != 4) {
        return tl::make_unexpected(make_error(
            Error::Code::InvalidArgument,
            "addChannel: sizeOfLengthValue must be 2 or 4"));
    }
    if (def.dataType == DataType::Unsupported) {
        return tl::make_unexpected(make_error(
            Error::Code::InvalidArgument,
            "addChannel: dataType Unsupported is not writeable"));
    }
    if (def.channelType == ChannelType::Unsupported) {
        return tl::make_unexpected(make_error(
            Error::Code::InvalidArgument,
            "addChannel: channelType Unsupported is not writeable"));
    }
    if (channels_.size() >= 0xFFFF) {
        return tl::make_unexpected(make_error(
            Error::Code::InvalidArgument,
            "addChannel: too many channels (max 65535)"));
    }

    auto const idx = static_cast<std::uint16_t>(channels_.size());
    detail::ChannelState st;
    st.datatype_lock = def.dataType;
    channels_.push_back(std::move(def));
    channel_states_.push_back(st);
    return idx;
}

// ── start / close ────────────────────────────────────────────────────

Result<void> StreamingWriter::start() {
    if (state_ == State::Broken) {
        return tl::make_unexpected(*sticky_error_);
    }
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
    detail::FileInfoDraft fi;
    fi.creator = creator_;
    fi.tag = tag_;
    fi.reason = reason_;
    fi.createdAtLatitude = created_at_latitude_;
    fi.createdAtLongitude = created_at_longitude_;
    fi.createdAtAltitude = created_at_altitude_;
    fi.namespaceSep = namespace_sep_;
    fi.comment = comment_;
    MetaBlock meta = detail::build_metablock(fi, channels_);

    // Serialize the metablock and build the magic-header line.
    std::string const json_body = serializeMetablockJson(meta);
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

// ── doWriteBlock — the I/O gate ────────────────────────────────────

Result<void> StreamingWriter::doWriteBlock(std::uint8_t const* data,
                                             std::size_t size) {
    if (state_ == State::Broken) {
        return tl::make_unexpected(*sticky_error_);
    }
    if (state_ != State::Streaming) {
        return tl::make_unexpected(make_error(
            Error::Code::InvalidArgument,
            "doWriteBlock: writer not in Streaming state"));
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

std::uint8_t StreamingWriter::sovFor(std::uint16_t channel) const noexcept {
    assert(channel < channels_.size());
    return channels_[channel].sizeOfLengthValue;
}

std::optional<Error> StreamingWriter::requireStreamingState() const {
    if (state_ == State::Broken) return *sticky_error_;
    if (state_ == State::Closed) {
        return make_error(Error::Code::InvalidArgument, "writer is closed");
    }
    if (state_ == State::Configure) {
        return make_error(Error::Code::InvalidArgument,
                          "call start() before write*");
    }
    return std::nullopt;
}

std::optional<Error> StreamingWriter::requireEquidistantChannel(
        std::uint16_t channel, DataType expected) {
    if (auto err = requireStreamingState()) return err;
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

std::optional<Error> StreamingWriter::requireTimestampedChannel(
        std::uint16_t channel, DataType expected) {
    if (auto err = requireStreamingState()) return err;
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

std::optional<Error> StreamingWriter::requireVariableChannel(
        std::uint16_t channel, DataType expected) {
    if (auto err = requireStreamingState()) return err;
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

// ── Equidistant + Timestamped numeric forwarding methods ────────────
//
// The public non-template methods forward to the templated *_impl
// bodies that live further down. The GPS + Variable methods are
// non-template entry points with their own bodies.

Result<void> StreamingWriter::startEquidistantSegment(
        std::uint16_t channel, std::int64_t start_ts, double rate,
        float const* samples, std::size_t count) {
    return startEquidistantSegmentImpl<float>(channel, start_ts, rate,
                                                  samples, count);
}
Result<void> StreamingWriter::startEquidistantSegment(
        std::uint16_t channel, std::int64_t start_ts, double rate,
        double const* samples, std::size_t count) {
    return startEquidistantSegmentImpl<double>(channel, start_ts, rate,
                                                   samples, count);
}
Result<void> StreamingWriter::appendEquidistantSamples(
        std::uint16_t channel, float const* samples, std::size_t count) {
    return appendEquidistantSamplesImpl<float>(channel, samples, count);
}
Result<void> StreamingWriter::appendEquidistantSamples(
        std::uint16_t channel, double const* samples, std::size_t count) {
    return appendEquidistantSamplesImpl<double>(channel, samples, count);
}

// ── GPS array ─────────────────────────────────────────────────────

Result<void> StreamingWriter::writeTimestampedGpsSamples(
        std::uint16_t channel, std::int64_t const* timestampsNs,
        GpsLocation const* values, std::size_t count) {
    if (count == 0) {
        return tl::make_unexpected(make_error(
            Error::Code::InvalidArgument,
            "writeTimestampedGpsSamples: count must be > 0"));
    }
    if (auto err = requireTimestampedChannel(
            channel, DataType::GpsLocation)) {
        return tl::make_unexpected(*err);
    }

    auto const sov = sovFor(channel);
    // GPS wire-format per sample: GPS_WIRE_SIZE bytes (3 little-endian
    // doubles for latitude, longitude, altitude per block.h:57-72).
    std::size_t const max_per_block =
        osf::detail::max_samples_per_timestamped_block(
            /*value_size=*/osf::detail::GPS_WIRE_SIZE, sov);

    std::size_t written = 0;
    while (written < count) {
        std::size_t const chunk =
            std::min(count - written, max_per_block);
        scratch_buffer_.clear();
        if (auto enc = osf::detail::encode_abs_timestamp_data_gps(
                scratch_buffer_, channel, sov,
                timestampsNs + written, values + written, chunk); !enc) {
            return enc;
        }
        if (auto wr = doWriteBlock(scratch_buffer_.data(),
                                      scratch_buffer_.size()); !wr) {
            return wr;
        }
        written += chunk;
    }
    return {};
}

// ── GPS scalar — forwards to array ────────────────────────────────

Result<void> StreamingWriter::writeTimestampedGpsSample(
        std::uint16_t channel, std::int64_t timestampNs,
        GpsLocation value) {
    return writeTimestampedGpsSamples(channel, &timestampNs,
                                         &value, 1);
}

// ── Variable (string + binary) — single-sample per spec ──────────

Result<void> StreamingWriter::writeTimestampedString(
        std::uint16_t channel, std::int64_t timestampNs,
        std::string_view value) {
    if (auto err = requireVariableChannel(channel, DataType::String)) {
        return tl::make_unexpected(*err);
    }
    auto const sov = sovFor(channel);
    std::size_t const capacity = osf::detail::variable_sample_capacity(sov);
    if (value.size() > capacity) {
        return tl::make_unexpected(make_error(
            Error::Code::InvalidBlock,
            "channel " + std::to_string(channel) +
                ": variable sample size " + std::to_string(value.size()) +
                " bytes exceeds the maximum single-block payload (" +
                std::to_string(capacity) + " bytes for sizeoflengthvalue=" +
                std::to_string(sov) + "). Declare sizeoflengthvalue=4 at "
                "addChannel() time for channels that may carry larger "
                "payloads."));
    }
    scratch_buffer_.clear();
    if (auto enc = osf::detail::encode_abs_timestamp_data(
            scratch_buffer_, channel, sov, timestampNs, value); !enc) {
        return enc;
    }
    return doWriteBlock(scratch_buffer_.data(), scratch_buffer_.size());
}

Result<void> StreamingWriter::writeTimestampedBinary(
        std::uint16_t channel, std::int64_t timestampNs,
        BinarySample value) {
    if (auto err = requireVariableChannel(channel, DataType::Binary)) {
        return tl::make_unexpected(*err);
    }
    auto const sov = sovFor(channel);
    std::size_t const capacity = osf::detail::variable_sample_capacity(sov);
    if (value.size > capacity) {
        return tl::make_unexpected(make_error(
            Error::Code::InvalidBlock,
            "channel " + std::to_string(channel) +
                ": variable sample size " + std::to_string(value.size) +
                " bytes exceeds the maximum single-block payload (" +
                std::to_string(capacity) + " bytes for sizeoflengthvalue=" +
                std::to_string(sov) + "). Declare sizeoflengthvalue=4 at "
                "addChannel() time for channels that may carry larger "
                "payloads."));
    }
    scratch_buffer_.clear();
    if (auto enc = osf::detail::encode_abs_timestamp_data(
            scratch_buffer_, channel, sov, timestampNs, value); !enc) {
        return enc;
    }
    return doWriteBlock(scratch_buffer_.data(), scratch_buffer_.size());
}

// ── writeTimestampedSamplesImpl<T> ────────────────────────────────

template <typename T>
Result<void> StreamingWriter::writeTimestampedSamplesImpl(
        std::uint16_t channel, std::int64_t const* timestampsNs,
        T const* values, std::size_t count) {
    if (count == 0) {
        return tl::make_unexpected(make_error(
            Error::Code::InvalidArgument,
            "writeTimestampedSamples: count must be > 0"));
    }
    if (auto err = requireTimestampedChannel(
            channel, data_type_for<T>())) {
        return tl::make_unexpected(*err);
    }

    auto const sov = sovFor(channel);
    std::size_t const max_per_block =
        osf::detail::max_samples_per_timestamped_block(sizeof(T), sov);

    std::size_t written = 0;
    while (written < count) {
        std::size_t const chunk =
            std::min(count - written, max_per_block);
        scratch_buffer_.clear();
        if (auto enc = osf::detail::encode_abs_timestamp_data<T>(
                scratch_buffer_, channel, sov,
                timestampsNs + written, values + written, chunk); !enc) {
            return enc;
        }
        if (auto wr = doWriteBlock(scratch_buffer_.data(),
                                      scratch_buffer_.size()); !wr) {
            return wr;
        }
        written += chunk;
    }
    return {};
}

// ── startEquidistantSegmentImpl<T> / appendEquidistantSamplesImpl<T> ───

template <typename T>
Result<void> StreamingWriter::startEquidistantSegmentImpl(
        std::uint16_t channel, std::int64_t start_ts, double rate,
        T const* samples, std::size_t count) {
    if (count == 0) {
        return tl::make_unexpected(make_error(
            Error::Code::InvalidArgument,
            "startEquidistantSegment: count must be > 0"));
    }
    if (!(rate > 0.0) || !std::isfinite(rate)) {
        return tl::make_unexpected(make_error(
            Error::Code::InvalidArgument,
            "startEquidistantSegment: sampleRateHz must be a "
            "positive finite double"));
    }
    if (auto err = requireEquidistantChannel(
            channel, data_type_for<T>())) {
        return tl::make_unexpected(*err);
    }

    auto const sov = sovFor(channel);
    std::size_t const max_first =
        osf::detail::max_samples_per_start_block(sizeof(T), sov);
    std::size_t const max_cont =
        osf::detail::max_samples_per_continued_block(sizeof(T), sov);

    // First chunk as bcStartData.
    std::size_t const first = std::min(count, max_first);
    scratch_buffer_.clear();
    if (auto enc = osf::detail::encode_start_data<T>(
            scratch_buffer_, channel, sov, start_ts, rate,
            samples, first); !enc) {
        return enc;
    }
    if (auto wr = doWriteBlock(scratch_buffer_.data(),
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
        if (auto wr = doWriteBlock(scratch_buffer_.data(),
                                      scratch_buffer_.size()); !wr) {
            return wr;
        }
        written += chunk;
    }
    return {};
}

template <typename T>
Result<void> StreamingWriter::appendEquidistantSamplesImpl(
        std::uint16_t channel, T const* samples, std::size_t count) {
    if (count == 0) {
        return tl::make_unexpected(make_error(
            Error::Code::InvalidArgument,
            "appendEquidistantSamples: count must be > 0"));
    }
    if (auto err = requireEquidistantChannel(
            channel, data_type_for<T>())) {
        return tl::make_unexpected(*err);
    }
    if (!channel_states_[channel].segment_open) {
        return tl::make_unexpected(make_error(
            Error::Code::InvalidBlock,
            "channel " + std::to_string(channel) +
                ": append without start"));
    }

    auto const sov = sovFor(channel);
    std::size_t const max_cont =
        osf::detail::max_samples_per_continued_block(sizeof(T), sov);

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
        if (auto wr = doWriteBlock(scratch_buffer_.data(),
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
StreamingWriter::startEquidistantSegmentImpl<float>(
    std::uint16_t, std::int64_t, double, float const*, std::size_t);
template Result<void>
StreamingWriter::startEquidistantSegmentImpl<double>(
    std::uint16_t, std::int64_t, double, double const*, std::size_t);
template Result<void>
StreamingWriter::appendEquidistantSamplesImpl<float>(
    std::uint16_t, float const*, std::size_t);
template Result<void>
StreamingWriter::appendEquidistantSamplesImpl<double>(
    std::uint16_t, double const*, std::size_t);

// ── Explicit instantiations of writeTimestampedSamplesImpl<T> ─────
// Explicit instantiations for the 11 numeric types supported by the
// writeTimestampedSample / writeTimestampedSamples public API.

#define OSF_INSTANTIATE_TIMESTAMPED_IMPL(T)                                  \
    template Result<void>                                                    \
    StreamingWriter::writeTimestampedSamplesImpl<T>(                      \
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
