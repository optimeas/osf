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
    function CollectStats(AMgr: TOSFDataManager): TArray<TChannelStat>;
    procedure EmitHuman(const AStats: TArray<TChannelStat>);
    procedure EmitJson(const AStats: TArray<TChannelStat>);
  public
    function Name: string; override;
    function ShortDescription: string; override;
    function DoExecute: Integer; override;
    procedure PrintHelp; override;
  end;

implementation

uses
  System.StrUtils;

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

function TOsfStatCommand.CollectStats(AMgr: TOSFDataManager): TArray<TChannelStat>;
var
  I, J: Integer;
  Ch: TOSFDataChannel;
  W: TWelford;
  Rec: TChannelStat;
begin
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
    Rec.SampleCount := Ch.SampleCount;

    if Rec.IsNumeric and (Ch.SampleCount > 0) then
    begin
      W := Default(TWelford);
      for J := 0 to Ch.SampleCount - 1 do
        W.Add(Ch.ValueAsDouble(J));
      Rec.Min := W.MinV;
      Rec.Max := W.MaxV;
      Rec.Mean := W.Mean;
      Rec.StdDev := W.StdDev;
    end;

    Result[I] := Rec;
  end;
end;

procedure TOsfStatCommand.EmitHuman(const AStats: TArray<TChannelStat>);
const
  C_HEADER = '%-30s %-8s %-8s %10s %12s %12s %12s %12s';
  C_NUMERIC = '%-30s %-8s %-8s %10d %12.4f %12.4f %12.4f %12.4f';
  C_NON_NUM = '%-30s %-8s %-8s %10d   (not numeric)';
var
  Rec: TChannelStat;
  AnyPrecisionLoss: Boolean;
begin
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

procedure TOsfStatCommand.EmitJson(const AStats: TArray<TChannelStat>);
var
  Arr: TJSONArray;
  Obj: TJSONObject;
  Rec: TChannelStat;
begin
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
    PrintJson(Arr.Format(2));
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

    // --start / --end: post-load timestamp filtering is not yet wired
    // in this build; the manager has no interval filter and the merger
    // path is overkill for a single-file scan. Document and ignore.
    if HasFlag('--start') or HasFlag('--end') then
      PrintErr('osftool stat: --start / --end are accepted but not yet honoured in this build');

    Stats := CollectStats(Mgr);
    if FJson then
      EmitJson(Stats)
    else
      EmitHuman(Stats);
  finally
    Mgr.Free;
  end;
  Result := EXIT_OK;
end;

end.
