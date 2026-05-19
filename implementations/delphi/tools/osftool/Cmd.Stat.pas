// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

// "stat" verb. Computes min / max / mean / stddev per numeric channel
// using Welford's online algorithm (numerically stable, single pass,
// O(1) memory per channel). String, binary and gpslocation channels
// are listed but marked "not numeric" — they have no scalar value to
// reduce.
unit Cmd.Stat;

interface

uses
  System.SysUtils,
  System.IOUtils,
  System.DateUtils,
  System.JSON,
  System.Math,
  Cmd.Base,
  OSF.Types,
  OSF.Channel,
  OSF.Log,
  OSF.Data.Manager,
  OSF.Data.Channels;

type
  TChannelStat = record
    Name: string;
    DataType: TOSFDataType;
    PhysicalUnit: string;
    SampleCount: Int64;
    Min: Double;
    Max: Double;
    Mean: Double;
    StdDev: Double;
    IsNumeric: Boolean;
    PrecisionLossRisk: Boolean;
  end;

  TOsfStatCommand = class(TBaseCommand)
  strict private
    // Time-bounded statistics. Zero on either side means "no bound on
    // that side"; passing both zero matches the unfiltered (whole-file)
    // case. Bounds are inclusive on both ends.
    function CollectStats(AMgr: TOSFDataManager;
      AStartNs, AEndNs: Int64): TArray<TChannelStat>;
    procedure EmitHuman(const AStats: TArray<TChannelStat>;
      AStartUtc, AEndUtc: TDateTime);
    procedure EmitJson(const AStats: TArray<TChannelStat>;
      AStartUtc, AEndUtc: TDateTime);
  public
    function Name: string; override;
    function ShortDescription: string; override;
    function DoExecute: Integer; override;
    procedure PrintHelp; override;
  end;

implementation

uses
  System.StrUtils;

const
  C_NS_PER_DAY = 86400.0 * 1.0E9;
  C_ISO_FMT    = 'yyyy-mm-dd"T"hh:nn:ss';

// ── ISO 8601 + Unix-ns helpers ───────────────────────────────────────────────
//
// Local copies of the same helpers used in Cmd.Merge.pas. Kept private so
// this command stays self-contained — no inter-command dependency.

function ParseIso8601(const AStr: string; out ADT: TDateTime): Boolean;
var
  Y, M, D, H, N, S: Word;
begin
  Result := False;
  ADT := 0;
  if Length(AStr) < 19 then
    Exit;
  try
    Y := StrToInt(Copy(AStr, 1, 4));
    M := StrToInt(Copy(AStr, 6, 2));
    D := StrToInt(Copy(AStr, 9, 2));
    H := StrToInt(Copy(AStr, 12, 2));
    N := StrToInt(Copy(AStr, 15, 2));
    S := StrToInt(Copy(AStr, 18, 2));
    ADT := EncodeDateTime(Y, M, D, H, N, S, 0);
    Result := True;
  except
    Result := False;
  end;
end;

function UtcDateTimeToUnixNs(ADT: TDateTime): Int64;
begin
  if ADT = 0 then
    Exit(0);
  Result := Round((ADT - EncodeDate(1970, 1, 1)) * C_NS_PER_DAY);
end;

function FormatIntervalBound(ADT: TDateTime): string;
begin
  if ADT = 0 then
    Exit('-');
  Result := FormatDateTime(C_ISO_FMT, ADT);
end;

// ── Welford ──────────────────────────────────────────────────────────────────

type
  TWelford = record
    Count: Int64;
    Mean: Double;
    M2: Double;
    MinV: Double;
    MaxV: Double;
    procedure Add(AValue: Double);
    function StdDev: Double;
  end;

procedure TWelford.Add(AValue: Double);
var
  Delta, Delta2: Double;
begin
  if Count = 0 then
  begin
    MinV := AValue;
    MaxV := AValue;
  end
  else
  begin
    if AValue < MinV then MinV := AValue;
    if AValue > MaxV then MaxV := AValue;
  end;
  Inc(Count);
  Delta := AValue - Mean;
  Mean := Mean + Delta / Count;
  Delta2 := AValue - Mean;
  M2 := M2 + Delta * Delta2;
end;

function TWelford.StdDev: Double;
begin
  if Count < 2 then
    Exit(0);
  // Bessel-corrected sample standard deviation.
  Result := Sqrt(M2 / (Count - 1));
end;

// ── Type helpers ─────────────────────────────────────────────────────────────

function IsNumericType(DT: TOSFDataType): Boolean;
begin
  Result := DT in [dtBool,
                   dtInt8, dtInt16, dtInt32, dtInt64,
                   dtUInt8, dtUInt16, dtUInt32, dtUInt64,
                   dtFloat, dtDouble];
end;

function IsInt64Type(DT: TOSFDataType): Boolean;
begin
  Result := DT in [dtInt64, dtUInt64];
end;

// ── TOsfStatCommand ─────────────────────────────────────────────────────────

function TOsfStatCommand.Name: string;
begin
  Result := 'stat';
end;

function TOsfStatCommand.ShortDescription: string;
begin
  Result := 'Compute statistics (min, max, mean, ...) per channel';
end;

procedure TOsfStatCommand.PrintHelp;
begin
  Print('osftool stat <file> [channel ...] [options]');
  Print('');
  Print('Arguments:');
  Print('  file       .osf or .osfz file');
  Print('  channel    Optional channel names (omit for all)');
  Print('');
  Print('Options:');
  Print('  --start <time>   Only consider samples from this UTC time (ISO 8601)');
  Print('  --end <time>     Only consider samples up to this UTC time (ISO 8601)');
  Print('  --json           Output as JSON');
  Print('  --quiet / --verbose');
  Print('');
  Print('Note: Int64/UInt64 channels may lose precision when converted to');
  Print('      Double for statistical calculations.');
end;

function TOsfStatCommand.CollectStats(AMgr: TOSFDataManager;
  AStartNs, AEndNs: Int64): TArray<TChannelStat>;
var
  I, J: Integer;
  Ch: TOSFDataChannel;
  W: TWelford;
  Rec: TChannelStat;
  Ts: Int64;
  HasLower, HasUpper, Keep: Boolean;
begin
  HasLower := AStartNs > 0;
  HasUpper := AEndNs > 0;

  SetLength(Result, AMgr.ChannelCount);
  for I := 0 to AMgr.ChannelCount - 1 do
  begin
    Ch := AMgr.Channels[I];
    Rec := Default(TChannelStat);
    Rec.Name := Ch.Name;
    Rec.PhysicalUnit := Ch.PhysicalUnit;
    if Assigned(Ch.ChannelDef) then
      Rec.DataType := Ch.ChannelDef.DataType
    else
      Rec.DataType := dtBinary;
    Rec.IsNumeric := IsNumericType(Rec.DataType);
    Rec.PrecisionLossRisk := IsInt64Type(Rec.DataType);

    if Rec.IsNumeric and (Ch.SampleCount > 0) then
    begin
      W := Default(TWelford);
      for J := 0 to Ch.SampleCount - 1 do
      begin
        Ts := Ch.TimestampNsAt(J);
        Keep := True;
        if HasLower and (Ts < AStartNs) then Keep := False;
        if HasUpper and (Ts > AEndNs)   then Keep := False;
        if Keep then
          W.Add(Ch.ValueAsDouble(J));
      end;
      // Report the in-range sample count, not the channel's total.
      // For an unfiltered run (no bounds) this equals Ch.SampleCount.
      Rec.SampleCount := W.Count;
      Rec.Min := W.MinV;
      Rec.Max := W.MaxV;
      Rec.Mean := W.Mean;
      Rec.StdDev := W.StdDev;
    end
    else
      // Non-numeric channels still report total samples — there is no
      // per-value timestamp filter to apply because no statistics run.
      Rec.SampleCount := Ch.SampleCount;

    Result[I] := Rec;
  end;
end;

procedure TOsfStatCommand.EmitHuman(const AStats: TArray<TChannelStat>;
  AStartUtc, AEndUtc: TDateTime);
const
  C_HEADER = '%-30s %-8s %-8s %10s %12s %12s %12s %12s';
  C_NUMERIC = '%-30s %-8s %-8s %10d %12.4f %12.4f %12.4f %12.4f';
  C_NON_NUM = '%-30s %-8s %-8s %10d   (not numeric)';
var
  Rec: TChannelStat;
  AnyPrecisionLoss: Boolean;
begin
  if (AStartUtc <> 0) or (AEndUtc <> 0) then
  begin
    Printf('Interval: %s .. %s',
      [FormatIntervalBound(AStartUtc), FormatIntervalBound(AEndUtc)]);
    Print('');
  end;
  Print(Format(C_HEADER, ['Channel', 'Type', 'Unit', 'Samples', 'Min', 'Max', 'Mean', 'StdDev']));
  Print(StringOfChar('-', 30 + 8 + 8 + 10 + 12 + 12 + 12 + 12 + 7));
  AnyPrecisionLoss := False;
  for Rec in AStats do
  begin
    if Rec.IsNumeric then
      Print(Format(C_NUMERIC,
        [Rec.Name, OSFDataTypeToString(Rec.DataType), Rec.PhysicalUnit,
         Rec.SampleCount, Rec.Min, Rec.Max, Rec.Mean, Rec.StdDev]))
    else
      Print(Format(C_NON_NUM,
        [Rec.Name, OSFDataTypeToString(Rec.DataType), Rec.PhysicalUnit,
         Rec.SampleCount]));
    if Rec.PrecisionLossRisk then
      AnyPrecisionLoss := True;
  end;
  if AnyPrecisionLoss then
  begin
    Print('');
    Print('Note: Int64/UInt64 channels may lose precision when converted to Double.');
  end;
end;

procedure TOsfStatCommand.EmitJson(const AStats: TArray<TChannelStat>;
  AStartUtc, AEndUtc: TDateTime);
var
  Arr: TJSONArray;
  Obj: TJSONObject;
  Rec: TChannelStat;
  HasFilter: Boolean;
  Root: TJSONObject;
begin
  HasFilter := (AStartUtc <> 0) or (AEndUtc <> 0);
  Arr := TJSONArray.Create;
  try
    for Rec in AStats do
    begin
      Obj := TJSONObject.Create;
      Arr.AddElement(Obj);
      Obj.AddPair('channel', Rec.Name);
      Obj.AddPair('datatype', OSFDataTypeToString(Rec.DataType));
      if Rec.PhysicalUnit <> '' then
        Obj.AddPair('unit', Rec.PhysicalUnit);
      Obj.AddPair('sample_count', TJSONNumber.Create(Rec.SampleCount));
      Obj.AddPair('numeric', TJSONBool.Create(Rec.IsNumeric));
      if Rec.IsNumeric and (Rec.SampleCount > 0) then
      begin
        Obj.AddPair('min',    TJSONNumber.Create(Rec.Min));
        Obj.AddPair('max',    TJSONNumber.Create(Rec.Max));
        Obj.AddPair('mean',   TJSONNumber.Create(Rec.Mean));
        Obj.AddPair('stddev', TJSONNumber.Create(Rec.StdDev));
      end;
      if Rec.PrecisionLossRisk then
        Obj.AddPair('precision_loss_risk', TJSONBool.Create(True));
    end;
    if not HasFilter then
    begin
      // Unfiltered run keeps the historical bare-array shape so
      // consumers parsing it do not need to change.
      PrintJson(Arr.Format(2));
      Arr := nil; // ownership transferred via PrintJson's no-op copy
    end
    else
    begin
      Root := TJSONObject.Create;
      try
        if AStartUtc <> 0 then
          Root.AddPair('interval_start_utc', FormatIntervalBound(AStartUtc));
        if AEndUtc <> 0 then
          Root.AddPair('interval_end_utc',   FormatIntervalBound(AEndUtc));
        Root.AddPair('channels', Arr);
        Arr := nil; // now owned by Root
        PrintJson(Root.Format(2));
      finally
        Root.Free;
      end;
    end;
  finally
    Arr.Free;
  end;
end;

function TOsfStatCommand.DoExecute: Integer;
var
  Positionals: TArray<string>;
  FileName: string;
  Channels: TArray<string>;
  Mgr: TOSFDataManager;
  Stats: TArray<TChannelStat>;
  I: Integer;
  StartStr, EndStr: string;
  StartUtc, EndUtc: TDateTime;
  StartNs, EndNs: Int64;
begin
  Positionals := PositionalArgs(['--start', '--end']);
  if Length(Positionals) < 1 then
  begin
    PrintErr('osftool stat: expected a file argument');
    Exit(EXIT_BAD_ARGS);
  end;
  FileName := Positionals[0];
  if not TFile.Exists(FileName) then
  begin
    PrintErrf('osftool stat: file not found: %s', [FileName]);
    Exit(EXIT_NOT_FOUND);
  end;

  // Remaining positionals are channel names for the filter.
  SetLength(Channels, Length(Positionals) - 1);
  for I := 1 to High(Positionals) do
    Channels[I - 1] := Positionals[I];

  // --start / --end: each is optional; the Welford loop filters per
  // sample on TimestampNsAt. Zero on either side means "no bound on
  // that side"; both zero = unfiltered whole-file run.
  StartUtc := 0;
  EndUtc := 0;
  StartNs := 0;
  EndNs := 0;
  StartStr := FlagValue('--start', '');
  EndStr := FlagValue('--end', '');
  if StartStr <> '' then
  begin
    if not ParseIso8601(StartStr, StartUtc) then
    begin
      PrintErrf('osftool stat: invalid --start: %s', [StartStr]);
      Exit(EXIT_BAD_ARGS);
    end;
    StartNs := UtcDateTimeToUnixNs(StartUtc);
  end;
  if EndStr <> '' then
  begin
    if not ParseIso8601(EndStr, EndUtc) then
    begin
      PrintErrf('osftool stat: invalid --end: %s', [EndStr]);
      Exit(EXIT_BAD_ARGS);
    end;
    EndNs := UtcDateTimeToUnixNs(EndUtc);
  end;
  if (StartNs > 0) and (EndNs > 0) and (EndNs < StartNs) then
  begin
    PrintErr('osftool stat: --end is earlier than --start');
    Exit(EXIT_BAD_ARGS);
  end;

  Mgr := TOSFDataManager.Create;
  try
    Mgr.OnLog := HandleLog;
    Mgr.DebugEnabled := FVerbose;
    Mgr.ChannelFilter := Channels;
    try
      Mgr.LoadFromFile(FileName);
    except
      on E: Exception do
      begin
        PrintErrf('osftool stat: cannot load %s: %s', [FileName, E.Message]);
        Exit(EXIT_FORMAT_ERROR);
      end;
    end;

    Stats := CollectStats(Mgr, StartNs, EndNs);
    if FJson then
      EmitJson(Stats, StartUtc, EndUtc)
    else
      EmitHuman(Stats, StartUtc, EndUtc);
  finally
    Mgr.Free;
  end;
  Result := EXIT_OK;
end;

end.
