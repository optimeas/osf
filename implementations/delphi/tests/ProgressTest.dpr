// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

// Smoke test for the OSF.Progress.* reporter subsystem. Asserts the pure
// helper functions, exercises the LogFile decorator against a temp file,
// and drives a full merge-like event sequence through every reporter.
//
// Run with no argument for the automated checks (exit 0 = all passed).
// Run with "live" to drive the live ANSI reporter for visual inspection.
program ProgressTest;

{$APPTYPE CONSOLE}

uses
  System.SysUtils,
  System.IOUtils,
  OSF.Log                  in '..\src\OSF.Log.pas',
  OSF.Progress             in '..\src\OSF.Progress.pas',
  OSF.Progress.Console     in '..\src\OSF.Progress.Console.pas',
  OSF.Progress.Quiet       in '..\src\OSF.Progress.Quiet.pas',
  OSF.Progress.Verbose     in '..\src\OSF.Progress.Verbose.pas',
  OSF.Progress.Json        in '..\src\OSF.Progress.Json.pas',
  OSF.Progress.Fallback    in '..\src\OSF.Progress.Fallback.pas',
  OSF.Progress.Live        in '..\src\OSF.Progress.Live.pas',
  OSF.Progress.LogFile     in '..\src\OSF.Progress.LogFile.pas';

var
  Failures: Integer = 0;

procedure Check(ACondition: Boolean; const ADescription: string);
begin
  if ACondition then
    Writeln('  ok    ', ADescription)
  else
  begin
    Writeln('  FAIL  ', ADescription);
    Inc(Failures);
  end;
end;

// Drives a reporter through a representative merge run.
procedure RunSequence(const AReporter: IProgressReporter);
var
  I: Integer;
begin
  AReporter.ScanStarted('V:\data\osf');
  AReporter.ScanFinished(8);
  AReporter.SidecarStarted(8);
  for I := 1 to 8 do
    AReporter.SidecarProgress(I, 8);
  AReporter.SidecarFinished(3);
  AReporter.ReadStarted(8);
  for I := 1 to 8 do
  begin
    AReporter.FileStarted(I, 8, Format('V:\data\osf\recording_%.3d.osfz', [I]));
    if I = 5 then
      AReporter.FileError(I, Format('V:\data\osf\recording_%.3d.osfz', [I]),
        'cannot decompress OSFZ container: invalid gzip header')
    else
      AReporter.FileFinished(I, 12, 4096);
  end;
  AReporter.Log(llInfo, 'per-channel chatter that the live view swallows');
  AReporter.Log(llWarning, 'channel "X": type mismatch');
  AReporter.WriteStarted('merged.osf');
  AReporter.WriteFinished('merged.osf', 1234567);
  AReporter.Summary(7, 8, 12400, 1, 1);
end;

procedure TestShortenPath;
var
  Long, Short, Exact: string;
begin
  Writeln('ShortenPath');
  Short := 'V:\data\short.osfz';
  Check(ShortenPath(Short) = Short, 'short path is returned unchanged');

  Exact := StringOfChar('a', 80);
  Check(ShortenPath(Exact) = Exact, 'exactly 80 chars returned unchanged');

  Long := StringOfChar('a', 81);
  Check(Length(ShortenPath(Long)) = 80, '81 chars shortened to exactly 80');

  Long := StringOfChar('x', 200);
  Check(Length(ShortenPath(Long)) = 80, '200 chars shortened to exactly 80');
  Check(Pos('...', ShortenPath(Long)) > 0, 'shortened path contains the ellipsis');
end;

procedure TestFormatHelpers;
begin
  Writeln('FormatDuration / FormatSummaryLine');
  Check(FormatDuration(12400) = '12.4s', '12400 ms -> "12.4s"');
  Check(FormatDuration(500) = '0.5s', '500 ms -> "0.5s"');
  Check(FormatDuration(134000) = '2m 14s', '134000 ms -> "2m 14s"');

  Check(FormatSummaryLine(346, 346, 1000, 0, 0) =
        'Done. Merged 346 of 346 files in 1.0s.',
        'no errors / no warnings omits the parenthesis');
  Check(FormatSummaryLine(346, 346, 1000, 0, 3) =
        'Done. Merged 346 of 346 files in 1.0s (3 warnings).',
        'warnings only shows "(N warnings)"');
  Check(FormatSummaryLine(344, 346, 12400, 2, 5) =
        'Done. Merged 344 of 346 files in 12.4s (2 errors, 5 warnings).',
        'errors shows "(E errors, W warnings)"');
end;

procedure TestLogFileDecorator;
var
  LogPath, Content: string;
  Reporter: IProgressReporter;
begin
  Writeln('LogFile decorator');
  LogPath := TPath.Combine(TPath.GetTempPath, 'osf_progress_test.log');
  if TFile.Exists(LogPath) then
    TFile.Delete(LogPath);

  Reporter := TOSFLogFileProgressReporter.Create(
    TOSFQuietProgressReporter.Create, LogPath);
  RunSequence(Reporter);
  Reporter := nil;  // releases the decorator -> closes the file

  Check(TFile.Exists(LogPath), 'log file created');
  Content := TFile.ReadAllText(LogPath, TEncoding.UTF8);
  Check(Pos('[INFO ] Scanning directory: V:\data\osf', Content) > 0,
        'log file holds the scan line');
  Check(Pos('[ERROR] V:\data\osf\recording_005.osfz: cannot decompress', Content) > 0,
        'log file holds the file-error line');
  Check(Pos('[WARN ] channel "X": type mismatch', Content) > 0,
        'log file holds the warning line (all levels persisted)');
  Check(Pos('Done. Merged 7 of 8 files', Content) > 0,
        'log file holds the summary line');
end;

begin
  if SameText(ParamStr(1), 'live') then
  begin
    RunSequence(TOSFLiveProgressReporter.Create);
    Exit;
  end;

  Writeln('OSF.Progress smoke test');
  Writeln('-----------------------');
  try
    TestShortenPath;
    TestFormatHelpers;
    TestLogFileDecorator;

    Writeln('Verbose reporter sequence:');
    RunSequence(TOSFVerboseProgressReporter.Create);
    Writeln('JSON reporter sequence:');
    RunSequence(TOSFJsonProgressReporter.Create);
    Writeln('Fallback reporter sequence:');
    RunSequence(TOSFFallbackProgressReporter.Create);
  except
    on E: Exception do
    begin
      Writeln('  FAIL  unhandled ', E.ClassName, ': ', E.Message);
      Inc(Failures);
    end;
  end;

  Writeln('-----------------------');
  if Failures = 0 then
  begin
    Writeln('PROGRESS SMOKE TEST PASSED');
    ExitCode := 0;
  end
  else
  begin
    Writeln(Format('PROGRESS SMOKE TEST FAILED (%d failure(s))', [Failures]));
    ExitCode := 1;
  end;
end.
