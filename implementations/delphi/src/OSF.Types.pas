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

unit OSF.Types;

interface

uses
  System.SysUtils,
  System.DateUtils;

const
  // Magic header tokens written as the very first bytes of every OSF file.
  // Two legacy spellings for the original OSF4 token are present in the wild:
  // the long form 'OCEAN_STREAMING_FORMAT4' and the short form
  // 'OCEAN_STREAM_FORMAT4' (the variant emitted by the original demo
  // generator). Both are accepted on read.
  OSF_MAGIC_OSF4                = 'OSF4';
  OSF_MAGIC_OSF5                = 'OSF5';
  OSF_MAGIC_OCEAN_STREAM        = 'OCEAN_STREAMING_FORMAT4';  // long legacy
  OSF_MAGIC_OCEAN_STREAM_LEGACY = 'OCEAN_STREAM_FORMAT4';     // short legacy

  // Magic trailer written at the end of OSF4 files (optional).
  // Format: "OSF_STREAM_END <offset>========..." padded to exactly 40 bytes.
  // The offset is the file position of the 0xFFFF info channel block.
  OSF_MAGIC_STREAM_END     = 'OSF_STREAM_END';
  OSF_TRAILER_TOTAL_LENGTH = 40;

  // Channel index that identifies the optional info/trailer block.
  OSF_INFO_CHANNEL_INDEX = $FFFF;

  // Control byte bit definitions.
  // Bit 7 = 1 means the block contains N > 1 samples; the sample count
  // is stored as a uint32 immediately after the control byte.
  OSF_BLOCK_MULTI_VALUE_FLAG = $80;
  OSF_BLOCK_TYPE_MASK        = $7F;

  // Default value for sizeoflengthvalue when not specified in the meta block.
  OSF_DEFAULT_LENGTH_FIELD_SIZE = 2;

  // Default namespace separator for hierarchical channel names.
  OSF_DEFAULT_NAMESPACE_SEP = '.';

type
  // Detected file version from the magic header line.
  TOSFVersion = (
    osvUnknown,
    osvOSF4,
    osvOSF5
  );

  // Meta block encoding, determined by the first byte after the magic header.
  // '<' = XML (OSF4), '{' = JSON (OSF5).
  TOSFMetaFormat = (
    mfXML,
    mfJSON
  );

  // Block content type — lower 7 bits of the control byte.
  // Values 1-4 and 7 are deprecated and not written by new implementations.
  // OSF5 readers must handle bcContinuedRelStampData for backward compatibility
  // with OSF4 files; all others can be silently skipped using the length field.
  TBlockContent = (
    bcReserved              = 0,
    bcTrustedTimestamp      = 1,  // deprecated; ignore on read
    bcTimebaseRealign       = 2,  // deprecated; ignore on read
    bcStatusEvent           = 3,  // deprecated; ignore on read
    bcMessageEvent          = 4,  // deprecated; ignore on read
    bcContinuedData         = 5,
    bcStartData             = 6,
    bcContinuedRelStampData = 7,  // deprecated; read support required for OSF4
    bcAbsTimeStampData      = 8
  );

  // Value data type of a channel, as declared in the meta block.
  // Unsigned integer types (dtUInt8..dtUInt64) are supported in both OSF4 and OSF5.
  TOSFDataType = (
    dtBool,
    dtInt8, dtInt16, dtInt32, dtInt64,
    dtUInt8, dtUInt16, dtUInt32, dtUInt64,
    dtFloat, dtDouble,
    dtString, dtBinary,
    dtGpsData
  );

  // Structural layout of a channel's data blocks.
  TOSFChannelType = (
    ctScalar,
    ctVector,
    ctMatrix,
    ctBinary
  );

  // Width of the length field that precedes each data block for a channel.
  // Stored as the sizeoflengthvalue attribute/property in the meta block.
  // lfs2 is the default and sufficient for all scalar/vector channels.
  // lfs4 is required for binary channels carrying images or large payloads.
  TOSFLengthFieldSize = (
    lfs2 = 2,  // uint16 — max 65 535 bytes per block
    lfs4 = 4   // uint32 — max ~4 GB per block
  );

  // GPS position — 24 bytes.
  TOSFGpsData = packed record
    Longitude : Double;
    Latitude  : Double;
    Altitude  : Double;
  end;

  // Base exception for all OSF errors.
  EOSFException = class(Exception);

  // Raised when a file's binary structure violates the OSF specification.
  EOSFFormatError = class(EOSFException);

  // Raised when a file version or feature is not supported by this implementation.
  EOSFVersionError = class(EOSFException);

  // A single entry in the optional free-form metadata list embedded in the
  // OSF header. In OSF5 JSON: the "info" array. In OSF4 XML: <infos><info .../>.
  TOSFMetaItem = record
    Name     : string;
    Value    : string;
    DataType : string;  // typically 'string', 'int', 'double'
    UnitStr  : string;
  end;

// Compile-time binary layout assertions.
{$IF SizeOf(TOSFGpsData) <> 24}
  {$MESSAGE ERROR 'TOSFGpsData must be exactly 24 bytes'}
{$ENDIF}

resourcestring
  SOSFUnknownDataType        = 'Unknown OSF data type: "%s"';
  SOSFRemovedDataType        = 'OSF data type "%s" is no longer supported in this OSF revision';
  SOSFUnhandledDataType      = 'Unhandled TOSFDataType value: %d';
  SOSFUnknownChannelType     = 'Unknown OSF channel type: "%s"';
  SOSFUnhandledChannelType   = 'Unhandled TOSFChannelType value: %d';
  SOSFUnknownBlockTypeByte   = 'Unknown block type byte: %d';
  SOSFInvalidLengthFieldSize = 'Invalid sizeoflengthvalue %d — must be 2 or 4';

// Data type helpers.
function OSFDataTypeFromString(const S: string): TOSFDataType;
function OSFDataTypeToString(DT: TOSFDataType): string;
// Returns the fixed byte size of DT, or 0 for variable-length types (string, binary).
function OSFDataTypeFixedSize(DT: TOSFDataType): Integer;
function OSFDataTypeIsVariableLength(DT: TOSFDataType): Boolean;

// Channel type helpers.
function OSFChannelTypeFromString(const S: string): TOSFChannelType;
function OSFChannelTypeToString(CT: TOSFChannelType): string;

// Control byte helpers.
function OSFBlockTypeFromByte(ControlByte: Byte): TBlockContent;
function OSFBlockHasMultipleValues(ControlByte: Byte): Boolean;
function OSFMakeControlByte(BlockType: TBlockContent; MultiValue: Boolean): Byte;

// Length field size helpers.
function OSFLengthFieldSizeFromInt(Value: Integer): TOSFLengthFieldSize;

// Version detection from a magic token read out of the header line.
function OSFVersionFromMagic(const Magic: string): TOSFVersion;

// Returns the current UTC time formatted as ISO 8601 with millisecond precision.
// Example: "2026-05-03T14:22:07.456Z"
function OSFUtcNowISO8601: string;

// Returns the current UTC time as nanoseconds since the Unix epoch (1970-01-01T00:00:00Z).
// Precision is limited to ~1 microsecond due to Double arithmetic on the day count.
function OSFNowAsUnixNs: Int64;

implementation

function OSFDataTypeFromString(const S: string): TOSFDataType;
var
  Lower: string;
begin
  Lower := LowerCase(S);
  if      Lower = 'bool'      then Exit(dtBool)
  else if Lower = 'int8'      then Exit(dtInt8)
  else if Lower = 'int16'     then Exit(dtInt16)
  else if Lower = 'int32'     then Exit(dtInt32)
  else if Lower = 'int64'     then Exit(dtInt64)
  else if Lower = 'uint8'     then Exit(dtUInt8)
  else if Lower = 'uint16'    then Exit(dtUInt16)
  else if Lower = 'uint32'    then Exit(dtUInt32)
  else if Lower = 'uint64'    then Exit(dtUInt64)
  else if Lower = 'float'     then Exit(dtFloat)
  else if Lower = 'float32'   then Exit(dtFloat)    // legacy alias
  else if Lower = 'double'    then Exit(dtDouble)
  else if Lower = 'string'    then Exit(dtString)
  else if Lower = 'binary'    then Exit(dtBinary)
  else if Lower = 'bytearray' then Exit(dtBinary)   // OSF4 legacy alias
  else if Lower = 'gpsdata'   then Exit(dtGpsData);

  // Datatypes removed in spec revision 2026-05-04 — fail loudly so callers
  // see exactly which one tripped them up.
  if (Lower = 'pair') or (Lower = 'triple') or (Lower = 'candata') then
    raise EOSFFormatError.CreateFmt(SOSFRemovedDataType, [S]);

  raise EOSFFormatError.CreateFmt(SOSFUnknownDataType, [S]);
end;

function OSFDataTypeToString(DT: TOSFDataType): string;
begin
  case DT of
    dtBool:    Result := 'bool';
    dtInt8:    Result := 'int8';
    dtInt16:   Result := 'int16';
    dtInt32:   Result := 'int32';
    dtInt64:   Result := 'int64';
    dtUInt8:   Result := 'uint8';
    dtUInt16:  Result := 'uint16';
    dtUInt32:  Result := 'uint32';
    dtUInt64:  Result := 'uint64';
    dtFloat:   Result := 'float';
    dtDouble:  Result := 'double';
    dtString:  Result := 'string';
    dtBinary:  Result := 'binary';
    dtGpsData: Result := 'gpsdata';
  else
    raise EOSFFormatError.CreateFmt(SOSFUnhandledDataType, [Ord(DT)]);
  end;
end;

function OSFDataTypeFixedSize(DT: TOSFDataType): Integer;
begin
  case DT of
    dtBool:    Result := 1;
    dtInt8:    Result := 1;
    dtInt16:   Result := 2;
    dtInt32:   Result := 4;
    dtInt64:   Result := 8;
    dtUInt8:   Result := 1;
    dtUInt16:  Result := 2;
    dtUInt32:  Result := 4;
    dtUInt64:  Result := 8;
    dtFloat:   Result := 4;
    dtDouble:  Result := 8;
    dtString:  Result := 0;
    dtBinary:  Result := 0;
    dtGpsData: Result := 24;
  else
    raise EOSFFormatError.CreateFmt(SOSFUnhandledDataType, [Ord(DT)]);
  end;
end;

function OSFDataTypeIsVariableLength(DT: TOSFDataType): Boolean;
begin
  Result := DT in [dtString, dtBinary];
end;

function OSFChannelTypeFromString(const S: string): TOSFChannelType;
var
  Lower: string;
begin
  Lower := LowerCase(S);
  if      Lower = 'scalar' then Exit(ctScalar)
  else if Lower = 'vector' then Exit(ctVector)
  else if Lower = 'matrix' then Exit(ctMatrix)
  else if Lower = 'binary' then Exit(ctBinary);

  raise EOSFFormatError.CreateFmt(SOSFUnknownChannelType, [S]);
end;

function OSFChannelTypeToString(CT: TOSFChannelType): string;
begin
  case CT of
    ctScalar: Result := 'scalar';
    ctVector: Result := 'vector';
    ctMatrix: Result := 'matrix';
    ctBinary: Result := 'binary';
  else
    raise EOSFFormatError.CreateFmt(SOSFUnhandledChannelType, [Ord(CT)]);
  end;
end;

function OSFBlockTypeFromByte(ControlByte: Byte): TBlockContent;
var
  TypeBits: Byte;
begin
  TypeBits := ControlByte and OSF_BLOCK_TYPE_MASK;
  if Integer(TypeBits) > Ord(bcAbsTimeStampData) then
    raise EOSFFormatError.CreateFmt(SOSFUnknownBlockTypeByte, [TypeBits]);
  Result := TBlockContent(TypeBits);
end;

function OSFBlockHasMultipleValues(ControlByte: Byte): Boolean;
begin
  Result := (ControlByte and OSF_BLOCK_MULTI_VALUE_FLAG) <> 0;
end;

function OSFMakeControlByte(BlockType: TBlockContent; MultiValue: Boolean): Byte;
begin
  Result := Ord(BlockType) and OSF_BLOCK_TYPE_MASK;
  if MultiValue then
    Result := Result or OSF_BLOCK_MULTI_VALUE_FLAG;
end;

function OSFLengthFieldSizeFromInt(Value: Integer): TOSFLengthFieldSize;
begin
  case Value of
    2: Result := lfs2;
    4: Result := lfs4;
  else
    raise EOSFFormatError.CreateFmt(SOSFInvalidLengthFieldSize, [Value]);
  end;
end;

function OSFVersionFromMagic(const Magic: string): TOSFVersion;
begin
  if (Magic = OSF_MAGIC_OSF4) or
     (Magic = OSF_MAGIC_OCEAN_STREAM) or
     (Magic = OSF_MAGIC_OCEAN_STREAM_LEGACY) then
    Result := osvOSF4
  else if Magic = OSF_MAGIC_OSF5 then
    Result := osvOSF5
  else
    Result := osvUnknown;
end;

function OSFUtcNowISO8601: string;
var
  UtcNow: TDateTime;
begin
  UtcNow := TTimeZone.Local.ToUniversalTime(Now);
  Result := FormatDateTime('yyyy-mm-dd"T"hh:nn:ss"."zzz"Z"', UtcNow);
end;

function OSFNowAsUnixNs: Int64;
var
  UtcNow: TDateTime;
begin
  UtcNow := TTimeZone.Local.ToUniversalTime(Now);
  Result := Round((UtcNow - EncodeDate(1970, 1, 1)) * 86400.0 * 1.0e9);
end;

end.
