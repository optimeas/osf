// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

unit Test.OSF.Filer.Integrity;

interface

uses
  System.SysUtils,
  OSF.Types,
  DUnitX.TestFramework;

type
  [TestFixture]
  TFilerIntegrityTests = class
  strict private
    function WriteEquidistantCrc(WithIntegrity: Boolean): TBytes;
    function WriteStringCrc: TBytes;
    function WriteMixedCrc: TBytes;
    function DataStart(const B: TBytes): Integer;
    procedure ReadCounters(const B: TBytes; out Integrity: TOSFIntegrityProfile;
      out CrcFailed, UnknownSkipped: UInt32; out Truncated: Boolean;
      out Blocks, Samples: Integer; out Status: string);
    function IntegrityDir: string;
  public
    [Test] procedure WriteReadRoundtripPreservesData;
    [Test] procedure HeaderCarriesCrc32cToken;
    [Test] procedure MetablockByteFlipRejected;
    [Test] procedure NumericFrameCrcMismatchSkipped;
    [Test] procedure StringFrameCrcMismatchSkipped;
    [Test] procedure ControlByte9SkippedInProfilelessFile;
    [Test] procedure VerificationStatusValues;
    [Test] procedure CrossValidationReferenceFiles;
  end;

implementation

uses
  System.Classes,
  System.IOUtils,
  OSF.CRC32C,
  OSF.Channel,
  OSF.Log,
  OSF.Filer;

function StreamBytes(MS: TMemoryStream): TBytes;
begin
  SetLength(Result, MS.Size);
  if MS.Size > 0 then
    Move(MS.Memory^, Result[0], MS.Size);
end;

function TFilerIntegrityTests.WriteEquidistantCrc(WithIntegrity: Boolean): TBytes;
var
  MS: TMemoryStream;
  F: TOSFFile;
  Ch: TOSFChannelDef;
  S: array of Double;
  I: Integer;
begin
  MS := TMemoryStream.Create;
  F := TOSFFile.Create;
  try
    F.CreateForWrite(MS, False, osvOSF5);
    Ch := TOSFChannelDef.Create(0, 'Ch/Value', ctScalar, dtDouble);
    Ch.SampleRate := 100.0;
    Ch.TimeIncrement := Round(1.0e9 / 100.0);
    F.AddChannel(Ch);
    if WithIntegrity then
      F.IntegrityProfile := ipCrc32c;
    F.WriteHeader;
    SetLength(S, 5);
    for I := 0 to 4 do
      S[I] := 1.5 + I;
    F.WriteEquidistantBlock(0, S, 1000);
    F.Close;
    Result := StreamBytes(MS);
  finally
    F.Free;
    MS.Free;
  end;
end;

function TFilerIntegrityTests.WriteStringCrc: TBytes;
var
  MS: TMemoryStream;
  F: TOSFFile;
  I: Integer;
begin
  MS := TMemoryStream.Create;
  F := TOSFFile.Create;
  try
    F.CreateForWrite(MS, False, osvOSF5);
    F.AddChannel(TOSFChannelDef.Create(0, 'Ch/Log', ctScalar, dtString));
    F.IntegrityProfile := ipCrc32c;
    F.WriteHeader;
    for I := 0 to 2 do
      F.WriteTimestampedSample(0, 10 + I * 10, TEncoding.UTF8.GetBytes('event-' + IntToStr(I)));
    F.Close;
    Result := StreamBytes(MS);
  finally
    F.Free;
    MS.Free;
  end;
end;

function TFilerIntegrityTests.WriteMixedCrc: TBytes;
var
  MS: TMemoryStream;
  F: TOSFFile;
  Ch: TOSFChannelDef;
  S: array of Double;
  Blob: TBytes;
  I: Integer;
begin
  MS := TMemoryStream.Create;
  F := TOSFFile.Create;
  try
    F.CreateForWrite(MS, False, osvOSF5);
    Ch := TOSFChannelDef.Create(0, 'Ch/Value', ctScalar, dtDouble);
    Ch.SampleRate := 100.0;
    Ch.TimeIncrement := Round(1.0e9 / 100.0);
    F.AddChannel(Ch);
    F.AddChannel(TOSFChannelDef.Create(1, 'Ch/Log', ctScalar, dtString));
    F.AddChannel(TOSFChannelDef.Create(2, 'Ch/Blob', ctBinary, dtBinary));
    F.IntegrityProfile := ipCrc32c;
    F.WriteHeader;
    SetLength(S, 8);
    for I := 0 to 7 do
      S[I] := I * 1.25;
    F.WriteEquidistantBlock(0, S, 1000);
    for I := 0 to 2 do
      F.WriteTimestampedSample(1, 10 + I * 10, TEncoding.UTF8.GetBytes('e' + IntToStr(I)));
    for I := 0 to 1 do
    begin
      SetLength(Blob, 3);
      Blob[0] := I; Blob[1] := I + 1; Blob[2] := I + 2;
      F.WriteTimestampedSample(2, 100 + I, Blob);
    end;
    F.Close;
    Result := StreamBytes(MS);
  finally
    F.Free;
    MS.Free;
  end;
end;

function TFilerIntegrityTests.DataStart(const B: TBytes): Integer;
var
  I, NL: Integer;
  Line: string;
  Parts: TArray<string>;
begin
  NL := 0;
  for I := 0 to High(B) do
    if B[I] = 10 then begin NL := I; Break; end;
  SetString(Line, PAnsiChar(@B[0]), NL);
  Parts := Line.Split([' ']);
  Result := (NL + 1) + StrToInt(Parts[1]);
end;

procedure TFilerIntegrityTests.ReadCounters(const B: TBytes;
  out Integrity: TOSFIntegrityProfile; out CrcFailed, UnknownSkipped: UInt32;
  out Truncated: Boolean; out Blocks, Samples: Integer; out Status: string);
var
  MS: TBytesStream;
  F: TOSFFile;
  Block: TOSFDataBlock;
begin
  Blocks := 0; Samples := 0;
  MS := TBytesStream.Create(B);
  F := TOSFFile.Create;
  try
    F.OpenForRead(MS, False);
    while F.ReadNextBlock(Block) do
      if not Block.IsInfoBlock then
      begin
        Inc(Blocks);
        Inc(Samples, Block.SampleCount);
      end;
    Integrity := F.IntegrityProfile;
    CrcFailed := F.BlocksCRCFailed;
    UnknownSkipped := F.BlocksUnknownTypeSkipped;
    Truncated := F.TruncationSeen;
    Status := F.VerificationStatus;
  finally
    F.Free;
    MS.Free;
  end;
end;

function TFilerIntegrityTests.IntegrityDir: string;
begin
  Result := TPath.GetFullPath(TPath.Combine(ExtractFilePath(ParamStr(0)),
    '..\..\..\examples\generated\integrity'));
end;

procedure TFilerIntegrityTests.WriteReadRoundtripPreservesData;
var
  Integrity: TOSFIntegrityProfile;
  CrcFailed, Unknown: UInt32;
  Truncated: Boolean;
  Blocks, Samples: Integer;
  Status: string;
begin
  ReadCounters(WriteMixedCrc, Integrity, CrcFailed, Unknown, Truncated, Blocks, Samples, Status);
  Assert.AreEqual(Ord(ipCrc32c), Ord(Integrity), 'integrity');
  Assert.AreEqual(UInt32(0), CrcFailed, 'crc failures');
  Assert.AreEqual(13, Samples, 'samples (8 eq + 3 str + 2 bin)');
  Assert.AreEqual('crc_valid', Status);
end;

procedure TFilerIntegrityTests.HeaderCarriesCrc32cToken;
var
  B: TBytes;
  Line: string;
  I, NL: Integer;
begin
  B := WriteEquidistantCrc(True);
  NL := 0;
  for I := 0 to High(B) do
    if B[I] = 10 then begin NL := I; Break; end;
  SetString(Line, PAnsiChar(@B[0]), NL);
  Assert.Contains(Line, ' crc32c:');
end;

procedure TFilerIntegrityTests.MetablockByteFlipRejected;
var
  B: TBytes;
  MS: TBytesStream;
  F: TOSFFile;
  Raised: Boolean;
  NL, I: Integer;
begin
  B := WriteEquidistantCrc(True);
  NL := 0;
  for I := 0 to High(B) do
    if B[I] = 10 then begin NL := I; Break; end;
  B[NL + 3] := B[NL + 3] xor $FF; // flip a metablock byte
  Raised := False;
  MS := TBytesStream.Create(B);
  F := TOSFFile.Create;
  try
    try
      F.OpenForRead(MS, False);
    except
      on E: Exception do
        Raised := E.Message.Contains('metablock CRC');
    end;
  finally
    F.Free;
    MS.Free;
  end;
  Assert.IsTrue(Raised, 'metablock CRC mismatch must reject the file');
end;

procedure TFilerIntegrityTests.NumericFrameCrcMismatchSkipped;
var
  B: TBytes;
  Integrity: TOSFIntegrityProfile;
  CrcFailed, Unknown: UInt32;
  Truncated: Boolean;
  Blocks, Samples: Integer;
  Status: string;
begin
  B := WriteEquidistantCrc(True);
  B[DataStart(B) + 6] := B[DataStart(B) + 6] xor $FF; // corrupt block payload
  ReadCounters(B, Integrity, CrcFailed, Unknown, Truncated, Blocks, Samples, Status);
  Assert.IsTrue(CrcFailed >= 1, 'expected a frame CRC failure');
  Assert.AreEqual('invalid', Status);
end;

procedure TFilerIntegrityTests.StringFrameCrcMismatchSkipped;
var
  B: TBytes;
  Integrity: TOSFIntegrityProfile;
  CrcFailed, Unknown: UInt32;
  Truncated: Boolean;
  Blocks, Samples: Integer;
  Status: string;
begin
  B := WriteStringCrc;
  B[DataStart(B) + 6] := B[DataStart(B) + 6] xor $FF; // corrupt string block payload
  ReadCounters(B, Integrity, CrcFailed, Unknown, Truncated, Blocks, Samples, Status);
  Assert.IsTrue(CrcFailed >= 1, 'expected a frame CRC failure on the string block');
end;

procedure TFilerIntegrityTests.ControlByte9SkippedInProfilelessFile;
var
  B: TBytes;
  Integrity: TOSFIntegrityProfile;
  CrcFailed, Unknown: UInt32;
  Truncated: Boolean;
  Blocks, Samples: Integer;
  Status: string;
begin
  // A profile-less file: flip the first block's control byte to 9.
  B := WriteEquidistantCrc(False);
  B[DataStart(B) + 4] := $09; // [ci(2)][len(2)][control] -> offset +4
  ReadCounters(B, Integrity, CrcFailed, Unknown, Truncated, Blocks, Samples, Status);
  Assert.AreEqual(UInt32(1), Unknown, 'unknown control byte must be skipped (Fix C)');
  Assert.IsFalse(Truncated, 'a skip must not be reported as truncation');
end;

procedure TFilerIntegrityTests.VerificationStatusValues;
var
  Integrity: TOSFIntegrityProfile;
  CrcFailed, Unknown: UInt32;
  Truncated: Boolean;
  Blocks, Samples: Integer;
  Status: string;
  B: TBytes;
begin
  // none: profile-less file.
  ReadCounters(WriteEquidistantCrc(False), Integrity, CrcFailed, Unknown, Truncated, Blocks, Samples, Status);
  Assert.AreEqual('none', Status);
  // crc_valid: intact crc file.
  ReadCounters(WriteEquidistantCrc(True), Integrity, CrcFailed, Unknown, Truncated, Blocks, Samples, Status);
  Assert.AreEqual('crc_valid', Status);
  // invalid: corrupted crc file.
  B := WriteEquidistantCrc(True);
  B[DataStart(B) + 6] := B[DataStart(B) + 6] xor $FF;
  ReadCounters(B, Integrity, CrcFailed, Unknown, Truncated, Blocks, Samples, Status);
  Assert.AreEqual('invalid', Status);
end;

procedure TFilerIntegrityTests.CrossValidationReferenceFiles;
const
  FILES: array[0..3] of string = (
    'osf5_crc_equidistant.osf', 'osf5_crc_variable.osf',
    'osf5_equidistant_crc_delphi.osf', 'osf5_variable_crc_delphi.osf');
var
  Dir, Name: string;
  F: TOSFFile;
  Block: TOSFDataBlock;
  Blocks: Integer;
begin
  Dir := IntegrityDir;
  for Name in FILES do
  begin
    Blocks := 0;
    F := TOSFFile.Create;
    try
      F.OpenForRead(TPath.Combine(Dir, Name));
      while F.ReadNextBlock(Block) do
        if not Block.IsInfoBlock then Inc(Blocks);
      Assert.AreEqual(Ord(ipCrc32c), Ord(F.IntegrityProfile), Name + ': integrity');
      Assert.AreEqual(UInt32(0), F.BlocksCRCFailed, Name + ': crc failures');
      Assert.IsTrue(Blocks > 0, Name + ': has blocks');
    finally
      F.Free;
    end;
  end;
end;

initialization
  TDUnitX.RegisterTestFixture(TFilerIntegrityTests);

end.
