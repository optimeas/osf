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
    FSidecarFinished : Boolean;
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
    procedure WriteFinished(const AOutputPath: string; ABytes: Int64); override;
    procedure Log(ALevel: TOSFLogLevel; const AMessage: string); override;
    procedure Summary(AFilesOk, AFilesTotal: Integer; ADurationMs: Int64;
                      AErrors, AWarnings: Integer); override;
    procedure StartProgress(const AText: string; AIndex, ATotal: Integer); override;
    procedure DoProgress(const AText: string; AIndex, ATotal: Integer); override;
    procedure EndProgress; override;
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

  // Layout / severity-marker frames - not translatable prose.
  MSG_FILE_LINE  = '  %s';
  MSG_BAR_LINE   = '  [%s] %d%% (%d/%d)';
  MSG_FILE_ERROR = '  [ERROR] %s: %s';
  MSG_LOG_ERROR  = '  [ERROR] %s';
  MSG_LOG_WARNING  = '  [WARNING] %s';

resourcestring
  MSG_SCAN_STARTED  = 'Scanning directory: %s';
  MSG_SCAN_FINISHED = 'Found %d OSF files.';
  MSG_SIDECAR_STARTED = 'Reading file Information from %d files...';
  MSG_SIDECAR_DONE  = 'Done. %d new sidecar files created.';
  MSG_READING       = 'Reading files...';
  MSG_WRITE_STARTED = 'Writing output: %s';
  MSG_WRITE_FINISHED= 'File %s created. File size is %s %s';

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
  FSidecarFinished := false;
  EndProgress;
  // Leave the terminal tidy if the run ends with the block still pinned.
  inherited;
end;

procedure TOSFLiveProgressReporter.DoProgress(const AText: string; AIndex,
  ATotal: Integer);
var
  Filled, Percent: Integer;
  Bar, S: string;
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

  S := AText;
  if S.Length>0 then
    S := ' - '+S;

  Write(Output, #13 + MakeConsoleString( Format(MSG_BAR_LINE, [Bar, Percent, AIndex, ATotal])+S) + #13);
  Flush(Output);
  FBlockDrawn := True;
end;

procedure TOSFLiveProgressReporter.EndProgress;
begin
  Writeln;
  Flush(Output);
end;

procedure TOSFLiveProgressReporter.ScanStarted(const ADirectory: string);
begin
  Writeln(Output, Format(MSG_SCAN_STARTED, [ADirectory]));
  Flush(Output);

  //HideCursor;
  Write(#27+'[?25l');
  Flush(Output);
end;

procedure TOSFLiveProgressReporter.ScanFinished(AFileCount: Integer);
begin
  Writeln(Output, Format(MSG_SCAN_FINISHED, [AFileCount]));
  Flush(Output);

  //ShowCursor;
  Write(#27+'[?25h');
  Flush(Output);
end;

procedure TOSFLiveProgressReporter.SidecarStarted(ATotal: Integer);
begin
  FSidecarTotal := ATotal;
  FSidecarFinished := False;

  Writeln(Output, Format(MSG_SIDECAR_STARTED, [ATotal]));
  Flush(Output);

  StartProgress( '', 0, ATotal);
end;

procedure TOSFLiveProgressReporter.StartProgress(const AText: string; AIndex,
  ATotal: Integer);
begin
  Writeln;
  Flush(Output);
  DoProgress(AText, AIndex, ATotal);
end;

procedure TOSFLiveProgressReporter.SidecarProgress(ADone, ATotal: Integer);
begin
  DoProgress( '', ADone, ATotal);
end;

procedure TOSFLiveProgressReporter.SidecarFinished(ACreated: Integer);
begin
  FSidecarFinished := true;
  EndProgress;

  Writeln(Output, #13 + Format(MSG_SIDECAR_DONE, [ACreated]));
  Flush(Output);
end;

procedure TOSFLiveProgressReporter.ReadStarted(ATotal: Integer);
begin
  Writeln(Output, MSG_READING);
  Flush(Output);
  StartProgress( '', 0, ATotal);
end;

procedure TOSFLiveProgressReporter.FileStarted(AIndex, ATotal: Integer;
  const APath: string);
begin
  DoProgress(ExtractFilename(APath), AIndex, ATotal);
end;

procedure TOSFLiveProgressReporter.FileError(AIndex: Integer;
  const APath, AErrorMessage: string);
begin
  Writeln(Output, Format(MSG_FILE_ERROR,
    [ExtractFileName(APath), FirstLine(AErrorMessage)]));
  Flush(Output);
end;

procedure TOSFLiveProgressReporter.Log(ALevel: TOSFLogLevel; const AMessage: string);
begin
  // Warnings and errors always get a line; INFO is surfaced only once the
  // sidecar phase is over; DEBUG is always swallowed in the live view.
  if (ALevel < llWarning) and not (FSidecarFinished and (ALevel = llInfo)) then
    Exit;
  if ALevel = llWarning then
    Writeln(Output, MakeConsoleString( Format(MSG_LOG_WARNING, [FirstLine(AMessage)])))
  else if ALevel = llError then
    Writeln(Output, MakeConsoleString( Format(MSG_LOG_ERROR, [FirstLine(AMessage)])))

  //Write all Info Messages
  else if FSidecarFinished and (ALevel = llInfo) then
    Writeln(Output, MakeConsoleString( AMessage));

  Flush(Output);
end;

procedure TOSFLiveProgressReporter.WriteFinished(const AOutputPath: string;
  ABytes: Int64);
begin
  Writeln(Output, Format(MSG_WRITE_FINISHED, [AOutputPath, IntToStr( ABytes div 1024), 'kB']));
  Flush(Output);
end;

procedure TOSFLiveProgressReporter.WriteStarted(const AOutputPath: string);
begin
  EndProgress;
  Writeln(Output);
  Writeln(Output, Format(MSG_WRITE_STARTED, [AOutputPath]));
  Flush(Output);
end;

procedure TOSFLiveProgressReporter.Summary(AFilesOk, AFilesTotal: Integer;
  ADurationMs: Int64; AErrors, AWarnings: Integer);
begin
  Writeln(Output, FormatSummaryLine(AFilesOk, AFilesTotal, ADurationMs,
                                    AErrors, AWarnings));
  Flush(Output);
end;

end.
