// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

// Builds the suite of demo OSF files used for visual testing. Every method
// is self-contained — a failure in one file does not stop the others.
//
// Per spec revision 2026-05-24 the trailing 0x00 byte on `string` and
// `binary` values inside bcAbsTimeStampData is version-deterministic:
// OSF4 writers append it (handled centrally in OSF.Filer), OSF5 writers
// do not. The generator passes the bare payload bytes in both cases;
// OSF.Filer adds the terminator only for the OSF4 path based on the
// file's FVersion.
unit OSFDemoGenerator;

interface

uses
  System.SysUtils,
  System.IOUtils,
  System.Math,
  System.StrUtils,
  OSF.Types,
  OSF.Log,
  OSF.Channel,
  OSF.Filer;

type
  TOSFDemoGenerator = class;

  // Per-file generator callback. Receives the full file path so the wrapper
  // RunOne knows which file to log on success or failure.
  TOSFGenMethod = procedure(const FullPath: string; Version: TOSFVersion;
                            SampleCount: Integer) of object;

  TOSFDemoGenerator = class
  private
    FOnLog: TOSFLogEvent;

    procedure Log(Level: TOSFLogLevel; const Fmt: string; const Args: array of const);

    // Sets file metadata to the values defined in the brief. Called for
    // every generated file before WriteHeader.
    procedure ConfigureMetadata(Filer: TOSFFile);

    // Returns the current UTC time in nanoseconds since Unix epoch.
    function NowNs: Int64;

    // Returns a pseudo-random per-sample interval in nanoseconds, between
    // 10 ms and 200 ms inclusive.
    function RandomIntervalNs: Int64;

    // Records a successful file write to the log, computing the on-disk size
    // after the filer has flushed.
    procedure LogFileResult(const FullPath: string; ChannelCount, SampleCount: Integer);

    // Per-file generators — each is wrapped in try/except in RunOne.
    procedure GenScalarNumeric    (const FullPath: string; Version: TOSFVersion; SampleCount: Integer);
    procedure GenScalarUnsigned   (const FullPath: string; Version: TOSFVersion; SampleCount: Integer);
    procedure GenScalarInt64      (const FullPath: string; Version: TOSFVersion; SampleCount: Integer);
    procedure GenTimestampedString(const FullPath: string; Version: TOSFVersion; SampleCount: Integer);
    procedure GenTimestampedBinary(const FullPath: string; Version: TOSFVersion; SampleCount: Integer);
    procedure GenGpsLocation      (const FullPath: string; Version: TOSFVersion; SampleCount: Integer);
    procedure GenEquidistant      (const FullPath: string; Version: TOSFVersion; SampleCount: Integer);
    procedure GenMixed            (const FullPath: string; Version: TOSFVersion; SampleCount: Integer);
    procedure GenMixedExtended    (const FullPath: string; Version: TOSFVersion; SampleCount: Integer);

    // Helper that wraps one file's generation: dispatches to Body, logs
    // success or failure. Body itself owns the TOSFFile and is responsible
    // for closing it before returning.
    procedure RunOne(const OutputDir, FileName: string;
                     Version: TOSFVersion;
                     SampleCount: Integer;
                     Body: TOSFGenMethod);
  public
    // Generates every demo file for the given version into OutputDir.
    // Creates OutputDir if it does not exist.
    procedure GenerateAll(const OutputDir: string;
                          Version: TOSFVersion;
                          SampleCount: Integer);
    property OnLog: TOSFLogEvent read FOnLog write FOnLog;
  end;

resourcestring
  SLogWritten          = 'Written: %s (%d bytes, %d channels, %d samples)';
  SLogFailed           = 'Failed: %s — %s';
  SLogStartingVersion  = 'Generating %s files in %s';
  SLogVersionFinished  = 'Done with %s';

implementation

uses
  System.Classes;

const
  CREATOR_STR  = 'OSFGenerator/1.0';
  TAG_STR      = 'demo';
  REASON_STR   = 'GENERATOR';
  CENTRE_LAT   = 48.1374;
  CENTRE_LON   = 11.5755;
  CENTRE_ALT   = 519.0;
  GPS_RADIUS   = 0.01;            // degrees
  BINARY_SIZE  = 64;              // bytes per binary sample (excluding the
                                  // 0x00 the filer appends automatically)
  PNG_MIME     = 'image/png';

// ── Encoding helpers ────────────────────────────────────────────────────────

function ToBytes(const Buffer; Size: Integer): TBytes;
begin
  SetLength(Result, Size);
  if Size > 0 then
    Move(Buffer, Result[0], Size);
end;

function DoubleBytes(V: Double): TBytes;     begin Result := ToBytes(V, SizeOf(V)); end;
function FloatBytes(V: Single): TBytes;      begin Result := ToBytes(V, SizeOf(V)); end;
function Int32Bytes(V: Int32): TBytes;       begin Result := ToBytes(V, SizeOf(V)); end;
function Int16Bytes(V: SmallInt): TBytes;    begin Result := ToBytes(V, SizeOf(V)); end;
function Int8Bytes(V: ShortInt): TBytes;     begin Result := ToBytes(V, SizeOf(V)); end;
function Int64Bytes(V: Int64): TBytes;       begin Result := ToBytes(V, SizeOf(V)); end;
function UInt8Bytes(V: Byte): TBytes;        begin Result := ToBytes(V, SizeOf(V)); end;
function UInt16Bytes(V: Word): TBytes;       begin Result := ToBytes(V, SizeOf(V)); end;
function UInt32Bytes(V: UInt32): TBytes;     begin Result := ToBytes(V, SizeOf(V)); end;
function UInt64Bytes(V: UInt64): TBytes;     begin Result := ToBytes(V, SizeOf(V)); end;

function BoolBytes(V: Boolean): TBytes;
var
  B: Byte;
begin
  if V then B := 1 else B := 0;
  Result := ToBytes(B, 1);
end;

function StringBytes(const S: string): TBytes;
begin
  // UTF-8 payload only — the filer appends the trailing 0x00 itself.
  Result := TEncoding.UTF8.GetBytes(S);
end;

function BinaryPayload(SampleIdx: Integer): TBytes;
var
  I: Integer;
  B: Byte;
begin
  SetLength(Result, BINARY_SIZE);
  B := SampleIdx mod 256;
  for I := 0 to BINARY_SIZE - 1 do
    Result[I] := B;
end;

function GpsBytes(SampleIdx, SampleCount: Integer): TBytes;
var
  Loc   : TOSFGpsLocation;
  Theta : Double;
begin
  if SampleCount > 0 then
    Theta := 2.0 * Pi * SampleIdx / SampleCount
  else
    Theta := 0;
  Loc.Latitude  := CENTRE_LAT + GPS_RADIUS * Sin(Theta);
  Loc.Longitude := CENTRE_LON + GPS_RADIUS * Cos(Theta);
  Loc.Altitude  := CENTRE_ALT + SampleIdx;
  Result := ToBytes(Loc, SizeOf(Loc));
end;

// ── Channel-builder helpers ──────────────────────────────────────────────────

function MakeTimestampedScalar(Index: Integer; const Name: string;
  DataType: TOSFDataType; const PhysicalUnit: string = ''): TOSFChannelDef;
begin
  Result := TOSFChannelDef.Create(Index, Name, ctScalar, DataType);
  Result.PhysicalUnit := PhysicalUnit;
  // Timestamped channel: TimeIncrement stays 0.
end;

function MakeEquidistantDouble(Index: Integer; const Name: string;
  SampleRateHz: Double; const PhysicalUnit: string = ''): TOSFChannelDef;
begin
  Result := TOSFChannelDef.Create(Index, Name, ctScalar, dtDouble);
  Result.PhysicalUnit  := PhysicalUnit;
  Result.SampleRate    := SampleRateHz;
  // Hint in metablock; bcStartData carries the authoritative rate.
  Result.TimeIncrement := Round(1.0e9 / SampleRateHz);
end;

function MakeBinaryChannel(Index: Integer; const Name, MimeType: string): TOSFChannelDef;
begin
  Result := TOSFChannelDef.Create(Index, Name, ctScalar, dtBinary);
  Result.MimeType        := MimeType;
  Result.LengthFieldSize := lfs4;     // binary blocks may exceed 64 kB
end;

// ── TOSFDemoGenerator ───────────────────────────────────────────────────────

procedure TOSFDemoGenerator.Log(Level: TOSFLogLevel; const Fmt: string; const Args: array of const);
begin
  if not Assigned(FOnLog) then
    Exit;
  try
    FOnLog(Level, Format(Fmt, Args));
  except
    // Never propagate from a buggy log handler or a broken Format string.
  end;
end;

procedure TOSFDemoGenerator.ConfigureMetadata(Filer: TOSFFile);
var
  Meta: TOSFFileMetadata;
begin
  Meta             := Filer.Metadata;
  Meta.Creator     := CREATOR_STR;
  Meta.Tag         := TAG_STR;
  Meta.Reason      := REASON_STR;
  Meta.Latitude    := CENTRE_LAT;
  Meta.Longitude   := CENTRE_LON;
  Meta.Altitude    := CENTRE_ALT;
  Filer.Metadata   := Meta;
end;

function TOSFDemoGenerator.NowNs: Int64;
begin
  Result := OSFNowAsUnixNs;
end;

function TOSFDemoGenerator.RandomIntervalNs: Int64;
begin
  // 10 .. 200 ms, in 1 ms steps.
  Result := Int64(10 + Random(191)) * 1_000_000;
end;

procedure TOSFDemoGenerator.RunOne(const OutputDir, FileName: string;
                                    Version: TOSFVersion;
                                    SampleCount: Integer;
                                    Body: TOSFGenMethod);
var
  FullPath: string;
begin
  FullPath := TPath.Combine(OutputDir, FileName);
  try
    Body(FullPath, Version, SampleCount);
  except
    on E: Exception do
      Log(llError, SLogFailed, [FileName, E.Message]);
  end;
end;

procedure TOSFDemoGenerator.GenerateAll(const OutputDir: string;
                                         Version: TOSFVersion;
                                         SampleCount: Integer);
var
  Prefix: string;
begin
  if not TDirectory.Exists(OutputDir) then
    TDirectory.CreateDirectory(OutputDir);

  case Version of
    osvOSF4: Prefix := 'osf4_';
    osvOSF5: Prefix := 'osf5_';
  else
    raise EOSFException.Create('GenerateAll: unsupported version');
  end;

  Log(llInfo, SLogStartingVersion,
      [IfThen(Version = osvOSF4, 'OSF4', 'OSF5'), OutputDir]);

  // Re-seed Randomize once per run so timestamps differ across invocations.
  Randomize;

  RunOne(OutputDir, Prefix + 'scalar_numeric.osf',     Version, SampleCount, GenScalarNumeric);
  RunOne(OutputDir, Prefix + 'scalar_unsigned.osf',    Version, SampleCount, GenScalarUnsigned);
  RunOne(OutputDir, Prefix + 'scalar_int64.osf',       Version, SampleCount, GenScalarInt64);
  RunOne(OutputDir, Prefix + 'timestamped_string.osf', Version, SampleCount, GenTimestampedString);
  RunOne(OutputDir, Prefix + 'timestamped_binary.osf', Version, SampleCount, GenTimestampedBinary);
  RunOne(OutputDir, Prefix + 'gpslocation.osf',        Version, SampleCount, GenGpsLocation);
  RunOne(OutputDir, Prefix + 'equidistant.osf',        Version, SampleCount, GenEquidistant);
  RunOne(OutputDir, Prefix + 'mixed.osf',              Version, SampleCount, GenMixed);

  if Version = osvOSF5 then
    RunOne(OutputDir, 'osf5_mixed_extended.osf', osvOSF5, SampleCount, GenMixedExtended);

  Log(llInfo, SLogVersionFinished,
      [IfThen(Version = osvOSF4, 'OSF4', 'OSF5')]);
end;

procedure TOSFDemoGenerator.LogFileResult(const FullPath: string;
  ChannelCount, SampleCount: Integer);
var
  Size: Int64;
begin
  Size := 0;
  if TFile.Exists(FullPath) then
    Size := TFile.GetSize(FullPath);
  Log(llInfo, SLogWritten,
      [TPath.GetFileName(FullPath), Size, ChannelCount, SampleCount]);
end;

// ── Per-file generators ──────────────────────────────────────────────────────

procedure TOSFDemoGenerator.GenScalarNumeric(const FullPath: string;
  Version: TOSFVersion; SampleCount: Integer);
var
  Filer    : TOSFFile;
  StartNs  : Int64;
  TsCh     : array[0..5] of Int64;
  I, K, N  : Integer;
  Theta    : Double;
begin
  Filer := TOSFFile.Create;
  try
    Filer.CreateForWrite(FullPath, Version);
    ConfigureMetadata(Filer);

    Filer.AddChannel(MakeTimestampedScalar(0, 'Sensor/Double', dtDouble, 'V'));
    Filer.AddChannel(MakeTimestampedScalar(1, 'Sensor/Float',  dtFloat,  'A'));
    Filer.AddChannel(MakeTimestampedScalar(2, 'Sensor/Int32',  dtInt32));
    Filer.AddChannel(MakeTimestampedScalar(3, 'Sensor/Int16',  dtInt16));
    Filer.AddChannel(MakeTimestampedScalar(4, 'Sensor/Int8',   dtInt8));
    Filer.AddChannel(MakeTimestampedScalar(5, 'Sensor/Bool',   dtBool));

    Filer.WriteHeader;

    StartNs := NowNs;
    for K := 0 to 5 do
      TsCh[K] := StartNs;

    N := SampleCount;
    for I := 0 to N - 1 do
    begin
      if N > 0 then
        Theta := 2.0 * Pi * I / N
      else
        Theta := 0;
      TsCh[0] := TsCh[0] + RandomIntervalNs; Filer.WriteTimestampedSample(0, TsCh[0], DoubleBytes(Sin(Theta) * 100.0));
      TsCh[1] := TsCh[1] + RandomIntervalNs; Filer.WriteTimestampedSample(1, TsCh[1], FloatBytes(Single(Cos(Theta) * 50.0)));
      TsCh[2] := TsCh[2] + RandomIntervalNs; Filer.WriteTimestampedSample(2, TsCh[2], Int32Bytes(I mod 32767));
      TsCh[3] := TsCh[3] + RandomIntervalNs; Filer.WriteTimestampedSample(3, TsCh[3], Int16Bytes(SmallInt(I mod 32767)));
      TsCh[4] := TsCh[4] + RandomIntervalNs; Filer.WriteTimestampedSample(4, TsCh[4], Int8Bytes(ShortInt(I mod 127)));
      TsCh[5] := TsCh[5] + RandomIntervalNs; Filer.WriteTimestampedSample(5, TsCh[5], BoolBytes((I mod 10) < 5));
    end;

    Filer.Close;
    LogFileResult(FullPath, 6, SampleCount);
  finally
    Filer.Free;
  end;
end;

procedure TOSFDemoGenerator.GenScalarUnsigned(const FullPath: string;
  Version: TOSFVersion; SampleCount: Integer);
var
  Filer   : TOSFFile;
  StartNs : Int64;
  TsCh    : array[0..3] of Int64;
  I, K, N : Integer;
begin
  Filer := TOSFFile.Create;
  try
    Filer.CreateForWrite(FullPath, Version);
    ConfigureMetadata(Filer);

    Filer.AddChannel(MakeTimestampedScalar(0, 'Sensor/UInt8',  dtUInt8));
    Filer.AddChannel(MakeTimestampedScalar(1, 'Sensor/UInt16', dtUInt16));
    Filer.AddChannel(MakeTimestampedScalar(2, 'Sensor/UInt32', dtUInt32));
    Filer.AddChannel(MakeTimestampedScalar(3, 'Sensor/UInt64', dtUInt64));

    Filer.WriteHeader;

    StartNs := NowNs;
    for K := 0 to 3 do
      TsCh[K] := StartNs;

    N := SampleCount;
    for I := 0 to N - 1 do
    begin
      TsCh[0] := TsCh[0] + RandomIntervalNs; Filer.WriteTimestampedSample(0, TsCh[0], UInt8Bytes(Byte(I mod 256)));
      TsCh[1] := TsCh[1] + RandomIntervalNs; Filer.WriteTimestampedSample(1, TsCh[1], UInt16Bytes(Word(I mod 65535)));
      TsCh[2] := TsCh[2] + RandomIntervalNs; Filer.WriteTimestampedSample(2, TsCh[2], UInt32Bytes(UInt32(I mod 100000)));
      TsCh[3] := TsCh[3] + RandomIntervalNs; Filer.WriteTimestampedSample(3, TsCh[3], UInt64Bytes(UInt64(I) * 1000000));
    end;

    Filer.Close;
    LogFileResult(FullPath, 4, SampleCount);
  finally
    Filer.Free;
  end;
end;

procedure TOSFDemoGenerator.GenScalarInt64(const FullPath: string;
  Version: TOSFVersion; SampleCount: Integer);
var
  Filer   : TOSFFile;
  Ts      : Int64;
  I, N    : Integer;
begin
  Filer := TOSFFile.Create;
  try
    Filer.CreateForWrite(FullPath, Version);
    ConfigureMetadata(Filer);

    Filer.AddChannel(MakeTimestampedScalar(0, 'Sensor/Int64Counter', dtInt64));
    Filer.WriteHeader;

    Ts := NowNs;
    N  := SampleCount;
    for I := 0 to N - 1 do
    begin
      Ts := Ts + RandomIntervalNs;
      Filer.WriteTimestampedSample(0, Ts, Int64Bytes(Int64(I) * 1000000));
    end;

    Filer.Close;
    LogFileResult(FullPath, 1, SampleCount);
  finally
    Filer.Free;
  end;
end;

procedure TOSFDemoGenerator.GenTimestampedString(const FullPath: string;
  Version: TOSFVersion; SampleCount: Integer);
var
  Filer   : TOSFFile;
  Ch      : TOSFChannelDef;
  Ts      : Int64;
  I, N    : Integer;
begin
  Filer := TOSFFile.Create;
  try
    Filer.CreateForWrite(FullPath, Version);
    ConfigureMetadata(Filer);

    Ch := TOSFChannelDef.Create(0, 'Sensor/Message', ctScalar, dtString);
    Ch.LengthFieldSize := lfs4;     // strings may grow beyond 64 kB
    Filer.AddChannel(Ch);

    Filer.WriteHeader;

    Ts := NowNs;
    N  := SampleCount;
    for I := 0 to N - 1 do
    begin
      Ts := Ts + RandomIntervalNs;
      Filer.WriteTimestampedSample(0, Ts, StringBytes('Sample_' + IntToStr(I)));
    end;

    Filer.Close;
    LogFileResult(FullPath, 1, SampleCount);
  finally
    Filer.Free;
  end;
end;

procedure TOSFDemoGenerator.GenTimestampedBinary(const FullPath: string;
  Version: TOSFVersion; SampleCount: Integer);
var
  Filer   : TOSFFile;
  Ts      : Int64;
  I, N    : Integer;
begin
  Filer := TOSFFile.Create;
  try
    Filer.CreateForWrite(FullPath, Version);
    ConfigureMetadata(Filer);

    Filer.AddChannel(MakeBinaryChannel(0, 'Sensor/Image', PNG_MIME));
    Filer.WriteHeader;

    Ts := NowNs;
    N  := SampleCount;
    for I := 0 to N - 1 do
    begin
      Ts := Ts + RandomIntervalNs;
      Filer.WriteTimestampedSample(0, Ts, BinaryPayload(I));
    end;

    Filer.Close;
    LogFileResult(FullPath, 1, SampleCount);
  finally
    Filer.Free;
  end;
end;

procedure TOSFDemoGenerator.GenGpsLocation(const FullPath: string;
  Version: TOSFVersion; SampleCount: Integer);
var
  Filer   : TOSFFile;
  Ts      : Int64;
  I, N    : Integer;
begin
  Filer := TOSFFile.Create;
  try
    Filer.CreateForWrite(FullPath, Version);
    ConfigureMetadata(Filer);

    Filer.AddChannel(MakeTimestampedScalar(0, 'Sensor/Position', dtGpsLocation));
    Filer.WriteHeader;

    Ts := NowNs;
    N  := SampleCount;
    for I := 0 to N - 1 do
    begin
      Ts := Ts + RandomIntervalNs;
      Filer.WriteTimestampedSample(0, Ts, GpsBytes(I, N));
    end;

    Filer.Close;
    LogFileResult(FullPath, 1, SampleCount);
  finally
    Filer.Free;
  end;
end;

procedure TOSFDemoGenerator.GenEquidistant(const FullPath: string;
  Version: TOSFVersion; SampleCount: Integer);
const
  RATES: array[0..2] of Double = (100.0, 1000.0, 10000.0);
  NAMES: array[0..2] of string = ('Sensor/Vibration100Hz',
                                   'Sensor/Vibration1kHz',
                                   'Sensor/Vibration10kHz');
var
  Filer   : TOSFFile;
  StartNs : Int64;
  Samples : array of Double;
  I, K, N : Integer;
  Theta   : Double;
begin
  Filer := TOSFFile.Create;
  try
    Filer.CreateForWrite(FullPath, Version);
    ConfigureMetadata(Filer);

    for K := 0 to 2 do
      Filer.AddChannel(MakeEquidistantDouble(K, NAMES[K], RATES[K], 'm/s²'));

    Filer.WriteHeader;

    N := SampleCount;
    SetLength(Samples, N);
    for I := 0 to N - 1 do
    begin
      if N > 0 then
        Theta := 2.0 * Pi * I / N
      else
        Theta := 0;
      Samples[I] := Sin(Theta) * 100.0;
    end;

    StartNs := NowNs;
    for K := 0 to 2 do
      Filer.WriteEquidistantBlock(K, Samples, StartNs);

    Filer.Close;
    LogFileResult(FullPath, 3, SampleCount);
  finally
    Filer.Free;
  end;
end;

procedure TOSFDemoGenerator.GenMixed(const FullPath: string;
  Version: TOSFVersion; SampleCount: Integer);
var
  Filer       : TOSFFile;
  StartNs     : Int64;
  TsTd, TsTi  : Int64;     // timestamped double, timestamped int16
  TsTb        : Int64;     // timestamped bool
  Samples     : array of Double;
  I, N        : Integer;
  Theta       : Double;
begin
  Filer := TOSFFile.Create;
  try
    Filer.CreateForWrite(FullPath, Version);
    ConfigureMetadata(Filer);

    Filer.AddChannel(MakeTimestampedScalar(0, 'Sensor/TemperatureC',     dtDouble, '°C'));
    Filer.AddChannel(MakeTimestampedScalar(1, 'Sensor/PressureCounter',  dtInt16,  'counts'));
    Filer.AddChannel(MakeEquidistantDouble(2, 'Sensor/Vibration10Hz',    10.0, 'm/s²'));
    Filer.AddChannel(MakeTimestampedScalar(3, 'Sensor/AlarmFlag',        dtBool));

    Filer.WriteHeader;

    StartNs := NowNs;
    TsTd    := StartNs;
    TsTi    := StartNs;
    TsTb    := StartNs;

    N := SampleCount;
    SetLength(Samples, N);
    for I := 0 to N - 1 do
    begin
      if N > 0 then
        Theta := 2.0 * Pi * I / N
      else
        Theta := 0;
      Samples[I] := Sin(Theta) * 100.0;

      TsTd := TsTd + RandomIntervalNs;
      Filer.WriteTimestampedSample(0, TsTd, DoubleBytes(Sin(Theta) * 100.0));

      TsTi := TsTi + RandomIntervalNs;
      Filer.WriteTimestampedSample(1, TsTi, Int16Bytes(SmallInt(I mod 32767)));

      TsTb := TsTb + RandomIntervalNs;
      Filer.WriteTimestampedSample(3, TsTb, BoolBytes((I mod 10) < 5));
    end;

    // Equidistant block at 10 Hz, written as a single multi-value block.
    Filer.WriteEquidistantBlock(2, Samples, StartNs);

    Filer.Close;
    LogFileResult(FullPath, 4, SampleCount);
  finally
    Filer.Free;
  end;
end;

procedure TOSFDemoGenerator.GenMixedExtended(const FullPath: string;
  Version: TOSFVersion; SampleCount: Integer);
var
  Filer   : TOSFFile;
  StartNs : Int64;
  Ts0, Ts1, Ts2, Ts3, Ts4: Int64;
  I, N    : Integer;
  Theta   : Double;
  ChStr   : TOSFChannelDef;
begin
  Filer := TOSFFile.Create;
  try
    Filer.CreateForWrite(FullPath, Version);
    ConfigureMetadata(Filer);

    Filer.AddChannel(MakeTimestampedScalar(0, 'Sensor/Temperature',     dtDouble, '°C'));
    Filer.AddChannel(MakeTimestampedScalar(1, 'Sensor/Counter',         dtInt16,  'counts'));
    Filer.AddChannel(MakeTimestampedScalar(2, 'Sensor/AlarmFlag',       dtBool));

    ChStr := TOSFChannelDef.Create(3, 'Sensor/Message', ctScalar, dtString);
    ChStr.LengthFieldSize := lfs4;
    Filer.AddChannel(ChStr);

    Filer.AddChannel(MakeTimestampedScalar(4, 'Sensor/Position',        dtGpsLocation));

    Filer.WriteHeader;

    StartNs := NowNs;
    Ts0 := StartNs; Ts1 := StartNs; Ts2 := StartNs; Ts3 := StartNs; Ts4 := StartNs;

    N := SampleCount;
    for I := 0 to N - 1 do
    begin
      if N > 0 then
        Theta := 2.0 * Pi * I / N
      else
        Theta := 0;

      Ts0 := Ts0 + RandomIntervalNs; Filer.WriteTimestampedSample(0, Ts0, DoubleBytes(Sin(Theta) * 100.0));
      Ts1 := Ts1 + RandomIntervalNs; Filer.WriteTimestampedSample(1, Ts1, Int16Bytes(SmallInt(I mod 32767)));
      Ts2 := Ts2 + RandomIntervalNs; Filer.WriteTimestampedSample(2, Ts2, BoolBytes((I mod 10) < 5));
      Ts3 := Ts3 + RandomIntervalNs; Filer.WriteTimestampedSample(3, Ts3, StringBytes('Sample_' + IntToStr(I)));
      Ts4 := Ts4 + RandomIntervalNs; Filer.WriteTimestampedSample(4, Ts4, GpsBytes(I, N));
    end;

    Filer.Close;
    LogFileResult(FullPath, 5, SampleCount);
  finally
    Filer.Free;
  end;
end;

end.
