// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

// Console helpers shared by the progress reporters: terminal detection,
// ANSI virtual-terminal activation, and middle-ellipsis path shortening.
unit OSF.Progress.Console;

interface

uses
  OSF.Log;

const
  // Width the live display shortens file paths to.
  PROGRESS_PATH_MAX_LEN = 80;

// Five-character level tag used in "[LEVEL] message" log lines, matching
// the format osftool has always written.
function LogLevelTag(ALevel: TOSFLogLevel): string;

// Shortens APath to at most AMaxLen characters by replacing the middle with
// "..."; paths already within the limit are returned unchanged.
function ShortenPath(const APath: string; AMaxLen: Integer = PROGRESS_PATH_MAX_LEN): string;

// True when standard output is an interactive terminal (not a pipe/file).
function IsConsoleTty: Boolean;

// Enables ANSI escape processing on the Windows console. No-op elsewhere
// and harmless when stdout is redirected.
procedure EnableVirtualTerminalProcessing;

// Formats a millisecond duration as "12.4s", or "2m 14s" beyond a minute.
function FormatDuration(AMilliseconds: Int64): string;

// Builds the final-summary line, e.g.
//   "Done. Merged 344 of 346 files in 12.4s (2 errors, 5 warnings)."
// The parenthesis is omitted when there are no errors and no warnings.
function FormatSummaryLine(AFilesOk, AFilesTotal: Integer; ADurationMs: Int64;
                           AErrors, AWarnings: Integer): string;

implementation

uses
  System.SysUtils
  {$IFDEF MSWINDOWS}
  , Winapi.Windows
  {$ELSE}
  , Posix.Unistd
  {$ENDIF};

const
  ELLIPSIS = '...';
  LEVEL_TAGS: array[TOSFLogLevel] of string = ('DEBUG', 'INFO ', 'WARN ', 'ERROR');

resourcestring
  MSG_SUMMARY         = 'Done. Merged %d of %d files in %s';
  MSG_SUMMARY_ERRWARN = ' (%d errors, %d warnings)';
  MSG_SUMMARY_WARN    = ' (%d warnings)';

function LogLevelTag(ALevel: TOSFLogLevel): string;
begin
  Result := LEVEL_TAGS[ALevel];
end;

function ShortenPath(const APath: string; AMaxLen: Integer): string;
var
  KeepStart, KeepEnd: Integer;
begin
  if Length(APath) <= AMaxLen then
    Exit(APath);
  // Split the budget left after the ellipsis between head and tail.
  KeepEnd := (AMaxLen - Length(ELLIPSIS)) div 2;
  KeepStart := AMaxLen - Length(ELLIPSIS) - KeepEnd;
  Result := Copy(APath, 1, KeepStart) + ELLIPSIS +
            Copy(APath, Length(APath) - KeepEnd + 1, KeepEnd);
end;

function IsConsoleTty: Boolean;
{$IFDEF MSWINDOWS}
var
  Handle: THandle;
  Mode: DWORD;
begin
  Handle := GetStdHandle(STD_OUTPUT_HANDLE);
  Result := (Handle <> INVALID_HANDLE_VALUE) and (Handle <> 0)
            and (GetFileType(Handle) = FILE_TYPE_CHAR)
            and GetConsoleMode(Handle, Mode);
end;
{$ELSE}
begin
  Result := isatty(STDOUT_FILENO) <> 0;
end;
{$ENDIF}

procedure EnableVirtualTerminalProcessing;
{$IFDEF MSWINDOWS}
const
  ENABLE_VIRTUAL_TERMINAL_PROCESSING_FLAG = $0004;
var
  Handle: THandle;
  Mode: DWORD;
begin
  Handle := GetStdHandle(STD_OUTPUT_HANDLE);
  if (Handle <> INVALID_HANDLE_VALUE) and GetConsoleMode(Handle, Mode) then
    SetConsoleMode(Handle, Mode or ENABLE_VIRTUAL_TERMINAL_PROCESSING_FLAG);
end;
{$ELSE}
begin
  // POSIX terminals process ANSI escapes natively.
end;
{$ENDIF}

function FormatDuration(AMilliseconds: Int64): string;
begin
  // Invariant settings: the decimal point must be a dot regardless of the
  // machine locale.
  if AMilliseconds < 60000 then
    Result := Format('%.1fs', [AMilliseconds / 1000], TFormatSettings.Invariant)
  else
    Result := Format('%dm %ds', [AMilliseconds div 60000,
                                 (AMilliseconds div 1000) mod 60]);
end;

function FormatSummaryLine(AFilesOk, AFilesTotal: Integer; ADurationMs: Int64;
  AErrors, AWarnings: Integer): string;
begin
  Result := Format(MSG_SUMMARY,
    [AFilesOk, AFilesTotal, FormatDuration(ADurationMs)]);
  if AErrors > 0 then
    Result := Result + Format(MSG_SUMMARY_ERRWARN, [AErrors, AWarnings])
  else if AWarnings > 0 then
    Result := Result + Format(MSG_SUMMARY_WARN, [AWarnings]);
  Result := Result + '.';
end;

end.
