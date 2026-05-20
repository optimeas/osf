// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

// "channels" verb. Lists every channel in the metablock with optional
// wildcard filtering. When the .json sidecar is up to date, per-channel
// sample counts and timestamps come from there; otherwise the columns
// report "?" (the metablock alone does not carry that information).
unit Cmd.Channels;

interface

uses
  System.SysUtils,
  System.IOUtils,
  System.DateUtils,
  System.JSON,
  System.Masks,
  System.Generics.Collections,
  Cmd.Base,
  OSF.Types,
  OSF.Channel,
  OSF.Log,
  OSF.Filer,
  OSF.Meta.Cache;

type
  TOsfChannelsCommand = class(TBaseCommand)
  strict private
    function MatchesFilter(const AName, APattern: string): Boolean;
    procedure PrintTable(const AFile: string; AFiler: TOSFFile;
      ACache: TOSFMetaCache; const APattern: string);
    procedure EmitJson(AFiler: TOSFFile; ACache: TOSFMetaCache;
      const APattern: string);
  public
    function Name: string; override;
    function ShortDescription: string; override;
    function DoExecute: Integer; override;
    procedure PrintHelp; override;
  end;

implementation

uses
  System.StrUtils;

const
  C_ISO_FMT = 'yyyy-mm-dd"T"hh:nn:ss';
  C_NS_PER_DAY = 86400.0 * 1.0E9;

function UnixNsToUtcDateTime(ANs: Int64): TDateTime;
begin
  if ANs = 0 then
    Exit(0);
  Result := EncodeDate(1970, 1, 1) + (ANs / C_NS_PER_DAY);
end;

function FormatTs(ANs: Int64): string;
begin
  if ANs = 0 then
    Exit('-');
  Result := FormatDateTime(C_ISO_FMT, UnixNsToUtcDateTime(ANs));
end;

// Returns the cache channel matching AName (case-insensitive) or a
// default record (sample count = 0, timestamps = 0) when the cache has
// no entry for it.
function CacheChannelFor(ACache: TOSFMetaCache; const AName: string): TOSFCacheChannel;
var
  Channels: TArray<TOSFCacheChannel>;
  I: Integer;
begin
  Result := Default(TOSFCacheChannel);
  if not Assigned(ACache) then
    Exit;
  Channels := ACache.Channels;
  for I := 0 to High(Channels) do
    if SameText(Channels[I].Name, AName) then
      Exit(Channels[I]);
end;

// ── TOsfChannelsCommand ─────────────────────────────────────────────────────

function TOsfChannelsCommand.Name: string;
begin
  Result := 'channels';
end;

function TOsfChannelsCommand.ShortDescription: string;
begin
  Result := 'List all channels in a file';
end;

procedure TOsfChannelsCommand.PrintHelp;
begin
  Print('osftool channels <file> [options]');
  Print('');
  Print('Arguments:');
  Print('  file                .osf or .osfz file');
  Print('');
  Print('Options:');
  Print('  --filter <pattern>  Wildcard filter on channel name (e.g. GPS.*)');
  Print('  --json              Output as JSON array');
  Print('  --no-cache          Do not consult .json sidecar');
  Print('  --quiet / --verbose');
end;

function TOsfChannelsCommand.MatchesFilter(const AName, APattern: string): Boolean;
begin
  if APattern = '' then
    Exit(True);
  Result := MatchesMask(AName, APattern);
end;

procedure TOsfChannelsCommand.PrintTable(const AFile: string; AFiler: TOSFFile;
  ACache: TOSFMetaCache; const APattern: string);
const
  C_HEADER = '%-4s %-30s %-10s %-8s %-10s %-19s %-19s';
  C_ROW    = '%-4d %-30s %-10s %-8s %-10s %-19s %-19s';
var
  I: Integer;
  Def: TOSFChannelDef;
  CacheCh: TOSFCacheChannel;
  Shown: Integer;
  HasCache: Boolean;
  SamplesStr, FirstStr, LastStr: string;
begin
  HasCache := Assigned(ACache);
  Print(Format(C_HEADER, ['#', 'Name', 'Type', 'Unit', 'Samples', 'First', 'Last']));
  Print(StringOfChar('-', 4 + 30 + 10 + 8 + 10 + 19 + 19 + 6));
  Shown := 0;
  for I := 0 to AFiler.Channels.Count - 1 do
  begin
    Def := AFiler.Channels[I];
    if MatchesFilter(Def.Name, APattern) then
    begin
      if HasCache then
      begin
        CacheCh := CacheChannelFor(ACache, Def.Name);
        SamplesStr := IntToStr(CacheCh.SampleCount);
        FirstStr := FormatTs(CacheCh.FirstTimestampNs);
        LastStr := FormatTs(CacheCh.LastTimestampNs);
      end
      else
      begin
        SamplesStr := '?';
        FirstStr := '?';
        LastStr := '?';
      end;
      Print(Format(C_ROW, [Def.Index, Def.Name,
        OSFDataTypeToString(Def.DataType), Def.PhysicalUnit,
        SamplesStr, FirstStr, LastStr]));
      Inc(Shown);
    end;
  end;
  Print('');
  if (APattern <> '') and (Shown < AFiler.Channels.Count) then
    Printf('%d of %d channels matched filter "%s".',
      [Shown, AFiler.Channels.Count, APattern])
  else
    Printf('%d channels total.', [AFiler.Channels.Count]);
end;

procedure TOsfChannelsCommand.EmitJson(AFiler: TOSFFile; ACache: TOSFMetaCache;
  const APattern: string);
var
  Arr: TJSONArray;
  Obj: TJSONObject;
  I: Integer;
  Def: TOSFChannelDef;
  CacheCh: TOSFCacheChannel;
  HasCache: Boolean;
begin
  HasCache := Assigned(ACache);
  Arr := TJSONArray.Create;
  try
    for I := 0 to AFiler.Channels.Count - 1 do
    begin
      Def := AFiler.Channels[I];
      if MatchesFilter(Def.Name, APattern) then
      begin
        Obj := TJSONObject.Create;
        Arr.AddElement(Obj);
        Obj.AddPair('index', TJSONNumber.Create(Def.Index));
        Obj.AddPair('name', Def.Name);
        Obj.AddPair('datatype', OSFDataTypeToString(Def.DataType));
        if Def.PhysicalUnit <> '' then
          Obj.AddPair('physicalunit', Def.PhysicalUnit);
        if HasCache then
        begin
          CacheCh := CacheChannelFor(ACache, Def.Name);
          Obj.AddPair('sample_count',      TJSONNumber.Create(CacheCh.SampleCount));
          Obj.AddPair('first_timestamp_ns', TJSONNumber.Create(CacheCh.FirstTimestampNs));
          Obj.AddPair('last_timestamp_ns',  TJSONNumber.Create(CacheCh.LastTimestampNs));
        end;
      end;
    end;
    PrintJson(Arr.Format(2));
  finally
    Arr.Free;
  end;
end;

function TOsfChannelsCommand.DoExecute: Integer;
var
  Positionals: TArray<string>;
  FileName, Pattern: string;
  Filer: TOSFFile;
  Cache: TOSFMetaCache;
  Builder: TOSFMetaCacheBuilder;
  UseCache: Boolean;
begin
  Positionals := PositionalArgs(['--filter']);
  if Length(Positionals) < 1 then
  begin
    PrintErr('osftool channels: expected a file argument');
    Exit(EXIT_BAD_ARGS);
  end;
  FileName := Positionals[0];
  Pattern := FlagValue('--filter', '');
  UseCache := not HasFlag('--no-cache');

  if not TFile.Exists(FileName) then
  begin
    PrintErrf('osftool channels: file not found: %s', [FileName]);
    Exit(EXIT_NOT_FOUND);
  end;

  Cache := nil;
  Filer := TOSFFile.Create;
  try
    Filer.OnLog := HandleLog;
    Filer.DebugEnabled := FVerbose;
    try
      Filer.OpenForRead(FileName);
    except
      on E: Exception do
      begin
        PrintErrf('osftool channels: failed to open %s: %s', [FileName, E.Message]);
        Exit(EXIT_FORMAT_ERROR);
      end;
    end;

    // Cache supplies the per-channel sample count and timestamps. Without
    // it we still list the channel definitions (no scan).
    if UseCache then
    begin
      Cache := TOSFMetaCache.Create;
      try
        if TOSFMetaCache.IsValid(FileName) then
          Cache.LoadFromFile(TOSFMetaCache.CachePathFor(FileName))
        else
        begin
          Builder := TOSFMetaCacheBuilder.Create;
          try
            Builder.OnLog := HandleLog;
            Builder.DebugEnabled := FVerbose;
            FreeAndNil(Cache);
            Cache := Builder.BuildFromFile(FileName);
          finally
            Builder.Free;
          end;
        end;
      except
        // A cache build that fails (corrupt input, etc.) is a soft
        // failure for the channels command - we still list the metablock.
        FreeAndNil(Cache);
      end;
    end;

    if FJson then
      EmitJson(Filer, Cache, Pattern)
    else
      PrintTable(FileName, Filer, Cache, Pattern);
  finally
    Cache.Free;
    Filer.Free;
  end;
  Result := EXIT_OK;
end;

end.
