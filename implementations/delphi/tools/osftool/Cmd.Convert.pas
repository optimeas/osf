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

// ── TOsfConvertCommand ──────────────────────────────────────────────────────

function TOsfConvertCommand.Name: string;
begin
  Result := 'convert';
end;

function TOsfConvertCommand.ShortDescription: string;
begin
  Result := 'Convert between OSF4 and OSF5';
end;

procedure TOsfConvertCommand.PrintHelp;
begin
  Print('osftool convert <inputfile> <outputfile> [options]');
  Print('');
  Print('Arguments:');
  Print('  inputfile    Source .osf or .osfz');
  Print('  outputfile   Output .osf');
  Print('');
  Print('Options:');
  Print('  --osf4    Write as OSF4 (default: from config "output.format")');
  Print('  --osf5    Write as OSF5 (explicit; default in fresh installs)');
  Print('  --json');
  Print('  --quiet / --verbose');
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
    PrintErr('osftool convert: expected <inputfile> <outputfile>');
    Exit(EXIT_BAD_ARGS);
  end;
  InputFile := Positionals[0];
  OutputFile := Positionals[1];

  if not TFile.Exists(InputFile) then
  begin
    PrintErrf('osftool convert: input file not found: %s', [InputFile]);
    Exit(EXIT_NOT_FOUND);
  end;

  if HasFlag('--osf4') and HasFlag('--osf5') then
  begin
    PrintErr('osftool convert: --osf4 and --osf5 are mutually exclusive');
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
      Printf('Reading: %s  (OSF4)', [TPath.GetFileName(InputFile)])
    else if SourceVersion = osvOSF5 then
      Printf('Reading: %s  (OSF5)', [TPath.GetFileName(InputFile)])
    else
      Printf('Reading: %s  (version unknown)', [TPath.GetFileName(InputFile)]);
    if TargetVersion = osvOSF4 then
      Printf('Writing: %s  (OSF4)', [TPath.GetFileName(OutputFile)])
    else
      Printf('Writing: %s  (OSF5)', [TPath.GetFileName(OutputFile)]);
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
        PrintErrf('osftool convert: failed: %s', [E.Message]);
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
    Printf('Done. Written: %d bytes.', [OutSize]);
  Result := EXIT_OK;
end;

end.
