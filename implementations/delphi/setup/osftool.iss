; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Optimeas GmbH
;
; Inno Setup script for the osftool command-line tool.
;
; Deploys OsfTool.exe together with the HDF5 runtime it needs for the
; "export --format hdf5" path (hdf5.dll plus the MSVC redistributable
; DLLs that build links against), and adds the install directory to PATH
; so "osftool" is callable from any shell. The HDF5 runtime lands in a
; lib\ subfolder, which Hdf5.Api resolves as "lib\ next to the exe".
;
; The installer lets the user choose at runtime between an all-users
; install (Program Files, system PATH) and a per-user install
; (%LocalAppData%\Programs, user PATH).
;
; ----------------------------------------------------------------------
; Build prerequisites — perform all three before compiling this script:
;
;   1. Build osftool for the Win64 / Release configuration. The default
;      IDE output path is expected:
;          implementations\delphi\tools\osftool\Win64\Release\OsfTool.exe
;      If you build elsewhere, adjust the OsfToolExe define below.
;
;   2. Fetch the HDF5 runtime DLLs (they are never committed to git):
;          powershell -File ..\..\..\dataformats\hdf5\lib\install-hdf5.ps1
;      This populates dataformats\hdf5\lib\win64\ with hdf5.dll and the
;      MSVC runtime DLLs. Without it the *.dll source pattern below
;      matches nothing and ISCC stops with an error.
;
;   3. Compile this script with Inno Setup 6 (ISCC.exe).
;
; The compiled installer is written next to this script as
; osftool-<version>-setup-x64.exe.
; ----------------------------------------------------------------------

#define MyAppName "OsfTool"
; osftool release version — adjust per release.
#define MyAppVersion "0.7.0"
#define MyAppPublisher "Optimeas GmbH"
#define MyAppExeName "OsfTool.exe"

; Build inputs, resolved relative to this .iss file.
#define OsfToolExe "..\tools\osftool\Win64\Release\OsfTool.exe"
#define Hdf5LibDir "..\..\..\dataformats\hdf5\lib\win64"
#define RepoLicense "..\..\..\LICENSE"

[Setup]
AppId={{8B5E2D4A-3F1C-4A7E-9B6D-2E8C1F0A5D3B}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
VersionInfoVersion={#MyAppVersion}.0
DefaultDirName={autopf}\{#MyAppName}
DisableProgramGroupPage=yes
LicenseFile={#RepoLicense}
OutputDir=.
OutputBaseFilename=osftool-{#MyAppVersion}-setup-x64
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
; Default to an all-users install but let the user pick "just me" at
; runtime; the install directory and PATH hive follow that choice.
PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=dialog
; Make Setup broadcast WM_SETTINGCHANGE so new shells see the PATH edit.
ChangesEnvironment=yes
UninstallDisplayIcon={app}\{#MyAppExeName}

[Languages]
Name: "en"; MessagesFile: "compiler:Default.isl"
Name: "de"; MessagesFile: "compiler:Languages\German.isl"

[Files]
Source: "{#OsfToolExe}"; DestDir: "{app}"; Flags: ignoreversion
; HDF5 runtime: hdf5.dll plus the bundled MSVC redistributable DLLs.
; Placed in lib\ so the loaded hdf5.dll resolves its own dependencies
; from there (Hdf5.Api loads it with LOAD_WITH_ALTERED_SEARCH_PATH).
Source: "{#Hdf5LibDir}\*.dll"; DestDir: "{app}\lib"; Flags: ignoreversion
Source: "{#RepoLicense}"; DestDir: "{app}"; DestName: "LICENSE.txt"; Flags: ignoreversion

[Code]
const
  EnvKeyHKLM = 'SYSTEM\CurrentControlSet\Control\Session Manager\Environment';
  EnvKeyHKCU = 'Environment';

// PATH lives in the machine hive for an all-users install, the user hive
// otherwise. The Environment key is not WOW64-redirected, so the plain
// registry functions are correct from the 32-bit installer process.
function EnvRoot: Integer;
begin
  if IsAdminInstallMode then
    Result := HKEY_LOCAL_MACHINE
  else
    Result := HKEY_CURRENT_USER;
end;

function EnvKey: string;
begin
  if IsAdminInstallMode then
    Result := EnvKeyHKLM
  else
    Result := EnvKeyHKCU;
end;

procedure AddDirToPath(const Dir: string);
var
  Path: string;
begin
  if not RegQueryStringValue(EnvRoot, EnvKey, 'Path', Path) then
    Path := '';
  // Already present (case-insensitive, delimited by ';')? Then do nothing.
  if Pos(';' + Uppercase(Dir) + ';', ';' + Uppercase(Path) + ';') > 0 then
    Exit;
  if (Path <> '') and (Path[Length(Path)] <> ';') then
    Path := Path + ';';
  RegWriteExpandStringValue(EnvRoot, EnvKey, 'Path', Path + Dir);
end;

procedure RemoveDirFromPath(const Dir: string);
var
  Path, NewPath, Item: string;
  P: Integer;
begin
  if not RegQueryStringValue(EnvRoot, EnvKey, 'Path', Path) then
    Exit;
  // Rebuild PATH from its ';'-separated items, dropping Dir.
  NewPath := '';
  while Path <> '' do
  begin
    P := Pos(';', Path);
    if P > 0 then
    begin
      Item := Copy(Path, 1, P - 1);
      Delete(Path, 1, P);
    end
    else
    begin
      Item := Path;
      Path := '';
    end;
    if (Item <> '') and (CompareText(Item, Dir) <> 0) then
    begin
      if NewPath <> '' then
        NewPath := NewPath + ';';
      NewPath := NewPath + Item;
    end;
  end;
  RegWriteExpandStringValue(EnvRoot, EnvKey, 'Path', NewPath);
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
    AddDirToPath(ExpandConstant('{app}'));
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usUninstall then
    RemoveDirFromPath(ExpandConstant('{app}'));
end;
