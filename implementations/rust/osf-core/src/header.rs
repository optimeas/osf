// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

//! OSF magic-header detection.
//!
//! Every OSF file starts with a single ASCII line that identifies the
//! version and announces the byte length of the metablock that follows:
//!
//! ```text
//! <IDENTIFIER> <metablock_length>\n
//! ```
//!
//! The identifier is one of:
//!
//! | On disk                   | Maps to            |
//! |---------------------------|--------------------|
//! | `OSF4`                    | [`OsfVersion::Osf4`] |
//! | `OCEAN_STREAM_FORMAT4`    | [`OsfVersion::Osf4`] |
//! | `OCEAN_STREAMING_FORMAT4` | [`OsfVersion::Osf4`] |
//! | `OSF5`                    | [`OsfVersion::Osf5`] |
//!
//! `OCEAN_STREAM_FORMAT4` is what currently deployed optiMEAS devices
//! emit, so it is not a curiosity: production field files in this repo
//! (`examples/steam_loco.osf`, `examples/motorbike.osf`) use it.

use crate::error::OsfError;
use crate::integrity::IntegrityProfile;
use std::io::Read;

/// Cap on the magic-header line length. The longest valid line is roughly
/// `OCEAN_STREAMING_FORMAT4 ` (24) + 20 digits of `u64::MAX` + `\n` = 45
/// bytes. 128 leaves comfortable headroom for unforeseen identifiers
/// without letting a corrupt or non-OSF file run away.
const MAX_MAGIC_HEADER_LEN: usize = 128;

/// On-disk OSF format version, derived from the magic header.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum OsfVersion {
    /// OSF4: XML metablock, classic control-byte set, file trailer.
    Osf4,
    /// OSF5: JSON metablock, simplified control byte, no trailer.
    Osf5,
}

/// Parsed contents of the OSF magic-header line.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct MagicHeader {
    /// Version detected from the identifier prefix.
    pub version: OsfVersion,
    /// Byte length of the metablock that immediately follows the
    /// terminating newline.
    pub metablock_len: u64,
    /// Integrity level declared by the header tokens (`none` when the line
    /// carries no integrity token).
    pub integrity: IntegrityProfile,
    /// CRC32C of the raw metablock bytes, carried by the `crc32c` token.
    /// `None` unless `integrity` is at least `Crc32c`.
    pub metablock_crc: Option<u32>,
}

/// Read and parse the magic-header line from `reader`.
///
/// On success the reader is positioned immediately after the terminating
/// `\n`, so the next read returns the first byte of the metablock.
///
/// # Errors
///
/// Returns [`OsfError::Io`] if the underlying reader fails,
/// [`OsfError::MagicHeaderTooLong`] if no `\n` is seen within
/// [`MAX_MAGIC_HEADER_LEN`] bytes, [`OsfError::InvalidMagicHeader`] if the
/// line is malformed (missing length, non-ASCII, unparseable number), and
/// [`OsfError::UnsupportedVersion`] if the identifier is not one of the
/// known OSF4/OSF5 spellings.
pub fn parse_magic_header<R: Read>(reader: &mut R) -> Result<MagicHeader, OsfError> {
    let line = read_first_line(reader)?;
    parse_magic_header_line(&line)
}

fn read_first_line<R: Read>(reader: &mut R) -> Result<String, OsfError> {
    let mut buf = Vec::with_capacity(48);
    let mut byte = [0u8; 1];

    loop {
        let n = reader.read(&mut byte)?;
        if n == 0 {
            return Err(OsfError::InvalidMagicHeader(
                "unexpected end of input before newline".into(),
            ));
        }
        if byte[0] == b'\n' {
            break;
        }
        buf.push(byte[0]);
        if buf.len() > MAX_MAGIC_HEADER_LEN {
            return Err(OsfError::MagicHeaderTooLong(MAX_MAGIC_HEADER_LEN));
        }
    }

    // Trim a stray CR if the file was produced on a system that emitted
    // CRLF for the magic-header line. The spec is LF-only, but tolerating
    // CRLF here costs nothing and avoids a bogus parse failure.
    if buf.last() == Some(&b'\r') {
        buf.pop();
    }

    String::from_utf8(buf).map_err(|e| {
        OsfError::InvalidMagicHeader(format!("magic header is not valid UTF-8: {e}"))
    })
}

fn parse_magic_header_line(line: &str) -> Result<MagicHeader, OsfError> {
    let (identifier, rest) = line.split_once(' ').ok_or_else(|| {
        OsfError::InvalidMagicHeader(format!(
            "expected '<identifier> <length>', got: {line:?}"
        ))
    })?;

    let version = identifier_to_version(identifier)?;

    // The header grammar is `identifier SP metablock-len *(SP token) LF` with
    // exactly one space between fields and no trailing space. Splitting on a
    // single ' ' therefore turns a double or trailing space into an empty
    // field, which is malformed.
    let mut fields = rest.split(' ');

    let len_field = fields.next().unwrap_or("");
    if len_field.is_empty() {
        return Err(OsfError::InvalidMagicHeader(format!(
            "missing metablock length after identifier {identifier:?}"
        )));
    }
    let metablock_len: u64 = len_field.parse().map_err(|_| {
        OsfError::InvalidMagicHeader(format!(
            "metablock length is not a valid u64: {len_field:?}"
        ))
    })?;

    let mut integrity = IntegrityProfile::None;
    let mut metablock_crc = None;
    let mut saw_crc = false;

    for token in fields {
        if token.is_empty() {
            return Err(OsfError::InvalidMagicHeader(
                "malformed magic header: fields must be separated by a single space with no \
                 trailing space"
                    .into(),
            ));
        }
        // Tokens are an OSF5-only feature; OSF4 identifiers must not carry them.
        if version != OsfVersion::Osf5 {
            return Err(OsfError::InvalidMagicHeader(format!(
                "header tokens are only allowed for OSF5; found {token:?} after an OSF4 identifier"
            )));
        }
        let (key, value) = token.split_once(':').ok_or_else(|| {
            OsfError::InvalidMagicHeader(format!(
                "malformed header token, expected 'key:value': {token:?}"
            ))
        })?;
        if key.is_empty()
            || !key
                .bytes()
                .all(|b| b.is_ascii_lowercase() || b.is_ascii_digit() || b == b'-')
        {
            return Err(OsfError::InvalidMagicHeader(format!(
                "malformed header token key (lowercase a-z, 0-9, '-' only): {key:?}"
            )));
        }
        match key {
            "crc32c" => {
                metablock_crc = Some(parse_crc32c_token(value)?);
                integrity = IntegrityProfile::Crc32c;
                saw_crc = true;
            }
            "ed25519" => {
                validate_ed25519_keyid(value)?;
                if !saw_crc {
                    return Err(OsfError::InvalidMagicHeader(
                        "ed25519 token is only valid after a crc32c token (crc32c first)".into(),
                    ));
                }
                integrity = IntegrityProfile::Ed25519;
            }
            other => return Err(OsfError::UnknownHeaderToken(other.to_string())),
        }
    }

    Ok(MagicHeader {
        version,
        metablock_len,
        integrity,
        metablock_crc,
    })
}

/// Parse the value of a `crc32c` header token: exactly 8 **uppercase** hex
/// digits, per the integrity-profile grammar (readers check casing strictly).
fn parse_crc32c_token(value: &str) -> Result<u32, OsfError> {
    let is_upper_hex = |b: u8| b.is_ascii_digit() || (b'A'..=b'F').contains(&b);
    if value.len() != 8 || !value.bytes().all(is_upper_hex) {
        return Err(OsfError::InvalidMagicHeader(format!(
            "crc32c token value must be 8 uppercase hex digits, got {value:?}"
        )));
    }
    u32::from_str_radix(value, 16).map_err(|_| {
        OsfError::InvalidMagicHeader(format!("crc32c token value is not valid hex: {value:?}"))
    })
}

/// Validate the value of an `ed25519` header token: exactly 16 **lowercase**
/// hex digits (the 8-byte key id). Verification of the signature itself is out
/// of scope for this crate.
fn validate_ed25519_keyid(value: &str) -> Result<(), OsfError> {
    let is_lower_hex = |b: u8| b.is_ascii_digit() || (b'a'..=b'f').contains(&b);
    if value.len() != 16 || !value.bytes().all(is_lower_hex) {
        return Err(OsfError::InvalidMagicHeader(format!(
            "ed25519 keyid must be 16 lowercase hex digits, got {value:?}"
        )));
    }
    Ok(())
}

fn identifier_to_version(identifier: &str) -> Result<OsfVersion, OsfError> {
    match identifier {
        "OSF4" | "OCEAN_STREAM_FORMAT4" | "OCEAN_STREAMING_FORMAT4" => Ok(OsfVersion::Osf4),
        "OSF5" => Ok(OsfVersion::Osf5),
        other => Err(OsfError::UnsupportedVersion(other.to_string())),
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::Cursor;

    fn parse(bytes: &[u8]) -> Result<MagicHeader, OsfError> {
        let mut cursor = Cursor::new(bytes);
        parse_magic_header(&mut cursor)
    }

    #[test]
    fn parses_modern_osf4_identifier() {
        let header = parse(b"OSF4 928\n<rest>").unwrap();
        assert_eq!(header.version, OsfVersion::Osf4);
        assert_eq!(header.metablock_len, 928);
    }

    #[test]
    fn parses_legacy_ocean_stream_format4_identifier() {
        let header = parse(b"OCEAN_STREAM_FORMAT4 26279\n").unwrap();
        assert_eq!(header.version, OsfVersion::Osf4);
        assert_eq!(header.metablock_len, 26279);
    }

    #[test]
    fn parses_legacy_ocean_streaming_format4_identifier() {
        let header = parse(b"OCEAN_STREAMING_FORMAT4 12345\n").unwrap();
        assert_eq!(header.version, OsfVersion::Osf4);
        assert_eq!(header.metablock_len, 12345);
    }

    #[test]
    fn parses_osf5_identifier() {
        let header = parse(b"OSF5 895\n{\"osf\":...").unwrap();
        assert_eq!(header.version, OsfVersion::Osf5);
        assert_eq!(header.metablock_len, 895);
    }

    #[test]
    fn rejects_unknown_identifier() {
        let err = parse(b"OSF99 100\n").unwrap_err();
        assert!(
            matches!(err, OsfError::UnsupportedVersion(ref s) if s == "OSF99"),
            "got {err:?}"
        );
    }

    #[test]
    fn rejects_missing_length() {
        let err = parse(b"OSF5\n").unwrap_err();
        assert!(matches!(err, OsfError::InvalidMagicHeader(_)), "got {err:?}");
    }

    #[test]
    fn rejects_non_numeric_length() {
        let err = parse(b"OSF5 abc\n").unwrap_err();
        assert!(matches!(err, OsfError::InvalidMagicHeader(_)), "got {err:?}");
    }

    #[test]
    fn rejects_runaway_input_without_newline() {
        let payload = vec![b'X'; MAX_MAGIC_HEADER_LEN + 10];
        let err = parse(&payload).unwrap_err();
        assert!(
            matches!(err, OsfError::MagicHeaderTooLong(MAX_MAGIC_HEADER_LEN)),
            "got {err:?}"
        );
    }

    #[test]
    fn rejects_truncated_input() {
        let err = parse(b"OSF5 895").unwrap_err();
        assert!(matches!(err, OsfError::InvalidMagicHeader(_)), "got {err:?}");
    }

    #[test]
    fn tolerates_crlf_terminator() {
        let header = parse(b"OSF5 42\r\n").unwrap();
        assert_eq!(header.version, OsfVersion::Osf5);
        assert_eq!(header.metablock_len, 42);
    }

    #[test]
    fn plain_osf5_has_no_integrity() {
        let header = parse(b"OSF5 895\n").unwrap();
        assert_eq!(header.integrity, IntegrityProfile::None);
        assert_eq!(header.metablock_crc, None);
    }

    #[test]
    fn parses_crc32c_token() {
        let header = parse(b"OSF5 84512 crc32c:9A3F01BC\n").unwrap();
        assert_eq!(header.integrity, IntegrityProfile::Crc32c);
        assert_eq!(header.metablock_crc, Some(0x9A3F_01BC));
        assert_eq!(header.metablock_len, 84512);
    }

    #[test]
    fn rejects_unknown_header_token() {
        let err = parse(b"OSF5 100 sha256:ABCD\n").unwrap_err();
        assert!(
            matches!(err, OsfError::UnknownHeaderToken(ref k) if k == "sha256"),
            "got {err:?}"
        );
    }

    #[test]
    fn rejects_token_on_osf4_identifier() {
        let err = parse(b"OSF4 100 crc32c:9A3F01BC\n").unwrap_err();
        assert!(matches!(err, OsfError::InvalidMagicHeader(_)), "got {err:?}");
    }

    #[test]
    fn rejects_crc32c_lowercase_hex() {
        let err = parse(b"OSF5 100 crc32c:9a3f01bc\n").unwrap_err();
        assert!(matches!(err, OsfError::InvalidMagicHeader(_)), "got {err:?}");
    }

    #[test]
    fn rejects_crc32c_wrong_length() {
        let err = parse(b"OSF5 100 crc32c:9A3F01\n").unwrap_err();
        assert!(matches!(err, OsfError::InvalidMagicHeader(_)), "got {err:?}");
    }

    #[test]
    fn rejects_double_space_separator() {
        let err = parse(b"OSF5  84512\n").unwrap_err();
        assert!(matches!(err, OsfError::InvalidMagicHeader(_)), "got {err:?}");
    }

    #[test]
    fn rejects_trailing_space() {
        let err = parse(b"OSF5 84512 \n").unwrap_err();
        assert!(matches!(err, OsfError::InvalidMagicHeader(_)), "got {err:?}");
    }

    #[test]
    fn parses_crc32c_then_ed25519() {
        let header = parse(b"OSF5 84512 crc32c:9A3F01BC ed25519:0123456789abcdef\n").unwrap();
        assert_eq!(header.integrity, IntegrityProfile::Ed25519);
        assert_eq!(header.metablock_crc, Some(0x9A3F_01BC));
    }

    #[test]
    fn rejects_ed25519_without_crc32c() {
        let err = parse(b"OSF5 84512 ed25519:0123456789abcdef\n").unwrap_err();
        assert!(matches!(err, OsfError::InvalidMagicHeader(_)), "got {err:?}");
    }

    #[test]
    fn rejects_ed25519_before_crc32c() {
        let err = parse(b"OSF5 84512 ed25519:0123456789abcdef crc32c:9A3F01BC\n").unwrap_err();
        assert!(matches!(err, OsfError::InvalidMagicHeader(_)), "got {err:?}");
    }

    #[test]
    fn rejects_ed25519_uppercase_keyid() {
        let err = parse(b"OSF5 84512 crc32c:9A3F01BC ed25519:0123456789ABCDEF\n").unwrap_err();
        assert!(matches!(err, OsfError::InvalidMagicHeader(_)), "got {err:?}");
    }

    #[test]
    fn reader_is_positioned_after_newline() {
        let bytes = b"OSF5 7\nMETABLOCK_BYTES_FOLLOW";
        let mut cursor = Cursor::new(&bytes[..]);
        let _ = parse_magic_header(&mut cursor).unwrap();
        let mut rest = Vec::new();
        cursor.read_to_end(&mut rest).unwrap();
        assert_eq!(rest, b"METABLOCK_BYTES_FOLLOW");
    }
}
