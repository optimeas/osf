// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

/**
 * @file binaryio_p.h
 * @brief File-private little-endian byte-conversion helpers.
 *
 * Read and write halves are kept fully symmetric so the next
 * file touching binary I/O is not tempted to bypass the
 * abstraction. readLeI8 / writeLeI8 are trivial single-byte
 * operations but ship as free functions for naming consistency.
 *
 * All functions are noexcept. The pointer arguments must refer
 * to at least sizeof(T) bytes of storage; no bounds checking
 * happens here — that is the caller's responsibility.
 *
 * Float helpers use std::memcpy to move bit patterns between
 * floating-point and integer representations, avoiding
 * strict-aliasing UB. Compilers fold this to a single mov.
 */

#ifndef OSF_DETAIL_BINARY_IO_HPP
#define OSF_DETAIL_BINARY_IO_HPP

#include <cstdint>
#include <cstring>

namespace osf::detail {

// ── Reads ───────────────────────────────────────────────────────────

inline std::int8_t readLeI8(std::uint8_t const* p) noexcept {
    return static_cast<std::int8_t>(*p);
}

inline std::uint16_t readLeU16(std::uint8_t const* p) noexcept {
    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(p[0]) |
        (static_cast<std::uint16_t>(p[1]) << 8));
}

inline std::uint32_t readLeU32(std::uint8_t const* p) noexcept {
    return static_cast<std::uint32_t>(p[0]) |
           (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) |
           (static_cast<std::uint32_t>(p[3]) << 24);
}

inline std::uint64_t readLeU64(std::uint8_t const* p) noexcept {
    return static_cast<std::uint64_t>(p[0]) |
           (static_cast<std::uint64_t>(p[1]) << 8) |
           (static_cast<std::uint64_t>(p[2]) << 16) |
           (static_cast<std::uint64_t>(p[3]) << 24) |
           (static_cast<std::uint64_t>(p[4]) << 32) |
           (static_cast<std::uint64_t>(p[5]) << 40) |
           (static_cast<std::uint64_t>(p[6]) << 48) |
           (static_cast<std::uint64_t>(p[7]) << 56);
}

inline std::int16_t readLeI16(std::uint8_t const* p) noexcept {
    return static_cast<std::int16_t>(readLeU16(p));
}

inline std::int32_t readLeI32(std::uint8_t const* p) noexcept {
    return static_cast<std::int32_t>(readLeU32(p));
}

inline std::int64_t readLeI64(std::uint8_t const* p) noexcept {
    return static_cast<std::int64_t>(readLeU64(p));
}

inline float readLeF32(std::uint8_t const* p) noexcept {
    std::uint32_t const bits = readLeU32(p);
    float v;
    std::memcpy(&v, &bits, sizeof(v));
    return v;
}

inline double readLeF64(std::uint8_t const* p) noexcept {
    std::uint64_t const bits = readLeU64(p);
    double v;
    std::memcpy(&v, &bits, sizeof(v));
    return v;
}

// ── Writes ──────────────────────────────────────────────────────────

inline void writeLeI8(std::uint8_t* p, std::int8_t v) noexcept {
    *p = static_cast<std::uint8_t>(v);
}

inline void writeLeU16(std::uint8_t* p, std::uint16_t v) noexcept {
    p[0] = static_cast<std::uint8_t>(v & 0xFF);
    p[1] = static_cast<std::uint8_t>((v >> 8) & 0xFF);
}

inline void writeLeU32(std::uint8_t* p, std::uint32_t v) noexcept {
    p[0] = static_cast<std::uint8_t>(v & 0xFF);
    p[1] = static_cast<std::uint8_t>((v >> 8) & 0xFF);
    p[2] = static_cast<std::uint8_t>((v >> 16) & 0xFF);
    p[3] = static_cast<std::uint8_t>((v >> 24) & 0xFF);
}

inline void writeLeU64(std::uint8_t* p, std::uint64_t v) noexcept {
    p[0] = static_cast<std::uint8_t>(v & 0xFF);
    p[1] = static_cast<std::uint8_t>((v >> 8) & 0xFF);
    p[2] = static_cast<std::uint8_t>((v >> 16) & 0xFF);
    p[3] = static_cast<std::uint8_t>((v >> 24) & 0xFF);
    p[4] = static_cast<std::uint8_t>((v >> 32) & 0xFF);
    p[5] = static_cast<std::uint8_t>((v >> 40) & 0xFF);
    p[6] = static_cast<std::uint8_t>((v >> 48) & 0xFF);
    p[7] = static_cast<std::uint8_t>((v >> 56) & 0xFF);
}

inline void writeLeI16(std::uint8_t* p, std::int16_t v) noexcept {
    writeLeU16(p, static_cast<std::uint16_t>(v));
}

inline void writeLeI32(std::uint8_t* p, std::int32_t v) noexcept {
    writeLeU32(p, static_cast<std::uint32_t>(v));
}

inline void writeLeI64(std::uint8_t* p, std::int64_t v) noexcept {
    writeLeU64(p, static_cast<std::uint64_t>(v));
}

inline void writeLeF32(std::uint8_t* p, float v) noexcept {
    std::uint32_t bits;
    std::memcpy(&bits, &v, sizeof(bits));
    writeLeU32(p, bits);
}

inline void writeLeF64(std::uint8_t* p, double v) noexcept {
    std::uint64_t bits;
    std::memcpy(&bits, &v, sizeof(bits));
    writeLeU64(p, bits);
}

}  // namespace osf::detail

#endif  // OSF_DETAIL_BINARY_IO_HPP
