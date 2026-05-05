// Copyright 2026 Optimeas GmbH
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Lightweight logging hook for the OSF Delphi units. Classes that already have
// a required base class (e.g. TPersistent) cannot inherit from TOSFLoggable —
// they copy the three logging members verbatim instead. The Log() helper is
// always silent when OnLog is unassigned; it never raises, even if the user's
// log handler does.
unit OSF.Log;

interface

type
  TOSFLogLevel = (llDebug, llInfo, llWarning, llError);

  // The user's log sink. Receives every emitted message that survives the
  // DebugEnabled filter.
  TOSFLogEvent = reference to procedure(Level: TOSFLogLevel; const Msg: string);

  // Convenience base for classes that have no other base requirement.
  // Provides Log() and the OnLog / DebugEnabled properties.
  TOSFLoggable = class
  private
    FOnLog: TOSFLogEvent;
    FDebugEnabled: Boolean;
  protected
    procedure Log(Level: TOSFLogLevel; const Msg: string); overload;
    procedure Log(Level: TOSFLogLevel; const Fmt: string; const Args: array of const); overload;
  public
    // DebugEnabled controls whether llDebug messages are forwarded to OnLog.
    // Default: False. Set to True to enable verbose block-level logging.
    property DebugEnabled: Boolean read FDebugEnabled write FDebugEnabled;
    property OnLog: TOSFLogEvent read FOnLog write FOnLog;
  end;

implementation

uses
  System.SysUtils;

procedure TOSFLoggable.Log(Level: TOSFLogLevel; const Msg: string);
begin
  if not Assigned(FOnLog) then
    Exit;
  if (Level = llDebug) and (not FDebugEnabled) then
    Exit;
  try
    FOnLog(Level, Msg);
  except
    // Never let a buggy log handler propagate into core OSF code.
  end;
end;

procedure TOSFLoggable.Log(Level: TOSFLogLevel; const Fmt: string; const Args: array of const);
begin
  if not Assigned(FOnLog) then
    Exit;
  if (Level = llDebug) and (not FDebugEnabled) then
    Exit;
  try
    FOnLog(Level, Format(Fmt, Args));
  except
    // Never let a buggy log handler or a broken Format string propagate.
  end;
end;

end.
