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

**Why a separate directory (not directly in `examples/generated/`):** these
files require an integrity-aware reader. Integrity-*unaware* consumers — the
low-level example loops that glob `examples/generated/*.osf` non-recursively,
and the shared `examples/reference_manifest.json` conformance contract — would
fail on them (a CRC-unaware reader folds the trailing frame-CRC bytes into a
string/binary value). They are therefore **not** listed in
`reference_manifest.json` yet; add them there once every implementation the
manifest drives supports level `crc`.
