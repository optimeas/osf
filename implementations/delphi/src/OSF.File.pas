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

unit OSF.&File;

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
  OSF.Channel;

type
  // File-level metadata written in the OSF meta block.
  // When writing: set fields before calling WriteHeader.
  // When reading: populated by OpenForRead after the meta block is parsed.
  TOSFFileMetadata = record
    CreatedUtc  : TDateTime;   // UTC; writer defaults to UtcNow if zero
    Creator     : string;
    Tag         : string;
    Reason      : string;
    Comment     : string;
    NamespaceSep: string;      // default '.'
    Longitude   : Double;
    Latitude    : Double;
    Altitude    : Double;
  end;

  // A single data block read from the stream.
  // Callers use the associated channel's DataType to interpret RawPayload.
  //
  // Layout of RawPayload by BlockType:
  //   bcStartData             — N contiguous encoded data values (timestamp in StartTimestampNs)
  //   bcContinuedData         — N contiguous encoded data values
  //   bcAbsTimeStampData      — N × [int64 timestamp, encoded value] interleaved
  //   bcContinuedRelStampData — N × [uint32 delta_ns, encoded value] interleaved (OSF4 only)
  //   info block ($FFFF)      — raw UTF-8 XML or JSON (IsInfoBlock = True)
  TOSFDataBlock = record
    ChannelIndex    : Word;
    BlockType       : TBlockContent;
    MultiValue      : Boolean;          // bit 7 of the control byte
    SampleCount     : UInt32;           // 1 when not MultiValue
    StartTimestampNs: Int64;            // bcStartData only; 0 for all other types
    RawPayload      : TBytes;
    IsInfoBlock     : Boolean;          // True when ChannelIndex = $FFFF
  end;

  TOSFFileMode = (fmClosed, fmRead, fmWrite);

  TOSFFile = class
  private
    FStream        : TStream;
    FOwnsStream    : Boolean;
    FMode          : TOSFFileMode;
    FVersion       : TOSFVersion;
    FMetaFormat    : TOSFMetaFormat;
    FChannels      : TObjectList<TOSFChannelDef>;
    FMetadata      : TOSFFileMetadata;
    FInfoItems     : TList<TOSFMetaItem>;
    FHeaderWritten : Boolean;

    // Parses the magic header line, reads the meta block, and populates
    // FVersion, FMetaFormat, FMetadata, FChannels, and FInfoItems.
    procedure ReadMagicAndMeta;
    procedure ParseXMLMeta(const Data: TBytes);
    procedure ParseJSONMeta(const Data: TBytes);

    // Builds the complete meta block as UTF-8 bytes.
    function  BuildJSONMeta: TBytes;
    function  BuildXMLMeta:  TBytes;

    // Returns the channel with the given index, or nil if not found.
    function  FindChannel(Index: Integer): TOSFChannelDef;

    // Low-level stream primitives. ReadXxx raise EReadError on short reads.
    function  ReadUInt16: Word;
    function  ReadUInt32: UInt32;
    procedure WriteUInt16(Value: Word);
    procedure WriteUInt32(Value: UInt32);
    procedure WriteRawBytes(const Data: TBytes);

    function  GetChannelCount: Integer;
    function  GetInfoItemCount: Integer;
  public
    constructor Create;
    destructor  Destroy; override;

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
    function  ReadNextBlock(out Block: TOSFDataBlock): Boolean;

    // ── Writing ───────────────────────────────────────────────────────────
    procedure CreateForWrite(const FileName: string;
                              AVersion: TOSFVersion = osvOSF5); overload;
    procedure CreateForWrite(AStream: TStream;
                              AOwnsStream: Boolean = False;
                              AVersion: TOSFVersion = osvOSF5); overload;

    // Registers a channel definition. Must be called before WriteHeader.
    // Raises EOSFException on duplicate channel index or duplicate non-empty name.
    // Returns the channel index (= Def.Index).
    function  AddChannel(Def: TOSFChannelDef): Integer;

    // Adds a free-form metadata item. Items are written into the OSF5 "info"
    // array or the OSF4 <infos> section. Must be called before WriteHeader.
    procedure AddInfoItem(const AName, AValue: string;
                           const ADataType: string = 'string';
                           const AUnit: string = '');

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
    // First call per channel: writes bcStartData including FirstTimestampNs.
    //   FirstTimestampNs must be a valid Unix timestamp in nanoseconds.
    // Subsequent calls: writes bcContinuedData; FirstTimestampNs is ignored.
    //
    // For OSF4 equidistant channels, also pre-set Channel.StartTimestampNs
    // before calling WriteHeader so the XML <channel> attribute carries the
    // start timestamp in the form expected by OSF4-only readers.
    procedure WriteEquidistantBlock(ChannelIndex: Integer;
                                     const Samples: array of Double;
                                     FirstTimestampNs: Int64 = 0);

    // Writes a single timestamped sample as a bcAbsTimeStampData block.
    // Value must contain the raw encoded bytes for the channel's DataType.
    procedure WriteTimestampedSample(ChannelIndex: Integer;
                                      TimestampNs: Int64;
                                      const Value: TBytes);

    // Writes multiple timestamped samples as one bcAbsTimeStampData block.
    // For variable-length channels (string/binary) in multi-sample blocks,
    // a uint32 length prefix is added per value as required by the spec.
    // Length(Timestamps) must equal Length(Values).
    procedure WriteTimestampedBlock(ChannelIndex: Integer;
                                     const Timestamps: array of Int64;
                                     const Values: array of TBytes);

    // Convenience: write a batch of timestamped doubles in one block.
    procedure WriteTimestampedDoubles(ChannelIndex: Integer;
                                       const Timestamps: array of Int64;
                                       const Values: array of Double);

    // Flushes and closes the stream.
    procedure Close;

    // ── Properties ────────────────────────────────────────────────────────
    property Version      : TOSFVersion                 read FVersion;
    property MetaFormat   : TOSFMetaFormat              read FMetaFormat;
    property Channels     : TObjectList<TOSFChannelDef> read FChannels;
    property ChannelCount : Integer                     read GetChannelCount;
    property InfoItems    : TList<TOSFMetaItem>         read FInfoItems;
    property InfoItemCount: Integer                     read GetInfoItemCount;
    property Metadata     : TOSFFileMetadata            read FMetadata write FMetadata;

    // Looks up a channel by name; returns nil if not found.
    function ChannelByName(const Name: string): TOSFChannelDef;
    // Looks up a channel by its Index attribute; returns nil if not found.
    function ChannelByIndex(Index: Integer): TOSFChannelDef;
  end;

implementation

const
  OSF_FORMAT_OSF5 = 'osf5';

// ── Local helpers ────────────────────────────────────────────────────────────

function XMLEscape(const S: string): string;
begin
  Result := S;
  Result := StringReplace(Result, '&',  '&amp;',  [rfReplaceAll]);
  Result := StringReplace(Result, '"',  '&quot;', [rfReplaceAll]);
  Result := StringReplace(Result, '<',  '&lt;',   [rfReplaceAll]);
  Result := StringReplace(Result, '>',  '&gt;',   [rfReplaceAll]);
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
  FS : TFormatSettings;
  S  : string;
begin
  if not Node.HasAttribute(AttrName) then
    Exit(Default);
  S  := Node.Attributes[AttrName];
  S  := StringReplace(S, ',', '.', [rfReplaceAll]);
  FS := TFormatSettings.Invariant;
  Result := StrToFloatDef(S, Default, FS);
end;

function JStr(Obj: TJSONObject; const Key, Default: string): string;
var Val: TJSONValue;
begin
  Val := Obj.GetValue(Key);
  if Assigned(Val) then Result := Val.Value else Result := Default;
end;

function JInt(Obj: TJSONObject; const Key: string; Default: Integer): Integer;
var Val: TJSONValue;
begin
  Val := Obj.GetValue(Key);
  if Assigned(Val) then Result := (Val as TJSONNumber).AsInt else Result := Default;
end;

function JDbl(Obj: TJSONObject; const Key: string; Default: Double): Double;
var
  Val : TJSONValue;
  FS  : TFormatSettings;
  S   : string;
begin
  Val := Obj.GetValue(Key);
  if not Assigned(Val) then
    Exit(Default);
  if Val is TJSONNumber then
    Exit((Val as TJSONNumber).AsDouble);
  S  := Val.Value;
  S  := StringReplace(S, ',', '.', [rfReplaceAll]);
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
  if Length(S) < 19 then Exit;
  try
    Y   := StrToInt(Copy(S, 1, 4));
    M   := StrToInt(Copy(S, 6, 2));
    D   := StrToInt(Copy(S, 9, 2));
    H   := StrToInt(Copy(S, 12, 2));
    N   := StrToInt(Copy(S, 15, 2));
    Sec := StrToInt(Copy(S, 18, 2));
    Ms  := 0;
    if (Length(S) >= 23) and (S[20] = '.') then
      Ms := StrToIntDef(Copy(S, 21, 3), 0);
    Result := EncodeDateTime(Y, M, D, H, N, Sec, Ms);
  except
    Result := 0;
  end;
end;

// Reads a single LF-terminated ASCII line from the stream. Strips a trailing CR
// if present. Stops at the first LF or after MaxLen characters as a safety bound.
function ReadAsciiLine(AStream: TStream; MaxLen: Integer = 1024): AnsiString;
var
  Ch        : AnsiChar;
  BytesRead : Integer;
begin
  Result := '';
  while Length(Result) < MaxLen do
  begin
    BytesRead := AStream.Read(Ch, 1);
    if BytesRead = 0 then Exit;
    if Ch = #10 then Break;
    Result := Result + Ch;
  end;
  if (Length(Result) > 0) and (Result[Length(Result)] = #13) then
    SetLength(Result, Length(Result) - 1);
end;

// ── TOSFFile ──────────────────────────────────────────────────────────────────

constructor TOSFFile.Create;
begin
  inherited Create;
  FChannels      := TObjectList<TOSFChannelDef>.Create(True);
  FInfoItems     := TList<TOSFMetaItem>.Create;
  FMode          := fmClosed;
  FVersion       := osvUnknown;
  FHeaderWritten := False;
end;

destructor TOSFFile.Destroy;
begin
  Close;
  FInfoItems.Free;
  FChannels.Free;
  inherited;
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
  SavePos : Int64;
  Line    : AnsiString;
  Parts   : TArray<string>;
begin
  Result   := False;
  AVersion := osvUnknown;
  if not Assigned(AStream) then Exit;

  SavePos := AStream.Position;
  try
    Line  := ReadAsciiLine(AStream);
    Parts := string(Line).Split([' ']);
    if Length(Parts) >= 1 then
    begin
      AVersion := OSFVersionFromMagic(Parts[0]);
      Result   := AVersion <> osvUnknown;
    end;
  finally
    AStream.Position := SavePos;
  end;
end;

// ── Stream primitives ─────────────────────────────────────────────────────────

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

// ── Reading ───────────────────────────────────────────────────────────────────

procedure TOSFFile.OpenForRead(const FileName: string);
begin
  OpenForRead(TFileStream.Create(FileName, fmOpenRead or fmShareDenyWrite), True);
end;

procedure TOSFFile.OpenForRead(AStream: TStream; AOwnsStream: Boolean);
begin
  if FMode <> fmClosed then
    raise EOSFException.Create('TOSFFile: already open');
  FStream     := AStream;
  FOwnsStream := AOwnsStream;
  FMode       := fmRead;
  FChannels.Clear;
  FInfoItems.Clear;
  ReadMagicAndMeta;
end;

procedure TOSFFile.ReadMagicAndMeta;
var
  Line      : AnsiString;
  Parts     : TArray<string>;
  MetaSize  : Int64;
  MetaBytes : TBytes;
  FirstByte : Byte;
begin
  Line := ReadAsciiLine(FStream);
  if Length(Line) = 0 then
    raise EOSFFormatError.Create('Unexpected end of stream reading magic header');

  Parts := string(Line).Split([' ']);
  if Length(Parts) < 2 then
    raise EOSFFormatError.Create('Invalid OSF magic header: ' + string(Line));

  FVersion := OSFVersionFromMagic(Parts[0]);
  if FVersion = osvUnknown then
    raise EOSFVersionError.CreateFmt('Unsupported OSF magic token: "%s"', [Parts[0]]);

  MetaSize := StrToInt64Def(Parts[1], -1);
  if MetaSize <= 0 then
    raise EOSFFormatError.CreateFmt('Invalid meta block size in header: "%s"', [Parts[1]]);

  // Read the complete meta block.
  SetLength(MetaBytes, MetaSize);
  FStream.ReadBuffer(MetaBytes[0], MetaSize);

  // Determine meta format from the first byte: '<' = XML, '{' = JSON.
  FirstByte := MetaBytes[0];
  case Chr(FirstByte) of
    '<': FMetaFormat := mfXML;
    '{': FMetaFormat := mfJSON;
  else
    raise EOSFFormatError.CreateFmt(
      'Unknown meta block format: first byte is 0x%02X (expected ''<'' or ''{'')', [FirstByte]);
  end;

  case FMetaFormat of
    mfXML:  ParseXMLMeta(MetaBytes);
    mfJSON: ParseJSONMeta(MetaBytes);
  end;
end;

procedure TOSFFile.ParseJSONMeta(const Data: TBytes);
var
  JSONText : string;
  Root     : TJSONObject;
  OSFNode  : TJSONObject;
  FileNode : TJSONObject;
  ChanArr  : TJSONArray;
  InfoArr  : TJSONArray;
  I        : Integer;
  Ch       : TOSFChannelDef;
  InfoObj  : TJSONObject;
  Item     : TOSFMetaItem;
begin
  JSONText := TEncoding.UTF8.GetString(Data);
  Root := TJSONObject.ParseJSONValue(JSONText) as TJSONObject;
  if not Assigned(Root) then
    raise EOSFFormatError.Create('Failed to parse OSF5 JSON meta block');
  try
    OSFNode := Root.GetValue('osf') as TJSONObject;
    if not Assigned(OSFNode) then
      raise EOSFFormatError.Create('JSON meta block is missing the top-level "osf" key');

    // Newer files put file metadata in a "file" sub-object; older files put
    // it directly under "osf". Support both.
    FileNode := OSFNode.GetValue('file') as TJSONObject;
    if not Assigned(FileNode) then
      FileNode := OSFNode;

    FMetadata.CreatedUtc   := ParseISO8601DateTime(JStr(FileNode, 'created_utc', ''));
    FMetadata.Creator      := JStr(FileNode, 'creator',                 '');
    FMetadata.Tag          := JStr(FileNode, 'tag',                     '');
    FMetadata.Reason       := JStr(FileNode, 'reason',                  '');
    FMetadata.Comment      := JStr(FileNode, 'comment',                 '');
    FMetadata.NamespaceSep := JStr(FileNode, 'namespacesep',            OSF_DEFAULT_NAMESPACE_SEP);
    FMetadata.Longitude    := JDbl(FileNode, 'created_at_longitude',    0.0);
    FMetadata.Latitude     := JDbl(FileNode, 'created_at_latitude',     0.0);
    FMetadata.Altitude     := JDbl(FileNode, 'created_at_altitude',     0.0);

    ChanArr := OSFNode.GetValue('channels') as TJSONArray;
    if Assigned(ChanArr) then
      for I := 0 to ChanArr.Count - 1 do
      begin
        Ch := TOSFChannelDef.FromJSONObject(ChanArr.Items[I] as TJSONObject);
        FChannels.Add(Ch);
      end;

    InfoArr := OSFNode.GetValue('info') as TJSONArray;
    if Assigned(InfoArr) then
      for I := 0 to InfoArr.Count - 1 do
      begin
        InfoObj := InfoArr.Items[I] as TJSONObject;
        Item.Name     := JStr(InfoObj, 'name',     '');
        Item.Value    := JStr(InfoObj, 'value',    '');
        Item.DataType := JStr(InfoObj, 'datatype', 'string');
        Item.UnitStr  := JStr(InfoObj, 'unit',     '');
        FInfoItems.Add(Item);
      end;
  finally
    Root.Free;
  end;
end;

procedure TOSFFile.ParseXMLMeta(const Data: TBytes);
var
  XMLDoc      : IXMLDocument;
  XMLText     : string;
  RootNode    : IXMLNode;
  ChansNode   : IXMLNode;
  InfosNode   : IXMLNode;
  I           : Integer;
  Node        : IXMLNode;
  Ch          : TOSFChannelDef;
  Item        : TOSFMetaItem;
begin
  XMLText := TEncoding.UTF8.GetString(Data);
  XMLDoc  := TXMLDocument.Create(nil);
  XMLDoc.LoadFromXML(XMLText);
  XMLDoc.Active := True;

  RootNode := XMLDoc.DocumentElement;
  if not Assigned(RootNode) then
    raise EOSFFormatError.Create('XML meta block has no root element');

  // File-level metadata from the root element's attributes. Works for either
  // <optimeas> (OSF4) or <osf> (synthetic) — we only look at the attributes.
  if RootNode.HasAttribute('creator')      then FMetadata.Creator      := RootNode.Attributes['creator'];
  if RootNode.HasAttribute('tag')          then FMetadata.Tag          := RootNode.Attributes['tag'];
  if RootNode.HasAttribute('reason')       then FMetadata.Reason       := RootNode.Attributes['reason'];
  if RootNode.HasAttribute('comment')      then FMetadata.Comment      := RootNode.Attributes['comment'];
  if RootNode.HasAttribute('created_utc')  then
    FMetadata.CreatedUtc := ParseISO8601DateTime(RootNode.Attributes['created_utc']);
  if RootNode.HasAttribute('namespacesep') then FMetadata.NamespaceSep := RootNode.Attributes['namespacesep']
  else                                          FMetadata.NamespaceSep := OSF_DEFAULT_NAMESPACE_SEP;
  FMetadata.Longitude := XMLAttrDoubleLocal(RootNode, 'longitude', 0.0);
  FMetadata.Latitude  := XMLAttrDoubleLocal(RootNode, 'latitude',  0.0);
  FMetadata.Altitude  := XMLAttrDoubleLocal(RootNode, 'altitude',  0.0);

  // Channel definitions.
  ChansNode := RootNode.ChildNodes.FindNode('channels');
  if Assigned(ChansNode) then
    for I := 0 to ChansNode.ChildNodes.Count - 1 do
    begin
      Node := ChansNode.ChildNodes[I];
      if Node.NodeName = 'channel' then
      begin
        Ch := TOSFChannelDef.FromXMLNode(Node);
        FChannels.Add(Ch);
      end;
    end;

  // Free-form metadata items.
  InfosNode := RootNode.ChildNodes.FindNode('infos');
  if Assigned(InfosNode) then
    for I := 0 to InfosNode.ChildNodes.Count - 1 do
    begin
      Node := InfosNode.ChildNodes[I];
      if Node.NodeName = 'info' then
      begin
        Item.Name     := XMLAttrStrLocal(Node, 'name',     '');
        Item.Value    := XMLAttrStrLocal(Node, 'value',    '');
        Item.DataType := XMLAttrStrLocal(Node, 'datatype', 'string');
        Item.UnitStr  := XMLAttrStrLocal(Node, 'unit',     '');
        FInfoItems.Add(Item);
      end;
    end;
end;

function TOSFFile.ReadNextBlock(out Block: TOSFDataBlock): Boolean;
var
  BytesRead   : Integer;
  ChannelIndex: Word;
  LenField    : UInt32;
  Payload     : TBytes;
  CtrlByte    : Byte;
  Channel     : TOSFChannelDef;
  SampleCount : UInt32;
  Offset      : Integer;
  PayloadSize : Integer;
begin
  Result := False;
  // Default() is required here because TOSFDataBlock contains TBytes (a managed type);
  // FillChar on managed fields corrupts reference counts.
  Block := Default(TOSFDataBlock);

  // Try to read the 2-byte channel index. A zero-byte read here is clean EOF.
  BytesRead := FStream.Read(ChannelIndex, SizeOf(ChannelIndex));
  if BytesRead = 0 then Exit;
  if BytesRead < SizeOf(ChannelIndex) then Exit;  // Truncated — best-effort stop.
  Block.ChannelIndex := ChannelIndex;

  if ChannelIndex = OSF_INFO_CHANNEL_INDEX then
  begin
    // The info/trailer block always uses a uint32 length field (spec 0xFFFF section).
    Block.IsInfoBlock := True;
    try
      LenField := ReadUInt32;
      SetLength(Payload, LenField);
      if LenField > 0 then
        FStream.ReadBuffer(Payload[0], LenField);
    except
      on EReadError do Exit;
    end;
    if LenField > 0 then
    begin
      Block.BlockType := OSFBlockTypeFromByte(Payload[0]);
      SetLength(Block.RawPayload, LenField - 1);
      if LenField > 1 then
        Move(Payload[1], Block.RawPayload[0], LenField - 1);
    end;
    Result := True;
    Exit;
  end;

  // Regular data block: look up the channel to determine the length field width.
  Channel := FindChannel(ChannelIndex);
  if not Assigned(Channel) then
    raise EOSFFormatError.CreateFmt(
      'Data block references channel index %d which is not defined in the meta block',
      [ChannelIndex]);

  try
    case Channel.LengthFieldSize of
      lfs2: LenField := ReadUInt16;
      lfs4: LenField := ReadUInt32;
    else
      LenField := 0;
    end;

    if LenField = 0 then
      raise EOSFFormatError.CreateFmt('Zero-length data block for channel %d', [ChannelIndex]);

    SetLength(Payload, LenField);
    FStream.ReadBuffer(Payload[0], LenField);
  except
    on EReadError do Exit;  // Truncated block — stop cleanly.
  end;

  CtrlByte         := Payload[0];
  Block.BlockType  := OSFBlockTypeFromByte(CtrlByte);
  Block.MultiValue := OSFBlockHasMultipleValues(CtrlByte);
  Offset           := 1;

  // bcStartData carries an absolute start timestamp before the data values.
  if Block.BlockType = bcStartData then
  begin
    if Integer(LenField) < Offset + 8 then Exit;
    Move(Payload[Offset], Block.StartTimestampNs, 8);
    Inc(Offset, 8);
    Channel.LastTimestampNs := Block.StartTimestampNs;
  end;

  // When bit 7 is set the block contains N > 1 samples; count is stored as uint32.
  if Block.MultiValue then
  begin
    if Integer(LenField) < Offset + 4 then Exit;
    Move(Payload[Offset], SampleCount, 4);
    Inc(Offset, 4);
    Block.SampleCount := SampleCount;
  end
  else
    Block.SampleCount := 1;

  // Everything remaining is the raw encoded data area.
  PayloadSize := Integer(LenField) - Offset;
  if PayloadSize > 0 then
  begin
    SetLength(Block.RawPayload, PayloadSize);
    Move(Payload[Offset], Block.RawPayload[0], PayloadSize);
  end;

  Channel.SampleCount := Channel.SampleCount + Block.SampleCount;

  Result := True;
end;

// ── Writing ───────────────────────────────────────────────────────────────────

procedure TOSFFile.CreateForWrite(const FileName: string; AVersion: TOSFVersion);
begin
  CreateForWrite(TFileStream.Create(FileName, fmCreate), True, AVersion);
end;

procedure TOSFFile.CreateForWrite(AStream: TStream; AOwnsStream: Boolean;
                                   AVersion: TOSFVersion);
begin
  if FMode <> fmClosed then
    raise EOSFException.Create('TOSFFile: already open');
  if not (AVersion in [osvOSF4, osvOSF5]) then
    raise EOSFException.Create('CreateForWrite: AVersion must be osvOSF4 or osvOSF5');

  FStream        := AStream;
  FOwnsStream    := AOwnsStream;
  FMode          := fmWrite;
  FVersion       := AVersion;
  case AVersion of
    osvOSF4: FMetaFormat := mfXML;
    osvOSF5: FMetaFormat := mfJSON;
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
    raise EOSFException.Create('AddChannel must be called before WriteHeader');

  // A duplicate index produces an unreadable file (the reader can't tell the
  // two channels apart). A duplicate name is permitted by the spec but almost
  // always indicates a caller bug, so we reject it as well.
  for I := 0 to FChannels.Count - 1 do
  begin
    if FChannels[I].Index = Def.Index then
      raise EOSFException.CreateFmt(
        'AddChannel: duplicate channel index %d', [Def.Index]);
    if (Def.Name <> '') and (FChannels[I].Name = Def.Name) then
      raise EOSFException.CreateFmt(
        'AddChannel: duplicate channel name "%s"', [Def.Name]);
  end;

  FChannels.Add(Def);
  Result := Def.Index;
end;

procedure TOSFFile.AddInfoItem(const AName, AValue, ADataType, AUnit: string);
var
  Item: TOSFMetaItem;
begin
  if FHeaderWritten then
    raise EOSFException.Create('AddInfoItem must be called before WriteHeader');
  Item.Name     := AName;
  Item.Value    := AValue;
  Item.DataType := ADataType;
  Item.UnitStr  := AUnit;
  FInfoItems.Add(Item);
end;

function TOSFFile.BuildJSONMeta: TBytes;
var
  Root     : TJSONObject;
  OSFNode  : TJSONObject;
  FileNode : TJSONObject;
  ChanArr  : TJSONArray;
  InfoArr  : TJSONArray;
  InfoObj  : TJSONObject;
  I        : Integer;
  Item     : TOSFMetaItem;
begin
  // Build top-down so each freshly created object becomes owned by its parent
  // before the next allocation. If any later step raises, freeing Root cascades
  // through every node and no orphan leaks.
  Root := TJSONObject.Create;
  try
    OSFNode := TJSONObject.Create;
    Root.AddPair('osf', OSFNode);
    OSFNode.AddPair('format',  OSF_FORMAT_OSF5);
    OSFNode.AddPair('version', TJSONNumber.Create(5));

    FileNode := TJSONObject.Create;
    OSFNode.AddPair('file', FileNode);
    FileNode.AddPair('created_utc', FormatUTCDateTime(FMetadata.CreatedUtc));
    if FMetadata.Creator      <> '' then FileNode.AddPair('creator',      FMetadata.Creator);
    if FMetadata.Tag          <> '' then FileNode.AddPair('tag',          FMetadata.Tag);
    if FMetadata.Reason       <> '' then FileNode.AddPair('reason',       FMetadata.Reason);
    if FMetadata.Comment      <> '' then FileNode.AddPair('comment',      FMetadata.Comment);
    if FMetadata.NamespaceSep <> '' then FileNode.AddPair('namespacesep', FMetadata.NamespaceSep);
    if FMetadata.Longitude    <> 0  then
      FileNode.AddPair('created_at_longitude', TJSONNumber.Create(FMetadata.Longitude));
    if FMetadata.Latitude     <> 0  then
      FileNode.AddPair('created_at_latitude',  TJSONNumber.Create(FMetadata.Latitude));
    if FMetadata.Altitude     <> 0  then
      FileNode.AddPair('created_at_altitude',  TJSONNumber.Create(FMetadata.Altitude));

    ChanArr := TJSONArray.Create;
    OSFNode.AddPair('channels', ChanArr);
    for I := 0 to FChannels.Count - 1 do
      FChannels[I].AppendJSON(ChanArr);

    if FInfoItems.Count > 0 then
    begin
      InfoArr := TJSONArray.Create;
      OSFNode.AddPair('info', InfoArr);
      for I := 0 to FInfoItems.Count - 1 do
      begin
        Item    := FInfoItems[I];
        InfoObj := TJSONObject.Create;
        InfoArr.AddElement(InfoObj);
        InfoObj.AddPair('name',     Item.Name);
        InfoObj.AddPair('value',    Item.Value);
        InfoObj.AddPair('datatype', Item.DataType);
        if Item.UnitStr <> '' then
          InfoObj.AddPair('unit', Item.UnitStr);
      end;
    end;

    Result := TEncoding.UTF8.GetBytes(Root.ToJSON);
  finally
    Root.Free;
  end;
end;

function TOSFFile.BuildXMLMeta: TBytes;
var
  B    : TStringBuilder;
  FS   : TFormatSettings;
  I    : Integer;
  Item : TOSFMetaItem;
begin
  FS := TFormatSettings.Invariant;
  B  := TStringBuilder.Create;
  try
    B.AppendLine('<?xml version="1.0" encoding="UTF-8"?>');
    B.Append('<optimeas');
    B.AppendFormat(' creator="%s"', [XMLEscape(FMetadata.Creator)]);
    B.AppendFormat(' created_utc="%s"', [FormatUTCDateTime(FMetadata.CreatedUtc)]);
    if FMetadata.Tag       <> '' then B.AppendFormat(' tag="%s"',     [XMLEscape(FMetadata.Tag)]);
    if FMetadata.Reason    <> '' then B.AppendFormat(' reason="%s"',  [XMLEscape(FMetadata.Reason)]);
    if FMetadata.Comment   <> '' then B.AppendFormat(' comment="%s"', [XMLEscape(FMetadata.Comment)]);
    B.AppendFormat(' namespacesep="%s"', [XMLEscape(FMetadata.NamespaceSep)]);
    if FMetadata.Longitude <> 0  then B.AppendFormat(' longitude="%s"', [FloatToStr(FMetadata.Longitude, FS)]);
    if FMetadata.Latitude  <> 0  then B.AppendFormat(' latitude="%s"',  [FloatToStr(FMetadata.Latitude,  FS)]);
    if FMetadata.Altitude  <> 0  then B.AppendFormat(' altitude="%s"',  [FloatToStr(FMetadata.Altitude,  FS)]);
    B.AppendLine('>');

    B.AppendFormat('  <channels count="%d">', [FChannels.Count]);
    B.AppendLine;
    for I := 0 to FChannels.Count - 1 do
      FChannels[I].AppendXML(B);
    B.AppendLine('  </channels>');

    if FInfoItems.Count > 0 then
    begin
      B.AppendLine('  <infos>');
      for I := 0 to FInfoItems.Count - 1 do
      begin
        Item := FInfoItems[I];
        B.Append('    <info');
        B.AppendFormat(' name="%s"',     [XMLEscape(Item.Name)]);
        B.AppendFormat(' value="%s"',    [XMLEscape(Item.Value)]);
        B.AppendFormat(' datatype="%s"', [XMLEscape(Item.DataType)]);
        if Item.UnitStr <> '' then
          B.AppendFormat(' unit="%s"', [XMLEscape(Item.UnitStr)]);
        B.AppendLine('/>');
      end;
      B.AppendLine('  </infos>');
    end;

    B.AppendLine('</optimeas>');
    Result := TEncoding.UTF8.GetBytes(B.ToString);
  finally
    B.Free;
  end;
end;

procedure TOSFFile.WriteHeader;
var
  MetaBytes  : TBytes;
  HeaderLine : TBytes;
  HeaderStr  : string;
  Magic      : string;
begin
  if FMode <> fmWrite then
    raise EOSFException.Create('WriteHeader requires the file to be open for writing');
  if FHeaderWritten then
    raise EOSFException.Create('WriteHeader has already been called');

  if FMetadata.CreatedUtc = 0 then
    FMetadata.CreatedUtc := TTimeZone.Local.ToUniversalTime(Now);
  if FMetadata.NamespaceSep = '' then
    FMetadata.NamespaceSep := OSF_DEFAULT_NAMESPACE_SEP;

  case FVersion of
    osvOSF4:
      begin
        MetaBytes := BuildXMLMeta;
        Magic     := OSF_MAGIC_OSF4;
      end;
    osvOSF5:
      begin
        MetaBytes := BuildJSONMeta;
        Magic     := OSF_MAGIC_OSF5;
      end;
  else
    raise EOSFException.Create('WriteHeader: unsupported FVersion');
  end;

  // Magic header line: "<MAGIC> <metabytecount>\n"
  HeaderStr  := Format('%s %d'#10, [Magic, Length(MetaBytes)]);
  HeaderLine := TEncoding.ASCII.GetBytes(HeaderStr);

  FStream.WriteBuffer(HeaderLine[0], Length(HeaderLine));
  FStream.WriteBuffer(MetaBytes[0],  Length(MetaBytes));

  FHeaderWritten := True;
end;

procedure TOSFFile.WriteEquidistantBlock(ChannelIndex: Integer;
                                          const Samples: array of Double;
                                          FirstTimestampNs: Int64);
var
  Channel    : TOSFChannelDef;
  IsStart    : Boolean;
  CtrlByte   : Byte;
  N          : Integer;
  PayloadSize: Integer;
  Buf        : TBytes;
  Pos        : Integer;
  NVal       : UInt32;
begin
  if not FHeaderWritten then
    raise EOSFException.Create('WriteHeader must be called before writing data blocks');

  Channel := ChannelByIndex(ChannelIndex);
  if not Assigned(Channel) then
    raise EOSFFormatError.CreateFmt('WriteEquidistantBlock: unknown channel index %d', [ChannelIndex]);

  N       := Length(Samples);
  IsStart := not Channel.StartBlockWritten;

  if N = 0 then Exit;

  if IsStart and (FirstTimestampNs = 0) then
    raise EOSFFormatError.Create(
      'WriteEquidistantBlock: FirstTimestampNs must be provided for the first block of each channel');

  if IsStart then
    CtrlByte := OSFMakeControlByte(bcStartData, N > 1)
  else
    CtrlByte := OSFMakeControlByte(bcContinuedData, N > 1);

  // Payload: [ctrl 1B] [int64 ts 8B if start] [uint32 N 4B if N>1] [double×N]
  PayloadSize := 1;
  if IsStart then Inc(PayloadSize, 8);
  if N > 1   then Inc(PayloadSize, 4);
  Inc(PayloadSize, N * SizeOf(Double));

  SetLength(Buf, PayloadSize);
  Pos      := 0;
  Buf[Pos] := CtrlByte;
  Inc(Pos);

  if IsStart then
  begin
    Move(FirstTimestampNs, Buf[Pos], 8);
    Inc(Pos, 8);
  end;

  if N > 1 then
  begin
    NVal := N;
    Move(NVal, Buf[Pos], 4);
    Inc(Pos, 4);
  end;

  Move(Samples[0], Buf[Pos], N * SizeOf(Double));

  WriteUInt16(Word(ChannelIndex));
  case Channel.LengthFieldSize of
    lfs2: WriteUInt16(Word(PayloadSize));
    lfs4: WriteUInt32(UInt32(PayloadSize));
  end;
  WriteRawBytes(Buf);

  Channel.StartBlockWritten := True;
  Channel.SampleCount       := Channel.SampleCount + N;
  if IsStart then
  begin
    Channel.StartTimestampNs := FirstTimestampNs;
    Channel.LastTimestampNs  := FirstTimestampNs;
  end;
end;

procedure TOSFFile.WriteTimestampedSample(ChannelIndex: Integer;
                                           TimestampNs: Int64;
                                           const Value: TBytes);
var
  Channel    : TOSFChannelDef;
  CtrlByte   : Byte;
  PayloadSize: Integer;
  Buf        : TBytes;
begin
  if not FHeaderWritten then
    raise EOSFException.Create('WriteHeader must be called before writing data blocks');

  Channel := ChannelByIndex(ChannelIndex);
  if not Assigned(Channel) then
    raise EOSFFormatError.CreateFmt('WriteTimestampedSample: unknown channel index %d', [ChannelIndex]);

  // Single sample: bit 7 = 0, no sample-count field, no per-value length prefix
  // (the block length tells the reader the value size).
  CtrlByte    := OSFMakeControlByte(bcAbsTimeStampData, False);
  PayloadSize := 1 + 8 + Length(Value);

  SetLength(Buf, PayloadSize);
  Buf[0] := CtrlByte;
  Move(TimestampNs, Buf[1], 8);
  if Length(Value) > 0 then
    Move(Value[0], Buf[9], Length(Value));

  WriteUInt16(Word(ChannelIndex));
  case Channel.LengthFieldSize of
    lfs2: WriteUInt16(Word(PayloadSize));
    lfs4: WriteUInt32(UInt32(PayloadSize));
  end;
  WriteRawBytes(Buf);

  Channel.SampleCount     := Channel.SampleCount + 1;
  Channel.LastTimestampNs := TimestampNs;
end;

procedure TOSFFile.WriteTimestampedBlock(ChannelIndex: Integer;
                                          const Timestamps: array of Int64;
                                          const Values: array of TBytes);
var
  Channel    : TOSFChannelDef;
  N          : Integer;
  IsVariable : Boolean;
  IsMulti    : Boolean;
  CtrlByte   : Byte;
  MS         : TMemoryStream;
  I          : Integer;
  Cnt        : UInt32;
  Len4       : UInt32;
  TS         : Int64;
  Payload    : TBytes;
begin
  if not FHeaderWritten then
    raise EOSFException.Create('WriteHeader must be called before writing data blocks');

  N := Length(Timestamps);
  if N = 0 then Exit;
  if Length(Values) <> N then
    raise EOSFException.Create(
      'WriteTimestampedBlock: Timestamps and Values lengths must match');

  Channel := ChannelByIndex(ChannelIndex);
  if not Assigned(Channel) then
    raise EOSFFormatError.CreateFmt(
      'WriteTimestampedBlock: unknown channel index %d', [ChannelIndex]);

  IsVariable := OSFDataTypeIsVariableLength(Channel.DataType);
  IsMulti    := N > 1;
  CtrlByte   := OSFMakeControlByte(bcAbsTimeStampData, IsMulti);

  MS := TMemoryStream.Create;
  try
    MS.WriteBuffer(CtrlByte, 1);
    if IsMulti then
    begin
      Cnt := N;
      MS.WriteBuffer(Cnt, SizeOf(Cnt));
    end;

    for I := 0 to N - 1 do
    begin
      TS := Timestamps[I];
      MS.WriteBuffer(TS, SizeOf(TS));
      // Variable-length values inside a multi-sample block need a uint32 length
      // prefix per value. Single-sample blocks don't need it because the block
      // length already determines the value size.
      if IsVariable and IsMulti then
      begin
        Len4 := Length(Values[I]);
        MS.WriteBuffer(Len4, SizeOf(Len4));
      end;
      if Length(Values[I]) > 0 then
        MS.WriteBuffer(Values[I][0], Length(Values[I]));
    end;

    SetLength(Payload, MS.Size);
    if MS.Size > 0 then
    begin
      MS.Position := 0;
      MS.ReadBuffer(Payload[0], MS.Size);
    end;
  finally
    MS.Free;
  end;

  WriteUInt16(Word(ChannelIndex));
  case Channel.LengthFieldSize of
    lfs2: WriteUInt16(Word(Length(Payload)));
    lfs4: WriteUInt32(UInt32(Length(Payload)));
  end;
  WriteRawBytes(Payload);

  Channel.SampleCount     := Channel.SampleCount + N;
  Channel.LastTimestampNs := Timestamps[N - 1];
end;

procedure TOSFFile.WriteTimestampedDoubles(ChannelIndex: Integer;
                                            const Timestamps: array of Int64;
                                            const Values: array of Double);
var
  EncodedValues : array of TBytes;
  I             : Integer;
  D             : Double;
begin
  if Length(Values) <> Length(Timestamps) then
    raise EOSFException.Create(
      'WriteTimestampedDoubles: Timestamps and Values lengths must match');
  SetLength(EncodedValues, Length(Values));
  for I := 0 to High(Values) do
  begin
    SetLength(EncodedValues[I], SizeOf(Double));
    D := Values[I];
    Move(D, EncodedValues[I][0], SizeOf(Double));
  end;
  WriteTimestampedBlock(ChannelIndex, Timestamps, EncodedValues);
end;

// ── Common ────────────────────────────────────────────────────────────────────

procedure TOSFFile.Close;
begin
  if FMode = fmClosed then Exit;
  try
    if FOwnsStream then
      FreeAndNil(FStream)
    else
      FStream := nil;
  finally
    FMode := fmClosed;
  end;
end;

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
