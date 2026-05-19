// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

// osftool command interface and shared base class.
//
// Every osftool verb (merge, export, info, ...) is implemented as a
// TBaseCommand subclass that publishes its Name, ShortDescription, an
// Execute method and a PrintHelp method. The TOsfToolDispatcher in
// OsfTool.pas iterates a list of these instances to route arguments.
//
// Output policy lives here (Print, PrintErr, PrintJson, HandleLog) so
// every command shares the same stdout/stderr conventions and respects
// the global --json / --quiet / --verbose switches identically.
unit Cmd.Base;

interface

uses
  System.SysUtils,
  System.Classes,
  OSF.Log;

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
  protected
    FJson: Boolean;
    FQuiet: Boolean;
    FVerbose: Boolean;

    // Argument helpers. Lookups are case-insensitive on the flag spelling.
    function HasFlag(const AFlag: string): Boolean;
    // For --flag <value> patterns. If the flag is not present, ADefault is
    // returned. If the flag has no following argument, ADefault is returned.
    function FlagValue(const AFlag: string; const ADefault: string = ''): string;
    // Subset of FArgs that are positional (no leading '--') and not a
    // value consumed by a known value-taking flag.
    function PositionalArgs(const AValueFlags: array of string): TArray<string>;

    // Output helpers. Print writes to stdout, PrintErr to stderr. Both
    // append a line terminator. PrintJson is for the --json mode.
    procedure Print(const AMsg: string);
    procedure Printf(const AFmt: string; const AArgs: array of const);
    procedure PrintErr(const AMsg: string);
    procedure PrintErrf(const AFmt: string; const AArgs: array of const);
    procedure PrintJson(const AJson: string);

    // Log handler installed on TOSFLoggable.OnLog for OSF units. Forwards
    // to stderr; llDebug is suppressed unless --verbose is set.
    procedure HandleLog(ALevel: TOSFLogLevel; const AMsg: string);
  public
    function Name: string; virtual; abstract;
    function ShortDescription: string; virtual; abstract;
    // Parses --json / --quiet / --verbose into FJson / FQuiet / FVerbose,
    // stores AArgs in FArgs, then dispatches to DoExecute. Subclasses
    // override DoExecute and never override Execute.
    function Execute(const AArgs: TArray<string>): Integer; virtual;
    procedure PrintHelp; virtual; abstract;

    // Concrete commands override this. The base Execute has already
    // peeled off the global flags by the time DoExecute is called.
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
  System.StrUtils;

procedure StdoutLine(const AMsg: string);
begin
  Writeln(Output, AMsg);
end;

procedure StderrLine(const AMsg: string);
begin
  Writeln(ErrOutput, AMsg);
end;

// ── TBaseCommand ─────────────────────────────────────────────────────────────

function TBaseCommand.Execute(const AArgs: TArray<string>): Integer;
var
  I: Integer;
begin
  FArgs := AArgs;
  FJson := False;
  FQuiet := False;
  FVerbose := False;

  for I := 0 to High(AArgs) do
  begin
    if SameText(AArgs[I], '--json') then
      FJson := True
    else if SameText(AArgs[I], '--quiet') then
      FQuiet := True
    else if SameText(AArgs[I], '--verbose') then
      FVerbose := True;
  end;

  // --quiet and --json are mutually compatible: --json wins for stdout,
  // --quiet still suppresses any human prose. --verbose only ever adds to
  // stderr, never to stdout, so it composes with both.
  Result := DoExecute;
end;

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
  // First pass: collect indices of every position that follows a known
  // value-taking flag. Those positions are arguments to flags, not
  // positionals.
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
        // It is a flag. If it is in AValueFlags, the next argument
        // belongs to it. Either way the flag itself is not positional.
        IsValueFlag := False;
        for J := 0 to High(AValueFlags) do
          if SameText(Pos[I], AValueFlags[J]) then
          begin
            IsValueFlag := True;
            Break;
          end;
        if IsValueFlag and (I + 1 < Length(Pos)) then
          Inc(I); // also skip the value
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

procedure TBaseCommand.Print(const AMsg: string);
begin
  if FQuiet then
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

procedure TBaseCommand.HandleLog(ALevel: TOSFLogLevel; const AMsg: string);
const
  C_LEVEL: array[TOSFLogLevel] of string = ('DEBUG', 'INFO ', 'WARN ', 'ERROR');
begin
  // --verbose required to surface llDebug. --quiet suppresses both
  // llDebug and llInfo (warnings and errors always go through so the
  // user is not surprised by a silent failure).
  if (ALevel = llDebug) and (not FVerbose) then
    Exit;
  if FQuiet and (ALevel = llInfo) then
    Exit;
  PrintErr(Format('[%s] %s', [C_LEVEL[ALevel], AMsg]));
end;

end.
