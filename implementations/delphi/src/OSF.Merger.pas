// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

// OSF merger.
//
// Combines the per-channel sample streams from many OSF / OSFZ files that
// overlap a [start, end] UTC interval into a single OSF output file (or a
// single TOSFDataManager). The merger relies on the sidecar JSON caches
// produced by OSF.Meta.Cache to decide which files overlap the interval
// without opening the binary OSF data inside.
//
// Output format choice: every merged channel is emitted as
// bcAbsTimeStampData. Equidistant input segments are expanded into
// per-sample timestamps before being written. This trades on-disk size
// (timestamped blocks are larger than equidistant ones) for two things:
//
//   1. Correctness across heterogeneous inputs - different input files
//      can carry different segment boundaries for the same channel name;
//      a flat timestamped stream merges them deterministically.
//   2. Implementation simplicity - TOSFFile.WriteEquidistantBlock is
//      Double-only, so preserving equidistant for non-Double channels
//      would need additional public writer surface on TOSFFile.
unit OSF.Merger;

interface

uses
  System.SysUtils,
  System.Classes,
  System.DateUtils,
  System.IOUtils,
  System.Generics.Collections,
  System.Generics.Defaults,
  OSF.Types,
  OSF.Channel,
  OSF.Log,
  OSF.Filer,
  OSF.Data.Manager,
  OSF.Data.Channels,
  OSF.Meta.Cache,
  OSF.Progress;

type
  // Overlap policy applied per channel when two incoming samples carry the
  // exact same timestamp.
  TOverlapStrategy = (osSkip, osOverwrite);

  // A scanned file paired with its parsed sidecar cache. Cache is owned
  // by the merger and freed in Destroy / between Scan calls.
  TOSFFileEntry = record
    FilePath: string;
    Cache: TOSFMetaCache;
  end;

  TOSFMerger = class(TOSFLoggable)
  strict private
    FRootDirectory: string;
    FFileList: TArray<string>;
    FIntervalStartNs: Int64;
    FIntervalEndNs: Int64;
    FChannelFilter: TArray<string>;
    FOverlapStrategy: TOverlapStrategy;
    FOutputVersion: TOSFVersion;
    FUseCache: Boolean;
    FScanResult: TArray<TOSFFileEntry>;
    FReporter: IProgressReporter;
    FFileErrorCount: Integer;
    FFoundFileCount: Integer;
    procedure FreeScanCaches;
    function GatherInputFilesFromRoot: TArray<string>;
    function LoadOrBuildCache(const AOsfFile: string): TOSFMetaCache;
    function ShouldKeepFile(ACache: TOSFMetaCache): Boolean;
    procedure SortByFirstTimestamp(var AEntries: TArray<TOSFFileEntry>);
    procedure WriteMergedToStream(AStream: TStream; const AOutputLabel: string);
    procedure LogChannelMismatch(const AName, AHaveType, AGotType: string);
  public
    constructor Create;
    destructor Destroy; override;

    // Convenience: convert a UTC TDateTime pair into the ns-since-epoch
    // form used by the merger.
    procedure SetInterval(AStartUtc, AEndUtc: TDateTime);

    // Scan the configured source (RootDirectory or FileList) and select
    // files whose cached time range overlaps [IntervalStartNs, IntervalEndNs].
    // Returns the ordered list (ascending by first_timestamp_ns).
    // The merger holds onto the returned entries and frees their caches
    // on the next Scan call or on destruction.
    function Scan: TArray<TOSFFileEntry>;

    // Loads the scanned files and merges them in memory. Result is a
    // freshly constructed TOSFDataManager owned by the caller.
    function Merge: TOSFDataManager;

    // Writes the merged stream directly to AOutputFile. Internally calls
    // WriteMergedToStream on a TFileStream and skips the TOSFDataManager
    // round-trip - handy for large merges that should not stay resident.
    procedure SaveToFile(const AOutputFile: string);

    // Scan + WriteMergedToStream in one call - combines configuration
    // checks (sanitising the interval, ensuring at least one input file)
    // with the write itself. Raises EOSFException if nothing to merge.
    procedure Execute(const AOutputFile: string);

    // --- Input source (use one or the other) ---
    property RootDirectory: string read FRootDirectory write FRootDirectory;
    property FileList: TArray<string> read FFileList write FFileList;

    // --- Time interval (UTC, nanoseconds since Unix epoch) ---
    property IntervalStartNs: Int64 read FIntervalStartNs write FIntervalStartNs;
    property IntervalEndNs: Int64 read FIntervalEndNs write FIntervalEndNs;

    // --- Channel selection - empty = all channels.  Case-insensitive ---
    property ChannelFilter: TArray<string> read FChannelFilter write FChannelFilter;

    // --- Merge options ---
    property OverlapStrategy: TOverlapStrategy read FOverlapStrategy write FOverlapStrategy;
    property OutputVersion: TOSFVersion read FOutputVersion write FOutputVersion;

    // When False, the merger never reads or writes JSON sidecar caches -
    // every Scan rebuilds them in memory. Default: True.
    property UseCache: Boolean read FUseCache write FUseCache;

    // Optional structured-progress sink. When assigned, the merger emits
    // phase events through it; when nil it falls back to the plain OnLog
    // messages, so callers that need no progress display are unaffected.
    property Reporter: IProgressReporter read FReporter write FReporter;
    // Count of input files dropped during Scan because they could not be
    // read. Valid after Scan; reset at the start of every Scan.
    property FileErrorCount: Integer read FFileErrorCount;
    // Total number of .osf / .osfz files the last Scan discovered, before
    // interval filtering. Valid after Scan.
    property FoundFileCount: Integer read FFoundFileCount;
  end;

resourcestring
  SOSFMergerNothingToMerge      = 'OSF merger: no input files in the configured interval';
  SOSFMergerNoInterval          = 'OSF merger: interval is empty (IntervalEndNs <= IntervalStartNs)';
  SOSFMergerLogScanRoot         = 'Scan: scanning %s';
  SOSFMergerLogScanFound        = 'Scan: found %d OSF / OSFZ files';
  SOSFMergerLogScanOverlap      = 'Scan: %d files overlap interval %s .. %s';
  SOSFMergerLogScanBuildingCache = 'Scan: building cache for new file: %s';
  SOSFMergerLogMergingFile      = 'Merging file %d/%d: %s';
  SOSFMergerLogChannelMismatch  = 'Channel "%s": type mismatch (%s vs %s) - skipping for this file';
  SOSFMergerLogMergeComplete    = 'Merge complete: %d channels, %d total samples';

implementation

uses
  System.Math,
  System.StrUtils;

const
  C_NS_PER_DAY = 86400.0 * 1.0E9;

// ── Timestamp helpers ────────────────────────────────────────────────────────

function UtcDateTimeToUnixNs(ADT: TDateTime): Int64;
begin
  if ADT = 0 then
    Exit(0);
  Result := Round((ADT - EncodeDate(1970, 1, 1)) * C_NS_PER_DAY);
end;

function UnixNsToUtcDateTime(ANs: Int64): TDateTime;
begin
  if ANs = 0 then
    Exit(0);
  Result := EncodeDate(1970, 1, 1) + (ANs / C_NS_PER_DAY);
end;

function FormatIso8601Utc(ADT: TDateTime): string;
begin
  if ADT = 0 then
    Exit('(none)');
  Result := FormatDateTime('yyyy-mm-dd"T"hh:nn:ss"Z"', ADT);
end;

// ── Per-channel sample accumulator ───────────────────────────────────────────

type
  // One merged sample. Holds the absolute UTC nanosecond timestamp and the
  // raw bytes of the value as encoded for the channel's DataType.
  TMergedSample = record
    TimestampNs: Int64;
    Value: TBytes;
  end;

  // Per-output-channel accumulator. The merger keeps one of these per
  // unique channel name (case-insensitive) encountered across the inputs.
  TChannelMerge = class
  strict private
    FName: string;
    FDef: TOSFChannelDef;
    FSamples: TDictionary<Int64, TBytes>;
  public
    constructor Create(ADef: TOSFChannelDef);
    destructor Destroy; override;

    // Adds (or replaces) a sample for the given timestamp. Returns True if
    // the sample was newly inserted, False if it was a duplicate timestamp
    // and the strategy was osSkip (the existing value stays). With
    // osOverwrite, the caller's bytes replace the existing entry and the
    // function still returns True.
    function AddSample(ATs: Int64; const AValue: TBytes; AStrategy: TOverlapStrategy): Boolean;

    function SampleCount: Integer;
    // Returns the samples sorted ascending by timestamp. Caller takes a
    // snapshot at the moment of the call; subsequent AddSample calls do
    // not affect the returned array.
    function GetSortedSamples: TArray<TMergedSample>;

    property Name: string read FName;
    property Def: TOSFChannelDef read FDef;
  end;

constructor TChannelMerge.Create(ADef: TOSFChannelDef);
begin
  inherited Create;
  FDef := ADef;
  FName := ADef.Name;
  FSamples := TDictionary<Int64, TBytes>.Create;
end;

destructor TChannelMerge.Destroy;
begin
  FSamples.Free;
  inherited;
end;

function TChannelMerge.AddSample(ATs: Int64; const AValue: TBytes; AStrategy: TOverlapStrategy): Boolean;
begin
  if FSamples.ContainsKey(ATs) then
  begin
    if AStrategy = osOverwrite then
    begin
      FSamples.AddOrSetValue(ATs, AValue);
      Result := True;
    end
    else
      Result := False;
  end
  else
  begin
    FSamples.Add(ATs, AValue);
    Result := True;
  end;
end;

function TChannelMerge.SampleCount: Integer;
begin
  Result := FSamples.Count;
end;

function TChannelMerge.GetSortedSamples: TArray<TMergedSample>;
var
  Keys: TArray<Int64>;
  I: Integer;
  Comparer: IComparer<Int64>;
begin
  Keys := FSamples.Keys.ToArray;
  Comparer := TComparer<Int64>.Construct(
    function(const L, R: Int64): Integer
    begin
      if L < R then Exit(-1);
      if L > R then Exit(1);
      Result := 0;
    end);
  TArray.Sort<Int64>(Keys, Comparer);

  SetLength(Result, Length(Keys));
  for I := 0 to High(Keys) do
  begin
    Result[I].TimestampNs := Keys[I];
    Result[I].Value := FSamples[Keys[I]];
  end;
end;

// ── Value-to-bytes encoder ───────────────────────────────────────────────────

// Encodes the value at SrcIdx of the source channel to the raw byte form
// required by its DataType. Returns False for types this build cannot
// re-encode (currently: none - every fixed type plus string / binary /
// gpslocation is covered through ValueAsString / TValueAsBytes paths).
function EncodeSampleFromSource(ASrc: TOSFDataChannel; ASrcIdx: Integer; out ABytes: TBytes): Boolean;
var
  FixedSize: Integer;
  ValD: Double;
  ValF: Single;
  Val32: Int32;
  ValU32: UInt32;
  Val64: Int64;
  ValU64: UInt64;
  ValU16: UInt16;
  ValI16: Int16;
  ValU8: Byte;
  ValI8: ShortInt;
  AsStr: string;
  AsUtf8: TBytes;
  Gps: TOSFGpsLocation;
begin
  Result := True;
  if not Assigned(ASrc.ChannelDef) then
    Exit(False);

  FixedSize := OSFDataTypeFixedSize(ASrc.ChannelDef.DataType);

  case ASrc.ChannelDef.DataType of
    dtDouble:
      begin
        SetLength(ABytes, FixedSize);
        ValD := ASrc.ValueAsDouble(ASrcIdx);
        Move(ValD, ABytes[0], FixedSize);
      end;
    dtFloat:
      begin
        SetLength(ABytes, FixedSize);
        ValF := Single(ASrc.ValueAsDouble(ASrcIdx));
        Move(ValF, ABytes[0], FixedSize);
      end;
    dtInt8:
      begin
        SetLength(ABytes, FixedSize);
        ValI8 := ShortInt(Round(ASrc.ValueAsDouble(ASrcIdx)));
        Move(ValI8, ABytes[0], FixedSize);
      end;
    dtInt16:
      begin
        SetLength(ABytes, FixedSize);
        ValI16 := Int16(Round(ASrc.ValueAsDouble(ASrcIdx)));
        Move(ValI16, ABytes[0], FixedSize);
      end;
    dtInt32:
      begin
        SetLength(ABytes, FixedSize);
        Val32 := Int32(Round(ASrc.ValueAsDouble(ASrcIdx)));
        Move(Val32, ABytes[0], FixedSize);
      end;
    dtInt64:
      begin
        SetLength(ABytes, FixedSize);
        Val64 := Round(ASrc.ValueAsDouble(ASrcIdx));
        Move(Val64, ABytes[0], FixedSize);
      end;
    dtUInt8:
      begin
        SetLength(ABytes, FixedSize);
        ValU8 := Byte(Round(ASrc.ValueAsDouble(ASrcIdx)));
        Move(ValU8, ABytes[0], FixedSize);
      end;
    dtUInt16:
      begin
        SetLength(ABytes, FixedSize);
        ValU16 := UInt16(Round(ASrc.ValueAsDouble(ASrcIdx)));
        Move(ValU16, ABytes[0], FixedSize);
      end;
    dtUInt32:
      begin
        SetLength(ABytes, FixedSize);
        ValU32 := UInt32(Round(ASrc.ValueAsDouble(ASrcIdx)));
        Move(ValU32, ABytes[0], FixedSize);
      end;
    dtUInt64:
      begin
        SetLength(ABytes, FixedSize);
        ValU64 := UInt64(Round(ASrc.ValueAsDouble(ASrcIdx)));
        Move(ValU64, ABytes[0], FixedSize);
      end;
    dtBool:
      begin
        SetLength(ABytes, FixedSize);
        if ASrc.ValueAsDouble(ASrcIdx) <> 0 then
          ABytes[0] := 1
        else
          ABytes[0] := 0;
      end;
    dtString:
      begin
        AsStr := ASrc.ValueAsString(ASrcIdx);
        AsUtf8 := TEncoding.UTF8.GetBytes(AsStr);
        // Trailing 0x00 is added by TOSFFile.WriteTimestampedBlock when
        // the channel is variable-length; we hand it the bare bytes.
        ABytes := AsUtf8;
      end;
    dtBinary:
      begin
        // For binary channels stored in a TList<TBytes>, the source
        // channel's ValueAsString returns a hex / base64 representation
        // that loses fidelity. Fetch the raw bytes via RTTI-style cast
        // on the concrete subclass.
        if ASrc is TOSFTimestampedBinaryChannel then
          ABytes := Copy(TOSFTimestampedBinaryChannel(ASrc).Values[ASrcIdx])
        else if ASrc is TOSFEquidistantBinaryChannel then
          ABytes := Copy(TOSFEquidistantBinaryChannel(ASrc).Values[ASrcIdx])
        else
          Exit(False);
      end;
    dtGpsLocation:
      begin
        // Gps channels carry TOSFGpsLocation records (24 bytes packed).
        // The Values[I] indexer returns a value copy on TList<TOSFGpsLocation>;
        // we stage it through a local so Move sees a writeable LHS.
        SetLength(ABytes, FixedSize);
        if ASrc is TOSFTimestampedGpsChannel then
          Gps := TOSFTimestampedGpsChannel(ASrc).Values[ASrcIdx]
        else if ASrc is TOSFEquidistantGpsChannel then
          Gps := TOSFEquidistantGpsChannel(ASrc).Values[ASrcIdx]
        else
          Exit(False);
        Move(Gps, ABytes[0], FixedSize);
      end;
  else
    Exit(False);
  end;
end;

// ── TOSFMerger ───────────────────────────────────────────────────────────────

constructor TOSFMerger.Create;
begin
  inherited Create;
  FOverlapStrategy := osSkip;
  FOutputVersion := osvOSF5;
  FUseCache := True;
end;

destructor TOSFMerger.Destroy;
begin
  FreeScanCaches;
  inherited;
end;

procedure TOSFMerger.FreeScanCaches;
var
  I: Integer;
begin
  for I := 0 to High(FScanResult) do
    if Assigned(FScanResult[I].Cache) then
    begin
      FScanResult[I].Cache.Free;
      FScanResult[I].Cache := nil;
    end;
  SetLength(FScanResult, 0);
end;

procedure TOSFMerger.SetInterval(AStartUtc, AEndUtc: TDateTime);
begin
  FIntervalStartNs := UtcDateTimeToUnixNs(AStartUtc);
  FIntervalEndNs := UtcDateTimeToUnixNs(AEndUtc);
end;

function TOSFMerger.GatherInputFilesFromRoot: TArray<string>;
var
  Found: TStringList;
  S: string;
begin
  Found := TStringList.Create;
  try
    Found.Sorted := True;
    Found.Duplicates := dupIgnore;
    if TDirectory.Exists(FRootDirectory) then
    begin
      for S in TDirectory.GetFiles(FRootDirectory, '*.osf', TSearchOption.soAllDirectories) do
        Found.Add(S);
      for S in TDirectory.GetFiles(FRootDirectory, '*.osfz', TSearchOption.soAllDirectories) do
        Found.Add(S);
    end;
    Result := Found.ToStringArray;
  finally
    Found.Free;
  end;
end;

function TOSFMerger.LoadOrBuildCache(const AOsfFile: string): TOSFMetaCache;
var
  Builder: TOSFMetaCacheBuilder;
  CachePath: string;
  CacheValid: Boolean;
begin
  CachePath := TOSFMetaCache.CachePathFor(AOsfFile);
  CacheValid := FUseCache and TOSFMetaCache.IsValid(AOsfFile);
  if CacheValid then
  begin
    Result := TOSFMetaCache.Create;
    try
      Result.LoadFromFile(CachePath);
    except
      Result.Free;
      raise;
    end;
    Exit;
  end;

  // No (or invalid) sidecar - build one in memory and (optionally) save it.
  Log(llInfo, SOSFMergerLogScanBuildingCache, [ExtractFileName(AOsfFile)]);
  Builder := TOSFMetaCacheBuilder.Create;
  try
    Builder.OnLog := OnLog;
    Builder.DebugEnabled := DebugEnabled;
    Result := Builder.BuildFromFile(AOsfFile);
    if FUseCache then
    try
      Result.SaveToFile(CachePath);
    except
      // A failed sidecar save is non-fatal; the in-memory cache is still
      // usable for this run.
    end;
  finally
    Builder.Free;
  end;
end;

function TOSFMerger.ShouldKeepFile(ACache: TOSFMetaCache): Boolean;
begin
  // Empty caches (no channels with samples) cannot overlap anything.
  if (ACache.FirstTimestampNs = 0) and (ACache.LastTimestampNs = 0) then
    Exit(False);
  Result := (ACache.LastTimestampNs >= FIntervalStartNs) and
            (ACache.FirstTimestampNs <= FIntervalEndNs);
end;

procedure TOSFMerger.SortByFirstTimestamp(var AEntries: TArray<TOSFFileEntry>);
var
  Comparer: IComparer<TOSFFileEntry>;
begin
  Comparer := TComparer<TOSFFileEntry>.Construct(
    function(const L, R: TOSFFileEntry): Integer
    begin
      if L.Cache.FirstTimestampNs < R.Cache.FirstTimestampNs then Exit(-1);
      if L.Cache.FirstTimestampNs > R.Cache.FirstTimestampNs then Exit(1);
      Result := 0;
    end);
  TArray.Sort<TOSFFileEntry>(AEntries, Comparer);
end;

function TOSFMerger.Scan: TArray<TOSFFileEntry>;
var
  Sources: TArray<string>;
  I, CacheCreated: Integer;
  Cache: TOSFMetaCache;
  Kept: TList<TOSFFileEntry>;
  Entry: TOSFFileEntry;
begin
  FreeScanCaches;
  FFileErrorCount := 0;
  if Assigned(FReporter) then
    FReporter.ScanStarted(FRootDirectory);

  if Length(FFileList) > 0 then
    Sources := FFileList
  else
  begin
    Log(llInfo, SOSFMergerLogScanRoot, [FRootDirectory]);
    Sources := GatherInputFilesFromRoot;
  end;
  FFoundFileCount := Length(Sources);
  Log(llInfo, SOSFMergerLogScanFound, [Length(Sources)]);
  if Assigned(FReporter) then
    FReporter.ScanFinished(Length(Sources));

  // The sidecar phase is only surfaced when at least one cache has to be
  // built - when every sidecar is already valid it stays invisible.
  if Assigned(FReporter) then
    FReporter.SidecarStarted(Length(Sources));

  CacheCreated := 0;
  Kept := TList<TOSFFileEntry>.Create;
  try
    for I := 0 to High(Sources) do
    begin
      // A single broken file (corrupt metablock, truncation the builder
      // cannot recover from) must not kill the whole scan. Drop it and
      // carry on - as a structured file error when a reporter is set,
      // otherwise as a plain warning.
      Cache := nil;
      try
        if not TOSFMetaCache.IsValid(Sources[i]) then
          inc(CacheCreated);
        Cache := LoadOrBuildCache(Sources[I]);
      except
        on E: Exception do
        begin
          if Assigned(FReporter) then
          begin
            Inc(FFileErrorCount);
            FReporter.FileError(I + 1, Sources[I],
              E.ClassName + ': ' + E.Message);
          end
          else
            Log(llWarning, 'Scan: skipping "%s" - %s: %s',
              [ExtractFileName(Sources[I]), E.ClassName, E.Message]);
          FreeAndNil(Cache);
        end;
      end;
      if Assigned(FReporter) then
        FReporter.SidecarProgress(I + 1, Length(Sources));
      if Assigned(Cache) then
      begin
        if ShouldKeepFile(Cache) then
        begin
          Entry.FilePath := Sources[I];
          Entry.Cache := Cache;
          Kept.Add(Entry);
        end
        else
          Cache.Free;
      end;
    end;
    FScanResult := Kept.ToArray;
  finally
    Kept.Free;
  end;

  if Assigned(FReporter) then
    FReporter.SidecarFinished(CacheCreated);

  SortByFirstTimestamp(FScanResult);

  Log(llInfo, SOSFMergerLogScanOverlap, [Length(FScanResult),
    FormatIso8601Utc(UnixNsToUtcDateTime(FIntervalStartNs)),
    FormatIso8601Utc(UnixNsToUtcDateTime(FIntervalEndNs))]);

  Result := FScanResult;
end;

procedure TOSFMerger.LogChannelMismatch(const AName, AHaveType, AGotType: string);
begin
  Log(llWarning, SOSFMergerLogChannelMismatch, [AName, AHaveType, AGotType]);
end;

// Adds all in-interval samples from a source channel to the matching merge
// accumulator. Caller has already verified data-type compatibility.
procedure IngestChannel(AMerge: TChannelMerge; ASrc: TOSFDataChannel;
  AIntervalStartNs, AIntervalEndNs: Int64; AStrategy: TOverlapStrategy);
var
  I: Integer;
  Ts: Int64;
  Bytes: TBytes;
begin
  for I := 0 to ASrc.SampleCount - 1 do
  begin
    Ts := ASrc.TimestampNsAt(I);
    if (Ts >= AIntervalStartNs) and (Ts <= AIntervalEndNs) then
      if EncodeSampleFromSource(ASrc, I, Bytes) then
        AMerge.AddSample(Ts, Bytes, AStrategy);
  end;
end;

// Loads one OSF file into a fresh TOSFDataManager (with the merger's
// ChannelFilter applied) and ingests every loaded channel into the
// accumulator dictionary.
procedure TOSFMerger.WriteMergedToStream(AStream: TStream; const AOutputLabel: string);
var
  Entry: TOSFFileEntry;
  FileIndex: Integer;
  FileSamples: Integer;
  Manager: TOSFDataManager;
  I: Integer;
  SrcChan: TOSFDataChannel;
  Merge: TChannelMerge;
  Accumulators: TObjectDictionary<string, TChannelMerge>;
  Order: TList<string>;
  OutFiler: TOSFFile;
  Key: string;
  ClonedDef: TOSFChannelDef;
  Samples: TArray<TMergedSample>;
  Timestamps: TArray<Int64>;
  Values: TArray<TBytes>;
  J: Integer;
  TotalSamples: Int64;
begin
  Accumulators := TObjectDictionary<string, TChannelMerge>.Create([doOwnsValues]);
  Order := TList<string>.Create;
  try
    // ── Ingest every file in scan order ────────────────────────────────────
    if Assigned(FReporter) then
      FReporter.ReadStarted(Length(FScanResult));
    for FileIndex := 0 to High(FScanResult) do
    begin
      Entry := FScanResult[FileIndex];
      if Assigned(FReporter) then
        FReporter.FileStarted(FileIndex + 1, Length(FScanResult), Entry.FilePath)
      else
        Log(llInfo, SOSFMergerLogMergingFile,
          [FileIndex + 1, Length(FScanResult), ExtractFileName(Entry.FilePath)]);

      FileSamples := 0;
      Manager := TOSFDataManager.Create;
      try
        Manager.OnLog := OnLog;
        Manager.DebugEnabled := DebugEnabled;
        Manager.ChannelFilter := FChannelFilter;
        Manager.LoadFromFile(Entry.FilePath);

        for I := 0 to Manager.Channels.Count - 1 do
        begin
          SrcChan := Manager.Channels[I];
          Inc(FileSamples, SrcChan.SampleCount);
          if not Assigned(SrcChan.ChannelDef) then
          begin
            // Defensive: a freshly loaded manager always wires defs, but
            // guard so a future shape change doesn't blow up the merge.
          end
          else
          begin
            Key := LowerCase(SrcChan.Name);
            if not Accumulators.TryGetValue(Key, Merge) then
            begin
              // First sighting - clone the def so the accumulator outlives
              // the per-file manager.
              ClonedDef := TOSFChannelDef.Create(
                SrcChan.ChannelDef.Index, SrcChan.ChannelDef.Name,
                SrcChan.ChannelDef.ChannelType, SrcChan.ChannelDef.DataType);
              ClonedDef.PhysicalUnit := SrcChan.ChannelDef.PhysicalUnit;
              ClonedDef.PhysicalDimension := SrcChan.ChannelDef.PhysicalDimension;
              ClonedDef.MimeType := SrcChan.ChannelDef.MimeType;
              ClonedDef.DisplayName := SrcChan.ChannelDef.DisplayName;
              ClonedDef.Comment := SrcChan.ChannelDef.Comment;
              // sizeoflengthvalue=4 keeps room for big merged blocks.
              ClonedDef.LengthFieldSize := lfs4;
              // The merger emits everything as timestamped.
              ClonedDef.TimeIncrement := 0;
              Merge := TChannelMerge.Create(ClonedDef);
              Accumulators.Add(Key, Merge);
              Order.Add(Key);
              IngestChannel(Merge, SrcChan, FIntervalStartNs, FIntervalEndNs, FOverlapStrategy);
            end
            else if Merge.Def.DataType <> SrcChan.ChannelDef.DataType then
              LogChannelMismatch(ExtractFileName(Entry.FilePath)+':'+SrcChan.Name,
                OSFDataTypeToString(Merge.Def.DataType),
                OSFDataTypeToString(SrcChan.ChannelDef.DataType))
            else
              IngestChannel(Merge, SrcChan, FIntervalStartNs, FIntervalEndNs, FOverlapStrategy);
          end;
        end;
        if Assigned(FReporter) then
          FReporter.FileFinished(FileIndex + 1, Manager.Channels.Count, FileSamples);
      finally
        Manager.Free;
      end;
    end;

    // ── Write the merged result as one OSF file ───────────────────────────
    if Assigned(FReporter) then
      FReporter.WriteStarted(AOutputLabel);
    OutFiler := TOSFFile.Create;
    try
      OutFiler.OnLog := OnLog;
      OutFiler.DebugEnabled := DebugEnabled;
      OutFiler.CreateForWrite(AStream, False, FOutputVersion);

      // Reassign channel indices 0..N-1 in deterministic ingest order.
      for I := 0 to Order.Count - 1 do
      begin
        Merge := Accumulators[Order[I]];
        Merge.Def.Index := I;
        OutFiler.AddChannel(Merge.Def);
      end;
      OutFiler.WriteHeader;

      if Assigned(FReporter) then
        FReporter.StartProgress('', 0, Order.Count);

      TotalSamples := 0;
      for I := 0 to Order.Count - 1 do
      begin
        Merge := Accumulators[Order[I]];
        Samples := Merge.GetSortedSamples;
        if Length(Samples) > 0 then
        begin
          SetLength(Timestamps, Length(Samples));
          SetLength(Values, Length(Samples));
          for J := 0 to High(Samples) do
          begin
            Timestamps[J] := Samples[J].TimestampNs;
            Values[J] := Samples[J].Value;
          end;
          OutFiler.WriteTimestampedBlock(Merge.Def.Index, Timestamps, Values);
          TotalSamples := TotalSamples + Length(Samples);
        end;

        if Assigned(FReporter) then
          FReporter.DoProgress('', I+1, Order.Count);
      end;

      OutFiler.Close;

      if Assigned(FReporter) then
        FReporter.EndProgress;

      Log(llInfo, SOSFMergerLogMergeComplete, [Order.Count, TotalSamples]);
      if Assigned(FReporter) then
        FReporter.WriteFinished(AOutputLabel, AStream.Size);
    finally
      OutFiler.Free;
    end;
  finally
    Order.Free;
    // doOwnsValues frees the TChannelMerge instances; the cloned defs
    // they hold are freed by Merge.Destroy.
    Accumulators.Free;
  end;
end;

function TOSFMerger.Merge: TOSFDataManager;
var
  MS: TMemoryStream;
begin
  if FIntervalEndNs <= FIntervalStartNs then
    raise EOSFException.Create(SOSFMergerNoInterval);
  if Length(FScanResult) = 0 then
    Scan;
  if Length(FScanResult) = 0 then
    raise EOSFException.Create(SOSFMergerNothingToMerge);

  MS := TMemoryStream.Create;
  try
    WriteMergedToStream(MS, '');
    MS.Position := 0;
    Result := TOSFDataManager.Create;
    try
      Result.OnLog := OnLog;
      Result.DebugEnabled := DebugEnabled;
      Result.LoadFromStream(MS);
    except
      Result.Free;
      raise;
    end;
  finally
    MS.Free;
  end;
end;

procedure TOSFMerger.SaveToFile(const AOutputFile: string);
var
  FS: TFileStream;
begin
  if FIntervalEndNs <= FIntervalStartNs then
    raise EOSFException.Create(SOSFMergerNoInterval);
  if Length(FScanResult) = 0 then
    Scan;
  if Length(FScanResult) = 0 then
    raise EOSFException.Create(SOSFMergerNothingToMerge);

  FS := TFileStream.Create(AOutputFile, fmCreate);
  try
    WriteMergedToStream(FS, AOutputFile);
  finally
    FS.Free;
  end;
end;

procedure TOSFMerger.Execute(const AOutputFile: string);
begin
  Scan;
  SaveToFile(AOutputFile);
end;

end.
