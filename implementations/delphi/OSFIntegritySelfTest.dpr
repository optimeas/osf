// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

// Self-test for the OSF5 integrity profile (level crc) Delphi implementation.
// Covers the CRC32C primitive, the magic-header tokenizer (positive + negative
// cases), a writer round-trip, and cross-validation against the Rust-generated
// reference files. Exits non-zero on any failure.
//
// The repository has no DUnitX scaffolding for the OSF units; per the task
// brief this self-test procedure plus the osftool-based negative checks stand
// in for a formal DUnitX suite. Build + run from implementations/delphi.
program OSFIntegritySelfTest;
{$APPTYPE CONSOLE}
uses
  System.SysUtils,
  System.Classes,
  OSF.Types in 'src\OSF.Types.pas',
  OSF.CRC32C in 'src\OSF.CRC32C.pas',
  OSF.Channel in 'src\OSF.Channel.pas',
  OSF.Log in 'src\OSF.Log.pas',
  OSF.Filer in 'src\OSF.Filer.pas';

var
  Failures: Integer = 0;

procedure Check(const What: string; Ok: Boolean);
begin
  if Ok then
    Writeln('  [ok]   ', What)
  else
  begin
    Writeln('  [FAIL] ', What);
    Inc(Failures);
  end;
end;

// Feed a crafted header line to the tokenizer; return the raised message,
// or '' if OpenForRead did not raise before the metablock read.
function TokenizeError(const HeaderLine: string): string;
var
  F: TOSFFile;
  MS: TMemoryStream;
  B: TBytes;
begin
  Result := '';
  // 16 junk metablock bytes; the tokenizer runs before they are read.
  B := TEncoding.ANSI.GetBytes(HeaderLine + #10 + '0123456789012345');
  MS := TMemoryStream.Create;
  F := TOSFFile.Create;
  try
    MS.WriteBuffer(B[0], Length(B));
    MS.Position := 0;
    try
      F.OpenForRead(MS, False);
    except
      on E: Exception do
        Result := E.Message;
    end;
  finally
    F.Free;
    MS.Free;
  end;
end;

procedure TestCRC32C;
begin
  Writeln('CRC32C primitive:');
  Check('self-test vectors', CRC32CSelfTest);
  Check('"123456789" = 0xE3069283',
    CRC32C(TEncoding.ANSI.GetBytes('123456789')) = Cardinal($E3069283));
end;

procedure TestTokenizer;
begin
  Writeln('Header tokenizer (negative cases):');
  Check('unknown token rejected',
    TokenizeError('OSF5 16 sha256:ABCD').Contains('unknown header token'));
  Check('token on OSF4 rejected',
    TokenizeError('OSF4 16 crc32c:9A3F01BC').Contains('not allowed'));
  Check('crc32c lowercase hex rejected',
    TokenizeError('OSF5 16 crc32c:9a3f01bc').Contains('crc32c'));
  Check('crc32c wrong length rejected',
    TokenizeError('OSF5 16 crc32c:9A3F01').Contains('crc32c'));
  Check('ed25519 without crc32c rejected',
    TokenizeError('OSF5 16 ed25519:0123456789abcdef').Contains('ed25519'));
end;

procedure TestWriteRoundtrip;
const
  TMP = 'selftest_crc.osf';
var
  F: TOSFFile;
  Ch: TOSFChannelDef;
  Samples: array of Double;
  Block: TOSFDataBlock;
  I, Total: Integer;
begin
  Writeln('Writer round-trip (level crc):');
  F := TOSFFile.Create;
  try
    F.CreateForWrite(TMP, osvOSF5);
    Ch := TOSFChannelDef.Create(0, 'Ch/Value', ctScalar, dtDouble);
    Ch.SampleRate := 100.0;
    Ch.TimeIncrement := Round(1.0e9 / 100.0);
    F.AddChannel(Ch);
    F.IntegrityProfile := ipCrc32c;
    F.WriteHeader;
    SetLength(Samples, 8);
    for I := 0 to 7 do
      Samples[I] := I * 1.25;
    F.WriteEquidistantBlock(0, Samples, 1000);
    F.Close;
  finally
    F.Free;
  end;

  Total := 0;
  F := TOSFFile.Create;
  try
    F.OpenForRead(TMP);
    while F.ReadNextBlock(Block) do
      if not Block.IsInfoBlock then
        Inc(Total, Block.SampleCount);
    Check('reads back as crc32c', F.IntegrityProfile = ipCrc32c);
    Check('no CRC failures', F.BlocksCRCFailed = 0);
    Check('8 samples preserved', Total = 8);
    Check('status crc_valid', F.VerificationStatus = 'crc_valid');
  finally
    F.Free;
  end;
  DeleteFile(TMP);
end;

function ReadRef(const Path: string; ExpChannels: Integer): Boolean;
var
  F: TOSFFile;
  Block: TOSFDataBlock;
  Blocks: Integer;
begin
  Blocks := 0;
  F := TOSFFile.Create;
  try
    F.OpenForRead(Path);
    while F.ReadNextBlock(Block) do
      if not Block.IsInfoBlock then Inc(Blocks);
    Result := (F.IntegrityProfile = ipCrc32c) and (F.BlocksCRCFailed = 0)
      and (not F.TruncationSeen) and (F.ChannelCount = ExpChannels) and (Blocks > 0);
  finally
    F.Free;
  end;
end;

procedure TestRustCrossValidation;
const
  Base = '..\..\examples\generated\integrity\';
begin
  Writeln('Cross-validation (reading Rust-generated files):');
  Check('osf5_crc_equidistant.osf', ReadRef(Base + 'osf5_crc_equidistant.osf', 3));
  Check('osf5_crc_variable.osf', ReadRef(Base + 'osf5_crc_variable.osf', 2));
end;

begin
  Writeln('OSF5 integrity profile (level crc) — Delphi self-test');
  Writeln('=====================================================');
  TestCRC32C;
  TestTokenizer;
  TestWriteRoundtrip;
  TestRustCrossValidation;
  Writeln('=====================================================');
  if Failures = 0 then
    Writeln('ALL PASSED')
  else
  begin
    Writeln(Format('%d FAILURE(S)', [Failures]));
    Halt(1);
  end;
end.
