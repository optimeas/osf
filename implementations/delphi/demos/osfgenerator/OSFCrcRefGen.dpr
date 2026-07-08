// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

// Generates Delphi-written OSF5 integrity-profile (level crc) reference files
// into examples/generated/integrity/:
//   - osf5_equidistant_crc_delphi.osf : three equidistant double channels
//   - osf5_variable_crc_delphi.osf    : one string + one binary channel
//
// These carry a crc32c magic-header token and a per-block frame CRC32C. They
// live in the integrity/ subdir (not the shared reference_manifest.json) so
// integrity-unaware harnesses do not read them without honouring the CRC.
// Run from implementations/delphi: OSFCrcRefGen[.exe]
program OSFCrcRefGen;
{$APPTYPE CONSOLE}
uses
  System.SysUtils,
  System.Classes,
  System.IOUtils,
  OSF.Types in '..\..\src\OSF.Types.pas',
  OSF.CRC32C in '..\..\src\OSF.CRC32C.pas',
  OSF.Channel in '..\..\src\OSF.Channel.pas',
  OSF.Log in '..\..\src\OSF.Log.pas',
  OSF.Filer in '..\..\src\OSF.Filer.pas';

function OutDir: string;
begin
  // demos\osfgenerator -> repo examples\generated\integrity
  Result := TPath.GetFullPath('..\..\..\..\examples\generated\integrity');
  TDirectory.CreateDirectory(Result);
end;

procedure GenEquidistant(const Dir: string);
const
  NAMES: array[0..2] of string = ('Sensor/Vibration100Hz', 'Sensor/Vibration1kHz', 'Sensor/Vibration10kHz');
  RATES: array[0..2] of Double = (100.0, 1000.0, 10000.0);
var
  F: TOSFFile;
  Ch: TOSFChannelDef;
  K, I: Integer;
  Samples: array of Double;
  Path: string;
begin
  Path := TPath.Combine(Dir, 'osf5_equidistant_crc_delphi.osf');
  F := TOSFFile.Create;
  try
    F.CreateForWrite(Path, osvOSF5);
    for K := 0 to 2 do
    begin
      Ch := TOSFChannelDef.Create(K, NAMES[K], ctScalar, dtDouble);
      Ch.SampleRate := RATES[K];
      Ch.TimeIncrement := Round(1.0e9 / RATES[K]);
      F.AddChannel(Ch);
    end;
    F.IntegrityProfile := ipCrc32c;
    F.WriteHeader;
    SetLength(Samples, 100);
    for K := 0 to 2 do
    begin
      for I := 0 to 99 do
        Samples[I] := Sin(I * 0.1 * Ln(RATES[K]));
      F.WriteEquidistantBlock(K, Samples, 1000000);
    end;
    F.Close;
    Writeln('wrote ', Path);
  finally
    F.Free;
  end;
end;

procedure GenVariable(const Dir: string);
var
  F: TOSFFile;
  I: Integer;
  Ts: Int64;
  Blob: TBytes;
  Path: string;
begin
  Path := TPath.Combine(Dir, 'osf5_variable_crc_delphi.osf');
  F := TOSFFile.Create;
  try
    F.CreateForWrite(Path, osvOSF5);
    F.AddChannel(TOSFChannelDef.Create(0, 'Sensor/Log', ctScalar, dtString));
    F.AddChannel(TOSFChannelDef.Create(1, 'Sensor/Blob', ctBinary, dtBinary));
    F.IntegrityProfile := ipCrc32c;
    F.WriteHeader;
    for I := 0 to 9 do
    begin
      Ts := 1000000 + Int64(I) * 1000000;
      F.WriteTimestampedSample(0, Ts, TEncoding.UTF8.GetBytes(Format('event-%.3d', [I])));
      SetLength(Blob, 3);
      Blob[0] := Byte(I); Blob[1] := Byte(I + 1); Blob[2] := Byte(I + 2);
      F.WriteTimestampedSample(1, Ts, Blob);
    end;
    F.Close;
    Writeln('wrote ', Path);
  finally
    F.Free;
  end;
end;

var
  Dir: string;
begin
  Dir := OutDir;
  GenEquidistant(Dir);
  GenVariable(Dir);
end.
