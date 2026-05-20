// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

// Idiomatic Delphi wrappers around the raw HDF5 handles. Each class owns one
// hid_t and releases it in the destructor, so callers manage HDF5 objects
// with ordinary try..finally instead of bare identifiers.
//
// This unit knows nothing about OSF — it is a reusable HDF5 abstraction.
// Windows-only; compiles empty on other platforms (HDF5 export targets Win64).
unit Hdf5.Wrapper;

interface

{$IFDEF MSWINDOWS}
uses
  System.SysUtils,
  Hdf5.Types,
  Hdf5.Api;

type
  // ── Common RAII base ────────────────────────────────────────────────────────
  // Holds one HDF5 identifier and closes it on destruction. Concrete classes
  // implement CloseHandle with the matching H5*close call.
  THdf5Handle = class
  strict private
    FHandle: hid_t;
  strict protected
    procedure CloseHandle; virtual; abstract;
    procedure SetHandle(Value: hid_t);
  public
    destructor Destroy; override;
    property Handle: hid_t read FHandle;
  end;

  // ── Property list (FAPL / DCPL / LCPL / ...) ────────────────────────────────
  THdf5PropertyList = class(THdf5Handle)
  strict protected
    procedure CloseHandle; override;
  public
    constructor Create(ClassId: hid_t);
    procedure SetChunk(ChunkSize: hsize_t);
    procedure SetDeflate(Level: Cardinal);
    procedure SetShuffle;
    procedure SetLibverBounds(Low, High: Integer);
    procedure SetCharEncoding(Encoding: Integer);
    procedure SetCreateIntermediateGroup(Enable: Boolean);
  end;

  // ── Dataspace ────────────────────────────────────────────────────────────────
  THdf5Dataspace = class(THdf5Handle)
  strict protected
    procedure CloseHandle; override;
  public
    // Scalar (rank 0) dataspace — used for single-valued attributes.
    constructor CreateScalar;
    // 1-D extensible dataspace: current size InitialSize, max H5S_UNLIMITED.
    constructor CreateUnlimited1D(InitialSize: hsize_t);
  end;

  // ── Datatype ─────────────────────────────────────────────────────────────────
  // Always owns the wrapped type — only created or copied types are wrapped.
  // Predefined _g types are passed around as raw hid_t and never wrapped.
  THdf5Datatype = class(THdf5Handle)
  strict protected
    procedure CloseHandle; override;
  public
    constructor CreateCompound(Size: NativeUInt);
    constructor CreateCopy(SourceType: hid_t);
    procedure Insert(const MemberName: UTF8String; Offset: NativeUInt; MemberType: hid_t);
    procedure SetVariableLength;
    procedure SetCharSet(CharSet: Integer);
  end;

  // ── File ─────────────────────────────────────────────────────────────────────
  // The file identifier doubles as the root location for groups, datasets and
  // root attributes.
  THdf5File = class(THdf5Handle)
  strict protected
    procedure CloseHandle; override;
  public
    constructor Create(const Path: UTF8String; Flags: Cardinal = H5F_ACC_TRUNC);
  end;

  // ── Group ────────────────────────────────────────────────────────────────────
  THdf5Group = class(THdf5Handle)
  strict protected
    procedure CloseHandle; override;
  public
    constructor Create(LocId: hid_t; const Name: UTF8String; Lcpl: hid_t);
  end;

  // ── Dataset ──────────────────────────────────────────────────────────────────
  THdf5Dataset = class(THdf5Handle)
  strict private
    FRowCount: hsize_t;
  strict protected
    procedure CloseHandle; override;
  public
    constructor Create(LocId: hid_t; const Name: UTF8String;
      TypeId, SpaceId, Dcpl, Lcpl: hid_t);
    // Extends the dataset by Count rows and writes Buf into them. Buf is laid
    // out according to MemType. Wraps the extend / hyperslab / write sequence.
    procedure AppendChunk(Buf: Pointer; Count: NativeInt; MemType: hid_t);
    property RowCount: hsize_t read FRowCount;
  end;

  // ── Attribute helpers ────────────────────────────────────────────────────────
  // Attributes are written once and never reopened, so these are stateless
  // class procedures rather than handle-owning instances. LocId is a file,
  // group or dataset identifier.
  THdf5Attribute = class
  public
    class procedure WriteUtf8String(LocId: hid_t; const Name, Value: UTF8String);
    class procedure WriteInt64(LocId: hid_t; const Name: UTF8String; Value: Int64);
    class procedure WriteUInt16(LocId: hid_t; const Name: UTF8String; Value: Word);
    class procedure WriteDouble(LocId: hid_t; const Name: UTF8String; Value: Double);
    class procedure WriteBoolean(LocId: hid_t; const Name: UTF8String; Value: Boolean);
  end;
{$ENDIF}

implementation

{$IFDEF MSWINDOWS}

// ── THdf5Handle ───────────────────────────────────────────────────────────────

procedure THdf5Handle.SetHandle(Value: hid_t);
begin
  FHandle := Value;
end;

destructor THdf5Handle.Destroy;
begin
  if FHandle > 0 then
  begin
    try
      CloseHandle;
    except
      // Destructors must not raise; a failed close is unrecoverable anyway.
    end;
    FHandle := 0;
  end;
  inherited;
end;

// ── THdf5PropertyList ─────────────────────────────────────────────────────────

constructor THdf5PropertyList.Create(ClassId: hid_t);
begin
  inherited Create;
  SetHandle(CheckH5Id(H5Pcreate(ClassId), 'H5Pcreate'));
end;

procedure THdf5PropertyList.CloseHandle;
begin
  H5Pclose(Handle);
end;

procedure THdf5PropertyList.SetChunk(ChunkSize: hsize_t);
var
  Dims: hsize_t;
begin
  Dims := ChunkSize;
  CheckH5(H5Pset_chunk(Handle, 1, @Dims), 'H5Pset_chunk');
end;

procedure THdf5PropertyList.SetDeflate(Level: Cardinal);
begin
  CheckH5(H5Pset_deflate(Handle, Level), 'H5Pset_deflate');
end;

procedure THdf5PropertyList.SetShuffle;
begin
  CheckH5(H5Pset_shuffle(Handle), 'H5Pset_shuffle');
end;

procedure THdf5PropertyList.SetLibverBounds(Low, High: Integer);
begin
  CheckH5(H5Pset_libver_bounds(Handle, Low, High), 'H5Pset_libver_bounds');
end;

procedure THdf5PropertyList.SetCharEncoding(Encoding: Integer);
begin
  CheckH5(H5Pset_char_encoding(Handle, Encoding), 'H5Pset_char_encoding');
end;

procedure THdf5PropertyList.SetCreateIntermediateGroup(Enable: Boolean);
begin
  CheckH5(H5Pset_create_intermediate_group(Handle, Ord(Enable)), 'H5Pset_create_intermediate_group');
end;

// ── THdf5Dataspace ────────────────────────────────────────────────────────────

constructor THdf5Dataspace.CreateScalar;
begin
  inherited Create;
  SetHandle(CheckH5Id(H5Screate(H5S_SCALAR), 'H5Screate'));
end;

constructor THdf5Dataspace.CreateUnlimited1D(InitialSize: hsize_t);
var
  Dims, MaxDims: hsize_t;
begin
  inherited Create;
  Dims := InitialSize;
  MaxDims := H5S_UNLIMITED;
  SetHandle(CheckH5Id(H5Screate_simple(1, @Dims, @MaxDims), 'H5Screate_simple'));
end;

procedure THdf5Dataspace.CloseHandle;
begin
  H5Sclose(Handle);
end;

// ── THdf5Datatype ─────────────────────────────────────────────────────────────

constructor THdf5Datatype.CreateCompound(Size: NativeUInt);
begin
  inherited Create;
  SetHandle(CheckH5Id(H5Tcreate(H5T_COMPOUND, Size), 'H5Tcreate'));
end;

constructor THdf5Datatype.CreateCopy(SourceType: hid_t);
begin
  inherited Create;
  SetHandle(CheckH5Id(H5Tcopy(SourceType), 'H5Tcopy'));
end;

procedure THdf5Datatype.CloseHandle;
begin
  H5Tclose(Handle);
end;

procedure THdf5Datatype.Insert(const MemberName: UTF8String; Offset: NativeUInt;
  MemberType: hid_t);
begin
  CheckH5(H5Tinsert(Handle, PAnsiChar(MemberName), Offset, MemberType), 'H5Tinsert');
end;

procedure THdf5Datatype.SetVariableLength;
begin
  CheckH5(H5Tset_size(Handle, H5T_VARIABLE), 'H5Tset_size');
end;

procedure THdf5Datatype.SetCharSet(CharSet: Integer);
begin
  CheckH5(H5Tset_cset(Handle, CharSet), 'H5Tset_cset');
end;

// ── THdf5File ─────────────────────────────────────────────────────────────────

constructor THdf5File.Create(const Path: UTF8String; Flags: Cardinal = H5F_ACC_TRUNC);
var
  Fapl, Fid: hid_t;
begin
  inherited Create;
  // Modern format: the file may use features up to the latest 1.14 version.
  Fapl := CheckH5Id(H5Pcreate(H5P_CLS_FILE_ACCESS_ID), 'H5Pcreate(FAPL)');
  try
    CheckH5(H5Pset_libver_bounds(Fapl, H5F_LIBVER_V110, H5F_LIBVER_LATEST),
      'H5Pset_libver_bounds');
    Fid := H5Fcreate(PAnsiChar(Path), Flags, H5P_DEFAULT, Fapl);
  finally
    H5Pclose(Fapl);
  end;
  SetHandle(CheckH5Id(Fid, 'H5Fcreate'));
end;

procedure THdf5File.CloseHandle;
begin
  H5Fclose(Handle);
end;

// ── THdf5Group ────────────────────────────────────────────────────────────────

constructor THdf5Group.Create(LocId: hid_t; const Name: UTF8String; Lcpl: hid_t);
begin
  inherited Create;
  SetHandle(CheckH5Id(
    H5Gcreate2(LocId, PAnsiChar(Name), Lcpl, H5P_DEFAULT, H5P_DEFAULT), 'H5Gcreate2'));
end;

procedure THdf5Group.CloseHandle;
begin
  H5Gclose(Handle);
end;

// ── THdf5Dataset ──────────────────────────────────────────────────────────────

constructor THdf5Dataset.Create(LocId: hid_t; const Name: UTF8String;
  TypeId, SpaceId, Dcpl, Lcpl: hid_t);
begin
  inherited Create;
  FRowCount := 0;
  SetHandle(CheckH5Id(
    H5Dcreate2(LocId, PAnsiChar(Name), TypeId, SpaceId, Lcpl, Dcpl, H5P_DEFAULT),
    'H5Dcreate2'));
end;

procedure THdf5Dataset.CloseHandle;
begin
  H5Dclose(Handle);
end;

procedure THdf5Dataset.AppendChunk(Buf: Pointer; Count: NativeInt; MemType: hid_t);
var
  NewSize, StartOffset, ChunkCount: hsize_t;
  FileSpace, MemSpace: hid_t;
begin
  if Count <= 0 then
    Exit;
  StartOffset := FRowCount;
  ChunkCount := hsize_t(Count);
  NewSize := FRowCount + ChunkCount;
  CheckH5(H5Dset_extent(Handle, @NewSize), 'H5Dset_extent');
  FileSpace := CheckH5Id(H5Dget_space(Handle), 'H5Dget_space');
  try
    CheckH5(H5Sselect_hyperslab(FileSpace, H5S_SELECT_SET, @StartOffset, nil,
      @ChunkCount, nil), 'H5Sselect_hyperslab');
    MemSpace := CheckH5Id(H5Screate_simple(1, @ChunkCount, nil), 'H5Screate_simple');
    try
      CheckH5(H5Dwrite(Handle, MemType, MemSpace, FileSpace, H5P_DEFAULT, Buf),
        'H5Dwrite');
    finally
      H5Sclose(MemSpace);
    end;
  finally
    H5Sclose(FileSpace);
  end;
  FRowCount := NewSize;
end;

// ── THdf5Attribute ────────────────────────────────────────────────────────────

class procedure THdf5Attribute.WriteUtf8String(LocId: hid_t; const Name, Value: UTF8String);
var
  StrType: THdf5Datatype;
  Space: THdf5Dataspace;
  AttrId: hid_t;
  ValuePtr: PAnsiChar;
begin
  StrType := THdf5Datatype.CreateCopy(H5T_C_S1);
  try
    StrType.SetVariableLength;
    StrType.SetCharSet(H5T_CSET_UTF8);
    Space := THdf5Dataspace.CreateScalar;
    try
      AttrId := CheckH5Id(H5Acreate2(LocId, PAnsiChar(Name), StrType.Handle,
        Space.Handle, H5P_DEFAULT, H5P_DEFAULT), 'H5Acreate2');
      try
        // VLEN string write expects a pointer TO the char pointer (char**).
        ValuePtr := PAnsiChar(Value);
        CheckH5(H5Awrite(AttrId, StrType.Handle, @ValuePtr), 'H5Awrite');
      finally
        H5Aclose(AttrId);
      end;
    finally
      Space.Free;
    end;
  finally
    StrType.Free;
  end;
end;

class procedure THdf5Attribute.WriteInt64(LocId: hid_t; const Name: UTF8String;
  Value: Int64);
var
  Space: THdf5Dataspace;
  AttrId: hid_t;
begin
  Space := THdf5Dataspace.CreateScalar;
  try
    AttrId := CheckH5Id(H5Acreate2(LocId, PAnsiChar(Name), H5T_STD_I64LE,
      Space.Handle, H5P_DEFAULT, H5P_DEFAULT), 'H5Acreate2');
    try
      CheckH5(H5Awrite(AttrId, H5T_NATIVE_INT64, @Value), 'H5Awrite');
    finally
      H5Aclose(AttrId);
    end;
  finally
    Space.Free;
  end;
end;

class procedure THdf5Attribute.WriteUInt16(LocId: hid_t; const Name: UTF8String;
  Value: Word);
var
  Space: THdf5Dataspace;
  AttrId: hid_t;
begin
  Space := THdf5Dataspace.CreateScalar;
  try
    AttrId := CheckH5Id(H5Acreate2(LocId, PAnsiChar(Name), H5T_STD_U16LE,
      Space.Handle, H5P_DEFAULT, H5P_DEFAULT), 'H5Acreate2');
    try
      CheckH5(H5Awrite(AttrId, H5T_NATIVE_UINT16, @Value), 'H5Awrite');
    finally
      H5Aclose(AttrId);
    end;
  finally
    Space.Free;
  end;
end;

class procedure THdf5Attribute.WriteDouble(LocId: hid_t; const Name: UTF8String;
  Value: Double);
var
  Space: THdf5Dataspace;
  AttrId: hid_t;
begin
  Space := THdf5Dataspace.CreateScalar;
  try
    AttrId := CheckH5Id(H5Acreate2(LocId, PAnsiChar(Name), H5T_IEEE_F64LE,
      Space.Handle, H5P_DEFAULT, H5P_DEFAULT), 'H5Acreate2');
    try
      CheckH5(H5Awrite(AttrId, H5T_NATIVE_DOUBLE, @Value), 'H5Awrite');
    finally
      H5Aclose(AttrId);
    end;
  finally
    Space.Free;
  end;
end;

class procedure THdf5Attribute.WriteBoolean(LocId: hid_t; const Name: UTF8String;
  Value: Boolean);
var
  Space: THdf5Dataspace;
  AttrId: hid_t;
  ByteValue: Byte;
begin
  ByteValue := Ord(Value);
  Space := THdf5Dataspace.CreateScalar;
  try
    AttrId := CheckH5Id(H5Acreate2(LocId, PAnsiChar(Name), H5T_STD_U8LE,
      Space.Handle, H5P_DEFAULT, H5P_DEFAULT), 'H5Acreate2');
    try
      CheckH5(H5Awrite(AttrId, H5T_NATIVE_UINT8, @ByteValue), 'H5Awrite');
    finally
      H5Aclose(AttrId);
    end;
  finally
    Space.Free;
  end;
end;
{$ENDIF}

end.
