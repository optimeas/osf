// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

#include "writer_common.hpp"

#include "osf/types.hpp"

namespace osf::detail {

std::size_t max_samples_per_start_block(std::size_t value_size,
                                        std::uint8_t sov) noexcept {
    constexpr std::size_t OVERHEAD = 1u + 8u + 8u + 4u;
    std::size_t const max_payload = max_payload_for_sov(sov);
    if (max_payload <= OVERHEAD) return 1;
    std::size_t const samples = (max_payload - OVERHEAD) / value_size;
    return (samples == 0) ? 1u : samples;
}

std::size_t max_samples_per_continued_block(std::size_t value_size,
                                            std::uint8_t sov) noexcept {
    constexpr std::size_t OVERHEAD = 1u + 4u;
    std::size_t const max_payload = max_payload_for_sov(sov);
    if (max_payload <= OVERHEAD) return 1;
    std::size_t const samples = (max_payload - OVERHEAD) / value_size;
    return (samples == 0) ? 1u : samples;
}

std::size_t max_samples_per_timestamped_block(std::size_t value_size,
                                              std::uint8_t sov) noexcept {
    constexpr std::size_t OVERHEAD = 1u + 4u;
    std::size_t const per_sample = 8u + value_size;
    std::size_t const max_payload = max_payload_for_sov(sov);
    if (max_payload <= OVERHEAD) return 1;
    std::size_t const samples = (max_payload - OVERHEAD) / per_sample;
    return (samples == 0) ? 1u : samples;
}

std::size_t variable_sample_capacity(std::uint8_t sov) noexcept {
    std::size_t const max_payload = max_payload_for_sov(sov);
    return (max_payload <= VARIABLE_BLOCK_OVERHEAD_BYTES)
               ? 0u
               : (max_payload - VARIABLE_BLOCK_OVERHEAD_BYTES);
}

MetaBlock build_metablock(FileInfoDraft const& fi,
                          std::vector<ChannelDef> const& channels) {
    MetaBlock meta;
    meta.file_info.version              = 5;
    meta.file_info.creator             = fi.creator;
    meta.file_info.tag                 = fi.tag;
    meta.file_info.reason              = fi.reason;
    meta.file_info.created_at_latitude  = fi.created_at_latitude;
    meta.file_info.created_at_longitude = fi.created_at_longitude;
    meta.file_info.created_at_altitude  = fi.created_at_altitude;
    meta.file_info.namespace_sep       = fi.namespace_sep;
    meta.file_info.comment             = fi.comment;

    for (std::size_t i = 0; i < channels.size(); ++i) {
        ChannelDef const& d = channels[i];
        Channel chx;
        chx.index = static_cast<std::uint16_t>(i);
        chx.name  = d.name;
        chx.data_type = d.data_type;
        // Normalise: equidistant stays equidistant; everything else is
        // scalar (Delphi reference convention; BACKLOG Task-7 #2).
        chx.channel_type = (d.channel_type == ChannelType::Equidistant)
                               ? ChannelType::Equidistant
                               : ChannelType::Scalar;
        chx.size_of_length_value = d.size_of_length_value;
        chx.time_increment_ns = d.time_increment_ns;
        chx.physical_unit = d.physical_unit;
        chx.physical_dimension = d.physical_dimension;
        chx.display_name = d.display_name;
        chx.mime_type = d.mime_type;
        chx.reference = d.reference;
        chx.comment = d.comment;
        meta.channels.push_back(std::move(chx));
    }
    return meta;
}

}  // namespace osf::detail
