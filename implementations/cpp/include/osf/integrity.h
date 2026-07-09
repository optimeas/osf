// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

/// \file integrity.h
/// OSF5 integrity profile — level declaration.
///
/// The integrity profile is an optional, backward-compatible OSF5 feature (see
/// the specification's *OSF5 Integrity Profile* reference). This header carries
/// the level declaration shared by the magic header, the reader statistics and
/// the writers. Level `signed` (Ed25519 / hash chain / PKI) is represented only
/// as the `Ed25519` enumerator so that a signed file stays readable and
/// CRC-checked; verifying signatures is out of scope for this build.

#pragma once

namespace osf {

/// Integrity level declared for a file by the magic-header token.
///
/// Strictly ordered ladder `None` ⊂ `Crc32c` ⊂ `Ed25519`: each level implies
/// the ones below it.
enum class IntegrityProfile {
    /// No integrity declaration — the file carries no CRCs or signatures.
    None,
    /// Level `crc`: a `crc32c` header token plus a per-block frame CRC32C.
    Crc32c,
    /// Level `signed`: additionally an Ed25519 signature chain. This build
    /// reads and CRC-checks such files but does not verify signatures.
    Ed25519,
};

/// Status-vocabulary name of an integrity profile: `"none"`, `"crc32c"`, or
/// `"ed25519"`.
[[nodiscard]] inline char const* integrityProfileName(IntegrityProfile p) noexcept {
    switch (p) {
        case IntegrityProfile::Crc32c:
            return "crc32c";
        case IntegrityProfile::Ed25519:
            return "ed25519";
        case IntegrityProfile::None:
            break;
    }
    return "none";
}

}  // namespace osf
