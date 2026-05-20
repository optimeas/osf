// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

// Live progress reporter for an interactive terminal. Keeps a two-line
// block (current file + progress bar) pinned at the bottom, redrawn in
// place via ANSI cursor moves; error lines are pushed permanently above
// it. INFO/WARN log noise is swallowed — only milestones and errors show.
unit OSF.Progress.Live;

interface

uses
  OSF.Log,
  OSF.Progress;

type
  TOSFLiveProgressReporter = class(TOSFProgressReporterBase)
  strict private
    FBlockDrawn: Boolean;
    FSidecarTotal: Integer;
    procedure DrawBlock(const APath: string; AIndex, ATotal: Integer);
    procedure EraseBlock;
  public
    constructor Create;
    destructor Destroy; override;
    procedure ScanStarted(const ADirectory: string); override;
    procedure ScanFinished(AFileCount: Integer); override;
    procedure SidecarStarted(ATotal: Integer); override;
    procedure SidecarProgress(ADone, ATotal: Integer); override;
    procedure SidecarFinished(ACreated: Integer); override;
    procedure ReadStarted(ATotal: Integer); override;
    procedure FileStarted(AIndex, ATotal: Integer; const APath: string); override;
    procedure FileError(AIndex: Integer; const APath, AErrorMessage: string); override;
    procedure WriteStarted(const AOutputPath: string); override;
    procedure Log(ALevel: TOSFLogLevel; const AMessage: string); override;
    procedure Summary(AFilesOk, AFilesTotal: Integer; ADurationMs: Int64;
                      AErrors, AWarnings: Integer); override;
  end;

implementation

uses
  System.SysUtils,
  OSF.Progress.Console;

const
  // ANSI control sequences (active after EnableVirtualTerminalProcessing).
  CSI             = #27'[';
  ANSI_CURSOR_UP2 = CSI + '2A';   // move the cursor up two lines
  ANSI_ERASE_DOWN = CSI + 'J';    // erase from the cursor to end of screen

  // Progress bar geometry. The block characters are written as code-point
  // literals so this source file stays pure ASCII.
  BAR_WIDTH  = 40;
  BAR_FILLED = #$2588;  // U+2588 FULL BLOCK
  BAR_EMPTY  = #$2591;  // U+2591 LIGHT SHADE

  MSG_SCAN_STARTED  = 'Scanning directory: %s';
  MSG_SCAN_FINISHED = 'Found %d OSF files.';
  MSG_SIDECAR       = 'Creating sidecar files... %d/%d';
  MSG_SIDECAR_DONE  = 'Creating sidecar files... %d/%d done.';
  MSG_READING       = 'Reading files...';
  MSG_FILE_LINE     = '  %s';
  MSG_BAR_LINE      = '  [%s] %d%% (%d/%d)';
  MSG_FILE_ERROR    = '  [ERROR] %s: %s';
  MSG_LOG_ERROR     = '  [ERROR] %s';
  MSG_WRITE_STARTED = 'Writing output: %s';

// Returns AText up to its first line break — multi-line messages would
// corrupt the two-line block accounting.
function FirstLine(const AText: string): string;
var
  I: Integer;
begin
  for I := 1 to Length(AText) do
    if (AText[I] = #13) or (AText[I] = #10) then
      Exit(Copy(AText, 1, I - 1));
  Result := AText;
end;

constructor TOSFLiveProgressReporter.Create;
begin
  inherited Create;
  EnableVirtualTerminalProcessing;
  FBlockDrawn := False;
end;

destructor TOSFLiveProgressReporter.Destroy;
begin
  // Leave the terminal tidy if the run ends with the block still pinned.
  EraseBlock;
  inherited;
end;

procedure TOSFLiveProgressReporter.DrawBlock(const APath: string;
  AIndex, ATotal: Integer);
var
  Filled, Percent: Integer;
  Bar: string;
begin
  if ATotal > 0 then
  begin
    Filled := Round(BAR_WIDTH * AIndex / ATotal);
    Percent := Round(100 * AIndex / ATotal);
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

  Writeln(Output, Format(MSG_FILE_LINE, [ShortenPath(APath)]));
  Writeln(Output, Format(MSG_BAR_LINE, [Bar, Percent, AIndex, ATotal]));
  Flush(Output);
  FBlockDrawn := True;
end;

procedure TOSFLiveProgressReporter.EraseBlock;
begin
  if not FBlockDrawn then
    Exit;
  // Cursor sits one line below the block; step up over both lines and wipe.
  Write(Output, ANSI_CURSOR_UP2 + ANSI_ERASE_DOWN);
  Flush(Output);
  FBlockDrawn := False;
end;

procedure TOSFLiveProgressReporter.ScanStarted(const ADirectory: string);
begin
  Writeln(Output, Format(MSG_SCAN_STARTED, [ADirectory]));
  Flush(Output);
end;

procedure TOSFLiveProgressReporter.ScanFinished(AFileCount: Integer);
begin
  Writeln(Output, Format(MSG_SCAN_FINISHED, [AFileCount]));
  Flush(Output);
end;

procedure TOSFLiveProgressReporter.SidecarStarted(ATotal: Integer);
begin
  FSidecarTotal := ATotal;
end;

procedure TOSFLiveProgressReporter.SidecarProgress(ADone, ATotal: Integer);
begin
  // Carriage return overwrites the running count on one line.
  Write(Output, #13 + Format(MSG_SIDECAR, [ADone, ATotal]));
  Flush(Output);
end;

procedure TOSFLiveProgressReporter.SidecarFinished(ACreated: Integer);
begin
  Writeln(Output, #13 + Format(MSG_SIDECAR_DONE, [FSidecarTotal, FSidecarTotal]));
  Flush(Output);
end;

procedure TOSFLiveProgressReporter.ReadStarted(ATotal: Integer);
begin
  Writeln(Output, MSG_READING);
  Flush(Output);
  FBlockDrawn := False;
end;

procedure TOSFLiveProgressReporter.FileStarted(AIndex, ATotal: Integer;
  const APath: string);
begin
  EraseBlock;
  DrawBlock(APath, AIndex, ATotal);
end;

procedure TOSFLiveProgressReporter.FileError(AIndex: Integer;
  const APath, AErrorMessage: string);
begin
  EraseBlock;
  Writeln(Output, Format(MSG_FILE_ERROR,
    [ExtractFileName(APath), FirstLine(AErrorMessage)]));
  Flush(Output);
end;

procedure TOSFLiveProgressReporter.Log(ALevel: TOSFLogLevel; const AMessage: string);
begin
  // INFO and WARN are swallowed in the live view; only errors get a line.
  if ALevel <> llError then
    Exit;
  EraseBlock;
  Writeln(Output, Format(MSG_LOG_ERROR, [FirstLine(AMessage)]));
  Flush(Output);
end;

procedure TOSFLiveProgressReporter.WriteStarted(const AOutputPath: string);
begin
  EraseBlock;
  Writeln(Output);
  Writeln(Output, Format(MSG_WRITE_STARTED, [AOutputPath]));
  Flush(Output);
end;

procedure TOSFLiveProgressReporter.Summary(AFilesOk, AFilesTotal: Integer;
  ADurationMs: Int64; AErrors, AWarnings: Integer);
begin
  EraseBlock;
  Writeln(Output, FormatSummaryLine(AFilesOk, AFilesTotal, ADurationMs,
                                    AErrors, AWarnings));
  Flush(Output);
end;

end.
