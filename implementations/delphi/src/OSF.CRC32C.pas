// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

/// <summary>
///   CRC32C (Castagnoli, polynomial 0x1EDC6F41 — the CRC-32/ISCSI algorithm)
///   used by the OSF5 integrity profile for the metablock checksum and the
///   per-block frame CRC.
/// </summary>
/// <remarks>
///   Pure Pascal, table-based. <c>System.ZLib</c> is unsuitable: it only
///   offers the Ethernet CRC-32 (polynomial 0x04C11DB7). The check value for
///   the ASCII string "123456789" is 0xE3069283, matching the other OSF
///   implementations byte for byte.
/// </remarks>
unit OSF.CRC32C;

interface

uses
  System.SysUtils;

type
  /// <summary>Incremental CRC32C accumulator.</summary>
  TCRC32C = record
  private
    FCrc: Cardinal;
  public
    /// <summary>Reset the accumulator to the initial state.</summary>
    procedure Init;
    /// <summary>Fold <paramref name="Count"/> bytes at <paramref name="Buffer"/> into the running CRC.</summary>
    procedure Update(const Buffer; Count: NativeInt); overload;
    /// <summary>Fold a byte array into the running CRC.</summary>
    procedure Update(const Bytes: TBytes); overload;
    /// <summary>Finalise and return the CRC32C value.</summary>
    function Final: Cardinal;
  end;

/// <summary>One-shot CRC32C over a raw buffer.</summary>
function CRC32C(const Buffer; Count: NativeInt): Cardinal; overload;
/// <summary>One-shot CRC32C over a byte array.</summary>
function CRC32C(const Bytes: TBytes): Cardinal; overload;

/// <summary>
///   Self-test against known CRC32C check vectors. Returns True on success.
/// </summary>
function CRC32CSelfTest: Boolean;

implementation

const
  // Reversed CRC-32/ISCSI (Castagnoli) polynomial.
  CRC32C_POLY_REVERSED = Cardinal($82F63B78);
  CRC32C_INIT          = Cardinal($FFFFFFFF);

var
  CRC32CTable: array[0..255] of Cardinal;

procedure BuildTable;
var
  N, K: Integer;
  C: Cardinal;
begin
  for N := 0 to 255 do
  begin
    C := Cardinal(N);
    for K := 0 to 7 do
      if (C and 1) <> 0 then
        C := CRC32C_POLY_REVERSED xor (C shr 1)
      else
        C := C shr 1;
    CRC32CTable[N] := C;
  end;
end;

{ TCRC32C }

procedure TCRC32C.Init;
begin
  FCrc := CRC32C_INIT;
end;

procedure TCRC32C.Update(const Buffer; Count: NativeInt);
var
  P: PByte;
  I: NativeInt;
begin
  P := @Buffer;
  for I := 0 to Count - 1 do
  begin
    FCrc := CRC32CTable[(FCrc xor P^) and $FF] xor (FCrc shr 8);
    Inc(P);
  end;
end;

procedure TCRC32C.Update(const Bytes: TBytes);
begin
  if Length(Bytes) > 0 then
    Update(Bytes[0], Length(Bytes));
end;

function TCRC32C.Final: Cardinal;
begin
  Result := FCrc xor CRC32C_INIT;
end;

function CRC32C(const Buffer; Count: NativeInt): Cardinal;
var
  C: TCRC32C;
begin
  C.Init;
  C.Update(Buffer, Count);
  Result := C.Final;
end;

function CRC32C(const Bytes: TBytes): Cardinal;
begin
  if Length(Bytes) = 0 then
    Result := CRC32C(Pointer(nil)^, 0)
  else
    Result := CRC32C(Bytes[0], Length(Bytes));
end;

function CRC32CSelfTest: Boolean;
var
  S: TBytes;
begin
  Result := True;
  // Canonical CRC-32/ISCSI check value for "123456789".
  S := TEncoding.ANSI.GetBytes('123456789');
  Result := Result and (CRC32C(S) = Cardinal($E3069283));
  // Empty input.
  Result := Result and (CRC32C(Pointer(nil)^, 0) = 0);
  // 32 bytes of 0x00 → 0x8A9136AA (RFC 3720 iSCSI CRC test vector).
  SetLength(S, 32);
  FillChar(S[0], 32, 0);
  Result := Result and (CRC32C(S) = Cardinal($8A9136AA));
  // 32 bytes of 0xFF → 0x62A8AB43 (RFC 3720 iSCSI CRC test vector).
  FillChar(S[0], 32, $FF);
  Result := Result and (CRC32C(S) = Cardinal($62A8AB43));
end;

initialization
  BuildTable;

end.
