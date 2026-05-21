// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

// "cache" verb. Front-end for OSF.Meta.Cache.TOSFMetaCacheBuilder.
// Subcommands: build, rebuild, clean, status. Operates on every
// .osf / .osfz file found recursively under a root directory.
unit Cmd.Cache;

interface

uses
  System.SysUtils,
  System.Classes,
  System.IOUtils,
  System.JSON,
  System.Generics.Collections,
  Cmd.Base,
  OSF.Log,
  OSF.Meta.Cache;

type
  TOsfCacheCommand = class(TBaseCommand)
  strict private
    function GatherOsfFiles(const ARoot: string; ARecursive: Boolean): TArray<string>;
    function RunBuild(const ARoot: string; AForce: Boolean): Integer;
    function RunClean(const ARoot: string): Integer;
    function RunStatus(const ARoot: string): Integer;
  public
    function Name: string; override;
    function ShortDescription: string; override;
    function DoExecute: Integer; override;
    procedure PrintHelp; override;
  end;

implementation

uses
  System.StrUtils;

resourcestring
  SCacheDesc = 'Manage .json sidecar cache files';
  SCacheHelp =
    'osftool cache <subcommand> <rootdir> [options]' + sLineBreak +
    sLineBreak +
    'Subcommands:' + sLineBreak +
    '  build    Build missing .json sidecars (skip if already valid)' + sLineBreak +
    '  rebuild  Force rebuild all .json sidecars' + sLineBreak +
    '  clean    Delete all .json sidecars under rootdir' + sLineBreak +
    '  status   Show which .osf/.osfz files have no valid sidecar' + sLineBreak +
    sLineBreak +
    'Options:' + sLineBreak +
    '  --recursive   Include subdirectories (default: true)' + sLineBreak +
    '  --no-recursive  Restrict to the root directory only' + sLineBreak +
    '  --json' + sLineBreak +
    '  --quiet / --verbose';
  SCacheErrExpectArgs  = 'osftool cache: expected <subcommand> <rootdir>';
  SCacheErrUnknownSub  = 'osftool cache: unknown subcommand "%s"';
  SCacheErrDirNotFound = 'osftool cache: directory not found: %s';
  SCacheScanning = 'Scanning %s ...';
  SCacheFound    = 'Found %d OSF files.';
  SCacheSkip     = '  [skip]  %s  (cache valid)';
  SCacheBuilt    = '  [build] %s  -> %d channels';
  SCacheFail     = '  [fail]  %s: %s';
  SCacheBuildSummary      = 'Built: %d new caches. Skipped: %d (already valid).%s';
  SCacheBuildFailedSuffix = ' Failed: %d.';
  SCacheRemoved      = '  removed: %s';
  SCacheRemoveFailed = '  failed:  %s: %s';
  SCacheCleanSummary = 'Removed %d cache files under %s.';
  SCacheMissing       = '  missing: %s';
  SCacheStatusSummary = '%d files: %d with valid cache, %d missing.';

// ── helpers ──────────────────────────────────────────────────────────────────

function IsOsfFile(const APath: string): Boolean;
var
  Ext: string;
begin
  Ext := LowerCase(TPath.GetExtension(APath));
  Result := (Ext = '.osf') or (Ext = '.osfz');
end;

// ── TOsfCacheCommand ────────────────────────────────────────────────────────

function TOsfCacheCommand.Name: string;
begin
  Result := 'cache';
end;

function TOsfCacheCommand.ShortDescription: string;
begin
  Result := SCacheDesc;
end;

procedure TOsfCacheCommand.PrintHelp;
begin
  Print(SCacheHelp);
end;

function TOsfCacheCommand.GatherOsfFiles(const ARoot: string; ARecursive: Boolean): TArray<string>;
var
  Mode: TSearchOption;
  Found: TStringList;
  S: string;
begin
  Found := TStringList.Create;
  try
    Found.Sorted := True;
    Found.Duplicates := dupIgnore;
    if ARecursive then
      Mode := TSearchOption.soAllDirectories
    else
      Mode := TSearchOption.soTopDirectoryOnly;
    if TDirectory.Exists(ARoot) then
    begin
      for S in TDirectory.GetFiles(ARoot, '*.osf', Mode) do Found.Add(S);
      for S in TDirectory.GetFiles(ARoot, '*.osfz', Mode) do Found.Add(S);
    end;
    Result := Found.ToStringArray;
  finally
    Found.Free;
  end;
end;

function TOsfCacheCommand.RunBuild(const ARoot: string; AForce: Boolean): Integer;
var
  Files: TArray<string>;
  Builder: TOSFMetaCacheBuilder;
  Cache: TOSFMetaCache;
  CachePath, F: string;
  Recursive: Boolean;
  Built, Skipped, FailedCount: Integer;
  Root: TJSONObject;
  Arr: TJSONArray;
  Item: TJSONObject;
  Status: string;
begin
  Recursive := not HasFlag('--no-recursive');
  Files := GatherOsfFiles(ARoot, Recursive);
  Print(Format(SCacheScanning, [ARoot]));
  Print(Format(SCacheFound, [Length(Files)]));

  Built := 0;
  Skipped := 0;
  FailedCount := 0;
  Root := nil;
  Arr := nil;
  if FJson then
  begin
    Root := TJSONObject.Create;
    Arr := TJSONArray.Create;
    Root.AddPair('files', Arr);
  end;
  Builder := TOSFMetaCacheBuilder.Create;
  try
    Builder.OnLog := HandleLog;
    Builder.DebugEnabled := FVerbose;
    for F in Files do
    begin
      CachePath := TOSFMetaCache.CachePathFor(F);
      if (not AForce) and TOSFMetaCache.IsValid(F) then
      begin
        Inc(Skipped);
        if not FJson then
          Printf(SCacheSkip, [TPath.GetFileName(F)]);
        Status := 'skipped';
      end
      else
      try
        Cache := Builder.BuildFromFile(F);
        try
          Cache.SaveToFile(CachePath);
          Inc(Built);
          if not FJson then
            Printf(SCacheBuilt,
              [TPath.GetFileName(F), Length(Cache.Channels)]);
          Status := 'built';
        finally
          Cache.Free;
        end;
      except
        on E: Exception do
        begin
          Inc(FailedCount);
          PrintErrf(SCacheFail, [TPath.GetFileName(F), E.Message]);
          Status := 'failed';
        end;
      end;
      if FJson then
      begin
        Item := TJSONObject.Create;
        Arr.AddElement(Item);
        Item.AddPair('file', F);
        Item.AddPair('status', Status);
      end;
    end;

    if FJson then
    begin
      Root.AddPair('built', TJSONNumber.Create(Built));
      Root.AddPair('skipped', TJSONNumber.Create(Skipped));
      Root.AddPair('failed', TJSONNumber.Create(FailedCount));
      PrintJson(Root.Format(2));
    end
    else
    begin
      Printf(SCacheBuildSummary,
        [Built, Skipped, IfThen(FailedCount > 0, Format(SCacheBuildFailedSuffix, [FailedCount]), '')]);
    end;
  finally
    if FJson then
      Root.Free;
    Builder.Free;
  end;
  if FailedCount > 0 then
    Result := EXIT_IO_ERROR
  else
    Result := EXIT_OK;
end;

function TOsfCacheCommand.RunClean(const ARoot: string): Integer;
var
  Files: TArray<string>;
  F, CachePath: string;
  Recursive: Boolean;
  Removed: Integer;
begin
  Recursive := not HasFlag('--no-recursive');
  Files := GatherOsfFiles(ARoot, Recursive);
  Removed := 0;
  for F in Files do
  begin
    CachePath := TOSFMetaCache.CachePathFor(F);
    if TFile.Exists(CachePath) then
    try
      TFile.Delete(CachePath);
      Inc(Removed);
      if not FJson then
        Printf(SCacheRemoved, [TPath.GetFileName(CachePath)]);
    except
      on E: Exception do
        PrintErrf(SCacheRemoveFailed, [CachePath, E.Message]);
    end;
  end;
  if FJson then
    PrintJson(Format('{"removed": %d}', [Removed]))
  else
    Printf(SCacheCleanSummary, [Removed, ARoot]);
  Result := EXIT_OK;
end;

function TOsfCacheCommand.RunStatus(const ARoot: string): Integer;
var
  Files: TArray<string>;
  F: string;
  Recursive: Boolean;
  Valid, Missing: Integer;
  Root: TJSONObject;
  Arr: TJSONArray;
  Item: TJSONObject;
begin
  Recursive := not HasFlag('--no-recursive');
  Files := GatherOsfFiles(ARoot, Recursive);
  Valid := 0;
  Missing := 0;
  Root := nil;
  Arr := nil;
  if FJson then
  begin
    Root := TJSONObject.Create;
    Arr := TJSONArray.Create;
    Root.AddPair('files', Arr);
  end;

  for F in Files do
  begin
    if TOSFMetaCache.IsValid(F) then
    begin
      Inc(Valid);
      if FJson then
      begin
        Item := TJSONObject.Create;
        Arr.AddElement(Item);
        Item.AddPair('file', F);
        Item.AddPair('cache_valid', TJSONBool.Create(True));
      end;
    end
    else
    begin
      Inc(Missing);
      if not FJson then
        Printf(SCacheMissing, [F]);
      if FJson then
      begin
        Item := TJSONObject.Create;
        Arr.AddElement(Item);
        Item.AddPair('file', F);
        Item.AddPair('cache_valid', TJSONBool.Create(False));
      end;
    end;
  end;

  if FJson then
  begin
    Root.AddPair('valid', TJSONNumber.Create(Valid));
    Root.AddPair('missing', TJSONNumber.Create(Missing));
    try
      PrintJson(Root.Format(2));
    finally
      Root.Free;
    end;
  end
  else
    Printf(SCacheStatusSummary,
      [Length(Files), Valid, Missing]);
  Result := EXIT_OK;
end;

function TOsfCacheCommand.DoExecute: Integer;
var
  Positionals: TArray<string>;
  Sub, Root: string;
begin
  Positionals := PositionalArgs([]);
  if Length(Positionals) < 2 then
  begin
    PrintErr(SCacheErrExpectArgs);
    Exit(EXIT_BAD_ARGS);
  end;
  Sub := LowerCase(Positionals[0]);
  Root := Positionals[1];
  if (Sub <> 'build') and (Sub <> 'rebuild') and (Sub <> 'clean') and (Sub <> 'status') then
  begin
    PrintErrf(SCacheErrUnknownSub, [Sub]);
    Exit(EXIT_BAD_ARGS);
  end;
  if not TDirectory.Exists(Root) then
  begin
    PrintErrf(SCacheErrDirNotFound, [Root]);
    Exit(EXIT_NOT_FOUND);
  end;

  if Sub = 'build' then
    Result := RunBuild(Root, False)
  else if Sub = 'rebuild' then
    Result := RunBuild(Root, True)
  else if Sub = 'clean' then
    Result := RunClean(Root)
  else
    Result := RunStatus(Root);
end;

end.
