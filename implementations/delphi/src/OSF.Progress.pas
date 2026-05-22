// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

// Progress-reporting interface for long-running, multi-file operations.
//
// The interface decouples an operation (currently osftool's merge) from
// how its progress is presented. An operation feeds it two kinds of input:
//
//   * structured phase events (ScanStarted, FileStarted, Summary, ...) for
//     the milestones worth showing, and
//   * Log(level, message) — the catch-all that receives the operation's
//     entire diagnostic stream (the per-channel chatter, warnings, ...),
//     normally bridged in from the existing TOSFLoggable.OnLog chain.
//
// Each concrete reporter decides what to surface: a live reporter shows the
// phase events and errors and swallows the Log() flood; a verbose reporter
// prints everything; a log-file decorator persists everything.
unit OSF.Progress;

interface

uses
  OSF.Log;

type
  // Implemented by every concrete reporter. Reference-counted — hold it in
  // an IProgressReporter variable and let ARC free it.
  IProgressReporter = interface
    ['{3F1A9C82-7E54-4B6D-9A2F-1C8D5E3B4A60}']
    // ── Phase 1: directory scan ───────────────────────────────────────────────
    procedure ScanStarted(const ADirectory: string);
    procedure ScanFinished(AFileCount: Integer);
    // ── Phase 2: sidecar creation (only when caches are actually built) ───────
    procedure SidecarStarted(ATotal: Integer);
    procedure SidecarProgress(ADone, ATotal: Integer);
    procedure SidecarFinished(ACreated: Integer);
    // ── Phase 3: reading the input files ──────────────────────────────────────
    procedure ReadStarted(ATotal: Integer);
    procedure FileStarted(AIndex, ATotal: Integer; const APath: string);
    procedure FileFinished(AIndex: Integer; AChannels, ASamples: Integer);
    procedure FileError(AIndex: Integer; const APath, AErrorMessage: string);
    // ── Phase 4: writing the output file ──────────────────────────────────────
    procedure WriteStarted(const AOutputPath: string);
    procedure WriteFinished(const AOutputPath: string; ABytes: Int64);
    // ── Diagnostic stream — fed from the operation's TOSFLoggable.OnLog ───────
    procedure Log(ALevel: TOSFLogLevel; const AMessage: string);
    // ── Phase 5: final summary ────────────────────────────────────────────────
    procedure Summary(AFilesOk, AFilesTotal: Integer; ADurationMs: Int64;
                      AErrors, AWarnings: Integer);
    // ── Helper ───────────────────────────────────────────────────────────────
    procedure StartProgress(const AText: string; AIndex, ATotal: Integer);
    procedure DoProgress(const AText: string; AIndex, ATotal: Integer);
    procedure EndProgress;
  end;

  // Do-nothing adapter base. Implements every IProgressReporter method as an
  // empty virtual stub so a concrete reporter overrides only the events it
  // actually renders.
  TOSFProgressReporterBase = class(TInterfacedObject, IProgressReporter)
  public
    procedure ScanStarted(const ADirectory: string); virtual;
    procedure ScanFinished(AFileCount: Integer); virtual;
    procedure SidecarStarted(ATotal: Integer); virtual;
    procedure SidecarProgress(ADone, ATotal: Integer); virtual;
    procedure SidecarFinished(ACreated: Integer); virtual;
    procedure ReadStarted(ATotal: Integer); virtual;
    procedure FileStarted(AIndex, ATotal: Integer; const APath: string); virtual;
    procedure FileFinished(AIndex: Integer; AChannels, ASamples: Integer); virtual;
    procedure FileError(AIndex: Integer; const APath, AErrorMessage: string); virtual;
    procedure WriteStarted(const AOutputPath: string); virtual;
    procedure WriteFinished(const AOutputPath: string; ABytes: Int64); virtual;
    procedure Log(ALevel: TOSFLogLevel; const AMessage: string); virtual;
    procedure Summary(AFilesOk, AFilesTotal: Integer; ADurationMs: Int64;
                      AErrors, AWarnings: Integer); virtual;
    procedure StartProgress(const AText: string; AIndex, ATotal: Integer); virtual;
    procedure DoProgress(const AText: string; AIndex, ATotal: Integer); virtual;
    procedure EndProgress; virtual;
  end;

implementation

procedure TOSFProgressReporterBase.ScanStarted(const ADirectory: string);
begin
end;

procedure TOSFProgressReporterBase.ScanFinished(AFileCount: Integer);
begin
end;

procedure TOSFProgressReporterBase.SidecarStarted(ATotal: Integer);
begin
end;

procedure TOSFProgressReporterBase.StartProgress(const AText: string; AIndex,
  ATotal: Integer);
begin

end;

procedure TOSFProgressReporterBase.SidecarProgress(ADone, ATotal: Integer);
begin
end;

procedure TOSFProgressReporterBase.SidecarFinished(ACreated: Integer);
begin
end;

procedure TOSFProgressReporterBase.ReadStarted(ATotal: Integer);
begin
end;

procedure TOSFProgressReporterBase.FileStarted(AIndex, ATotal: Integer; const APath: string);
begin
end;

procedure TOSFProgressReporterBase.FileFinished(AIndex: Integer; AChannels, ASamples: Integer);
begin
end;

procedure TOSFProgressReporterBase.DoProgress(const AText: string; AIndex,
  ATotal: Integer);
begin

end;

procedure TOSFProgressReporterBase.EndProgress;
begin

end;

procedure TOSFProgressReporterBase.FileError(AIndex: Integer; const APath, AErrorMessage: string);
begin
end;

procedure TOSFProgressReporterBase.WriteStarted(const AOutputPath: string);
begin
end;

procedure TOSFProgressReporterBase.WriteFinished(const AOutputPath: string; ABytes: Int64);
begin
end;

procedure TOSFProgressReporterBase.Log(ALevel: TOSFLogLevel; const AMessage: string);
begin
end;

procedure TOSFProgressReporterBase.Summary(AFilesOk, AFilesTotal: Integer;
  ADurationMs: Int64; AErrors, AWarnings: Integer);
begin
end;

end.
