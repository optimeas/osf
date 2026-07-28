// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

// Sidecar metadata cache for OSF / OSFZ files.
//
// Every OSF file 'name.osf' (or 'name.osfz') can have a peer file
// 'name.json' that records, in pre-aggregated form:
//   - source file size + last-modified timestamp (validity stamp)
//   - first / last sample timestamp across the whole file
//   - per channel: index, name, datatype, unit, sample count, first / last
//     timestamp
//
// The cache exists so that interactive tools like the merger can decide
// without opening every binary file whether a given file overlaps a time
// interval and which channels it carries. Building the cache scans the
// file once with TOSFFile in read mode but discards all sample bytes;
// only timestamps and counts are kept.
unit OSF.Meta.Cache;

interface

uses
  System.SysUtils,
  System.Classes,
  System.DateUtils,
  System.IOUtils,
  System.JSON,
  System.Generics.Collections,
  OSF.Types,
  OSF.Channel,
  OSF.Log,
  OSF.Filer;

const
  OSF_CACHE_VERSION = 1;
  OSF_CACHE_EXT     = '.json';

type
  // Per-channel summary stored in the sidecar. Independent record so it can
  // live in plain TArray without boxing or TPersistent overhead.
  TOSFCacheChannel = record
    Index: Integer;
    Name: string;
    DataType: string;
    PhysicalUnit: string;
    SampleCount: Int64;
    FirstTimestampNs: Int64;
    LastTimestampNs: Int64;
    FirstTimestampUtc: TDateTime;
    LastTimestampUtc: TDateTime;
  end;

  // Owns the parsed cache contents. Lifetime is independent from the source
  // OSF file: callers create one, fill it (manually or via the builder), and
  // free it themselves.
  TOSFMetaCache = class
  strict private
    FSourceFile: string;
    FSourceSize: Int64;
    FSourceModified: TDateTime;
    FFirstTimestampNs: Int64;
    FLastTimestampNs: Int64;
    FFirstTimestampUtc: TDateTime;
    FLastTimestampUtc: TDateTime;
    FTruncated: Boolean;
    FChannels: TArray<TOSFCacheChannel>;
  public
    constructor Create;

    // Persistence — JSON sidecar with the structure documented in
    // docs/de/references/osf5.md (extension: this project, not the OSF spec).
    procedure SaveToFile(const AJsonFile: string);
    procedure LoadFromFile(const AJsonFile: string);

    // Returns True iff the sidecar at CachePathFor(AOsfFile) exists, parses,
    // and its source_size + source_modified fields match the on-disk file.
    // Any I/O or parse error is a soft 'invalid' verdict, not an exception.
    class function IsValid(const AOsfFile: string): Boolean;

    // Maps 'D:\data\foo.osf' or 'D:\data\foo.osfz' to 'D:\data\foo.json'.
    // The base name is taken without its OSF extension; both .osf and .osfz
    // map to the same sidecar path. Callers placing both variants in the
    // same directory must rename or move one to keep the cache unique.
    class function CachePathFor(const AOsfFile: string): string;

    // Convenience: every cached channel name in metablock order.
    function ChannelNames: TArray<string>;
    function ChannelByName(const AName: string): TOSFCacheChannel;
    function HasChannel(const AName: string): Boolean;

    property SourceFile: string read FSourceFile write FSourceFile;
    property SourceSize: Int64 read FSourceSize write FSourceSize;
    property SourceModified: TDateTime read FSourceModified write FSourceModified;
    property FirstTimestampNs: Int64 read FFirstTimestampNs write FFirstTimestampNs;
    property LastTimestampNs: Int64 read FLastTimestampNs write FLastTimestampNs;
    property FirstTimestampUtc: TDateTime read FFirstTimestampUtc write FFirstTimestampUtc;
    property LastTimestampUtc: TDateTime read FLastTimestampUtc write FLastTimestampUtc;
    // True when the source file was truncated; the builder writes a
    // partial cache and flips this flag so consumers can decide what to do.
    property Truncated: Boolean read FTruncated write FTruncated;
    property Channels: TArray<TOSFCacheChannel> read FChannels write FChannels;
  end;

  // Builds a TOSFMetaCache by streaming through an OSF file with TOSFFile
  // in read mode. Sample payloads are read (so the stream stays aligned)
  // but discarded — only timestamps and counts are kept.
  TOSFMetaCacheBuilder = class
  strict private
    procedure UpdateGlobalRange(ACache: TOSFMetaCache);
  public
    // Scans AOsfFile and returns a freshly built TOSFMetaCache. Caller owns
    // the result. Never raises on a truncated file; the cache then carries
    // Truncated = True and the partial timestamps it could collect.
    function BuildFromFile(const AOsfFile: string): TOSFMetaCache;

    // Build-and-save in one call. If IsValid returns True and AForce is
    // False the sidecar is left alone — the existing cache is considered
    // up to date and we do not re-read the OSF file.
    procedure EnsureCache(const AOsfFile: string; AForce: Boolean = False);
  end;

resourcestring
  SOSFCacheVersionMismatch    = 'OSF meta cache version mismatch: file has %d, expected %d';
  SOSFCacheLogBuilt           = 'Cache built: %s  channels=%d  %s .. %s';
  SOSFCacheLogReused          = 'Cache reused (still valid): %s';
  SOSFCacheLogTruncatedSource = 'OSF source was truncated; cache saved with truncated=true';
  SOSFCacheNotJSONObject      = 'OSF meta cache: not a JSON object';

implementation

uses
  System.Math;

// ── nanosecond <-> TDateTime helpers ─────────────────────────────────────────

// Converts a Unix-epoch nanosecond timestamp to a UTC TDateTime. Loses
// precision below microseconds (TDateTime is a Double with one day = 1.0).
function UnixNsToUtcDateTime(ANs: Int64): TDateTime;
const
  C_NS_PER_DAY = 86400.0 * 1.0E9;
begin
  if ANs = 0 then
    Exit(0);
  Result := EncodeDate(1970, 1, 1) + (ANs / C_NS_PER_DAY);
end;

function UtcDateTimeToUnixNs(ADT: TDateTime): Int64;
const
  C_NS_PER_DAY = 86400.0 * 1.0E9;
begin
  if ADT = 0 then
    Exit(0);
  Result := Round((ADT - EncodeDate(1970, 1, 1)) * C_NS_PER_DAY);
end;

function FormatIso8601Utc(ADT: TDateTime): string;
begin
  if ADT = 0 then
    Exit('');
  Result := FormatDateTime('yyyy-mm-dd"T"hh:nn:ss"."zzz"Z"', ADT);
end;

function ParseIso8601Utc(const AStr: string): TDateTime;
var
  Y, M, D, H, N, S, Ms: Word;
begin
  Result := 0;
  if Length(AStr) < 19 then
    Exit;
  try
    Y := StrToInt(Copy(AStr, 1, 4));
    M := StrToInt(Copy(AStr, 6, 2));
    D := StrToInt(Copy(AStr, 9, 2));
    H := StrToInt(Copy(AStr, 12, 2));
    N := StrToInt(Copy(AStr, 15, 2));
    S := StrToInt(Copy(AStr, 18, 2));
    Ms := 0;
    if (Length(AStr) >= 23) and (AStr[20] = '.') then
      Ms := StrToIntDef(Copy(AStr, 21, 3), 0);
    Result := EncodeDateTime(Y, M, D, H, N, S, Ms);
  except
    Result := 0;
  end;
end;

// ── small JSON helpers (mirroring OSF.Filer style) ───────────────────────────

function JStr(AObj: TJSONObject; const AKey, ADefault: string): string;
var
  Val: TJSONValue;
begin
  Val := AObj.GetValue(AKey);
  if Assigned(Val) then
    Result := Val.Value
  else
    Result := ADefault;
end;

function JInt(AObj: TJSONObject; const AKey: string; ADefault: Integer): Integer;
var
  Val: TJSONValue;
begin
  Val := AObj.GetValue(AKey);
  if Assigned(Val) then
    Result := StrToIntDef(Val.Value, ADefault)
  else
    Result := ADefault;
end;

function JInt64(AObj: TJSONObject; const AKey: string; ADefault: Int64): Int64;
var
  Val: TJSONValue;
begin
  Val := AObj.GetValue(AKey);
  if Assigned(Val) then
    Result := StrToInt64Def(Val.Value, ADefault)
  else
    Result := ADefault;
end;

function JBool(AObj: TJSONObject; const AKey: string; ADefault: Boolean): Boolean;
var
  Val: TJSONValue;
begin
  Val := AObj.GetValue(AKey);
  if Val is TJSONBool then
    Result := (Val as TJSONBool).AsBoolean
  else
    Result := ADefault;
end;

// ── TOSFMetaCache ────────────────────────────────────────────────────────────

constructor TOSFMetaCache.Create;
begin
  inherited Create;
  FSourceSize := 0;
  FFirstTimestampNs := 0;
  FLastTimestampNs := 0;
  FFirstTimestampUtc := 0;
  FLastTimestampUtc := 0;
  FTruncated := False;
  SetLength(FChannels, 0);
end;

class function TOSFMetaCache.CachePathFor(const AOsfFile: string): string;
var
  Dir, Base: string;
begin
  Dir := TPath.GetDirectoryName(AOsfFile);
  Base := TPath.GetFileNameWithoutExtension(AOsfFile);
  Result := TPath.Combine(Dir, Base + OSF_CACHE_EXT);
end;

class function TOSFMetaCache.IsValid(const AOsfFile: string): Boolean;
var
  CachePath: string;
  Cache: TOSFMetaCache;
  ActualSize: Int64;
  ActualModified: TDateTime;
begin
  Result := False;
  CachePath := CachePathFor(AOsfFile);
  if not TFile.Exists(CachePath) then
    Exit;
  if not TFile.Exists(AOsfFile) then
    Exit;

  ActualSize := TFile.GetSize(AOsfFile);
  ActualModified := TFile.GetLastWriteTime(AOsfFile);

  Cache := TOSFMetaCache.Create;
  try
    try
      Cache.LoadFromFile(CachePath);
    except
      // Parse error / version mismatch / I/O failure -> invalid.
      Exit;
    end;
    if Cache.SourceSize <> ActualSize then
      Exit;
    // TDateTime equality across SaveToFile/LoadFromFile roundtrips loses
    // sub-millisecond precision; compare with a 1.5 s tolerance to absorb
    // both that drift and any filesystem-level granularity differences.
    if Abs(Cache.SourceModified - ActualModified) > (1.5 / 86400) then
      Exit;
    Result := True;
  finally
    Cache.Free;
  end;
end;

function TOSFMetaCache.ChannelNames: TArray<string>;
var
  I: Integer;
begin
  SetLength(Result, Length(FChannels));
  for I := 0 to High(FChannels) do
    Result[I] := FChannels[I].Name;
end;

function TOSFMetaCache.ChannelByName(const AName: string): TOSFCacheChannel;
var
  I: Integer;
  Lower: string;
begin
  Lower := LowerCase(AName);
  for I := 0 to High(FChannels) do
    if LowerCase(FChannels[I].Name) = Lower then
      Exit(FChannels[I]);
  Result := Default(TOSFCacheChannel);
end;

function TOSFMetaCache.HasChannel(const AName: string): Boolean;
var
  I: Integer;
  Lower: string;
begin
  Lower := LowerCase(AName);
  for I := 0 to High(FChannels) do
    if LowerCase(FChannels[I].Name) = Lower then
      Exit(True);
  Result := False;
end;

procedure TOSFMetaCache.SaveToFile(const AJsonFile: string);
var
  Root: TJSONObject;
  ChanArr: TJSONArray;
  ChanObj: TJSONObject;
  I: Integer;
  Bytes: TBytes;
begin
  Root := TJSONObject.Create;
  try
    Root.AddPair('osf_cache_version', TJSONNumber.Create(OSF_CACHE_VERSION));
    Root.AddPair('source_file', FSourceFile);
    Root.AddPair('source_size', TJSONNumber.Create(FSourceSize));
    Root.AddPair('source_modified', FormatIso8601Utc(FSourceModified));
    Root.AddPair('first_timestamp_ns', TJSONNumber.Create(FFirstTimestampNs));
    Root.AddPair('last_timestamp_ns', TJSONNumber.Create(FLastTimestampNs));
    Root.AddPair('first_timestamp_utc', FormatIso8601Utc(FFirstTimestampUtc));
    Root.AddPair('last_timestamp_utc', FormatIso8601Utc(FLastTimestampUtc));
    if FTruncated then
      Root.AddPair('truncated', TJSONBool.Create(True));

    ChanArr := TJSONArray.Create;
    Root.AddPair('channels', ChanArr);
    for I := 0 to High(FChannels) do
    begin
      ChanObj := TJSONObject.Create;
      ChanArr.AddElement(ChanObj);
      ChanObj.AddPair('index', TJSONNumber.Create(FChannels[I].Index));
      ChanObj.AddPair('name', FChannels[I].Name);
      ChanObj.AddPair('datatype', FChannels[I].DataType);
      if FChannels[I].PhysicalUnit <> '' then
        ChanObj.AddPair('physicalunit', FChannels[I].PhysicalUnit);
      ChanObj.AddPair('sample_count', TJSONNumber.Create(FChannels[I].SampleCount));
      ChanObj.AddPair('first_timestamp_ns', TJSONNumber.Create(FChannels[I].FirstTimestampNs));
      ChanObj.AddPair('last_timestamp_ns', TJSONNumber.Create(FChannels[I].LastTimestampNs));
      ChanObj.AddPair('first_timestamp_utc', FormatIso8601Utc(FChannels[I].FirstTimestampUtc));
      ChanObj.AddPair('last_timestamp_utc', FormatIso8601Utc(FChannels[I].LastTimestampUtc));
    end;

    Bytes := TEncoding.UTF8.GetBytes(Root.ToJSON);
    TFile.WriteAllBytes(AJsonFile, Bytes);
  finally
    Root.Free;
  end;
end;

procedure TOSFMetaCache.LoadFromFile(const AJsonFile: string);
var
  Root: TJSONObject;
  ChanArr: TJSONArray;
  ChanObj: TJSONObject;
  I: Integer;
  Bytes: TBytes;
  CacheVer: Integer;
begin
  Bytes := TFile.ReadAllBytes(AJsonFile);
  Root := TJSONObject.ParseJSONValue(TEncoding.UTF8.GetString(Bytes)) as TJSONObject;
  if not Assigned(Root) then
    raise EOSFFormatError.Create(SOSFCacheNotJSONObject);
  try
    CacheVer := JInt(Root, 'osf_cache_version', 0);
    if CacheVer <> OSF_CACHE_VERSION then
      raise EOSFFormatError.CreateFmt(SOSFCacheVersionMismatch, [CacheVer, OSF_CACHE_VERSION]);

    FSourceFile        := JStr(Root, 'source_file', '');
    FSourceSize        := JInt64(Root, 'source_size', 0);
    FSourceModified    := ParseIso8601Utc(JStr(Root, 'source_modified', ''));
    FFirstTimestampNs  := JInt64(Root, 'first_timestamp_ns', 0);
    FLastTimestampNs   := JInt64(Root, 'last_timestamp_ns', 0);
    FFirstTimestampUtc := ParseIso8601Utc(JStr(Root, 'first_timestamp_utc', ''));
    FLastTimestampUtc  := ParseIso8601Utc(JStr(Root, 'last_timestamp_utc', ''));
    FTruncated         := JBool(Root, 'truncated', False);

    ChanArr := Root.GetValue('channels') as TJSONArray;
    if not Assigned(ChanArr) then
      SetLength(FChannels, 0)
    else
    begin
      SetLength(FChannels, ChanArr.Count);
      for I := 0 to ChanArr.Count - 1 do
      begin
        ChanObj := ChanArr.Items[I] as TJSONObject;
        FChannels[I].Index              := JInt(ChanObj, 'index', 0);
        FChannels[I].Name               := JStr(ChanObj, 'name', '');
        FChannels[I].DataType           := JStr(ChanObj, 'datatype', '');
        FChannels[I].PhysicalUnit       := JStr(ChanObj, 'physicalunit', '');
        FChannels[I].SampleCount        := JInt64(ChanObj, 'sample_count', 0);
        FChannels[I].FirstTimestampNs   := JInt64(ChanObj, 'first_timestamp_ns', 0);
        FChannels[I].LastTimestampNs    := JInt64(ChanObj, 'last_timestamp_ns', 0);
        FChannels[I].FirstTimestampUtc  := ParseIso8601Utc(JStr(ChanObj, 'first_timestamp_utc', ''));
        FChannels[I].LastTimestampUtc   := ParseIso8601Utc(JStr(ChanObj, 'last_timestamp_utc', ''));
      end;
    end;
  finally
    Root.Free;
  end;
end;

// ── TOSFMetaCacheBuilder ─────────────────────────────────────────────────────

type
  // Running per-channel state used while scanning blocks. Held in a
  // TDictionary keyed by channel index so equidistant continuation can
  // pick up the last segment's anchor and sample rate.
  TChannelScanStats = record
    Initialised: Boolean;
    SampleCount: Int64;
    FirstTs: Int64;
    LastTs: Int64;
    // For equidistant continuation: anchor and per-sample increment in ns.
    EquidistantAnchorTs: Int64;
    EquidistantIncrementNs: Int64;
    EquidistantSamplesSinceAnchor: Int64;
  end;

procedure UpdateRange(var AStats: TChannelScanStats; ATs: Int64);
begin
  if not AStats.Initialised then
  begin
    AStats.FirstTs := ATs;
    AStats.LastTs := ATs;
    AStats.Initialised := True;
  end
  else
  begin
    if ATs < AStats.FirstTs then
      AStats.FirstTs := ATs;
    if ATs > AStats.LastTs then
      AStats.LastTs := ATs;
  end;
end;

// Extracts first and last timestamp from a bcAbsTimeStampData payload.
// Layout per spec:
//   single  : [int64 ts][value...]                          (no count field)
//   multi   : [int64 ts][maybe uint32 len][value...] × N
// We rely on Block.SampleCount and the channel datatype to size each entry.
// Variable-length data types use a per-sample uint32 length prefix in the
// multi-sample form.
procedure ExtractAbsRange(ABlock: TOSFDataBlock; AChannel: TOSFChannelDef;
  out AFirst, ALast: Int64);
var
  Pos: Integer;
  PayloadLen: Integer;
  FixedSize: Integer;
  IsVariable, IsMulti: Boolean;
  Len4: UInt32;
  I: Integer;
  Ts: Int64;
begin
  AFirst := 0;
  ALast := 0;
  PayloadLen := Length(ABlock.RawPayload);
  if PayloadLen < 8 then
    Exit;

  IsVariable := OSFDataTypeIsVariableLength(AChannel.DataType);
  IsMulti := ABlock.SampleCount > 1;
  FixedSize := OSFDataTypeFixedSize(AChannel.DataType);

  Pos := 0;
  for I := 0 to Integer(ABlock.SampleCount) - 1 do
  begin
    if Pos + 8 > PayloadLen then
      Exit;
    Move(ABlock.RawPayload[Pos], Ts, 8);
    Inc(Pos, 8);
    if I = 0 then
      AFirst := Ts;
    ALast := Ts;

    if IsVariable then
    begin
      if IsMulti then
      begin
        if Pos + 4 > PayloadLen then
          Exit;
        Move(ABlock.RawPayload[Pos], Len4, 4);
        Inc(Pos, 4);
        Inc(Pos, Integer(Len4));
      end
      else
        // Single-sample variable: rest of payload is one value.
        Pos := PayloadLen;
    end
    else
      Inc(Pos, FixedSize);
  end;
end;

procedure ApplyStartBlock(var AStats: TChannelScanStats; ABlock: TOSFDataBlock);
begin
  AStats.EquidistantAnchorTs := ABlock.StartTimestampNs;
  if ABlock.SampleRate > 0 then
    AStats.EquidistantIncrementNs := Round(1.0E9 / ABlock.SampleRate);
  AStats.EquidistantSamplesSinceAnchor := 0;
end;

procedure ApplyEquiSamples(var AStats: TChannelScanStats; ASamples: Int64);
var
  FirstTs, LastTs: Int64;
begin
  if AStats.EquidistantIncrementNs <= 0 then
    Exit;
  FirstTs := AStats.EquidistantAnchorTs +
             AStats.EquidistantSamplesSinceAnchor * AStats.EquidistantIncrementNs;
  LastTs := FirstTs + (ASamples - 1) * AStats.EquidistantIncrementNs;
  UpdateRange(AStats, FirstTs);
  UpdateRange(AStats, LastTs);
  AStats.SampleCount := AStats.SampleCount + ASamples;
  AStats.EquidistantSamplesSinceAnchor := AStats.EquidistantSamplesSinceAnchor + ASamples;
end;

procedure DispatchBlockToStats(AStatsMap: TDictionary<Integer, TChannelScanStats>;
  AFiler: TOSFFile; const ABlock: TOSFDataBlock);
var
  Stats: TChannelScanStats;
  Channel: TOSFChannelDef;
  First, Last: Int64;
begin
  if ABlock.IsInfoBlock then
    Exit;

  // The Filer guarantees Block.ChannelIndex is present in its metablock when
  // we reach this point; new channels never appear out of nowhere.
  if not AStatsMap.TryGetValue(Integer(ABlock.ChannelIndex), Stats) then
    Exit;
  Channel := AFiler.ChannelByIndex(Integer(ABlock.ChannelIndex));
  if not Assigned(Channel) then
    Exit;

  case ABlock.BlockType of
    bcStartData:
      begin
        ApplyStartBlock(Stats, ABlock);
        ApplyEquiSamples(Stats, ABlock.SampleCount);
      end;
    bcContinuedData:
      ApplyEquiSamples(Stats, ABlock.SampleCount);
    bcAbsTimeStampData:
      begin
        ExtractAbsRange(ABlock, Channel, First, Last);
        UpdateRange(Stats, First);
        UpdateRange(Stats, Last);
        Stats.SampleCount := Stats.SampleCount + ABlock.SampleCount;
      end;
    bcMessageEvent:
      begin
        // OSF-UP4 / DECISIONS §26. Deployed device firmware writes OSF4 string
        // channels this way, so these blocks are real channel content and must
        // be counted here too - otherwise the sidecar (and with it every
        // cache-backed consumer) would report zero samples for a channel the
        // data manager decodes in full, and a stale sidecar would keep saying
        // so across runs. The contract this arm exists to hold is: what the
        // cache records equals what TOSFDataManager reports for the same file.
        //
        // No frame parsing here. TOSFFile.DecodeMessageEventPayload has
        // already unwrapped the length-prefixed frame before the block
        // reaches ReadNextBlock's caller, so the sample's absolute timestamp
        // is in StartTimestampNs and SampleCount is 1 - one sample, one
        // timestamp, hence a single UpdateRange call.
        //
        // The shapes the specification leaves unspecified (bit 7 set, a
        // datatype other than string/binary) never arrive here at all: the
        // filer skips them, so this dispatch automatically agrees with the
        // manager rather than with the raw block stream.
        UpdateRange(Stats, ABlock.StartTimestampNs);
        Stats.SampleCount := Stats.SampleCount + ABlock.SampleCount;
      end;
  end;

  AStatsMap.AddOrSetValue(Integer(ABlock.ChannelIndex), Stats);
end;

procedure TOSFMetaCacheBuilder.UpdateGlobalRange(ACache: TOSFMetaCache);
var
  I: Integer;
  GotAny: Boolean;
  MinTs, MaxTs: Int64;
begin
  GotAny := False;
  MinTs := 0;
  MaxTs := 0;
  for I := 0 to High(ACache.Channels) do
    if ACache.Channels[I].SampleCount > 0 then
    begin
      if not GotAny then
      begin
        MinTs := ACache.Channels[I].FirstTimestampNs;
        MaxTs := ACache.Channels[I].LastTimestampNs;
        GotAny := True;
      end
      else
      begin
        if ACache.Channels[I].FirstTimestampNs < MinTs then
          MinTs := ACache.Channels[I].FirstTimestampNs;
        if ACache.Channels[I].LastTimestampNs > MaxTs then
          MaxTs := ACache.Channels[I].LastTimestampNs;
      end;
    end;
  if GotAny then
  begin
    ACache.FirstTimestampNs := MinTs;
    ACache.LastTimestampNs := MaxTs;
    ACache.FirstTimestampUtc := UnixNsToUtcDateTime(MinTs);
    ACache.LastTimestampUtc := UnixNsToUtcDateTime(MaxTs);
  end;
end;

function TOSFMetaCacheBuilder.BuildFromFile(const AOsfFile: string): TOSFMetaCache;
var
  Filer: TOSFFile;
  Block: TOSFDataBlock;
  StatsMap: TDictionary<Integer, TChannelScanStats>;
  I: Integer;
  Def: TOSFChannelDef;
  Stats: TChannelScanStats;
  CacheChan: TOSFCacheChannel;
  ResolvedChannels: TList<TOSFCacheChannel>;
begin
  Result := TOSFMetaCache.Create;
  try
    Result.SourceFile := ExtractFileName(AOsfFile);
    Result.SourceSize := TFile.GetSize(AOsfFile);
    Result.SourceModified := TFile.GetLastWriteTime(AOsfFile);

    StatsMap := TDictionary<Integer, TChannelScanStats>.Create;
    ResolvedChannels := TList<TOSFCacheChannel>.Create;
    Filer := TOSFFile.Create;
    try
      // Filer log messages reach any registered listener directly via
      // the global Logger — no forwarding needed. Truncation is read
      // off Filer.TruncationSeen after the scan completes.
      Filer.OpenForRead(AOsfFile);

      // Seed per-channel stats from the metablock so every declared channel
      // appears in the result even if it never produced a data block.
      for I := 0 to Filer.Channels.Count - 1 do
        StatsMap.AddOrSetValue(Filer.Channels[I].Index, Default(TChannelScanStats));

      while Filer.ReadNextBlock(Block) do
        DispatchBlockToStats(StatsMap, Filer, Block);

      // Materialise the result list in metablock order.
      for I := 0 to Filer.Channels.Count - 1 do
      begin
        Def := Filer.Channels[I];
        Stats := Default(TChannelScanStats);
        StatsMap.TryGetValue(Def.Index, Stats);

        CacheChan := Default(TOSFCacheChannel);
        CacheChan.Index := Def.Index;
        CacheChan.Name := Def.Name;
        CacheChan.DataType := OSFDataTypeToString(Def.DataType);
        CacheChan.PhysicalUnit := Def.PhysicalUnit;
        CacheChan.SampleCount := Stats.SampleCount;
        CacheChan.FirstTimestampNs := Stats.FirstTs;
        CacheChan.LastTimestampNs := Stats.LastTs;
        CacheChan.FirstTimestampUtc := UnixNsToUtcDateTime(Stats.FirstTs);
        CacheChan.LastTimestampUtc := UnixNsToUtcDateTime(Stats.LastTs);
        ResolvedChannels.Add(CacheChan);
      end;

      Result.Channels := ResolvedChannels.ToArray;
      Result.Truncated := Filer.TruncationSeen;
      UpdateGlobalRange(Result);

      Logger.Write(SOSFCacheLogBuilt,
        [ExtractFileName(AOsfFile),
         Length(Result.Channels),
         FormatIso8601Utc(Result.FirstTimestampUtc),
         FormatIso8601Utc(Result.LastTimestampUtc)],
        llInfo, 'TOSFMetaCacheBuilder');
      if Filer.TruncationSeen then
        Logger.Write(SOSFCacheLogTruncatedSource, llWarning,
                              'TOSFMetaCacheBuilder');
    finally
      Filer.Free;
      ResolvedChannels.Free;
      StatsMap.Free;
    end;
  except
    Result.Free;
    raise;
  end;
end;

procedure TOSFMetaCacheBuilder.EnsureCache(const AOsfFile: string; AForce: Boolean);
var
  Cache: TOSFMetaCache;
  CachePath: string;
begin
  CachePath := TOSFMetaCache.CachePathFor(AOsfFile);
  if (not AForce) and TOSFMetaCache.IsValid(AOsfFile) then
  begin
    Logger.Write(SOSFCacheLogReused, [ExtractFileName(AOsfFile)],
                          llDebug, 'TOSFMetaCacheBuilder');
    Exit;
  end;
  Cache := BuildFromFile(AOsfFile);
  try
    Cache.SaveToFile(CachePath);
  finally
    Cache.Free;
  end;
end;

end.
