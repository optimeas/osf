# HDF5 DLL Binding — Knowledge Base

Everything needed to call the official HDF5 C library (`hdf5.dll`) from
Delphi: C→Delphi type mapping, the `_g`-variable mechanism, the
mandatory initialisation order, the API signatures the exporter uses,
and the enum/constant values.

The hard part of an OSF→HDF5 exporter is **not** the HDF5 format — the
DLL handles all of that — it is the **correct DLL binding**. Most
failures of hand-written HDF5 bindings come from the pitfalls in
sections 2 and 3 below.

This knowledge base is valid for **HDF5 ≥ 1.10** (where `hid_t` became a
64-bit integer). The OSF exporter pins **HDF5 1.14.4-3**.

---

## 1. Basic types — C → Delphi

| C type (HDF5) | C definition | Delphi type | Bytes |
|---|---|---|---|
| `hid_t` | `int64_t` | `Int64` | 8 |
| `herr_t` | `int` | `Integer` | 4 |
| `hsize_t` | `uint64_t` | `UInt64` | 8 |
| `hssize_t` | `int64_t` | `Int64` | 8 |
| `haddr_t` | `uint64_t` | `UInt64` | 8 |
| `htri_t` | `int` (tri-state: >0 true, 0 false, <0 error) | `Integer` | 4 |
| `hbool_t` | `bool` (C99) | `ByteBool` | 1 |
| `size_t` | platform-dependent | `NativeUInt` | 8 / 4 |
| `unsigned` | `unsigned int` | `Cardinal` | 4 |
| `const char *` | C string | `PAnsiChar` (UTF-8) | pointer |
| `void *` | any pointer | `Pointer` | pointer |

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

`hid_t` is a **64-bit integer** since HDF5 1.10. Never declare it as
32-bit.

---

## 2. Calling convention, DLL name, symbols

- **DLL:** `hdf5.dll` (Release build of the official distribution).
- **Calling convention:** every function is declared **`cdecl`** — also
  on Win64. Delphi's default `register` corrupts the stack.
- **Symbol names:** plain C names, no mangling, no `_` prefix or `@n`
  suffix. `GetProcAddress(h, 'H5Fcreate')` returns the function pointer
  directly.
- **Architecture:** a Delphi Win64 build needs the Win64 `hdf5.dll`.
  Never mix 32/64-bit.

---

## 3. The `_g`-variable mechanism — the key pitfall

In C, predefined datatypes such as `H5T_NATIVE_INT` or `H5T_STD_I32LE`
are **not constants** — they are preprocessor macros pointing at
**global variables exported by the DLL**:

```c
#define H5T_NATIVE_INT (H5OPEN H5T_NATIVE_INT_g)
```

`H5T_NATIVE_INT_g` is a real global `hid_t` variable in the DLL's data
segment. There are **no fixed numeric values** — the values must be read
from the DLL at run time:

```pascal
function GetH5Var(aHandle: HMODULE; const aName: AnsiString): hid_t;
var
  P: Phid_t;
begin
  P := Phid_t(GetProcAddress(aHandle, PAnsiChar(aName)));
  if P = nil then
    raise EHdf5DllNotLoaded.CreateFmt('HDF5 symbol not found: %s', [aName]);
  Result := P^;   // dereference the global variable
end;
```

The same applies to the **property-list class IDs**
(`H5P_FILE_ACCESS`, `H5P_DATASET_CREATE`, …) — they are `_g` variables
too (`H5P_CLS_FILE_ACCESS_ID_g` etc.).

### 3.1 Mandatory initialisation order

```
1. LoadLibrary('hdf5.dll')
2. resolve every function pointer via GetProcAddress
3. call H5open()                       — exactly once, before anything else
4. call H5Eset_auto2(H5E_DEFAULT, nil, nil)   — silence the auto error print
5. ONLY NOW read the _g variables
   (H5T_NATIVE_*_g, H5T_STD_*_g, H5T_IEEE_*_g, H5P_CLS_*_g, H5T_C_S1_g)
6. ... real work ...
7. H5close()       at program end
8. FreeLibrary
```

If step 3 is skipped, or step 5 is done before step 3, the `_g`
variables contain `0` or garbage and every subsequent API call fails.
**This is the single most common error in hand-written HDF5 bindings.**

---

## 4. String handling & UTF-8

- HDF5 expects **null-terminated UTF-8 strings** (`const char *`). In
  Delphi: `UTF8String` → `PAnsiChar`.
- Delphi `string` (UTF-16) must be converted (`UTF8String` cast) before
  every call.
- For HDF5 **object names** (groups, datasets, attributes) to be treated
  as UTF-8, set `H5Pset_char_encoding(lcpl, H5T_CSET_UTF8)` on the Link
  Creation Property List. Without it, names are treated as ASCII —
  harmless for pure-ASCII names, wrong for umlauts in channel names.
- For HDF5 **string datatypes**, set `H5Tset_cset(typeId, H5T_CSET_UTF8)`.

### 4.1 Writing a variable-length string

`H5Awrite` / `H5Dwrite` for a VLEN string expects a pointer **to** a
`PAnsiChar` (i.e. `char**`), not the string itself:

```pascal
var
  LUtf8: UTF8String;
  LPtr:  PAnsiChar;
begin
  LUtf8 := UTF8String('°C');
  LPtr  := PAnsiChar(LUtf8);
  H5Awrite(attrId, strType, @LPtr);   // pointer TO the pointer
end;
```

---

## 5. Error handling

- `hid_t`-returning functions: negative = error.
- `herr_t`-returning functions: negative = error, 0/positive = OK.
- `htri_t`-returning functions: >0 true, 0 false, <0 error.
- HDF5 otherwise prints an error stack to `stderr`. `H5Eprint2` wants a
  C `FILE*`, awkward from Delphi. **Disable the auto-print** right after
  `H5open()` with `H5Eset_auto2(0, nil, nil)` (`H5E_DEFAULT = 0`), and
  check return values instead.
- Wrap every call: `CheckH5(result, funcName)` and
  `CheckH5Id(id, funcName): hid_t` raise `EHdf5ApiError` on a negative
  result.

Exception hierarchy:

```
EHdf5Exception
  EHdf5ApiError       — a negative return value from an HDF5 call
  EHdf5DllNotLoaded   — hdf5.dll could not be loaded (lists the search paths)
```

---

## 6. API function signatures

All `cdecl`. The subset the exporter needs.

**Initialisation**

```c
herr_t H5open(void);
herr_t H5close(void);
herr_t H5get_libversion(unsigned *majnum, unsigned *minnum, unsigned *relnum);
herr_t H5Eset_auto2(hid_t estack_id, void *func, void *client_data);
```

**File**

```c
hid_t  H5Fcreate(const char *filename, unsigned flags, hid_t fcpl_id, hid_t fapl_id);
herr_t H5Fclose (hid_t file_id);
herr_t H5Fflush (hid_t object_id, int scope);
```

**Group**

```c
hid_t  H5Gcreate2(hid_t loc_id, const char *name, hid_t lcpl_id, hid_t gcpl_id, hid_t gapl_id);
herr_t H5Gclose  (hid_t group_id);
htri_t H5Lexists (hid_t loc_id, const char *name, hid_t lapl_id);
```

**Dataspace**

```c
hid_t  H5Screate(int type);
hid_t  H5Screate_simple(int rank, const hsize_t dims[], const hsize_t maxdims[]);
herr_t H5Sclose(hid_t space_id);
herr_t H5Sselect_hyperslab(hid_t space_id, int op,
                           const hsize_t start[], const hsize_t stride[],
                           const hsize_t count[], const hsize_t block[]);
```

**Datatype**

```c
hid_t  H5Tcopy(hid_t type_id);
hid_t  H5Tcreate(int type, size_t size);
herr_t H5Tinsert(hid_t parent_id, const char *name, size_t offset, hid_t member_id);
herr_t H5Tset_size(hid_t type_id, size_t size);
herr_t H5Tset_tag(hid_t type, const char *tag);
herr_t H5Tset_cset(hid_t type_id, int cset);
herr_t H5Tclose(hid_t type_id);
```

**Dataset**

```c
hid_t  H5Dcreate2(hid_t loc_id, const char *name, hid_t type_id, hid_t space_id,
                  hid_t lcpl_id, hid_t dcpl_id, hid_t dapl_id);
herr_t H5Dclose(hid_t dset_id);
herr_t H5Dwrite(hid_t dset_id, hid_t mem_type_id, hid_t mem_space_id,
                hid_t file_space_id, hid_t dxpl_id, const void *buf);
herr_t H5Dset_extent(hid_t dset_id, const hsize_t size[]);
hid_t  H5Dget_space(hid_t dset_id);
```

**Attribute**

```c
hid_t  H5Acreate2(hid_t loc_id, const char *attr_name, hid_t type_id, hid_t space_id,
                  hid_t acpl_id, hid_t aapl_id);
herr_t H5Awrite(hid_t attr_id, hid_t type_id, const void *buf);
herr_t H5Aclose(hid_t attr_id);
```

**Property list**

```c
hid_t  H5Pcreate(hid_t cls_id);
herr_t H5Pclose(hid_t plist_id);
herr_t H5Pset_chunk(hid_t plist_id, int ndims, const hsize_t dim[]);
herr_t H5Pset_deflate(hid_t plist_id, unsigned level);          // 0..9
herr_t H5Pset_shuffle(hid_t plist_id);
herr_t H5Pset_libver_bounds(hid_t plist_id, int low, int high);
herr_t H5Pset_char_encoding(hid_t plist_id, int encoding);
herr_t H5Pset_create_intermediate_group(hid_t plist_id, unsigned crt_intmd);
```

### 6.1 Delphi function-pointer pattern

```pascal
type
  TH5open           = function: herr_t; cdecl;
  TH5get_libversion = function(majnum, minnum, relnum: PCardinal): herr_t; cdecl;
  TH5Fcreate        = function(filename: PAnsiChar; flags: Cardinal;
                               fcpl_id, fapl_id: hid_t): hid_t; cdecl;
  TH5Gcreate2       = function(loc_id: hid_t; name: PAnsiChar;
                               lcpl_id, gcpl_id, gapl_id: hid_t): hid_t; cdecl;
  TH5Screate_simple = function(rank: Integer; const dims: Phsize_t;
                               const maxdims: Phsize_t): hid_t; cdecl;
  TH5Tcreate        = function(cls: Integer; size: NativeUInt): hid_t; cdecl;
  TH5Tinsert        = function(parent_id: hid_t; name: PAnsiChar;
                               offset: NativeUInt; member_id: hid_t): herr_t; cdecl;
  TH5Dcreate2       = function(loc_id: hid_t; name: PAnsiChar;
                               type_id, space_id, lcpl_id, dcpl_id,
                               dapl_id: hid_t): hid_t; cdecl;
  TH5Dwrite         = function(dset_id, mem_type_id, mem_space_id,
                               file_space_id, dxpl_id: hid_t;
                               const buf: Pointer): herr_t; cdecl;
  TH5Pset_chunk     = function(plist_id: hid_t; ndims: Integer;
                               const dim: Phsize_t): herr_t; cdecl;
  TH5Pset_deflate   = function(plist_id: hid_t; level: Cardinal): herr_t; cdecl;
  // ... remaining functions analogous
```

Array parameters (`const hsize_t dims[]`) are passed as `Phsize_t` (a
pointer to the first element). Enum parameters are `int`-wide in C — in
Delphi declare them `Integer` and keep the enum values as named integer
constants.

---

## 7. Enum values and constants

**File access flags** (`H5Fcreate`/`H5Fopen` `flags`)

| Constant | Value |
|---|---|
| `H5F_ACC_RDONLY` | `0x0000` |
| `H5F_ACC_RDWR` | `0x0001` |
| `H5F_ACC_TRUNC` | `0x0002` |
| `H5F_ACC_EXCL` | `0x0004` |

**`H5F_libver_t`** (`H5Pset_libver_bounds`)

| Constant | Value |
|---|---|
| `H5F_LIBVER_EARLIEST` | 0 |
| `H5F_LIBVER_V18` | 1 |
| `H5F_LIBVER_V110` | 2 |
| `H5F_LIBVER_V112` | 3 |
| `H5F_LIBVER_V114` | 4 |
| `H5F_LIBVER_LATEST` | currently highest (= 4 in 1.14.x) |

Use `H5Pset_libver_bounds(fapl, H5F_LIBVER_V110, H5F_LIBVER_LATEST)`.

**`H5T_class_t`** (`H5Tcreate`)

| Constant | Value |
|---|---|
| `H5T_INTEGER` | 0 |
| `H5T_FLOAT` | 1 |
| `H5T_STRING` | 3 |
| `H5T_OPAQUE` | 5 |
| `H5T_COMPOUND` | 6 |

**`H5T_cset_t`:** `H5T_CSET_ASCII = 0`, `H5T_CSET_UTF8 = 1`

**`H5S_class_t`:** `H5S_SCALAR = 0`, `H5S_SIMPLE = 1`, `H5S_NULL = 2`

**`H5S_seloper_t`:** `H5S_SELECT_SET = 0`

**`H5F_scope_t`:** `H5F_SCOPE_LOCAL = 0`, `H5F_SCOPE_GLOBAL = 1`

**Special constants**

```pascal
const
  H5P_DEFAULT   : hid_t      = 0;
  H5S_ALL       : hid_t      = 0;
  H5E_DEFAULT   : hid_t      = 0;
  H5S_UNLIMITED : hsize_t    = High(UInt64);
  H5T_VARIABLE  : NativeUInt = High(NativeUInt);
```

---

## 8. `_g` symbols to resolve at run time

Resolve these **after `H5open()`**. They are exported global `hid_t`
variables; read them with `GetH5Var` (section 3).

**Standard storage types (little-endian)**

```
H5T_STD_I8LE_g   H5T_STD_I16LE_g   H5T_STD_I32LE_g   H5T_STD_I64LE_g
H5T_STD_U8LE_g   H5T_STD_U16LE_g   H5T_STD_U32LE_g   H5T_STD_U64LE_g
H5T_IEEE_F32LE_g  H5T_IEEE_F64LE_g
H5T_C_S1_g        (base for string types)
```

**Native (memory) types**

```
H5T_NATIVE_INT8_g    H5T_NATIVE_UINT8_g
H5T_NATIVE_INT16_g   H5T_NATIVE_UINT16_g
H5T_NATIVE_INT32_g   H5T_NATIVE_UINT32_g
H5T_NATIVE_INT64_g   H5T_NATIVE_UINT64_g
H5T_NATIVE_FLOAT_g   H5T_NATIVE_DOUBLE_g
```

**Property-list class IDs** (the `cls_id` argument of `H5Pcreate`)

```
H5P_CLS_FILE_CREATE_ID_g
H5P_CLS_FILE_ACCESS_ID_g
H5P_CLS_DATASET_CREATE_ID_g
H5P_CLS_DATASET_ACCESS_ID_g
H5P_CLS_LINK_CREATE_ID_g
H5P_CLS_ATTRIBUTE_CREATE_ID_g
```

`H5Pcreate(H5P_FILE_ACCESS)` therefore means: read
`H5P_CLS_FILE_ACCESS_ID_g` via `GetH5Var`, then pass **that value** to
`H5Pcreate`.

---

## 9. Workflow sequences

All sequences assume: DLL loaded, function pointers bound, `H5open()`
called, `_g` variables read, `H5Eset_auto2(0, nil, nil)` set.

### 9.1 Create a file with the modern format

```
fapl   := H5Pcreate(H5P_CLS_FILE_ACCESS_ID_g)
H5Pset_libver_bounds(fapl, H5F_LIBVER_V110, H5F_LIBVER_LATEST)
fileId := H5Fcreate('out.h5', H5F_ACC_TRUNC, H5P_DEFAULT, fapl)
H5Pclose(fapl)
...
H5Fclose(fileId)
```

### 9.2 Group hierarchy with UTF-8 names

```
lcpl := H5Pcreate(H5P_CLS_LINK_CREATE_ID_g)
H5Pset_char_encoding(lcpl, H5T_CSET_UTF8)
H5Pset_create_intermediate_group(lcpl, 1)   // auto-create missing parents
grpId := H5Gcreate2(fileId, '/Motor/Block', lcpl, H5P_DEFAULT, H5P_DEFAULT)
H5Gclose(grpId)
H5Pclose(lcpl)
```

### 9.3 Compound datatype `{ int64 timestamp_ns; double value }`

```pascal
type
  TSampleRec = packed record
    TimestampNs: Int64;   // offset 0
    Value:       Double;  // offset 8
  end;                    // SizeOf = 16
```

```
ct := H5Tcreate(H5T_COMPOUND, SizeOf(TSampleRec))   // 16
H5Tinsert(ct, 'timestamp_ns', 0, H5T_STD_I64LE_g)
H5Tinsert(ct, 'value',        8, H5T_IEEE_F64LE_g)
```

The record **must be `packed`** so the field offsets match the
`H5Tinsert` offsets.

### 9.4 Extensible (chunked) 1-D dataset

```
dims[0]    := 0
maxdims[0] := H5S_UNLIMITED
space := H5Screate_simple(1, @dims[0], @maxdims[0])

dcpl := H5Pcreate(H5P_CLS_DATASET_CREATE_ID_g)
chunk[0] := 8192
H5Pset_chunk(dcpl, 1, @chunk[0])     // MANDATORY for an unlimited dimension
H5Pset_shuffle(dcpl)
H5Pset_deflate(dcpl, 4)

dsetId := H5Dcreate2(grpId, 'Temperatur', ct, space,
                     H5P_DEFAULT, dcpl, H5P_DEFAULT)
H5Pclose(dcpl)
H5Sclose(space)
```

### 9.5 Append a batch (hyperslab)

```
newSize[0] := oldCount + n
H5Dset_extent(dsetId, @newSize[0])

fileSpace := H5Dget_space(dsetId)
start[0]  := oldCount;  count[0] := n
H5Sselect_hyperslab(fileSpace, H5S_SELECT_SET, @start[0], nil, @count[0], nil)

memSpace := H5Screate_simple(1, @count[0], nil)
H5Dwrite(dsetId, ct, memSpace, fileSpace, H5P_DEFAULT, @buf[0])

H5Sclose(memSpace)
H5Sclose(fileSpace)
oldCount := oldCount + n
```

### 9.6 String attribute

```
strType := H5Tcopy(H5T_C_S1_g)
H5Tset_size(strType, H5T_VARIABLE)
H5Tset_cset(strType, H5T_CSET_UTF8)
aSpace  := H5Screate(H5S_SCALAR)
attrId  := H5Acreate2(dsetId, 'units', strType, aSpace, H5P_DEFAULT, H5P_DEFAULT)
valuePtr := PAnsiChar(UTF8String('°C'))
H5Awrite(attrId, strType, @valuePtr)        // pointer TO the pointer
H5Aclose(attrId);  H5Sclose(aSpace);  H5Tclose(strType)
```

### 9.7 Numeric attribute

```
aSpace := H5Screate(H5S_SCALAR)
attrId := H5Acreate2(dsetId, 'sample_period_ns', H5T_STD_I64LE_g, aSpace,
                     H5P_DEFAULT, H5P_DEFAULT)
val : Int64 := 1000000
H5Awrite(attrId, H5T_NATIVE_INT64_g, @val)
H5Aclose(attrId);  H5Sclose(aSpace)
```

### 9.8 Cleanup rule

Every `H5*create*` / `H5*open*` / `H5Pcreate` / `H5Screate*` / `H5Tcopy`
/ `H5Tcreate` that returns a positive `hid_t` must be closed with the
matching `H5*close`. In Delphi, guard each with `try..finally`. Close
order: attributes / datasets first, then groups, the file last.

---

## 10. Pitfall checklist

1. **`cdecl` forgotten** → stack corruption. Every HDF5 function is `cdecl`.
2. **`H5open()` not called** → every `_g` variable is 0.
3. **`_g` variables read before `H5open()`** → invalid values.
4. **`hid_t` declared 32-bit** → wrong; `hid_t` is `Int64`.
5. **Delphi `string` passed directly** → HDF5 wants UTF-8/`PAnsiChar`.
6. **Compound record not `packed`** → wrong offsets, corrupt data.
7. **Handles not closed** → leaks. `try..finally` per `hid_t`.
8. **Chunking forgotten with `H5S_UNLIMITED`** → `H5Dcreate2` fails.
9. **32/64-bit mismatch** → a Delphi Win64 build needs the Win64 DLL.
10. **VLEN string written wrong** → `H5Awrite` expects `char**`.
11. **Error auto-print interferes** → `H5Eset_auto2(0, nil, nil)` after
    `H5open()`.
12. **UTF-8 names without LCPL encoding** → umlauts corrupted.
13. **Property-list class IDs treated as constants** → they are `_g`
    variables.

---

## 11. The bundled HDF5 build

The OSF exporter pins **HDF5 1.14.4-3**, the official HDF Group
`win-vs2022_cl` x64 build. `zlib` is **statically linked** into that
`hdf5.dll`, so `H5Pset_deflate` works without a separate `zlib.dll`. The
build's MSVC runtime dependencies (`msvcp140*.dll`, `vcruntime140*.dll`,
`concrt140.dll`) are deployed alongside `hdf5.dll` by `install-hdf5.ps1`.
