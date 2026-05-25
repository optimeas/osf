// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

// "info" verb. Shows file metadata, channel count, and first / last
// timestamps. Uses the sidecar cache when available (and not disabled
// via --no-cache) so the operation does not need to scan all blocks.
unit Cmd.Info;

interface

uses
  System.SysUtils,
  System.IOUtils,
  System.DateUtils,
  System.JSON,
  Cmd.Base,
  OSF.Types,
  OSF.Channel,
  OSF.Log,
  OSF.Filer,
  OSF.Meta.Cache;

type
  TOsfInfoCommand = class(TBaseCommand)
  strict private
    procedure PrintHuman(const AFile: string; AFiler: TOSFFile;
      AFirstNs, ALastNs: Int64);
    procedure EmitJson(const AFile: string; AFiler: TOSFFile;
      AFirstNs, ALastNs: Int64);
    function ResolveTimeRange(const AFile: string; AFiler: TOSFFile;
      out AFirstNs, ALastNs: Int64): Boolean;
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
  C_ISO_FMT = 'yyyy-mm-dd"T"hh:nn:ss"."zzz"Z"';
  C_NS_PER_DAY = 86400.0 * 1.0E9;

resourcestring
  SInfoDesc = 'Show file metadata and time range';
  SInfoHelp =
    'osftool info <file> [options]' + sLineBreak +
    sLineBreak +
    'Show file metadata and global time range.' + sLineBreak +
    sLineBreak +
    'Arguments:' + sLineBreak +
    '  file        .osf or .osfz file' + sLineBreak +
    sLineBreak +
    'Options:' + sLineBreak +
    '  --json      Output as JSON' + sLineBreak +
    '  --no-cache  Do not consult .json sidecar; scan the OSF file directly' + sLineBreak +
    '  --quiet / --verbose';
  SInfoErrExpectFile   = 'osftool info: expected a file argument';
  SInfoErrFileNotFound = 'osftool info: file not found: %s';
  SInfoErrOpenFailed   = 'osftool info: failed to open %s: %s';
  SInfoFile           = 'File:        %s';
  SInfoSize           = 'Size:        %d bytes';
  SInfoVersionOsf4    = 'Version:     OSF4';
  SInfoVersionOsf5    = 'Version:     OSF5';
  SInfoVersionUnknown = 'Version:     unknown';
  SInfoCreator        = 'Creator:     %s';
  SInfoCreatedUtc     = 'Created UTC: %s';
  SInfoTag            = 'Tag:         %s';
  SInfoReason         = 'Reason:      %s';
  SInfoComment        = 'Comment:     %s';
  SInfoChannels       = 'Channels:    %d';
  SInfoFirstData      = 'First data:  %s';
  SInfoLastData       = 'Last data:   %s';
  SInfoDuration       = 'Duration:    %s';

function UnixNsToUtcDateTime(ANs: Int64): TDateTime;
begin
  if ANs = 0 then
    Exit(0);
  Result := EncodeDate(1970, 1, 1) + (ANs / C_NS_PER_DAY);
end;

function FormatUtc(ADT: TDateTime): string;
begin
  if ADT = 0 then
    Exit('');
  Result := FormatDateTime(C_ISO_FMT, ADT);
end;

function FormatDuration(AStartNs, AEndNs: Int64): string;
var
  Total: Int64;
  H, M, S: Int64;
begin
  if (AStartNs = 0) or (AEndNs = 0) or (AEndNs < AStartNs) then
    Exit('-');
  Total := (AEndNs - AStartNs) div 1000000000; // ns → s
  H := Total div 3600;
  M := (Total mod 3600) div 60;
  S := Total mod 60;
  Result := Format('%dh %dm %ds', [H, M, S]);
end;

// ── TOsfInfoCommand ─────────────────────────────────────────────────────────

function TOsfInfoCommand.Name: string;
begin
  Result := 'info';
end;

function TOsfInfoCommand.ShortDescription: string;
begin
  Result := SInfoDesc;
end;

procedure TOsfInfoCommand.PrintHelp;
begin
  Print(SInfoHelp);
end;

// Reads the global first/last timestamp from the sidecar if available,
// otherwise scans every block of the source file via TOSFMetaCacheBuilder.
function TOsfInfoCommand.ResolveTimeRange(const AFile: string; AFiler: TOSFFile;
  out AFirstNs, ALastNs: Int64): Boolean;
var
  Cache: TOSFMetaCache;
  Builder: TOSFMetaCacheBuilder;
  UseCache: Boolean;
begin
  AFirstNs := 0;
  ALastNs := 0;
  UseCache := not HasFlag('--no-cache');

  if UseCache and TOSFMetaCache.IsValid(AFile) then
  begin
    Cache := TOSFMetaCache.Create;
    try
      Cache.LoadFromFile(TOSFMetaCache.CachePathFor(AFile));
      AFirstNs := Cache.FirstTimestampNs;
      ALastNs := Cache.LastTimestampNs;
      Exit(True);
    finally
      Cache.Free;
    end;
  end;

  // Fall back to a full scan (cache builder discards sample data; this
  // still touches every block but never holds samples in memory).
  Builder := TOSFMetaCacheBuilder.Create;
  try
    Cache := Builder.BuildFromFile(AFile);
    try
      AFirstNs := Cache.FirstTimestampNs;
      ALastNs := Cache.LastTimestampNs;
      Result := True;
    finally
      Cache.Free;
    end;
  finally
    Builder.Free;
  end;
end;

procedure TOsfInfoCommand.PrintHuman(const AFile: string; AFiler: TOSFFile;
  AFirstNs, ALastNs: Int64);
begin
  Printf(SInfoFile, [TPath.GetFileName(AFile)]);
  Printf(SInfoSize, [TFile.GetSize(AFile)]);
  case AFiler.Version of
    osvOSF4: Print(SInfoVersionOsf4);
    osvOSF5: Print(SInfoVersionOsf5);
  else
    Print(SInfoVersionUnknown);
  end;
  if AFiler.Metadata.Creator <> '' then
    Printf(SInfoCreator, [AFiler.Metadata.Creator]);
  if AFiler.Metadata.CreatedUtc <> 0 then
    Printf(SInfoCreatedUtc, [FormatUtc(AFiler.Metadata.CreatedUtc)]);
  if AFiler.Metadata.Tag <> '' then
    Printf(SInfoTag, [AFiler.Metadata.Tag]);
  if AFiler.Metadata.Reason <> '' then
    Printf(SInfoReason, [AFiler.Metadata.Reason]);
  if AFiler.Metadata.Comment <> '' then
    Printf(SInfoComment, [AFiler.Metadata.Comment]);
  Printf(SInfoChannels, [AFiler.ChannelCount]);
  if AFirstNs > 0 then
    Printf(SInfoFirstData, [FormatUtc(UnixNsToUtcDateTime(AFirstNs))]);
  if ALastNs > 0 then
    Printf(SInfoLastData, [FormatUtc(UnixNsToUtcDateTime(ALastNs))]);
  Printf(SInfoDuration, [FormatDuration(AFirstNs, ALastNs)]);
end;

procedure TOsfInfoCommand.EmitJson(const AFile: string; AFiler: TOSFFile;
  AFirstNs, ALastNs: Int64);
var
  Root: TJSONObject;
  VersionStr: string;
begin
  case AFiler.Version of
    osvOSF4: VersionStr := 'OSF4';
    osvOSF5: VersionStr := 'OSF5';
  else
    VersionStr := 'unknown';
  end;

  Root := TJSONObject.Create;
  try
    Root.AddPair('file', TPath.GetFileName(AFile));
    Root.AddPair('size', TJSONNumber.Create(TFile.GetSize(AFile)));
    Root.AddPair('version', VersionStr);
    if AFiler.Metadata.Creator <> '' then
      Root.AddPair('creator', AFiler.Metadata.Creator);
    if AFiler.Metadata.CreatedUtc <> 0 then
      Root.AddPair('created_utc', FormatUtc(AFiler.Metadata.CreatedUtc));
    if AFiler.Metadata.Tag <> '' then
      Root.AddPair('tag', AFiler.Metadata.Tag);
    if AFiler.Metadata.Reason <> '' then
      Root.AddPair('reason', AFiler.Metadata.Reason);
    if AFiler.Metadata.Comment <> '' then
      Root.AddPair('comment', AFiler.Metadata.Comment);
    Root.AddPair('channel_count', TJSONNumber.Create(AFiler.ChannelCount));
    Root.AddPair('first_data_ns', TJSONNumber.Create(AFirstNs));
    Root.AddPair('last_data_ns',  TJSONNumber.Create(ALastNs));
    Root.AddPair('first_data_utc', FormatUtc(UnixNsToUtcDateTime(AFirstNs)));
    Root.AddPair('last_data_utc',  FormatUtc(UnixNsToUtcDateTime(ALastNs)));
    PrintJson(Root.Format(2));
  finally
    Root.Free;
  end;
end;

function TOsfInfoCommand.DoExecute: Integer;
var
  Positionals: TArray<string>;
  FileName: string;
  Filer: TOSFFile;
  FirstNs, LastNs: Int64;
begin
  Positionals := PositionalArgs([]);
  if Length(Positionals) < 1 then
  begin
    PrintErr(SInfoErrExpectFile);
    Exit(EXIT_BAD_ARGS);
  end;
  FileName := Positionals[0];
  if not TFile.Exists(FileName) then
  begin
    PrintErrf(SInfoErrFileNotFound, [FileName]);
    Exit(EXIT_NOT_FOUND);
  end;

  Filer := TOSFFile.Create;
  try
    try
      Filer.OpenForRead(FileName);
    except
      on E: Exception do
      begin
        PrintErrf(SInfoErrOpenFailed, [FileName, E.Message]);
        Exit(EXIT_FORMAT_ERROR);
      end;
    end;

    ResolveTimeRange(FileName, Filer, FirstNs, LastNs);

    if FJson then
      EmitJson(FileName, Filer, FirstNs, LastNs)
    else
      PrintHuman(FileName, Filer, FirstNs, LastNs);
  finally
    Filer.Free;
  end;
  Result := EXIT_OK;
end;

end.
