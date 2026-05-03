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
  System.SysUtils;

const
  // Magic header tokens written as the very first bytes of every OSF file.
  OSF_MAGIC_OSF4         = 'OSF4';
  OSF_MAGIC_OSF5         = 'OSF5';
  OSF_MAGIC_OCEAN_STREAM = 'OCEAN_STREAMING_FORMAT4';  // legacy OSF4 alias

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
  // dtPair and dtTriple are OSF5-only; an OSF4 file must never contain them.
  TOSFDataType = (
    dtBool,
    dtInt8,
    dtInt16,
    dtInt32,
    dtInt64,
    dtFloat,
    dtDouble,
    dtString,
    dtBinary,
    dtCanData,
    dtGpsData,
    dtPair,    // OSF5 only — two doubles, 16 bytes
    dtTriple   // OSF5 only — three doubles, 24 bytes
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

  // CAN frame — 16 bytes. Layout matches the OSF candata binary encoding.
  // Bytes 13-15 are explicit padding required to reach the 16-byte total.
  TOSFCanData = packed record
    CanID   : UInt32;              // 32-bit CAN ID including flags
    DLC     : Byte;                // data length code (0..8)
    Payload : array[0..7] of Byte; // CAN payload bytes
    Pad     : array[0..2] of Byte; // padding to 16 bytes total
  end;

  // GPS position — 24 bytes.
  TOSFGpsData = packed record
    Longitude : Double;
    Latitude  : Double;
    Altitude  : Double;
  end;

  // Two-component double value (OSF5 only) — 16 bytes.
  // Typical uses: X/Y coordinate pairs, force/displacement, real/imaginary.
  TOSFPair = packed record
    V1 : Double;
    V2 : Double;
  end;

  // Three-component double value (OSF5 only) — 24 bytes.
  // Typical uses: 3-axis accelerometer, gyroscope, magnetometer.
  TOSFTriple = packed record
    V1 : Double;
    V2 : Double;
    V3 : Double;
  end;

  // Base exception for all OSF errors.
  EOSFException = class(Exception);

  // Raised when a file's binary structure violates the OSF specification.
  EOSFFormatError = class(EOSFException);

  // Raised when a file version or feature is not supported by this implementation.
  EOSFVersionError = class(EOSFException);

// Compile-time binary layout assertions.
{$IF SizeOf(TOSFCanData) <> 16}
  {$MESSAGE ERROR 'TOSFCanData must be exactly 16 bytes'}
{$ENDIF}
{$IF SizeOf(TOSFGpsData) <> 24}
  {$MESSAGE ERROR 'TOSFGpsData must be exactly 24 bytes'}
{$ENDIF}
{$IF SizeOf(TOSFPair) <> 16}
  {$MESSAGE ERROR 'TOSFPair must be exactly 16 bytes'}
{$ENDIF}
{$IF SizeOf(TOSFTriple) <> 24}
  {$MESSAGE ERROR 'TOSFTriple must be exactly 24 bytes'}
{$ENDIF}

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

implementation

function OSFDataTypeFromString(const S: string): TOSFDataType;
var
  Lower: string;
begin
  Lower := LowerCase(S);
  if      Lower = 'bool'    then Exit(dtBool)
  else if Lower = 'int8'    then Exit(dtInt8)
  else if Lower = 'int16'   then Exit(dtInt16)
  else if Lower = 'int32'   then Exit(dtInt32)
  else if Lower = 'int64'   then Exit(dtInt64)
  else if Lower = 'float'   then Exit(dtFloat)
  else if Lower = 'double'  then Exit(dtDouble)
  else if Lower = 'string'  then Exit(dtString)
  else if Lower = 'binary'  then Exit(dtBinary)
  else if Lower = 'candata' then Exit(dtCanData)
  else if Lower = 'gpsdata' then Exit(dtGpsData)
  else if Lower = 'pair'    then Exit(dtPair)
  else if Lower = 'triple'  then Exit(dtTriple);

  raise EOSFFormatError.CreateFmt('Unknown OSF data type: "%s"', [S]);
end;

function OSFDataTypeToString(DT: TOSFDataType): string;
begin
  case DT of
    dtBool:    Result := 'bool';
    dtInt8:    Result := 'int8';
    dtInt16:   Result := 'int16';
    dtInt32:   Result := 'int32';
    dtInt64:   Result := 'int64';
    dtFloat:   Result := 'float';
    dtDouble:  Result := 'double';
    dtString:  Result := 'string';
    dtBinary:  Result := 'binary';
    dtCanData: Result := 'candata';
    dtGpsData: Result := 'gpsdata';
    dtPair:    Result := 'pair';
    dtTriple:  Result := 'triple';
  else
    raise EOSFFormatError.CreateFmt('Unhandled TOSFDataType value: %d', [Ord(DT)]);
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
    dtFloat:   Result := 4;
    dtDouble:  Result := 8;
    dtString:  Result := 0;
    dtBinary:  Result := 0;
    dtCanData: Result := 16;
    dtGpsData: Result := 24;
    dtPair:    Result := 16;
    dtTriple:  Result := 24;
  else
    raise EOSFFormatError.CreateFmt('Unhandled TOSFDataType value: %d', [Ord(DT)]);
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

  raise EOSFFormatError.CreateFmt('Unknown OSF channel type: "%s"', [S]);
end;

function OSFChannelTypeToString(CT: TOSFChannelType): string;
begin
  case CT of
    ctScalar: Result := 'scalar';
    ctVector: Result := 'vector';
    ctMatrix: Result := 'matrix';
    ctBinary: Result := 'binary';
  else
    raise EOSFFormatError.CreateFmt('Unhandled TOSFChannelType value: %d', [Ord(CT)]);
  end;
end;

function OSFBlockTypeFromByte(ControlByte: Byte): TBlockContent;
var
  TypeBits: Byte;
begin
  TypeBits := ControlByte and OSF_BLOCK_TYPE_MASK;
  if TypeBits > Ord(bcAbsTimeStampData) then
    raise EOSFFormatError.CreateFmt('Unknown block type byte: %d', [TypeBits]);
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
    raise EOSFFormatError.CreateFmt(
      'Invalid sizeoflengthvalue %d — must be 2 or 4', [Value]);
  end;
end;

function OSFVersionFromMagic(const Magic: string): TOSFVersion;
begin
  if (Magic = OSF_MAGIC_OSF4) or (Magic = OSF_MAGIC_OCEAN_STREAM) then
    Result := osvOSF4
  else if Magic = OSF_MAGIC_OSF5 then
    Result := osvOSF5
  else
    Result := osvUnknown;
end;

end.
