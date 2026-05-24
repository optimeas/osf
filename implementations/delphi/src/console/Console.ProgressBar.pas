// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

// Console.ProgressBar — minimal in-place progress bar for CLI
// applications.
//
// Self-contained: no OSF.* dependencies. Drop into any Delphi console
// program and call Start / Update / Finish from a worker thread or
// from the main loop.
//
// On an interactive TTY the bar is drawn in place using CR (plus ANSI
// virtual-terminal processing on Windows), so the same line is
// redrawn on each Update. On a redirected stdout (pipe, log file, CI)
// the redraw collapses to throttled plain "Progress: N% (i/m)" lines
// so log captures stay readable.
//
// Bar characters are Unicode block elements (U+2588 / U+2591) which
// require a UTF-8 code page on the Windows console. OsfTool sets that
// globally at startup; other callers must do likewise (or live with
// the question marks).
unit Console.ProgressBar;

interface

type
  TConsoleProgressBar = class
  strict private
    FInteractive: Boolean;
    FActive: Boolean;
    FMax: Integer;
    FStartTick: Int64;
    FLastEmitMs: Int64;
    FLastPercent: Integer;
    FLastValue: Integer;
    procedure DrawInteractive(AValue: Integer; const AMsg: string);
    procedure DrawFallback(AValue: Integer; const AMsg: string);
  public
    constructor Create;
    destructor Destroy; override;

    // Start a fresh progress sequence with the given max value. Subsequent
    // Update calls scale against it. AMsg, if non-empty, is printed as a
    // standalone line before the bar appears.
    procedure Start(AMaxValue: Integer; const AMsg: string = '');

    // Redraw the bar at the given value. AMsg, if non-empty, is appended
    // after the bar (typically a current file name or similar context).
    procedure Update(AValue: Integer; const AMsg: string = '');

    // End the progress sequence. The cursor is moved past the bar line
    // on an interactive terminal; AMsg, if non-empty, is printed as a
    // standalone line after that.
    procedure Finish(const AMsg: string = '');

    // True if the bar is being drawn in-place on an interactive terminal;
    // False on a redirected stdout (then Update emits throttled plain
    // lines instead of redrawing).
    property Interactive: Boolean read FInteractive;
  end;

// Middle-ellipsis path shortener — keeps prefix and tail, fills with
// "..." between them. Convenient for embedding long file paths in
// Update messages without overflowing the bar line.
function ShortenPath(const APath: string; AMaxLen: Integer = 100): string;

implementation

uses
  System.SysUtils,
  System.Diagnostics
{$IFDEF MSWINDOWS}
  , Winapi.Windows
{$ELSE}
  , Posix.Unistd
{$ENDIF};

const
  // Width of the bar's filled-character region (the [###----] portion).
  BAR_WIDTH = 40;
  BAR_FILLED = #$2588;  // U+2588 FULL BLOCK
  BAR_EMPTY  = #$2591;  // U+2591 LIGHT SHADE

  // Fixed display width for the bar line; longer messages get truncated,
  // shorter messages get padded with spaces so the previous content is
  // overwritten cleanly on the next redraw.
  LINE_WIDTH = 100;

  ELLIPSIS = '...';

  // Fallback throttling (when stdout is redirected): emit at most every
  // EMIT_PERCENT_STEP percentage points OR EMIT_VALUE_STEP value units,
  // but never more often than EMIT_MIN_INTERVAL_MS milliseconds. The
  // final value (AValue >= FMax) is always emitted regardless of the
  // throttle.
  EMIT_PERCENT_STEP    = 5;
  EMIT_VALUE_STEP      = 50;
  EMIT_MIN_INTERVAL_MS = 2000;

// ── Terminal detection / ANSI setup ─────────────────────────────────────

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

// ── Utility ─────────────────────────────────────────────────────────────

function ShortenPath(const APath: string; AMaxLen: Integer): string;
var
  KeepStart, KeepEnd: Integer;
begin
  if Length(APath) <= AMaxLen then
    Exit(APath);
  KeepEnd := (AMaxLen - Length(ELLIPSIS)) div 2;
  KeepStart := AMaxLen - Length(ELLIPSIS) - KeepEnd;
  Result := Copy(APath, 1, KeepStart) + ELLIPSIS +
            Copy(APath, Length(APath) - KeepEnd + 1, KeepEnd);
end;

// Fit AText into a fixed-width buffer: truncate if too long, pad with
// spaces if too short. Used so a long previous line is overwritten by a
// shorter new one without leaving residue characters behind.
function PadToWidth(const AText: string; AWidth: Integer): string;
begin
  Result := Copy(AText, 1, AWidth);
  if Length(Result) < AWidth then
    Result := Result + StringOfChar(' ', AWidth - Length(Result));
end;

// ── TConsoleProgressBar ─────────────────────────────────────────────────

constructor TConsoleProgressBar.Create;
begin
  inherited;
  FInteractive := IsConsoleTty;
  if FInteractive then
    EnableVirtualTerminalProcessing;
end;

destructor TConsoleProgressBar.Destroy;
begin
  // Leave the terminal tidy if the bar is still pinned.
  if FActive then
    Finish;
  inherited;
end;

procedure TConsoleProgressBar.Start(AMaxValue: Integer; const AMsg: string);
begin
  FActive := True;
  FMax := AMaxValue;
  FStartTick := TStopwatch.GetTimeStamp;
  FLastEmitMs := -EMIT_MIN_INTERVAL_MS;
  FLastPercent := -EMIT_PERCENT_STEP;
  FLastValue := -EMIT_VALUE_STEP;

  if AMsg <> '' then
  begin
    Writeln(Output, AMsg);
    Flush(Output);
  end;
  // Initial frame so the user sees an empty bar immediately.
  Update(0, '');
end;

procedure TConsoleProgressBar.Update(AValue: Integer; const AMsg: string);
begin
  if not FActive then
    Exit;
  if FInteractive then
    DrawInteractive(AValue, AMsg)
  else
    DrawFallback(AValue, AMsg);
end;

procedure TConsoleProgressBar.Finish(const AMsg: string);
begin
  if not FActive then
    Exit;
  FActive := False;

  if FInteractive then
  begin
    // Move past the in-place line so subsequent output starts on a
    // fresh line.
    Writeln(Output);
    Flush(Output);
  end;

  if AMsg <> '' then
  begin
    Writeln(Output, AMsg);
    Flush(Output);
  end;
end;

procedure TConsoleProgressBar.DrawInteractive(AValue: Integer; const AMsg: string);
var
  Filled, Percent: Integer;
  Bar, Line, Suffix: string;
begin
  if FMax > 0 then
  begin
    Filled := Round(BAR_WIDTH * AValue / FMax);
    Percent := Round(100 * AValue / FMax);
  end
  else
  begin
    Filled := 0;
    Percent := 0;
  end;
  if Filled < 0 then
    Filled := 0
  else if Filled > BAR_WIDTH then
    Filled := BAR_WIDTH;

  Bar := StringOfChar(BAR_FILLED, Filled) +
         StringOfChar(BAR_EMPTY, BAR_WIDTH - Filled);

  Suffix := '';
  if AMsg <> '' then
    Suffix := ' - ' + AMsg;

  Line := Format('[%s] %3d%% (%d/%d)', [Bar, Percent, AValue, FMax]) + Suffix;
  Write(Output, #13 + PadToWidth(Line, LINE_WIDTH) + #13);
  Flush(Output);
end;

procedure TConsoleProgressBar.DrawFallback(AValue: Integer; const AMsg: string);
var
  Percent: Integer;
  ElapsedMs: Int64;
  IsFinal, CrossedThreshold: Boolean;
  Line: string;
begin
  if FMax <= 0 then
    Exit;
  Percent := Round(100 * AValue / FMax);
  ElapsedMs := Round((TStopwatch.GetTimeStamp - FStartTick) /
                     TStopwatch.Frequency * 1000);
  IsFinal := AValue >= FMax;
  CrossedThreshold := ((Percent - FLastPercent) >= EMIT_PERCENT_STEP) or
                      ((AValue - FLastValue) >= EMIT_VALUE_STEP);
  if not IsFinal then
    if not (CrossedThreshold and
            ((ElapsedMs - FLastEmitMs) >= EMIT_MIN_INTERVAL_MS)) then
      Exit;

  Line := Format('Progress: %d%% (%d/%d)', [Percent, AValue, FMax]);
  if AMsg <> '' then
    Line := Line + ' - ' + AMsg;
  Writeln(Output, Line);
  Flush(Output);
  FLastPercent := Percent;
  FLastValue := AValue;
  FLastEmitMs := ElapsedMs;
end;

end.
