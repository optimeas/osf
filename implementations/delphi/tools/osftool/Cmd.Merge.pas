// Copyright 2026 Optimeas GmbH
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// "merge" verb. Replaces the standalone OsfMerge.dpr from the previous
// task. Thin wrapper around TOSFMerger; defaults pulled from
// TOsfToolConfig when the corresponding flag is not on the command line.
unit Cmd.Merge;

interface

uses
  System.SysUtils,
  System.IOUtils,
  System.DateUtils,
  System.JSON,
  Cmd.Base,
  OSF.Types,
  OSF.Log,
  OSF.Merger,
  OsfToolConfig;

type
  TOsfMergeCommand = class(TBaseCommand)
  public
    function Name: string; override;
    function ShortDescription: string; override;
    function DoExecute: Integer; override;
    procedure PrintHelp; override;
  end;

implementation

uses
  System.StrUtils;

// ── shared timestamp parsing ────────────────────────────────────────────────

function ParseIso8601(const AStr: string; out ADT: TDateTime): Boolean;
var
  Y, M, D, H, N, S: Word;
begin
  Result := False;
  ADT := 0;
  if Length(AStr) < 19 then
    Exit;
  try
    Y := StrToInt(Copy(AStr, 1, 4));
    M := StrToInt(Copy(AStr, 6, 2));
    D := StrToInt(Copy(AStr, 9, 2));
    H := StrToInt(Copy(AStr, 12, 2));
    N := StrToInt(Copy(AStr, 15, 2));
    S := StrToInt(Copy(AStr, 18, 2));
    ADT := EncodeDateTime(Y, M, D, H, N, S, 0);
    Result := True;
  except
    Result := False;
  end;
end;

// ── TOsfMergeCommand ────────────────────────────────────────────────────────

function TOsfMergeCommand.Name: string;
begin
  Result := 'merge';
end;

function TOsfMergeCommand.ShortDescription: string;
begin
  Result := 'Merge OSF files from a directory over a time interval';
end;

procedure TOsfMergeCommand.PrintHelp;
begin
  Print('osftool merge <rootdir> <outputfile> <starttime> <endtime> [channel ...] [options]');
  Print('');
  Print('Arguments:');
  Print('  rootdir      Root directory (recursive scan for .osf and .osfz)');
  Print('  outputfile   Output file path (.osf)');
  Print('  starttime    Interval start, ISO 8601: 2024-01-15T10:00:00');
  Print('  endtime      Interval end,   ISO 8601: 2024-01-15T12:00:00');
  Print('  channel      Optional channel names (omit for all)');
  Print('');
  Print('Options:');
  Print('  --osf4         Write OSF4 output (default: from config "output.format")');
  Print('  --overwrite    Overwrite overlapping timestamps (default: from config "output.overlap")');
  Print('  --no-cache     Do not read or write .json sidecar files');
  Print('  --json         Machine-readable summary on stdout');
  Print('  --quiet / --verbose');
end;

function TOsfMergeCommand.DoExecute: Integer;
var
  Positionals: TArray<string>;
  Root, OutputFile, StartStr, EndStr: string;
  StartUtc, EndUtc: TDateTime;
  Channels: TArray<string>;
  Merger: TOSFMerger;
  Entries: TArray<TOSFFileEntry>;
  Cfg: TOsfToolConfig;
  OutSize: Int64;
  I: Integer;
  Result_: TJSONObject;
begin
  Positionals := PositionalArgs([]);
  if Length(Positionals) < 4 then
  begin
    PrintErr('osftool merge: expected <rootdir> <outputfile> <starttime> <endtime>');
    Exit(EXIT_BAD_ARGS);
  end;
  Root := Positionals[0];
  OutputFile := Positionals[1];
  StartStr := Positionals[2];
  EndStr := Positionals[3];

  if not ParseIso8601(StartStr, StartUtc) then
  begin
    PrintErrf('osftool merge: invalid starttime: %s', [StartStr]);
    Exit(EXIT_BAD_ARGS);
  end;
  if not ParseIso8601(EndStr, EndUtc) then
  begin
    PrintErrf('osftool merge: invalid endtime: %s', [EndStr]);
    Exit(EXIT_BAD_ARGS);
  end;
  if not TDirectory.Exists(Root) then
  begin
    PrintErrf('osftool merge: rootdir not found: %s', [Root]);
    Exit(EXIT_NOT_FOUND);
  end;

  // Remaining positionals are channel filter entries.
  SetLength(Channels, Length(Positionals) - 4);
  for I := 4 to High(Positionals) do
    Channels[I - 4] := Positionals[I];

  Cfg := TOsfToolConfig.Create;
  Merger := TOSFMerger.Create;
  try
    Cfg.Load;
    Merger.OnLog := HandleLog;
    Merger.DebugEnabled := FVerbose;
    Merger.RootDirectory := Root;
    Merger.SetInterval(StartUtc, EndUtc);
    Merger.ChannelFilter := Channels;
    Merger.UseCache := not HasFlag('--no-cache');

    if HasFlag('--osf4') then
      Merger.OutputVersion := osvOSF4
    else if SameText(Cfg.Get(C_KEY_OUTPUT_FORMAT), 'osf4') then
      Merger.OutputVersion := osvOSF4
    else
      Merger.OutputVersion := osvOSF5;

    if HasFlag('--overwrite') then
      Merger.OverlapStrategy := osOverwrite
    else if SameText(Cfg.Get(C_KEY_OUTPUT_OVERLAP), 'overwrite') then
      Merger.OverlapStrategy := osOverwrite
    else
      Merger.OverlapStrategy := osSkip;

    if not FJson then
      Printf('Scanning %s ...', [Root]);
    try
      Entries := Merger.Scan;
    except
      on E: Exception do
      begin
        PrintErrf('osftool merge: scan failed: %s', [E.Message]);
        Exit(EXIT_IO_ERROR);
      end;
    end;
    if Length(Entries) = 0 then
    begin
      PrintErr('osftool merge: no files overlap the interval');
      Exit(EXIT_NOT_FOUND);
    end;
    if not FJson then
      Printf('Found %d files in interval. Merging ...', [Length(Entries)]);

    try
      Merger.SaveToFile(OutputFile);
    except
      on E: Exception do
      begin
        PrintErrf('osftool merge: write failed: %s', [E.Message]);
        Exit(EXIT_IO_ERROR);
      end;
    end;

    OutSize := 0;
    if TFile.Exists(OutputFile) then
      OutSize := TFile.GetSize(OutputFile);

    if FJson then
    begin
      Result_ := TJSONObject.Create;
      try
        Result_.AddPair('status', 'ok');
        Result_.AddPair('files_merged', TJSONNumber.Create(Length(Entries)));
        Result_.AddPair('output_file', OutputFile);
        Result_.AddPair('output_size', TJSONNumber.Create(OutSize));
        PrintJson(Result_.Format(2));
      finally
        Result_.Free;
      end;
    end
    else
      Printf('Written: %s (%d bytes)', [OutputFile, OutSize]);
  finally
    Merger.Free;
    Cfg.Free;
  end;
  Result := EXIT_OK;
end;

end.
