// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

// Verbose progress reporter: no live display. Every phase event and every
// log line scrolls plainly on stdout — the classic diagnostic view.
unit OSF.Progress.Verbose;

interface

uses
  OSF.Log,
  OSF.Progress;

type
  TOSFVerboseProgressReporter = class(TOSFProgressReporterBase)
  public
    procedure ScanStarted(const ADirectory: string); override;
    procedure ScanFinished(AFileCount: Integer); override;
    procedure SidecarStarted(ATotal: Integer); override;
    procedure SidecarFinished(ACreated: Integer); override;
    procedure ReadStarted(ATotal: Integer); override;
    procedure FileStarted(AIndex, ATotal: Integer; const APath: string); override;
    procedure FileFinished(AIndex: Integer; AChannels, ASamples: Integer); override;
    procedure FileError(AIndex: Integer; const APath, AErrorMessage: string); override;
    procedure WriteStarted(const AOutputPath: string); override;
    procedure WriteFinished(const AOutputPath: string; ABytes: Int64); override;
    procedure Log(ALevel: TOSFLogLevel; const AMessage: string); override;
    procedure Summary(AFilesOk, AFilesTotal: Integer; ADurationMs: Int64;
                      AErrors, AWarnings: Integer); override;
  end;

implementation

uses
  System.SysUtils,
  OSF.Progress.Console;

const
  // Layout / severity-marker frames - not translatable prose.
  MSG_FILE_ERROR = '[ERROR] %s: %s';
  MSG_LOG_LINE   = '[%s] %s';

resourcestring
  MSG_SCAN_STARTED    = 'Scanning directory: %s';
  MSG_SCAN_FINISHED   = 'Found %d OSF files.';
  MSG_SIDECAR_STARTED = 'Creating sidecar files (%d candidates)...';
  MSG_SIDECAR_DONE    = 'Sidecar files created: %d';
  MSG_READ_STARTED    = 'Reading %d files...';
  MSG_FILE_STARTED    = 'Reading file %d/%d: %s';
  MSG_FILE_FINISHED   = '  file %d done: %d channels, %d samples';
  MSG_WRITE_STARTED   = 'Writing output: %s';
  MSG_WRITE_FINISHED  = 'Wrote %s (%d bytes)';

procedure TOSFVerboseProgressReporter.ScanStarted(const ADirectory: string);
begin
  Writeln(Output, Format(MSG_SCAN_STARTED, [ADirectory]));
end;

procedure TOSFVerboseProgressReporter.ScanFinished(AFileCount: Integer);
begin
  Writeln(Output, Format(MSG_SCAN_FINISHED, [AFileCount]));
end;

procedure TOSFVerboseProgressReporter.SidecarStarted(ATotal: Integer);
begin
  Writeln(Output, Format(MSG_SIDECAR_STARTED, [ATotal]));
end;

procedure TOSFVerboseProgressReporter.SidecarFinished(ACreated: Integer);
begin
  Writeln(Output, Format(MSG_SIDECAR_DONE, [ACreated]));
end;

procedure TOSFVerboseProgressReporter.ReadStarted(ATotal: Integer);
begin
  Writeln(Output, Format(MSG_READ_STARTED, [ATotal]));
end;

procedure TOSFVerboseProgressReporter.FileStarted(AIndex, ATotal: Integer;
  const APath: string);
begin
  Writeln(Output, Format(MSG_FILE_STARTED, [AIndex, ATotal, APath]));
end;

procedure TOSFVerboseProgressReporter.FileFinished(AIndex: Integer;
  AChannels, ASamples: Integer);
begin
  Writeln(Output, Format(MSG_FILE_FINISHED, [AIndex, AChannels, ASamples]));
end;

procedure TOSFVerboseProgressReporter.FileError(AIndex: Integer;
  const APath, AErrorMessage: string);
begin
  Writeln(Output, Format(MSG_FILE_ERROR, [ExtractFileName(APath), AErrorMessage]));
end;

procedure TOSFVerboseProgressReporter.WriteStarted(const AOutputPath: string);
begin
  Writeln(Output, Format(MSG_WRITE_STARTED, [AOutputPath]));
end;

procedure TOSFVerboseProgressReporter.WriteFinished(const AOutputPath: string;
  ABytes: Int64);
begin
  Writeln(Output, Format(MSG_WRITE_FINISHED, [AOutputPath, ABytes]));
end;

procedure TOSFVerboseProgressReporter.Log(ALevel: TOSFLogLevel; const AMessage: string);
begin
  Writeln(Output, Format(MSG_LOG_LINE, [LogLevelTag(ALevel), AMessage]));
end;

procedure TOSFVerboseProgressReporter.Summary(AFilesOk, AFilesTotal: Integer;
  ADurationMs: Int64; AErrors, AWarnings: Integer);
begin
  Writeln(Output, FormatSummaryLine(AFilesOk, AFilesTotal, ADurationMs,
                                    AErrors, AWarnings));
end;

end.
