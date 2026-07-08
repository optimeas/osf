// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

unit Test.OSF.Filer.Header;

interface

uses
  DUnitX.TestFramework;

type
  // Magic-header tokenizer matrix. Negative cases raise before the metablock
  // is read, so a crafted header line plus junk bytes is enough.
  [TestFixture]
  TFilerHeaderTests = class
  strict private
    // Opens a crafted header line on a memory stream and returns the raised
    // message, or '' if OpenForRead did not raise.
    function TokenizeError(const HeaderLine: string): string;
  public
    [Test] procedure UnknownTokenRejected;
    [Test] procedure TokenOnOsf4Rejected;
    [Test] procedure Crc32cLowercaseRejected;
    [Test] procedure Crc32cWrongLengthRejected;
    [Test] procedure Ed25519WithoutCrcRejected;
    [Test] procedure Ed25519UppercaseRejected;
    [Test] procedure Ed25519BeforeCrcRejected;
    // Fix 1 — strict single-space grammar (spec 1.2): no trailing space,
    // exactly one space between fields.
    [Test] procedure TrailingSpaceRejected;
    [Test] procedure TrailingSpaceAfterTokenRejected;
    [Test] procedure DoubleSpaceRejected;
  end;

implementation

uses
  System.SysUtils,
  System.Classes,
  OSF.Types,
  OSF.CRC32C,
  OSF.Channel,
  OSF.Log,
  OSF.Filer;

function TFilerHeaderTests.TokenizeError(const HeaderLine: string): string;
var
  F: TOSFFile;
  MS: TMemoryStream;
  B: TBytes;
begin
  Result := '';
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

procedure TFilerHeaderTests.UnknownTokenRejected;
begin
  Assert.Contains(TokenizeError('OSF5 16 sha256:ABCD'), 'unknown header token');
end;

procedure TFilerHeaderTests.TokenOnOsf4Rejected;
begin
  Assert.Contains(TokenizeError('OSF4 16 crc32c:9A3F01BC'), 'not allowed');
end;

procedure TFilerHeaderTests.Crc32cLowercaseRejected;
begin
  Assert.Contains(TokenizeError('OSF5 16 crc32c:9a3f01bc'), 'crc32c');
end;

procedure TFilerHeaderTests.Crc32cWrongLengthRejected;
begin
  Assert.Contains(TokenizeError('OSF5 16 crc32c:9A3F01'), 'crc32c');
end;

procedure TFilerHeaderTests.Ed25519WithoutCrcRejected;
begin
  Assert.Contains(TokenizeError('OSF5 16 ed25519:0123456789abcdef'), 'ed25519');
end;

procedure TFilerHeaderTests.Ed25519UppercaseRejected;
begin
  Assert.Contains(TokenizeError('OSF5 16 crc32c:9A3F01BC ed25519:0123456789ABCDEF'), 'ed25519');
end;

procedure TFilerHeaderTests.Ed25519BeforeCrcRejected;
begin
  Assert.Contains(TokenizeError('OSF5 16 ed25519:0123456789abcdef crc32c:9A3F01BC'), 'ed25519');
end;

procedure TFilerHeaderTests.TrailingSpaceRejected;
begin
  Assert.Contains(TokenizeError('OSF5 16 '), 'malformed');
end;

procedure TFilerHeaderTests.TrailingSpaceAfterTokenRejected;
begin
  Assert.Contains(TokenizeError('OSF5 16 crc32c:ABCD1234 '), 'malformed');
end;

procedure TFilerHeaderTests.DoubleSpaceRejected;
begin
  // A double space collapses to an empty field, which must be rejected.
  Assert.AreNotEqual('', TokenizeError('OSF5  16'));
end;

initialization
  TDUnitX.RegisterTestFixture(TFilerHeaderTests);

end.
