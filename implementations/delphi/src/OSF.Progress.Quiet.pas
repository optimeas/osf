// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

// Quiet progress reporter: prints nothing on success. Only file errors and
// error-level log lines reach stderr; the exit code carries the verdict.
unit OSF.Progress.Quiet;

interface

uses
  OSF.Log,
  OSF.Progress;

type
  TOSFQuietProgressReporter = class(TOSFProgressReporterBase)
  public
    procedure FileError(AIndex: Integer; const APath, AErrorMessage: string); override;
    procedure Log(ALevel: TOSFLogLevel; const AMessage: string); override;
  end;

implementation

uses
  System.SysUtils;

const
  FMT_FILE_ERROR = '[ERROR] %s: %s';
  FMT_LOG_ERROR  = '[ERROR] %s';

procedure TOSFQuietProgressReporter.FileError(AIndex: Integer;
  const APath, AErrorMessage: string);
begin
  Writeln(ErrOutput, Format(FMT_FILE_ERROR, [ExtractFileName(APath), AErrorMessage]));
end;

procedure TOSFQuietProgressReporter.Log(ALevel: TOSFLogLevel; const AMessage: string);
begin
  if ALevel = llError then
    Writeln(ErrOutput, Format(FMT_LOG_ERROR, [AMessage]));
end;

end.
