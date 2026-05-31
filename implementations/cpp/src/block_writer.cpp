// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

#include "osf/block_writer.hpp"

#include "writer_common.hpp"
#include "osf/metablock.hpp"

#include <fstream>
#include <utility>

namespace osf {

// Minimal storage for Task 3 — real typed sample buffers land in Task 4.
struct BlockWriter::ChannelData {
    DataType datatype_lock = DataType::Unsupported;
};

// ── Special members (defined out-of-line so vector<ChannelData> can
//    instantiate against the now-complete ChannelData type) ────────────

BlockWriter::BlockWriter()                                     = default;
BlockWriter::~BlockWriter()                                    = default;
BlockWriter::BlockWriter(BlockWriter const&)                   = default;
BlockWriter& BlockWriter::operator=(BlockWriter const&)        = default;
BlockWriter::BlockWriter(BlockWriter&&) noexcept               = default;
BlockWriter& BlockWriter::operator=(BlockWriter&&) noexcept    = default;

// ── Internal helpers ─────────────────────────────────────────────────

namespace {

Error make_error(Error::Code code, std::string msg) {
    return Error{code, std::move(msg)};
}

}  // namespace

// ── File-info setters ────────────────────────────────────────────────

void BlockWriter::set_creator(std::string v)       { file_info_.creator       = std::move(v); }
void BlockWriter::set_tag(std::string v)           { file_info_.tag           = std::move(v); }
void BlockWriter::set_reason(std::string v)        { file_info_.reason        = std::move(v); }
void BlockWriter::set_namespace_sep(std::string v) { file_info_.namespace_sep = std::move(v); }
void BlockWriter::set_comment(std::string v)       { file_info_.comment       = std::move(v); }

void BlockWriter::set_location(double lat, double lon, double alt) {
    file_info_.created_at_latitude  = lat;
    file_info_.created_at_longitude = lon;
    file_info_.created_at_altitude  = alt;
}

// ── add_channel ──────────────────────────────────────────────────────

Result<std::uint16_t> BlockWriter::add_channel(ChannelDef def) {
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
    ChannelData cd;
    cd.datatype_lock = def.data_type;
    name_to_index_.emplace(def.name, idx);
    channels_.push_back(std::move(def));
    channel_data_.push_back(std::move(cd));
    return idx;
}

// ── channel_count / channel_index ───────────────────────────────────

std::size_t BlockWriter::channel_count() const noexcept {
    return channels_.size();
}

std::optional<std::uint16_t>
BlockWriter::channel_index(std::string_view name) const {
    auto it = name_to_index_.find(std::string{name});
    if (it == name_to_index_.end()) return std::nullopt;
    return it->second;
}

// ── write_to / write_to_file ─────────────────────────────────────────

Result<void> BlockWriter::write_to(std::ostream& out) const {
    if (channels_.empty()) {
        return tl::make_unexpected(make_error(
            Error::Code::InvalidArgument,
            "write_to: no channels declared"));
    }
    (void) out;   // real emit lands in Task 4
    return {};
}

Result<void> BlockWriter::write_to_file(std::filesystem::path path) const {
    std::ofstream f(path, std::ios::binary);
    if (!f) {
        return tl::make_unexpected(make_error(
            Error::Code::IoError,
            "write_to_file: cannot open " + path.string()));
    }
    return write_to(f);
}

}  // namespace osf
