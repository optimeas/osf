// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

// Plain C-to-Delphi type, constant and exception declarations for the HDF5
// C library binding. This unit knows nothing about OSF — it is a pure HDF5
// API vocabulary, shared by Hdf5.Api and Hdf5.Wrapper.
//
// hid_t is a 64-bit integer (HDF5 >= 1.10); never declare it 32-bit. Every
// HDF5 function is cdecl. See dataformats/hdf5/WISSENSBASIS.md for the
// binding rationale and the _g-variable mechanism.
unit Hdf5.Types;

interface

uses
  System.SysUtils;

type
  // ── Core scalar types (C -> Delphi) ─────────────────────────────────────────
  hid_t    = Int64;     // object / identifier handle (int64_t)
  herr_t   = Integer;   // generic error return (int): < 0 means error
  hsize_t  = UInt64;    // dataspace dimension size (uint64_t)
  hssize_t = Int64;     // signed dimension size (int64_t)
  haddr_t  = UInt64;    // file address (uint64_t)
  htri_t   = Integer;   // tri-state (int): > 0 true, 0 false, < 0 error
  hbool_t  = ByteBool;  // C99 bool

  Phid_t   = ^hid_t;
  Phsize_t = ^hsize_t;

  // ── Exceptions ──────────────────────────────────────────────────────────────
  // Base for every error raised by the HDF5 binding units.
  EHdf5Exception = class(Exception);
  // A negative return value came back from an HDF5 API call.
  EHdf5ApiError = class(EHdf5Exception);
  // hdf5.dll could not be loaded, or a required symbol was not found in it.
  EHdf5DllNotLoaded = class(EHdf5Exception);

const
  // ── File access flags (H5Fcreate / H5Fopen) ────────────────────────────────
  H5F_ACC_RDONLY = $0000;
  H5F_ACC_RDWR   = $0001;
  H5F_ACC_TRUNC  = $0002;
  H5F_ACC_EXCL   = $0004;

  // ── H5F_libver_t — library format version bounds ────────────────────────────
  H5F_LIBVER_EARLIEST = 0;
  H5F_LIBVER_V18      = 1;
  H5F_LIBVER_V110     = 2;
  H5F_LIBVER_V112     = 3;
  H5F_LIBVER_V114     = 4;
  H5F_LIBVER_LATEST   = H5F_LIBVER_V114;  // highest format in HDF5 1.14.x

  // ── H5T_class_t — datatype class (H5Tcreate) ────────────────────────────────
  H5T_INTEGER  = 0;
  H5T_FLOAT    = 1;
  H5T_STRING   = 3;
  H5T_OPAQUE   = 5;
  H5T_COMPOUND = 6;

  // ── H5T_cset_t — character set ───────────────────────────────────────────────
  H5T_CSET_ASCII = 0;
  H5T_CSET_UTF8  = 1;

  // ── H5S_class_t — dataspace class (H5Screate) ───────────────────────────────
  H5S_SCALAR = 0;
  H5S_SIMPLE = 1;
  H5S_NULL   = 2;

  // ── H5S_seloper_t — hyperslab selection operator ────────────────────────────
  H5S_SELECT_SET = 0;

  // ── H5F_scope_t — flush scope ────────────────────────────────────────────────
  H5F_SCOPE_LOCAL  = 0;
  H5F_SCOPE_GLOBAL = 1;

  // ── Special handle / sentinel constants ──────────────────────────────────────
  // H5P_DEFAULT, H5S_ALL and H5E_DEFAULT are all the literal 0; kept as typed
  // hid_t constants so call sites read as intent rather than a magic zero.
  H5P_DEFAULT   : hid_t      = 0;
  H5S_ALL       : hid_t      = 0;
  H5E_DEFAULT   : hid_t      = 0;
  H5S_UNLIMITED : hsize_t    = High(UInt64);     // ((hsize_t)-1)
  H5T_VARIABLE  : NativeUInt = High(NativeUInt); // ((size_t)-1)

implementation

end.
