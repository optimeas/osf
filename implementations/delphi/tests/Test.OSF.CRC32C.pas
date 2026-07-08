// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

unit Test.OSF.CRC32C;

interface

uses
  DUnitX.TestFramework;

type
  [TestFixture]
  TCRC32CTests = class
  public
    [Test]
    procedure CheckValue123456789;
    [Test]
    procedure EmptyInputIsZero;
    [Test]
    procedure RFC3720ThirtyTwoZeroBytes;
    [Test]
    procedure RFC3720ThirtyTwoOneBytes;
    [Test]
    procedure SelfTestPasses;
    [Test]
    procedure IncrementalMatchesOneShot;
  end;

implementation

uses
  System.SysUtils,
  OSF.CRC32C;

procedure TCRC32CTests.CheckValue123456789;
begin
  // Canonical CRC-32/ISCSI (Castagnoli) check value.
  Assert.AreEqual(Cardinal($E3069283),
    CRC32C(TEncoding.ANSI.GetBytes('123456789')));
end;

procedure TCRC32CTests.EmptyInputIsZero;
begin
  Assert.AreEqual(Cardinal(0), CRC32C(Pointer(nil)^, 0));
end;

procedure TCRC32CTests.RFC3720ThirtyTwoZeroBytes;
var
  B: TBytes;
begin
  SetLength(B, 32);
  FillChar(B[0], 32, 0);
  Assert.AreEqual(Cardinal($8A9136AA), CRC32C(B));
end;

procedure TCRC32CTests.RFC3720ThirtyTwoOneBytes;
var
  B: TBytes;
begin
  SetLength(B, 32);
  FillChar(B[0], 32, $FF);
  Assert.AreEqual(Cardinal($62A8AB43), CRC32C(B));
end;

procedure TCRC32CTests.SelfTestPasses;
begin
  Assert.IsTrue(CRC32CSelfTest);
end;

procedure TCRC32CTests.IncrementalMatchesOneShot;
var
  C: TCRC32C;
  B: TBytes;
begin
  // Feeding the input in two chunks must equal the one-shot value.
  B := TEncoding.ANSI.GetBytes('123456789');
  C.Init;
  C.Update(B[0], 4);
  C.Update(B[4], Length(B) - 4);
  Assert.AreEqual(CRC32C(B), C.Final);
end;

initialization
  TDUnitX.RegisterTestFixture(TCRC32CTests);

end.
