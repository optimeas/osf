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

// Renders a TOSFDataManager as a delimited text file with one (X, Y) column
// pair per channel. Default flavour is German Excel friendly: ISO-8859-1
// encoding, ';' column separator, ',' decimal separator, dotted timestamp.
// Adjust the four configuration properties to match other locales.
unit OSF.Export.CSV;

interface

uses
  System.SysUtils,
  System.Classes,
  System.Generics.Collections,
  OSF.Types,
  OSF.Log,
  OSF.Data.Manager,
  OSF.Data.Channels,
  OSF.Export;

type
  TOSFCSVExporter = class(TOSFExporter)
  private
    FDecimalSeparator : Char;
    FColumnSeparator  : Char;
    FEncoding         : TEncoding;
    FOwnsEncoding     : Boolean;
    FTimestampFormat  : string;

    procedure SetEncoding(Value: TEncoding);

    // Cell formatting helpers.
    function  ApplyDecimalSep(const S: string): string;
    function  FormatTimestamp(const TS: TDateTime): string;
    function  CellFromValue(Channel: TOSFDataChannel; SampleIdx: Integer): string;

    // Active-channel selection (mirrors the base ActiveChannels but emits
    // skip and precision-loss log messages along the way).
    function  CollectActive: TArray<TOSFDataChannel>;

    // Row builders — one method per header row plus the data row.
    function  BuildHeaderRow_Name      (const Channels: TArray<TOSFDataChannel>): string;
    function  BuildHeaderRow_ChannelTyp(const Channels: TArray<TOSFDataChannel>): string;
    function  BuildHeaderRow_Comment   (const Channels: TArray<TOSFDataChannel>): string;
    function  BuildHeaderRow_Unit      (const Channels: TArray<TOSFDataChannel>): string;
    function  BuildHeaderRow_Start     (const Channels: TArray<TOSFDataChannel>): string;
    function  BuildHeaderRow_Last      (const Channels: TArray<TOSFDataChannel>): string;
    function  BuildHeaderRow_Axes      (const Channels: TArray<TOSFDataChannel>): string;
    function  BuildDataRow             (const Channels: TArray<TOSFDataChannel>;
                                         SampleIdx: Integer): string;

    procedure WriteLine(AStream: TStream; const Line: string);
  protected
    procedure DoExport(const FileName: string); override;
  public
    constructor Create(DataManager: TOSFDataManager);
    destructor  Destroy; override;

    // The character that replaces '.' in numeric cells. Default ','.
    property DecimalSeparator : Char    read FDecimalSeparator write FDecimalSeparator;
    // Column delimiter. Default ';'.
    property ColumnSeparator  : Char    read FColumnSeparator  write FColumnSeparator;
    // Output encoding. Default ISO-8859-1 (code page 28591). Assigning a
    // user-provided TEncoding transfers ownership to the caller; the
    // exporter only frees the encoding it constructed itself.
    property Encoding         : TEncoding read FEncoding       write SetEncoding;
    // Format string passed to FormatDateTime for timestamp cells.
    // Default 'dd.MM.yyyy HH:mm:ss'.
    property TimestampFormat  : string  read FTimestampFormat  write FTimestampFormat;
  end;

implementation

const
  ISO_8859_1_CODEPAGE   = 28591;
  CSV_LINE_TERMINATOR   = #13#10;

  CSV_HDR_NAME          = 'Name:';
  CSV_HDR_CHANNEL_TYP   = 'Channel-Typ:';
  CSV_HDR_COMMENT       = 'Comment:';
  CSV_HDR_UNIT          = 'Unit:';
  CSV_HDR_START_TS      = 'Start-Timestamp:';
  CSV_HDR_LAST_TS       = 'Last-Timestamp:';
  CSV_TIME_AXIS_LABEL   = 'time';
  CSV_TIME_AXIS_UNIT    = 's';
  CSV_CHANNEL_TYP_VALUE = 'XY';
  CSV_X_VALUES_HEADER   = 'X-Values';
  CSV_Y_VALUES_HEADER   = 'Y-Values';

  CSV_INFO_STARTED      = 'CSV export started: %s  channels=%d';
  CSV_INFO_FINISHED     = 'CSV export finished: %s  rows=%d  bytes=%d';
  CSV_DEBUG_SKIP_EMPTY  = 'Skipping empty channel: %s';
  CSV_WARN_PRECISION    = 'Channel [%s] has Int64/UInt64 values — CSV output may lose precision';
  CSV_ERROR_FAILED      = 'CSV export failed: %s';

// Returns True for OSF data types whose ValueAsString produces a single number
// where '.' is the decimal point. Pair/Triple, Gps, Can produce composite
// strings; String/Binary aren't numeric — none of those should have the
// decimal-separator substitution applied.
function IsSingleNumericType(DT: TOSFDataType): Boolean;
begin
  Result := DT in [dtBool,
                    dtInt8,  dtInt16, dtInt32, dtInt64,
                    dtUInt8, dtUInt16, dtUInt32, dtUInt64,
                    dtFloat, dtDouble];
end;

// ── Construction / destruction ──────────────────────────────────────────────

constructor TOSFCSVExporter.Create(DataManager: TOSFDataManager);
begin
  inherited Create(DataManager);
  FDecimalSeparator := ',';
  FColumnSeparator  := ';';
  FTimestampFormat  := 'dd.MM.yyyy HH:mm:ss';
  FEncoding         := TEncoding.GetEncoding(ISO_8859_1_CODEPAGE);
  FOwnsEncoding     := True;
end;

destructor TOSFCSVExporter.Destroy;
begin
  if FOwnsEncoding then
    FEncoding.Free;
  inherited;
end;

procedure TOSFCSVExporter.SetEncoding(Value: TEncoding);
begin
  if Value = FEncoding then Exit;
  if FOwnsEncoding then
    FEncoding.Free;
  FEncoding     := Value;
  // The new encoding belongs to the caller; we only free what we constructed.
  FOwnsEncoding := False;
end;

// ── Helpers ──────────────────────────────────────────────────────────────────

function TOSFCSVExporter.ApplyDecimalSep(const S: string): string;
begin
  if FDecimalSeparator = '.' then
    Result := S
  else
    Result := StringReplace(S, '.', FDecimalSeparator, [rfReplaceAll]);
end;

function TOSFCSVExporter.FormatTimestamp(const TS: TDateTime): string;
begin
  Result := FormatDateTime(FTimestampFormat, TS);
end;

function TOSFCSVExporter.CellFromValue(Channel: TOSFDataChannel;
  SampleIdx: Integer): string;
begin
  Result := Channel.ValueAsString(SampleIdx);
  if IsSingleNumericType(Channel.OriginalDataType) then
    Result := ApplyDecimalSep(Result);
end;

procedure TOSFCSVExporter.WriteLine(AStream: TStream; const Line: string);
var
  Bytes: TBytes;
begin
  Bytes := FEncoding.GetBytes(Line + CSV_LINE_TERMINATOR);
  if Length(Bytes) > 0 then
    AStream.WriteBuffer(Bytes[0], Length(Bytes));
end;

function TOSFCSVExporter.CollectActive: TArray<TOSFDataChannel>;
var
  I, Cnt : Integer;
  Ch     : TOSFDataChannel;
  Keep   : Boolean;
begin
  if not Assigned(DataManager) then
  begin
    SetLength(Result, 0);
    Exit;
  end;
  SetLength(Result, DataManager.ChannelCount);
  Cnt := 0;
  for I := 0 to DataManager.ChannelCount - 1 do
  begin
    Ch   := DataManager.Channels[I];
    Keep := (not ExcludeEmptyChannels) or (Ch.SampleCount > 0);
    if Keep then
    begin
      Result[Cnt] := Ch;
      Inc(Cnt);
      if Ch.HasDoublePrecisionLoss then
        Log(llWarning, CSV_WARN_PRECISION, [Ch.Name]);
    end
    else
      Log(llDebug, CSV_DEBUG_SKIP_EMPTY, [Ch.Name]);
  end;
  SetLength(Result, Cnt);
end;

// ── Header row builders ──────────────────────────────────────────────────────

function TOSFCSVExporter.BuildHeaderRow_Name(
  const Channels: TArray<TOSFDataChannel>): string;
var
  I: Integer;
begin
  Result := CSV_HDR_NAME;
  for I := 0 to High(Channels) do
    Result := Result + FColumnSeparator + CSV_TIME_AXIS_LABEL +
                       FColumnSeparator + Channels[I].Name;
end;

function TOSFCSVExporter.BuildHeaderRow_ChannelTyp(
  const Channels: TArray<TOSFDataChannel>): string;
var
  I: Integer;
begin
  Result := CSV_HDR_CHANNEL_TYP;
  for I := 0 to High(Channels) do
    Result := Result + FColumnSeparator + CSV_CHANNEL_TYP_VALUE +
                       FColumnSeparator;
end;

function TOSFCSVExporter.BuildHeaderRow_Comment(
  const Channels: TArray<TOSFDataChannel>): string;
var
  I: Integer;
begin
  Result := CSV_HDR_COMMENT;
  for I := 0 to High(Channels) do
    Result := Result + FColumnSeparator +
                       FColumnSeparator + Channels[I].Comment;
end;

function TOSFCSVExporter.BuildHeaderRow_Unit(
  const Channels: TArray<TOSFDataChannel>): string;
var
  I: Integer;
begin
  Result := CSV_HDR_UNIT;
  for I := 0 to High(Channels) do
    Result := Result + FColumnSeparator + CSV_TIME_AXIS_UNIT +
                       FColumnSeparator + Channels[I].PhysicalUnit;
end;

function TOSFCSVExporter.BuildHeaderRow_Start(
  const Channels: TArray<TOSFDataChannel>): string;
var
  I: Integer;
begin
  Result := CSV_HDR_START_TS;
  for I := 0 to High(Channels) do
    Result := Result + FColumnSeparator + FormatTimestamp(Channels[I].StartTimeUtc) +
                       FColumnSeparator;
end;

function TOSFCSVExporter.BuildHeaderRow_Last(
  const Channels: TArray<TOSFDataChannel>): string;
var
  I: Integer;
begin
  Result := CSV_HDR_LAST_TS;
  for I := 0 to High(Channels) do
    Result := Result + FColumnSeparator + FormatTimestamp(Channels[I].EndTimeUtc) +
                       FColumnSeparator;
end;

function TOSFCSVExporter.BuildHeaderRow_Axes(
  const Channels: TArray<TOSFDataChannel>): string;
var
  I: Integer;
begin
  // Row 8: empty Col 0, then per channel: 'X-Values' and 'Y-Values'.
  Result := '';
  for I := 0 to High(Channels) do
    Result := Result + FColumnSeparator + CSV_X_VALUES_HEADER +
                       FColumnSeparator + CSV_Y_VALUES_HEADER;
end;

function TOSFCSVExporter.BuildDataRow(const Channels: TArray<TOSFDataChannel>;
  SampleIdx: Integer): string;
var
  I     : Integer;
  Ch    : TOSFDataChannel;
  XCell : string;
  YCell : string;
begin
  // Data rows always start with an empty Col 0.
  Result := '';
  for I := 0 to High(Channels) do
  begin
    Ch := Channels[I];
    if SampleIdx < Ch.SampleCount then
    begin
      XCell := FormatTimestamp(Ch.TimestampUtcAt(SampleIdx));
      YCell := CellFromValue(Ch, SampleIdx);
    end
    else
    begin
      XCell := '';
      YCell := '';
    end;
    Result := Result + FColumnSeparator + XCell +
                       FColumnSeparator + YCell;
  end;
end;

// ── DoExport ─────────────────────────────────────────────────────────────────

procedure TOSFCSVExporter.DoExport(const FileName: string);
var
  FS             : TFileStream;
  Active         : TArray<TOSFDataChannel>;
  MaxSampleCount : Integer;
  I, RowIdx      : Integer;
  RowsEmitted    : Integer;
begin
  Active := CollectActive;
  Log(llInfo, CSV_INFO_STARTED, [FileName, Length(Active)]);

  // Find the longest channel so the data section ends after the last sample
  // of the longest channel; shorter channels emit empty cells beyond their end.
  MaxSampleCount := 0;
  for I := 0 to High(Active) do
    if Active[I].SampleCount > MaxSampleCount then
      MaxSampleCount := Active[I].SampleCount;

  RowsEmitted := 0;
  FS := TFileStream.Create(FileName, fmCreate);
  try
    try
      // Header block — exactly 8 rows (row 7 is intentionally blank).
      WriteLine(FS, BuildHeaderRow_Name      (Active)); Inc(RowsEmitted);
      WriteLine(FS, BuildHeaderRow_ChannelTyp(Active)); Inc(RowsEmitted);
      WriteLine(FS, BuildHeaderRow_Comment   (Active)); Inc(RowsEmitted);
      WriteLine(FS, BuildHeaderRow_Unit      (Active)); Inc(RowsEmitted);
      WriteLine(FS, BuildHeaderRow_Start     (Active)); Inc(RowsEmitted);
      WriteLine(FS, BuildHeaderRow_Last      (Active)); Inc(RowsEmitted);
      WriteLine(FS, '');                                Inc(RowsEmitted);
      WriteLine(FS, BuildHeaderRow_Axes      (Active)); Inc(RowsEmitted);

      // Data block.
      for RowIdx := 0 to MaxSampleCount - 1 do
      begin
        WriteLine(FS, BuildDataRow(Active, RowIdx));
        Inc(RowsEmitted);
      end;

      Log(llInfo, CSV_INFO_FINISHED, [FileName, RowsEmitted, FS.Size]);
    except
      on E: Exception do
      begin
        // CSV-specific failure log; the inherited Export wrapper logs the
        // generic 'Export failed: ...' message after we re-raise.
        Log(llError, CSV_ERROR_FAILED, [E.Message]);
        raise;
      end;
    end;
  finally
    FS.Free;
  end;
end;

end.
