# OSF5 integrity-profile reference files (level `crc`)

OSF5 files that carry the **integrity profile at level `crc`** (a `crc32c`
magic-header token over the metablock plus a per-block frame CRC32C). They exist
for cross-implementation validation of integrity-aware readers.

Rust-generated (`cargo run --example gen_crc_refs` from `implementations/rust/osf-core`):

- `osf5_crc_equidistant.osf` — three equidistant `double` channels.
- `osf5_crc_variable.osf` — one `string` and one `binary` channel.

Delphi-generated (`OSFCrcRefGen` from `implementations/delphi`):

- `osf5_equidistant_crc_delphi.osf` — three equidistant `double` channels.
- `osf5_variable_crc_delphi.osf` — one `string` and one `binary` channel.

The Rust and Delphi implementations read each other's files with zero CRC
failures (byte-identical CRC values in both directions).

**Part of the shared conformance contract (since 2026-07-09):** these four files
are listed in `examples/reference_manifest.json` under sub-path keys
(`integrity/osf5_crc_equidistant.osf`, …). Every implementation the manifest
drives (Java, Rust, C++, Delphi) now supports integrity level `crc`, so their
manifest-driven conformance tests load these files — resolving the sub-path keys
under `examples/generated/` — and additionally assert the reported integrity
profile plus zero frame-CRC failures.

**Why a separate directory (not directly in `examples/generated/`):** these
files require an integrity-aware reader. The integrity-*unaware* example loops
that glob `examples/generated/*.osf` **non-recursively** never see them (a
CRC-unaware reader would fold the trailing frame-CRC bytes into a string/binary
value). The manifest reaches them explicitly via their `integrity/` sub-path
keys, so only integrity-aware conformance tests load them.
