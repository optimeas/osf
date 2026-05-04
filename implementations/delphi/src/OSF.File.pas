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

unit OSF.File;

interface

uses
  System.SysUtils,
  System.Classes,
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
    CreatedUtc  : TDateTime;   // UTC; writer defaults to Now if zero
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
  //   bcStartData       — N contiguous encoded data values (timestamp in StartTimestampNs)
  //   bcContinuedData   — N contiguous encoded data values
  //   bcAbsTimeStampData — N × [int64 timestamp, encoded value] interleaved
  //   bcContinuedRelStampData — N × [uint32 delta_ns, encoded value] interleaved (OSF4 only)
  //   info block ($FFFF) — raw UTF-8 XML or JSON (IsInfoBlock = True)
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
    FHeaderWritten : Boolean;

    // Parses the magic header line, reads the meta block, and populates
    // FVersion, FMetaFormat, FMetadata, and FChannels.
    procedure ReadMagicAndMeta;
    procedure ParseXMLMeta(const Data: TBytes);
    procedure ParseJSONMeta(const Data: TBytes);

    // Builds the complete JSON meta block as UTF-8 bytes.
    function  BuildJSONMeta: TBytes;

    // Returns the channel with the given index, or nil if not found.
    function  FindChannel(Index: Integer): TOSFChannelDef;

    // Low-level stream primitives. ReadXxx raise EOSFFormatError on short reads.
    function  ReadUInt16: Word;
    function  ReadUInt32: UInt32;
    function  ReadInt64:  Int64;
    procedure WriteUInt16(Value: Word);
    procedure WriteUInt32(Value: UInt32);
    procedure WriteInt64(Value: Int64);
    procedure WriteRawBytes(const Data: TBytes);

    function  GetChannelCount: Integer;
  public
    constructor Create;
    destructor  Destroy; override;

    // ── Reading ───────────────────────────────────────────────────────────
    procedure OpenForRead(const FileName: string); overload;
    procedure OpenForRead(AStream: TStream; AOwnsStream: Boolean = False); overload;

    // Reads the next data block from the stream.
    // Returns False at clean EOF or when the stream is truncated mid-block
    // (best-effort: all complete blocks before the truncation point are returned).
    // Never raises on a truncated or partially-written file.
    function  ReadNextBlock(out Block: TOSFDataBlock): Boolean;

    // ── Writing ───────────────────────────────────────────────────────────
    procedure CreateForWrite(const FileName: string); overload;
    procedure CreateForWrite(AStream: TStream; AOwnsStream: Boolean = False); overload;

    // Registers a channel definition. Must be called before WriteHeader.
    // Returns the channel index (= Def.Index; caller is responsible for uniqueness).
    function  AddChannel(Def: TOSFChannelDef): Integer;

    // Commits the OSF5 magic header and JSON meta block to the stream.
    // Must be called after all AddChannel calls and before any WriteXxx calls.
    // Defaults CreatedUtc to UtcNow and NamespaceSep to '.' if not set.
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
    procedure WriteEquidistantBlock(ChannelIndex: Integer;
                                     const Samples: array of Double;
                                     FirstTimestampNs: Int64 = 0);

    // Writes a single timestamped sample as a bcAbsTimeStampData block.
    // Value must contain the raw encoded bytes for the channel's DataType.
    procedure WriteTimestampedSample(ChannelIndex: Integer;
                                      TimestampNs: Int64;
                                      const Value: TBytes);

    // Flushes and closes the stream. For OSF5, no trailer is written.
    procedure Close;

    // ── Properties ────────────────────────────────────────────────────────
    property Version     : TOSFVersion                 read FVersion;
    property MetaFormat  : TOSFMetaFormat              read FMetaFormat;
    property Channels    : TObjectList<TOSFChannelDef> read FChannels;
    property ChannelCount: Integer                     read GetChannelCount;
    property Metadata    : TOSFFileMetadata            read FMetadata write FMetadata;

    // Looks up a channel by name; returns nil if not found.
    function ChannelByName(const Name: string): TOSFChannelDef;
    // Looks up a channel by its Index attribute; returns nil if not found.
    function ChannelByIndex(Index: Integer): TOSFChannelDef;
  end;

implementation

// ── JSON helpers (local to this unit) ────────────────────────────────────────

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
var Val: TJSONValue;
begin
  Val := Obj.GetValue(Key);
  if Assigned(Val) then Result := (Val as TJSONNumber).AsDouble else Result := Default;
end;

// Formats a TDateTime as UTC ISO 8601 with 'Z' suffix.
function FormatUTCDateTime(DT: TDateTime): string;
begin
  Result := FormatDateTime('yyyy-mm-dd"T"hh:nn:ss"Z"', DT);
end;

// ── TOSFFile ──────────────────────────────────────────────────────────────────

constructor TOSFFile.Create;
begin
  inherited Create;
  FChannels    := TObjectList<TOSFChannelDef>.Create(True);
  FMode        := fmClosed;
  FVersion     := osvUnknown;
  FHeaderWritten := False;
end;

destructor TOSFFile.Destroy;
begin
  Close;
  FChannels.Free;
  inherited;
end;

function TOSFFile.GetChannelCount: Integer;
begin
  Result := FChannels.Count;
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

function TOSFFile.ReadInt64: Int64;
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

procedure TOSFFile.WriteInt64(Value: Int64);
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
  ReadMagicAndMeta;
end;

procedure TOSFFile.ReadMagicAndMeta;
var
  Line      : AnsiString;
  Ch        : AnsiChar;
  BytesRead : Integer;
  Parts     : TArray<string>;
  MetaSize  : Int64;
  MetaBytes : TBytes;
  FirstByte : Byte;
begin
  // Read the magic header line terminated by LF ($0A).
  Line := '';
  repeat
    BytesRead := FStream.Read(Ch, 1);
    if BytesRead = 0 then
      raise EOSFFormatError.Create('Unexpected end of stream reading magic header');
    if Ch <> #10 then
      Line := Line + Ch;
  until Ch = #10;

  // Strip trailing CR for files written with CRLF line endings.
  if (Length(Line) > 0) and (Line[Length(Line)] = #13) then
    SetLength(Line, Length(Line) - 1);

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
  ChanArr  : TJSONArray;
  I        : Integer;
  Ch       : TOSFChannelDef;
begin
  JSONText := TEncoding.UTF8.GetString(Data);
  Root := TJSONObject.ParseJSONValue(JSONText) as TJSONObject;
  if not Assigned(Root) then
    raise EOSFFormatError.Create('Failed to parse OSF5 JSON meta block');
  try
    OSFNode := Root.GetValue('osf') as TJSONObject;
    if not Assigned(OSFNode) then
      raise EOSFFormatError.Create('JSON meta block is missing the top-level "osf" key');

    FMetadata.CreatedUtc   := 0;  // parsed separately if needed; stored as string in file
    FMetadata.Creator      := JStr(OSFNode, 'creator',               '');
    FMetadata.Tag          := JStr(OSFNode, 'tag',                   '');
    FMetadata.Reason       := JStr(OSFNode, 'reason',                '');
    FMetadata.Comment      := JStr(OSFNode, 'comment',               '');
    FMetadata.NamespaceSep := JStr(OSFNode, 'namespacesep',          OSF_DEFAULT_NAMESPACE_SEP);
    FMetadata.Longitude    := JDbl(OSFNode, 'created_at_longitude',  0.0);
    FMetadata.Latitude     := JDbl(OSFNode, 'created_at_latitude',   0.0);
    FMetadata.Altitude     := JDbl(OSFNode, 'created_at_altitude',   0.0);

    ChanArr := OSFNode.GetValue('channels') as TJSONArray;
    if Assigned(ChanArr) then
      for I := 0 to ChanArr.Count - 1 do
      begin
        Ch := TOSFChannelDef.FromJSONObject(ChanArr.Items[I] as TJSONObject);
        FChannels.Add(Ch);
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
  I           : Integer;
  Node        : IXMLNode;
  Ch          : TOSFChannelDef;
begin
  XMLText := TEncoding.UTF8.GetString(Data);
  XMLDoc  := TXMLDocument.Create(nil);
  XMLDoc.LoadFromXML(XMLText);
  XMLDoc.Active := True;

  RootNode := XMLDoc.DocumentElement;
  if not Assigned(RootNode) then
    raise EOSFFormatError.Create('XML meta block has no root element');

  // File-level metadata from <osf> element attributes.
  if RootNode.HasAttribute('creator')      then FMetadata.Creator      := RootNode.Attributes['creator'];
  if RootNode.HasAttribute('tag')          then FMetadata.Tag          := RootNode.Attributes['tag'];
  if RootNode.HasAttribute('reason')       then FMetadata.Reason       := RootNode.Attributes['reason'];
  if RootNode.HasAttribute('comment')      then FMetadata.Comment      := RootNode.Attributes['comment'];
  if RootNode.HasAttribute('namespacesep') then FMetadata.NamespaceSep := RootNode.Attributes['namespacesep']
  else                                          FMetadata.NamespaceSep := OSF_DEFAULT_NAMESPACE_SEP;

  // Channel definitions from <channels><channel .../></channels>.
  ChansNode := RootNode.ChildNodes.FindNode('channels');
  if not Assigned(ChansNode) then
    Exit;

  for I := 0 to ChansNode.ChildNodes.Count - 1 do
  begin
    Node := ChansNode.ChildNodes[I];
    if Node.NodeName = 'channel' then
    begin
      Ch := TOSFChannelDef.FromXMLNode(Node);
      FChannels.Add(Ch);
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

procedure TOSFFile.CreateForWrite(const FileName: string);
begin
  CreateForWrite(TFileStream.Create(FileName, fmCreate), True);
end;

procedure TOSFFile.CreateForWrite(AStream: TStream; AOwnsStream: Boolean);
begin
  if FMode <> fmClosed then
    raise EOSFException.Create('TOSFFile: already open');
  FStream        := AStream;
  FOwnsStream    := AOwnsStream;
  FMode          := fmWrite;
  FVersion       := osvOSF5;
  FMetaFormat    := mfJSON;
  FHeaderWritten := False;
  FChannels.Clear;
end;

function TOSFFile.AddChannel(Def: TOSFChannelDef): Integer;
begin
  if FHeaderWritten then
    raise EOSFException.Create('AddChannel must be called before WriteHeader');
  FChannels.Add(Def);
  Result := Def.Index;
end;

function TOSFFile.BuildJSONMeta: TBytes;
var
  Root    : TJSONObject;
  OSFNode : TJSONObject;
  ChanArr : TJSONArray;
  I       : Integer;
begin
  ChanArr := TJSONArray.Create;
  for I := 0 to FChannels.Count - 1 do
    FChannels[I].AppendJSON(ChanArr);

  OSFNode := TJSONObject.Create;
  OSFNode.AddPair('version',    TJSONNumber.Create(5));
  OSFNode.AddPair('created_utc', FormatUTCDateTime(FMetadata.CreatedUtc));
  if FMetadata.Creator      <> '' then OSFNode.AddPair('creator',      FMetadata.Creator);
  if FMetadata.Tag          <> '' then OSFNode.AddPair('tag',          FMetadata.Tag);
  if FMetadata.Reason       <> '' then OSFNode.AddPair('reason',       FMetadata.Reason);
  if FMetadata.Comment      <> '' then OSFNode.AddPair('comment',      FMetadata.Comment);
  if FMetadata.NamespaceSep <> '' then OSFNode.AddPair('namespacesep', FMetadata.NamespaceSep);
  if FMetadata.Longitude    <> 0  then OSFNode.AddPair('created_at_longitude', TJSONNumber.Create(FMetadata.Longitude));
  if FMetadata.Latitude     <> 0  then OSFNode.AddPair('created_at_latitude',  TJSONNumber.Create(FMetadata.Latitude));
  if FMetadata.Altitude     <> 0  then OSFNode.AddPair('created_at_altitude',  TJSONNumber.Create(FMetadata.Altitude));
  OSFNode.AddPair('channels', ChanArr);   // OSFNode takes ownership of ChanArr

  Root := TJSONObject.Create;
  Root.AddPair('osf', OSFNode);           // Root takes ownership of OSFNode
  try
    Result := TEncoding.UTF8.GetBytes(Root.ToJSON);
  finally
    Root.Free;
  end;
end;

procedure TOSFFile.WriteHeader;
var
  MetaBytes  : TBytes;
  HeaderLine : TBytes;
  HeaderStr  : string;
begin
  if FMode <> fmWrite then
    raise EOSFException.Create('WriteHeader requires the file to be open for writing');
  if FHeaderWritten then
    raise EOSFException.Create('WriteHeader has already been called');

  if FMetadata.CreatedUtc = 0 then
    FMetadata.CreatedUtc := Now;
  if FMetadata.NamespaceSep = '' then
    FMetadata.NamespaceSep := OSF_DEFAULT_NAMESPACE_SEP;

  MetaBytes := BuildJSONMeta;

  // Magic header line: "OSF5 <metabytecount>\n"
  HeaderStr  := Format('%s %d'#10, [OSF_MAGIC_OSF5, Length(MetaBytes)]);
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
    Channel.LastTimestampNs := FirstTimestampNs;
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

  // Single sample: bit 7 = 0, no sample-count field.
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
