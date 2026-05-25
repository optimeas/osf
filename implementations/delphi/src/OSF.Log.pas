// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

// Central logging + progress dispatcher for the OSF Delphi units.
//
// One process-wide TOSFLog instance, exposed as the global variable
// Logger, collects log messages and progress events from every OSF
// unit and fans them out to any number of registered listeners. CLI
// programs, GUI applications and tests each register their own
// listener with callbacks that decide what to do with the events —
// render a progress bar, append to a file, push to a memo, emit
// JSON-Lines, or just stay silent.
//
// Usage from any OSF unit:
//   uses OSF.Log;
//   ...
//   Logger.Write('Opening file', llInfo, 'TOSFFile');
//   Logger.ProgressStart(100, 'Reading...');
//
// Listener filtering is per-listener via the MinLevel property
// (default llUser). A message with Ord(Level) < Ord(MinLevel) is
// silently dropped before the OnAddLogMessage callback runs. The
// TOSFLog instance itself does not filter — every listener decides
// for itself.
//
// Thread safety: register / unregister / write / progress events
// are all serialised through a critical section that protects the
// listener list. Iteration happens on a snapshot taken under the
// lock, so a listener can safely register or unregister another
// listener inside its own callback without invalidating the
// in-flight iteration.
//
// Listener ownership: caller-owned. The TOSFLog singleton keeps
// references only — the caller that created a TLoggerListener is
// responsible for unregistering and freeing it.
unit OSF.Log;

interface

uses
  System.Generics.Collections,
  System.SyncObjs;

type
  // Verbosity-ascending ordering:
  //   Ord(llDebug)   = 0  - chatty internal trace, off by default
  //   Ord(llInfo)    = 1  - per-step internal progress, verbose mode
  //   Ord(llUser)    = 2  - default user-facing output (CLI / GUI)
  //   Ord(llWarning) = 3  - recoverable problems
  //   Ord(llError)   = 4  - hard failures
  // Listener MinLevel uses the natural ">=" filter: a listener with
  // MinLevel = llUser shows User + Warning + Error and hides Info +
  // Debug. The OSF library writes only the minimum at the higher
  // levels and channels diagnostic detail into Debug / Info; the
  // calling application decides which listener gets which floor.
  TOSFLogLevel = (llDebug, llInfo, llUser, llWarning, llError);

  TOSFLogEvent = reference to procedure(const Msg: string;
                                        Level: TOSFLogLevel;
                                        const Sender: string);

  TOSFProgressStartEvent = reference to procedure(MaxValue: Integer;
                                                  const Msg: string);
  TOSFProgressEvent      = reference to procedure(Value: Integer;
                                                  const Msg: string);
  TOSFProgressEndEvent   = reference to procedure(const Msg: string);

  // A pluggable listener. Create an instance, assign one or more of
  // the four event properties, set MinLevel if a non-default cutoff
  // is needed, and register the instance with the global Logger.
  // The caller stays the owner — Unregister + Free at shutdown.
  //
  // Convention (not enforced): do not subclass. Use a plain instance
  // and hand the events methods of your own class — that keeps the
  // listener flat and avoids inheritance ceremony for the typical
  // case (CLI program, GUI form, test fixture).
  TLoggerListener = class
  private
    FMinLevel: TOSFLogLevel;
    FOnAddLogMessage: TOSFLogEvent;
    FOnStartProgress: TOSFProgressStartEvent;
    FOnDoProgress:    TOSFProgressEvent;
    FOnEndProgress:   TOSFProgressEndEvent;
  public
    constructor Create;

    // Minimum severity this listener wants to receive. Default llUser.
    property MinLevel: TOSFLogLevel read FMinLevel write FMinLevel;

    property OnAddLogMessage: TOSFLogEvent           read FOnAddLogMessage write FOnAddLogMessage;
    property OnStartProgress: TOSFProgressStartEvent read FOnStartProgress write FOnStartProgress;
    property OnDoProgress:    TOSFProgressEvent      read FOnDoProgress    write FOnDoProgress;
    property OnEndProgress:   TOSFProgressEndEvent   read FOnEndProgress   write FOnEndProgress;
  end;

  TOSFLog = class
  private
    FListeners: TList<TLoggerListener>;
    FLock: TCriticalSection;
    function Snapshot: TArray<TLoggerListener>;
  public
    constructor Create;
    destructor Destroy; override;

    // Add or remove a listener. Both operations are no-ops on nil.
    // RegisterListener silently rejects a listener that is already
    // registered (no double-delivery).
    procedure RegisterListener(AListener: TLoggerListener);
    procedure UnregisterListener(AListener: TLoggerListener);

    // Emit a log message. Listeners filter by their own MinLevel.
    // The Sender string is conventionally the class name of the
    // emitting code (e.g. 'TOSFFile'); free-form, may be empty.
    procedure Write(const Msg: string;
                    Level: TOSFLogLevel = llDebug;
                    const Sender: string = ''); overload;

    // Format-overload. Skips Format() entirely (and the listener
    // dispatch) if no listener wants the given level.
    procedure Write(const Fmt: string; const Args: array of const;
                    Level: TOSFLogLevel = llDebug;
                    const Sender: string = ''); overload;

    // Progress events. MaxValue stays implicit between
    // ProgressStart and EndProgress; DoProgress carries only the
    // current value plus an optional context message (e.g. the
    // current file name). Nesting is not supported — a second
    // ProgressStart before EndProgress is delivered as-is and
    // resets any listener-side state.
    procedure ProgressStart(MaxValue: Integer; const Msg: string = '');
    procedure DoProgress(Value: Integer; const Msg: string = '');
    procedure EndProgress(const Msg: string = '');

    // True if at least one registered listener accepts this level.
    // Callers wrap expensive log-message construction in this check
    // to avoid the Format() cost when nobody is listening:
    //   if Logger.IsLevelActive(llDebug) then
    //     Logger.Write('expensive %s %d', [s, n], llDebug, 'TFoo');
    function IsLevelActive(Level: TOSFLogLevel): Boolean;
  end;

var
  // Process-wide singleton instance, created in the OSF.Log unit
  // initialization and freed in the finalization. Always non-nil
  // while the unit is loaded.
  Logger: TOSFLog;

implementation

uses
  System.SysUtils;

// ── TLoggerListener ─────────────────────────────────────────────────────

constructor TLoggerListener.Create;
begin
  inherited;
  FMinLevel := llUser;
end;

// ── TOSFLog ─────────────────────────────────────────────────────────────

constructor TOSFLog.Create;
begin
  inherited;
  FListeners := TList<TLoggerListener>.Create;
  FLock := TCriticalSection.Create;
end;

destructor TOSFLog.Destroy;
begin
  FListeners.Free;
  FLock.Free;
  inherited;
end;

procedure TOSFLog.RegisterListener(AListener: TLoggerListener);
begin
  if AListener = nil then
    Exit;
  FLock.Enter;
  try
    if not FListeners.Contains(AListener) then
      FListeners.Add(AListener);
  finally
    FLock.Leave;
  end;
end;

procedure TOSFLog.UnregisterListener(AListener: TLoggerListener);
begin
  if AListener = nil then
    Exit;
  FLock.Enter;
  try
    FListeners.Remove(AListener);
  finally
    FLock.Leave;
  end;
end;

function TOSFLog.Snapshot: TArray<TLoggerListener>;
begin
  FLock.Enter;
  try
    Result := FListeners.ToArray;
  finally
    FLock.Leave;
  end;
end;

procedure TOSFLog.Write(const Msg: string; Level: TOSFLogLevel; const Sender: string);
var
  L: TLoggerListener;
begin
  for L in Snapshot do
  begin
    if Ord(Level) < Ord(L.MinLevel) then
      Continue;
    if not Assigned(L.OnAddLogMessage) then
      Continue;
    try
      L.OnAddLogMessage(Msg, Level, Sender);
    except
      // Never let a buggy listener propagate into core OSF code.
    end;
  end;
end;

procedure TOSFLog.Write(const Fmt: string; const Args: array of const;
  Level: TOSFLogLevel; const Sender: string);
begin
  if not IsLevelActive(Level) then
    Exit;
  try
    Write(Format(Fmt, Args), Level, Sender);
  except
    // Never let a broken Format string propagate.
  end;
end;

procedure TOSFLog.ProgressStart(MaxValue: Integer; const Msg: string);
var
  L: TLoggerListener;
begin
  for L in Snapshot do
    if Assigned(L.OnStartProgress) then
    begin
      try
        L.OnStartProgress(MaxValue, Msg);
      except
      end;
    end;
end;

procedure TOSFLog.DoProgress(Value: Integer; const Msg: string);
var
  L: TLoggerListener;
begin
  for L in Snapshot do
    if Assigned(L.OnDoProgress) then
    begin
      try
        L.OnDoProgress(Value, Msg);
      except
      end;
    end;
end;

procedure TOSFLog.EndProgress(const Msg: string);
var
  L: TLoggerListener;
begin
  for L in Snapshot do
    if Assigned(L.OnEndProgress) then
    begin
      try
        L.OnEndProgress(Msg);
      except
      end;
    end;
end;

function TOSFLog.IsLevelActive(Level: TOSFLogLevel): Boolean;
var
  L: TLoggerListener;
begin
  Result := False;
  for L in Snapshot do
    if Ord(Level) >= Ord(L.MinLevel) then
      Exit(True);
end;

initialization
  Logger := TOSFLog.Create;

finalization
  Logger.Free;
  Logger := nil;

end.
