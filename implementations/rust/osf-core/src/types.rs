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

//! Core OSF type enumerations.
//!
//! These mirror the spec revision **2026-05-04** datatype set. Removed
//! datatypes (`pair`, `triple`, `candata`, `gpsdata`) are intentionally
//! absent — readers must reject those legacy strings rather than silently
//! mapping them to a current type.

/// Data type carried by an OSF channel. Matches the on-disk `datatype`
/// attribute / JSON field exactly (lowercase, ASCII).
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum DataType {
    /// 1-byte boolean (`0x00` = false, anything else = true).
    Bool,
    /// Signed 8-bit integer.
    Int8,
    /// Signed 16-bit integer (little-endian on disk).
    Int16,
    /// Signed 32-bit integer (little-endian on disk).
    Int32,
    /// Signed 64-bit integer (little-endian on disk).
    Int64,
    /// Unsigned 8-bit integer. Added in spec revision 2026-05-04.
    UInt8,
    /// Unsigned 16-bit integer. Added in spec revision 2026-05-04.
    UInt16,
    /// Unsigned 32-bit integer. Added in spec revision 2026-05-04.
    UInt32,
    /// Unsigned 64-bit integer. Added in spec revision 2026-05-04.
    UInt64,
    /// IEEE-754 single-precision floating point.
    Float,
    /// IEEE-754 double-precision floating point.
    Double,
    /// UTF-8 string with a trailing `0x00` byte on disk
    /// (writer appends, reader strips).
    String,
    /// Opaque byte payload with a trailing `0x00` byte on disk; the
    /// `bytearray` spelling is accepted as a read-side alias.
    Binary,
    /// 24-byte struct of `latitude`, `longitude`, `altitude` as
    /// little-endian `double`s. Renamed from `gpsdata` in spec revision
    /// 2026-05-04.
    GpsLocation,
}

/// Whether a channel stores values at a fixed sample rate
/// (`equidistant`) or with an explicit timestamp per sample
/// (`timestamped`).
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum ChannelType {
    /// Channel with a fixed sample rate; timestamps are reconstructed from
    /// `bcStartData` segments and the sample index.
    Equidistant,
    /// Channel with an absolute timestamp per sample.
    Timestamped,
}

/// Block-content discriminator used in the OSF stream.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum BlockContent {
    /// Marks the start of a new equidistant segment; carries an `int64`
    /// start timestamp followed by a `double` sample rate.
    StartData,
    /// Carries one or more samples each prefixed with an absolute
    /// timestamp (timestamped channels).
    AbsTimeStampData,
    /// Carries a packed run of equidistant samples without per-sample
    /// timestamps.
    EquidistantData,
}
