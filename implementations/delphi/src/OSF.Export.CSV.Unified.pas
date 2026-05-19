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

// Single-timeline CSV exporter.
//
// Companion to OSF.Export.CSV (TOSFCSVExporter). The classic exporter
// emits one time + value column pair per channel (XY shape). This one
// emits a single timestamp column followed by one value column per
// channel: every row represents one unique absolute timestamp across
// all channels, with empty cells where a channel has no sample at
// that timestamp.
//
// The timeline is the sorted, de-duplicated union of every active
// channel's TimestampNsAt(i). Row emission uses one cursor per channel
// that advances when the channel's next timestamp matches the current
// timeline timestamp — O(total samples), no quadratic scan.
unit OSF.Export.CSV.Unified;

interface

uses
  System.SysUtils,
  System.Classes,
  System.Generics.Collections,
  OSF.Types,
  OSF.Log,
  OSF.Export,
  OSF.Data.Manager,
  OSF.Data.Channels;

type
  // Timestamp serialisation strategy for the single timestamp column.
  TUnifiedCSVTimestampFormat = (
    tfDateTime,    // 'dd.MM.yyyy HH:mm:ss.zzz'        — Excel-readable, default
    tfSeconds,     // 'dd.MM.yyyy HH:mm:ss'             — Excel-readable, no sub-second
    tfISO8601,     // 'yyyy-MM-ddTHH:mm:ss.nnnnnnnnnZ' — full nanosecond precision
    tfNanoseconds  // IntToStr(Int64)                  — machine-readable
  );

  TOSFUnifiedCSVExporter = class(TOSFExporter)
  strict private
    FDecimalSeparator: Char;
    FColumnSeparator: Char;
    FEncoding: TEncoding;
    FOwnsEncoding: Boolean;
    FTimestampFormat: TUnifiedCSVTimestampFormat;
    procedure SetEncoding(AValue: TEncoding);
    function BuildTimeline(const AChannels: TArray<TOSFDataChannel>): TList<Int64>;
    function BuildHeaderRow1(const AChannels: TArray<TOSFDataChannel>): string;
    function BuildHeaderRow2(const AChannels: TArray<TOSFDataChannel>): string;
    function CellFromValue(AChannel: TOSFDataChannel; ASampleIdx: Integer): string;
    function FormatTimestamp(ANs: Int64): string;
    procedure WriteLine(AStream: TStream; const ALine: string);
  protected
    procedure DoExport(const AFileName: string); override;
  public
    constructor Create(ADataManager: TOSFDataManager);
    destructor Destroy; override;

    // Decimal separator applied to numeric cells. Default ',' (German
    // Excel default). Use '.' for locale-neutral machine-readable output.
    property DecimalSeparator: Char read FDecimalSeparator write FDecimalSeparator;
    // Column separator. Default ';' (German Excel default).
    property ColumnSeparator: Char read FColumnSeparator write FColumnSeparator;
    // Output encoding. Default ISO-8859-1. Setting a user-constructed
    // TEncoding transfers ownership to the caller (we only free the
    // encoding we built ourselves).
    property Encoding: TEncoding read FEncoding write SetEncoding;
    // Timestamp serialisation. Default tfDateTime.
    property TimestampFormat: TUnifiedCSVTimestampFormat
      read FTimestampFormat write FTimestampFormat;
  end;

resourcestring
  // Log messages emitted from DoExport. Symbols mirror the classic
  // OSF.Export.CSV resource strings; values are unified-specific.
  SOSFLogUnifiedStarted     = 'Unified CSV export started: %s  channels=%d';
  SOSFLogUnifiedTimeline    = 'Timeline: %d unique timestamps across all channels';
  SOSFLogUnifiedSkipEmpty   = 'Skipping empty channel: %s';
  SOSFLogUnifiedFinished    = 'Unified CSV export finished: %s  rows=%d  bytes=%d';
  SOSFLogUnifiedFailed      = 'Unified CSV export failed: %s';

implementation

uses
  System.Generics.Defaults,
  System.DateUtils;

const
  // File-format constants — codepage and line terminator are part of
  // the on-disk shape, not user-visible text.
  ISO_8859_1_CODEPAGE = 28591;
  CSV_LINE_TERMINATOR = #13#10;
  C_NS_PER_DAY = 86400.0 * 1.0E9;

// ── Timestamp + type helpers ────────────────────────────────────────────────

function UnixNsToUtcDateTime(ANs: Int64): TDateTime;
begin
  if ANs = 0 then
    Exit(0);
  Result := EncodeDate(1970, 1, 1) + (ANs / C_NS_PER_DAY);
end;

// Numeric data types whose ValueAsString produces a single floating /
// integer number where '.' is the decimal point (so the substitution
// to DecimalSeparator is safe). Variable-length and composite types
// (string / binary / gpslocation) are written verbatim.
function IsSingleNumericType(DT: TOSFDataType): Boolean;
begin
  Result := DT in [dtBool,
                   dtInt8,  dtInt16, dtInt32, dtInt64,
                   dtUInt8, dtUInt16, dtUInt32, dtUInt64,
                   dtFloat, dtDouble];
end;

function TimestampFormatLabel(AFmt: TUnifiedCSVTimestampFormat): string;
begin
  case AFmt of
    tfDateTime:    Result := 'dd.MM.yyyy HH:mm:ss.zzz';
    tfSeconds:     Result := 'dd.MM.yyyy HH:mm:ss';
    tfISO8601:     Result := 'ISO 8601';
    tfNanoseconds: Result := 'ns';
  else
    Result := '';
  end;
end;

// ── Construction / destruction ──────────────────────────────────────────────

constructor TOSFUnifiedCSVExporter.Create(ADataManager: TOSFDataManager);
begin
  inherited Create(ADataManager);
  FDecimalSeparator := ',';
  FColumnSeparator := ';';
  FTimestampFormat := tfDateTime;
  FEncoding := TEncoding.GetEncoding(ISO_8859_1_CODEPAGE);
  FOwnsEncoding := True;
end;

destructor TOSFUnifiedCSVExporter.Destroy;
begin
  if FOwnsEncoding then
    FEncoding.Free;
  inherited;
end;

procedure TOSFUnifiedCSVExporter.SetEncoding(AValue: TEncoding);
begin
  if AValue = FEncoding then
    Exit;
  if FOwnsEncoding then
    FEncoding.Free;
  FEncoding := AValue;
  FOwnsEncoding := False;
end;

// ── Per-row helpers ─────────────────────────────────────────────────────────

function TOSFUnifiedCSVExporter.CellFromValue(AChannel: TOSFDataChannel;
  ASampleIdx: Integer): string;
begin
  Result := AChannel.ValueAsString(ASampleIdx);
  if (FDecimalSeparator <> '.') and IsSingleNumericType(AChannel.OriginalDataType) then
    Result := StringReplace(Result, '.', FDecimalSeparator, [rfReplaceAll]);
end;

function TOSFUnifiedCSVExporter.FormatTimestamp(ANs: Int64): string;
var
  Dt: TDateTime;
  Ms: Int64;
  Frac: Int64;
begin
  case FTimestampFormat of
    tfDateTime:
      begin
        Dt := UnixNsToUtcDateTime(ANs);
        // Always emit '.' as the sub-second separator regardless of
        // FDecimalSeparator — it is a structural marker for the time
        // string, not a numeric value the user wants reformatted.
        Ms := (ANs mod 1000000000) div 1000000;
        Result := FormatDateTime('dd.mm.yyyy hh:nn:ss', Dt) + '.' + Format('%.3d', [Ms]);
      end;
    tfSeconds:
      begin
        Dt := UnixNsToUtcDateTime(ANs);
        Result := FormatDateTime('dd.mm.yyyy hh:nn:ss', Dt);
      end;
    tfISO8601:
      begin
        Dt := UnixNsToUtcDateTime(ANs);
        Frac := ANs mod 1000000000;
        Result := FormatDateTime('yyyy-mm-dd"T"hh:nn:ss', Dt) + '.' + Format('%.9d', [Frac]) + 'Z';
      end;
    tfNanoseconds:
      Result := IntToStr(ANs);
  else
    Result := IntToStr(ANs);
  end;
end;

procedure TOSFUnifiedCSVExporter.WriteLine(AStream: TStream; const ALine: string);
var
  Bytes: TBytes;
begin
  Bytes := FEncoding.GetBytes(ALine + CSV_LINE_TERMINATOR);
  if Length(Bytes) > 0 then
    AStream.WriteBuffer(Bytes[0], Length(Bytes));
end;

// ── Timeline + headers ──────────────────────────────────────────────────────

function TOSFUnifiedCSVExporter.BuildTimeline(
  const AChannels: TArray<TOSFDataChannel>): TList<Int64>;
var
  Ch: TOSFDataChannel;
  I, ReadIdx, WriteIdx: Integer;
begin
  // First pass: collect every per-channel timestamp into one flat list,
  // then sort and dedupe in-place. The alternative — a k-way merge over
  // per-channel cursors with a heap — is more memory-efficient for
  // wide-and-long inputs, but is more code than this verb needs today.
  Result := TList<Int64>.Create;
  try
    for Ch in AChannels do
      for I := 0 to Ch.SampleCount - 1 do
        Result.Add(Ch.TimestampNsAt(I));
    Result.Sort;

    // In-place dedupe of the sorted list. WriteIdx is one past the last
    // kept value; we skip a value when it equals its left neighbour.
    if Result.Count > 1 then
    begin
      WriteIdx := 1;
      for ReadIdx := 1 to Result.Count - 1 do
        if Result[ReadIdx] <> Result[ReadIdx - 1] then
        begin
          if WriteIdx <> ReadIdx then
            Result[WriteIdx] := Result[ReadIdx];
          Inc(WriteIdx);
        end;
      Result.Count := WriteIdx;
    end;
  except
    Result.Free;
    raise;
  end;
end;

function TOSFUnifiedCSVExporter.BuildHeaderRow1(
  const AChannels: TArray<TOSFDataChannel>): string;
var
  Builder: TStringBuilder;
  I: Integer;
begin
  Builder := TStringBuilder.Create;
  try
    Builder.Append('Timestamp');
    for I := 0 to High(AChannels) do
    begin
      Builder.Append(FColumnSeparator);
      Builder.Append(AChannels[I].Name);
    end;
    Result := Builder.ToString;
  finally
    Builder.Free;
  end;
end;

function TOSFUnifiedCSVExporter.BuildHeaderRow2(
  const AChannels: TArray<TOSFDataChannel>): string;
var
  Builder: TStringBuilder;
  I: Integer;
begin
  Builder := TStringBuilder.Create;
  try
    Builder.Append(TimestampFormatLabel(FTimestampFormat));
    for I := 0 to High(AChannels) do
    begin
      Builder.Append(FColumnSeparator);
      Builder.Append(AChannels[I].PhysicalUnit);
    end;
    Result := Builder.ToString;
  finally
    Builder.Free;
  end;
end;

// ── DoExport ────────────────────────────────────────────────────────────────

procedure TOSFUnifiedCSVExporter.DoExport(const AFileName: string);
var
  Channels: TArray<TOSFDataChannel>;
  Timeline: TList<Int64>;
  Cursors: TArray<Integer>;
  Stream: TFileStream;
  Builder: TStringBuilder;
  I, Idx: Integer;
  Ts: Int64;
  Ch: TOSFDataChannel;
  Cursor: Integer;
  Rows: Integer;
  BytesWritten: Int64;
begin
  Channels := ActiveChannels;
  Log(llInfo, SOSFLogUnifiedStarted, [AFileName, Length(Channels)]);
  try
    Timeline := BuildTimeline(Channels);
    try
      Log(llInfo, SOSFLogUnifiedTimeline, [Timeline.Count]);

      SetLength(Cursors, Length(Channels));
      for I := 0 to High(Cursors) do
        Cursors[I] := 0;

      Stream := TFileStream.Create(AFileName, fmCreate);
      try
        // Two header rows: column names then unit row (which doubles as
        // the timestamp-format label for column 0).
        WriteLine(Stream, BuildHeaderRow1(Channels));
        WriteLine(Stream, BuildHeaderRow2(Channels));

        Rows := 0;
        Builder := TStringBuilder.Create;
        try
          for Idx := 0 to Timeline.Count - 1 do
          begin
            Ts := Timeline[Idx];
            Builder.Clear;
            Builder.Append(FormatTimestamp(Ts));
            for I := 0 to High(Channels) do
            begin
              Builder.Append(FColumnSeparator);
              Ch := Channels[I];
              Cursor := Cursors[I];
              // O(1) match check: if this channel's next-to-consume
              // sample carries the timeline timestamp, emit its value
              // and advance the cursor; otherwise leave the cell empty.
              if (Cursor < Ch.SampleCount) and (Ch.TimestampNsAt(Cursor) = Ts) then
              begin
                Builder.Append(CellFromValue(Ch, Cursor));
                Cursors[I] := Cursor + 1;
              end;
            end;
            WriteLine(Stream, Builder.ToString);
            Inc(Rows);
          end;
        finally
          Builder.Free;
        end;

        BytesWritten := Stream.Size;
      finally
        Stream.Free;
      end;

      Log(llInfo, SOSFLogUnifiedFinished, [AFileName, Rows, BytesWritten]);
    finally
      Timeline.Free;
    end;
  except
    on E: Exception do
    begin
      Log(llError, SOSFLogUnifiedFailed, [E.Message]);
      raise;
    end;
  end;
end;

end.
