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
    FIntegrity: TOSFIntegrityProfile;
    FVerifyStatus: string;
    FCRCFailed: UInt32;
    FSigSkipped: UInt32;
    FUnknownSkipped: UInt32;
    FZeroLengthSkipped: UInt32;
    procedure CheckBlock(const ABlock: TOSFDataBlock; AFiler: TOSFFile);
  protected
    // Capture filer warnings into FWarnings before falling through to
    // the base implementation (which writes them to stderr / JSON / log).
    procedure OnLogMessage(const AMsg: string; ALevel: TOSFLogLevel;
                           const ASender: string); override;
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

resourcestring
  SVerifyDesc = 'Check file integrity and block consistency';
  SVerifyHelp =
    'osftool verify <file> [options]' + sLineBreak +
    sLineBreak +
    'Checks performed:' + sLineBreak +
    '  1. Magic header readable and version recognised' + sLineBreak +
    '  2. Metablock parseable (XML or JSON valid)' + sLineBreak +
    '  3. All block channel indices present in metablock' + sLineBreak +
    '  4. No blocks with length exceeding file size' + sLineBreak +
    '  5. Timestamps monotonically increasing per channel' + sLineBreak +
    '  6. File ends cleanly (last block not truncated)' + sLineBreak +
    sLineBreak +
    'Options:' + sLineBreak +
    '  --strict   Treat warnings as errors (affects exit code)' + sLineBreak +
    '  --json     Output as JSON' + sLineBreak +
    '  --quiet / --verbose';
  SVerifyErrExpectFile        = 'osftool verify: expected a file argument';
  SVerifyErrFileNotFound      = 'osftool verify: file not found: %s';
  SVerifyErrCannotOpen        = 'osftool verify: cannot open %s: %s';
  SVerifyErrUnknownChannelIdx = 'block %d references unknown channel index %d';
  SVerifyErrFrameCRC = '%d block(s) failed their frame CRC (data invalid)';
  SVerifyWarnZeroLength = '%d block(s) had a zero-length field and were skipped ' +
    '(OSF-UP3: non-conforming writer artefact)';
  SVerifyLineIntegrity     = '  Integrity: %s (%s)';
  SVerifyLineCRCFailed     = '  CRC-failed blocks:  %d';
  SVerifyLineSigSkipped    = '  Signature blocks:   %d (skipped, unverified)';
  SVerifyLineUnknownSkip   = '  Unknown-type skips: %d';
  SVerifyLineZeroLenSkip   = '  Zero-length skips:  %d';
  SVerifyErrTimestampBackward =
    'channel "%s" (idx %d): bcStartData timestamp %d is earlier than previous %d';
  SVerifyVersionUnknown = 'unknown';
  SVerifyStatusOk      = 'OK';
  SVerifyStatusWarning = 'WARNING';
  SVerifyStatusErrors  = 'ERRORS';
  SVerifyLineVersion         = '  Version:  %s';
  SVerifyLineBlocks          = '  Blocks:   %d';
  SVerifyLineBlocksTruncated = '  Blocks:   %d (%d truncated)';
  SVerifyLineChannels        = '  Channels: %d';
  SVerifyLineWarnings        = '  Warnings: %d';
  SVerifyLineErrors          = '  Errors:   %d';

// ── TOsfVerifyCommand ───────────────────────────────────────────────────────

function TOsfVerifyCommand.Name: string;
begin
  Result := 'verify';
end;

function TOsfVerifyCommand.ShortDescription: string;
begin
  Result := SVerifyDesc;
end;

procedure TOsfVerifyCommand.PrintHelp;
begin
  Print(SVerifyHelp);
end;

procedure TOsfVerifyCommand.OnLogMessage(const AMsg: string;
  ALevel: TOSFLogLevel; const ASender: string);
begin
  if (ALevel = llWarning) and (FWarnings <> nil) then
    // The filer surfaces truncation-style problems via warning messages.
    // We mirror them into the warning list verbatim so the final report
    // shows whatever the filer saw. The TOSFFile.TruncationSeen flag is
    // the authoritative truncation signal and is checked separately.
    FWarnings.Add(AMsg);
  // Always forward to the base callback so the message reaches stderr,
  // the JSON stream, or the log file as appropriate.
  inherited;
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
    FErrors.Add(Format(SVerifyErrUnknownChannelIdx,
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
      FErrors.Add(Format(SVerifyErrTimestampBackward,
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
    VersionStr := SVerifyVersionUnknown;
  end;

  if FErrors.Count > 0 then
    Status := SVerifyStatusErrors
  else if FWarnings.Count > 0 then
    Status := SVerifyStatusWarning
  else
    Status := SVerifyStatusOk;
  Printf('%s: %s', [TPath.GetFileName(AFile), Status]);
  Printf(SVerifyLineVersion, [VersionStr]);
  if FTruncatedCount > 0 then
    Printf(SVerifyLineBlocksTruncated, [FBlockCount, FTruncatedCount])
  else
    Printf(SVerifyLineBlocks, [FBlockCount]);
  Printf(SVerifyLineChannels, [AChannelCount]);
  if FIntegrity <> ipNone then
  begin
    Printf(SVerifyLineIntegrity, [OSFIntegrityProfileName(FIntegrity), FVerifyStatus]);
    Printf(SVerifyLineCRCFailed, [FCRCFailed]);
    if FSigSkipped > 0 then
      Printf(SVerifyLineSigSkipped, [FSigSkipped]);
  end;
  if (FUnknownSkipped > 0) or (FZeroLengthSkipped > 0) then
  begin
    Printf(SVerifyLineUnknownSkip, [FUnknownSkipped]);
    Printf(SVerifyLineZeroLenSkip, [FZeroLengthSkipped]);
  end;
  Printf(SVerifyLineWarnings, [FWarnings.Count]);
  for Msg in FWarnings do
    Printf('    [W] %s', [Msg]);
  Printf(SVerifyLineErrors, [FErrors.Count]);
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
    Root.AddPair('integrity', OSFIntegrityProfileName(FIntegrity));
    Root.AddPair('verification_status', FVerifyStatus);
    Root.AddPair('crc_failed_count',        TJSONNumber.Create(FCRCFailed));
    Root.AddPair('signature_skipped_count', TJSONNumber.Create(FSigSkipped));
    Root.AddPair('unknown_type_skipped_count', TJSONNumber.Create(FUnknownSkipped));
    Root.AddPair('zero_length_skipped_count', TJSONNumber.Create(FZeroLengthSkipped));
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
    PrintErr(SVerifyErrExpectFile);
    Exit(EXIT_BAD_ARGS);
  end;
  FileName := Positionals[0];
  Strict := HasFlag('--strict');

  if not TFile.Exists(FileName) then
  begin
    PrintErrf(SVerifyErrFileNotFound, [FileName]);
    Exit(EXIT_NOT_FOUND);
  end;

  FBlockCount := 0;
  FTruncatedCount := 0;
  FWarnings := TList<string>.Create;
  FErrors := TList<string>.Create;
  FLastTsPerChannel := TDictionary<Word, Int64>.Create;
  Filer := TOSFFile.Create;
  try
    try
      Filer.OpenForRead(FileName);
    except
      on E: Exception do
      begin
        PrintErrf(SVerifyErrCannotOpen, [FileName, E.Message]);
        Exit(EXIT_FORMAT_ERROR);
      end;
    end;
    Version := Filer.Version;
    ChannelCount := Filer.ChannelCount;

    while Filer.ReadNextBlock(Block) do
      CheckBlock(Block, Filer);
    if Filer.TruncationSeen then
      Inc(FTruncatedCount);

    // Integrity profile results (metablock CRC was already verified during
    // OpenForRead; a mismatch would have raised above).
    FIntegrity := Filer.IntegrityProfile;
    FVerifyStatus := Filer.VerificationStatus;
    FCRCFailed := Filer.BlocksCRCFailed;
    FSigSkipped := Filer.BlocksSignatureSkipped;
    FUnknownSkipped := Filer.BlocksUnknownTypeSkipped;
    FZeroLengthSkipped := Filer.BlocksZeroLengthSkipped;
    if FCRCFailed > 0 then
      FErrors.Add(Format(SVerifyErrFrameCRC, [FCRCFailed]));
    if FZeroLengthSkipped > 0 then
      FWarnings.Add(Format(SVerifyWarnZeroLength, [FZeroLengthSkipped]));

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
