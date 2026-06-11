// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

/// \file error.hpp
/// `osf::Error` and `osf::Result<T>` — the foundation error type used by
/// every fallible operation in the OSF C++ library.
///
/// The core API is exception-free: every operation that can fail returns
/// `Result<T>` (= `tl::expected<T, Error>`); the caller checks the result
/// before using the value. Consumers who prefer exceptions opt in via
/// `<osf/throwing.hpp>` (`osf::throwing::unwrap` and friends), which is
/// layered on top of this header — never the other way around.
///
/// See DECISIONS.md §20 for the rationale (Result<T> as the core API,
/// throwing wrappers as opt-in).

#pragma once

#include <string>
#include <string_view>
#include <utility>

#include <tl/expected.hpp>

namespace osf {

/// Structured error: a stable category `Code` plus a human-readable
/// `message` with the failure detail (offending value, channel index,
/// parser diagnostic, …). Compare `code` programmatically; treat
/// `message` as display-only text whose wording may change.
struct Error {
    enum class Code {
        /// Fallback when no more specific category applies. Also the
        /// code of a default-constructed `Error`.
        Unknown,
        /// The caller violated an API precondition: invalid
        /// `ChannelDef`, unknown channel index passed to a writer,
        /// zero-count write, writer used outside its lifecycle phase,
        /// non-positive sample rate, ….
        InvalidArgument,
        /// The underlying stream / file signalled a failure: file not
        /// openable, read error, write error, fsync failure.
        IoError,
        /// Generic parse failure that is neither a magic-header nor a
        /// metablock problem. Rarely used; the more specific codes
        /// below are preferred.
        ParseError,
        /// A requested entity does not exist (reserved for lookup-style
        /// APIs; channel lookups on `DataManager` return `nullptr`
        /// instead).
        NotFound,
        /// The first line of the stream is not a well-formed OSF magic
        /// header (missing separator, unparseable length, non-ASCII
        /// garbage). Almost always means "this is not an OSF file".
        InvalidMagicHeader,
        /// The magic-header line parses, but its identifier is none of
        /// the four accepted spellings (`OSF4`, `OSF5`,
        /// `OCEAN_STREAM_FORMAT4`, `OCEAN_STREAMING_FORMAT4`).
        UnsupportedVersion,
        /// No newline within `MAX_MAGIC_HEADER_LEN` bytes — the file
        /// cannot start with a valid OSF magic header.
        MagicHeaderTooLong,
        /// Metablock body was structurally malformed: missing required
        /// field, unparseable number, invalid `sizeoflengthvalue`, etc.
        /// The metablock is the contract for the binary blocks that
        /// follow, so a malformed metablock rejects the whole file.
        InvalidMetablock,
        /// Metablock referenced a string or datatype that was removed in
        /// spec revision 2026-05-04 (`pair`, `triple`, `candata`,
        /// `gpsdata`). The block decoder cannot reproduce the obsolete
        /// payload layout from a current build, so this is rejected
        /// rather than best-effort decoded.
        RemovedInSpec,
        /// JSON parser (nlohmann::json) could not tokenise the metablock
        /// body. Carries the parser's diagnostic in `message`.
        JsonParseError,
        /// XML parser (pugixml) could not tokenise the metablock body.
        /// Carries the parser's diagnostic plus byte offset in `message`.
        XmlParseError,
        /// The block stream referenced a channel index that does not
        /// appear in the metablock. Without the channel definition the
        /// reader cannot know how wide the length prefix is, so this
        /// is a hard error rather than a graceful skip — the file is
        /// corrupted. The index is included in `message`.
        UnknownChannelIndex,
        /// The block-stream payload was structurally malformed (wrong
        /// length for the declared data type, required field missing,
        /// equidistant block on a string/binary channel, …). Carries a
        /// per-block diagnostic in `message`.
        InvalidBlock,
        /// The same channel produced both equidistant blocks
        /// (`bcStartData` / `bcContinuedData`) and timestamped blocks
        /// (`bcAbsTimeStampData` / `bcContinuedRelStampData`). Spec
        /// revision 2026-05-04 forbids the mix per channel. Surfaced
        /// by the future `DataManager` layer; reserved here for shared
        /// use.
        ChannelMixedBlockTypes,
        /// A `bcContinuedData` block arrived for a channel that has not
        /// yet seen a `bcStartData`. Equidistant continuation depends
        /// on the most recent start block for its sample rate, so
        /// without an open segment the data has no meaningful timeline.
        /// Surfaced by the future `DataManager`.
        ContinuedDataWithoutStart,
        /// A `bcContinuedRelStampData` block arrived for a channel
        /// that has not yet observed an absolute timestamp. The first
        /// relative delta is anchored to the channel's last known
        /// absolute time; without an anchor the deltas cannot be
        /// lifted to absolute time. Surfaced by the future `DataManager`.
        RelStampWithoutAnchor,
        /// A block payload's typed variant did not match the channel's
        /// declared `data_type`. The reader normally enforces this at
        /// the stream level; surfaced here so the future `DataManager`
        /// has a typed code for the equivalent check.
        DataTypeMismatch,
    };

    /// Stable error category — the field to branch on.
    Code code = Code::Unknown;
    /// Human-readable detail (offending value, channel index, parser
    /// diagnostic). Display-only; wording is not part of the API.
    std::string message;

    Error() = default;
    Error(Code c, std::string msg) : code(c), message(std::move(msg)) {}
};

/// Return type of every fallible operation in the core API.
///
/// Usage idiom:
/// ```cpp
/// auto r = osf::DataManager::load_from_file(path);
/// if (!r) { /* r.error().code / r.error().message */ }
/// osf::DataManager const& mgr = *r;   // or r.value()
/// ```
template <typename T>
using Result = tl::expected<T, Error>;

// Returns a stable string identifier for the given Code, suitable
// for logging. The returned view points into static storage.
[[nodiscard]] std::string_view error_category_name(Error::Code code) noexcept;

}  // namespace osf
