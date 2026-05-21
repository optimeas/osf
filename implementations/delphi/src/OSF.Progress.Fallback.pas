// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

// Fallback progress reporter for non-interactive stdout (pipe or file):
// no ANSI, no overwriting. Emits a plain "Reading: N% (k/total)" line every
// 5% or every 50 files, throttled to at most one line every two seconds.
unit OSF.Progress.Fallback;

interface

uses
  OSF.Log,
  OSF.Progress;

type
  TOSFFallbackProgressReporter = class(TOSFProgressReporterBase)
  strict private
    FTotal: Integer;
    FStartTick: Int64;
    FLastEmitMs: Int64;
    FLastPercent: Integer;
    FLastCount: Integer;
    procedure MaybeEmit(AIndex: Integer);
  public
    procedure ScanStarted(const ADirectory: string); override;
    procedure ScanFinished(AFileCount: Integer); override;
    procedure ReadStarted(ATotal: Integer); override;
    procedure FileStarted(AIndex, ATotal: Integer; const APath: string); override;
    procedure FileError(AIndex: Integer; const APath, AErrorMessage: string); override;
    procedure Log(ALevel: TOSFLogLevel; const AMessage: string); override;
    procedure Summary(AFilesOk, AFilesTotal: Integer; ADurationMs: Int64;
                      AErrors, AWarnings: Integer); override;
  end;

implementation

uses
  System.SysUtils,
  System.Diagnostics,
  OSF.Progress.Console;

const
  // Layout / severity-marker frames - not translatable prose.
  MSG_FILE_ERROR = '[ERROR] %s: %s';
  MSG_LOG_ERROR  = '[ERROR] %s';

  // A progress line is printed once either threshold is crossed, but never
  // more often than the minimum interval.
  EMIT_PERCENT_STEP    = 5;
  EMIT_COUNT_STEP      = 50;
  EMIT_MIN_INTERVAL_MS = 2000;

resourcestring
  MSG_SCAN_STARTED  = 'Scanning directory: %s';
  MSG_SCAN_FINISHED = 'Found %d OSF files.';
  MSG_READING       = 'Reading: %d%% (%d/%d)';

procedure TOSFFallbackProgressReporter.ReadStarted(ATotal: Integer);
begin
  FTotal := ATotal;
  FStartTick := TStopwatch.GetTimeStamp;
  FLastEmitMs := -EMIT_MIN_INTERVAL_MS;
  FLastPercent := -EMIT_PERCENT_STEP;
  FLastCount := -EMIT_COUNT_STEP;
end;

procedure TOSFFallbackProgressReporter.MaybeEmit(AIndex: Integer);
var
  Percent: Integer;
  ElapsedMs: Int64;
  IsFinal, CrossedThreshold: Boolean;
begin
  if FTotal <= 0 then
    Exit;
  Percent := Round(100 * AIndex / FTotal);
  ElapsedMs := Round((TStopwatch.GetTimeStamp - FStartTick) /
                     TStopwatch.Frequency * 1000);
  IsFinal := AIndex >= FTotal;
  CrossedThreshold := ((Percent - FLastPercent) >= EMIT_PERCENT_STEP) or
                      ((AIndex - FLastCount) >= EMIT_COUNT_STEP);
  if IsFinal or (CrossedThreshold and
                 ((ElapsedMs - FLastEmitMs) >= EMIT_MIN_INTERVAL_MS)) then
  begin
    Writeln(Output, Format(MSG_READING, [Percent, AIndex, FTotal]));
    FLastPercent := Percent;
    FLastCount := AIndex;
    FLastEmitMs := ElapsedMs;
  end;
end;

procedure TOSFFallbackProgressReporter.ScanStarted(const ADirectory: string);
begin
  Writeln(Output, Format(MSG_SCAN_STARTED, [ADirectory]));
end;

procedure TOSFFallbackProgressReporter.ScanFinished(AFileCount: Integer);
begin
  Writeln(Output, Format(MSG_SCAN_FINISHED, [AFileCount]));
end;

procedure TOSFFallbackProgressReporter.FileStarted(AIndex, ATotal: Integer;
  const APath: string);
begin
  MaybeEmit(AIndex);
end;

procedure TOSFFallbackProgressReporter.FileError(AIndex: Integer;
  const APath, AErrorMessage: string);
begin
  Writeln(ErrOutput, Format(MSG_FILE_ERROR, [ExtractFileName(APath), AErrorMessage]));
end;

procedure TOSFFallbackProgressReporter.Log(ALevel: TOSFLogLevel; const AMessage: string);
begin
  if ALevel = llError then
    Writeln(ErrOutput, Format(MSG_LOG_ERROR, [AMessage]));
end;

procedure TOSFFallbackProgressReporter.Summary(AFilesOk, AFilesTotal: Integer;
  ADurationMs: Int64; AErrors, AWarnings: Integer);
begin
  Writeln(Output, FormatSummaryLine(AFilesOk, AFilesTotal, ADurationMs,
                                    AErrors, AWarnings));
end;

end.
