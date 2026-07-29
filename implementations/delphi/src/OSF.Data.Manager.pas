// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

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
    FChannels: TObjectList<TOSFDataChannel>;

    // File metadata.
    FSourceFileName: string;
    FSourceFileSize: Int64;
    FVersion: TOSFVersion;
    FCreatedUtc: TDateTime;
    FCreator: string;
    FTag: string;
    FReason: string;
    FLatitude: Double;
    FLongitude: Double;
    FAltitude: Double;

    // Pass-through channel filter. Set before LoadFromFile / LoadFromStream
    // to cause the internal TOSFFile to skip blocks for channels not in the
    // list. The manager itself has no filter logic; it never sees blocks for
    // skipped channels and therefore creates no TOSFDataChannel for them.
    FChannelFilter: TArray<string>;

    procedure CopyFrom(Source: TOSFDataManager);
    function GetChannelCount: Integer;

    // Loading helpers.
    procedure CopyFileMetadata(AFiler: TOSFFile);
    procedure CreateChannelsFromFiler(AFiler: TOSFFile);
    function ConsumeBlocks(AFiler: TOSFFile): Integer;
    procedure DispatchBlock(const Block: TOSFDataBlock);
    function FindChannelByDefIndex(DefIndex: Word): TOSFDataChannel;
    procedure LogChannelsSummary;
  public
    constructor Create; overload;
    constructor Create(Source: TOSFDataManager); overload;
    destructor Destroy; override;

    // Resets channels and metadata to an empty state.
    procedure Clear; virtual;

    // Deep copy. The clone owns independent ChannelDef copies and
    // independent data channels.
    function Clone: TOSFDataManager;

    // Assign override - accepts another TOSFDataManager and performs a deep
    // copy. Falls back to the inherited behaviour for unrelated source types.
    procedure Assign(Source: TPersistent); override;

    // Channel access.
    function ChannelByName(const Name: string): TOSFDataChannel;
    function ChannelByIndex(Index: Integer): TOSFDataChannel;
    function TryGetChannel(const Name: string; out Channel: TOSFDataChannel): Boolean;

    // Loading. Both methods clear the manager first; existing data is discarded.
    // Best-effort: never raise on truncated data - partial results are valid.
    procedure LoadFromFile(const FileName: string);
    procedure LoadFromStream(AStream: TStream);

    property ChannelCount: Integer read GetChannelCount;
    property Channels: TObjectList<TOSFDataChannel> read FChannels;

    property SourceFileName: string read FSourceFileName;
    property SourceFileSize: Int64 read FSourceFileSize;
    property Version: TOSFVersion read FVersion;
    property CreatedUtc: TDateTime read FCreatedUtc;
    property Creator: string read FCreator;
    property Tag: string read FTag;
    property Reason: string read FReason;
    property Latitude: Double read FLatitude;
    property Longitude: Double read FLongitude;
    property Altitude: Double read FAltitude;

    // Pass-through channel filter. When non-empty, the next LoadFromFile or
    // LoadFromStream call forwards it to the internal TOSFFile, which skips
    // blocks for channels whose name is not in the list. Names are matched
    // case-insensitively; the metablock is always parsed in full, so
    // unrelated channels still appear in TOSFFile.Channels but are absent
    // from TOSFDataManager.Channels because no blocks for them were loaded.
    property ChannelFilter: TArray<string> read FChannelFilter write FChannelFilter;
  end;

resourcestring
  // Log messages emitted by the data manager via Logger.
  SOSFLogLoadingFile = 'Loading OSF file: %s';
  SOSFLogLoadedChannels = 'Loaded %d channels';
  SOSFLogChannelSummary = '  [%s]  %d samples  %s .. %s';
  SOSFLogPrecisionLossInt64 = 'Channel [%s] uses Int64/UInt64 - ValueAsDouble may lose precision for values > 2^53';
  SOSFLogTruncatedFilePartial = 'Truncated or partial file - %d complete blocks read';
  SOSFLogChannelNotFoundName = 'Channel not found by name: "%s"';
  SOSFLogDataManagerCleared = 'DataManager cleared';

implementation

// ── Local helpers ────────────────────────────────────────────────────────────

// OSF.Channel.pas does not expose a Clone on TOSFChannelDef and we cannot
// modify that unit, so we replicate every persisted field here. I/O state
// (LastTimestampNs, SampleCount, StartBlockWritten) is intentionally not
// copied - that's filer state, not channel definition.
function CloneChannelDef(Src: TOSFChannelDef): TOSFChannelDef;
begin
  Result := TOSFChannelDef.Create(Src.Index, Src.Name, Src.ChannelType, Src.DataType);
  try
    Result.DisplayName := Src.DisplayName;
    Result.Reference := Src.Reference;
    Result.Comment := Src.Comment;
    Result.DataIdentifier := Src.DataIdentifier;
    Result.TimeIncrement := Src.TimeIncrement;
    Result.StartTimestampNs := Src.StartTimestampNs;
    Result.LengthFieldSize := Src.LengthFieldSize;
    Result.SampleRate := Src.SampleRate;
    Result.PhysicalUnit := Src.PhysicalUnit;
    Result.PhysicalDimension := Src.PhysicalDimension;
    Result.MimeType := Src.MimeType;
    Result.SpectrumType := Src.SpectrumType;
  except
    Result.Free;
    raise;
  end;
end;

// Decodes a bcAbsTimeStampData block and feeds samples to Channel.
// Layout depends on Block.SampleCount, the channel's data type, and the
// on-disk OSF version:
// Single-sample fixed         : [int64 ts][value bytes]
// Multi-sample fixed          : [int64 ts][value bytes] × N
// Single-sample variable      : [int64 ts][value bytes] (+ trailing 0x00 in OSF4)
//
// Multi-sample variable is intentionally not handled here — the caller
// (TOSFDataManager.DispatchBlock) filters such blocks out with a warning
// because there is no standard wire format for that combination. The
// historical Delphi per-sample uint32 length-prefix layout was removed in
// 2026-05-24 (no real-world files known to use it).
//
// Per spec rev 2026-05-24 the trailing 0x00 on variable-length payloads is
// version-deterministic: OSF4 writers MUST append it (so OSF4 readers strip
// the last byte unconditionally); OSF5 writers MUST NOT append it (so OSF5
// readers consume the payload verbatim and a trailing 0x00 is a regular
// data byte).
procedure DecodeAbsTimestampedBlock(AVersion: TOSFVersion; Channel: TOSFDataChannel; const Block: TOSFDataBlock);
var
  Pos, ValSize: Integer;
  PayloadSize: Integer;
  Ts: Int64;
  ValueBytes: TBytes;
  IsVariable: Boolean;
  StripTerminator: Boolean;
  FixedSize: Integer;
  I: Integer;
  ChannelDef: TOSFChannelDef;
  PayloadLen: Integer;
begin
  ChannelDef := Channel.ChannelDef;
  if not Assigned(ChannelDef) then
    Exit;

  IsVariable := OSFDataTypeIsVariableLength(ChannelDef.DataType);
  FixedSize := OSFDataTypeFixedSize(ChannelDef.DataType);
  StripTerminator := IsVariable and (AVersion = osvOSF4);
  PayloadLen := Length(Block.RawPayload);

  Assert(not (IsVariable and (Block.SampleCount > 1)),
    'DecodeAbsTimestampedBlock: multi-sample variable-length blocks must be ' +
    'filtered by the caller (TOSFDataManager.DispatchBlock)');

  Pos := 0;
  for I := 0 to Integer(Block.SampleCount) - 1 do
  begin
    if Pos + 8 > PayloadLen then
      Exit;
    Move(Block.RawPayload[Pos], Ts, 8);
    Inc(Pos, 8);

    if IsVariable then
      ValSize := PayloadLen - Pos  // single sample: rest of payload
    else
      ValSize := FixedSize;

    if (ValSize < 0) or (Pos + ValSize > PayloadLen) then
      Exit;

    if StripTerminator then
    begin
      // OSF4: drop the spec-mandated trailing 0x00. ValSize counts the
      // byte; the channel gets the bare payload.
      if ValSize < 1 then
        Exit;
      PayloadSize := ValSize - 1;
    end
    else
      // OSF5 variable, or any fixed-width type: payload is ValSize bytes
      // verbatim.
      PayloadSize := ValSize;

    SetLength(ValueBytes, PayloadSize);
    if PayloadSize > 0 then
      Move(Block.RawPayload[Pos], ValueBytes[0], PayloadSize);
    Inc(Pos, ValSize);

    Channel.AddRawSample(Ts, ValueBytes);
  end;
end;

// Decodes an equidistant data block (bcStartData or bcContinuedData).
// Layout: [value bytes] × SampleCount, all of fixed size.
// StartTs is the timestamp of the first sample in this block.
// The per-sample increment is derived from Channel.SampleRate (1e9 / SampleRate);
// TimeIncrement from the metablock is only a fallback.
procedure DecodeEquidistantBlock(Channel: TOSFDataChannel; const Block: TOSFDataBlock; StartTs: Int64);
var
  Pos, ValSize, PayloadLen: Integer;
  Ts, Increment: Int64;
  ValueBytes: TBytes;
  I: Integer;
  ChannelDef: TOSFChannelDef;
begin
  ChannelDef := Channel.ChannelDef;
  if not Assigned(ChannelDef) then
    Exit;

  ValSize := OSFDataTypeFixedSize(ChannelDef.DataType);
  if ValSize <= 0 then
    Exit; // variable-length equidistant not supported here

  if ChannelDef.SampleRate > 0 then
    Increment := Round(1.0E9 / ChannelDef.SampleRate)
  else
    Increment := ChannelDef.TimeIncrement;
  PayloadLen := Length(Block.RawPayload);

  Pos := 0;
  for I := 0 to Integer(Block.SampleCount) - 1 do
  begin
    if Pos + ValSize > PayloadLen then
      Exit;
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
  Pos, ValSize, PayloadLen: Integer;
  Delta: UInt32;
  Ts: Int64;
  ValueBytes: TBytes;
  I: Integer;
  ChannelDef: TOSFChannelDef;
begin
  ChannelDef := Channel.ChannelDef;
  if not Assigned(ChannelDef) then
    Exit;

  ValSize := OSFDataTypeFixedSize(ChannelDef.DataType);
  if ValSize <= 0 then
    Exit;

  Ts := Channel.EndTimestampNs;
  PayloadLen := Length(Block.RawPayload);

  Pos := 0;
  for I := 0 to Integer(Block.SampleCount) - 1 do
  begin
    if Pos + 4 > PayloadLen then
      Exit;
    Move(Block.RawPayload[Pos], Delta, 4);
    Inc(Pos, 4);
    Inc(Ts, Int64(Delta));

    if Pos + ValSize > PayloadLen then
      Exit;
    SetLength(ValueBytes, ValSize);
    Move(Block.RawPayload[Pos], ValueBytes[0], ValSize);
    Inc(Pos, ValSize);

    Channel.AddRawSample(Ts, ValueBytes);
  end;
end;

// (Logging now goes through the global Logger — see OSF.Log unit.)

// ── TOSFDataManager - construction / lifecycle ──────────────────────────────

constructor TOSFDataManager.Create;
begin
  inherited Create;
  // Same ownership pattern as uDataManager.pas: OwnsObjects = True so the
  // lists free their elements when the manager itself goes away.
  FOwnedChannelDefs := TObjectList<TOSFChannelDef>.Create(True);
  FChannels := TObjectList<TOSFDataChannel>.Create(True);
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
  FVersion := osvUnknown;
  FCreatedUtc := 0;
  FCreator := '';
  FTag := '';
  FReason := '';
  FLatitude := 0;
  FLongitude := 0;
  FAltitude := 0;
  Logger.Write(SOSFLogDataManagerCleared, llDebug, 'TOSFDataManager');
end;

function TOSFDataManager.GetChannelCount: Integer;
begin
  Result := FChannels.Count;
end;

// ── Clone / Assign / CopyFrom ────────────────────────────────────────────────

procedure TOSFDataManager.CopyFrom(Source: TOSFDataManager);
var
  I: Integer;
  ClonedDef: TOSFChannelDef;
  ClonedChan: TOSFDataChannel;
begin
  if Source = nil then
    Exit;
  Clear;

  // Metadata.
  FSourceFileName := Source.FSourceFileName;
  FSourceFileSize := Source.FSourceFileSize;
  FVersion := Source.FVersion;
  FCreatedUtc := Source.FCreatedUtc;
  FCreator := Source.FCreator;
  FTag := Source.FTag;
  FReason := Source.FReason;
  FLatitude := Source.FLatitude;
  FLongitude := Source.FLongitude;
  FAltitude := Source.FAltitude;

  // Clone defs first so channels can rebind to our own copies.
  FOwnedChannelDefs.Capacity := Source.FOwnedChannelDefs.Count;
  for I := 0 to Source.FOwnedChannelDefs.Count - 1 do
    FOwnedChannelDefs.Add(CloneChannelDef(Source.FOwnedChannelDefs[I]));

  // Clone channels with their values; rebind ChannelDef to our def copy.
  // FChannels[i] in source corresponds to FOwnedChannelDefs[i] in source -
  // we preserve that 1:1 ordering in the clone.
  FChannels.Capacity := Source.FChannels.Count;
  for I := 0 to Source.FChannels.Count - 1 do
  begin
    ClonedChan := Source.FChannels[I].Clone;
    ClonedDef := FOwnedChannelDefs[I];
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
  Logger.Write(SOSFLogChannelNotFoundName, [Name], llWarning, 'TOSFDataManager');
end;

function TOSFDataManager.ChannelByIndex(Index: Integer): TOSFDataChannel;
var
  I: Integer;
begin
  // Index here is the channel's logical index from its TOSFChannelDef, not
  // the position inside FChannels - same semantics as TOSFFile.ChannelByIndex.
  for I := 0 to FChannels.Count - 1 do
    if Assigned(FChannels[I].ChannelDef) and (FChannels[I].ChannelDef.Index = Index) then
      Exit(FChannels[I]);
  Result := nil;
end;

function TOSFDataManager.TryGetChannel(const Name: string; out Channel: TOSFDataChannel): Boolean;
begin
  Channel := ChannelByName(Name);
  Result := Assigned(Channel);
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
  Logger.Write(SOSFLogLoadingFile, [FileName], llInfo, 'TOSFDataManager');
  FS := TFileStream.Create(FileName, fmOpenRead or fmShareDenyWrite);
  try
    LoadFromStream(FS);
    // Set source info AFTER LoadFromStream - that call clears the manager.
    FSourceFileName := FileName;
    FSourceFileSize := FS.Size;
  finally
    FS.Free;
  end;
end;

procedure TOSFDataManager.LoadFromStream(AStream: TStream);
var
  Filer: TOSFFile;
  BlockCount: Integer;
begin
  Clear;

  Filer := TOSFFile.Create;
  try
    // Forward our channel filter to the filer so excluded blocks are skipped
    // at the stream level before they ever reach the manager. Filer log
    // messages reach any registered listener directly via the global Logger
    // — no forwarding needed. Truncation is read off Filer.TruncationSeen
    // after the scan completes.
    Filer.ChannelFilter := FChannelFilter;
    Filer.OpenForRead(AStream);
    CopyFileMetadata(Filer);
    CreateChannelsFromFiler(Filer);
    BlockCount := ConsumeBlocks(Filer);

    Logger.Write(SOSFLogLoadedChannels, [FChannels.Count], llInfo, 'TOSFDataManager');
    LogChannelsSummary;
    if Filer.TruncationSeen then
      Logger.Write(SOSFLogTruncatedFilePartial, [BlockCount], llWarning, 'TOSFDataManager');
  finally
    Filer.Free;
  end;
end;

procedure TOSFDataManager.LogChannelsSummary;
const
  TS_FMT = 'yyyy-mm-dd"T"hh:nn:ss"."zzz"Z"';
var
  I: Integer;
  Ch: TOSFDataChannel;
begin
  // Skip the per-channel walk entirely if no listener wants llDebug, so we
  // don't pay the FormatDateTime cost when nobody is listening.
  if not Logger.IsLevelActive(llDebug) then
    Exit;

  for I := 0 to FChannels.Count - 1 do
  begin
    Ch := FChannels[I];
    Logger.Write(SOSFLogChannelSummary, [Ch.Name, Ch.SampleCount, FormatDateTime(TS_FMT, Ch.StartTimeUtc), FormatDateTime(TS_FMT, Ch.EndTimeUtc)], llDebug, 'TOSFDataManager');
    if Ch.HasDoublePrecisionLoss then
      Logger.Write(SOSFLogPrecisionLossInt64, [Ch.Name], llDebug, 'TOSFDataManager');
  end;
end;

procedure TOSFDataManager.CopyFileMetadata(AFiler: TOSFFile);
begin
  FVersion := AFiler.Version;
  FCreatedUtc := AFiler.Metadata.CreatedUtc;
  FCreator := AFiler.Metadata.Creator;
  FTag := AFiler.Metadata.Tag;
  FReason := AFiler.Metadata.Reason;
  FLatitude := AFiler.Metadata.Latitude;
  FLongitude := AFiler.Metadata.Longitude;
  FAltitude := AFiler.Metadata.Altitude;
end;

procedure TOSFDataManager.CreateChannelsFromFiler(AFiler: TOSFFile);
var
  I: Integer;
  SrcDef: TOSFChannelDef;
  OwnedDef: TOSFChannelDef;
  DataChan: TOSFDataChannel;
begin
  for I := 0 to AFiler.Channels.Count - 1 do
  begin
    SrcDef := AFiler.Channels[I];
    // Skip excluded channels entirely - no def clone, no TOSFDataChannel.
    // The filer is already skipping their blocks at the stream level, so
    // creating an empty data channel here would only confuse downstream
    // consumers iterating Channels[].
    if AFiler.IsChannelIncluded(SrcDef.Index) then
    begin
      // Take an independent copy so the manager outlives the filer.
      OwnedDef := CloneChannelDef(SrcDef);
      FOwnedChannelDefs.Add(OwnedDef);
      DataChan := CreateOSFDataChannel(OwnedDef);
      FChannels.Add(DataChan);
    end;
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
  Channel: TOSFDataChannel;
  StartTs: Int64;
begin
  // Info / trailer blocks carry free-form XML/JSON, not sample data.
  if Block.IsInfoBlock then
    Exit;

  Channel := FindChannelByDefIndex(Block.ChannelIndex);
  if not Assigned(Channel) then
    Exit; // unknown channel - silently skip

  case Block.BlockType of
    bcAbsTimeStampData:
      begin
        // String and binary in bcAbsTimeStampData are one-sample-per-block
        // per spec. Multi-sample variable-length blocks have no standard
        // wire format — the historical Delphi per-sample uint32 length-
        // prefix layout was removed in 2026-05-24. Best-effort skip with
        // a warning per DECISIONS §8 so a single non-spec block does not
        // abort the rest of the file load.
        if Assigned(Channel.ChannelDef) and
           OSFDataTypeIsVariableLength(Channel.ChannelDef.DataType) and
           (Block.SampleCount > 1) then
          Logger.Write(
              'non-spec multi-sample variable-length bcAbsTimeStampData on ' +
              'channel %d (%s) - block skipped',
              [Block.ChannelIndex, Channel.ChannelDef.Name],
              llWarning, 'TOSFDataManager')
        else
          DecodeAbsTimestampedBlock(FVersion, Channel, Block);
      end;

    bcStartData:
      begin
        // Open a new segment whose start time matches the block. SampleRate
        // has already been recorded on Channel.ChannelDef by the filer.
        if Channel is TOSFEquidistantDataChannel then
          TOSFEquidistantDataChannel(Channel).BeginSegment(Block.StartTimestampNs);
        DecodeEquidistantBlock(Channel, Block, Block.StartTimestampNs);
        if Channel is TOSFEquidistantDataChannel then
          TOSFEquidistantDataChannel(Channel).AppendToCurrentSegment(Integer(Block.SampleCount));
      end;

    bcContinuedData:
      begin
        // Continue from the channel's last known timestamp + one increment.
        // The increment comes from SampleRate (authoritative); TimeIncrement
        // is only used as a fallback when SampleRate has not been set.
        if Assigned(Channel.ChannelDef) and (Channel.ChannelDef.SampleRate > 0) then
          StartTs := Channel.EndTimestampNs + Round(1.0E9 / Channel.ChannelDef.SampleRate)
        else if Assigned(Channel.ChannelDef) then
          StartTs := Channel.EndTimestampNs + Channel.ChannelDef.TimeIncrement
        else
          StartTs := Channel.EndTimestampNs;
        DecodeEquidistantBlock(Channel, Block, StartTs);
        if Channel is TOSFEquidistantDataChannel then
          TOSFEquidistantDataChannel(Channel).AppendToCurrentSegment(Integer(Block.SampleCount));
      end;

    bcContinuedRelStampData:
      DecodeRelTimestampedBlock(Channel, Block);

    bcMessageEvent:
      // OSF-UP4 / DECISIONS §26. Deployed device firmware writes OSF4 string
      // channels as bcMessageEvent, so these blocks carry real channel
      // content and must land as samples; before OSF-UP4 they fell into the
      // else-arm below and the channel arrived empty, silently.
      //
      // The filer has already unwrapped the length-prefixed frame, so the
      // block is exactly one sample: its absolute timestamp in
      // StartTimestampNs and the bare value bytes in RawPayload.
      //
      // Deliberately NOT routed through DecodeAbsTimestampedBlock. That
      // function applies OSF4's trailing-0x00 strip, a rule that governs
      // bcAbsTimeStampData framing only — a bcMessageEvent payload is
      // length-prefixed and carries no terminator, so reusing that path would
      // silently drop the last byte of every value. Feeding the channel
      // directly here is what keeps the strip unreachable from this branch.
      Channel.AddRawSample(Block.StartTimestampNs, Block.RawPayload);
  else
    // Reserved / deprecated block types are skipped - the filer already
    // consumed their bytes via the length field.
  end;
end;

end.
