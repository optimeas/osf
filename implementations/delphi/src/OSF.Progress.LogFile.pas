// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

// Log-file progress reporter: a decorator that forwards every call to an
// inner reporter unchanged and additionally writes a complete diagnostic
// log (all phase events and all log levels) to a UTF-8 file. Orthogonal to
// the console mode — pair it with any inner reporter.
unit OSF.Progress.LogFile;

interface

uses
  System.Classes,
  OSF.Log,
  OSF.Progress;

type
  TOSFLogFileProgressReporter = class(TInterfacedObject, IProgressReporter)
  strict private
    FInner: IProgressReporter;
    FStream: TFileStream;
    procedure WriteLogLine(ALevel: TOSFLogLevel; const AText: string);
  public
    // Opens ALogPath in truncate mode. Raises if the file cannot be created.
    constructor Create(const AInner: IProgressReporter; const ALogPath: string);
    destructor Destroy; override;

    procedure ScanStarted(const ADirectory: string);
    procedure ScanFinished(AFileCount: Integer);
    procedure SidecarStarted(ATotal: Integer);
    procedure SidecarProgress(ADone, ATotal: Integer);
    procedure SidecarFinished(ACreated: Integer);
    procedure ReadStarted(ATotal: Integer);
    procedure FileStarted(AIndex, ATotal: Integer; const APath: string);
    procedure FileFinished(AIndex: Integer; AChannels, ASamples: Integer);
    procedure FileError(AIndex: Integer; const APath, AErrorMessage: string);
    procedure WriteStarted(const AOutputPath: string);
    procedure WriteFinished(const AOutputPath: string; ABytes: Int64);
    procedure Log(ALevel: TOSFLogLevel; const AMessage: string);
    procedure Summary(AFilesOk, AFilesTotal: Integer; ADurationMs: Int64;
                      AErrors, AWarnings: Integer);
  end;

implementation

uses
  System.SysUtils,
  OSF.Progress.Console;

const
  MSG_SCAN_STARTED   = 'Scanning directory: %s';
  MSG_SCAN_FINISHED  = 'Found %d OSF files.';
  MSG_SIDECAR_STARTED = 'Creating sidecar files (%d candidates)...';
  MSG_SIDECAR_DONE   = 'Sidecar files created: %d';
  MSG_READ_STARTED   = 'Reading %d files...';
  MSG_FILE_STARTED   = 'Reading file %d/%d: %s';
  MSG_FILE_FINISHED  = 'File %d finished: %d channels, %d samples';
  MSG_FILE_ERROR     = '%s: %s';
  MSG_WRITE_STARTED  = 'Writing output: %s';
  MSG_WRITE_FINISHED = 'Wrote %s (%d bytes)';

constructor TOSFLogFileProgressReporter.Create(const AInner: IProgressReporter;
  const ALogPath: string);
begin
  inherited Create;
  FInner := AInner;
  FStream := TFileStream.Create(ALogPath, fmCreate);
end;

destructor TOSFLogFileProgressReporter.Destroy;
begin
  FStream.Free;
  FInner := nil;
  inherited;
end;

procedure TOSFLogFileProgressReporter.WriteLogLine(ALevel: TOSFLogLevel;
  const AText: string);
var
  Bytes: TBytes;
begin
  Bytes := TEncoding.UTF8.GetBytes('[' + LogLevelTag(ALevel) + '] ' + AText + sLineBreak);
  if Length(Bytes) > 0 then
    FStream.WriteBuffer(Bytes, Length(Bytes));
end;

procedure TOSFLogFileProgressReporter.ScanStarted(const ADirectory: string);
begin
  WriteLogLine(llInfo, Format(MSG_SCAN_STARTED, [ADirectory]));
  FInner.ScanStarted(ADirectory);
end;

procedure TOSFLogFileProgressReporter.ScanFinished(AFileCount: Integer);
begin
  WriteLogLine(llInfo, Format(MSG_SCAN_FINISHED, [AFileCount]));
  FInner.ScanFinished(AFileCount);
end;

procedure TOSFLogFileProgressReporter.SidecarStarted(ATotal: Integer);
begin
  WriteLogLine(llInfo, Format(MSG_SIDECAR_STARTED, [ATotal]));
  FInner.SidecarStarted(ATotal);
end;

procedure TOSFLogFileProgressReporter.SidecarProgress(ADone, ATotal: Integer);
begin
  // Transient counting — not logged; SidecarFinished records the outcome.
  FInner.SidecarProgress(ADone, ATotal);
end;

procedure TOSFLogFileProgressReporter.SidecarFinished(ACreated: Integer);
begin
  WriteLogLine(llInfo, Format(MSG_SIDECAR_DONE, [ACreated]));
  FInner.SidecarFinished(ACreated);
end;

procedure TOSFLogFileProgressReporter.ReadStarted(ATotal: Integer);
begin
  WriteLogLine(llInfo, Format(MSG_READ_STARTED, [ATotal]));
  FInner.ReadStarted(ATotal);
end;

procedure TOSFLogFileProgressReporter.FileStarted(AIndex, ATotal: Integer;
  const APath: string);
begin
  WriteLogLine(llInfo, Format(MSG_FILE_STARTED, [AIndex, ATotal, APath]));
  FInner.FileStarted(AIndex, ATotal, APath);
end;

procedure TOSFLogFileProgressReporter.FileFinished(AIndex: Integer;
  AChannels, ASamples: Integer);
begin
  WriteLogLine(llInfo, Format(MSG_FILE_FINISHED, [AIndex, AChannels, ASamples]));
  FInner.FileFinished(AIndex, AChannels, ASamples);
end;

procedure TOSFLogFileProgressReporter.FileError(AIndex: Integer;
  const APath, AErrorMessage: string);
begin
  WriteLogLine(llError, Format(MSG_FILE_ERROR, [APath, AErrorMessage]));
  FInner.FileError(AIndex, APath, AErrorMessage);
end;

procedure TOSFLogFileProgressReporter.WriteStarted(const AOutputPath: string);
begin
  WriteLogLine(llInfo, Format(MSG_WRITE_STARTED, [AOutputPath]));
  FInner.WriteStarted(AOutputPath);
end;

procedure TOSFLogFileProgressReporter.WriteFinished(const AOutputPath: string;
  ABytes: Int64);
begin
  WriteLogLine(llInfo, Format(MSG_WRITE_FINISHED, [AOutputPath, ABytes]));
  FInner.WriteFinished(AOutputPath, ABytes);
end;

procedure TOSFLogFileProgressReporter.Log(ALevel: TOSFLogLevel; const AMessage: string);
begin
  // Every level reaches the file, regardless of the console mode.
  WriteLogLine(ALevel, AMessage);
  FInner.Log(ALevel, AMessage);
end;

procedure TOSFLogFileProgressReporter.Summary(AFilesOk, AFilesTotal: Integer;
  ADurationMs: Int64; AErrors, AWarnings: Integer);
begin
  WriteLogLine(llInfo, FormatSummaryLine(AFilesOk, AFilesTotal, ADurationMs,
                                         AErrors, AWarnings));
  FInner.Summary(AFilesOk, AFilesTotal, ADurationMs, AErrors, AWarnings);
end;

end.
