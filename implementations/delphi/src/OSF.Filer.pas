// Copyright 2026 Optimeas GmbH
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

unit OSF.Filer;

interface

uses
  System.SysUtils,
  System.Classes,
  System.DateUtils,
  System.Generics.Collections,
  System.JSON,
  Xml.XMLIntf,
  Xml.XMLDoc,
  OSF.Types,
  OSF.Channel,
  OSF.Log;

type
  // File-level metadata written in the OSF meta block.
  // When writing: set fields before calling WriteHeader.
  // When reading: populated by OpenForRead after the meta block is parsed.
  TOSFFileMetadata = record
    CreatedUtc: TDateTime; // UTC; writer defaults to UtcNow if zero
    Creator: string;
    Tag: string;
    Reason: string;
    Comment: string;
    NamespaceSep: string; // default '.'
    Longitude: Double;
    Latitude: Double;
    Altitude: Double;
  end;

  // A single data block read from the stream.
  // Callers use the associated channel's DataType to interpret RawPayload.
  //
  // Layout of RawPayload by BlockType:
  // bcStartData             — N contiguous encoded data values (timestamp in StartTimestampNs)
  // bcContinuedData         — N contiguous encoded data values
  // bcAbsTimeStampData      — N × [int64 timestamp, encoded value] interleaved
  // bcContinuedRelStampData — N × [uint32 delta_ns, encoded value] interleaved (OSF4 only)
  // info block ($FFFF)      — raw UTF-8 XML or JSON (IsInfoBlock = True)
  TOSFDataBlock = record
    ChannelIndex: Word;
    BlockType: TBlockContent;
    MultiValue: Boolean; // bit 7 of the control byte
    SampleCount: UInt32; // 1 when not MultiValue
    StartTimestampNs: Int64; // bcStartData only; 0 for all other types
    SampleRate: Double; // bcStartData only; 0.0 for all other types
    RawPayload: TBytes;
    IsInfoBlock: Boolean; // True when ChannelIndex = $FFFF
  end;

  TOSFFileMode = (fmClosed, fmRead, fmWrite);

  TOSFFile = class
  private
    FStream: TStream;
    FOwnsStream: Boolean;
    FMode: TOSFFileMode;
    FVersion: TOSFVersion;
    FMetaFormat: TOSFMetaFormat;
    FChannels: TObjectList<TOSFChannelDef>;
    FMetadata: TOSFFileMetadata;
    FInfoItems: TList<TOSFMetaItem>;
    FHeaderWritten: Boolean;

    // Logging — copied verbatim from TOSFLoggable because TOSFFile already
    // has an inheritance constraint and cannot subclass TOSFLoggable.
    FOnLog: TOSFLogEvent;
    FDebugEnabled: Boolean;
    // Source filename, set by the file-based OpenForRead/CreateForWrite
    // overloads. Used purely for log message formatting; empty when the
    // user opened the filer on a raw stream.
    FSourceName: string;
    procedure Log(Level: TOSFLogLevel; const Msg: string); overload;
    procedure Log(Level: TOSFLogLevel; const Fmt: string; const Args: array of const); overload;

    // Magic header line + meta block dispatcher.
    procedure ReadMagicAndMeta;

    // JSON meta block — build / parse split into focused helpers.
    function BuildJSONMeta: TBytes;
    procedure AppendJSONFileNode(Parent: TJSONObject);
    procedure AppendJSONChannels(Parent: TJSONObject);
    procedure AppendJSONInfoArray(Parent: TJSONObject);
    procedure ParseJSONMeta(const Data: TBytes);
    procedure ParseJSONFileMetadata(FileNode: TJSONObject);
    procedure ParseJSONChannels(OSFNode: TJSONObject);
    procedure ParseJSONInfo(OSFNode: TJSONObject);

    // XML meta block — build / parse split into focused helpers.
    function BuildXMLMeta: TBytes;
    procedure AppendXMLOpenTag(B: TStringBuilder);
    procedure AppendXMLChannels(B: TStringBuilder);
    procedure AppendXMLInfos(B: TStringBuilder);
    procedure ParseXMLMeta(const Data: TBytes);
    procedure ParseXMLRootAttributes(RootNode: IXMLNode);
    procedure ParseXMLChannels(RootNode: IXMLNode);
    procedure ParseXMLInfos(RootNode: IXMLNode);

    // Block reading — split so each helper stays under 30 lines and the
    // truncation guards are at the top of their function.
    function TryReadChannelIndex(out ChannelIndex: Word): Boolean;
    function ReadInfoBlock(var Block: TOSFDataBlock): Boolean;
    function ReadDataBlock(ChannelIndex: Word; var Block: TOSFDataBlock): Boolean;
    function DecodeBlockPayload(Channel: TOSFChannelDef; const Payload: TBytes; LenField: UInt32; var Block: TOSFDataBlock): Boolean;

    // Shared low-level write — emits channel index, length field, and payload.
    procedure WriteDataBlock(Channel: TOSFChannelDef; const Payload: TBytes);

    // Channel lookup.
    function FindChannel(Index: Integer): TOSFChannelDef;

    // Stream primitives. ReadXxx raise EReadError on short reads.
    function ReadUInt16: Word;
    function ReadUInt32: UInt32;
    procedure WriteUInt16(Value: Word);
    procedure WriteUInt32(Value: UInt32);
    procedure WriteRawBytes(const Data: TBytes);

    function GetChannelCount: Integer;
    function GetInfoItemCount: Integer;
  public
    constructor Create;
    destructor Destroy; override;

    // Detects the OSF version of the stream without consuming it.
    // The stream position is restored before returning. Returns False if the
    // stream does not start with a recognised OSF magic header.
    class function PeekMagic(AStream: TStream; out AVersion: TOSFVersion): Boolean;

    // ── Reading ───────────────────────────────────────────────────────────
    procedure OpenForRead(const FileName: string); overload;
    procedure OpenForRead(AStream: TStream; AOwnsStream: Boolean = False); overload;

    // Reads the next data block from the stream.
    // Returns False at clean EOF or when the stream is truncated mid-block
    // (best-effort: all complete blocks before the truncation point are returned).
    // Never raises on a truncated or partially-written file.
    function ReadNextBlock(out Block: TOSFDataBlock): Boolean;

    // ── Writing ───────────────────────────────────────────────────────────
    procedure CreateForWrite(const FileName: string; AVersion: TOSFVersion = osvOSF5); overload;
    procedure CreateForWrite(AStream: TStream; AOwnsStream: Boolean = False; AVersion: TOSFVersion = osvOSF5); overload;

    // Registers a channel definition. Must be called before WriteHeader.
    // Raises EOSFException on duplicate channel index or duplicate non-empty name.
    // Returns the channel index (= Def.Index).
    function AddChannel(Def: TOSFChannelDef): Integer;

    // Adds a free-form metadata item. Items are written into the OSF5 "info"
    // array or the OSF4 <infos> section. Must be called before WriteHeader.
    procedure AddInfoItem(const AName, AValue: string; const ADataType: string = 'string'; const AUnit: string = '');

    // Commits the magic header and meta block to the stream.
    // Must be called after all AddChannel/AddInfoItem calls and before any
    // WriteXxx data calls. Defaults CreatedUtc to UtcNow and NamespaceSep to '.'
    // if not set. The meta format follows FVersion (XML for OSF4, JSON for OSF5).
    procedure WriteHeader;

    // Writes an equidistant block for ChannelIndex.
    //
    // Only Double values are supported by this method. Double is by far the
    // most common data type for equidistant measurement channels. For other
    // numeric types (int16, float, ...) write raw blocks using the stream directly.
    //
    // FirstTimestampNs > 0 emits bcStartData and opens a new segment for the
    // channel. The very first block of every channel must pass a non-zero
    // timestamp; later in the file, passing a non-zero timestamp again starts
    // an additional segment (used for triggered captures or drift correction).
    // FirstTimestampNs = 0 (the default) emits bcContinuedData and appends to
    // the most recent segment.
    //
    // Channel.SampleRate must be set before the first call. The same value is
    // emitted on every bcStartData block.
    //
    // For OSF4 equidistant channels, also pre-set Channel.StartTimestampNs
    // before calling WriteHeader so the XML <channel> attribute carries the
    // start timestamp in the form expected by OSF4-only readers.
    procedure WriteEquidistantBlock(ChannelIndex: Integer; const Samples: array of Double; FirstTimestampNs: Int64 = 0);

    // Writes a single timestamped sample as a bcAbsTimeStampData block.
    // Value must contain the raw encoded bytes for the channel's DataType.
    procedure WriteTimestampedSample(ChannelIndex: Integer; TimestampNs: Int64; const Value: TBytes);

    // Writes multiple timestamped samples as one bcAbsTimeStampData block.
    // For variable-length channels (string/binary) in multi-sample blocks,
    // a uint32 length prefix is added per value as required by the spec.
    // Length(Timestamps) must equal Length(Values).
    procedure WriteTimestampedBlock(ChannelIndex: Integer; const Timestamps: array of Int64; const Values: array of TBytes);

    // Convenience: write a batch of timestamped doubles in one block.
    procedure WriteTimestampedDoubles(ChannelIndex: Integer; const Timestamps: array of Int64; const Values: array of Double);

    // Flushes and closes the stream.
    procedure Close;

    // ── Properties ────────────────────────────────────────────────────────
    property Version: TOSFVersion read FVersion;
    property MetaFormat: TOSFMetaFormat read FMetaFormat;
    property Channels: TObjectList<TOSFChannelDef> read FChannels;
    property ChannelCount: Integer read GetChannelCount;
    property InfoItems: TList<TOSFMetaItem> read FInfoItems;
    property InfoItemCount: Integer read GetInfoItemCount;
    property Metadata: TOSFFileMetadata read FMetadata write FMetadata;

    // Looks up a channel by name; returns nil if not found.
    function ChannelByName(const Name: string): TOSFChannelDef;
    // Looks up a channel by its Index attribute; returns nil if not found.
    function ChannelByIndex(Index: Integer): TOSFChannelDef;

    // Logging hook — emit human-readable progress / diagnostic messages.
    // Default: unassigned (silent). DebugEnabled gates llDebug messages.
    property DebugEnabled: Boolean read FDebugEnabled write FDebugEnabled;
    property OnLog: TOSFLogEvent read FOnLog write FOnLog;
  end;

resourcestring
  // Lifecycle / mode errors.
  SOSFFileAlreadyOpen = 'TOSFFile: already open';
  SOSFCreateForWriteBadVersion = 'CreateForWrite: AVersion must be osvOSF4 or osvOSF5';
  SOSFAddChannelAfterHeader = 'AddChannel must be called before WriteHeader';
  SOSFAddInfoItemAfterHeader = 'AddInfoItem must be called before WriteHeader';
  SOSFWriteHeaderNotWriteMode = 'WriteHeader requires the file to be open for writing';
  SOSFWriteHeaderAlreadyCalled = 'WriteHeader has already been called';
  SOSFWriteHeaderBadVersion = 'WriteHeader: unsupported FVersion';
  SOSFWriteBeforeHeader = 'WriteHeader must be called before writing data blocks';

  // AddChannel uniqueness errors.
  SOSFDuplicateChannelIndex = 'AddChannel: duplicate channel index %d';
  SOSFDuplicateChannelName = 'AddChannel: duplicate channel name "%s"';

  // Header / meta-block parse errors.
  SOSFUnexpectedEOFInHeader = 'Unexpected end of stream reading magic header';
  SOSFInvalidMagicHeader = 'Invalid OSF magic header: %s';
  SOSFUnsupportedMagicToken = 'Unsupported OSF magic token: "%s"';
  SOSFInvalidMetaBlockSize = 'Invalid meta block size in header: "%s"';
  SOSFUnknownMetaFormat = 'Unknown meta block format: first byte is 0x%02X (expected ''<'' or ''{'')';
  SOSFParseJSONMetaFailed = 'Failed to parse OSF5 JSON meta block';
  SOSFMissingOSFKey = 'JSON meta block is missing the top-level "osf" key';
  SOSFXMLNoRootElement = 'XML meta block has no root element';

  // Data-block read errors.
  SOSFUnknownChannelInBlock = 'Data block references channel index %d which is not defined in the meta block';
  SOSFZeroLengthBlock = 'Zero-length data block for channel %d';

  // Data-block write errors.
  SOSFEquiUnknownChannel = 'WriteEquidistantBlock: unknown channel index %d';
  SOSFEquiNoFirstTimestamp = 'WriteEquidistantBlock: FirstTimestampNs must be provided for the first block of each channel';
  SOSFEquiNoSampleRate = 'WriteEquidistantBlock: Channel.SampleRate must be > 0 before the first equidistant block is written';
  SOSFTimestampedUnknown = 'WriteTimestampedSample: unknown channel index %d';
  SOSFTSBlockUnknown = 'WriteTimestampedBlock: unknown channel index %d';
  SOSFTSBlockLengthMismatch = 'WriteTimestampedBlock: Timestamps and Values lengths must match';
  SOSFTSDoublesLengthMismatch = 'WriteTimestampedDoubles: Timestamps and Values lengths must match';

  // Log messages — informational, debug and warning text emitted via OnLog.
  SOSFLogOpeningFile = 'Opening file for read: %s (%d bytes)';
  SOSFLogDetectedVersion = 'Detected version: %s, meta format: %s';
  SOSFLogChannelsDefined = 'Channels defined in meta block: %d';
  SOSFLogChannelEntry = '  [%d] %s  type=%s  equidistant=%s';
  SOSFLogBlockRead = 'Block: channel=%d  type=%s  samples=%d  bytes=%d';
  SOSFLogTruncatedBlock = 'Truncated block at offset %d — stopping';
  SOSFLogUnknownBlockTypeInfo = 'Unknown block type %d in info block — skipping';
  SOSFLogUnknownChannelInBlock = 'Block references unknown channel index %d — skipping';
  SOSFLogUnknownBlockType = 'Unknown block type %d at offset %d — skipping';
  SOSFLogWritingHeader = 'Writing header: version=%s  channels=%d';
  SOSFLogWriteEquidistant = 'WriteEquidistant: channel=%d  samples=%d';
  SOSFLogWriteTimestamped = 'WriteTimestamped: channel=%d  samples=%d';
  SOSFLogFileClosed = 'File closed: %s  total bytes=%d';

implementation

const
  OSF_FORMAT_OSF5 = 'osf5';

  // ── Local helpers ────────────────────────────────────────────────────────────

function XMLEscape(const S: string): string;
begin
  Result := S;
  Result := StringReplace(Result, '&', '&amp;', [rfReplaceAll]);
  Result := StringReplace(Result, '"', '&quot;', [rfReplaceAll]);
  Result := StringReplace(Result, '<', '&lt;', [rfReplaceAll]);
  Result := StringReplace(Result, '>', '&gt;', [rfReplaceAll]);
end;

function XMLAttrStrLocal(Node: IXMLNode; const AttrName, Default: string): string;
begin
  if Node.HasAttribute(AttrName) then
    Result := Node.Attributes[AttrName]
  else
    Result := Default;
end;

function XMLAttrDoubleLocal(Node: IXMLNode; const AttrName: string; Default: Double): Double;
var
  FS: TFormatSettings;
  S: string;
begin
  if not Node.HasAttribute(AttrName) then
    Exit(Default);
  S := Node.Attributes[AttrName];
  S := StringReplace(S, ',', '.', [rfReplaceAll]);
  FS := TFormatSettings.Invariant;
  Result := StrToFloatDef(S, Default, FS);
end;

function JStr(Obj: TJSONObject; const Key, Default: string): string;
var
  Val: TJSONValue;
begin
  Val := Obj.GetValue(Key);
  if Assigned(Val) then
    Result := Val.Value
  else
    Result := Default;
end;

function JDbl(Obj: TJSONObject; const Key: string; Default: Double): Double;
var
  Val: TJSONValue;
  FS: TFormatSettings;
  S: string;
begin
  Val := Obj.GetValue(Key);
  if not Assigned(Val) then
    Exit(Default);
  if Val is TJSONNumber then
    Exit((Val as TJSONNumber).AsDouble);
  S := Val.Value;
  S := StringReplace(S, ',', '.', [rfReplaceAll]);
  FS := TFormatSettings.Invariant;
  Result := StrToFloatDef(S, Default, FS);
end;

// Formats a TDateTime as UTC ISO 8601 with millisecond precision and 'Z' suffix.
function FormatUTCDateTime(DT: TDateTime): string;
begin
  Result := FormatDateTime('yyyy-mm-dd"T"hh:nn:ss"."zzz"Z"', DT);
end;

// Parses an ISO 8601 datetime string of the form yyyy-mm-ddThh:nn:ss[.zzz][Z].
// Returns 0 on parse failure.
function ParseISO8601DateTime(const S: string): TDateTime;
var
  Y, M, D, H, N, Sec, Ms: Word;
begin
  Result := 0;
  if Length(S) < 19 then
    Exit;
  try
    Y := StrToInt(Copy(S, 1, 4));
    M := StrToInt(Copy(S, 6, 2));
    D := StrToInt(Copy(S, 9, 2));
    H := StrToInt(Copy(S, 12, 2));
    N := StrToInt(Copy(S, 15, 2));
    Sec := StrToInt(Copy(S, 18, 2));
    Ms := 0;
    if (Length(S) >= 23) and (S[20] = '.') then
      Ms := StrToIntDef(Copy(S, 21, 3), 0);
    Result := EncodeDateTime(Y, M, D, H, N, Sec, Ms);
  except
    Result := 0;
  end;
end;

function VersionToLogString(V: TOSFVersion): string;
begin
  case V of
    osvOSF4:
      Result := 'OSF4';
    osvOSF5:
      Result := 'OSF5';
    osvUnknown:
      Result := 'unknown';
  else
    Result := 'unknown';
  end;
end;

function MetaFormatToLogString(F: TOSFMetaFormat): string;
begin
  case F of
    mfXML:
      Result := 'XML';
    mfJSON:
      Result := 'JSON';
  else
    Result := '?';
  end;
end;

function BlockTypeToLogString(BT: TBlockContent): string;
begin
  case BT of
    bcReserved:
      Result := 'reserved';
    bcTrustedTimestamp:
      Result := 'trustedTimestamp';
    bcTimebaseRealign:
      Result := 'timebaseRealign';
    bcStatusEvent:
      Result := 'statusEvent';
    bcMessageEvent:
      Result := 'messageEvent';
    bcContinuedData:
      Result := 'continuedData';
    bcStartData:
      Result := 'startData';
    bcContinuedRelStampData:
      Result := 'continuedRelStampData';
    bcAbsTimeStampData:
      Result := 'absTimeStampData';
  else
    Result := 'unknown';
  end;
end;

function BoolToLogString(B: Boolean): string;
begin
  if B then
    Result := 'true'
  else
    Result := 'false';
end;

// Reads a single LF-terminated ASCII line from the stream. Strips a trailing CR
// if present. Stops at the first LF or after MaxLen characters as a safety bound.
function ReadAsciiLine(AStream: TStream; MaxLen: Integer = 1024): AnsiString;
var
  Ch: AnsiChar;
  BytesRead: Integer;
  Done: Boolean;
begin
  Result := '';
  Done := False;
  while (not Done) and (Length(Result) < MaxLen) do
  begin
    BytesRead := AStream.Read(Ch, 1);
    if BytesRead = 0 then
      Done := True
    else if Ch = #10 then
      Done := True
    else
      Result := Result + Ch;
  end;
  if (Length(Result) > 0) and (Result[Length(Result)] = #13) then
    SetLength(Result, Length(Result) - 1);
end;

// Builds the binary payload of an equidistant data block.
// Layout: [ctrl 1B] [int64 ts 8B + double SampleRate 8B if start] [uint32 N 4B if N>1] [double × N]
function EncodeEquidistantPayload(IsStart: Boolean; FirstTimestampNs: Int64; SampleRate: Double; const Samples: array of Double): TBytes;
var
  CtrlByte: Byte;
  PayloadSize: Integer;
  Pos: Integer;
  N: Integer;
  NVal: UInt32;
begin
  N := Length(Samples);
  if IsStart then
    CtrlByte := OSFMakeControlByte(bcStartData, N > 1)
  else
    CtrlByte := OSFMakeControlByte(bcContinuedData, N > 1);

  PayloadSize := 1;
  if IsStart then
    Inc(PayloadSize, 16); // int64 ts + double SampleRate
  if N > 1 then
    Inc(PayloadSize, 4);
  Inc(PayloadSize, N * SizeOf(Double));

  SetLength(Result, PayloadSize);
  Pos := 0;
  Result[Pos] := CtrlByte;
  Inc(Pos);

  if IsStart then
  begin
    Move(FirstTimestampNs, Result[Pos], 8);
    Inc(Pos, 8);
    Move(SampleRate, Result[Pos], 8);
    Inc(Pos, 8);
  end;

  if N > 1 then
  begin
    NVal := N;
    Move(NVal, Result[Pos], 4);
    Inc(Pos, 4);
  end;

  Move(Samples[0], Result[Pos], N * SizeOf(Double));
end;

// Builds the binary payload of an absolute-timestamped data block.
// Layout: [ctrl 1B] [uint32 N 4B if N>1] [int64 ts | (uint32 len if variable) | bytes]*
//
// For variable-length data types (string, binary), every value is written with
// a trailing 0x00 byte per the spec (revision 2026-05-04). The trailing byte is
// included in the per-value uint32 length when present, and in the block-level
// length field for the single-sample / no-length-prefix case.
function EncodeTimestampedPayload(IsVariableLength: Boolean; const Timestamps: array of Int64; const Values: array of TBytes): TBytes;
const
  ZERO_BYTE: Byte = 0;
var
  N: Integer;
  IsMulti: Boolean;
  CtrlByte: Byte;
  Ms: TMemoryStream;
  I: Integer;
  Cnt: UInt32;
  Len4: UInt32;
  TS: Int64;
  ValueLen: Integer;
begin
  N := Length(Timestamps);
  IsMulti := N > 1;
  CtrlByte := OSFMakeControlByte(bcAbsTimeStampData, IsMulti);

  Ms := TMemoryStream.Create;
  try
    Ms.WriteBuffer(CtrlByte, 1);
    if IsMulti then
    begin
      Cnt := N;
      Ms.WriteBuffer(Cnt, SizeOf(Cnt));
    end;

    for I := 0 to N - 1 do
    begin
      TS := Timestamps[I];
      Ms.WriteBuffer(TS, SizeOf(TS));
      ValueLen := Length(Values[I]);
      // Variable-length values inside a multi-sample block need a uint32
      // length prefix per value. The length includes the trailing null byte.
      // Single-sample blocks don't need a per-value prefix because the block
      // length already determines the value size (also including the null byte).
      if IsVariableLength and IsMulti then
      begin
        Len4 := UInt32(ValueLen + 1); // +1 for the trailing 0x00
        Ms.WriteBuffer(Len4, SizeOf(Len4));
      end;
      if ValueLen > 0 then
        Ms.WriteBuffer(Values[I][0], ValueLen);
      if IsVariableLength then
        Ms.WriteBuffer(ZERO_BYTE, 1);
    end;

    SetLength(Result, Ms.Size);
    if Ms.Size > 0 then
    begin
      Ms.Position := 0;
      Ms.ReadBuffer(Result[0], Ms.Size);
    end;
  finally
    Ms.Free;
  end;
end;

// ── TOSFFile — logging helpers (verbatim copy of TOSFLoggable.Log) ───────────

procedure TOSFFile.Log(Level: TOSFLogLevel; const Msg: string);
begin
  if not Assigned(FOnLog) then
    Exit;
  if (Level = llDebug) and (not FDebugEnabled) then
    Exit;
  try
    FOnLog(Level, Msg);
  except
    // Never let a buggy log handler propagate.
  end;
end;

procedure TOSFFile.Log(Level: TOSFLogLevel; const Fmt: string; const Args: array of const);
begin
  if not Assigned(FOnLog) then
    Exit;
  if (Level = llDebug) and (not FDebugEnabled) then
    Exit;
  try
    FOnLog(Level, Format(Fmt, Args));
  except
    // Never let a buggy log handler or a broken Format string propagate.
  end;
end;

// ── TOSFFile — construction / lifecycle ───────────────────────────────────────

constructor TOSFFile.Create;
begin
  inherited Create;
  FChannels := TObjectList<TOSFChannelDef>.Create(True);
  FInfoItems := TList<TOSFMetaItem>.Create;
  FMode := fmClosed;
  FVersion := osvUnknown;
  FHeaderWritten := False;
end;

destructor TOSFFile.Destroy;
begin
  Close;
  FInfoItems.Free;
  FChannels.Free;
  inherited;
end;

procedure TOSFFile.Close;
var
  TotalBytes: Int64;
  SourceStr: string;
begin
  if FMode = fmClosed then
    Exit;
  TotalBytes := 0;
  if Assigned(FStream) then
    TotalBytes := FStream.Position;
  if FSourceName <> '' then
    SourceStr := FSourceName
  else
    SourceStr := '<stream>';
  try
    if FOwnsStream then
      FreeAndNil(FStream)
    else
      FStream := nil;
  finally
    FMode := fmClosed;
  end;
  // Pre-format with Format() so we exercise the single-string Log overload —
  // the array-of-const overload is exercised by every other call site.
  Log(llInfo, Format(SOSFLogFileClosed, [SourceStr, TotalBytes]));
end;

function TOSFFile.GetChannelCount: Integer;
begin
  Result := FChannels.Count;
end;

function TOSFFile.GetInfoItemCount: Integer;
begin
  Result := FInfoItems.Count;
end;

class function TOSFFile.PeekMagic(AStream: TStream; out AVersion: TOSFVersion): Boolean;
var
  SavePos: Int64;
  Line: AnsiString;
  Parts: TArray<string>;
begin
  Result := False;
  AVersion := osvUnknown;
  if not Assigned(AStream) then
    Exit;

  SavePos := AStream.Position;
  try
    Line := ReadAsciiLine(AStream);
    Parts := string(Line).Split([' ']);
    if Length(Parts) >= 1 then
    begin
      AVersion := OSFVersionFromMagic(Parts[0]);
      Result := AVersion <> osvUnknown;
    end;
  finally
    AStream.Position := SavePos;
  end;
end;

// ── Stream primitives ────────────────────────────────────────────────────────

function TOSFFile.ReadUInt16: Word;
begin
  FStream.ReadBuffer(Result, SizeOf(Result));
end;

function TOSFFile.ReadUInt32: UInt32;
begin
  FStream.ReadBuffer(Result, SizeOf(Result));
end;

procedure TOSFFile.WriteUInt16(Value: Word);
begin
  FStream.WriteBuffer(Value, SizeOf(Value));
end;

procedure TOSFFile.WriteUInt32(Value: UInt32);
begin
  FStream.WriteBuffer(Value, SizeOf(Value));
end;

procedure TOSFFile.WriteRawBytes(const Data: TBytes);
begin
  if Length(Data) > 0 then
    FStream.WriteBuffer(Data[0], Length(Data));
end;

// ── Reading: open + magic header dispatch ────────────────────────────────────

procedure TOSFFile.OpenForRead(const FileName: string);
var
  FS: TFileStream;
begin
  FS := TFileStream.Create(FileName, fmOpenRead or fmShareDenyWrite);
  FSourceName := FileName;
  Log(llInfo, SOSFLogOpeningFile, [FileName, FS.Size]);
  OpenForRead(FS, True);
end;

procedure TOSFFile.OpenForRead(AStream: TStream; AOwnsStream: Boolean);
var
  I: Integer;
  Ch: TOSFChannelDef;
begin
  if FMode <> fmClosed then
    raise EOSFException.Create(SOSFFileAlreadyOpen);
  FStream := AStream;
  FOwnsStream := AOwnsStream;
  FMode := fmRead;
  FChannels.Clear;
  FInfoItems.Clear;
  ReadMagicAndMeta;

  Log(llInfo, SOSFLogDetectedVersion, [VersionToLogString(FVersion), MetaFormatToLogString(FMetaFormat)]);
  Log(llInfo, SOSFLogChannelsDefined, [FChannels.Count]);
  for I := 0 to FChannels.Count - 1 do
  begin
    Ch := FChannels[I];
    Log(llDebug, SOSFLogChannelEntry, [Ch.Index, Ch.Name, OSFDataTypeToString(Ch.DataType), BoolToLogString(Ch.IsEquidistant)]);
  end;
end;

procedure TOSFFile.ReadMagicAndMeta;
var
  Line: AnsiString;
  Parts: TArray<string>;
  MetaSize: Int64;
  MetaBytes: TBytes;
  FirstByte: Byte;
begin
  Line := ReadAsciiLine(FStream);
  if Length(Line) = 0 then
    raise EOSFFormatError.Create(SOSFUnexpectedEOFInHeader);

  Parts := string(Line).Split([' ']);
  if Length(Parts) < 2 then
    raise EOSFFormatError.CreateFmt(SOSFInvalidMagicHeader, [string(Line)]);

  FVersion := OSFVersionFromMagic(Parts[0]);
  if FVersion = osvUnknown then
    raise EOSFVersionError.CreateFmt(SOSFUnsupportedMagicToken, [Parts[0]]);

  MetaSize := StrToInt64Def(Parts[1], -1);
  if MetaSize <= 0 then
    raise EOSFFormatError.CreateFmt(SOSFInvalidMetaBlockSize, [Parts[1]]);

  SetLength(MetaBytes, MetaSize);
  FStream.ReadBuffer(MetaBytes[0], MetaSize);

  // Dispatch on the first byte: '<' = XML (OSF4), '{' = JSON (OSF5).
  FirstByte := MetaBytes[0];
  case Chr(FirstByte) of
    '<':
      FMetaFormat := mfXML;
    '{':
      FMetaFormat := mfJSON;
  else
    raise EOSFFormatError.CreateFmt(SOSFUnknownMetaFormat, [FirstByte]);
  end;

  case FMetaFormat of
    mfXML:
      ParseXMLMeta(MetaBytes);
    mfJSON:
      ParseJSONMeta(MetaBytes);
  end;
end;

// ── Reading: JSON meta block ─────────────────────────────────────────────────

procedure TOSFFile.ParseJSONMeta(const Data: TBytes);
var
  JSONText: string;
  Root: TJSONObject;
  OSFNode: TJSONObject;
  FileNode: TJSONObject;
begin
  JSONText := TEncoding.UTF8.GetString(Data);
  Root := TJSONObject.ParseJSONValue(JSONText) as TJSONObject;
  if not Assigned(Root) then
    raise EOSFFormatError.Create(SOSFParseJSONMetaFailed);
  try
    OSFNode := Root.GetValue('osf') as TJSONObject;
    if not Assigned(OSFNode) then
      raise EOSFFormatError.Create(SOSFMissingOSFKey);

    // Newer files put file metadata in a "file" sub-object; older files put
    // it directly under "osf". Support both.
    FileNode := OSFNode.GetValue('file') as TJSONObject;
    if not Assigned(FileNode) then
      FileNode := OSFNode;

    ParseJSONFileMetadata(FileNode);
    ParseJSONChannels(OSFNode);
    ParseJSONInfo(OSFNode);
  finally
    Root.Free;
  end;
end;

procedure TOSFFile.ParseJSONFileMetadata(FileNode: TJSONObject);
begin
  FMetadata.CreatedUtc := ParseISO8601DateTime(JStr(FileNode, 'created_utc', ''));
  FMetadata.Creator := JStr(FileNode, 'creator', '');
  FMetadata.Tag := JStr(FileNode, 'tag', '');
  FMetadata.Reason := JStr(FileNode, 'reason', '');
  FMetadata.Comment := JStr(FileNode, 'comment', '');
  FMetadata.NamespaceSep := JStr(FileNode, 'namespacesep', OSF_DEFAULT_NAMESPACE_SEP);
  FMetadata.Longitude := JDbl(FileNode, 'created_at_longitude', 0.0);
  FMetadata.Latitude := JDbl(FileNode, 'created_at_latitude', 0.0);
  FMetadata.Altitude := JDbl(FileNode, 'created_at_altitude', 0.0);
end;

procedure TOSFFile.ParseJSONChannels(OSFNode: TJSONObject);
var
  ChanArr: TJSONArray;
  I: Integer;
begin
  ChanArr := OSFNode.GetValue('channels') as TJSONArray;
  if not Assigned(ChanArr) then
    Exit;
  for I := 0 to ChanArr.Count - 1 do
    FChannels.Add(TOSFChannelDef.FromJSONObject(ChanArr.Items[I] as TJSONObject));
end;

procedure TOSFFile.ParseJSONInfo(OSFNode: TJSONObject);
var
  InfoArr: TJSONArray;
  InfoObj: TJSONObject;
  Item: TOSFMetaItem;
  I: Integer;
begin
  InfoArr := OSFNode.GetValue('info') as TJSONArray;
  if not Assigned(InfoArr) then
    Exit;
  for I := 0 to InfoArr.Count - 1 do
  begin
    InfoObj := InfoArr.Items[I] as TJSONObject;
    Item.Name := JStr(InfoObj, 'name', '');
    Item.Value := JStr(InfoObj, 'value', '');
    Item.DataType := JStr(InfoObj, 'datatype', 'string');
    Item.UnitStr := JStr(InfoObj, 'unit', '');
    FInfoItems.Add(Item);
  end;
end;

// ── Reading: XML meta block ──────────────────────────────────────────────────

procedure TOSFFile.ParseXMLMeta(const Data: TBytes);
var
  XMLDoc: IXMLDocument;
  XMLText: string;
  RootNode: IXMLNode;
begin
  XMLText := TEncoding.UTF8.GetString(Data);
  XMLDoc := TXMLDocument.Create(nil);
  XMLDoc.LoadFromXML(XMLText);
  XMLDoc.Active := True;

  RootNode := XMLDoc.DocumentElement;
  if not Assigned(RootNode) then
    raise EOSFFormatError.Create(SOSFXMLNoRootElement);

  ParseXMLRootAttributes(RootNode);
  ParseXMLChannels(RootNode);
  ParseXMLInfos(RootNode);
end;

procedure TOSFFile.ParseXMLRootAttributes(RootNode: IXMLNode);
begin
  // Works for either <optimeas> (OSF4) or <osf> (synthetic) — only attributes.
  if RootNode.HasAttribute('creator') then
    FMetadata.Creator := RootNode.Attributes['creator'];
  if RootNode.HasAttribute('tag') then
    FMetadata.Tag := RootNode.Attributes['tag'];
  if RootNode.HasAttribute('reason') then
    FMetadata.Reason := RootNode.Attributes['reason'];
  if RootNode.HasAttribute('comment') then
    FMetadata.Comment := RootNode.Attributes['comment'];
  if RootNode.HasAttribute('created_utc') then
    FMetadata.CreatedUtc := ParseISO8601DateTime(RootNode.Attributes['created_utc']);
  if RootNode.HasAttribute('namespacesep') then
    FMetadata.NamespaceSep := RootNode.Attributes['namespacesep']
  else
    FMetadata.NamespaceSep := OSF_DEFAULT_NAMESPACE_SEP;
  FMetadata.Longitude := XMLAttrDoubleLocal(RootNode, 'longitude', 0.0);
  FMetadata.Latitude := XMLAttrDoubleLocal(RootNode, 'latitude', 0.0);
  FMetadata.Altitude := XMLAttrDoubleLocal(RootNode, 'altitude', 0.0);
end;

procedure TOSFFile.ParseXMLChannels(RootNode: IXMLNode);
var
  ChansNode: IXMLNode;
  Node: IXMLNode;
  I: Integer;
begin
  ChansNode := RootNode.ChildNodes.FindNode('channels');
  if not Assigned(ChansNode) then
    Exit;
  for I := 0 to ChansNode.ChildNodes.Count - 1 do
  begin
    Node := ChansNode.ChildNodes[I];
    if Node.NodeName = 'channel' then
      FChannels.Add(TOSFChannelDef.FromXMLNode(Node));
  end;
end;

procedure TOSFFile.ParseXMLInfos(RootNode: IXMLNode);
var
  InfosNode: IXMLNode;
  Node: IXMLNode;
  Item: TOSFMetaItem;
  I: Integer;
begin
  InfosNode := RootNode.ChildNodes.FindNode('infos');
  if not Assigned(InfosNode) then
    Exit;
  for I := 0 to InfosNode.ChildNodes.Count - 1 do
  begin
    Node := InfosNode.ChildNodes[I];
    if Node.NodeName = 'info' then
    begin
      Item.Name := XMLAttrStrLocal(Node, 'name', '');
      Item.Value := XMLAttrStrLocal(Node, 'value', '');
      Item.DataType := XMLAttrStrLocal(Node, 'datatype', 'string');
      Item.UnitStr := XMLAttrStrLocal(Node, 'unit', '');
      FInfoItems.Add(Item);
    end;
  end;
end;

// ── Reading: data blocks ─────────────────────────────────────────────────────

function TOSFFile.ReadNextBlock(out Block: TOSFDataBlock): Boolean;
var
  ChannelIndex: Word;
  StartOffset: Int64;
begin
  // Default() initialises managed fields (TBytes) without corrupting refcounts.
  Block := Default (TOSFDataBlock);

  // Record the offset at the start of the block so warnings can pinpoint it.
  StartOffset := 0;
  if Assigned(FStream) then
    StartOffset := FStream.Position;

  if not TryReadChannelIndex(ChannelIndex) then
    Exit(False);
  Block.ChannelIndex := ChannelIndex;

  if ChannelIndex = OSF_INFO_CHANNEL_INDEX then
    Result := ReadInfoBlock(Block)
  else
    Result := ReadDataBlock(ChannelIndex, Block);

  if Result and (not Block.IsInfoBlock) then
    Log(llDebug, SOSFLogBlockRead, [ChannelIndex, BlockTypeToLogString(Block.BlockType), Block.SampleCount, Length(Block.RawPayload)])
  else if (not Result) and (not Block.IsInfoBlock) then
    Log(llWarning, SOSFLogTruncatedBlock, [StartOffset]);
end;

function TOSFFile.TryReadChannelIndex(out ChannelIndex: Word): Boolean;
var
  BytesRead: Integer;
begin
  // A zero-byte read here is clean EOF; a short read is mid-header truncation.
  // Either case: best-effort stop without raising.
  BytesRead := FStream.Read(ChannelIndex, SizeOf(ChannelIndex));
  Result := BytesRead = SizeOf(ChannelIndex);
end;

function TOSFFile.ReadInfoBlock(var Block: TOSFDataBlock): Boolean;
var
  LenField: UInt32;
  Payload: TBytes;
  TypeBits: Byte;
begin
  // The info/trailer block always uses a uint32 length field (spec §$FFFF).
  Block.IsInfoBlock := True;
  try
    LenField := ReadUInt32;
    SetLength(Payload, LenField);
    if LenField > 0 then
      FStream.ReadBuffer(Payload[0], LenField);
  except
    on EReadError do
      Exit(False);
  end;
  if LenField > 0 then
  begin
    TypeBits := Payload[0] and OSF_BLOCK_TYPE_MASK;
    if Integer(TypeBits) > Ord(bcAbsTimeStampData) then
    begin
      Log(llWarning, SOSFLogUnknownBlockTypeInfo, [TypeBits]);
      Exit(False);
    end;
    Block.BlockType := TBlockContent(TypeBits);
    SetLength(Block.RawPayload, LenField - 1);
    if LenField > 1 then
      Move(Payload[1], Block.RawPayload[0], LenField - 1);
  end;
  Result := True;
end;

function TOSFFile.ReadDataBlock(ChannelIndex: Word; var Block: TOSFDataBlock): Boolean;
var
  Channel: TOSFChannelDef;
  LenField: UInt32;
  Payload: TBytes;
begin
  // Channel must have been declared in the meta block; otherwise we cannot
  // know the length-field width and have to stop the best-effort scan here.
  Channel := FindChannel(ChannelIndex);
  if not Assigned(Channel) then
  begin
    Log(llWarning, SOSFLogUnknownChannelInBlock, [ChannelIndex]);
    Exit(False);
  end;

  try
    case Channel.LengthFieldSize of
      lfs2:
        LenField := ReadUInt16;
      lfs4:
        LenField := ReadUInt32;
    else
      LenField := 0;
    end;
    if LenField = 0 then
      raise EOSFFormatError.CreateFmt(SOSFZeroLengthBlock, [ChannelIndex]);
    SetLength(Payload, LenField);
    FStream.ReadBuffer(Payload[0], LenField);
  except
    on EReadError do
      Exit(False); // truncated mid-block — best-effort stop
  end;

  Result := DecodeBlockPayload(Channel, Payload, LenField, Block);
end;

function TOSFFile.DecodeBlockPayload(Channel: TOSFChannelDef; const Payload: TBytes; LenField: UInt32; var Block: TOSFDataBlock): Boolean;
var
  CtrlByte: Byte;
  TypeBits: Byte;
  Offset: Integer;
  RequiredLen: Integer;
  PayloadSize: Integer;
  SampleCount: UInt32;
  Pos: Int64;
  SampleRate: Double;
begin
  Result := False;
  if Length(Payload) < 1 then
    Exit;

  Pos := 0;
  if Assigned(FStream) then
    Pos := FStream.Position;

  // Decode the block type without going through OSFBlockTypeFromByte so we can
  // log a warning instead of letting an unknown type byte raise.
  CtrlByte := Payload[0];
  TypeBits := CtrlByte and OSF_BLOCK_TYPE_MASK;
  if Integer(TypeBits) > Ord(bcAbsTimeStampData) then
  begin
    Log(llWarning, SOSFLogUnknownBlockType, [TypeBits, Pos]);
    Exit;
  end;
  Block.BlockType := TBlockContent(TypeBits);
  Block.MultiValue := OSFBlockHasMultipleValues(CtrlByte);

  // Pre-validate the encoded prefix length so the body needs no further checks.
  // bcStartData carries 8 bytes (int64 timestamp) + 8 bytes (double SampleRate).
  RequiredLen := 1;
  if Block.BlockType = bcStartData then
    Inc(RequiredLen, 16);
  if Block.MultiValue then
    Inc(RequiredLen, 4);
  if Integer(LenField) < RequiredLen then
    Exit;

  Offset := 1;
  if Block.BlockType = bcStartData then
  begin
    Move(Payload[Offset], Block.StartTimestampNs, 8);
    Inc(Offset, 8);
    Channel.LastTimestampNs := Block.StartTimestampNs;
    Move(Payload[Offset], SampleRate, 8);
    Inc(Offset, 8);
    Block.SampleRate := SampleRate;
    Channel.SampleRate := SampleRate;
  end;

  if Block.MultiValue then
  begin
    Move(Payload[Offset], SampleCount, 4);
    Inc(Offset, 4);
    Block.SampleCount := SampleCount;
  end
  else
    Block.SampleCount := 1;

  PayloadSize := Integer(LenField) - Offset;
  if PayloadSize > 0 then
  begin
    SetLength(Block.RawPayload, PayloadSize);
    Move(Payload[Offset], Block.RawPayload[0], PayloadSize);
  end;

  Channel.SampleCount := Channel.SampleCount + Block.SampleCount;
  Result := True;
end;

// ── Writing: open + structural setup ─────────────────────────────────────────

procedure TOSFFile.CreateForWrite(const FileName: string; AVersion: TOSFVersion);
begin
  FSourceName := FileName;
  CreateForWrite(TFileStream.Create(FileName, fmCreate), True, AVersion);
end;

procedure TOSFFile.CreateForWrite(AStream: TStream; AOwnsStream: Boolean; AVersion: TOSFVersion);
begin
  if FMode <> fmClosed then
    raise EOSFException.Create(SOSFFileAlreadyOpen);
  if not(AVersion in [osvOSF4, osvOSF5]) then
    raise EOSFException.Create(SOSFCreateForWriteBadVersion);

  FStream := AStream;
  FOwnsStream := AOwnsStream;
  FMode := fmWrite;
  FVersion := AVersion;
  case AVersion of
    osvOSF4:
      FMetaFormat := mfXML;
    osvOSF5:
      FMetaFormat := mfJSON;
  end;
  FHeaderWritten := False;
  FChannels.Clear;
  FInfoItems.Clear;
end;

function TOSFFile.AddChannel(Def: TOSFChannelDef): Integer;
var
  I: Integer;
begin
  if FHeaderWritten then
    raise EOSFException.Create(SOSFAddChannelAfterHeader);

  // A duplicate index produces an unreadable file (the reader can't tell the
  // two channels apart). A duplicate name is permitted by the spec but almost
  // always indicates a caller bug, so we reject it as well.
  for I := 0 to FChannels.Count - 1 do
  begin
    if FChannels[I].Index = Def.Index then
      raise EOSFException.CreateFmt(SOSFDuplicateChannelIndex, [Def.Index]);
    if (Def.Name <> '') and (FChannels[I].Name = Def.Name) then
      raise EOSFException.CreateFmt(SOSFDuplicateChannelName, [Def.Name]);
  end;

  FChannels.Add(Def);
  Result := Def.Index;
end;

procedure TOSFFile.AddInfoItem(const AName, AValue, ADataType, AUnit: string);
var
  Item: TOSFMetaItem;
begin
  if FHeaderWritten then
    raise EOSFException.Create(SOSFAddInfoItemAfterHeader);
  Item.Name := AName;
  Item.Value := AValue;
  Item.DataType := ADataType;
  Item.UnitStr := AUnit;
  FInfoItems.Add(Item);
end;

// ── Writing: JSON meta block ─────────────────────────────────────────────────

function TOSFFile.BuildJSONMeta: TBytes;
var
  Root: TJSONObject;
  OSFNode: TJSONObject;
begin
  // Build top-down so each freshly created object becomes owned by its parent
  // before the next allocation. Freeing Root cascades through every node.
  Root := TJSONObject.Create;
  try
    OSFNode := TJSONObject.Create;
    Root.AddPair('osf', OSFNode);
    OSFNode.AddPair('format', OSF_FORMAT_OSF5);
    OSFNode.AddPair('version', TJSONNumber.Create(5));

    AppendJSONFileNode(OSFNode);
    AppendJSONChannels(OSFNode);
    AppendJSONInfoArray(OSFNode);

    Result := TEncoding.UTF8.GetBytes(Root.ToJSON);
  finally
    Root.Free;
  end;
end;

procedure TOSFFile.AppendJSONFileNode(Parent: TJSONObject);
var
  FileNode: TJSONObject;
begin
  FileNode := TJSONObject.Create;
  Parent.AddPair('file', FileNode);
  FileNode.AddPair('created_utc', FormatUTCDateTime(FMetadata.CreatedUtc));
  if FMetadata.Creator <> '' then
    FileNode.AddPair('creator', FMetadata.Creator);
  if FMetadata.Tag <> '' then
    FileNode.AddPair('tag', FMetadata.Tag);
  if FMetadata.Reason <> '' then
    FileNode.AddPair('reason', FMetadata.Reason);
  if FMetadata.Comment <> '' then
    FileNode.AddPair('comment', FMetadata.Comment);
  if FMetadata.NamespaceSep <> '' then
    FileNode.AddPair('namespacesep', FMetadata.NamespaceSep);
  if FMetadata.Longitude <> 0 then
    FileNode.AddPair('created_at_longitude', TJSONNumber.Create(FMetadata.Longitude));
  if FMetadata.Latitude <> 0 then
    FileNode.AddPair('created_at_latitude', TJSONNumber.Create(FMetadata.Latitude));
  if FMetadata.Altitude <> 0 then
    FileNode.AddPair('created_at_altitude', TJSONNumber.Create(FMetadata.Altitude));
end;

procedure TOSFFile.AppendJSONChannels(Parent: TJSONObject);
var
  ChanArr: TJSONArray;
  I: Integer;
begin
  ChanArr := TJSONArray.Create;
  Parent.AddPair('channels', ChanArr);
  for I := 0 to FChannels.Count - 1 do
    FChannels[I].AppendJSON(ChanArr);
end;

procedure TOSFFile.AppendJSONInfoArray(Parent: TJSONObject);
var
  InfoArr: TJSONArray;
  InfoObj: TJSONObject;
  Item: TOSFMetaItem;
  I: Integer;
begin
  if FInfoItems.Count = 0 then
    Exit;
  InfoArr := TJSONArray.Create;
  Parent.AddPair('info', InfoArr);
  for I := 0 to FInfoItems.Count - 1 do
  begin
    Item := FInfoItems[I];
    InfoObj := TJSONObject.Create;
    InfoArr.AddElement(InfoObj);
    InfoObj.AddPair('name', Item.Name);
    InfoObj.AddPair('value', Item.Value);
    InfoObj.AddPair('datatype', Item.DataType);
    if Item.UnitStr <> '' then
      InfoObj.AddPair('unit', Item.UnitStr);
  end;
end;

// ── Writing: XML meta block ──────────────────────────────────────────────────

function TOSFFile.BuildXMLMeta: TBytes;
var
  B: TStringBuilder;
begin
  B := TStringBuilder.Create;
  try
    B.Append('<?xml version="1.0" encoding="UTF-8"?>'#10);
    AppendXMLOpenTag(B);
    AppendXMLChannels(B);
    AppendXMLInfos(B);
    B.Append('</optimeas>'#10);
    Result := TEncoding.UTF8.GetBytes(B.ToString);
  finally
    B.Free;
  end;
end;

procedure TOSFFile.AppendXMLOpenTag(B: TStringBuilder);
var
  FS: TFormatSettings;
begin
  FS := TFormatSettings.Invariant;
  B.Append('<optimeas');
  B.AppendFormat(' creator="%s"', [XMLEscape(FMetadata.Creator)]);
  B.AppendFormat(' created_utc="%s"', [FormatUTCDateTime(FMetadata.CreatedUtc)]);
  if FMetadata.Tag <> '' then
    B.AppendFormat(' tag="%s"', [XMLEscape(FMetadata.Tag)]);
  if FMetadata.Reason <> '' then
    B.AppendFormat(' reason="%s"', [XMLEscape(FMetadata.Reason)]);
  if FMetadata.Comment <> '' then
    B.AppendFormat(' comment="%s"', [XMLEscape(FMetadata.Comment)]);
  B.AppendFormat(' namespacesep="%s"', [XMLEscape(FMetadata.NamespaceSep)]);
  if FMetadata.Longitude <> 0 then
    B.AppendFormat(' longitude="%s"', [FloatToStr(FMetadata.Longitude, FS)]);
  if FMetadata.Latitude <> 0 then
    B.AppendFormat(' latitude="%s"', [FloatToStr(FMetadata.Latitude, FS)]);
  if FMetadata.Altitude <> 0 then
    B.AppendFormat(' altitude="%s"', [FloatToStr(FMetadata.Altitude, FS)]);
  B.Append('>'#10);
end;

procedure TOSFFile.AppendXMLChannels(B: TStringBuilder);
var
  I: Integer;
begin
  B.AppendFormat('  <channels count="%d">'#10, [FChannels.Count]);
  for I := 0 to FChannels.Count - 1 do
    FChannels[I].AppendXML(B);
  B.Append('  </channels>'#10);
end;

procedure TOSFFile.AppendXMLInfos(B: TStringBuilder);
var
  I: Integer;
  Item: TOSFMetaItem;
begin
  if FInfoItems.Count = 0 then
    Exit;
  B.Append('  <infos>'#10);
  for I := 0 to FInfoItems.Count - 1 do
  begin
    Item := FInfoItems[I];
    B.Append('    <info');
    B.AppendFormat(' name="%s"', [XMLEscape(Item.Name)]);
    B.AppendFormat(' value="%s"', [XMLEscape(Item.Value)]);
    B.AppendFormat(' datatype="%s"', [XMLEscape(Item.DataType)]);
    if Item.UnitStr <> '' then
      B.AppendFormat(' unit="%s"', [XMLEscape(Item.UnitStr)]);
    B.Append('/>'#10);
  end;
  B.Append('  </infos>'#10);
end;

// ── Writing: header + data blocks ────────────────────────────────────────────

procedure TOSFFile.WriteHeader;
var
  MetaBytes: TBytes;
  HeaderLine: TBytes;
  HeaderStr: string;
  Magic: string;
begin
  if FMode <> fmWrite then
    raise EOSFException.Create(SOSFWriteHeaderNotWriteMode);
  if FHeaderWritten then
    raise EOSFException.Create(SOSFWriteHeaderAlreadyCalled);

  if FMetadata.CreatedUtc = 0 then
    FMetadata.CreatedUtc := TTimeZone.Local.ToUniversalTime(Now);
  if FMetadata.NamespaceSep = '' then
    FMetadata.NamespaceSep := OSF_DEFAULT_NAMESPACE_SEP;

  case FVersion of
    osvOSF4:
      begin
        MetaBytes := BuildXMLMeta;
        Magic := OSF_MAGIC_OSF4;
      end;
    osvOSF5:
      begin
        MetaBytes := BuildJSONMeta;
        Magic := OSF_MAGIC_OSF5;
      end;
  else
    raise EOSFException.Create(SOSFWriteHeaderBadVersion);
  end;

  HeaderStr := Format('%s %d'#10, [Magic, Length(MetaBytes)]);
  HeaderLine := TEncoding.ASCII.GetBytes(HeaderStr);

  FStream.WriteBuffer(HeaderLine[0], Length(HeaderLine));
  FStream.WriteBuffer(MetaBytes[0], Length(MetaBytes));

  FHeaderWritten := True;
  Log(llInfo, SOSFLogWritingHeader, [VersionToLogString(FVersion), FChannels.Count]);
end;

procedure TOSFFile.WriteDataBlock(Channel: TOSFChannelDef; const Payload: TBytes);
begin
  WriteUInt16(Word(Channel.Index));
  case Channel.LengthFieldSize of
    lfs2:
      WriteUInt16(Word(Length(Payload)));
    lfs4:
      WriteUInt32(UInt32(Length(Payload)));
  end;
  WriteRawBytes(Payload);
end;

procedure TOSFFile.WriteEquidistantBlock(ChannelIndex: Integer; const Samples: array of Double; FirstTimestampNs: Int64);
var
  Channel: TOSFChannelDef;
  IsStart: Boolean;
  N: Integer;
  Payload: TBytes;
begin
  if not FHeaderWritten then
    raise EOSFException.Create(SOSFWriteBeforeHeader);

  Channel := ChannelByIndex(ChannelIndex);
  if not Assigned(Channel) then
    raise EOSFFormatError.CreateFmt(SOSFEquiUnknownChannel, [ChannelIndex]);

  N := Length(Samples);
  if N = 0 then
    Exit;

  // A non-zero FirstTimestampNs always starts a new segment (bcStartData).
  // If no timestamp was passed but no segment has been opened yet, the caller
  // forgot to seed the channel — fail loudly.
  IsStart := (FirstTimestampNs <> 0) or (not Channel.StartBlockWritten);
  if IsStart and (FirstTimestampNs = 0) then
    raise EOSFFormatError.Create(SOSFEquiNoFirstTimestamp);
  if IsStart and (Channel.SampleRate <= 0) then
    raise EOSFFormatError.Create(SOSFEquiNoSampleRate);

  Payload := EncodeEquidistantPayload(IsStart, FirstTimestampNs, Channel.SampleRate, Samples);
  WriteDataBlock(Channel, Payload);

  Channel.StartBlockWritten := True;
  Channel.SampleCount := Channel.SampleCount + N;
  if IsStart then
  begin
    Channel.StartTimestampNs := FirstTimestampNs;
    Channel.LastTimestampNs := FirstTimestampNs;
  end;
  Log(llDebug, SOSFLogWriteEquidistant, [ChannelIndex, N]);
end;

procedure TOSFFile.WriteTimestampedSample(ChannelIndex: Integer; TimestampNs: Int64; const Value: TBytes);
begin
  // A single-sample call is just a one-element batch — same byte layout because
  // the multi-value flag stays clear and no count field is written.
  WriteTimestampedBlock(ChannelIndex, [TimestampNs], [Value]);
end;

procedure TOSFFile.WriteTimestampedBlock(ChannelIndex: Integer; const Timestamps: array of Int64; const Values: array of TBytes);
var
  Channel: TOSFChannelDef;
  N: Integer;
  Payload: TBytes;
begin
  if not FHeaderWritten then
    raise EOSFException.Create(SOSFWriteBeforeHeader);

  N := Length(Timestamps);
  if N = 0 then
    Exit;
  if Length(Values) <> N then
    raise EOSFException.Create(SOSFTSBlockLengthMismatch);

  Channel := ChannelByIndex(ChannelIndex);
  if not Assigned(Channel) then
    raise EOSFFormatError.CreateFmt(SOSFTSBlockUnknown, [ChannelIndex]);

  Payload := EncodeTimestampedPayload(OSFDataTypeIsVariableLength(Channel.DataType), Timestamps, Values);
  WriteDataBlock(Channel, Payload);

  Channel.SampleCount := Channel.SampleCount + N;
  Channel.LastTimestampNs := Timestamps[N - 1];
  Log(llDebug, SOSFLogWriteTimestamped, [ChannelIndex, N]);
end;

procedure TOSFFile.WriteTimestampedDoubles(ChannelIndex: Integer; const Timestamps: array of Int64; const Values: array of Double);
var
  EncodedValues: array of TBytes;
  I: Integer;
  D: Double;
begin
  if Length(Values) <> Length(Timestamps) then
    raise EOSFException.Create(SOSFTSDoublesLengthMismatch);
  SetLength(EncodedValues, Length(Values));
  for I := 0 to High(Values) do
  begin
    SetLength(EncodedValues[I], SizeOf(Double));
    D := Values[I];
    Move(D, EncodedValues[I][0], SizeOf(Double));
  end;
  WriteTimestampedBlock(ChannelIndex, Timestamps, EncodedValues);
end;

// ── Lookup ───────────────────────────────────────────────────────────────────

function TOSFFile.FindChannel(Index: Integer): TOSFChannelDef;
var
  I: Integer;
begin
  for I := 0 to FChannels.Count - 1 do
    if FChannels[I].Index = Index then
      Exit(FChannels[I]);
  Result := nil;
end;

function TOSFFile.ChannelByName(const Name: string): TOSFChannelDef;
var
  I: Integer;
begin
  for I := 0 to FChannels.Count - 1 do
    if FChannels[I].Name = Name then
      Exit(FChannels[I]);
  Result := nil;
end;

function TOSFFile.ChannelByIndex(Index: Integer): TOSFChannelDef;
begin
  Result := FindChannel(Index);
end;

end.
