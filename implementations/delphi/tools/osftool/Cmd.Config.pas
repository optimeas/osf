// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

// "config" verb. Inspect and mutate the persistent JSON config file
// used as the default-value source for every other command. Also
// exposes install-path / uninstall-path subcommands that add or
// remove the osftool executable's directory from the current user's
// PATH - without requiring administrator privileges on Windows.
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
    function RunInstallPath: Integer;
    function RunUninstallPath: Integer;
{$IFDEF MSWINDOWS}
    // Windows-only implementation helpers. Kept private so the Posix
    // branch never references them.
    function DoInstallPathWindows(const AExeDir: string): Integer;
    function DoUninstallPathWindows(const AExeDir: string): Integer;
{$ELSE}
    // Posix branch of both install-path / uninstall-path: prints the
    // shell snippet the user should add (or remove) by hand. Picks a
    // .zshrc vs .bashrc hint based on the build target.
    procedure PrintShellInstallInstructions(const AExeDir: string);
    procedure PrintShellUninstallInstructions(const AExeDir: string);
{$ENDIF}
  public
    function Name: string; override;
    function ShortDescription: string; override;
    function DoExecute: Integer; override;
    procedure PrintHelp; override;
  end;

implementation

uses
  System.Classes,
  System.StrUtils,
  System.IOUtils,
  System.Generics.Collections
  {$IFDEF MSWINDOWS}
  , Winapi.Windows
  , Winapi.Messages
  , System.Win.Registry
  {$ENDIF}
  ;

const
  // Windows hard limit on the value side of an environment variable.
  // We refuse a write that would exceed this rather than corrupting the
  // user's PATH silently.
  C_WIN_ENV_MAX_LEN = 32767;

resourcestring
  SConfigDesc = 'View and edit default settings';
  SConfigHelp =
    'osftool config                       Show all current settings' + sLineBreak +
    'osftool config set <key> <value>     Set a value' + sLineBreak +
    'osftool config reset                 Reset all settings to defaults' + sLineBreak +
    'osftool config install-path          Add osftool to user PATH (no admin required)' + sLineBreak +
    'osftool config uninstall-path        Remove osftool from user PATH' + sLineBreak +
    'osftool config --json                Show current settings as JSON' + sLineBreak +
    sLineBreak +
    'Keys and defaults:' + sLineBreak +
    '  output.format          osf5          Default output format for merge/convert' + sLineBreak +
    '  output.overlap         skip          Overlap strategy: skip or overwrite' + sLineBreak +
    '  export.decimal_sep     ,             CSV decimal separator' + sLineBreak +
    '  export.encoding        iso-8859-1    CSV encoding' + sLineBreak +
    '  cache.enabled          true          Use .json sidecar files' + sLineBreak +
    '  cache.auto_build       true          Auto-build cache during scan';
  SConfigFileLine         = 'Config file: %s';
  SConfigSetOk            = 'Set %s = %s';
  SConfigResetOk          = 'Config reset to defaults.';
  SConfigErrSaveFailed    = 'osftool config: failed to save: %s';
  SConfigErrSetExpectArgs = 'osftool config set: expected <key> <value>';
  SConfigErrUnknownSub    = 'osftool config: unknown subcommand "%s"';
  SConfigPathAlready      = 'osftool is already in PATH: %s';
  SConfigNoChanges        = 'No changes made.';
  SConfigPathAdded        = 'Added to PATH: %s';
  SConfigPathRemoved      = 'Removed from PATH: %s';
  SConfigPathNotFound     = 'osftool directory was not found in PATH. No changes made.';
  SConfigRestartTerminal  = 'Restart your terminal for the change to take effect.';
  SConfigErrPathTooLong   =
    'osftool: refused to write PATH - resulting %d characters exceeds the Windows %d-char limit';
  SConfigErrWritePathReg  = 'osftool: failed to write PATH to HKCU\Environment';
  SConfigErrWritePath     = 'osftool: failed to write PATH: %s';
  SConfigShellNoAutoPath  = 'osftool cannot modify PATH automatically on this platform.';
  SConfigShellAddIntro    = 'Add the following line to your shell configuration file';
  SConfigShellRemoveIntro = 'Remove the following line from your shell configuration file';
  SConfigShellConfigHint  = '(~/.zshrc on macOS, ~/.bashrc on Linux, or equivalent):';
  SConfigShellReload      = 'Then reload your shell:';

function TOsfConfigCommand.Name: string;
begin
  Result := 'config';
end;

function TOsfConfigCommand.ShortDescription: string;
begin
  Result := SConfigDesc;
end;

procedure TOsfConfigCommand.PrintHelp;
begin
  Print(SConfigHelp);
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
      Printf(SConfigFileLine, [TOsfToolConfig.ConfigFilePath]);
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
        PrintErrf(SConfigErrSaveFailed, [E.Message]);
        Exit(EXIT_IO_ERROR);
      end;
    end;
    if not FQuiet then
      Printf(SConfigSetOk, [AKey, AValue]);
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
        PrintErrf(SConfigErrSaveFailed, [E.Message]);
        Exit(EXIT_IO_ERROR);
      end;
    end;
    if not FQuiet then
      Print(SConfigResetOk);
  finally
    Cfg.Free;
  end;
  Result := EXIT_OK;
end;

// ── PATH-installer helpers ──────────────────────────────────────────────────
//
// The directory of the running executable is the canonical "install
// dir" - copying osftool.exe somewhere else and pointing PATH at the
// new location is the user's intent we honour.

// Returns the directory containing the running executable, with a
// trailing path separator stripped so case-insensitive comparison is
// straightforward.
function GetExeDir: string;
begin
  Result := ExtractFilePath(ParamStr(0));
  if (Length(Result) > 0) and (Result[Length(Result)] = PathDelim) then
    SetLength(Result, Length(Result) - 1);
end;

// Compare-form normalisation: lowercase, strip trailing slash, normalise
// forward slashes to backslashes on Windows so 'C:/foo/bar' matches
// 'C:\foo\bar\' as expected.
function NormalizeForCompare(const APath: string): string;
begin
  Result := LowerCase(APath);
  if (Length(Result) > 0) and (Result[Length(Result)] = PathDelim) then
    SetLength(Result, Length(Result) - 1);
  {$IFDEF MSWINDOWS}
  Result := StringReplace(Result, '/', '\', [rfReplaceAll]);
  if (Length(Result) > 0) and (Result[Length(Result)] = '\') then
    SetLength(Result, Length(Result) - 1);
  {$ENDIF}
end;

{$IFDEF MSWINDOWS}

// Reads HKCU\Environment\PATH. Returns an empty string when either the
// key or the value does not exist (treating missing keys as a clean
// empty PATH so install-path on a fresh account still succeeds).
function ReadUserPath: string;
var
  Reg: TRegistry;
begin
  Result := '';
  Reg := TRegistry.Create(KEY_READ);
  try
    Reg.RootKey := HKEY_CURRENT_USER;
    if Reg.OpenKeyReadOnly('Environment') then
    try
      if Reg.ValueExists('PATH') then
        Result := Reg.ReadString('PATH');
    finally
      Reg.CloseKey;
    end;
  finally
    Reg.Free;
  end;
end;

// Writes HKCU\Environment\PATH as REG_EXPAND_SZ. The expand-string
// variant is important: existing entries may contain %SystemRoot% or
// %USERPROFILE% placeholders that consumers expect to be expanded.
// Returns False on a failed open / write, True on success.
function WriteUserPath(const ANewPath: string): Boolean;
var
  Reg: TRegistry;
begin
  Result := False;
  Reg := TRegistry.Create(KEY_WRITE or KEY_READ);
  try
    Reg.RootKey := HKEY_CURRENT_USER;
    if Reg.OpenKey('Environment', True) then
    try
      Reg.WriteExpandString('PATH', ANewPath);
      Result := True;
    finally
      Reg.CloseKey;
    end;
  finally
    Reg.Free;
  end;
end;

// Tells Explorer + already-running processes to refresh their copy of
// the environment block. New cmd / PowerShell windows then inherit the
// fresh PATH without the user having to log out and back in. We
// timeout fast (5 s) because a hung Explorer must not block the verb.
procedure BroadcastEnvChange;
begin
  SendMessageTimeout(HWND_BROADCAST, WM_SETTINGCHANGE, 0,
    LPARAM(PChar('Environment')), SMTO_ABORTIFHUNG, 5000, nil);
end;

{$ENDIF}

{$IFNDEF MSWINDOWS}

procedure TOsfConfigCommand.PrintShellInstallInstructions(const AExeDir: string);
begin
  Print(SConfigShellNoAutoPath);
  Print('');
  Print(SConfigShellAddIntro);
  Print(SConfigShellConfigHint);
  Print('');
  Printf('  export PATH="$PATH:%s"', [AExeDir]);
  Print('');
  Print(SConfigShellReload);
{$IFDEF MACOS}
  Print('  source ~/.zshrc');
{$ELSE}
  Print('  source ~/.bashrc');
{$ENDIF}
end;

procedure TOsfConfigCommand.PrintShellUninstallInstructions(const AExeDir: string);
begin
  Print(SConfigShellNoAutoPath);
  Print('');
  Print(SConfigShellRemoveIntro);
  Print(SConfigShellConfigHint);
  Print('');
  Printf('  export PATH="$PATH:%s"', [AExeDir]);
end;

{$ENDIF}

{$IFDEF MSWINDOWS}

function TOsfConfigCommand.DoInstallPathWindows(const AExeDir: string): Integer;
var
  CurrentPath, NormalizedDir, NewPath: string;
  Existing: string;
begin
  CurrentPath := ReadUserPath;
  NormalizedDir := NormalizeForCompare(AExeDir);

  // Already-present check. Walking the current PATH entry-by-entry
  // avoids false positives that a plain InStr would produce for a
  // directory whose name is a substring of another entry.
  for Existing in CurrentPath.Split([';']) do
    if (Existing <> '') and (NormalizeForCompare(Existing) = NormalizedDir) then
    begin
      Printf(SConfigPathAlready, [AExeDir]);
      Print(SConfigNoChanges);
      Exit(EXIT_OK);
    end;

  if CurrentPath = '' then
    NewPath := AExeDir
  else
    NewPath := CurrentPath + ';' + AExeDir;

  if Length(NewPath) > C_WIN_ENV_MAX_LEN then
  begin
    PrintErrf(SConfigErrPathTooLong,
      [Length(NewPath), C_WIN_ENV_MAX_LEN]);
    Exit(EXIT_IO_ERROR);
  end;

  try
    if not WriteUserPath(NewPath) then
    begin
      PrintErr(SConfigErrWritePathReg);
      Exit(EXIT_IO_ERROR);
    end;
  except
    on E: Exception do
    begin
      PrintErrf(SConfigErrWritePath, [E.Message]);
      Exit(EXIT_IO_ERROR);
    end;
  end;

  BroadcastEnvChange;
  Printf(SConfigPathAdded, [AExeDir]);
  Print(SConfigRestartTerminal);
  Result := EXIT_OK;
end;

function TOsfConfigCommand.DoUninstallPathWindows(const AExeDir: string): Integer;
var
  CurrentPath, NormalizedDir, NewPath: string;
  Existing: string;
  Kept: TList<string>;
  RemovedCount: Integer;
begin
  CurrentPath := ReadUserPath;
  NormalizedDir := NormalizeForCompare(AExeDir);

  RemovedCount := 0;
  Kept := TList<string>.Create;
  try
    for Existing in CurrentPath.Split([';']) do
      if NormalizeForCompare(Existing) = NormalizedDir then
        Inc(RemovedCount)
      else
        Kept.Add(Existing);

    if RemovedCount = 0 then
    begin
      Print(SConfigPathNotFound);
      Exit(EXIT_OK);
    end;

    NewPath := string.Join(';', Kept.ToArray);

    try
      if not WriteUserPath(NewPath) then
      begin
        PrintErr(SConfigErrWritePathReg);
        Exit(EXIT_IO_ERROR);
      end;
    except
      on E: Exception do
      begin
        PrintErrf(SConfigErrWritePath, [E.Message]);
        Exit(EXIT_IO_ERROR);
      end;
    end;
  finally
    Kept.Free;
  end;

  BroadcastEnvChange;
  Printf(SConfigPathRemoved, [AExeDir]);
  Print(SConfigRestartTerminal);
  Result := EXIT_OK;
end;

{$ENDIF}

function TOsfConfigCommand.RunInstallPath: Integer;
var
  ExeDir: string;
begin
  ExeDir := GetExeDir;
{$IFDEF MSWINDOWS}
  Result := DoInstallPathWindows(ExeDir);
{$ELSE}
  PrintShellInstallInstructions(ExeDir);
  Result := EXIT_OK;
{$ENDIF}
end;

function TOsfConfigCommand.RunUninstallPath: Integer;
var
  ExeDir: string;
begin
  ExeDir := GetExeDir;
{$IFDEF MSWINDOWS}
  Result := DoUninstallPathWindows(ExeDir);
{$ELSE}
  PrintShellUninstallInstructions(ExeDir);
  Result := EXIT_OK;
{$ENDIF}
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
      PrintErr(SConfigErrSetExpectArgs);
      Exit(EXIT_BAD_ARGS);
    end;
    Result := RunSet(Positionals[1], Positionals[2]);
  end
  else if Sub = 'reset' then
    Result := RunReset
  else if Sub = 'install-path' then
    Result := RunInstallPath
  else if Sub = 'uninstall-path' then
    Result := RunUninstallPath
  else
  begin
    PrintErrf(SConfigErrUnknownSub, [Sub]);
    Result := EXIT_BAD_ARGS;
  end;
end;

end.
