// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

// osftool command dispatcher.
//
// Single entry point used by the .dpr: registers every verb command,
// matches ParamStr(1) against the published Name, and forwards the
// remaining arguments to the matching command's Execute method.
// Returns an exit code (see Cmd.Base.EXIT_*) that the .dpr assigns to
// ExitCode.
unit OsfTool.Dispatcher;

interface

uses
  System.SysUtils,
  Cmd.Base;

type
  TOsfToolDispatcher = class
  strict private
    FCommands: TArray<IOsfCommand>;
    procedure RegisterDefaults;
    function FindCommand(const AName: string): IOsfCommand;
    procedure PrintGlobalHelp;
  public
    constructor Create;

    // Top-level entry. AArgs is everything from the command line except
    // the program name. Returns the exit code to propagate to the OS.
    function Run(const AArgs: TArray<string>): Integer;

    property Commands: TArray<IOsfCommand> read FCommands;
  end;

implementation

uses
  System.StrUtils,
  OSF.Version,
  Cmd.Merge,
  Cmd.Export,
  Cmd.Info,
  Cmd.Channels,
  Cmd.Stat,
  Cmd.Cache,
  Cmd.Config,
  Cmd.Convert,
  Cmd.Verify;

constructor TOsfToolDispatcher.Create;
begin
  inherited Create;
  RegisterDefaults;
end;

procedure TOsfToolDispatcher.RegisterDefaults;
begin
  // Order is the human-help order. Keep it stable; users grep it.
  FCommands := [
    TOsfMergeCommand.Create     as IOsfCommand,
    TOsfExportCommand.Create    as IOsfCommand,
    TOsfInfoCommand.Create      as IOsfCommand,
    TOsfChannelsCommand.Create  as IOsfCommand,
    TOsfStatCommand.Create      as IOsfCommand,
    TOsfCacheCommand.Create     as IOsfCommand,
    TOsfConfigCommand.Create    as IOsfCommand,
    TOsfConvertCommand.Create   as IOsfCommand,
    TOsfVerifyCommand.Create    as IOsfCommand
  ];
end;

function TOsfToolDispatcher.FindCommand(const AName: string): IOsfCommand;
var
  C: IOsfCommand;
begin
  for C in FCommands do
    if SameText(C.Name, AName) then
      Exit(C);
  Result := nil;
end;

procedure TOsfToolDispatcher.PrintGlobalHelp;
var
  C: IOsfCommand;
begin
  StdoutLine('osftool - Open Streaming Format command-line tool');
  StdoutLine('Version: ' + GetVersionString);
  StdoutLine('');
  StdoutLine('Usage:  osftool <command> [options] [arguments]');
  StdoutLine('        osftool <command> --help');
  StdoutLine('');
  StdoutLine('Commands:');
  for C in FCommands do
    StdoutLine(Format('  %-10s %s', [C.Name, C.ShortDescription]));
  StdoutLine('');
  StdoutLine('Global options:');
  StdoutLine('  -h, --help     Show this help message');
  StdoutLine('  -V, --version  Show version information');
  StdoutLine('      --short    With --version: print only the version number');
  StdoutLine('');
  StdoutLine('Exit codes: 0=ok  1=bad args  2=not found  3=io error  4=format error');
end;

function TOsfToolDispatcher.Run(const AArgs: TArray<string>): Integer;
var
  CmdName: string;
  C: IOsfCommand;
  Rest: TArray<string>;
  I: Integer;
  ShortVersion: Boolean;
begin
  if Length(AArgs) = 0 then
  begin
    PrintGlobalHelp;
    Exit(EXIT_OK);
  end;

  // Top-level --version / -V, analogous to --help. -V is matched
  // case-sensitively so it never collides with a verb's lowercase -v.
  if SameText(AArgs[0], '--version') or (AArgs[0] = '-V') then
  begin
    ShortVersion := False;
    for I := 1 to High(AArgs) do
      if SameText(AArgs[I], '--short') then
        ShortVersion := True;
    if ShortVersion then
      StdoutLine(GetVersionString)
    else
      StdoutLine(GetFullVersionString);
    Exit(EXIT_OK);
  end;

  CmdName := AArgs[0];
  if SameText(CmdName, '--help') or SameText(CmdName, '-h') or SameText(CmdName, '-?') then
  begin
    PrintGlobalHelp;
    Exit(EXIT_OK);
  end;

  C := FindCommand(CmdName);
  if not Assigned(C) then
  begin
    StderrLine('osftool: unknown command: ' + CmdName);
    StderrLine('Run "osftool --help" for usage.');
    Exit(EXIT_BAD_ARGS);
  end;

  // If the verb is followed by --help, the command prints its own help
  // and exits 0 without doing any work.
  for I := 1 to High(AArgs) do
    if SameText(AArgs[I], '--help') or SameText(AArgs[I], '-h') or SameText(AArgs[I], '-?') then
    begin
      C.PrintHelp;
      Exit(EXIT_OK);
    end;

  SetLength(Rest, Length(AArgs) - 1);
  for I := 1 to High(AArgs) do
    Rest[I - 1] := AArgs[I];

  Result := C.Execute(Rest);
end;

end.
