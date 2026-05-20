// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

// JSON progress reporter: emits one compact JSON object per phase event on
// stdout (JSON-Lines). The diagnostic Log() stream is intentionally not
// emitted so the structured stream stays clean for machine consumers.
unit OSF.Progress.Json;

interface

uses
  System.JSON,
  OSF.Progress;

type
  TOSFJsonProgressReporter = class(TOSFProgressReporterBase)
  strict private
    procedure Emit(AObj: TJSONObject);
  public
    procedure ScanStarted(const ADirectory: string); override;
    procedure ScanFinished(AFileCount: Integer); override;
    procedure SidecarProgress(ADone, ATotal: Integer); override;
    procedure SidecarFinished(ACreated: Integer); override;
    procedure ReadStarted(ATotal: Integer); override;
    procedure FileStarted(AIndex, ATotal: Integer; const APath: string); override;
    procedure FileFinished(AIndex: Integer; AChannels, ASamples: Integer); override;
    procedure FileError(AIndex: Integer; const APath, AErrorMessage: string); override;
    procedure WriteStarted(const AOutputPath: string); override;
    procedure WriteFinished(const AOutputPath: string; ABytes: Int64); override;
    procedure Summary(AFilesOk, AFilesTotal: Integer; ADurationMs: Int64;
                      AErrors, AWarnings: Integer); override;
  end;

implementation

const
  EV_SCAN_STARTED    = 'scan_started';
  EV_SCAN_FINISHED   = 'scan_finished';
  EV_SIDECAR_PROGRESS = 'sidecar_progress';
  EV_SIDECAR_FINISHED = 'sidecar_finished';
  EV_READ_STARTED    = 'read_started';
  EV_FILE_STARTED    = 'file_started';
  EV_FILE_FINISHED   = 'file_finished';
  EV_FILE_ERROR      = 'file_error';
  EV_WRITE_STARTED   = 'write_started';
  EV_WRITE_FINISHED  = 'write_finished';
  EV_SUMMARY         = 'summary';

procedure TOSFJsonProgressReporter.Emit(AObj: TJSONObject);
begin
  try
    // ToString yields compact JSON (no pretty-print, no inter-field space).
    Writeln(Output, AObj.ToString);
  finally
    AObj.Free;
  end;
end;

procedure TOSFJsonProgressReporter.ScanStarted(const ADirectory: string);
var
  Obj: TJSONObject;
begin
  Obj := TJSONObject.Create;
  Obj.AddPair('event', EV_SCAN_STARTED);
  Obj.AddPair('directory', ADirectory);
  Emit(Obj);
end;

procedure TOSFJsonProgressReporter.ScanFinished(AFileCount: Integer);
var
  Obj: TJSONObject;
begin
  Obj := TJSONObject.Create;
  Obj.AddPair('event', EV_SCAN_FINISHED);
  Obj.AddPair('file_count', TJSONNumber.Create(AFileCount));
  Emit(Obj);
end;

procedure TOSFJsonProgressReporter.SidecarProgress(ADone, ATotal: Integer);
var
  Obj: TJSONObject;
begin
  Obj := TJSONObject.Create;
  Obj.AddPair('event', EV_SIDECAR_PROGRESS);
  Obj.AddPair('done', TJSONNumber.Create(ADone));
  Obj.AddPair('total', TJSONNumber.Create(ATotal));
  Emit(Obj);
end;

procedure TOSFJsonProgressReporter.SidecarFinished(ACreated: Integer);
var
  Obj: TJSONObject;
begin
  Obj := TJSONObject.Create;
  Obj.AddPair('event', EV_SIDECAR_FINISHED);
  Obj.AddPair('created', TJSONNumber.Create(ACreated));
  Emit(Obj);
end;

procedure TOSFJsonProgressReporter.ReadStarted(ATotal: Integer);
var
  Obj: TJSONObject;
begin
  Obj := TJSONObject.Create;
  Obj.AddPair('event', EV_READ_STARTED);
  Obj.AddPair('total', TJSONNumber.Create(ATotal));
  Emit(Obj);
end;

procedure TOSFJsonProgressReporter.FileStarted(AIndex, ATotal: Integer;
  const APath: string);
var
  Obj: TJSONObject;
begin
  Obj := TJSONObject.Create;
  Obj.AddPair('event', EV_FILE_STARTED);
  Obj.AddPair('index', TJSONNumber.Create(AIndex));
  Obj.AddPair('total', TJSONNumber.Create(ATotal));
  Obj.AddPair('path', APath);
  Emit(Obj);
end;

procedure TOSFJsonProgressReporter.FileFinished(AIndex: Integer;
  AChannels, ASamples: Integer);
var
  Obj: TJSONObject;
begin
  Obj := TJSONObject.Create;
  Obj.AddPair('event', EV_FILE_FINISHED);
  Obj.AddPair('index', TJSONNumber.Create(AIndex));
  Obj.AddPair('channels', TJSONNumber.Create(AChannels));
  Obj.AddPair('samples', TJSONNumber.Create(ASamples));
  Emit(Obj);
end;

procedure TOSFJsonProgressReporter.FileError(AIndex: Integer;
  const APath, AErrorMessage: string);
var
  Obj: TJSONObject;
begin
  Obj := TJSONObject.Create;
  Obj.AddPair('event', EV_FILE_ERROR);
  Obj.AddPair('index', TJSONNumber.Create(AIndex));
  Obj.AddPair('path', APath);
  Obj.AddPair('error', AErrorMessage);
  Emit(Obj);
end;

procedure TOSFJsonProgressReporter.WriteStarted(const AOutputPath: string);
var
  Obj: TJSONObject;
begin
  Obj := TJSONObject.Create;
  Obj.AddPair('event', EV_WRITE_STARTED);
  Obj.AddPair('output', AOutputPath);
  Emit(Obj);
end;

procedure TOSFJsonProgressReporter.WriteFinished(const AOutputPath: string;
  ABytes: Int64);
var
  Obj: TJSONObject;
begin
  Obj := TJSONObject.Create;
  Obj.AddPair('event', EV_WRITE_FINISHED);
  Obj.AddPair('output', AOutputPath);
  Obj.AddPair('bytes', TJSONNumber.Create(ABytes));
  Emit(Obj);
end;

procedure TOSFJsonProgressReporter.Summary(AFilesOk, AFilesTotal: Integer;
  ADurationMs: Int64; AErrors, AWarnings: Integer);
var
  Obj: TJSONObject;
begin
  Obj := TJSONObject.Create;
  Obj.AddPair('event', EV_SUMMARY);
  Obj.AddPair('files_ok', TJSONNumber.Create(AFilesOk));
  Obj.AddPair('files_total', TJSONNumber.Create(AFilesTotal));
  Obj.AddPair('duration_ms', TJSONNumber.Create(ADurationMs));
  Obj.AddPair('errors', TJSONNumber.Create(AErrors));
  Obj.AddPair('warnings', TJSONNumber.Create(AWarnings));
  Emit(Obj);
end;

end.
