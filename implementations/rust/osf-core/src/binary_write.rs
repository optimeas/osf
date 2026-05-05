// Copyright 2026 Optimeas GmbH
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

//! Little-endian write helpers used by the OSF block writer.
//!
//! The reader side uses `byteorder::ReadBytesExt`; this module is the
//! symmetric write side. Variable-length payloads (`string`, `binary`)
//! get the trailing `0x00` byte appended that spec rev 2026-05-04
//! mandates for `bcAbsTimeStampData`.

use byteorder::{LittleEndian, WriteBytesExt};
use std::io::{self, Write};

pub(crate) fn write_u8<W: Write>(w: &mut W, v: u8) -> io::Result<()> {
    w.write_u8(v)
}

pub(crate) fn write_i8<W: Write>(w: &mut W, v: i8) -> io::Result<()> {
    w.write_i8(v)
}

pub(crate) fn write_u16<W: Write>(w: &mut W, v: u16) -> io::Result<()> {
    w.write_u16::<LittleEndian>(v)
}

pub(crate) fn write_i16<W: Write>(w: &mut W, v: i16) -> io::Result<()> {
    w.write_i16::<LittleEndian>(v)
}

pub(crate) fn write_u32<W: Write>(w: &mut W, v: u32) -> io::Result<()> {
    w.write_u32::<LittleEndian>(v)
}

pub(crate) fn write_i32<W: Write>(w: &mut W, v: i32) -> io::Result<()> {
    w.write_i32::<LittleEndian>(v)
}

pub(crate) fn write_u64<W: Write>(w: &mut W, v: u64) -> io::Result<()> {
    w.write_u64::<LittleEndian>(v)
}

pub(crate) fn write_i64<W: Write>(w: &mut W, v: i64) -> io::Result<()> {
    w.write_i64::<LittleEndian>(v)
}

pub(crate) fn write_f32<W: Write>(w: &mut W, v: f32) -> io::Result<()> {
    w.write_f32::<LittleEndian>(v)
}

pub(crate) fn write_f64<W: Write>(w: &mut W, v: f64) -> io::Result<()> {
    w.write_f64::<LittleEndian>(v)
}

pub(crate) fn write_bool<W: Write>(w: &mut W, v: bool) -> io::Result<()> {
    write_u8(w, u8::from(v))
}

/// Write a length-prefixed length value (2 or 4 bytes per the
/// channel's `sizeoflengthvalue`). Returns the number of bytes written.
pub(crate) fn write_length_field<W: Write>(
    w: &mut W,
    size_of_length_value: u8,
    value: u32,
) -> io::Result<()> {
    match size_of_length_value {
        2 => write_u16(w, value as u16),
        4 => write_u32(w, value),
        other => Err(io::Error::new(
            io::ErrorKind::InvalidInput,
            format!("size_of_length_value must be 2 or 4, got {other}"),
        )),
    }
}

/// Write a UTF-8 string and append the spec-mandated trailing `0x00`
/// byte. Used for `bcAbsTimeStampData` payloads on `string` channels.
pub(crate) fn write_string_with_terminator<W: Write>(w: &mut W, s: &str) -> io::Result<()> {
    w.write_all(s.as_bytes())?;
    write_u8(w, 0)
}

/// Write a binary payload and append the spec-mandated trailing `0x00`
/// byte. Used for `bcAbsTimeStampData` payloads on `binary` channels.
pub(crate) fn write_binary_with_terminator<W: Write>(w: &mut W, data: &[u8]) -> io::Result<()> {
    w.write_all(data)?;
    write_u8(w, 0)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn roundtrip<T, W, R>(value: T, write: W, read: R) -> T
    where
        W: Fn(&mut Vec<u8>, T) -> io::Result<()>,
        R: Fn(&[u8]) -> T,
        T: Copy,
    {
        let mut buf = Vec::new();
        write(&mut buf, value).unwrap();
        read(&buf)
    }

    #[test]
    fn roundtrips_all_numeric_widths() {
        // u8 / i8
        assert_eq!(roundtrip(42u8, write_u8, |b| b[0]), 42u8);
        assert_eq!(roundtrip(-5i8, write_i8, |b| b[0] as i8), -5i8);

        // u16 LE
        let mut buf = Vec::new();
        write_u16(&mut buf, 0xABCD).unwrap();
        assert_eq!(buf, vec![0xCD, 0xAB]);

        // i64 LE
        let mut buf = Vec::new();
        write_i64(&mut buf, -1).unwrap();
        assert_eq!(buf, vec![0xFF; 8]);

        // f64 LE
        let mut buf = Vec::new();
        write_f64(&mut buf, 1.0).unwrap();
        assert_eq!(buf.len(), 8);
        // 1.0 in IEEE-754 double = 0x3FF0_0000_0000_0000, little-endian
        assert_eq!(buf, vec![0, 0, 0, 0, 0, 0, 0xF0, 0x3F]);
    }

    #[test]
    fn bool_writes_1_byte() {
        let mut buf = Vec::new();
        write_bool(&mut buf, true).unwrap();
        write_bool(&mut buf, false).unwrap();
        assert_eq!(buf, vec![1, 0]);
    }

    #[test]
    fn string_with_terminator_appends_null() {
        let mut buf = Vec::new();
        write_string_with_terminator(&mut buf, "hi").unwrap();
        assert_eq!(buf, b"hi\0");
    }

    #[test]
    fn binary_with_terminator_appends_null() {
        let mut buf = Vec::new();
        write_binary_with_terminator(&mut buf, &[0xFF, 0xD8, 0xFF]).unwrap();
        assert_eq!(buf, vec![0xFF, 0xD8, 0xFF, 0x00]);
    }

    #[test]
    fn empty_string_still_writes_terminator() {
        let mut buf = Vec::new();
        write_string_with_terminator(&mut buf, "").unwrap();
        assert_eq!(buf, vec![0]);
    }

    #[test]
    fn length_field_2_writes_u16() {
        let mut buf = Vec::new();
        write_length_field(&mut buf, 2, 0x1234).unwrap();
        assert_eq!(buf, vec![0x34, 0x12]);
    }

    #[test]
    fn length_field_4_writes_u32() {
        let mut buf = Vec::new();
        write_length_field(&mut buf, 4, 0x1234_5678).unwrap();
        assert_eq!(buf, vec![0x78, 0x56, 0x34, 0x12]);
    }

    #[test]
    fn length_field_invalid_size_errors() {
        let mut buf = Vec::new();
        let err = write_length_field(&mut buf, 3, 0).unwrap_err();
        assert_eq!(err.kind(), io::ErrorKind::InvalidInput);
    }
}
