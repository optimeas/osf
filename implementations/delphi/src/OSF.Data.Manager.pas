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

// In-memory container for a complete OSF file. Loads everything via TOSFFile
// (the streaming filer) into typed TOSFDataChannel instances and exposes
// file-level metadata as plain properties. Built so the loaded manager can
// outlive the source TOSFFile / stream.
unit OSF.Data.Manager;

interface

uses
  System.SysUtils,
  System.Classes,
  System.Generics.Collections,
  OSF.Types,
  OSF.Channel,
  OSF.Log,
  OSF.Filer,
  OSF.Data.Channels;

type
  TOSFDataManager = class(TPersistent)
  private
    // The manager owns its own copies of TOSFChannelDef. Each TOSFDataChannel
    // holds a non-owning reference into this list (positions match FChannels).
    FOwnedChannelDefs: TObjectList<TOSFChannelDef>;
    FChannels        : TObjectList<TOSFDataChannel>;

    // File metadata.
    FSourceFileName  : string;
    FSourceFileSize  : Int64;
    FVersion         : TOSFVersion;
    FCreatedUtc      : TDateTime;
    FCreator         : string;
    FTag             : string;
    FReason          : string;
    FLatitude        : Double;
    FLongitude       : Double;
    FAltitude        : Double;

    // Logging — copied verbatim from TOSFLoggable. TPersistent is the
    // required base class so the manager cannot inherit from TOSFLoggable.
    FOnLog           : TOSFLogEvent;
    FDebugEnabled    : Boolean;
    procedure Log(Level: TOSFLogLevel; const Msg: string); overload;
    procedure Log(Level: TOSFLogLevel; const Fmt: string;
                   const Args: array of const); overload;

    procedure CopyFrom(Source: TOSFDataManager);
    function  GetChannelCount: Integer;

    // Loading helpers.
    procedure CopyFileMetadata    (AFiler: TOSFFile);
    procedure CreateChannelsFromFiler(AFiler: TOSFFile);
    function  ConsumeBlocks       (AFiler: TOSFFile): Integer;
    procedure DispatchBlock       (const Block: TOSFDataBlock);
    function  FindChannelByDefIndex(DefIndex: Word): TOSFDataChannel;
    procedure LogChannelsSummary;
  public
    constructor Create; overload;
    constructor Create(Source: TOSFDataManager); overload;
    destructor  Destroy; override;

    // Resets channels and metadata to an empty state.
    procedure Clear; virtual;

    // Deep copy. The clone owns independent ChannelDef copies and
    // independent data channels.
    function  Clone: TOSFDataManager;

    // Assign override — accepts another TOSFDataManager and performs a deep
    // copy. Falls back to the inherited behaviour for unrelated source types.
    procedure Assign(Source: TPersistent); override;

    // Channel access.
    function  ChannelByName(const Name: string): TOSFDataChannel;
    function  ChannelByIndex(Index: Integer): TOSFDataChannel;
    function  TryGetChannel(const Name: string; out Channel: TOSFDataChannel): Boolean;

    // Loading. Both methods clear the manager first; existing data is discarded.
    // Best-effort: never raise on truncated data — partial results are valid.
    procedure LoadFromFile(const FileName: string);
    procedure LoadFromStream(AStream: TStream);

    property ChannelCount : Integer                       read GetChannelCount;
    property Channels     : TObjectList<TOSFDataChannel>  read FChannels;

    property SourceFileName : string      read FSourceFileName;
    property SourceFileSize : Int64       read FSourceFileSize;
    property Version        : TOSFVersion read FVersion;
    property CreatedUtc     : TDateTime   read FCreatedUtc;
    property Creator        : string      read FCreator;
    property Tag            : string      read FTag;
    property Reason         : string      read FReason;
    property Latitude       : Double      read FLatitude;
    property Longitude      : Double      read FLongitude;
    property Altitude       : Double      read FAltitude;

    // Logging hook — emits informational, warning and (optionally) debug
    // messages during LoadFromFile/LoadFromStream. The manager forwards its
    // settings to the internal TOSFFile so log messages from both layers go
    // through the same handler.
    property DebugEnabled : Boolean      read FDebugEnabled write FDebugEnabled;
    property OnLog        : TOSFLogEvent read FOnLog        write FOnLog;
  end;

implementation

// ── Local helpers ────────────────────────────────────────────────────────────

// OSF.Channel.pas does not expose a Clone on TOSFChannelDef and we cannot
// modify that unit, so we replicate every persisted field here. I/O state
// (LastTimestampNs, SampleCount, StartBlockWritten) is intentionally not
// copied — that's filer state, not channel definition.
function CloneChannelDef(Src: TOSFChannelDef): TOSFChannelDef;
begin
  Result := TOSFChannelDef.Create(Src.Index, Src.Name, Src.ChannelType, Src.DataType);
  try
    Result.DisplayName        := Src.DisplayName;
    Result.Reference          := Src.Reference;
    Result.Comment            := Src.Comment;
    Result.DataIdentifier     := Src.DataIdentifier;
    Result.TimeIncrement      := Src.TimeIncrement;
    Result.StartTimestampNs   := Src.StartTimestampNs;
    Result.LengthFieldSize    := Src.LengthFieldSize;
    Result.PhysicalUnit       := Src.PhysicalUnit;
    Result.PhysicalUnit1      := Src.PhysicalUnit1;
    Result.PhysicalUnit2      := Src.PhysicalUnit2;
    Result.PhysicalUnit3      := Src.PhysicalUnit3;
    Result.PhysicalDimension  := Src.PhysicalDimension;
    Result.PhysicalDimension1 := Src.PhysicalDimension1;
    Result.PhysicalDimension2 := Src.PhysicalDimension2;
    Result.PhysicalDimension3 := Src.PhysicalDimension3;
    Result.Scale              := Src.Scale;
    Result.Offset             := Src.Offset;
    Result.MimeType           := Src.MimeType;
    Result.SpectrumType       := Src.SpectrumType;
  except
    Result.Free;
    raise;
  end;
end;

// Decodes a bcAbsTimeStampData block and feeds samples to Channel.
// Layout depends on Block.SampleCount and the channel's data type:
//   Single-sample  : [int64 ts][value bytes]
//   Multi-sample   : [int64 ts][value bytes] × N
//   Multi-sample variable-length: [int64 ts][uint32 len][value bytes] × N
procedure DecodeAbsTimestampedBlock(Channel: TOSFDataChannel; const Block: TOSFDataBlock);
var
  Pos, ValSize : Integer;
  Ts           : Int64;
  Len          : UInt32;
  ValueBytes   : TBytes;
  IsVariable   : Boolean;
  IsMulti      : Boolean;
  FixedSize    : Integer;
  I            : Integer;
  ChannelDef   : TOSFChannelDef;
  PayloadLen   : Integer;
begin
  ChannelDef := Channel.ChannelDef;
  if not Assigned(ChannelDef) then Exit;

  IsVariable := OSFDataTypeIsVariableLength(ChannelDef.DataType);
  FixedSize  := OSFDataTypeFixedSize(ChannelDef.DataType);
  IsMulti    := Block.SampleCount > 1;
  PayloadLen := Length(Block.RawPayload);

  Pos := 0;
  for I := 0 to Integer(Block.SampleCount) - 1 do
  begin
    if Pos + 8 > PayloadLen then Exit;
    Move(Block.RawPayload[Pos], Ts, 8);
    Inc(Pos, 8);

    if IsVariable then
    begin
      if IsMulti then
      begin
        if Pos + 4 > PayloadLen then Exit;
        Move(Block.RawPayload[Pos], Len, 4);
        Inc(Pos, 4);
        ValSize := Integer(Len);
      end
      else
        ValSize := PayloadLen - Pos;  // single sample: rest of payload
    end
    else
      ValSize := FixedSize;

    if (ValSize < 0) or (Pos + ValSize > PayloadLen) then Exit;
    SetLength(ValueBytes, ValSize);
    if ValSize > 0 then
      Move(Block.RawPayload[Pos], ValueBytes[0], ValSize);
    Inc(Pos, ValSize);

    Channel.AddRawSample(Ts, ValueBytes);
  end;
end;

// Decodes an equidistant data block (bcStartData or bcContinuedData).
// Layout: [value bytes] × SampleCount, all of fixed size.
// StartTs is the timestamp of the first sample in this block.
procedure DecodeEquidistantBlock(Channel: TOSFDataChannel;
                                  const Block: TOSFDataBlock; StartTs: Int64);
var
  Pos, ValSize, PayloadLen : Integer;
  Ts, Increment            : Int64;
  ValueBytes               : TBytes;
  I                        : Integer;
  ChannelDef               : TOSFChannelDef;
begin
  ChannelDef := Channel.ChannelDef;
  if not Assigned(ChannelDef) then Exit;

  ValSize := OSFDataTypeFixedSize(ChannelDef.DataType);
  if ValSize <= 0 then Exit;  // variable-length equidistant not supported here

  Increment  := ChannelDef.TimeIncrement;
  PayloadLen := Length(Block.RawPayload);

  Pos := 0;
  for I := 0 to Integer(Block.SampleCount) - 1 do
  begin
    if Pos + ValSize > PayloadLen then Exit;
    SetLength(ValueBytes, ValSize);
    Move(Block.RawPayload[Pos], ValueBytes[0], ValSize);
    Inc(Pos, ValSize);

    Ts := StartTs + Int64(I) * Increment;
    Channel.AddRawSample(Ts, ValueBytes);
  end;
end;

// Decodes a deprecated OSF4 bcContinuedRelStampData block.
// Layout: [uint32 delta_ns][value bytes] × SampleCount. Deltas are added
// onto the channel's running EndTimestampNs.
procedure DecodeRelTimestampedBlock(Channel: TOSFDataChannel; const Block: TOSFDataBlock);
var
  Pos, ValSize, PayloadLen : Integer;
  Delta                    : UInt32;
  Ts                       : Int64;
  ValueBytes               : TBytes;
  I                        : Integer;
  ChannelDef               : TOSFChannelDef;
begin
  ChannelDef := Channel.ChannelDef;
  if not Assigned(ChannelDef) then Exit;

  ValSize := OSFDataTypeFixedSize(ChannelDef.DataType);
  if ValSize <= 0 then Exit;

  Ts         := Channel.EndTimestampNs;
  PayloadLen := Length(Block.RawPayload);

  Pos := 0;
  for I := 0 to Integer(Block.SampleCount) - 1 do
  begin
    if Pos + 4 > PayloadLen then Exit;
    Move(Block.RawPayload[Pos], Delta, 4);
    Inc(Pos, 4);
    Inc(Ts, Int64(Delta));

    if Pos + ValSize > PayloadLen then Exit;
    SetLength(ValueBytes, ValSize);
    Move(Block.RawPayload[Pos], ValueBytes[0], ValSize);
    Inc(Pos, ValSize);

    Channel.AddRawSample(Ts, ValueBytes);
  end;
end;

// ── TOSFDataManager — logging helpers (verbatim copy of TOSFLoggable.Log) ───

procedure TOSFDataManager.Log(Level: TOSFLogLevel; const Msg: string);
begin
  if not Assigned(FOnLog) then Exit;
  if (Level = llDebug) and (not FDebugEnabled) then Exit;
  try
    FOnLog(Level, Msg);
  except
    // Never let a buggy log handler propagate.
  end;
end;

procedure TOSFDataManager.Log(Level: TOSFLogLevel; const Fmt: string;
                               const Args: array of const);
begin
  if not Assigned(FOnLog) then Exit;
  if (Level = llDebug) and (not FDebugEnabled) then Exit;
  try
    FOnLog(Level, Format(Fmt, Args));
  except
    // Never let a buggy log handler or a broken Format string propagate.
  end;
end;

// ── TOSFDataManager — construction / lifecycle ──────────────────────────────

constructor TOSFDataManager.Create;
begin
  inherited Create;
  // Same ownership pattern as uDataManager.pas: OwnsObjects = True so the
  // lists free their elements when the manager itself goes away.
  FOwnedChannelDefs := TObjectList<TOSFChannelDef>.Create(True);
  FChannels         := TObjectList<TOSFDataChannel>.Create(True);
end;

constructor TOSFDataManager.Create(Source: TOSFDataManager);
begin
  Create;
  if Assigned(Source) then
    CopyFrom(Source);
end;

destructor TOSFDataManager.Destroy;
begin
  // FChannels holds references into FOwnedChannelDefs, so free the channels
  // first while their defs are still alive.
  FChannels.Free;
  FOwnedChannelDefs.Free;
  inherited;
end;

procedure TOSFDataManager.Clear;
begin
  FChannels.Clear;
  FOwnedChannelDefs.Clear;
  FSourceFileName := '';
  FSourceFileSize := 0;
  FVersion        := osvUnknown;
  FCreatedUtc     := 0;
  FCreator        := '';
  FTag            := '';
  FReason         := '';
  FLatitude       := 0;
  FLongitude      := 0;
  FAltitude       := 0;
  Log(llDebug, 'DataManager cleared');
end;

function TOSFDataManager.GetChannelCount: Integer;
begin
  Result := FChannels.Count;
end;

// ── Clone / Assign / CopyFrom ────────────────────────────────────────────────

procedure TOSFDataManager.CopyFrom(Source: TOSFDataManager);
var
  I          : Integer;
  ClonedDef  : TOSFChannelDef;
  ClonedChan : TOSFDataChannel;
begin
  if Source = nil then Exit;
  Clear;

  // Metadata.
  FSourceFileName := Source.FSourceFileName;
  FSourceFileSize := Source.FSourceFileSize;
  FVersion        := Source.FVersion;
  FCreatedUtc     := Source.FCreatedUtc;
  FCreator        := Source.FCreator;
  FTag            := Source.FTag;
  FReason         := Source.FReason;
  FLatitude       := Source.FLatitude;
  FLongitude      := Source.FLongitude;
  FAltitude       := Source.FAltitude;

  // Clone defs first so channels can rebind to our own copies.
  FOwnedChannelDefs.Capacity := Source.FOwnedChannelDefs.Count;
  for I := 0 to Source.FOwnedChannelDefs.Count - 1 do
    FOwnedChannelDefs.Add(CloneChannelDef(Source.FOwnedChannelDefs[I]));

  // Clone channels with their values; rebind ChannelDef to our def copy.
  // FChannels[i] in source corresponds to FOwnedChannelDefs[i] in source —
  // we preserve that 1:1 ordering in the clone.
  FChannels.Capacity := Source.FChannels.Count;
  for I := 0 to Source.FChannels.Count - 1 do
  begin
    ClonedChan := Source.FChannels[I].Clone;
    ClonedDef  := FOwnedChannelDefs[I];
    ClonedChan.ChannelDef := ClonedDef;
    FChannels.Add(ClonedChan);
  end;
end;

function TOSFDataManager.Clone: TOSFDataManager;
begin
  Result := TOSFDataManager.Create(Self);
end;

procedure TOSFDataManager.Assign(Source: TPersistent);
begin
  if Source is TOSFDataManager then
    CopyFrom(TOSFDataManager(Source))
  else
    inherited Assign(Source);
end;

// ── Channel access ───────────────────────────────────────────────────────────

function TOSFDataManager.ChannelByName(const Name: string): TOSFDataChannel;
var
  I: Integer;
begin
  for I := 0 to FChannels.Count - 1 do
    if FChannels[I].Name = Name then
      Exit(FChannels[I]);
  Result := nil;
  Log(llWarning, 'Channel not found by name: "%s"', [Name]);
end;

function TOSFDataManager.ChannelByIndex(Index: Integer): TOSFDataChannel;
var
  I: Integer;
begin
  // Index here is the channel's logical index from its TOSFChannelDef, not
  // the position inside FChannels — same semantics as TOSFFile.ChannelByIndex.
  for I := 0 to FChannels.Count - 1 do
    if Assigned(FChannels[I].ChannelDef) and (FChannels[I].ChannelDef.Index = Index) then
      Exit(FChannels[I]);
  Result := nil;
end;

function TOSFDataManager.TryGetChannel(const Name: string; out Channel: TOSFDataChannel): Boolean;
begin
  Channel := ChannelByName(Name);
  Result  := Assigned(Channel);
end;

function TOSFDataManager.FindChannelByDefIndex(DefIndex: Word): TOSFDataChannel;
var
  I: Integer;
begin
  for I := 0 to FChannels.Count - 1 do
    if Assigned(FChannels[I].ChannelDef) and (FChannels[I].ChannelDef.Index = Integer(DefIndex)) then
      Exit(FChannels[I]);
  Result := nil;
end;

// ── Loading ──────────────────────────────────────────────────────────────────

procedure TOSFDataManager.LoadFromFile(const FileName: string);
var
  FS: TFileStream;
begin
  Log(llInfo, 'Loading OSF file: %s', [FileName]);
  FS := TFileStream.Create(FileName, fmOpenRead or fmShareDenyWrite);
  try
    LoadFromStream(FS);
    // Set source info AFTER LoadFromStream — that call clears the manager.
    FSourceFileName := FileName;
    FSourceFileSize := FS.Size;
  finally
    FS.Free;
  end;
end;

procedure TOSFDataManager.LoadFromStream(AStream: TStream);
var
  Filer          : TOSFFile;
  TruncationSeen : Boolean;
  BlockCount     : Integer;
  UserOnLog      : TOSFLogEvent;
begin
  Clear;
  TruncationSeen := False;
  UserOnLog      := FOnLog;

  Filer := TOSFFile.Create;
  try
    Filer.DebugEnabled := FDebugEnabled;
    // Forward filer log events to the user's handler. The wrapper also watches
    // for the filer's truncation warning so we can emit a summary at the end.
    Filer.OnLog := procedure(Level: TOSFLogLevel; const Msg: string)
                   begin
                     if (Level = llWarning) and
                        (System.Pos('Truncated', Msg) > 0) then
                       TruncationSeen := True;
                     if Assigned(UserOnLog) then
                       UserOnLog(Level, Msg);
                   end;

    Filer.OpenForRead(AStream);
    CopyFileMetadata(Filer);
    CreateChannelsFromFiler(Filer);
    BlockCount := ConsumeBlocks(Filer);

    Log(llInfo, 'Loaded %d channels', [FChannels.Count]);
    LogChannelsSummary;
    if TruncationSeen then
      Log(llWarning, 'Truncated or partial file — %d complete blocks read',
          [BlockCount]);
  finally
    Filer.Free;
  end;
end;

procedure TOSFDataManager.LogChannelsSummary;
const
  TS_FMT = 'yyyy-mm-dd"T"hh:nn:ss"."zzz"Z"';
var
  I  : Integer;
  Ch : TOSFDataChannel;
begin
  // Skip the per-channel walk entirely if no log handler is attached so we
  // don't pay the FormatDateTime cost when nobody is listening.
  if not Assigned(FOnLog) then Exit;

  for I := 0 to FChannels.Count - 1 do
  begin
    Ch := FChannels[I];
    Log(llInfo, '  [%s]  %d samples  %s .. %s',
        [Ch.Name,
         Ch.SampleCount,
         FormatDateTime(TS_FMT, Ch.StartTimeUtc),
         FormatDateTime(TS_FMT, Ch.EndTimeUtc)]);
    if Ch.HasDoublePrecisionLoss then
      Log(llWarning,
          'Channel [%s] uses Int64/UInt64 — ValueAsDouble may lose precision for values > 2^53',
          [Ch.Name]);
  end;
end;

procedure TOSFDataManager.CopyFileMetadata(AFiler: TOSFFile);
begin
  FVersion    := AFiler.Version;
  FCreatedUtc := AFiler.Metadata.CreatedUtc;
  FCreator    := AFiler.Metadata.Creator;
  FTag        := AFiler.Metadata.Tag;
  FReason     := AFiler.Metadata.Reason;
  FLatitude   := AFiler.Metadata.Latitude;
  FLongitude  := AFiler.Metadata.Longitude;
  FAltitude   := AFiler.Metadata.Altitude;
end;

procedure TOSFDataManager.CreateChannelsFromFiler(AFiler: TOSFFile);
var
  I          : Integer;
  OwnedDef   : TOSFChannelDef;
  DataChan   : TOSFDataChannel;
begin
  for I := 0 to AFiler.Channels.Count - 1 do
  begin
    // Take an independent copy so the manager outlives the filer.
    OwnedDef := CloneChannelDef(AFiler.Channels[I]);
    FOwnedChannelDefs.Add(OwnedDef);
    DataChan := CreateOSFDataChannel(OwnedDef);
    FChannels.Add(DataChan);
  end;
end;

function TOSFDataManager.ConsumeBlocks(AFiler: TOSFFile): Integer;
var
  Block: TOSFDataBlock;
begin
  // Best-effort: ReadNextBlock returns False on clean EOF or truncation;
  // we never propagate exceptions from partial files.
  Result := 0;
  while AFiler.ReadNextBlock(Block) do
  begin
    DispatchBlock(Block);
    Inc(Result);
  end;
end;

procedure TOSFDataManager.DispatchBlock(const Block: TOSFDataBlock);
var
  Channel : TOSFDataChannel;
  StartTs : Int64;
begin
  // Info / trailer blocks carry free-form XML/JSON, not sample data.
  if Block.IsInfoBlock then Exit;

  Channel := FindChannelByDefIndex(Block.ChannelIndex);
  if not Assigned(Channel) then Exit;  // unknown channel — silently skip

  case Block.BlockType of
    bcAbsTimeStampData:
      DecodeAbsTimestampedBlock(Channel, Block);

    bcStartData:
      DecodeEquidistantBlock(Channel, Block, Block.StartTimestampNs);

    bcContinuedData:
      begin
        // Continue from the channel's last known timestamp + one increment.
        if Assigned(Channel.ChannelDef) then
          StartTs := Channel.EndTimestampNs + Channel.ChannelDef.TimeIncrement
        else
          StartTs := Channel.EndTimestampNs;
        DecodeEquidistantBlock(Channel, Block, StartTs);
      end;

    bcContinuedRelStampData:
      DecodeRelTimestampedBlock(Channel, Block);
  else
    // Reserved / deprecated block types are skipped — the filer already
    // consumed their bytes via the length field.
  end;
end;

end.
