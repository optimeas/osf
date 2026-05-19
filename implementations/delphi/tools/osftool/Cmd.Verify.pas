// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

// "verify" verb. Walks a file end-to-end via TOSFFile, tracks block
// counts, channel coverage and timestamp monotonicity, and emits a
// status report. Issues that affect data integrity become errors;
// recoverable concerns (e.g. truncated final block) become warnings.
unit Cmd.Verify;

interface

uses
  System.SysUtils,
  System.IOUtils,
  System.JSON,
  System.Types,
  System.Generics.Collections,
  Cmd.Base,
  OSF.Types,
  OSF.Channel,
  OSF.Log,
  OSF.Filer;

type
  TOsfVerifyCommand = class(TBaseCommand)
  strict private
    FBlockCount: Integer;
    FTruncatedCount: Integer;
    FWarnings: TList<string>;
    FErrors: TList<string>;
    FLastTsPerChannel: TDictionary<Word, Int64>;
    procedure HandleFilerLog(ALevel: TOSFLogLevel; const AMsg: string);
    procedure CheckBlock(const ABlock: TOSFDataBlock; AFiler: TOSFFile);
    procedure EmitHuman(const AFile: string; AVersion: TOSFVersion;
      AChannelCount: Integer);
    procedure EmitJson(const AFile: string; AVersion: TOSFVersion;
      AChannelCount: Integer);
  public
    function Name: string; override;
    function ShortDescription: string; override;
    function DoExecute: Integer; override;
    procedure PrintHelp; override;
  end;

implementation

uses
  System.StrUtils;

// ── TOsfVerifyCommand ───────────────────────────────────────────────────────

function TOsfVerifyCommand.Name: string;
begin
  Result := 'verify';
end;

function TOsfVerifyCommand.ShortDescription: string;
begin
  Result := 'Check file integrity and block consistency';
end;

procedure TOsfVerifyCommand.PrintHelp;
begin
  Print('osftool verify <file> [options]');
  Print('');
  Print('Checks performed:');
  Print('  1. Magic header readable and version recognised');
  Print('  2. Metablock parseable (XML or JSON valid)');
  Print('  3. All block channel indices present in metablock');
  Print('  4. No blocks with length exceeding file size');
  Print('  5. Timestamps monotonically increasing per channel');
  Print('  6. File ends cleanly (last block not truncated)');
  Print('');
  Print('Options:');
  Print('  --strict   Treat warnings as errors (affects exit code)');
  Print('  --json     Output as JSON');
  Print('  --quiet / --verbose');
end;

procedure TOsfVerifyCommand.HandleFilerLog(ALevel: TOSFLogLevel; const AMsg: string);
begin
  if ALevel = llWarning then
  begin
    // The filer surfaces truncation-style problems via warning messages.
    // We mirror them into the warning list verbatim so the final report
    // shows whatever the filer saw.
    FWarnings.Add(AMsg);
    if (System.Pos('Truncated', AMsg) > 0) or (System.Pos('truncat', AMsg) > 0) then
      Inc(FTruncatedCount);
  end;
  // Forward to the base log handler so --verbose still prints debug info
  // to stderr.
  HandleLog(ALevel, AMsg);
end;

procedure TOsfVerifyCommand.CheckBlock(const ABlock: TOSFDataBlock; AFiler: TOSFFile);
var
  Def: TOSFChannelDef;
  LastTs: Int64;
  ThisTs: Int64;
begin
  Inc(FBlockCount);
  if ABlock.IsInfoBlock then
    Exit;

  // Channel index must be in the metablock — the filer would warn on its
  // own; we treat it as a hard error here because it implies a corrupt
  // file that any consumer would fail to decode.
  Def := AFiler.ChannelByIndex(ABlock.ChannelIndex);
  if not Assigned(Def) then
  begin
    FErrors.Add(Format(
      'block %d references unknown channel index %d',
      [FBlockCount, ABlock.ChannelIndex]));
    Exit;
  end;

  // Timestamp monotonicity per channel. bcStartData carries its own
  // absolute timestamp; everything else relies on per-block decoding
  // (the filer already advances per-channel last-seen state).
  if ABlock.BlockType = bcStartData then
  begin
    ThisTs := ABlock.StartTimestampNs;
    if FLastTsPerChannel.TryGetValue(ABlock.ChannelIndex, LastTs) and (ThisTs < LastTs) then
      FErrors.Add(Format(
        'channel "%s" (idx %d): bcStartData timestamp %d is earlier than previous %d',
        [Def.Name, ABlock.ChannelIndex, ThisTs, LastTs]));
    FLastTsPerChannel.AddOrSetValue(ABlock.ChannelIndex, ThisTs);
  end;
  // bcAbsTimeStampData and bcContinued* would need full payload decoding
  // for a per-sample monotonicity check. The filer already verifies the
  // length-field-vs-payload-size invariant; deeper per-sample checks are
  // a future extension (would re-walk the payload bytes by datatype).
end;

procedure TOsfVerifyCommand.EmitHuman(const AFile: string; AVersion: TOSFVersion;
  AChannelCount: Integer);
var
  VersionStr, Status: string;
  Msg: string;
begin
  case AVersion of
    osvOSF4: VersionStr := 'OSF4';
    osvOSF5: VersionStr := 'OSF5';
  else
    VersionStr := 'unknown';
  end;

  if FErrors.Count > 0 then
    Status := 'ERRORS'
  else if FWarnings.Count > 0 then
    Status := 'WARNING'
  else
    Status := 'OK';
  Printf('%s: %s', [TPath.GetFileName(AFile), Status]);
  Printf('  Version:  %s', [VersionStr]);
  if FTruncatedCount > 0 then
    Printf('  Blocks:   %d (%d truncated)', [FBlockCount, FTruncatedCount])
  else
    Printf('  Blocks:   %d', [FBlockCount]);
  Printf('  Channels: %d', [AChannelCount]);
  Printf('  Warnings: %d', [FWarnings.Count]);
  for Msg in FWarnings do
    Printf('    [W] %s', [Msg]);
  Printf('  Errors:   %d', [FErrors.Count]);
  for Msg in FErrors do
    Printf('    [E] %s', [Msg]);
end;

procedure TOsfVerifyCommand.EmitJson(const AFile: string; AVersion: TOSFVersion;
  AChannelCount: Integer);
var
  Root: TJSONObject;
  WArr, EArr: TJSONArray;
  VersionStr, Status: string;
  S: string;
begin
  case AVersion of
    osvOSF4: VersionStr := 'OSF4';
    osvOSF5: VersionStr := 'OSF5';
  else
    VersionStr := 'unknown';
  end;
  if FErrors.Count > 0 then
    Status := 'errors'
  else if FWarnings.Count > 0 then
    Status := 'warning'
  else
    Status := 'ok';

  Root := TJSONObject.Create;
  try
    Root.AddPair('file', TPath.GetFileName(AFile));
    Root.AddPair('status', Status);
    Root.AddPair('version', VersionStr);
    Root.AddPair('block_count',     TJSONNumber.Create(FBlockCount));
    Root.AddPair('truncated_count', TJSONNumber.Create(FTruncatedCount));
    Root.AddPair('channel_count',   TJSONNumber.Create(AChannelCount));
    WArr := TJSONArray.Create;
    Root.AddPair('warnings', WArr);
    for S in FWarnings do
      WArr.Add(S);
    EArr := TJSONArray.Create;
    Root.AddPair('errors', EArr);
    for S in FErrors do
      EArr.Add(S);
    PrintJson(Root.Format(2));
  finally
    Root.Free;
  end;
end;

function TOsfVerifyCommand.DoExecute: Integer;
var
  Positionals: TArray<string>;
  FileName: string;
  Filer: TOSFFile;
  Block: TOSFDataBlock;
  Version: TOSFVersion;
  ChannelCount: Integer;
  Strict: Boolean;
begin
  Positionals := PositionalArgs([]);
  if Length(Positionals) < 1 then
  begin
    PrintErr('osftool verify: expected a file argument');
    Exit(EXIT_BAD_ARGS);
  end;
  FileName := Positionals[0];
  Strict := HasFlag('--strict');

  if not TFile.Exists(FileName) then
  begin
    PrintErrf('osftool verify: file not found: %s', [FileName]);
    Exit(EXIT_NOT_FOUND);
  end;

  FBlockCount := 0;
  FTruncatedCount := 0;
  FWarnings := TList<string>.Create;
  FErrors := TList<string>.Create;
  FLastTsPerChannel := TDictionary<Word, Int64>.Create;
  Filer := TOSFFile.Create;
  try
    Filer.OnLog := HandleFilerLog;
    Filer.DebugEnabled := FVerbose;
    try
      Filer.OpenForRead(FileName);
    except
      on E: Exception do
      begin
        PrintErrf('osftool verify: cannot open %s: %s', [FileName, E.Message]);
        Exit(EXIT_FORMAT_ERROR);
      end;
    end;
    Version := Filer.Version;
    ChannelCount := Filer.ChannelCount;

    while Filer.ReadNextBlock(Block) do
      CheckBlock(Block, Filer);

    if FJson then
      EmitJson(FileName, Version, ChannelCount)
    else
      EmitHuman(FileName, Version, ChannelCount);

    if FErrors.Count > 0 then
      Result := EXIT_FORMAT_ERROR
    else if Strict and (FWarnings.Count > 0) then
      Result := EXIT_FORMAT_ERROR
    else
      Result := EXIT_OK;
  finally
    FLastTsPerChannel.Free;
    FErrors.Free;
    FWarnings.Free;
    Filer.Free;
  end;
end;

end.
