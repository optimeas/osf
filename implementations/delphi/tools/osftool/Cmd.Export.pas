// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

// "export" verb. Hands a TOSFDataManager to TOSFCSVExporter. When
// --start / --end are given, the source is routed through TOSFMerger
// (single-file FileList) so interval clipping happens before the
// CSV writer sees the data.
unit Cmd.Export;

interface

uses
  System.SysUtils,
  System.IOUtils,
  System.DateUtils,
  System.JSON,
  Cmd.Base,
  OSF.Types,
  OSF.Log,
  OSF.Data.Manager,
  OSF.Export,
  OSF.Export.CSV,
  OSF.Export.CSV.Unified,
  {$IFDEF MSWINDOWS}
  OSF.Export.HDF5,
  {$ENDIF}
  OSF.Merger,
  OsfToolConfig;

type
  TOsfExportCommand = class(TBaseCommand)
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

resourcestring
  SExportDesc = 'Export OSF channels to CSV or other formats';
  SExportHelp =
    'osftool export <inputfile> <outputfile> [channel ...] [options]' + sLineBreak +
    sLineBreak +
    'Arguments:' + sLineBreak +
    '  inputfile    Source .osf or .osfz' + sLineBreak +
    '  outputfile   Output file path' + sLineBreak +
    '  channel      Optional channel names (omit for all)' + sLineBreak +
    sLineBreak +
    'Options:' + sLineBreak +
    '  --format <fmt>            Output format: csv (default), unified-csv, hdf5' + sLineBreak +
    '  --timestamp-format <fmt>  Timestamp format for unified-csv:' + sLineBreak +
    '                              datetime (default), seconds, iso8601, nanoseconds' + sLineBreak +
    '  --start <time>            Only export samples from this UTC time (ISO 8601)' + sLineBreak +
    '  --end <time>              Only export samples up to this UTC time (ISO 8601)' + sLineBreak +
    '  --decimal-sep <c>         Decimal separator: comma (default) or dot' + sLineBreak +
    '  --encoding <enc>          iso-8859-1 (default) or utf-8' + sLineBreak +
    '  --chunk-size <n>          hdf5: samples per chunk (default 8192)' + sLineBreak +
    '  --deflate-level <n>       hdf5: gzip level 0-9 (default 4)' + sLineBreak +
    '  --no-shuffle              hdf5: disable the shuffle filter' + sLineBreak +
    '  --namespace-sep <c>       hdf5: channel-name separator (default ".")' + sLineBreak +
    '  --hdf5-lib-dir <path>     hdf5: directory to search for hdf5.dll' + sLineBreak +
    '  --exclude-empty           Skip channels with 0 samples' + sLineBreak +
    '  --json                    Result summary as JSON' + sLineBreak +
    '  --quiet / --verbose';
  SExportErrExpectArgs        = 'osftool export: expected <inputfile> <outputfile>';
  SExportErrBadFormat         = 'osftool export: unsupported format "%s" (expected csv, unified-csv or hdf5)';
  SExportErrBadTsFormat       = 'osftool export: unknown --timestamp-format "%s" (expected datetime / seconds / iso8601 / nanoseconds)';
  SExportErrInputNotFound     = 'osftool export: input file not found: %s';
  SExportErrBadDecimalSep     = 'osftool export: invalid decimal separator "%s"';
  SExportErrStartNoEnd        = 'osftool export: --start without --end is not allowed';
  SExportErrEndNoStart        = 'osftool export: --end without --start is not allowed';
  SExportErrInvalidStart      = 'osftool export: invalid --start: %s';
  SExportErrInvalidEnd        = 'osftool export: invalid --end: %s';
  SExportErrFilterMergeFailed = 'osftool export: filter-merge failed: %s';
  SExportErrLoadFailed        = 'osftool export: load failed: %s';
  SExportErrWriteFailed       = 'osftool export: write failed: %s';
  SExportWritten              = 'Written: %s (%d channels, %d bytes)';

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

// ── TOsfExportCommand ───────────────────────────────────────────────────────

function TOsfExportCommand.Name: string;
begin
  Result := 'export';
end;

function TOsfExportCommand.ShortDescription: string;
begin
  Result := SExportDesc;
end;

procedure TOsfExportCommand.PrintHelp;
begin
  Print(SExportHelp);
end;

function TOsfExportCommand.DoExecute: Integer;
var
  Positionals: TArray<string>;
  InputFile, OutputFile, FmtName, DecStr, EncName, TsFmtStr: string;
  StartStr, EndStr: string;
  StartUtc, EndUtc: TDateTime;
  HasInterval, UseUtf8, FormatOk: Boolean;
  {$IFDEF MSWINDOWS}
  NsSep: string;
  {$ENDIF}
  Channels: TArray<string>;
  Mgr, OwnedMgr: TOSFDataManager;
  Merger: TOSFMerger;
  Exporter: TOSFExporter;
  Cfg: TOsfToolConfig;
  DecSep: Char;
  TsFmt: TUnifiedCSVTimestampFormat;
  I: Integer;
  OutSize: Int64;
  SummaryObj: TJSONObject;
begin
  Positionals := PositionalArgs([
    '--format', '--timestamp-format',
    '--start', '--end',
    '--decimal-sep', '--encoding',
    '--chunk-size', '--deflate-level', '--hdf5-lib-dir', '--namespace-sep']);
  if Length(Positionals) < 2 then
  begin
    PrintErr(SExportErrExpectArgs);
    Exit(EXIT_BAD_ARGS);
  end;
  InputFile := Positionals[0];
  OutputFile := Positionals[1];

  FmtName := LowerCase(FlagValue('--format', 'csv'));
  FormatOk := (FmtName = 'csv') or (FmtName = 'unified-csv');
  {$IFDEF MSWINDOWS}
  FormatOk := FormatOk or (FmtName = 'hdf5');
  {$ENDIF}
  if not FormatOk then
  begin
    PrintErrf(SExportErrBadFormat, [FmtName]);
    Exit(EXIT_BAD_ARGS);
  end;

  // --timestamp-format only affects unified-csv. We still parse it
  // unconditionally so an invalid value fails loudly rather than being
  // silently dropped on a future format switch.
  TsFmtStr := LowerCase(FlagValue('--timestamp-format', 'datetime'));
  if TsFmtStr = 'datetime' then
    TsFmt := tfDateTime
  else if TsFmtStr = 'seconds' then
    TsFmt := tfSeconds
  else if (TsFmtStr = 'iso8601') or (TsFmtStr = 'iso-8601') then
    TsFmt := tfISO8601
  else if (TsFmtStr = 'nanoseconds') or (TsFmtStr = 'ns') then
    TsFmt := tfNanoseconds
  else
  begin
    PrintErrf(SExportErrBadTsFormat,
      [TsFmtStr]);
    Exit(EXIT_BAD_ARGS);
  end;

  if not TFile.Exists(InputFile) then
  begin
    PrintErrf(SExportErrInputNotFound, [InputFile]);
    Exit(EXIT_NOT_FOUND);
  end;

  Cfg := TOsfToolConfig.Create;
  try
    Cfg.Load;
    DecStr := FlagValue('--decimal-sep', Cfg.Get(C_KEY_EXPORT_DECIMAL_SEP));
    EncName := LowerCase(FlagValue('--encoding', Cfg.Get(C_KEY_EXPORT_ENCODING)));
  finally
    Cfg.Free;
  end;

  if SameText(DecStr, 'dot') then
    DecSep := '.'
  else if (DecStr = ',') or SameText(DecStr, 'comma') then
    DecSep := ','
  else if Length(DecStr) = 1 then
    DecSep := DecStr[1]
  else
  begin
    PrintErrf(SExportErrBadDecimalSep, [DecStr]);
    Exit(EXIT_BAD_ARGS);
  end;

  StartStr := FlagValue('--start', '');
  EndStr := FlagValue('--end', '');
  HasInterval := (StartStr <> '') and (EndStr <> '');
  if (StartStr <> '') and (EndStr = '') then
  begin
    PrintErr(SExportErrStartNoEnd);
    Exit(EXIT_BAD_ARGS);
  end;
  if (EndStr <> '') and (StartStr = '') then
  begin
    PrintErr(SExportErrEndNoStart);
    Exit(EXIT_BAD_ARGS);
  end;

  StartUtc := 0; EndUtc := 0;
  if HasInterval then
  begin
    if not ParseIso8601(StartStr, StartUtc) then
    begin
      PrintErrf(SExportErrInvalidStart, [StartStr]);
      Exit(EXIT_BAD_ARGS);
    end;
    if not ParseIso8601(EndStr, EndUtc) then
    begin
      PrintErrf(SExportErrInvalidEnd, [EndStr]);
      Exit(EXIT_BAD_ARGS);
    end;
  end;

  // Remaining positionals are channel names.
  SetLength(Channels, Length(Positionals) - 2);
  for I := 2 to High(Positionals) do
    Channels[I - 2] := Positionals[I];

  // Acquire a TOSFDataManager. With an interval, route through TOSFMerger
  // to get interval clipping for free; without one, load directly so we
  // do not pay the merger's in-memory accumulator overhead.
  OwnedMgr := nil;
  Merger := nil;
  try
    if HasInterval then
    begin
      Merger := TOSFMerger.Create;
      Merger.OnLog := HandleLog;
      Merger.DebugEnabled := FVerbose;
      Merger.FileList := [InputFile];
      Merger.SetInterval(StartUtc, EndUtc);
      Merger.ChannelFilter := Channels;
      try
        OwnedMgr := Merger.Merge;
      except
        on E: Exception do
        begin
          PrintErrf(SExportErrFilterMergeFailed, [E.Message]);
          Exit(EXIT_IO_ERROR);
        end;
      end;
      Mgr := OwnedMgr;
    end
    else
    begin
      OwnedMgr := TOSFDataManager.Create;
      OwnedMgr.OnLog := HandleLog;
      OwnedMgr.DebugEnabled := FVerbose;
      OwnedMgr.ChannelFilter := Channels;
      try
        OwnedMgr.LoadFromFile(InputFile);
      except
        on E: Exception do
        begin
          PrintErrf(SExportErrLoadFailed, [E.Message]);
          Exit(EXIT_FORMAT_ERROR);
        end;
      end;
      Mgr := OwnedMgr;
    end;

    UseUtf8 := SameText(EncName, 'utf-8') or SameText(EncName, 'utf8');
    {$IFDEF MSWINDOWS}
    if FmtName = 'hdf5' then
      Exporter := TOSFHDF5Exporter.Create(Mgr)
    else
    {$ENDIF}
    if FmtName = 'unified-csv' then
      Exporter := TOSFUnifiedCSVExporter.Create(Mgr)
    else
      Exporter := TOSFCSVExporter.Create(Mgr);
    try
      Exporter.OnLog := HandleLog;
      Exporter.DebugEnabled := FVerbose;
      Exporter.ExcludeEmptyChannels := HasFlag('--exclude-empty');
      // The two exporter classes do not share a property base so we
      // type-dispatch here. Property semantics are identical in both:
      // DecimalSeparator is a Char, Encoding is owned by the exporter
      // only when the exporter constructed it itself.
      {$IFDEF MSWINDOWS}
      if Exporter is TOSFHDF5Exporter then
      begin
        TOSFHDF5Exporter(Exporter).ChunkSize :=
          StrToIntDef(FlagValue('--chunk-size', ''), 8192);
        TOSFHDF5Exporter(Exporter).DeflateLevel :=
          StrToIntDef(FlagValue('--deflate-level', ''), 4);
        TOSFHDF5Exporter(Exporter).UseShuffle := not HasFlag('--no-shuffle');
        TOSFHDF5Exporter(Exporter).LibraryDir := FlagValue('--hdf5-lib-dir', '');
        NsSep := FlagValue('--namespace-sep', '');
        if NsSep <> '' then
          TOSFHDF5Exporter(Exporter).NamespaceSep := NsSep;
      end
      else
      {$ENDIF}
      if Exporter is TOSFUnifiedCSVExporter then
      begin
        TOSFUnifiedCSVExporter(Exporter).DecimalSeparator := DecSep;
        TOSFUnifiedCSVExporter(Exporter).TimestampFormat := TsFmt;
        if UseUtf8 then
          TOSFUnifiedCSVExporter(Exporter).Encoding := TEncoding.UTF8;
      end
      else
      begin
        TOSFCSVExporter(Exporter).DecimalSeparator := DecSep;
        if UseUtf8 then
          TOSFCSVExporter(Exporter).Encoding := TEncoding.UTF8;
      end;
      try
        Exporter.Export(OutputFile);
      except
        on E: Exception do
        begin
          PrintErrf(SExportErrWriteFailed, [E.Message]);
          Exit(EXIT_IO_ERROR);
        end;
      end;
    finally
      Exporter.Free;
    end;

    OutSize := 0;
    if TFile.Exists(OutputFile) then
      OutSize := TFile.GetSize(OutputFile);

    if FJson then
    begin
      SummaryObj := TJSONObject.Create;
      try
        SummaryObj.AddPair('status', 'ok');
        SummaryObj.AddPair('input_file', InputFile);
        SummaryObj.AddPair('output_file', OutputFile);
        SummaryObj.AddPair('channels', TJSONNumber.Create(Mgr.ChannelCount));
        SummaryObj.AddPair('output_size', TJSONNumber.Create(OutSize));
        PrintJson(SummaryObj.Format(2));
      finally
        SummaryObj.Free;
      end;
    end
    else
      Printf(SExportWritten,
        [OutputFile, Mgr.ChannelCount, OutSize]);
  finally
    OwnedMgr.Free;
    Merger.Free;
  end;
  Result := EXIT_OK;
end;

end.
