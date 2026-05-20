// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

// Single source of truth for the osftool version. The build timestamp is
// read at run time from the executable's own file date, so it refreshes
// on every rebuild without a build step and without depending on shell
// environment variables (the {$I %DATE%} mechanism needs a DATE variable
// that PowerShell and a bare dcc invocation do not provide).
unit OSF.Version;

interface

const
  // Bump this on every release. Semantic versioning: MAJOR.MINOR.PATCH.
  // Also bump MyAppVersion in setup\osftool.iss to the same value.
  OSFTOOL_VERSION = '1.1.0';

// The bare version number, e.g. "1.1.0".
function GetVersionString: string;

// Two lines: "osftool <version>" followed by "Built: <yyyy-mm-dd hh:nn:ss>".
function GetFullVersionString: string;

implementation

uses
  System.SysUtils,
  System.IOUtils;

function GetVersionString: string;
begin
  Result := OSFTOOL_VERSION;
end;

function GetFullVersionString: string;
var
  BuildTime: TDateTime;
begin
  BuildTime := 0;
  try
    BuildTime := TFile.GetLastWriteTime(ParamStr(0));
  except
    // No executable file date available — fall back to the version only.
  end;
  if BuildTime > 0 then
    Result := Format('osftool %s' + sLineBreak + 'Built: %s',
      [OSFTOOL_VERSION, FormatDateTime('yyyy-mm-dd hh:nn:ss', BuildTime)])
  else
    Result := 'osftool ' + OSFTOOL_VERSION;
end;

end.
