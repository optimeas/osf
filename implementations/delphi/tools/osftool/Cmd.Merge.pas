// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

// "merge" verb. Thin wrapper around TOSFMerger; defaults pulled from
// TOsfToolConfig when the corresponding flag is absent. Progress is
// rendered through an IProgressReporter chosen from the output flags
// (--quiet / --verbose / --json / --log) — see OSF.Progress.*.
unit Cmd.Merge;

interface

uses
  System.SysUtils,
  System.IOUtils,
  System.DateUtils,
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
  System.Diagnostics,
  OSF.Progress,
  OSF.Progress.Console,
  OSF.Progress.Quiet,
  OSF.Progress.Verbose,
  OSF.Progress.Json,
  OSF.Progress.Fallback,
  OSF.Progress.Live,
  OSF.Progress.LogFile;

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

// Picks the reporter implementation matching the output flags, optionally
// wrapped in the log-file decorator. Raises if the log file cannot be
// created.
function CreateReporter(AQuiet, AVerbose, AJson: Boolean;
  const ALogPath: string): IProgressReporter;
var
  Base: IProgressReporter;
begin
  if AJson then
    Base := TOSFJsonProgressReporter.Create
  else if AQuiet then
    Base := TOSFQuietProgressReporter.Create
  else if AVerbose then
    Base := TOSFVerboseProgressReporter.Create
  else if IsConsoleTty then
    Base := TOSFLiveProgressReporter.Create
  else
    Base := TOSFFallbackProgressReporter.Create;

  if ALogPath <> '' then
    Result := TOSFLogFileProgressReporter.Create(Base, ALogPath)
  else
    Result := Base;
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
  Print('osftool merge <input-dir> <output-file> [channel ...] [options]');
  Print('');
  Print('Arguments:');
  Print('  input-dir     Root directory (recursive scan for .osf and .osfz)');
  Print('  output-file   Output file path (.osf)');
  Print('  channel       Optional channel names (omit for all)');
  Print('');
  Print('Options:');
  Print('  --start <ts>      Interval start, ISO 8601 (default: 1970-01-01T00:00:00)');
  Print('  --end <ts>        Interval end,   ISO 8601 (default: current date and time)');
  Print('  --osf4            Write OSF4 output (default: from config "output.format")');
  Print('  --overwrite       Overwrite overlapping timestamps');
  Print('  --no-cache        Do not read or write .json sidecar files');
  Print('');
  Print('Output options:');
  Print('  -q, --quiet       Suppress live output; errors only on stderr');
  Print('  -v, --verbose     Print all log messages (no live progress bar)');
  Print('      --json        Emit a machine-readable JSON-Lines event stream');
  Print('      --log <path>  Write a full diagnostic log to file');
  Print('');
  Print('Examples:');
  Print('  osftool merge ./data merged.osf');
  Print('  osftool merge ./data merged.osf --log merge.log');
  Print('  osftool merge ./data merged.osf --json > merge-events.jsonl');
  Print('  osftool merge ./data merged.osf --verbose');
end;

function TOsfMergeCommand.DoExecute: Integer;
var
  Positionals: TArray<string>;
  Root, OutputFile, StartStr, EndStr, LogPath: string;
  StartUtc, EndUtc: TDateTime;
  Channels: TArray<string>;
  Merger: TOSFMerger;
  Entries: TArray<TOSFFileEntry>;
  Cfg: TOsfToolConfig;
  I: Integer;
  Quiet, Verbose, Json: Boolean;
  Reporter: IProgressReporter;
  Watch: TStopwatch;
  WarnCount, ErrCount: Integer;
begin
  Positionals := PositionalArgs(['--start', '--end', '--log']);
  if Length(Positionals) < 2 then
  begin
    PrintErr('osftool merge: expected <input-dir> <output-file>');
    Exit(EXIT_BAD_ARGS);
  end;
  Root := Positionals[0];
  OutputFile := Positionals[1];

  // Output-mode flags. --quiet/--verbose/--json arrive pre-parsed from the
  // base class; -q / -v are merge-local short aliases.
  Quiet := FQuiet or HasFlag('-q');
  Verbose := FVerbose or HasFlag('-v');
  Json := FJson;
  LogPath := FlagValue('--log', '');

  if Quiet and Verbose then
  begin
    PrintErr('osftool merge: --quiet and --verbose are mutually exclusive');
    Exit(EXIT_BAD_ARGS);
  end;
  if Quiet and Json then
  begin
    PrintErr('osftool merge: --quiet and --json are mutually exclusive');
    Exit(EXIT_BAD_ARGS);
  end;
  if Verbose and Json then
  begin
    PrintErr('osftool merge: --verbose and --json are mutually exclusive');
    Exit(EXIT_BAD_ARGS);
  end;

  // --start / --end are optional. When omitted the interval defaults to
  // epoch (1970-01-01) .. now (UTC) — i.e. "merge everything".
  StartUtc := EncodeDate(1970, 1, 1);
  StartStr := FlagValue('--start', '');
  if (StartStr <> '') and not ParseIso8601(StartStr, StartUtc) then
  begin
    PrintErrf('osftool merge: invalid --start: %s', [StartStr]);
    Exit(EXIT_BAD_ARGS);
  end;

  EndUtc := TTimeZone.Local.ToUniversalTime(Now);
  EndStr := FlagValue('--end', '');
  if (EndStr <> '') and not ParseIso8601(EndStr, EndUtc) then
  begin
    PrintErrf('osftool merge: invalid --end: %s', [EndStr]);
    Exit(EXIT_BAD_ARGS);
  end;

  if not TDirectory.Exists(Root) then
  begin
    PrintErrf('osftool merge: input-dir not found: %s', [Root]);
    Exit(EXIT_NOT_FOUND);
  end;

  // Remaining positionals are channel filter entries.
  SetLength(Channels, Length(Positionals) - 2);
  for I := 2 to High(Positionals) do
    Channels[I - 2] := Positionals[I];

  // Build the reporter first — a bad --log path is reported here.
  try
    Reporter := CreateReporter(Quiet, Verbose, Json, LogPath);
  except
    on E: Exception do
    begin
      PrintErrf('osftool merge: cannot open log file "%s": %s', [LogPath, E.Message]);
      Exit(EXIT_IO_ERROR);
    end;
  end;

  WarnCount := 0;
  ErrCount := 0;
  Cfg := TOsfToolConfig.Create;
  Merger := TOSFMerger.Create;
  try
    Cfg.Load;
    Merger.Reporter := Reporter;
    // Bridge the merger's whole OnLog chain into the reporter's diagnostic
    // stream, counting warnings and stray errors for the summary line.
    Merger.OnLog :=
      procedure(ALevel: TOSFLogLevel; const AMsg: string)
      begin
        if ALevel = llWarning then
          Inc(WarnCount)
        else if ALevel = llError then
          Inc(ErrCount);
        Reporter.Log(ALevel, AMsg);
      end;
    Merger.DebugEnabled := Verbose;
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

    Watch := TStopwatch.StartNew;
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

    try
      Merger.SaveToFile(OutputFile);
    except
      on E: Exception do
      begin
        PrintErrf('osftool merge: write failed: %s', [E.Message]);
        Exit(EXIT_IO_ERROR);
      end;
    end;

    Reporter.Summary(Length(Entries), Merger.FoundFileCount,
      Watch.ElapsedMilliseconds, Merger.FileErrorCount + ErrCount, WarnCount);
  finally
    Merger.Free;
    Cfg.Free;
    Reporter := nil;
  end;
  Result := EXIT_OK;
end;

end.
