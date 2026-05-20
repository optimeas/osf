# OSF ↔ HDF5 — Format Infrastructure

Language-agnostic infrastructure for converting OSF files to **HDF5**
(Hierarchical Data Format 5). This directory holds only **HDF5-external**
material: the mapping specification, the DLL-binding knowledge base, and
the scripts that fetch the official HDF5 runtime.

The actual converter code lives with each language implementation — for
Delphi under `implementations/delphi/src/hdf5/` (the `hdf5.dll` wrapper)
and `implementations/delphi/src/OSF.Export.HDF5.pas` (the exporter,
wired into `osftool export --format hdf5`).

## Contents

| File | What |
|---|---|
| `SPEC.md` | OSF → HDF5 mapping specification — the authoritative rules every implementation follows |
| `WISSENSBASIS.md` | HDF5 C-library DLL-binding know-how: type mapping, the `_g`-variable mechanism, init order, API signatures, constants |
| `lib/` | HDF5 runtime fetch scripts + the runtime itself (binaries are not committed) |

## Growth rule

This directory **does not grow** with language implementations. When a
future implementation (Python, Rust, …) gains an HDF5 export, its code
goes under `implementations/<language>/` — nothing is added here. Only
HDF5-external material belongs in `dataformats/hdf5/`.

## Installing the HDF5 runtime

The HDF5 binaries (`hdf5.dll` and its MSVC runtime dependencies) are
**not committed** — `lib/.gitignore` excludes them. Fetch them with the
install script:

```powershell
# Windows x64
.\lib\install-hdf5.ps1
```

```bash
# Linux x64
./lib/install-hdf5.sh
```

The Windows script downloads HDF5 **1.14.4-3** (official HDF Group
`vs2022_cl` build) from GitHub, extracts the runtime DLLs into
`lib/win64/`, writes a SHA-256 of `hdf5.dll`, and records the version
and source URL in `lib/VERSION.txt`.

`zlib` is statically linked into this build of `hdf5.dll`, so there is
no separate `zlib.dll` to deploy — gzip/deflate dataset compression
works out of the box.

The Delphi exporter resolves `hdf5.dll` at run time; `lib/win64/` is one
of the paths it searches (see `WISSENSBASIS.md` and the
`Hdf5.Api` resolver).

## License

The OSF project is MIT-licensed (see the repository-root `LICENSE`).
The bundled HDF5 runtime is covered by the 3-clause BSD HDF5 license —
see `lib/LICENSE.txt`.
