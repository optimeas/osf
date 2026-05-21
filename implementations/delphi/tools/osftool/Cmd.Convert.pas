// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

// "convert" verb. Round-trips a single OSF / OSFZ file through
// TOSFMerger so the writer produces the target format (OSF4 or OSF5).
// The merger always emits bcAbsTimeStampData regardless of source
// shape; for equidistant channels this trades some on-disk compactness
// for format-correctness across versions.
unit Cmd.Convert;

interface

uses
  System.SysUtils,
  System.IOUtils,
  System.JSON,
  Cmd.Base,
  OSF.Types,
  OSF.Log,
  OSF.Filer,
  OSF.Merger,
  OsfToolConfig;

type
  TOsfConvertCommand = class(TBaseCommand)
  public
    function Name: string; override;
    function ShortDescription: string; override;
    function DoExecute: Integer; override;
    procedure PrintHelp; override;
  end;

implementation

uses
  System.StrUtils,
  System.DateUtils;

resourcestring
  SConvertDesc = 'Convert between OSF4 and OSF5';
  SConvertHelp =
    'osftool convert <inputfile> <outputfile> [options]' + sLineBreak +
    sLineBreak +
    'Arguments:' + sLineBreak +
    '  inputfile    Source .osf or .osfz' + sLineBreak +
    '  outputfile   Output .osf' + sLineBreak +
    sLineBreak +
    'Options:' + sLineBreak +
    '  --osf4    Write as OSF4 (default: from config "output.format")' + sLineBreak +
    '  --osf5    Write as OSF5 (explicit; default in fresh installs)' + sLineBreak +
    '  --json' + sLineBreak +
    '  --quiet / --verbose';
  SConvertErrExpectArgs        = 'osftool convert: expected <inputfile> <outputfile>';
  SConvertErrInputNotFound     = 'osftool convert: input file not found: %s';
  SConvertErrOsfFlagsExclusive = 'osftool convert: --osf4 and --osf5 are mutually exclusive';
  SConvertErrFailed            = 'osftool convert: failed: %s';
  SConvertReadingOsf4    = 'Reading: %s  (OSF4)';
  SConvertReadingOsf5    = 'Reading: %s  (OSF5)';
  SConvertReadingUnknown = 'Reading: %s  (version unknown)';
  SConvertWritingOsf4    = 'Writing: %s  (OSF4)';
  SConvertWritingOsf5    = 'Writing: %s  (OSF5)';
  SConvertDone           = 'Done. Written: %d bytes.';

// ── TOsfConvertCommand ──────────────────────────────────────────────────────

function TOsfConvertCommand.Name: string;
begin
  Result := 'convert';
end;

function TOsfConvertCommand.ShortDescription: string;
begin
  Result := SConvertDesc;
end;

procedure TOsfConvertCommand.PrintHelp;
begin
  Print(SConvertHelp);
end;

function PeekSourceVersion(const AFile: string): TOSFVersion;
var
  Filer: TOSFFile;
begin
  Result := osvUnknown;
  Filer := TOSFFile.Create;
  try
    try
      Filer.OpenForRead(AFile);
      Result := Filer.Version;
    except
      // Best-effort: an unreadable file just leaves the result as
      // osvUnknown so the caller can produce a meaningful message.
    end;
  finally
    Filer.Free;
  end;
end;

function TOsfConvertCommand.DoExecute: Integer;
var
  Positionals: TArray<string>;
  InputFile, OutputFile: string;
  TargetVersion: TOSFVersion;
  SourceVersion: TOSFVersion;
  Merger: TOSFMerger;
  Cfg: TOsfToolConfig;
  OutSize: Int64;
  ResultObj: TJSONObject;
begin
  Positionals := PositionalArgs([]);
  if Length(Positionals) < 2 then
  begin
    PrintErr(SConvertErrExpectArgs);
    Exit(EXIT_BAD_ARGS);
  end;
  InputFile := Positionals[0];
  OutputFile := Positionals[1];

  if not TFile.Exists(InputFile) then
  begin
    PrintErrf(SConvertErrInputNotFound, [InputFile]);
    Exit(EXIT_NOT_FOUND);
  end;

  if HasFlag('--osf4') and HasFlag('--osf5') then
  begin
    PrintErr(SConvertErrOsfFlagsExclusive);
    Exit(EXIT_BAD_ARGS);
  end;

  if HasFlag('--osf4') then
    TargetVersion := osvOSF4
  else if HasFlag('--osf5') then
    TargetVersion := osvOSF5
  else
  begin
    Cfg := TOsfToolConfig.Create;
    try
      Cfg.Load;
      if SameText(Cfg.Get(C_KEY_OUTPUT_FORMAT), 'osf4') then
        TargetVersion := osvOSF4
      else
        TargetVersion := osvOSF5;
    finally
      Cfg.Free;
    end;
  end;

  SourceVersion := PeekSourceVersion(InputFile);
  if not FJson then
  begin
    if SourceVersion = osvOSF4 then
      Printf(SConvertReadingOsf4, [TPath.GetFileName(InputFile)])
    else if SourceVersion = osvOSF5 then
      Printf(SConvertReadingOsf5, [TPath.GetFileName(InputFile)])
    else
      Printf(SConvertReadingUnknown, [TPath.GetFileName(InputFile)]);
    if TargetVersion = osvOSF4 then
      Printf(SConvertWritingOsf4, [TPath.GetFileName(OutputFile)])
    else
      Printf(SConvertWritingOsf5, [TPath.GetFileName(OutputFile)]);
  end;

  Merger := TOSFMerger.Create;
  try
    Merger.OnLog := HandleLog;
    Merger.DebugEnabled := FVerbose;
    Merger.FileList := [InputFile];
    // Interval: 1970-01-01 .. 2200-01-01 covers every realistic sample.
    Merger.SetInterval(EncodeDate(1970, 1, 1), EncodeDate(2200, 1, 1));
    Merger.OutputVersion := TargetVersion;
    try
      Merger.SaveToFile(OutputFile);
    except
      on E: Exception do
      begin
        PrintErrf(SConvertErrFailed, [E.Message]);
        Exit(EXIT_IO_ERROR);
      end;
    end;
  finally
    Merger.Free;
  end;

  OutSize := 0;
  if TFile.Exists(OutputFile) then
    OutSize := TFile.GetSize(OutputFile);

  if FJson then
  begin
    ResultObj := TJSONObject.Create;
    try
      ResultObj.AddPair('status', 'ok');
      ResultObj.AddPair('input_file', InputFile);
      ResultObj.AddPair('output_file', OutputFile);
      if SourceVersion = osvOSF4 then
        ResultObj.AddPair('source_version', 'OSF4')
      else if SourceVersion = osvOSF5 then
        ResultObj.AddPair('source_version', 'OSF5');
      if TargetVersion = osvOSF4 then
        ResultObj.AddPair('target_version', 'OSF4')
      else
        ResultObj.AddPair('target_version', 'OSF5');
      ResultObj.AddPair('output_size', TJSONNumber.Create(OutSize));
      PrintJson(ResultObj.Format(2));
    finally
      ResultObj.Free;
    end;
  end
  else
    Printf(SConvertDone, [OutSize]);
  Result := EXIT_OK;
end;

end.
