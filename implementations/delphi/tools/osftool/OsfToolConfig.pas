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

// Persistent osftool settings.
//
// Backed by a JSON file at:
//   Windows : %APPDATA%\osftool\config.json
//   POSIX   : ~/.config/osftool/config.json
//
// Keys are exposed in dot notation ("output.format", "cache.enabled") and
// are mapped onto nested JSON objects on save / load. Missing keys fall
// back to their hard-coded defaults; the file is never required to be
// present and the loader tolerates a missing file silently.
unit OsfToolConfig;

interface

uses
  System.SysUtils,
  System.Classes,
  System.IOUtils,
  System.JSON,
  System.Generics.Collections;

type
  TOsfToolConfig = class
  strict private
    FValues: TDictionary<string, string>;
    procedure ApplyDefaults;
    function BuildJsonObject: TJSONObject;
    procedure ImportJsonObject(AObj: TJSONObject; const APrefix: string);
  public
    constructor Create;
    destructor Destroy; override;

    // Load defaults, then overlay anything found in ConfigFilePath.
    // Silently uses defaults when the file is missing or unreadable.
    procedure Load;
    // Persist FValues to ConfigFilePath. Creates the parent directory
    // if needed. Raises EInOutError on a real filesystem failure.
    procedure Save;
    // Reset to defaults without touching disk.
    procedure Reset;

    function Get(const AKey: string): string;
    procedure SetValue(const AKey, AValue: string);
    function ContainsKey(const AKey: string): Boolean;

    // Returns every known key in deterministic order (defaults plus any
    // user-set keys that share the same prefix space).
    function Keys: TArray<string>;

    // Compact JSON representation of the in-memory settings.
    function AsJson: string;

    class function ConfigFilePath: string;
  end;

// Canonical default keys and values, exposed so callers (config command
// and individual command defaults) reference the same constants.
const
  C_KEY_OUTPUT_FORMAT       = 'output.format';
  C_KEY_OUTPUT_OVERLAP      = 'output.overlap';
  C_KEY_EXPORT_DECIMAL_SEP  = 'export.decimal_sep';
  C_KEY_EXPORT_ENCODING     = 'export.encoding';
  C_KEY_CACHE_ENABLED       = 'cache.enabled';
  C_KEY_CACHE_AUTO_BUILD    = 'cache.auto_build';

  C_DEFAULT_OUTPUT_FORMAT      = 'osf5';
  C_DEFAULT_OUTPUT_OVERLAP     = 'skip';
  C_DEFAULT_EXPORT_DECIMAL_SEP = ',';
  C_DEFAULT_EXPORT_ENCODING    = 'iso-8859-1';
  C_DEFAULT_CACHE_ENABLED      = 'true';
  C_DEFAULT_CACHE_AUTO_BUILD   = 'true';

implementation

uses
  System.StrUtils;

const
  // The canonical key order matches the human-readable "config" output
  // and the JSON serialisation. Centralised here so Keys, AsJson, the
  // config command and ApplyDefaults all stay in sync.
  C_KEY_ORDER: array[0..5] of string = (
    C_KEY_OUTPUT_FORMAT,
    C_KEY_OUTPUT_OVERLAP,
    C_KEY_EXPORT_DECIMAL_SEP,
    C_KEY_EXPORT_ENCODING,
    C_KEY_CACHE_ENABLED,
    C_KEY_CACHE_AUTO_BUILD
  );

constructor TOsfToolConfig.Create;
begin
  inherited Create;
  FValues := TDictionary<string, string>.Create;
  ApplyDefaults;
end;

destructor TOsfToolConfig.Destroy;
begin
  FValues.Free;
  inherited;
end;

procedure TOsfToolConfig.ApplyDefaults;
begin
  FValues.Clear;
  FValues.Add(C_KEY_OUTPUT_FORMAT,      C_DEFAULT_OUTPUT_FORMAT);
  FValues.Add(C_KEY_OUTPUT_OVERLAP,     C_DEFAULT_OUTPUT_OVERLAP);
  FValues.Add(C_KEY_EXPORT_DECIMAL_SEP, C_DEFAULT_EXPORT_DECIMAL_SEP);
  FValues.Add(C_KEY_EXPORT_ENCODING,    C_DEFAULT_EXPORT_ENCODING);
  FValues.Add(C_KEY_CACHE_ENABLED,      C_DEFAULT_CACHE_ENABLED);
  FValues.Add(C_KEY_CACHE_AUTO_BUILD,   C_DEFAULT_CACHE_AUTO_BUILD);
end;

procedure TOsfToolConfig.Reset;
begin
  ApplyDefaults;
end;

class function TOsfToolConfig.ConfigFilePath: string;
var
  Dir: string;
begin
  {$IFDEF MSWINDOWS}
  Dir := TPath.Combine(GetEnvironmentVariable('APPDATA'), 'osftool');
  {$ELSE}
  Dir := TPath.Combine(TPath.GetHomePath, '.config' + PathDelim + 'osftool');
  {$ENDIF}
  Result := TPath.Combine(Dir, 'config.json');
end;

function TOsfToolConfig.Get(const AKey: string): string;
begin
  if not FValues.TryGetValue(AKey, Result) then
    Result := '';
end;

procedure TOsfToolConfig.SetValue(const AKey, AValue: string);
begin
  FValues.AddOrSetValue(AKey, AValue);
end;

function TOsfToolConfig.ContainsKey(const AKey: string): Boolean;
begin
  Result := FValues.ContainsKey(AKey);
end;

function TOsfToolConfig.Keys: TArray<string>;
var
  I: Integer;
  Extra: TList<string>;
  K: string;
  Known: TDictionary<string, Boolean>;
begin
  Known := TDictionary<string, Boolean>.Create;
  Extra := TList<string>.Create;
  try
    for I := 0 to High(C_KEY_ORDER) do
      Known.Add(C_KEY_ORDER[I], True);
    for K in FValues.Keys do
      if not Known.ContainsKey(K) then
        Extra.Add(K);
    Extra.Sort;

    SetLength(Result, Length(C_KEY_ORDER) + Extra.Count);
    for I := 0 to High(C_KEY_ORDER) do
      Result[I] := C_KEY_ORDER[I];
    for I := 0 to Extra.Count - 1 do
      Result[Length(C_KEY_ORDER) + I] := Extra[I];
  finally
    Extra.Free;
    Known.Free;
  end;
end;

procedure TOsfToolConfig.ImportJsonObject(AObj: TJSONObject; const APrefix: string);
var
  I: Integer;
  Pair: TJSONPair;
  Key: string;
  ValStr: string;
begin
  for I := 0 to AObj.Count - 1 do
  begin
    Pair := AObj.Pairs[I];
    if APrefix = '' then
      Key := Pair.JsonString.Value
    else
      Key := APrefix + '.' + Pair.JsonString.Value;

    if Pair.JsonValue is TJSONObject then
      ImportJsonObject(TJSONObject(Pair.JsonValue), Key)
    else
    begin
      // Booleans, numbers, and strings all serialise via Value as plain
      // text; nulls become an empty string.
      if Pair.JsonValue is TJSONNull then
        ValStr := ''
      else
        ValStr := Pair.JsonValue.Value;
      FValues.AddOrSetValue(Key, ValStr);
    end;
  end;
end;

procedure TOsfToolConfig.Load;
var
  Path: string;
  Bytes: TBytes;
  Root: TJSONValue;
begin
  ApplyDefaults;
  Path := ConfigFilePath;
  if not TFile.Exists(Path) then
    Exit;
  try
    Bytes := TFile.ReadAllBytes(Path);
    Root := TJSONObject.ParseJSONValue(TEncoding.UTF8.GetString(Bytes));
    try
      if Root is TJSONObject then
        ImportJsonObject(TJSONObject(Root), '');
    finally
      Root.Free;
    end;
  except
    // Corrupt config file is a soft failure — fall back to defaults.
  end;
end;

function TOsfToolConfig.BuildJsonObject: TJSONObject;
var
  K, V: string;
  Parts: TArray<string>;
  I: Integer;
  Node, Child: TJSONObject;
  LastKey: string;
begin
  Result := TJSONObject.Create;
  try
    for K in Keys do
    begin
      if FValues.TryGetValue(K, V) then
      begin
        Parts := K.Split(['.']);
        if Length(Parts) > 0 then
        begin
          Node := Result;
          // Traverse / create nested objects for every segment except the
          // last.
          for I := 0 to High(Parts) - 1 do
          begin
            if Node.GetValue(Parts[I]) is TJSONObject then
              Node := TJSONObject(Node.GetValue(Parts[I]))
            else
            begin
              Child := TJSONObject.Create;
              Node.AddPair(Parts[I], Child);
              Node := Child;
            end;
          end;
          LastKey := Parts[High(Parts)];
          // Remove any pre-existing pair with the same name so a second
          // Save is idempotent. (TJSONObject.AddPair would otherwise
          // duplicate.)
          if Assigned(Node.GetValue(LastKey)) then
            Node.RemovePair(LastKey).Free;
          Node.AddPair(LastKey, V);
        end;
      end;
    end;
  except
    Result.Free;
    raise;
  end;
end;

procedure TOsfToolConfig.Save;
var
  Path, Dir, Text: string;
  Root: TJSONObject;
begin
  Path := ConfigFilePath;
  Dir := TPath.GetDirectoryName(Path);
  if (Dir <> '') and (not TDirectory.Exists(Dir)) then
    TDirectory.CreateDirectory(Dir);
  Root := BuildJsonObject;
  try
    Text := Root.Format(2);
    TFile.WriteAllBytes(Path, TEncoding.UTF8.GetBytes(Text));
  finally
    Root.Free;
  end;
end;

function TOsfToolConfig.AsJson: string;
var
  Root: TJSONObject;
begin
  Root := BuildJsonObject;
  try
    Result := Root.Format(2);
  finally
    Root.Free;
  end;
end;

end.
