// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

// osftool command interface and shared base class.
//
// Every osftool verb (merge, export, info, ...) is implemented as a
// TBaseCommand subclass that publishes its Name, ShortDescription, an
// Execute method and a PrintHelp method. The TOsfToolDispatcher in
// OsfTool.pas iterates a list of these instances to route arguments.
//
// Output policy lives here (Print, PrintErr, PrintJson) so every
// command shares the same stdout/stderr conventions and respects the
// global --json / --quiet / --verbose / --log switches identically.
//
// Logging policy: the OSF library emits log + progress events via the
// global Logger (OSF.Log unit). TBaseCommand.Execute registers one
// console listener (filtered by the output-mode flags) and, optionally,
// one file listener (--log <path>, always captures down to debug).
// Both listeners are torn down in the Execute finally.
unit Cmd.Base;

interface

uses
  System.SysUtils,
  System.Classes,
  OSF.Log,
  Console.ProgressBar;

const
  // Exit codes shared across every command. The dispatcher also returns
  // these from its main loop.
  EXIT_OK            = 0;
  EXIT_BAD_ARGS      = 1;
  EXIT_NOT_FOUND     = 2;
  EXIT_IO_ERROR      = 3;
  EXIT_FORMAT_ERROR  = 4;

type
  IOsfCommand = interface
    ['{6F2C8E14-8B6F-4F71-93A6-AE9D5C2C7A11}']
    function Name: string;
    function ShortDescription: string;
    function Execute(const AArgs: TArray<string>): Integer;
    procedure PrintHelp;
  end;

  TBaseCommand = class(TInterfacedObject, IOsfCommand)
  strict private
    FArgs: TArray<string>;
    FListener: TLoggerListener;
    FFileListener: TLoggerListener;
    FFileStream: TStreamWriter;
    FProgressBar: TConsoleProgressBar;
  protected
    FJson: Boolean;
    FQuiet: Boolean;
    FVerbose: Boolean;
    FLogPath: string;

    // Argument helpers. Lookups are case-insensitive on the flag spelling.
    function HasFlag(const AFlag: string): Boolean;
    // For --flag <value> patterns. If the flag is not present, ADefault is
    // returned. If the flag has no following argument, ADefault is returned.
    function FlagValue(const AFlag: string; const ADefault: string = ''): string;
    // Subset of FArgs that are positional (no leading '--') and not a
    // value consumed by a known value-taking flag.
    function PositionalArgs(const AValueFlags: array of string): TArray<string>;

    // Output helpers. Print writes to stdout, PrintErr to stderr. Both
    // append a line terminator. PrintJson is for the --json mode and is
    // never suppressed by --quiet. Print is suppressed by --quiet and
    // by --json (the JSON event stream is the only stdout in that mode).
    procedure Print(const AMsg: string);
    procedure Printf(const AFmt: string; const AArgs: array of const);
    procedure PrintErr(const AMsg: string);
    procedure PrintErrf(const AFmt: string; const AArgs: array of const);
    procedure PrintJson(const AJson: string);

    // Listener setup / teardown — called by Execute around DoExecute.
    // Subclasses do not normally override these; they override the per-
    // event callbacks below if they need command-specific routing.
    procedure SetupListeners; virtual;
    procedure TeardownListeners; virtual;

    // Default callbacks installed on the console listener. Subclasses
    // may override to add custom routing; the defaults render to
    // stderr (or to JSON on stdout in --json mode) and drive the
    // FProgressBar in interactive default mode.
    procedure OnLogMessage(const AMsg: string; ALevel: TOSFLogLevel;
                           const ASender: string); virtual;
    procedure OnProgressStart(MaxValue: Integer; const AMsg: string); virtual;
    procedure OnProgress(Value: Integer; const AMsg: string); virtual;
    procedure OnProgressEnd(const AMsg: string); virtual;

    // Callback installed on the optional file listener. Subclasses may
    // override to change the on-disk format; the default emits one line
    // per message with an ISO-8601 timestamp prefix.
    procedure OnFileLogMessage(const AMsg: string; ALevel: TOSFLogLevel;
                               const ASender: string); virtual;
  public
    function Name: string; virtual; abstract;
    function ShortDescription: string; virtual; abstract;
    // Parses the global flags (--json / --quiet / --verbose / --log) into
    // FJson / FQuiet / FVerbose / FLogPath, sets up listeners, dispatches
    // to DoExecute, and tears the listeners down again. Subclasses
    // override DoExecute and never override Execute.
    function Execute(const AArgs: TArray<string>): Integer; virtual;
    procedure PrintHelp; virtual; abstract;

    // Concrete commands override this. The base Execute has already
    // parsed the global flags and registered listeners by the time
    // DoExecute is called.
    function DoExecute: Integer; virtual; abstract;

    // Direct access for subclasses (PositionalArgs / FlagValue derive
    // from this). The array preserves order and includes flags.
    property RawArgs: TArray<string> read FArgs;
  end;

// stdout / stderr line writers — exposed so the dispatcher (which is
// not a TBaseCommand) can emit messages with the same formatting.
procedure StdoutLine(const AMsg: string);
procedure StderrLine(const AMsg: string);

implementation

uses
  System.StrUtils,
  System.JSON;

const
  C_LEVEL_TAG: array[TOSFLogLevel] of string =
    ('DEBUG', 'INFO ', 'USER ', 'WARN ', 'ERROR');

procedure StdoutLine(const AMsg: string);
begin
  Writeln(Output, AMsg);
end;

procedure StderrLine(const AMsg: string);
begin
  Writeln(ErrOutput, AMsg);
end;

// ── TBaseCommand — lifecycle ─────────────────────────────────────────────────

function TBaseCommand.Execute(const AArgs: TArray<string>): Integer;
var
  I: Integer;
begin
  FArgs := AArgs;
  FJson := False;
  FQuiet := False;
  FVerbose := False;
  FLogPath := '';

  for I := 0 to High(AArgs) do
  begin
    if SameText(AArgs[I], '--json') then
      FJson := True
    else if SameText(AArgs[I], '--quiet') or SameText(AArgs[I], '-q') then
      FQuiet := True
    else if SameText(AArgs[I], '--verbose') or SameText(AArgs[I], '-v') then
      FVerbose := True
    else if SameText(AArgs[I], '--log') and (I + 1 <= High(AArgs)) then
      FLogPath := AArgs[I + 1];
  end;

  SetupListeners;
  try
    Result := DoExecute;
  finally
    TeardownListeners;
  end;
end;

procedure TBaseCommand.SetupListeners;
begin
  // Console listener: visible level depends on the output-mode flag.
  //   --quiet   -> Warning + Error only
  //   --verbose -> everything down to Debug
  //   --json    -> Info + User + Warning + Error (rendered as JSON)
  //   default   -> User + Warning + Error
  FListener := TLoggerListener.Create;
  if FJson then
    FListener.MinLevel := llInfo
  else if FVerbose then
    FListener.MinLevel := llDebug
  else if FQuiet then
    FListener.MinLevel := llWarning
  else
    FListener.MinLevel := llUser;
  FListener.OnAddLogMessage := OnLogMessage;
  FListener.OnStartProgress := OnProgressStart;
  FListener.OnDoProgress    := OnProgress;
  FListener.OnEndProgress   := OnProgressEnd;
  Logger.RegisterListener(FListener);

  // Progress bar only in interactive default mode. The bar would
  // interfere with --verbose stderr noise, is silent under --quiet,
  // and is replaced by JSON events under --json.
  if not (FQuiet or FVerbose or FJson) then
    FProgressBar := TConsoleProgressBar.Create;

  // Optional file listener — always captures everything down to debug.
  if FLogPath <> '' then
  begin
    try
      FFileStream := TStreamWriter.Create(FLogPath, False, TEncoding.UTF8);
      FFileStream.AutoFlush := True;
      FFileListener := TLoggerListener.Create;
      FFileListener.MinLevel := llDebug;
      FFileListener.OnAddLogMessage := OnFileLogMessage;
      Logger.RegisterListener(FFileListener);
    except
      on E: Exception do
      begin
        FreeAndNil(FFileListener);
        FreeAndNil(FFileStream);
        PrintErrf('Cannot open log file "%s": %s', [FLogPath, E.Message]);
      end;
    end;
  end;
end;

procedure TBaseCommand.TeardownListeners;
begin
  if FListener <> nil then
    Logger.UnregisterListener(FListener);
  FreeAndNil(FListener);

  if FFileListener <> nil then
    Logger.UnregisterListener(FFileListener);
  FreeAndNil(FFileListener);
  FreeAndNil(FFileStream);

  FreeAndNil(FProgressBar);
end;

// ── TBaseCommand — flag helpers ──────────────────────────────────────────────

function TBaseCommand.HasFlag(const AFlag: string): Boolean;
var
  S: string;
begin
  for S in FArgs do
    if SameText(S, AFlag) then
      Exit(True);
  Result := False;
end;

function TBaseCommand.FlagValue(const AFlag: string; const ADefault: string): string;
var
  I: Integer;
begin
  for I := 0 to High(FArgs) - 1 do
    if SameText(FArgs[I], AFlag) then
      Exit(FArgs[I + 1]);
  Result := ADefault;
end;

function TBaseCommand.PositionalArgs(const AValueFlags: array of string): TArray<string>;
var
  Out: TList;
  Pos: TArray<string>;
  I, J: Integer;
  Skip: Boolean;
  IsValueFlag: Boolean;
begin
  SetLength(Pos, Length(FArgs));
  for I := 0 to High(Pos) do Pos[I] := FArgs[I];

  Out := TList.Create;
  try
    I := 0;
    while I < Length(Pos) do
    begin
      Skip := False;
      if (Length(Pos[I]) >= 2) and (Pos[I][1] = '-') and (Pos[I][2] = '-') then
      begin
        IsValueFlag := False;
        for J := 0 to High(AValueFlags) do
          if SameText(Pos[I], AValueFlags[J]) then
          begin
            IsValueFlag := True;
            Break;
          end;
        if IsValueFlag and (I + 1 < Length(Pos)) then
          Inc(I);
        Skip := True;
      end;
      if not Skip then
        Out.Add(Pointer(NativeInt(I)));
      Inc(I);
    end;

    SetLength(Result, Out.Count);
    for I := 0 to Out.Count - 1 do
      Result[I] := Pos[NativeInt(Out[I])];
  finally
    Out.Free;
  end;
end;

// ── TBaseCommand — output helpers ────────────────────────────────────────────

procedure TBaseCommand.Print(const AMsg: string);
begin
  // --quiet suppresses ordinary command output. --json redirects all
  // stdout to the JSON event stream; arbitrary Print calls would
  // pollute that stream, so they are suppressed too.
  if FQuiet or FJson then
    Exit;
  StdoutLine(AMsg);
end;

procedure TBaseCommand.Printf(const AFmt: string; const AArgs: array of const);
begin
  Print(Format(AFmt, AArgs));
end;

procedure TBaseCommand.PrintErr(const AMsg: string);
begin
  StderrLine(AMsg);
end;

procedure TBaseCommand.PrintErrf(const AFmt: string; const AArgs: array of const);
begin
  PrintErr(Format(AFmt, AArgs));
end;

procedure TBaseCommand.PrintJson(const AJson: string);
begin
  // --quiet is intentionally ignored here: --json --quiet is meaningless,
  // and consumers parsing stdout expect the JSON document to be present.
  StdoutLine(AJson);
end;

// ── TBaseCommand — listener callbacks ────────────────────────────────────────

procedure TBaseCommand.OnLogMessage(const AMsg: string; ALevel: TOSFLogLevel;
  const ASender: string);
var
  Obj: TJSONObject;
begin
  if FJson then
  begin
    Obj := TJSONObject.Create;
    try
      Obj.AddPair('event', 'log');
      Obj.AddPair('level', LowerCase(Trim(C_LEVEL_TAG[ALevel])));
      Obj.AddPair('msg', AMsg);
      if ASender <> '' then
        Obj.AddPair('sender', ASender);
      PrintJson(Obj.ToString);
    finally
      Obj.Free;
    end;
    Exit;
  end;
  PrintErr(Format('[%s] %s', [C_LEVEL_TAG[ALevel], AMsg]));
end;

procedure TBaseCommand.OnProgressStart(MaxValue: Integer; const AMsg: string);
var
  Obj: TJSONObject;
begin
  if FJson then
  begin
    Obj := TJSONObject.Create;
    try
      Obj.AddPair('event', 'progress_start');
      Obj.AddPair('max', TJSONNumber.Create(MaxValue));
      if AMsg <> '' then
        Obj.AddPair('msg', AMsg);
      PrintJson(Obj.ToString);
    finally
      Obj.Free;
    end;
    Exit;
  end;
  if FProgressBar <> nil then
    FProgressBar.Start(MaxValue, AMsg)
  else if FVerbose and (AMsg <> '') then
    PrintErr(AMsg);
end;

procedure TBaseCommand.OnProgress(Value: Integer; const AMsg: string);
var
  Obj: TJSONObject;
begin
  if FJson then
  begin
    Obj := TJSONObject.Create;
    try
      Obj.AddPair('event', 'progress');
      Obj.AddPair('value', TJSONNumber.Create(Value));
      if AMsg <> '' then
        Obj.AddPair('msg', AMsg);
      PrintJson(Obj.ToString);
    finally
      Obj.Free;
    end;
    Exit;
  end;
  if FProgressBar <> nil then
    FProgressBar.Update(Value, AMsg);
end;

procedure TBaseCommand.OnProgressEnd(const AMsg: string);
var
  Obj: TJSONObject;
begin
  if FJson then
  begin
    Obj := TJSONObject.Create;
    try
      Obj.AddPair('event', 'progress_end');
      if AMsg <> '' then
        Obj.AddPair('msg', AMsg);
      PrintJson(Obj.ToString);
    finally
      Obj.Free;
    end;
    Exit;
  end;
  if FProgressBar <> nil then
    FProgressBar.Finish(AMsg)
  else if FVerbose and (AMsg <> '') then
    PrintErr(AMsg);
end;

procedure TBaseCommand.OnFileLogMessage(const AMsg: string; ALevel: TOSFLogLevel;
  const ASender: string);
var
  TS: string;
begin
  if FFileStream = nil then
    Exit;
  TS := FormatDateTime('yyyy-mm-dd"T"hh:nn:ss.zzz', Now);
  if ASender <> '' then
    FFileStream.WriteLine(Format('%s [%s] %s: %s',
      [TS, C_LEVEL_TAG[ALevel], ASender, AMsg]))
  else
    FFileStream.WriteLine(Format('%s [%s] %s',
      [TS, C_LEVEL_TAG[ALevel], AMsg]));
end;

end.
