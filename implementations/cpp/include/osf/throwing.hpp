// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

/**
 * @file throwing.hpp
 * @brief Opt-in, exception-throwing convenience layer over the
 *        Result-based core API (DECISIONS §20).
 *
 * The entire OSF C++ core API is `Result<T>` (= `tl::expected<T, Error>`)
 * based: every fallible operation returns a `Result` the caller inspects.
 * Some consumers prefer RAII-style error propagation via exceptions. This
 * header exposes the same high-level operations as throwing functions,
 * plus an `unwrap` escape-hatch that turns any `Result<T>` into a value
 * (or an `osf::Exception`).
 *
 * Header-only and opt-in: consumers who never include `<osf/throwing.hpp>`
 * never pull in this layer. It is intentionally NOT part of the
 * `<osf/osf.hpp>` umbrella and is not compiled into the `osf` library.
 *
 * @code
 * #include <osf/throwing.hpp>
 * try {
 *     auto manager = osf::throwing::load("data.osf");
 *     osf::throwing::write_to_file(manager, "out.osf");
 *
 *     osf::StreamingWriter w{path};
 *     osf::throwing::unwrap(w.start());           // throws on error
 *     osf::throwing::unwrap(
 *         w.write_timestamped_sample<double>(ch, ts, value));
 * } catch (osf::Exception const& e) {
 *     log(e.code(), e.what());
 * }
 * @endcode
 */

#pragma once

#include "osf/block_writer.hpp"   // ::osf::write_to_file / write_to(DataManager, …)
#include "osf/error.hpp"
#include "osf/manager.hpp"        // DataManager::load_from_file / load_from_stream

#include <filesystem>
#include <istream>
#include <ostream>
#include <stdexcept>
#include <string>
#include <utility>

namespace osf {

/**
 * @brief Exception carrying an `osf::Error`, thrown by the `osf::throwing`
 *        layer.
 *
 * `what()` is the underlying `Error`'s message (or the stable category
 * name when the message is empty); `code()` / `error()` expose the
 * structured detail. Lives in namespace `osf` (not `osf::throwing`) per
 * DECISIONS §20.
 */
class Exception : public std::runtime_error {
public:
    explicit Exception(Error err)
        : std::runtime_error(
              err.message.empty()
                  ? std::string(error_category_name(err.code))
                  : err.message),
          error_(std::move(err)) {}

    /// The full structured error.
    [[nodiscard]] Error const& error() const noexcept { return error_; }

    /// The error category code.
    [[nodiscard]] Error::Code code() const noexcept { return error_.code; }

private:
    Error error_;
};

namespace throwing {

/**
 * @brief Return the value of a successful `Result`, or throw
 *        `osf::Exception` carrying its `Error`.
 *
 * Works on any `Result<T>` from the core API — including the writer
 * methods, which keeps the throwing layer thin (no per-method wrappers):
 * @code
 * auto idx = osf::throwing::unwrap(writer.add_channel(def));  // -> uint16_t
 * osf::throwing::unwrap(writer.start());                      // -> void
 * @endcode
 *
 * The `Result` is taken by value: a prvalue from a call expression moves
 * in and out; an lvalue is copied in (rare).
 */
template <typename T>
T unwrap(Result<T> r) {
    if (r) {
        return std::move(r).value();
    }
    throw Exception(std::move(r).error());
}

/// `Result<void>` overload (preferred over the template for void).
inline void unwrap(Result<void> r) {
    if (!r) {
        throw Exception(std::move(r).error());
    }
}

// ── Read ──────────────────────────────────────────────────────────────

/// Load an OSF / OSFZ file, or throw `osf::Exception`.
inline DataManager load(std::filesystem::path const& path) {
    return unwrap(DataManager::load_from_file(path));
}

/// Load from a seekable input stream, or throw `osf::Exception`.
inline DataManager load(std::istream& in) {
    return unwrap(DataManager::load_from_stream(in));
}

// ── Write (always OSF5 — DECISIONS §6) ────────────────────────────────

/// Write \p mgr to \p path as OSF5, or throw `osf::Exception`.
inline void write_to_file(DataManager const& mgr,
                          std::filesystem::path path) {
    // Fully qualified to call the core (non-throwing) convenience
    // function, not recurse into this overload.
    unwrap(::osf::write_to_file(mgr, std::move(path)));
}

/// Write \p mgr to \p out as OSF5, or throw `osf::Exception`.
inline void write_to(DataManager const& mgr, std::ostream& out) {
    unwrap(::osf::write_to(mgr, out));
}

}  // namespace throwing
}  // namespace osf
