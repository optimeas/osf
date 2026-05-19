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
  Result := 'Manage .json sidecar cache files';
end;

procedure TOsfCacheCommand.PrintHelp;
begin
  Print('osftool cache <subcommand> <rootdir> [options]');
  Print('');
  Print('Subcommands:');
  Print('  build    Build missing .json sidecars (skip if already valid)');
  Print('  rebuild  Force rebuild all .json sidecars');
  Print('  clean    Delete all .json sidecars under rootdir');
  Print('  status   Show which .osf/.osfz files have no valid sidecar');
  Print('');
  Print('Options:');
  Print('  --recursive   Include subdirectories (default: true)');
  Print('  --no-recursive  Restrict to the root directory only');
  Print('  --json');
  Print('  --quiet / --verbose');
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
  Print(Format('Scanning %s ...', [ARoot]));
  Print(Format('Found %d OSF files.', [Length(Files)]));

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
          Printf('  [skip]  %s  (cache valid)', [TPath.GetFileName(F)]);
        Status := 'skipped';
      end
      else
      try
        Cache := Builder.BuildFromFile(F);
        try
          Cache.SaveToFile(CachePath);
          Inc(Built);
          if not FJson then
            Printf('  [build] %s  -> %d channels',
              [TPath.GetFileName(F), Length(Cache.Channels)]);
          Status := 'built';
        finally
          Cache.Free;
        end;
      except
        on E: Exception do
        begin
          Inc(FailedCount);
          PrintErrf('  [fail]  %s: %s', [TPath.GetFileName(F), E.Message]);
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
      Printf('Built: %d new caches. Skipped: %d (already valid).%s',
        [Built, Skipped, IfThen(FailedCount > 0, Format(' Failed: %d.', [FailedCount]), '')]);
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
        Printf('  removed: %s', [TPath.GetFileName(CachePath)]);
    except
      on E: Exception do
        PrintErrf('  failed:  %s: %s', [CachePath, E.Message]);
    end;
  end;
  if FJson then
    PrintJson(Format('{"removed": %d}', [Removed]))
  else
    Printf('Removed %d cache files under %s.', [Removed, ARoot]);
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
        Printf('  missing: %s', [F]);
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
    Printf('%d files: %d with valid cache, %d missing.',
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
    PrintErr('osftool cache: expected <subcommand> <rootdir>');
    Exit(EXIT_BAD_ARGS);
  end;
  Sub := LowerCase(Positionals[0]);
  Root := Positionals[1];
  if (Sub <> 'build') and (Sub <> 'rebuild') and (Sub <> 'clean') and (Sub <> 'status') then
  begin
    PrintErrf('osftool cache: unknown subcommand "%s"', [Sub]);
    Exit(EXIT_BAD_ARGS);
  end;
  if not TDirectory.Exists(Root) then
  begin
    PrintErrf('osftool cache: directory not found: %s', [Root]);
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
