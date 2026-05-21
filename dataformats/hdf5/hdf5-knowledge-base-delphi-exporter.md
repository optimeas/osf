# HDF5 Knowledge Base: OSF→HDF5 Exporter in Delphi via libhdf5.dll

**Purpose of this document**

This document is a complete, self-contained knowledge base. It is handed to **Claude Chat**. From it, Claude Chat is to formulate a precise **instruction for Claude Code** that enables Claude Code to implement an **OSF→HDF5 exporter in Delphi**. The exporter uses the official **HDF5 C library (`hdf5.dll`) for Windows** — NO native reimplementation of the HDF5 binary format is built.

Claude Chat has no access to the source repository. Therefore this document contains all the necessary technical facts directly in the text: exact function signatures, type definitions, constant values, and binding mechanics.

As of: 2026-05-19. Sources: HDF5 source repo (`H5*public.h`, version 2.x; valid from HDF5 1.10 onward), OSF specification (`osf_general.md`), and the OSF Rust reference implementation.

---

## 1. The big picture — what is to be built

A Delphi application that reads an OSF4/OSF5 file and writes a semantically equivalent HDF5 file:

```
   OSF file   ──►  [Delphi exporter]  ──►  HDF5 file
  (.osf/.osfz)      |                       (.h5)
                    ├─ OSF parser (native in Delphi)
                    └─ HDF5 writer (calls into hdf5.dll)
```

- **OSF side:** fully implemented natively in Delphi (reading the binary format — see section 9).
- **HDF5 side:** all write operations go through function calls into `hdf5.dll`. The DLL handles all the complexity of the HDF5 binary format (superblock, B-trees, heaps, chunking, compression).

The hardest part is **not** the HDF5 format itself, but the **correct DLL binding from Delphi** (section 3). That is where most of the pitfalls lie.

---

## 2. Obtaining the HDF5 library (Windows)

There are two ways to get `hdf5.dll`: obtain it precompiled (easy) or build it yourself from source (full control).

### 2.1 Basic rules

- **Architecture:** must match the Delphi target platform — **Win64 DLL for Delphi Win64**, Win32 DLL for Delphi Win32. Never mixable.
- **Version:** This document applies to **HDF5 ≥ 1.10**. From 1.10 on, `hid_t` is a 64-bit integer; older versions (1.8) would have `hid_t` as 32-bit and are NOT supported. Recommended: a **stable, released** version — **HDF5 1.14.6** (broadest compatibility, readable by h5py/HDFView/MATLAB) or 2.0.0 / 2.1.0. Do **not** use a development/snapshot version.
- **Build type:** always use **Release** → the DLL is named `hdf5.dll`. A debug build produces `hdf5_D.dll`.
- **Relevant files:** `hdf5.dll` (core library). With deflate compression enabled, additionally `zlib.dll` — UNLESS zlib was statically baked into `hdf5.dll` (see 2.3), in which case it is not needed. `libaec.dll`/`szip.dll` are NOT required (no SZIP).
- **Deployment:** place the DLL(s) next to the exporter EXE (app directory) or in a folder on the `PATH`. The Delphi binding loads `hdf5.dll` via `LoadLibrary` from the app directory.
- **Version-dependent symbols:** Some predefined types only exist from certain versions on (`H5T_IEEE_F16*` from 1.14.4, `H5T_COMPLEX_*` from 2.0; the enum `H5F_LIBVER_V200` does not yet exist in 1.14). The base types needed for this exporter (`H5T_STD_I8..I64`, `U8..U64`, `H5T_IEEE_F32/F64`, all `H5T_NATIVE_*`, `H5T_C_S1`) and functions exist in **all** supported versions.

### 2.2 Path A — obtain precompiled (recommended when no special configuration is needed)

Download the official precompiled Windows binaries from the HDF Group (`hdfgroup.org`, Downloads section), version **1.14.6**, matching architecture. Copy `hdf5.dll` (and possibly `zlib.dll`) from the distribution into the app directory. Done.

### 2.3 Path B — build it yourself from source

Prerequisites: **Visual Studio 2022** (or just the "Build Tools for Visual Studio") and **CMake** ≥ 3.18.

**Important note on version selection:** A directly checked-out HDF5 `develop` branch is an unreleased development version — do NOT build from it. Use a stable release tag instead. If building from an existing HDF5 Git clone, fetch the tag into a separate worktree so the main branch stays untouched:

```powershell
git -C <path-to-hdf5-clone> worktree add <path>\hdf5-1.14.6 hdf5_1.14.6
```

**Configure and build** (64-bit; for 32-bit replace `-A x64` with `-A Win32` and choose your own folder names):

```powershell
cmake -G "Visual Studio 17 2022" -A x64 `
  -S <path>\hdf5-1.14.6 -B <path>\hdf5-1.14.6\build-x64 `
  -D BUILD_SHARED_LIBS=ON `
  -D HDF5_BUILD_CPP_LIB=OFF -D HDF5_BUILD_FORTRAN=OFF -D HDF5_BUILD_JAVA=OFF `
  -D HDF5_BUILD_HL_LIB=OFF -D HDF5_BUILD_EXAMPLES=OFF -D BUILD_TESTING=OFF `
  -D HDF5_BUILD_TOOLS=ON `
  -D HDF5_ENABLE_ZLIB_SUPPORT=ON -D HDF5_ENABLE_SZIP_SUPPORT=OFF `
  -D HDF5_ALLOW_EXTERNAL_SUPPORT=TGZ -D HDF5_USE_ZLIB_STATIC=ON

cmake --build <path>\hdf5-1.14.6\build-x64 --config Release --parallel
```

**Meaning of the options:**

| Option | Effect |
|---|---|
| `BUILD_SHARED_LIBS=ON` | produces the **DLL** instead of a static `.lib`. |
| `HDF5_ALLOW_EXTERNAL_SUPPORT=TGZ` + `HDF5_USE_ZLIB_STATIC=ON` | CMake downloads the zlib sources and **bakes zlib statically into `hdf5.dll`** → no separate `zlib.dll` at deployment. |
| `HDF5_ENABLE_ZLIB_SUPPORT=ON` | enables deflate compression (`H5Pset_deflate`). If it is not needed right away, it can be omitted along with the two zlib lines — then no download is required. |
| `HDF5_BUILD_TOOLS=ON` | also builds `h5dump.exe` — valuable for verifying the export results. |
| `CPP/FORTRAN/JAVA/HL/EXAMPLES/TESTING=OFF` | leaner, faster build; the exporter needs only the plain C library. |

### 2.4 Where the DLL is located after the build

The build and install folders do NOT exist beforehand — they are created by the CMake commands. After `cmake --build`, the DLL is directly in the build folder:

```
<path>\hdf5-1.14.6\build-x64\bin\Release\hdf5.dll      ← the finished DLL
<path>\hdf5-1.14.6\build-x64\bin\Release\h5dump.exe    ← verification tool
```

A separate `cmake --install` step is not necessary. Simply copy `hdf5.dll` into the exporter's app directory (next to the EXE).

---

## 3. CRITICAL: The HDF5 DLL binding in Delphi

This is the most error-prone part. Read it carefully.

### 3.1 Base types — C → Delphi

The HDF5 API uses its own type aliases. Exact mapping:

| C type (HDF5) | C definition | Delphi type | Bytes |
|---|---|---|---|
| `hid_t` | `int64_t` | `Int64` | 8 |
| `herr_t` | `int` | `Integer` (Int32) | 4 |
| `hsize_t` | `uint64_t` | `UInt64` | 8 |
| `hssize_t` | `int64_t` | `Int64` | 8 |
| `haddr_t` | `uint64_t` | `UInt64` | 8 |
| `htri_t` | `int` | `Integer` (tri-state: >0 = true, 0 = false, <0 = error) | 4 |
| `hbool_t` | `bool` (C99) | `ByteBool` | 1 |
| `size_t` | platform-dependent | `NativeUInt` | 8 (Win64) / 4 (Win32) |
| `unsigned` | `unsigned int` | `Cardinal` (UInt32) | 4 |
| `int` | `int` | `Integer` | 4 |
| `const char *` | C string | `PAnsiChar` (UTF-8, null-terminated) | pointer |
| `void *` | arbitrary pointer | `Pointer` | pointer |

Recommended Delphi type declaration:

```pascal
type
  hid_t    = Int64;
  herr_t   = Integer;
  hsize_t  = UInt64;
  hssize_t = Int64;
  haddr_t  = UInt64;
  htri_t   = Integer;
  hbool_t  = ByteBool;
  Phid_t   = ^hid_t;
  Phsize_t = ^hsize_t;
```

### 3.2 Calling convention & DLL name

- **DLL name:** `hdf5.dll` (release build of the official distribution).
- **Calling convention:** The HDF5 headers use the C standard convention without explicit decoration. In Delphi that means **`cdecl`** for ALL function declarations. (On Win64 there is technically only one convention, but `cdecl` must still be declared, because Delphi's default is `register` and would be wrong. On Win32, `cdecl` is mandatorily correct.)
- **Symbol names:** The DLL exports plain C names with no name mangling and no `_` prefix or `@n` suffix. `GetProcAddress(h, 'H5Fcreate')` returns the function pointer directly.

### 3.3 The `_g` variable mechanism — the most important pitfall

In C, the predefined data types such as `H5T_NATIVE_INT` or `H5T_STD_I32LE` are **not constants**, but preprocessor macros that point to **global variables** exported by the DLL. Example:

```c
#define H5T_NATIVE_INT (H5OPEN H5T_NATIVE_INT_g)
```

`H5T_NATIVE_INT_g` is a real global `hid_t` variable in the DLL's data segment. The `H5OPEN` construct ensures that `H5open()` is called if the library has not yet been initialized.

**Consequence for Delphi:** There are no fixed numeric values for `H5T_NATIVE_INT` etc. The values must be **fetched from the DLL at runtime**:

```pascal
// Dynamic binding: get the address of the variable, then dereference it.
function GetH5Var(aHandle: HMODULE; const aName: AnsiString): hid_t;
var
  P: Phid_t;
begin
  P := Phid_t(GetProcAddress(aHandle, PAnsiChar(aName)));
  if P = nil then
    raise Exception.CreateFmt('HDF5 symbol not found: %s', [aName]);
  Result := P^;   // read the value of the global variable
end;
```

The same applies to the **property list classes** (`H5P_FILE_ACCESS`, `H5P_DATASET_CREATE`, etc.) — these are also `_g` variables (`H5P_CLS_FILE_ACCESS_ID_g`, etc.).

With a **static import** this would also work:

```pascal
var
  H5T_NATIVE_INT_g: hid_t; external 'hdf5.dll' name 'H5T_NATIVE_INT_g';
```

But: in BOTH cases the value is only valid after `H5open()` (see 3.4).

### 3.4 Mandatory initialization order

```
1. LoadLibrary('hdf5.dll')                    // load the DLL
2. Fetch all function pointers via GetProcAddress
3. CALL H5open()  (exactly once, before anything else)
4. ONLY NOW read the _g variables (H5T_NATIVE_*_g, H5P_CLS_*_g, ...)
5. ... actual work ...
6. H5close()  at program end
7. FreeLibrary
```

If step 3 is omitted or step 4 is moved earlier, the `_g` variables contain `0` or garbage — and all subsequent API calls fail. **This is the most common mistake with hand-built HDF5 bindings.**

### 3.5 Static vs. dynamic binding — recommendation

- **Dynamic binding** (`LoadLibrary` + `GetProcAddress`): recommended. Advantage: a controlled error message when `hdf5.dll` is missing; the DLL can be optional.
- **Static binding** (`external 'hdf5.dll'`): easier to write, but the EXE will not even start if the DLL is missing — worse diagnostics.

Recommendation: encapsulate **dynamic binding** in a unit (`Hdf5.Api.pas`) that provides a set of function-pointer variables and a `LoadHdf5`/`UnloadHdf5` procedure.

### 3.6 String handling & UTF-8

- HDF5 functions expect `const char *` = **null-terminated UTF-8 strings**. In Delphi: `UTF8String` → `PAnsiChar`.
- Delphi's `string` (UTF-16) must be converted before every call via `UTF8Encode`/a `UTF8String` cast.
- For HDF5 to interpret object names (groups, datasets, attributes) as UTF-8 instead of ASCII: set `H5Pset_char_encoding(lcpl, H5T_CSET_UTF8)` on the **Link Creation Property List (LCPL)**. Without this, names are treated as ASCII — harmless for pure ASCII names, but important when channel names contain umlauts.
- For **string data types** (HDF5 datasets/attributes that store text): set `H5Tset_cset(typeId, H5T_CSET_UTF8)`.

### 3.7 Error handling

- Functions returning `hid_t`: **negative value = error**.
- Functions returning `herr_t`: **negative value = error**, `0` or positive = OK.
- Functions returning `htri_t`: `>0` = true, `0` = false, `<0` = error.
- By default, HDF5 prints an error stack to `stderr`. `H5Eprint2` expects a C `FILE*` pointer — unwieldy from Delphi. **Recommendation:** disable auto-printing right after `H5open()` with
  `H5Eset_auto2(H5E_DEFAULT, nil, nil)` (constant `H5E_DEFAULT = 0`),
  and check errors exclusively via the return values. Every API call is wrapped in a Delphi wrapper that raises an `EHdf5Exception` on a negative result.

---

## 4. HDF5 C API — function signatures

All functions are `cdecl`. Order: C prototype, followed by the Delphi function-pointer pattern.

### 4.1 Library initialization (H5public.h)

```c
herr_t H5open(void);
herr_t H5close(void);
herr_t H5garbage_collect(void);
herr_t H5get_libversion(unsigned *majnum, unsigned *minnum, unsigned *relnum);
```

### 4.2 File (H5Fpublic.h)

```c
hid_t  H5Fcreate(const char *filename, unsigned flags, hid_t fcpl_id, hid_t fapl_id);
hid_t  H5Fopen  (const char *filename, unsigned flags, hid_t fapl_id);
herr_t H5Fclose (hid_t file_id);
herr_t H5Fflush (hid_t object_id, H5F_scope_t scope);
```

### 4.3 Group (H5Gpublic.h)

```c
hid_t  H5Gcreate2(hid_t loc_id, const char *name, hid_t lcpl_id, hid_t gcpl_id, hid_t gapl_id);
hid_t  H5Gopen2  (hid_t loc_id, const char *name, hid_t gapl_id);
herr_t H5Gclose  (hid_t group_id);
```

### 4.4 Dataspace (H5Spublic.h)

```c
hid_t   H5Screate       (H5S_class_t type);
hid_t   H5Screate_simple(int rank, const hsize_t dims[], const hsize_t maxdims[]);
herr_t  H5Sclose        (hid_t space_id);
herr_t  H5Sselect_hyperslab(hid_t space_id, H5S_seloper_t op,
                            const hsize_t start[], const hsize_t stride[],
                            const hsize_t count[], const hsize_t block[]);
herr_t  H5Sset_extent_simple(hid_t space_id, int rank,
                             const hsize_t dims[], const hsize_t max[]);
int     H5Sget_simple_extent_dims(hid_t space_id, hsize_t dims[], hsize_t maxdims[]);
hssize_t H5Sget_simple_extent_npoints(hid_t space_id);
```

### 4.5 Datatype (H5Tpublic.h)

```c
hid_t  H5Tcopy        (hid_t type_id);
hid_t  H5Tcreate      (H5T_class_t type, size_t size);
herr_t H5Tinsert      (hid_t parent_id, const char *name, size_t offset, hid_t member_id);
herr_t H5Tset_size    (hid_t type_id, size_t size);
size_t H5Tget_size    (hid_t type_id);
herr_t H5Tset_tag     (hid_t type, const char *tag);
herr_t H5Tset_strpad  (hid_t type_id, H5T_str_t strpad);
herr_t H5Tset_cset    (hid_t type_id, H5T_cset_t cset);
hid_t  H5Tvlen_create (hid_t base_id);
hid_t  H5Tarray_create2(hid_t base_id, unsigned ndims, const hsize_t dim[]);
herr_t H5Tclose       (hid_t type_id);
htri_t H5Tequal       (hid_t type1_id, hid_t type2_id);
```

### 4.6 Dataset (H5Dpublic.h)

```c
hid_t  H5Dcreate2  (hid_t loc_id, const char *name, hid_t type_id, hid_t space_id,
                    hid_t lcpl_id, hid_t dcpl_id, hid_t dapl_id);
hid_t  H5Dopen2    (hid_t loc_id, const char *name, hid_t dapl_id);
herr_t H5Dclose    (hid_t dset_id);
herr_t H5Dwrite    (hid_t dset_id, hid_t mem_type_id, hid_t mem_space_id,
                    hid_t file_space_id, hid_t dxpl_id, const void *buf);
herr_t H5Dread     (hid_t dset_id, hid_t mem_type_id, hid_t mem_space_id,
                    hid_t file_space_id, hid_t dxpl_id, void *buf);
herr_t H5Dset_extent(hid_t dset_id, const hsize_t size[]);
hid_t  H5Dget_space(hid_t dset_id);
hid_t  H5Dget_type (hid_t dset_id);
```

### 4.7 Attribute (H5Apublic.h)

```c
hid_t  H5Acreate2(hid_t loc_id, const char *attr_name, hid_t type_id, hid_t space_id,
                  hid_t acpl_id, hid_t aapl_id);
hid_t  H5Aopen   (hid_t obj_id, const char *attr_name, hid_t aapl_id);
herr_t H5Awrite  (hid_t attr_id, hid_t type_id, const void *buf);
herr_t H5Aread   (hid_t attr_id, hid_t type_id, void *buf);
herr_t H5Aclose  (hid_t attr_id);
```

### 4.8 Property List (H5Ppublic.h)

```c
hid_t  H5Pcreate          (hid_t cls_id);
herr_t H5Pclose           (hid_t plist_id);
herr_t H5Pset_chunk       (hid_t plist_id, int ndims, const hsize_t dim[]);
herr_t H5Pset_deflate     (hid_t plist_id, unsigned level);          // 0..9
herr_t H5Pset_shuffle     (hid_t plist_id);
herr_t H5Pset_fletcher32  (hid_t plist_id);
herr_t H5Pset_libver_bounds(hid_t plist_id, H5F_libver_t low, H5F_libver_t high);
herr_t H5Pset_fill_value  (hid_t plist_id, hid_t type_id, const void *value);
herr_t H5Pset_fill_time   (hid_t plist_id, H5D_fill_time_t fill_time);
herr_t H5Pset_layout      (hid_t plist_id, H5D_layout_t layout);
herr_t H5Pset_char_encoding(hid_t plist_id, H5T_cset_t encoding);    // for LCPL/UTF-8
```

### 4.9 Errors & links (H5Epublic.h, H5Lpublic.h)

```c
herr_t H5Eset_auto2(hid_t estack_id, H5E_auto2_t func, void *client_data);
htri_t H5Lexists   (hid_t loc_id, const char *name, hid_t lapl_id);
```

### 4.10 Delphi function-pointer pattern

For dynamic binding, each function is declared as a procedural type. Examples (apply the pattern to all functions):

```pascal
type
  TH5open            = function: herr_t; cdecl;
  TH5close           = function: herr_t; cdecl;
  TH5Fcreate         = function(filename: PAnsiChar; flags: Cardinal;
                                fcpl_id, fapl_id: hid_t): hid_t; cdecl;
  TH5Fclose          = function(file_id: hid_t): herr_t; cdecl;
  TH5Gcreate2        = function(loc_id: hid_t; name: PAnsiChar;
                                lcpl_id, gcpl_id, gapl_id: hid_t): hid_t; cdecl;
  TH5Screate_simple  = function(rank: Integer; const dims: Phsize_t;
                                const maxdims: Phsize_t): hid_t; cdecl;
  TH5Tcreate         = function(cls: Integer; size: NativeUInt): hid_t; cdecl;
  TH5Tinsert         = function(parent_id: hid_t; name: PAnsiChar;
                                offset: NativeUInt; member_id: hid_t): herr_t; cdecl;
  TH5Dcreate2        = function(loc_id: hid_t; name: PAnsiChar;
                                type_id, space_id, lcpl_id, dcpl_id, dapl_id: hid_t): hid_t; cdecl;
  TH5Dwrite          = function(dset_id, mem_type_id, mem_space_id,
                                file_space_id, dxpl_id: hid_t;
                                const buf: Pointer): herr_t; cdecl;
  TH5Pcreate         = function(cls_id: hid_t): hid_t; cdecl;
  TH5Pset_chunk      = function(plist_id: hid_t; ndims: Integer;
                                const dim: Phsize_t): herr_t; cdecl;
  TH5Acreate2        = function(loc_id: hid_t; attr_name: PAnsiChar;
                                type_id, space_id, acpl_id, aapl_id: hid_t): hid_t; cdecl;
  TH5Awrite          = function(attr_id, type_id: hid_t;
                                const buf: Pointer): herr_t; cdecl;
```

Note: array parameters such as `const hsize_t dims[]` are passed in Delphi as `Phsize_t` (pointer to the first element). Enum parameters (`H5T_class_t`, `H5S_class_t`, etc.) are `int`-wide in C — declare them as `Integer` in Delphi and keep the enum values as named integer constants.

---

## 5. HDF5 enums & constants

### 5.1 File access flags (parameter `flags` of H5Fcreate/H5Fopen)

| Constant | Value | Meaning |
|---|---|---|
| `H5F_ACC_RDONLY` | `0x0000` | read only |
| `H5F_ACC_RDWR` | `0x0001` | read/write |
| `H5F_ACC_TRUNC` | `0x0002` | create new, overwrite (use for export) |
| `H5F_ACC_EXCL` | `0x0004` | create new, error if it exists |
| `H5F_ACC_CREAT` | `0x0010` | create if not present |

### 5.2 `H5F_libver_t` (for H5Pset_libver_bounds)

| Constant | Value |
|---|---|
| `H5F_LIBVER_EARLIEST` | `0` |
| `H5F_LIBVER_V18` | `1` |
| `H5F_LIBVER_V110` | `2` |
| `H5F_LIBVER_V112` | `3` |
| `H5F_LIBVER_V114` | `4` |
| `H5F_LIBVER_V200` | `5` |
| `H5F_LIBVER_LATEST` | `5` (= currently highest value) |

Recommendation for the exporter: `H5Pset_libver_bounds(fapl, H5F_LIBVER_V110, H5F_LIBVER_LATEST)` — modern features, broad readability.

### 5.3 `H5F_scope_t`

`H5F_SCOPE_LOCAL = 0`, `H5F_SCOPE_GLOBAL = 1`.

### 5.4 `H5T_class_t` (parameter of H5Tcreate, classification)

| Constant | Value | | Constant | Value |
|---|---|---|---|---|
| `H5T_NO_CLASS` | `-1` | | `H5T_OPAQUE` | `5` |
| `H5T_INTEGER` | `0` | | `H5T_COMPOUND` | `6` |
| `H5T_FLOAT` | `1` | | `H5T_REFERENCE` | `7` |
| `H5T_TIME` | `2` | | `H5T_ENUM` | `8` |
| `H5T_STRING` | `3` | | `H5T_VLEN` | `9` |
| `H5T_BITFIELD` | `4` | | `H5T_ARRAY` | `10` |
| | | | `H5T_COMPLEX` | `11` |

Practically relevant for H5Tcreate: `H5T_COMPOUND` (6), `H5T_OPAQUE` (5), `H5T_STRING` (3).

### 5.5 `H5T_str_t` (string padding)

`H5T_STR_NULLTERM = 0`, `H5T_STR_NULLPAD = 1`, `H5T_STR_SPACEPAD = 2`.

### 5.6 `H5T_cset_t` (character set)

`H5T_CSET_ASCII = 0`, `H5T_CSET_UTF8 = 1`.

### 5.7 `H5S_class_t` (parameter of H5Screate)

`H5S_SCALAR = 0`, `H5S_SIMPLE = 1`, `H5S_NULL = 2`.

### 5.8 `H5S_seloper_t` (hyperslab selection)

`H5S_SELECT_SET = 0` (the only relevant value for append), `H5S_SELECT_OR = 1`, ...

### 5.9 `H5D_layout_t`

`H5D_COMPACT = 0`, `H5D_CONTIGUOUS = 1`, `H5D_CHUNKED = 2`, `H5D_VIRTUAL = 3`.

### 5.10 `H5D_fill_time_t`

`H5D_FILL_TIME_ALLOC = 0`, `H5D_FILL_TIME_NEVER = 1`, `H5D_FILL_TIME_IFSET = 2`.

### 5.11 Special constants

| Constant | Value | Use |
|---|---|---|
| `H5P_DEFAULT` | `0` (as `hid_t`) | default property list everywhere |
| `H5S_ALL` | `0` (as `hid_t`) | "entire dataspace" in H5Dwrite/H5Dread |
| `H5S_UNLIMITED` | `HSIZE_UNDEF` = `0xFFFFFFFFFFFFFFFF` (UInt64 maximum) | unlimited max dimension |
| `H5T_VARIABLE` | `SIZE_MAX` = `0xFFFFFFFFFFFFFFFF` (NativeUInt maximum, Win64) | variable string length in H5Tset_size |
| `H5E_DEFAULT` | `0` (as `hid_t`) | default error stack |

Delphi definition:
```pascal
const
  H5P_DEFAULT   : hid_t   = 0;
  H5S_ALL       : hid_t   = 0;
  H5E_DEFAULT   : hid_t   = 0;
  H5S_UNLIMITED : hsize_t = High(UInt64);
  H5T_VARIABLE  : NativeUInt = High(NativeUInt);
```

---

## 6. List of `_g` symbols (fetch from the DLL at runtime)

These symbols are exported global variables of type `hid_t`. Read them after `H5open()` (see 3.3 / 3.4).

### 6.1 Standard storage types (fixed bit width, little-endian for export)

```
H5T_STD_I8LE_g   H5T_STD_I16LE_g   H5T_STD_I32LE_g   H5T_STD_I64LE_g
H5T_STD_U8LE_g   H5T_STD_U16LE_g   H5T_STD_U32LE_g   H5T_STD_U64LE_g
H5T_STD_I8BE_g   ... (BE variants also exist, not needed here)
H5T_IEEE_F32LE_g  H5T_IEEE_F64LE_g
H5T_C_S1_g        (base for string types)
```

### 6.2 Native (memory) types — for the `mem_type_id` parameter of H5Dwrite/H5Awrite

```
H5T_NATIVE_SCHAR_g   H5T_NATIVE_UCHAR_g
H5T_NATIVE_SHORT_g   H5T_NATIVE_USHORT_g
H5T_NATIVE_INT_g     H5T_NATIVE_UINT_g
H5T_NATIVE_LLONG_g   H5T_NATIVE_ULLONG_g
H5T_NATIVE_FLOAT_g   H5T_NATIVE_DOUBLE_g
H5T_NATIVE_INT8_g    H5T_NATIVE_UINT8_g
H5T_NATIVE_INT16_g   H5T_NATIVE_UINT16_g
H5T_NATIVE_INT32_g   H5T_NATIVE_UINT32_g
H5T_NATIVE_INT64_g   H5T_NATIVE_UINT64_g
```

Note: the `H5T_NATIVE_INT8_g` family (with explicit bit width) is the most unambiguous — it corresponds directly to Delphi's `ShortInt/Byte/SmallInt/Word/Integer/Cardinal/Int64/UInt64`.

### 6.3 Property list class IDs — for the `cls_id` parameter of H5Pcreate

```
H5P_CLS_FILE_CREATE_ID_g       → corresponds to macro H5P_FILE_CREATE
H5P_CLS_FILE_ACCESS_ID_g       → H5P_FILE_ACCESS
H5P_CLS_DATASET_CREATE_ID_g    → H5P_DATASET_CREATE
H5P_CLS_DATASET_ACCESS_ID_g    → H5P_DATASET_ACCESS
H5P_CLS_DATASET_XFER_ID_g      → H5P_DATASET_XFER
H5P_CLS_GROUP_CREATE_ID_g      → H5P_GROUP_CREATE
H5P_CLS_LINK_CREATE_ID_g       → H5P_LINK_CREATE
H5P_CLS_ATTRIBUTE_CREATE_ID_g  → H5P_ATTRIBUTE_CREATE
```

**Be sure to note:** `H5Pcreate(H5P_FILE_ACCESS)` means in practice: first read `H5P_CLS_FILE_ACCESS_ID_g`, then pass that value to `H5Pcreate`.

---

## 7. HDF5 data model (compact)

| Concept | Meaning |
|---|---|
| **File** | Container. Has a root group `/`. Format version controllable via FAPL/`libver_bounds`. |
| **Group** | Hierarchical "folder". Paths like `/Sensor/Motor`. Nestable. |
| **Dataset** | N-dimensional array. Consists of: datatype + dataspace + storage layout + data. |
| **Attribute** | Small metadata (name+type+value), attached to a group or dataset. Not extensible, not compressible. |
| **Datatype** | Type description. Atomic (integer/float/string) or compound (struct). |
| **Dataspace** | Shape of an array: rank + current dimensions + max dimensions. `H5S_SCALAR` for a single value. |
| **Property List** | Configuration object. FCPL (file create), FAPL (file access), DCPL (dataset create), LCPL (link create), etc. |

Important: HDF5 has **no** time-series concept. "Time" must itself be modeled as an ordinary dataset or as a compound field.

---

## 8. HDF5 API workflows (call sequences)

All sequences assume: DLL loaded, function pointers bound, `H5open()` called, `_g` variables read, `H5Eset_auto2(0, nil, nil)` set.

### 8.1 Create a file with the modern format

```
fapl  := H5Pcreate(H5P_CLS_FILE_ACCESS_ID_g)
H5Pset_libver_bounds(fapl, H5F_LIBVER_V110, H5F_LIBVER_LATEST)
fileId := H5Fcreate('out.h5', H5F_ACC_TRUNC, H5P_DEFAULT, fapl)
H5Pclose(fapl)
...
H5Fclose(fileId)
```

### 8.2 Create a group hierarchy (with UTF-8 names)

```
lcpl := H5Pcreate(H5P_CLS_LINK_CREATE_ID_g)
H5Pset_char_encoding(lcpl, H5T_CSET_UTF8)
grpId := H5Gcreate2(fileId, '/Motor', lcpl, H5P_DEFAULT, H5P_DEFAULT)
... subordinate groups analogously ...
H5Gclose(grpId)
H5Pclose(lcpl)
```
Note: groups must be created level by level if the intermediate levels do not exist — or set `H5Pset_create_intermediate_group(lcpl, 1)`, in which case HDF5 creates missing intermediate groups automatically.

### 8.3 Define a compound datatype `{int64 timestamp_ns; double value}`

Delphi record (packed!):
```pascal
type
  TSampleRec = packed record
    TimestampNs: Int64;   // offset 0
    Value:       Double;  // offset 8
  end;                    // SizeOf = 16
```
HDF5 calls:
```
ct := H5Tcreate(H5T_COMPOUND, SizeOf(TSampleRec))           // = 16
H5Tinsert(ct, 'timestamp_ns', 0, H5T_STD_I64LE_g)            // storage type
H5Tinsert(ct, 'value',        8, H5T_IEEE_F64LE_g)
```
The offsets (0, 8) must exactly match the record field offsets. `packed record` guarantees that. When writing, the same compound type is used as `mem_type_id` — HDF5 converts storage↔memory automatically.

### 8.4 Create an extensible (chunked) 1D dataset

```
dims[0]    := 0                       // start: empty
maxdims[0] := H5S_UNLIMITED
space := H5Screate_simple(1, @dims[0], @maxdims[0])

dcpl := H5Pcreate(H5P_CLS_DATASET_CREATE_ID_g)
chunk[0] := 8192
H5Pset_chunk(dcpl, 1, @chunk[0])       // chunking MANDATORY for UNLIMITED
H5Pset_shuffle(dcpl)                   // optional, better compression
H5Pset_deflate(dcpl, 4)                // GZIP level 4

dsetId := H5Dcreate2(grpId, 'Temperatur', ct, space,
                     H5P_DEFAULT, dcpl, H5P_DEFAULT)
H5Pclose(dcpl)
H5Sclose(space)
```

### 8.5 Append a block to the dataset (append via hyperslab)

Per OSF block with N new samples:
```
// 1. Enlarge the dataset
newSize[0] := oldCount + N
H5Dset_extent(dsetId, @newSize[0])

// 2. Get the file dataspace and select the target region
fileSpace := H5Dget_space(dsetId)
start[0] := oldCount;  count[0] := N
H5Sselect_hyperslab(fileSpace, H5S_SELECT_SET, @start[0], nil, @count[0], nil)

// 3. Memory dataspace for the write buffer
memSpace := H5Screate_simple(1, @count[0], nil)

// 4. Write (buf = pointer to Array[0..N-1] of TSampleRec)
H5Dwrite(dsetId, ct, memSpace, fileSpace, H5P_DEFAULT, @buf[0])

H5Sclose(memSpace)
H5Sclose(fileSpace)
oldCount := oldCount + N
```

### 8.6 Write a string attribute (e.g. `units = "°C"`)

```
strType := H5Tcopy(H5T_C_S1_g)
H5Tset_size(strType, H5T_VARIABLE)       // variable length
H5Tset_cset(strType, H5T_CSET_UTF8)
aSpace := H5Screate(H5S_SCALAR)
attrId := H5Acreate2(dsetId, 'units', strType, aSpace, H5P_DEFAULT, H5P_DEFAULT)

// For variable length, H5Awrite expects a pointer to a char* pointer:
valuePtr := PAnsiChar(UTF8String('°C'))
H5Awrite(attrId, strType, @valuePtr)

H5Aclose(attrId);  H5Sclose(aSpace);  H5Tclose(strType)
```

### 8.7 Write a numeric attribute (e.g. `sample_period_ns = 1000000`)

```
aSpace := H5Screate(H5S_SCALAR)
attrId := H5Acreate2(dsetId, 'sample_period_ns', H5T_STD_I64LE_g, aSpace,
                     H5P_DEFAULT, H5P_DEFAULT)
val : Int64 := 1000000
H5Awrite(attrId, H5T_NATIVE_INT64_g, @val)
H5Aclose(attrId);  H5Sclose(aSpace)
```

### 8.8 Cleanup rule

Every `H5*create*`/`H5*open*`/`H5Pcreate`/`H5Screate*`/`H5Tcopy`/`H5Tcreate` that returns a positive `hid_t` must be closed with the matching `H5*close`. In Delphi, consistently guard this with `try..finally`. Order when closing: first attributes/datasets, then groups, the file last.

---

## 9. OSF format (compact reference for the read side)

OSF (Open Streaming Format) is a binary, block-oriented streaming format for time-related data. This side is read **natively in Delphi**.

### 9.1 File layout

```
[Magic Header]  ASCII line
[Metablock]     XML (OSF4) or JSON (OSF5)
[Data block 1]
[Data block 2]
...
[Data block N]
[optional: info/trailer block with channel index 0xFFFF]
[optional: Magic Trailer]
```

### 9.2 Magic Header

ASCII line, terminated with `\n` (0x0A):
```
<IDENTIFIER> <metablock_length>\n
```
- **IDENTIFIER:** `OSF4`, `OCEAN_STREAM_FORMAT4`, `OCEAN_STREAMING_FORMAT4` (all version 4) or `OSF5` (version 5).
- **metablock_length:** decimal number, size of the following metablock in bytes.

### 9.3 OSFZ compression (file level)

An OSF file can be gzip- or zlib-compressed (`.osfz`). Detection from the first bytes:
- `0x1F 0x8B` → gzip
- `0x78 0x01 / 0x78 0x5E / 0x78 0x9C / 0x78 0xDA` → zlib
- otherwise → uncompressed (the `OSF...` text begins with `0x4F`)

The exporter must decompress transparently (Delphi: `System.ZLib`).

### 9.4 Metablock

Contains all channel definitions and file metadata. OSF4 as XML, OSF5 as JSON. Fields:

**File info (global):** `created_utc` (ISO 8601), `creator`, `created_at_latitude/longitude/altitude`, `reason`, `namespacesep` (separator character in channel names), `tag`, `comment`.

**Per channel:** `index` (u16, 0..N-1), `name` (hierarchical, separated by `namespacesep`), `channeltype` (`scalar`/`vector`/`matrix`), `datatype` (see 9.6), `timeincrement` (ns; 0 = timestamped), `sizeoflengthvalue` (2 or 4), `physicalunit`, `physicaldimension`, `displayname`, `comment`, `reference` (UUID), `mimetype` (for binary).

**Infos[]:** additional free-form key-value metadata (`name`, `datatype`, `value`).

### 9.5 Data blocks

Each block:
```
[Channel index]  uint16 LE
[Length]         uint16 LE  OR  uint32 LE   (depending on the channel's sizeoflengthvalue)
[ControlByte]    uint8
[Payload]        <Length> bytes
```

**ControlByte:** bit 7 = multi-sample flag (1 = multiple samples, then the payload begins with `uint32 N`; 0 = a single sample). Bits 0–6 = block type:

| Type | Name | Payload | Status |
|---|---|---|---|
| 5 | `bcContinuedData` | `[u32 N]` + N×value | active |
| 6 | `bcStartData` | `int64 start_time_ns` + `double rate_hz` + `[u32 N]` + N×value | active |
| 7 | `bcContinuedRelStampData` | `[u32 N]` + N×(`u32 delta_ns` + value) | OSF4, OSF5 read-only |
| 8 | `bcAbsTimeStampData` | `[u32 N]` + N×(`int64 time_ns` + value) | active |
| 0–4 | reserved/deprecated | — | skip + warn |

`[u32 N]` is only present when bit 7 is set; otherwise N = 1.

**Time reconstruction:**
- `bcStartData` opens a segment with an absolute start time and sample rate. Sample k: `time = start_time_ns + round(k * 1e9 / rate_hz)`.
- `bcContinuedData` continues the segment; time is extrapolated using the last seen rate.
- `bcAbsTimeStampData` carries its own int64 timestamp per sample.
- Multiple `bcStartData` of the same channel = multiple segments (drift correction/trigger).

### 9.6 OSF data types

`bool` (1 B), `int8/16/32/64`, `uint8/16/32/64` (all LE), `float` (IEEE 4 B), `double` (IEEE 8 B), `string` (UTF-8, **null-terminated**), `binary` (raw bytes, **null-terminated**), `gpslocation` (3×double = lat, lon, alt). All timestamps: `int64` nanoseconds since the Unix epoch.

### 9.7 Trailer

Channel index `0xFFFF` marks an info/trailer block at the end of the file: per-channel statistics (`samples`, `last_ns`) and `finalized_utc`/`reason`. Missing in the case of an aborted recording. An optional Magic Trailer `OSF_STREAM_END <position>===` (40 bytes) allows fast random access.

### 9.8 Robustness

OSF is append-only and valid at any time. With a truncated file, the reader must read up to the last **complete** block and stop cleanly (no crash).

---

## 10. Mapping OSF → HDF5

| OSF | HDF5 |
|---|---|
| File | HDF5 file |
| Magic Header version | root attribute `osf_version` |
| File-info fields | root attributes (`created_utc`, `creator`, `geo_lat/lon/alt`, `reason`, `namespace_separator`, `tag`, `comment`) |
| `Infos[]` | group `/info` with attributes |
| Channel `Motor.Block.Temp` (separator `.`) | group path `/Motor/Block` + dataset `Temp` |
| Channel data (scalar) | 1D dataset, compound `{int64 timestamp_ns; T value}` |
| Channel data (vector) | 2D dataset `[N × length]` + parallel timestamp dataset |
| Channel data (matrix) | 3D dataset `[N × rows × columns]` + parallel timestamp dataset |
| Channel metadata | dataset attributes: `osf_channel_index`, `osf_channel_type`, `osf_datatype`, `sample_period_ns`, `units`, `physical_dimension`, `long_name`, `comment`, `reference_uuid`, `mime_type` |
| Trailer statistics | root/dataset attributes `finalized_utc`, `finalized_reason`, `sample_count`, `last_timestamp_ns` |

### OSF data type → HDF5 type

| OSF | HDF5 storage type (`_g`) | HDF5 memory type (`_g`) |
|---|---|---|
| `bool`, `uint8` | `H5T_STD_U8LE_g` | `H5T_NATIVE_UINT8_g` |
| `int8` | `H5T_STD_I8LE_g` | `H5T_NATIVE_INT8_g` |
| `int16` / `uint16` | `H5T_STD_I16LE_g` / `H5T_STD_U16LE_g` | `H5T_NATIVE_INT16_g` / `H5T_NATIVE_UINT16_g` |
| `int32` / `uint32` | `H5T_STD_I32LE_g` / `H5T_STD_U32LE_g` | `H5T_NATIVE_INT32_g` / `H5T_NATIVE_UINT32_g` |
| `int64` / `uint64` | `H5T_STD_I64LE_g` / `H5T_STD_U64LE_g` | `H5T_NATIVE_INT64_g` / `H5T_NATIVE_UINT64_g` |
| `float` | `H5T_IEEE_F32LE_g` | `H5T_NATIVE_FLOAT_g` |
| `double` | `H5T_IEEE_F64LE_g` | `H5T_NATIVE_DOUBLE_g` |
| `string` | `H5Tcopy(H5T_C_S1_g)` + `H5Tset_size(VARIABLE)` + UTF-8 | likewise |
| `binary` | `H5T_OPAQUE` (tag = mimetype) or VLEN bytes | likewise |
| `gpslocation` | compound `{double lat; double lon; double alt}` | likewise |

### Losslessness

OSF→HDF5 is lossless, provided OSF-specific fields are carried along as `osf_*` attributes. Recommendation: one attribute `osf_was_equidistant` (boolean) per channel, so it remains possible to distinguish later whether the timestamps were equidistant or genuinely timestamped. Multiple `bcStartData` segments are flatly concatenated; if the segment boundaries are to be preserved, create an additional small dataset `<channel>__segments` with `[start_index, start_ns, rate_hz]` per segment.

---

## 11. Recommended exporter architecture (Delphi)

Unit breakdown that Claude Code should implement:

| Unit | Responsibility |
|---|---|
| `Hdf5.Api.pas` | Dynamic DLL binding: function-pointer variables, `_g` variables, `LoadHdf5`/`UnloadHdf5`, `H5open` call. |
| `Hdf5.Wrapper.pas` | Idiomatic classes `THdf5File`, `THdf5Group`, `THdf5Dataset`, `THdf5Attribute`, `THdf5Datatype` with RAII-style handle lifecycle (destructor closes the handle), error checking → `EHdf5Exception`. |
| `Osf.Types.pas` | Enums/records: `TOsfDataType`, `TOsfChannelType`, ControlByte decoding. |
| `Osf.Reader.pas` | Read the OSF file: Magic Header, metablock (XML/JSON), block iterator, OSFZ decompression. |
| `Osf.Meta.pas` | Data model `TOsfMetaBlock`, `TOsfChannel`, `TOsfFileInfo`. |
| `Export.Engine.pas` | Orchestration: read OSF → mapping → write HDF5. Keeps a dataset handle + write state per channel (last rate, sample counter). |
| `osf2hdf5.dpr` | Console CLI: `osf2hdf5 <input> [output] [--deflate N] [--chunk N] [--no-shuffle]`. |

**Streaming principle:** Do not load the entire OSF file into RAM. Keep a small sample buffer per channel (e.g. 8192 entries); when the threshold is reached, flush it to the HDF5 dataset via the append sequence (8.5), and drain all buffers at the end.

**Standard libraries:** `System.JSON` (OSF5 metablock), `Xml.XMLDoc` (OSF4 metablock), `System.ZLib` (OSFZ). No external packages needed.

---

## 12. Pitfalls (checklist)

1. **`cdecl` forgotten** → stack corruption / crash. Every HDF5 function is `cdecl`.
2. **`H5open()` not called** → all `_g` variables are 0, every call fails.
3. **`_g` variables read before `H5open()`** → stale/invalid values.
4. **`hid_t` declared as 32-bit** → wrong; `hid_t` has been `Int64` since HDF5 1.10.
5. **Delphi `string` passed directly** → HDF5 expects UTF-8/`PAnsiChar`, not UTF-16. Always go through `UTF8String`.
6. **Compound record not `packed`** → field offsets do not match the `H5Tinsert` offsets → corrupt data.
7. **Handles not closed** → memory leaks; handle exhaustion with very many channels. `try..finally` for every `hid_t`.
8. **Chunking forgotten with `H5S_UNLIMITED`** → `H5Dcreate2` fails. An unlimited dimension requires `H5Pset_chunk`.
9. **32/64-bit mismatch** → Delphi Win64 needs the Win64 `hdf5.dll`. Never mix.
10. **Variable-length string written incorrectly** → `H5Awrite`/`H5Dwrite` expects for VLEN strings a pointer to a `PAnsiChar` (i.e. `char**`), not the string directly.
11. **Missing dependent DLLs** → `zlib.dll` must be present as soon as `H5Pset_deflate` is used.
12. **Error autoprint is disruptive** → call `H5Eset_auto2(0, nil, nil)` right after `H5open()`; check errors via the return values.

---

## 13. Notes for the Claude Code instruction

Claude Chat should shape the final instruction to Claude Code so that it contains at least:

- **Goal definition:** console tool `osf2hdf5.exe` + reusable library, Delphi Win64.
- **Mandatory order:** first implement `Hdf5.Api.pas` (DLL binding) and verify it with a minimal test (`H5open` + print the version via `H5get_libversion`), BEFORE anything else is built. The DLL binding is risk number one.
- **DLL binding strictly per section 3** of this document: `cdecl`, `hid_t = Int64`, read the `_g` variables after `H5open()`.
- **HDF5 feature scope:** chunked + `H5S_UNLIMITED` + `H5Pset_deflate(4)` + `H5Pset_shuffle` (standard), `libver_bounds = V110..LATEST`.
- **Data layout:** compound `{int64 timestamp_ns; T value}` per scalar channel (strategy from section 10).
- **OSF read side native** in Delphi per section 9 (including OSFZ decompression, truncation tolerance).
- **Mapping** exactly per section 10, including the `osf_*` attribute convention.
- **Architecture** per section 11 (unit breakdown, streaming with a per-channel buffer).
- **Coding standard:** Delphi conventions (T-/F-/I- prefixes, `try..finally` for every handle, `UTF8String` at all external interfaces).
- **Tests:** DUnitX — DLL-binding smoke test, OSF parser unit tests, one integration test that converts a small OSF file and reads the result back with `hdf5.dll` and compares values.
- **Pitfalls** from section 12 explicitly carried over into the instruction as a warning list.
- **Open points the user must provide to Claude Code:** the concrete HDF5 DLL version and path, the Delphi version, the target folder/repository, and optionally example OSF files for testing.

---

**End of the knowledge base.** This document contains all the format and binding information needed to formulate a complete Claude Code instruction for the Delphi OSF→HDF5 exporter.
