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

// "config" verb. Inspect and mutate the persistent JSON config file
// used as the default-value source for every other command.
unit Cmd.Config;

interface

uses
  System.SysUtils,
  Cmd.Base,
  OsfToolConfig;

type
  TOsfConfigCommand = class(TBaseCommand)
  strict private
    function RunShow: Integer;
    function RunSet(const AKey, AValue: string): Integer;
    function RunReset: Integer;
  public
    function Name: string; override;
    function ShortDescription: string; override;
    function DoExecute: Integer; override;
    procedure PrintHelp; override;
  end;

implementation

function TOsfConfigCommand.Name: string;
begin
  Result := 'config';
end;

function TOsfConfigCommand.ShortDescription: string;
begin
  Result := 'View and edit default settings';
end;

procedure TOsfConfigCommand.PrintHelp;
begin
  Print('osftool config                       Show all current settings');
  Print('osftool config set <key> <value>     Set a value');
  Print('osftool config reset                 Reset all to defaults');
  Print('osftool config --json                Show settings as JSON');
  Print('');
  Print('Keys and defaults:');
  Print('  output.format          osf5          Default output format for merge/convert');
  Print('  output.overlap         skip          Overlap strategy: skip or overwrite');
  Print('  export.decimal_sep     ,             CSV decimal separator');
  Print('  export.encoding        iso-8859-1    CSV encoding');
  Print('  cache.enabled          true          Use .json sidecar files');
  Print('  cache.auto_build       true          Auto-build cache during scan');
end;

function TOsfConfigCommand.RunShow: Integer;
var
  Cfg: TOsfToolConfig;
  K: string;
begin
  Cfg := TOsfToolConfig.Create;
  try
    Cfg.Load;
    if FJson then
      PrintJson(Cfg.AsJson)
    else
    begin
      for K in Cfg.Keys do
        Printf('  %-22s = %s', [K, Cfg.Get(K)]);
      Print('');
      Printf('Config file: %s', [TOsfToolConfig.ConfigFilePath]);
    end;
  finally
    Cfg.Free;
  end;
  Result := EXIT_OK;
end;

function TOsfConfigCommand.RunSet(const AKey, AValue: string): Integer;
var
  Cfg: TOsfToolConfig;
begin
  Cfg := TOsfToolConfig.Create;
  try
    Cfg.Load;
    Cfg.SetValue(AKey, AValue);
    try
      Cfg.Save;
    except
      on E: Exception do
      begin
        PrintErrf('osftool config: failed to save: %s', [E.Message]);
        Exit(EXIT_IO_ERROR);
      end;
    end;
    if not FQuiet then
      Printf('Set %s = %s', [AKey, AValue]);
  finally
    Cfg.Free;
  end;
  Result := EXIT_OK;
end;

function TOsfConfigCommand.RunReset: Integer;
var
  Cfg: TOsfToolConfig;
begin
  Cfg := TOsfToolConfig.Create;
  try
    Cfg.Reset;
    try
      Cfg.Save;
    except
      on E: Exception do
      begin
        PrintErrf('osftool config: failed to save: %s', [E.Message]);
        Exit(EXIT_IO_ERROR);
      end;
    end;
    if not FQuiet then
      Print('Config reset to defaults.');
  finally
    Cfg.Free;
  end;
  Result := EXIT_OK;
end;

function TOsfConfigCommand.DoExecute: Integer;
var
  Positionals: TArray<string>;
  Sub: string;
begin
  Positionals := PositionalArgs([]);
  if Length(Positionals) = 0 then
    Exit(RunShow);

  Sub := LowerCase(Positionals[0]);
  if Sub = 'set' then
  begin
    if Length(Positionals) < 3 then
    begin
      PrintErr('osftool config set: expected <key> <value>');
      Exit(EXIT_BAD_ARGS);
    end;
    Result := RunSet(Positionals[1], Positionals[2]);
  end
  else if Sub = 'reset' then
    Result := RunReset
  else
  begin
    PrintErrf('osftool config: unknown subcommand "%s"', [Sub]);
    Result := EXIT_BAD_ARGS;
  end;
end;

end.
