// Copyright 2026 Optimeas GmbH
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

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

const
  C_OSFTOOL_VERSION = '1.0.0';

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
  StdoutLine('osftool — Open Streaming Format command-line tool');
  StdoutLine('Version: ' + C_OSFTOOL_VERSION);
  StdoutLine('');
  StdoutLine('Usage:  osftool <command> [options] [arguments]');
  StdoutLine('        osftool <command> --help');
  StdoutLine('');
  StdoutLine('Commands:');
  for C in FCommands do
    StdoutLine(Format('  %-10s %s', [C.Name, C.ShortDescription]));
  StdoutLine('');
  StdoutLine('Exit codes: 0=ok  1=bad args  2=not found  3=io error  4=format error');
end;

function TOsfToolDispatcher.Run(const AArgs: TArray<string>): Integer;
var
  CmdName: string;
  C: IOsfCommand;
  Rest: TArray<string>;
  I: Integer;
begin
  if Length(AArgs) = 0 then
  begin
    PrintGlobalHelp;
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
