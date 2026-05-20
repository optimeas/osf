// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

// Dynamic binding of the HDF5 C library (hdf5.dll). Resolves the DLL through
// a six-stage search, loads it, binds every function pointer, runs the
// mandatory H5open()-first initialisation and reads the predefined-datatype
// and property-list-class _g globals out of the DLL data segment.
//
// This unit knows nothing about OSF. Windows-only — HDF5 export targets
// Win64; on other platforms the unit compiles empty.
//
// Initialisation order (see WISSENSBASIS.md §3.1) is non-negotiable:
//   LoadLibrary -> GetProcAddress -> H5open() -> H5Eset_auto2 -> read _g vars.
unit Hdf5.Api;

interface

{$IFDEF MSWINDOWS}
uses
  System.SysUtils,
  Winapi.Windows,
  Hdf5.Types;

const
  OSF_HDF5_DLL_NAME = 'hdf5.dll';
  OSF_HDF5_ENV_VAR  = 'OSF_HDF5_LIB_DIR';
  {$IFDEF WIN64}
  OSF_HDF5_LIB_DIR_DEFAULT = 'V:\github\osf\dataformats\hdf5\lib\win64';
  {$ENDIF}
  {$IFDEF WIN32}
  OSF_HDF5_LIB_DIR_DEFAULT = 'V:\github\osf\dataformats\hdf5\lib\win32';
  {$ENDIF}

type
  // ── Function-pointer types — every HDF5 function is cdecl ───────────────────
  TH5open           = function: herr_t; cdecl;
  TH5close          = function: herr_t; cdecl;
  TH5get_libversion = function(majnum, minnum, relnum: PCardinal): herr_t; cdecl;
  TH5Eset_auto2     = function(estack_id: hid_t; func, client_data: Pointer): herr_t; cdecl;

  TH5Fcreate = function(filename: PAnsiChar; flags: Cardinal; fcpl_id, fapl_id: hid_t): hid_t; cdecl;
  TH5Fclose  = function(file_id: hid_t): herr_t; cdecl;
  TH5Fflush  = function(object_id: hid_t; scope: Integer): herr_t; cdecl;

  TH5Gcreate2 = function(loc_id: hid_t; name: PAnsiChar; lcpl_id, gcpl_id, gapl_id: hid_t): hid_t; cdecl;
  TH5Gclose   = function(group_id: hid_t): herr_t; cdecl;
  TH5Lexists  = function(loc_id: hid_t; name: PAnsiChar; lapl_id: hid_t): htri_t; cdecl;

  TH5Screate           = function(space_type: Integer): hid_t; cdecl;
  TH5Screate_simple    = function(rank: Integer; const dims, maxdims: Phsize_t): hid_t; cdecl;
  TH5Sclose            = function(space_id: hid_t): herr_t; cdecl;
  TH5Sselect_hyperslab = function(space_id: hid_t; op: Integer;
                                  const start, stride, count, block: Phsize_t): herr_t; cdecl;

  TH5Tcopy     = function(type_id: hid_t): hid_t; cdecl;
  TH5Tcreate   = function(cls: Integer; size: NativeUInt): hid_t; cdecl;
  TH5Tinsert   = function(parent_id: hid_t; name: PAnsiChar; offset: NativeUInt; member_id: hid_t): herr_t; cdecl;
  TH5Tset_size = function(type_id: hid_t; size: NativeUInt): herr_t; cdecl;
  TH5Tset_tag  = function(type_id: hid_t; tag: PAnsiChar): herr_t; cdecl;
  TH5Tset_cset = function(type_id: hid_t; cset: Integer): herr_t; cdecl;
  TH5Tclose    = function(type_id: hid_t): herr_t; cdecl;

  TH5Dcreate2    = function(loc_id: hid_t; name: PAnsiChar;
                            type_id, space_id, lcpl_id, dcpl_id, dapl_id: hid_t): hid_t; cdecl;
  TH5Dclose      = function(dset_id: hid_t): herr_t; cdecl;
  TH5Dwrite      = function(dset_id, mem_type_id, mem_space_id, file_space_id, dxpl_id: hid_t;
                            const buf: Pointer): herr_t; cdecl;
  TH5Dset_extent = function(dset_id: hid_t; const size: Phsize_t): herr_t; cdecl;
  TH5Dget_space  = function(dset_id: hid_t): hid_t; cdecl;

  TH5Acreate2 = function(loc_id: hid_t; attr_name: PAnsiChar;
                         type_id, space_id, acpl_id, aapl_id: hid_t): hid_t; cdecl;
  TH5Awrite   = function(attr_id, type_id: hid_t; const buf: Pointer): herr_t; cdecl;
  TH5Aclose   = function(attr_id: hid_t): herr_t; cdecl;

  TH5Pcreate                        = function(cls_id: hid_t): hid_t; cdecl;
  TH5Pclose                         = function(plist_id: hid_t): herr_t; cdecl;
  TH5Pset_chunk                     = function(plist_id: hid_t; ndims: Integer; const dim: Phsize_t): herr_t; cdecl;
  TH5Pset_deflate                   = function(plist_id: hid_t; level: Cardinal): herr_t; cdecl;
  TH5Pset_shuffle                   = function(plist_id: hid_t): herr_t; cdecl;
  TH5Pset_libver_bounds             = function(plist_id: hid_t; low, high: Integer): herr_t; cdecl;
  TH5Pset_char_encoding             = function(plist_id: hid_t; encoding: Integer): herr_t; cdecl;
  TH5Pset_create_intermediate_group = function(plist_id: hid_t; crt_intmd: Cardinal): herr_t; cdecl;

var
  // ── Bound function pointers — valid after TH5Lib.LoadDll ────────────────────
  H5open: TH5open;
  H5close: TH5close;
  H5get_libversion: TH5get_libversion;
  H5Eset_auto2: TH5Eset_auto2;
  H5Fcreate: TH5Fcreate;
  H5Fclose: TH5Fclose;
  H5Fflush: TH5Fflush;
  H5Gcreate2: TH5Gcreate2;
  H5Gclose: TH5Gclose;
  H5Lexists: TH5Lexists;
  H5Screate: TH5Screate;
  H5Screate_simple: TH5Screate_simple;
  H5Sclose: TH5Sclose;
  H5Sselect_hyperslab: TH5Sselect_hyperslab;
  H5Tcopy: TH5Tcopy;
  H5Tcreate: TH5Tcreate;
  H5Tinsert: TH5Tinsert;
  H5Tset_size: TH5Tset_size;
  H5Tset_tag: TH5Tset_tag;
  H5Tset_cset: TH5Tset_cset;
  H5Tclose: TH5Tclose;
  H5Dcreate2: TH5Dcreate2;
  H5Dclose: TH5Dclose;
  H5Dwrite: TH5Dwrite;
  H5Dset_extent: TH5Dset_extent;
  H5Dget_space: TH5Dget_space;
  H5Acreate2: TH5Acreate2;
  H5Awrite: TH5Awrite;
  H5Aclose: TH5Aclose;
  H5Pcreate: TH5Pcreate;
  H5Pclose: TH5Pclose;
  H5Pset_chunk: TH5Pset_chunk;
  H5Pset_deflate: TH5Pset_deflate;
  H5Pset_shuffle: TH5Pset_shuffle;
  H5Pset_libver_bounds: TH5Pset_libver_bounds;
  H5Pset_char_encoding: TH5Pset_char_encoding;
  H5Pset_create_intermediate_group: TH5Pset_create_intermediate_group;

  // ── Predefined datatypes — _g globals, read out of the DLL after H5open ─────
  // Storage types (little-endian, for on-disk layout).
  H5T_STD_I8LE, H5T_STD_I16LE, H5T_STD_I32LE, H5T_STD_I64LE: hid_t;
  H5T_STD_U8LE, H5T_STD_U16LE, H5T_STD_U32LE, H5T_STD_U64LE: hid_t;
  H5T_IEEE_F32LE, H5T_IEEE_F64LE: hid_t;
  H5T_C_S1: hid_t;
  // Native types (for in-memory buffers).
  H5T_NATIVE_INT8, H5T_NATIVE_INT16, H5T_NATIVE_INT32, H5T_NATIVE_INT64: hid_t;
  H5T_NATIVE_UINT8, H5T_NATIVE_UINT16, H5T_NATIVE_UINT32, H5T_NATIVE_UINT64: hid_t;
  H5T_NATIVE_FLOAT, H5T_NATIVE_DOUBLE: hid_t;
  // Property-list class IDs (the cls_id argument of H5Pcreate).
  H5P_CLS_FILE_CREATE_ID, H5P_CLS_FILE_ACCESS_ID: hid_t;
  H5P_CLS_DATASET_CREATE_ID, H5P_CLS_DATASET_ACCESS_ID: hid_t;
  H5P_CLS_LINK_CREATE_ID, H5P_CLS_ATTRIBUTE_CREATE_ID: hid_t;

type
  // Loads and initialises hdf5.dll. All state is class-level — there is one
  // process-wide HDF5 library instance.
  TH5Lib = class
  strict private
    class var FHandle: HMODULE;
    class var FLoaded: Boolean;
    class var FLoadedPath: string;
    class function GetIsLoaded: Boolean; static;
    class function CandidatePaths(const ExplicitDir: string): TArray<string>;
    class procedure BindFunctions;
    class procedure ReadGlobalVars;
  public
    // Builds the six-stage candidate list and returns the first path whose
    // file exists, or the bare DLL name (PATH lookup) if none exists.
    class function Resolve(const ExplicitDir: UTF8String = ''): UTF8String;
    // Loads exactly Path, binds the functions, runs H5open and reads the
    // _g globals. Raises on any failure.
    class procedure LoadDll(const Path: UTF8String);
    // Idempotent: loads the library via the six-stage search if not already
    // loaded. On total failure raises EHdf5DllNotLoaded listing every path
    // that was tried.
    class procedure EnsureLoaded(const ExplicitDir: UTF8String = '');
    // H5close + FreeLibrary. Safe to call when nothing is loaded.
    class procedure UnloadDll;
    class property IsLoaded: Boolean read GetIsLoaded;
    class property LoadedPath: string read FLoadedPath;
  end;

// Reads the value of an exported global hid_t variable (the _g mechanism).
function GetH5Var(LibHandle: HMODULE; const SymbolName: AnsiString): hid_t;
// Raises EHdf5ApiError when an herr_t-returning call came back negative.
procedure CheckH5(ResultCode: Integer; const FuncName: string);
// Raises EHdf5ApiError when a hid_t-returning call came back negative;
// otherwise passes the id straight through.
function CheckH5Id(Id: hid_t; const FuncName: string): hid_t;
{$ENDIF}

implementation

{$IFDEF MSWINDOWS}

resourcestring
  SHdf5DllLoadFailed   = 'Could not load the HDF5 library "%s".';
  SHdf5DllNotFound     = 'hdf5.dll could not be located. Paths tried:'#10'%s';
  SHdf5SymbolNotFound  = 'HDF5 symbol not found in the loaded DLL: %s';
  SHdf5ApiCallFailed   = 'HDF5 call %s failed (return code %d).';
  SHdf5ApiIdFailed     = 'HDF5 call %s returned an invalid identifier (%d).';

// ── Free functions ────────────────────────────────────────────────────────────

function GetH5Var(LibHandle: HMODULE; const SymbolName: AnsiString): hid_t;
var
  Address: Phid_t;
begin
  Address := Phid_t(GetProcAddress(LibHandle, PAnsiChar(SymbolName)));
  if Address = nil then
    raise EHdf5DllNotLoaded.CreateFmt(SHdf5SymbolNotFound, [string(SymbolName)]);
  Result := Address^;
end;

procedure CheckH5(ResultCode: Integer; const FuncName: string);
begin
  if ResultCode < 0 then
    raise EHdf5ApiError.CreateFmt(SHdf5ApiCallFailed, [FuncName, ResultCode]);
end;

function CheckH5Id(Id: hid_t; const FuncName: string): hid_t;
begin
  if Id < 0 then
    raise EHdf5ApiError.CreateFmt(SHdf5ApiIdFailed, [FuncName, Id]);
  Result := Id;
end;

// ── TH5Lib ────────────────────────────────────────────────────────────────────

class function TH5Lib.GetIsLoaded: Boolean;
begin
  Result := FLoaded;
end;

class function TH5Lib.CandidatePaths(const ExplicitDir: string): TArray<string>;
var
  List: TArray<string>;
  EnvDir: string;
  ExeDir: string;

  procedure Add(const Dir: string);
  begin
    if Dir = '' then
      Exit;
    List := List + [IncludeTrailingPathDelimiter(Dir) + OSF_HDF5_DLL_NAME];
  end;

begin
  List := nil;
  ExeDir := ExtractFilePath(ParamStr(0));
  // 1. Explicit directory supplied by the caller (CLI --hdf5-lib-dir).
  Add(ExplicitDir);
  // 2. Environment variable.
  EnvDir := GetEnvironmentVariable(OSF_HDF5_ENV_VAR);
  Add(EnvDir);
  // 3. Compile-time default for this platform.
  {$IF Defined(WIN64) or Defined(WIN32)}
  Add(OSF_HDF5_LIB_DIR_DEFAULT);
  {$IFEND}
  // 4. A lib\ subdirectory next to the executable (deployed layout).
  Add(ExeDir + 'lib');
  // 5. Next to the executable.
  Add(ExeDir);
  // 6. System PATH — bare name, resolved by the loader.
  List := List + [OSF_HDF5_DLL_NAME];
  Result := List;
end;

class function TH5Lib.Resolve(const ExplicitDir: UTF8String = ''): UTF8String;
var
  Candidates: TArray<string>;
  Candidate: string;
begin
  Candidates := CandidatePaths(string(ExplicitDir));
  for Candidate in Candidates do
    if (Candidate = OSF_HDF5_DLL_NAME) or FileExists(Candidate) then
      Exit(UTF8String(Candidate));
  // Unreachable: the bare DLL name always satisfies the loop above.
  Result := UTF8String(OSF_HDF5_DLL_NAME);
end;

class procedure TH5Lib.BindFunctions;

  function GetProc(const Name: AnsiString): Pointer;
  begin
    Result := GetProcAddress(FHandle, PAnsiChar(Name));
    if Result = nil then
      raise EHdf5DllNotLoaded.CreateFmt(SHdf5SymbolNotFound, [string(Name)]);
  end;

begin
  H5open            := TH5open(GetProc('H5open'));
  H5close           := TH5close(GetProc('H5close'));
  H5get_libversion  := TH5get_libversion(GetProc('H5get_libversion'));
  H5Eset_auto2      := TH5Eset_auto2(GetProc('H5Eset_auto2'));
  H5Fcreate         := TH5Fcreate(GetProc('H5Fcreate'));
  H5Fclose          := TH5Fclose(GetProc('H5Fclose'));
  H5Fflush          := TH5Fflush(GetProc('H5Fflush'));
  H5Gcreate2        := TH5Gcreate2(GetProc('H5Gcreate2'));
  H5Gclose          := TH5Gclose(GetProc('H5Gclose'));
  H5Lexists         := TH5Lexists(GetProc('H5Lexists'));
  H5Screate         := TH5Screate(GetProc('H5Screate'));
  H5Screate_simple  := TH5Screate_simple(GetProc('H5Screate_simple'));
  H5Sclose          := TH5Sclose(GetProc('H5Sclose'));
  H5Sselect_hyperslab := TH5Sselect_hyperslab(GetProc('H5Sselect_hyperslab'));
  H5Tcopy           := TH5Tcopy(GetProc('H5Tcopy'));
  H5Tcreate         := TH5Tcreate(GetProc('H5Tcreate'));
  H5Tinsert         := TH5Tinsert(GetProc('H5Tinsert'));
  H5Tset_size       := TH5Tset_size(GetProc('H5Tset_size'));
  H5Tset_tag        := TH5Tset_tag(GetProc('H5Tset_tag'));
  H5Tset_cset       := TH5Tset_cset(GetProc('H5Tset_cset'));
  H5Tclose          := TH5Tclose(GetProc('H5Tclose'));
  H5Dcreate2        := TH5Dcreate2(GetProc('H5Dcreate2'));
  H5Dclose          := TH5Dclose(GetProc('H5Dclose'));
  H5Dwrite          := TH5Dwrite(GetProc('H5Dwrite'));
  H5Dset_extent     := TH5Dset_extent(GetProc('H5Dset_extent'));
  H5Dget_space      := TH5Dget_space(GetProc('H5Dget_space'));
  H5Acreate2        := TH5Acreate2(GetProc('H5Acreate2'));
  H5Awrite          := TH5Awrite(GetProc('H5Awrite'));
  H5Aclose          := TH5Aclose(GetProc('H5Aclose'));
  H5Pcreate         := TH5Pcreate(GetProc('H5Pcreate'));
  H5Pclose          := TH5Pclose(GetProc('H5Pclose'));
  H5Pset_chunk      := TH5Pset_chunk(GetProc('H5Pset_chunk'));
  H5Pset_deflate    := TH5Pset_deflate(GetProc('H5Pset_deflate'));
  H5Pset_shuffle    := TH5Pset_shuffle(GetProc('H5Pset_shuffle'));
  H5Pset_libver_bounds := TH5Pset_libver_bounds(GetProc('H5Pset_libver_bounds'));
  H5Pset_char_encoding := TH5Pset_char_encoding(GetProc('H5Pset_char_encoding'));
  H5Pset_create_intermediate_group :=
    TH5Pset_create_intermediate_group(GetProc('H5Pset_create_intermediate_group'));
end;

class procedure TH5Lib.ReadGlobalVars;
begin
  // Storage types.
  H5T_STD_I8LE   := GetH5Var(FHandle, 'H5T_STD_I8LE_g');
  H5T_STD_I16LE  := GetH5Var(FHandle, 'H5T_STD_I16LE_g');
  H5T_STD_I32LE  := GetH5Var(FHandle, 'H5T_STD_I32LE_g');
  H5T_STD_I64LE  := GetH5Var(FHandle, 'H5T_STD_I64LE_g');
  H5T_STD_U8LE   := GetH5Var(FHandle, 'H5T_STD_U8LE_g');
  H5T_STD_U16LE  := GetH5Var(FHandle, 'H5T_STD_U16LE_g');
  H5T_STD_U32LE  := GetH5Var(FHandle, 'H5T_STD_U32LE_g');
  H5T_STD_U64LE  := GetH5Var(FHandle, 'H5T_STD_U64LE_g');
  H5T_IEEE_F32LE := GetH5Var(FHandle, 'H5T_IEEE_F32LE_g');
  H5T_IEEE_F64LE := GetH5Var(FHandle, 'H5T_IEEE_F64LE_g');
  H5T_C_S1       := GetH5Var(FHandle, 'H5T_C_S1_g');
  // Native types.
  H5T_NATIVE_INT8   := GetH5Var(FHandle, 'H5T_NATIVE_INT8_g');
  H5T_NATIVE_INT16  := GetH5Var(FHandle, 'H5T_NATIVE_INT16_g');
  H5T_NATIVE_INT32  := GetH5Var(FHandle, 'H5T_NATIVE_INT32_g');
  H5T_NATIVE_INT64  := GetH5Var(FHandle, 'H5T_NATIVE_INT64_g');
  H5T_NATIVE_UINT8  := GetH5Var(FHandle, 'H5T_NATIVE_UINT8_g');
  H5T_NATIVE_UINT16 := GetH5Var(FHandle, 'H5T_NATIVE_UINT16_g');
  H5T_NATIVE_UINT32 := GetH5Var(FHandle, 'H5T_NATIVE_UINT32_g');
  H5T_NATIVE_UINT64 := GetH5Var(FHandle, 'H5T_NATIVE_UINT64_g');
  H5T_NATIVE_FLOAT  := GetH5Var(FHandle, 'H5T_NATIVE_FLOAT_g');
  H5T_NATIVE_DOUBLE := GetH5Var(FHandle, 'H5T_NATIVE_DOUBLE_g');
  // Property-list class IDs.
  H5P_CLS_FILE_CREATE_ID    := GetH5Var(FHandle, 'H5P_CLS_FILE_CREATE_ID_g');
  H5P_CLS_FILE_ACCESS_ID    := GetH5Var(FHandle, 'H5P_CLS_FILE_ACCESS_ID_g');
  H5P_CLS_DATASET_CREATE_ID := GetH5Var(FHandle, 'H5P_CLS_DATASET_CREATE_ID_g');
  H5P_CLS_DATASET_ACCESS_ID := GetH5Var(FHandle, 'H5P_CLS_DATASET_ACCESS_ID_g');
  H5P_CLS_LINK_CREATE_ID    := GetH5Var(FHandle, 'H5P_CLS_LINK_CREATE_ID_g');
  H5P_CLS_ATTRIBUTE_CREATE_ID := GetH5Var(FHandle, 'H5P_CLS_ATTRIBUTE_CREATE_ID_g');
end;

class procedure TH5Lib.LoadDll(const Path: UTF8String);
var
  Handle: HMODULE;
  PathStr: string;
begin
  if FLoaded then
    Exit;
  PathStr := string(Path);
  if SameText(PathStr, OSF_HDF5_DLL_NAME) then
    // Bare name — let the OS resolve it through the standard search order.
    Handle := LoadLibrary(PChar(PathStr))
  else
    // Full path — LOAD_WITH_ALTERED_SEARCH_PATH makes the loader resolve the
    // dependent runtime DLLs (msvcp140, vcruntime140, ...) from hdf5.dll's
    // own directory, so the bundled lib\win64 set works without a system
    // VC++ redistributable.
    Handle := LoadLibraryEx(PChar(ExpandFileName(PathStr)), 0,
      LOAD_WITH_ALTERED_SEARCH_PATH);
  if Handle = 0 then
    raise EHdf5DllNotLoaded.CreateFmt(SHdf5DllLoadFailed, [PathStr]);
  FHandle := Handle;
  try
    // Mandatory order: bind, H5open, silence the error auto-printer, then
    // and only then read the _g globals (they are 0 before H5open).
    BindFunctions;
    CheckH5(H5open(), 'H5open');
    H5Eset_auto2(H5E_DEFAULT, nil, nil);
    ReadGlobalVars;
  except
    FreeLibrary(FHandle);
    FHandle := 0;
    raise;
  end;
  FLoaded := True;
  FLoadedPath := string(Path);
end;

class procedure TH5Lib.EnsureLoaded(const ExplicitDir: UTF8String = '');
var
  Candidates: TArray<string>;
  Candidate: string;
  LastError: string;
  Report: string;
begin
  if FLoaded then
    Exit;
  Candidates := CandidatePaths(string(ExplicitDir));
  LastError := '';
  for Candidate in Candidates do
    if (Candidate = OSF_HDF5_DLL_NAME) or FileExists(Candidate) then
      try
        LoadDll(UTF8String(Candidate));
        Exit;
      except
        on E: EHdf5Exception do
          LastError := E.Message;
      end;
  Report := string.Join(#10, Candidates);
  if LastError <> '' then
    Report := Report + #10'Last error: ' + LastError;
  raise EHdf5DllNotLoaded.CreateFmt(SHdf5DllNotFound, [Report]);
end;

class procedure TH5Lib.UnloadDll;
begin
  if not FLoaded then
    Exit;
  try
    if Assigned(H5close) then
      H5close();
  except
    // Shutdown is best-effort; never let H5close failure escape.
  end;
  FreeLibrary(FHandle);
  FHandle := 0;
  FLoaded := False;
  FLoadedPath := '';
end;

initialization

finalization
  TH5Lib.UnloadDll;
{$ENDIF}

end.
