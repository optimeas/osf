// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

// "merge" verb. Thin wrapper around TOSFMerger; defaults pulled from
// TOsfToolConfig when the corresponding flag is absent. Progress and
// log messages are rendered through the global Logger listener that
// TBaseCommand registers for each command — output-mode flags
// (--quiet / --verbose / --json / --log) drive the listener
// configuration there, so this verb stays focused on the merge logic.
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
  strict private
    FWarnCount: Integer;
    FErrCount: Integer;
  protected
    // Tally warnings + errors so the final summary line can report them.
    procedure OnLogMessage(const AMsg: string; ALevel: TOSFLogLevel;
                           const ASender: string); override;
  public
    function Name: string; override;
    function ShortDescription: string; override;
    function DoExecute: Integer; override;
    procedure PrintHelp; override;
  end;

implementation

uses
  System.Diagnostics;

resourcestring
  SMergeDesc = 'Merge OSF files from a directory over a time interval';
  SMergeHelp =
    'osftool merge <input-dir> <output-file> [channel ...] [options]' + sLineBreak +
    sLineBreak +
    'Arguments:' + sLineBreak +
    '  input-dir     Root directory (recursive scan for .osf and .osfz)' + sLineBreak +
    '  output-file   Output file path (.osf)' + sLineBreak +
    '  channel       Optional channel names (omit for all)' + sLineBreak +
    sLineBreak +
    'Options:' + sLineBreak +
    '  --start <ts>      Interval start, ISO 8601 (default: 1970-01-01T00:00:00)' + sLineBreak +
    '  --end <ts>        Interval end,   ISO 8601 (default: current date and time)' + sLineBreak +
    '  --osf4            Write OSF4 output (default: from config "output.format")' + sLineBreak +
    '  --overwrite       Overwrite overlapping timestamps' + sLineBreak +
    '  --no-cache        Do not read or write .json sidecar files' + sLineBreak +
    sLineBreak +
    'Output options:' + sLineBreak +
    '  -q, --quiet       Suppress live output; errors only on stderr' + sLineBreak +
    '  -v, --verbose     Print all log messages (no live progress bar)' + sLineBreak +
    '      --json        Emit a machine-readable JSON-Lines event stream' + sLineBreak +
    '      --log <path>  Write a full diagnostic log to file' + sLineBreak +
    sLineBreak +
    'Examples:' + sLineBreak +
    '  osftool merge ./data merged.osf' + sLineBreak +
    '  osftool merge ./data merged.osf --log merge.log' + sLineBreak +
    '  osftool merge ./data merged.osf --json > merge-events.jsonl' + sLineBreak +
    '  osftool merge ./data merged.osf --verbose';
  SMergeErrExpectArgs       = 'osftool merge: expected <input-dir> <output-file>';
  SMergeErrQuietVerbose     = 'osftool merge: --quiet and --verbose are mutually exclusive';
  SMergeErrQuietJson        = 'osftool merge: --quiet and --json are mutually exclusive';
  SMergeErrVerboseJson      = 'osftool merge: --verbose and --json are mutually exclusive';
  SMergeErrInvalidStart     = 'osftool merge: invalid --start: %s';
  SMergeErrInvalidEnd       = 'osftool merge: invalid --end: %s';
  SMergeErrInputDirNotFound = 'osftool merge: input-dir not found: %s';
  SMergeErrLogFile          = 'osftool merge: cannot open log file "%s": %s';
  SMergeErrScanFailed       = 'osftool merge: scan failed: %s';
  SMergeErrNoOverlap        = 'osftool merge: no files overlap the interval';
  SMergeErrWriteFailed      = 'osftool merge: write failed: %s';

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

procedure TOsfMergeCommand.OnLogMessage(const AMsg: string;
  ALevel: TOSFLogLevel; const ASender: string);
begin
  if ALevel = llWarning then
    Inc(FWarnCount)
  else if ALevel = llError then
    Inc(FErrCount);
  inherited;
end;

function TOsfMergeCommand.Name: string;
begin
  Result := 'merge';
end;

function TOsfMergeCommand.ShortDescription: string;
begin
  Result := SMergeDesc;
end;

procedure TOsfMergeCommand.PrintHelp;
begin
  Print(SMergeHelp);
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
  I: Integer;
  Watch: TStopwatch;
  DurationMs: Int64;
  SummaryLine: string;
begin
  Positionals := PositionalArgs(['--start', '--end', '--log']);
  if Length(Positionals) < 2 then
  begin
    PrintErr(SMergeErrExpectArgs);
    Exit(EXIT_BAD_ARGS);
  end;
  Root := Positionals[0];
  OutputFile := Positionals[1];

  // The base class has already parsed --quiet / --verbose / --json /
  // --log and set up listener filtering accordingly. Reject illegal
  // combinations here (the base class does not enforce mutual
  // exclusivity because some other commands accept all three).
  if FQuiet and FVerbose then
  begin
    PrintErr(SMergeErrQuietVerbose);
    Exit(EXIT_BAD_ARGS);
  end;
  if FQuiet and FJson then
  begin
    PrintErr(SMergeErrQuietJson);
    Exit(EXIT_BAD_ARGS);
  end;
  if FVerbose and FJson then
  begin
    PrintErr(SMergeErrVerboseJson);
    Exit(EXIT_BAD_ARGS);
  end;

  // --start / --end are optional. When omitted the interval defaults to
  // epoch (1970-01-01) .. now (UTC) — i.e. "merge everything".
  StartUtc := EncodeDate(1970, 1, 1);
  StartStr := FlagValue('--start', '');
  if (StartStr <> '') and not ParseIso8601(StartStr, StartUtc) then
  begin
    PrintErrf(SMergeErrInvalidStart, [StartStr]);
    Exit(EXIT_BAD_ARGS);
  end;

  EndUtc := TTimeZone.Local.ToUniversalTime(Now);
  EndStr := FlagValue('--end', '');
  if (EndStr <> '') and not ParseIso8601(EndStr, EndUtc) then
  begin
    PrintErrf(SMergeErrInvalidEnd, [EndStr]);
    Exit(EXIT_BAD_ARGS);
  end;

  if not TDirectory.Exists(Root) then
  begin
    PrintErrf(SMergeErrInputDirNotFound, [Root]);
    Exit(EXIT_NOT_FOUND);
  end;

  // Remaining positionals are channel filter entries.
  SetLength(Channels, Length(Positionals) - 2);
  for I := 2 to High(Positionals) do
    Channels[I - 2] := Positionals[I];

  FWarnCount := 0;
  FErrCount := 0;
  Cfg := TOsfToolConfig.Create;
  Merger := TOSFMerger.Create;
  try
    Cfg.Load;
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
        PrintErrf(SMergeErrScanFailed, [E.Message]);
        Exit(EXIT_IO_ERROR);
      end;
    end;
    if Length(Entries) = 0 then
    begin
      PrintErr(SMergeErrNoOverlap);
      Exit(EXIT_NOT_FOUND);
    end;

    try
      Merger.SaveToFile(OutputFile);
    except
      on E: Exception do
      begin
        PrintErrf(SMergeErrWriteFailed, [E.Message]);
        Exit(EXIT_IO_ERROR);
      end;
    end;

    DurationMs := Watch.ElapsedMilliseconds;
    SummaryLine := Format(
      'Done. Merged %d of %d files in %.1fs (%d errors, %d warnings).',
      [Length(Entries), Merger.FoundFileCount, DurationMs / 1000.0,
       Merger.FileErrorCount + FErrCount, FWarnCount],
      TFormatSettings.Invariant);
    Print(SummaryLine);
  finally
    Merger.Free;
    Cfg.Free;
  end;
  Result := EXIT_OK;
end;

end.
