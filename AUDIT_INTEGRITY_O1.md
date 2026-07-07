# O1 Audit — Parser Behavior Ahead of the Integrity Profile Spec Revision

**Type:** read-only audit — no code changes, no implementation commits.
**Date:** 2026-07-07.
**Scope:** current (pre-revision) behavior of the four OSF implementations at
the three points where the upcoming OSF5 "integrity profile" (CRC32C /
signatures / PKI) will change or presuppose behavior.
**References:** OSF5 integrity concept paper (Zenodo, DOI
[10.5281/zenodo.21227942](https://doi.org/10.5281/zenodo.21227942)); internal
architecture summary Rev. 4.

**Method note:** findings are grounded in source-level tracing of the actual
parser code (file + line + function), not in guesses. No runtime experiments
were executed — for point A the decisive behavior is fully determined by the
length-parse path (traced per implementation), and for point C by the
control-byte dispatch/skip path; a runtime probe would not change the verdicts.
The optional experiments from the brief were therefore substituted by exact code
traces (cheaper and equally conclusive).

---

## 1. Check points (recap)

- **A — Magic-header parser tolerance.** How is `OSF5 <number>\n` parsed, and what
  happens to `OSF5 84512 crc32c:9A3F01BC\n` (an extra space-separated token after
  the length)? Target of the revision: unknown tokens are "must understand" →
  **rejection** is the goal. Category **(a)** = hard error/rejection (already
  conformant), **(b)** = token ignored & file read (must be tightened), **(c)** =
  other.
- **B — Block reader payload-length validation.** Is `N × sample_size (+ header)
  == block_length` enforced, or are surplus trailing bytes tolerated? Behavior on
  **4 surplus bytes** (the future frame-CRC) for numeric blocks, and confirmation
  that variable-length string/binary blocks would absorb surplus into the value
  (motivating a fail-closed CRC framing).
- **C — Unknown control byte.** Behavior today for control-byte value **9**
  (future `bcIntegritySignature`) and unknown values generally: clean skip via the
  length field, or hard error / stream abort? Signature blocks must be cleanly
  skippable by non-stage-c readers.

---

## 2. Consolidation matrix (implementation × check point)

| Check point | Rust | Delphi | C++ | Java |
|---|---|---|---|---|
| **A** — trailing header token `crc32c:…` | **(a)** reject¹ | **(b) token ignored, file reads** | **(a)** reject | **(a)** reject |
| **B** — 4 surplus bytes, **numeric** block | tolerant — silent drop² | tolerant — silent drop² | tolerant — silent drop² | tolerant — silent drop² |
| **B** — surplus bytes, **string/binary** block | absorbed into value | absorbed into value | absorbed into value | absorbed into value |
| **C** — control byte **9** (unknown) | **skip-via-length (clean)** | **soft-abort → mislabeled "truncation"** | **skip-via-length (clean)** | **skip-via-length (clean)** |

¹ Rust rejects **incidentally**: the embedded space pollutes the `u64` parse, so
the diagnostic misattributes the cause ("length is not a valid u64") rather than
"unexpected trailing token". Same misleading-message caveat applies to C++ and
Java (both reject via the number parse failing on the extra token).
² Numeric surplus is **never** misread as an extra sample — the sample count is
authoritative (explicit `uint32 N` or implicit `N=1`), so the tail is simply
dropped. No error, no warning, no counter, in all four.

### Outliers worth flagging

- **A:** Delphi is the **only** implementation in category (b) — it tokenizes on
  space, uses `Parts[0]` (identifier) and `Parts[1]` (length) and **silently
  discards** any further tokens, so `OSF5 84512 crc32c:9A3F01BC` opens and reads
  normally. The other three reject (Rust/C++/Java feed the whole remainder to a
  single integer parse, which fails on the extra token).
- **C:** Delphi is again the **only** outlier, in the opposite direction — an
  unknown control byte does **not** skip-and-continue; it logs a warning, returns
  `False`, which the caller treats as truncation (`FTruncationSeen := True`) and
  **aborts the remaining scan**. Rust, C++ and Java all skip the block via its
  length field and continue cleanly.
- **B:** **uniform across all four** — no "payload fully consumed" assertion
  anywhere; numeric surplus silently dropped, variable-length surplus absorbed
  into the value. A frame-CRC must therefore be carved off **before** dispatch to
  the typed parser (a post-parse `remaining()==0` check is insufficient because
  string/binary trailing bytes are indistinguishable from real payload). This is
  the concrete justification for the fail-closed CRC-framing decision.

---

## 3. Migration effort (S / M / L)

| | A — header token | B — payload length / frame-CRC | C — control byte 9 |
|---|---|---|---|
| **Rust** | S (reject already holds; S–M to make it intentional + better message) | **M** (carve trailer, strict residual checks, verify) | S |
| **Delphi** | S (reject) / M (parse+verify a `crc32c:` token) | S numeric read-past / **M–L** var-length framing | **M** (make skip-and-continue, fix "truncation" label) |
| **C++** | S (none needed to reject; S–M to tokenize+accept) | **M** (thread trailer width into `next()` step 6/7, carve before dispatch) | S |
| **Java** | **M** (replace whole-remainder parse with whitespace tokenizer) | **M** numeric / **L** string/binary (greedy `body.remaining()` rework) | S |

**Aggregate read:**
- **B is the cross-cutting work item** and the strongest argument for fail-closed
  CRC framing: every reader would silently ignore (numeric) or absorb (string/
  binary) a naive trailing CRC. The frame-CRC must be part of the block framing
  known from the metablock/profile, carved off before the typed parser runs.
- **Delphi needs two behavioral fixes** the others do not: tighten A (reject
  unknown header tokens) and fix C (skip-and-continue past `bcIntegritySignature`
  instead of aborting).
- **C is essentially free** (S) for Rust/C++/Java — they already skip unknown
  block types forward-compatibly; only a nicer dedicated diagnostic/counter is
  optional.

**Correction to a prior assumption:** the brief lists Java as a "Zwischenstand".
The audit found the Java core is in fact a **complete** best-effort OSF4+OSF5
reader — `MagicHeaderParser` + `internal/BlockReader` (with `Block`,
`ChannelAssembler`, `DataManager`, JSON/XML metablock parsers, transparent OSFZ
read) are all implemented. So Java is a full participant in this audit, not a
partial one.

---

## 4. Per-implementation findings

### 4.1 Rust — `implementations/rust/osf-core/src/`

**A — Magic-header token tolerance**
- Fundstelle: `header.rs:104-130` `fn parse_magic_header_line` (called from
  `parse_magic_header`, line 67).
- Verhalten (`OSF5 84512 crc32c:9A3F01BC\n`): one `split_once(' ')` (line 105) →
  `identifier="OSF5"`, `rest="84512 crc32c:9A3F01BC"`. `rest.trim()` strips only
  leading/trailing whitespace; the interior space survives, so
  `len_str.parse::<u64>()` (line 120) fails → `Err(OsfError::InvalidMagicHeader(
  "metablock length is not a valid u64: \"84512 crc32c:9A3F01BC\""))`. File
  rejected. (A single trailing space is tolerated by `trim()`; any real extra
  token rejects.)
- Kategorie: **(a) hard error / rejection**, but incidental — rejection happens
  because the surplus token breaks the `u64` parse, not via a deliberate
  unknown-token rule; the message misattributes the cause.
- Migrationsbedarf: reject target already met (no silent acceptance) — **S**. To
  make it intentional (whitespace-tokenize, parse field 0 as length, understand or
  reject `crc32c:<hex>`): **S–M**.

**B — Block length validation**
- Fundstelle: framing/length `reader.rs:214-243` (`read_length_field`,
  `read_payload`) + `Iterator::next` `348-407` (reads exactly `length` bytes, no
  `N×size==length` check). Numeric parsers: `parse_start_data` `675-690`,
  `parse_continued_data` `692-700`, both via `read_numeric_n` `959-994`;
  `parse_abs_timestamp_data` `702-783`; `parse_continued_rel_stamp_data` `911-953`.
- Verhalten (4 surplus bytes numeric): **silent ignore.** Parsers read exactly N
  samples from the head of the body and never assert the slice is fully consumed;
  surplus dropped, never misread as a sample (N is explicit or implicit-1). A
  too-short body is the only length mismatch that errors (`InvalidBlock`).
- Verhalten (string/binary): surplus **absorbed as payload** —
  `parse_abs_timestamp_string_or_binary` `795-871`; single-sample path `818-825`
  takes all remaining bytes as the value (minus the OSF4 terminator); multi-sample
  requires `total % n == 0` (829) else `warn!` + fallback to single-sample,
  again absorbing the tail.
- Kategorie: **tolerant** (framing consumes `length`, no body-consumed check).
- Migrationsbedarf: thread an integrity-trailer length through `next()`, carve it
  off the body before dispatch, add strict "residual == 0" to numeric parsers;
  string/binary needs the trailer removed before the "rest = payload" step. **M**.

**C — Unknown control byte (value 9)**
- Fundstelle: `block.rs:341-356` `decode_control_byte` (`byte & 0x7F == 9` →
  `ControlKind::Unknown(9)`); dispatch `reader.rs:447-455`; skip `record_skip`
  `604-619`. Payload already read (step 6, line 395) before decode.
- Verhalten: **skip-via-length.** → `SkipReason::ReservedBlockType(9)` →
  `BlockKind::Skipped { bytes_skipped: length, payload: None }`;
  `stats.blocks_skipped_reserved_type` bumped; iteration continues, no hard error.
  (Unknown *channel index* is a hard error by contrast.)
- Diagnostic: structured only (`ReservedBlockType(9)` + counter) — **no log line**.
- Kategorie: **skip** (clean). Migrationsbedarf: non-stage-c read needs nothing;
  a dedicated `ControlKind`/`SkipReason` for the signature block is a nicety. **S**.

**Kurzfazit Rust:** fail-closed on header surplus (A rejects, incidentally, with a
misleading message) and cleanly forward-compatible for unknown block types (C).
The real gap is B: numeric surplus silently dropped, string/binary surplus
corrupts the value — no "body fully consumed" check anywhere.

### 4.2 Delphi — `implementations/delphi/src/`

**A — Magic-header token tolerance**
- Fundstelle: `OSF.Filer.pas:897-941` `TOSFFile.ReadMagicAndMeta` (key 905-919);
  `ReadAsciiLine` `481-501`; `OSFVersionFromMagic` `OSF.Types.pas:314-324`.
  Parallel `PeekMagic` `OSF.Filer.pas:755-778`.
- Verhalten: line tokenized on space — `Parts := string(Line).Split([' '])` (909).
  Validation is only `Length(Parts) < 2` → error (no upper bound). `Parts[0]`
  matched exactly to the magic; length from `Parts[1]` only
  (`StrToInt64Def(Parts[1], -1)`, 917); `Parts[2..]` never read. For
  `OSF5 84512 crc32c:9A3F01BC\n`: 3 tokens, OK, third discarded → **file opens and
  reads normally.** (Caveat: a CRC glued without a space, `84512crc32c…`, would
  make `StrToInt64Def` return -1 → rejection.)
- Kategorie: **(b) token ignored & file read** — extra tokens silently discarded,
  no error, no log.
- Migrationsbedarf: add an upper-bound/whitelist check after the split (reject
  `Length(Parts) > 2` unless the token is a recognized profile prefix). Reject-only
  **S**; real parse+verify of a `crc32c:` header token **M**.

**B — Block length validation**
- Fundstelle: framing `OSF.Filer.pas:1212-1246` `ReadDataBlock` + `1248-1319`
  `DecodeBlockPayload`; sample carving in `OSF.Data.Manager.pas`:
  `DecodeEquidistantBlock` (234-268, loop 257-267), `DecodeAbsTimestampedBlock`
  (~172-227), `DecodeRelTimestampedBlock` (273-310).
- Verhalten (4 surplus bytes numeric): **minimum-length check only** —
  `if Integer(LenField) < RequiredLen then Exit` (1286); never `N×size ==
  remaining`. Everything after the prefix becomes `RawPayload`
  (`PayloadSize := LenField - Offset`, 1310); manager decode loops are bounded by
  `Block.SampleCount`, so surplus trailing bytes are **never read** → **silently
  ignored** (not misread as a sample).
- Verhalten (string/binary): `ValSize := PayloadLen - Pos`
  (`OSF.Data.Manager.pas:199-200`) — the whole remainder (minus OSF4 `0x00`)
  becomes the value → surplus **swallowed into the value verbatim** (corrupts it).
- Kategorie: **tolerant** (length is a lower bound for numeric; "value = rest" for
  variable-length).
- Migrationsbedarf: numeric frame-CRC is transparently read-past today (no change
  needed to read past it) — **S**; variable-length needs explicit carve-off of the
  trailing CRC before decode across all three decoders + a verify pass — **M–L**.

**C — Unknown control byte (value 9)**
- Fundstelle: data path `OSF.Filer.pas:1271-1275` in `DecodeBlockPayload`; info/
  trailer path `1199-1203` in `ReadInfoBlock`; reaction `ReadNextBlock` `1160-1166`.
  Constants `OSF_BLOCK_TYPE_MASK=$7F`, `bcAbsTimeStampData=8` (`OSF.Types.pas:36,72`).
- Verhalten: `TypeBits := CtrlByte and $7F`; `if TypeBits > Ord(bcAbsTimeStampData)`
  (=8) → `Logger.Write(SOSFLogUnknownBlockType, …, llWarning)` then `Exit(False)`.
  Value 9 (9 > 8) takes this branch. Returning False is treated as **truncation**:
  `ReadNextBlock` sets `FTruncationSeen := True`, logs `SOSFLogTruncatedBlock …
  stopping` (llWarning), and the caller loop ends → **the whole scan aborts at
  that block** (no exception; the block's `LenField` bytes were already consumed).
  The `ReadInfoBlock` guard on the `$FFFF` channel behaves the same.
- Kategorie: **neither clean skip nor hard exception** — a warning-logged
  best-effort **stop**, mislabeled downstream as truncation. Skip-and-continue
  exists only for filter-excluded channels (`SkipExcludedBlock` `669-689`) and
  unknown channel indices (also `Exit(False)`), never for an unknown *block type*.
- Migrationsbedarf: change the unknown-type branches in `DecodeBlockPayload` /
  `ReadInfoBlock` (and the `Exit(False)` contract in `ReadDataBlock`/
  `ReadNextBlock`) to consume the already-read block and loop; fix the misleading
  "truncated … stopping" diagnostic. Mechanical but touches control flow in three
  spots — **M**.

**Kurzfazit Delphi:** lenient on the front (A: unknown header tokens silently
ignored, category b) and quiet in the middle (numeric surplus dropped) — both CRC-
rollout-friendly for reading past new bytes. Two real hazards: (1) variable-length
string/binary blocks absorb a trailing frame-CRC into the value (needs explicit
framing); (2) an unknown control byte (9) aborts the scan and is mislabeled as
truncation instead of skipping.

### 4.3 C++ — `implementations/cpp/`

**A — Magic-header token tolerance**
- Fundstelle: `src/header.cpp:71-108` `parseMagicHeaderLine` (via `parseMagicHeader`
  112-134); length parse 97-105; `readFirstLine` 22-55.
- Verhalten: first space splits identifier from `rest` (72); only trailing
  whitespace is trimmed (89-95); `from_chars` (98-100) parses an integer and line
  101 enforces `ptr == lenStr.data()+lenStr.size()` — the **entire remainder after
  the first space must be one uint64**, no interior separators. For
  `OSF5 84512 crc32c:9A3F01BC\n`: `from_chars` consumes `84512`, stops at the
  interior space → full-consume check fails.
- Kategorie: **(a) hard error / rejection** — `Error::Code::InvalidMagicHeader`,
  message `metablock length is not a valid uint64: "84512 crc32c:9A3F01BC"`
  (mediocre — says "not a valid uint64" rather than "unexpected trailing token").
- Migrationsbedarf: already rejects — **none functional**. To *recognize/accept* a
  CRC token, replace the whole-remainder contract (89-105) with real tokenization
  and improve the message. **S** (reject-only) / **S–M** (tokenize+accept).

**B — Block length validation**
- Fundstelle: full payload read up front `reader.cpp:737-751` (`readPayload`; `body
  = payload+1`, `bodyLen = length-1`); `parseStartData` `433-448`,
  `parseContinuedData` `450-457` via `readNumericN` `145-164` over `PayloadCursor`
  `51-113`; `parseAbsTimestampData` `315-372`; `parseContinuedRelStampData` `396-419`;
  string/binary `parseAbsTsStringOrBinary` `231-313`.
- Verhalten (4 surplus bytes numeric): **silent ignore.** `PayloadCursor` guards
  only underflow (`remaining() < width → nullopt → InvalidBlock`); sample count is
  authoritative (implicit 1 or wire `u32 N`). No `remaining()==0` assertion → the
  4 trailing bytes are neither consumed nor flagged.
- Verhalten (string/binary): surplus becomes value — single-sample (258-268) takes
  the entire `restLen-8` window; multi-sample (285-312) derives `perSample =
  restLen/n` and, if divisible, distributes surplus *into* the samples (a genuine
  misread), else degrades to one sample. No rejection.
- Kategorie: **tolerant** (underflow strict; overflow/surplus unvalidated).
- Migrationsbedarf: carve the trailing integrity bytes off *before* dispatch
  (thread profile awareness from the metablock into `next()` step 6/7,
  `reader.cpp:737-766`, reduce `bodyLen` by the CRC width, verify). A plain
  `remaining()==0` check is easy for numeric; the string/binary carve-off is the
  real work. **M**.

**C — Unknown control byte (value 9)**
- Fundstelle: `block.cpp:41-58` `decodeControlByte` (`default → ControlKind::Unknown`,
  so 9 → Unknown); dispatch `reader.cpp:778-780`; `makeSkipped` `753-766`;
  `recordSkip` `613-627`.
- Verhalten: **skip-via-length.** Payload already read via the length prefix →
  `Skipped` with `SkipReason::Kind::ReservedBlockType`, `rawByte = 9`;
  `m_stats.blocksSkippedReservedType` bumped (+ per-channel counters); iteration
  continues; not a hard error. Value 9 is lumped with genuine reserved bytes 0/2
  (no dedicated future-type reason/counter).
- Kategorie: **skip** (clean). Migrationsbedarf: nothing needed to keep old readers
  working; optionally add a distinct `SkipReason::Kind` + counter for the signature
  block. **S**.

**Kurzfazit C++:** already forward-compatible on both integrity-relevant axes —
control byte 9 cleanly skipped (as `ReservedBlockType`), magic header already
rejects a trailing `crc32c:` token. The gap is per-block payload validation:
underflow strict, but no "payload fully consumed" assertion, so a trailing
frame-CRC is silently ignored (numeric) or swallowed into the value
(string/binary) — carve off by profile knowledge before dispatch.

### 4.4 Java — `implementations/java/osf-java/`

**A — Magic-header token tolerance**
- Fundstelle: `src/main/java/com/optimeas/osf/MagicHeaderParser.java` `parseLine`
  115-141 (entry `parse(InputStream)` 71-102 / `parse(byte[])` 49-56). Reader
  `internal/BlockReader.java` fully implemented.
- Verhalten: **"rest of line as a single number"**, not tokenized. `sep =
  line.indexOf(' ')` splits at the first space; `rest = substring(sep+1).trim()`
  (122-123) — interior space survives; `Long.parseUnsignedLong(rest)` (134) throws
  `NumberFormatException`, caught (135) → `OsfException.MalformedFile("metablock
  length is not a valid uint64: \"84512 crc32c:9A3F01BC\"")`. File rejected. (No
  CRC/token test in `MagicHeaderParserTest` — behavior currently untested.)
- Kategorie: **(a) hard error / rejection**.
- Migrationsbedarf: replace whole-remainder parse with a whitespace tokenizer
  (token 0 = length, rest = optional `key:value` metadata). **M**.

**B — Block length validation**
- Fundstelle: `internal/BlockReader.java` — length `readAll` 136-138; payload slice
  163-168 (`payload = new byte[lengthI]; buf.get(payload)`); routing `decodeBlock`
  232-263; numeric `parseStartData` 278-285, `parseContinuedData` 289-294,
  `parseAbsTimestampData` 313-340; string/binary `parseAbsTsStringOrBinary` 349-400.
- Verhalten (4 surplus bytes numeric): **no `N×size==length` check.** `length` used
  only for bounds (163) and slicing; N from the `uint32` prefix or implicit 1
  (`readSampleCount` 265-274). Decoders read exactly `1 + N×size` from `body` and
  never verify `body` is drained → surplus **silently discarded**; block still
  counts as good (`incBlocksRead` 174). (If the 4 bytes sit *after* all blocks, the
  next iteration reads them as a channel index, lookup returns null →
  `markTruncated(); break` — flagged as truncation.)
- Verhalten (string/binary): N=1 (364-371) does `payload = new byte[body.remaining()];
  body.get(payload)` — greedily swallows all remaining bytes as the value → a
  frame-CRC lands inside the string/blob. N>1 (375-399) splits into equal segments;
  4 extra bytes break divisibility → fallback absorbs everything into one sample.
- Kategorie: **tolerant** (no exact-length assertion).
- Migrationsbedarf: numeric — post-decode "consumed == length" assertion + split off
  the trailing CRC before decode (**M**); string/binary — rework the greedy
  `body.remaining()` consumption so a frame-CRC is not treated as value bytes
  (**L**).

**C — Unknown control byte (value 9)**
- Fundstelle: `internal/BlockReader.java` `decodeBlock` `default:` 259-262 (`kind =
  control & 0x7F`, 236); payload pre-consumed by length 163-168; skip not counted
  as a read block (173-174).
- Verhalten: **skip-via-length.** Control 9 (and every undefined ≥ 9) →
  `Block.Skipped(channelIndex, SkipReason.RESERVED_BLOCK_TYPE, length)`; payload
  already advanced past via the length field → alignment preserved, decoding
  continues. Diagnostic = structured `Block.Skipped` record (`Block.java` 102-114),
  `bytesSkipped = length`; no log message, `incBlocksRead` deliberately not bumped.
  No hard error, no truncation flag. (A bit-7 multi flag on control 9, `0x89`, is
  masked off → also skips as kind 9.)
- Kategorie: **skip** (forward-compatible). Migrationsbedarf: to *process* a future
  `bcIntegritySignature=9`, add a `case 9` handler + a new `Block` variant. **S**.

**Kurzfazit Java:** the Java core (`com.optimeas.osf` / `osf-java`) is a complete
best-effort OSF4+OSF5 reader (MagicHeaderParser + internal/BlockReader + Block,
ChannelAssembler, DataManager, JSON/XML metablock parsers, transparent OSFZ read).
Against the integrity profile it is intolerant in the wrong place on the header (an
extra `crc32c:` token rejects the whole file, category a) and loosely tolerant on
blocks (no `N×size==length`; numeric CRC dropped, string/binary CRC swallowed;
control 9 skipped forward-compatibly). Main effort: header tokenization (M) and
per-block exact-length/CRC validation, hardest for the greedy string/binary path
(M–L).

---

## 5. Conclusions for the spec revision

1. **Frame-CRC framing must be metablock/profile-driven, carved off before the
   typed parser runs (fail-closed).** All four readers would otherwise silently
   ignore a numeric trailing CRC and corrupt string/binary values with it. A
   post-parse "fully consumed" check is enough for numeric but **not** for
   variable-length blocks — hence the fail-closed decision is correct and
   necessary.
2. **Unknown-block skip is already the fallback for three of four** (Rust, C++,
   Java skip control 9 via the length field). **Delphi must be changed** to
   skip-and-continue instead of aborting the scan (and stop mislabeling it as
   truncation).
3. **Header "must-understand" tokens:** Rust, C++, Java already reject an unknown
   trailing token (incidentally, with a misleading message — worth improving).
   **Delphi is the one implementation that silently accepts it** and must be
   tightened.
4. **Java is a full implementation**, not a partial one — plan its integrity work
   on par with Rust/C++.
