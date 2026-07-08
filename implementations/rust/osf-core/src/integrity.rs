// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

//! OSF5 integrity profile — level `crc` primitives.
//!
//! The integrity profile is an optional, backward-compatible OSF5 feature
//! (see the specification's *OSF5 Integrity Profile* reference). This module
//! carries the level declaration and the CRC32C primitive shared by the reader
//! and writer. Level `signed` (Ed25519 / hash chain / PKI) is only represented
//! here as the [`IntegrityProfile::Ed25519`] variant so that a signed file
//! stays readable and CRC-checked; its verification is out of scope for this
//! module.

/// Integrity level declared for a file by the magic-header token.
///
/// Strictly ordered ladder `None ⊂ Crc32c ⊂ Ed25519`: every level implies the
/// ones below it.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, Default)]
pub enum IntegrityProfile {
    /// No integrity declaration — the file carries no CRCs or signatures.
    #[default]
    None,
    /// Level `crc`: a `crc32c` header token plus a per-block frame CRC32C.
    Crc32c,
    /// Level `signed`: additionally an Ed25519 signature chain. This crate
    /// reads and CRC-checks such files but does not verify signatures.
    Ed25519,
}

impl std::fmt::Display for IntegrityProfile {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_str(match self {
            IntegrityProfile::None => "none",
            IntegrityProfile::Crc32c => "crc32c",
            IntegrityProfile::Ed25519 => "ed25519",
        })
    }
}

/// CRC32C (Castagnoli, polynomial 0x1EDC6F41 — the CRC-32/ISCSI algorithm)
/// over `bytes`.
///
/// Used for both the metablock checksum (carried by the `crc32c` header token)
/// and the per-block frame CRC.
pub(crate) fn crc32c(bytes: &[u8]) -> u32 {
    const ALGO: crc::Crc<u32> = crc::Crc::<u32>::new(&crc::CRC_32_ISCSI);
    ALGO.checksum(bytes)
}

/// CRC32C over the concatenation of `parts`.
///
/// The per-block frame CRC covers the channel index, the length field, the
/// control byte and the payload; those live in separate buffers on both the
/// read and write side, so this joins them for a single checksum.
pub(crate) fn crc32c_of_parts(parts: &[&[u8]]) -> u32 {
    let total: usize = parts.iter().map(|p| p.len()).sum();
    let mut buf = Vec::with_capacity(total);
    for part in parts {
        buf.extend_from_slice(part);
    }
    crc32c(&buf)
}

/// Verify the metablock CRC declared by the `crc32c` header token against the
/// raw metablock `body` bytes.
///
/// `expected` is [`crate::header::MagicHeader::metablock_crc`]; when it is
/// `None` (no integrity token) this is a no-op. A mismatch is a hard error —
/// nothing after the metablock is interpretable without a trustworthy one.
pub(crate) fn verify_metablock_crc(
    expected: Option<u32>,
    body: &[u8],
) -> Result<(), crate::error::OsfError> {
    if let Some(expected) = expected {
        let actual = crc32c(body);
        if actual != expected {
            return Err(crate::error::OsfError::MetablockCrcMismatch { expected, actual });
        }
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn crc32c_check_vector() {
        // The canonical CRC-32/ISCSI (Castagnoli) check value for the ASCII
        // string "123456789" is 0xE3069283.
        assert_eq!(crc32c(b"123456789"), 0xE306_9283);
    }

    #[test]
    fn crc32c_empty_is_zero() {
        assert_eq!(crc32c(b""), 0);
    }

    #[test]
    fn verify_metablock_crc_matches() {
        let body = b"{\"osf\":{\"version\":5}}";
        let expected = crc32c(body);
        assert!(verify_metablock_crc(Some(expected), body).is_ok());
    }

    #[test]
    fn verify_metablock_crc_mismatch_errors() {
        let body = b"{\"osf\":{\"version\":5}}";
        let wrong = crc32c(body) ^ 0x1;
        let err = verify_metablock_crc(Some(wrong), body).unwrap_err();
        assert!(
            matches!(err, crate::error::OsfError::MetablockCrcMismatch { .. }),
            "got {err:?}"
        );
    }

    #[test]
    fn verify_metablock_crc_none_is_noop() {
        assert!(verify_metablock_crc(None, b"anything").is_ok());
    }

    #[test]
    fn integrity_profile_display() {
        assert_eq!(IntegrityProfile::None.to_string(), "none");
        assert_eq!(IntegrityProfile::Crc32c.to_string(), "crc32c");
        assert_eq!(IntegrityProfile::Ed25519.to_string(), "ed25519");
    }
}
