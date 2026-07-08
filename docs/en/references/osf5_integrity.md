---
title: OSF5 Integrity Profile
description: Normative specification of the optional OSF5 integrity profile — CRC32C corruption detection, Ed25519 signature chain, and X.509 / PKI provenance
image: "/img/om_social_card.png"
keywords:
  - OSF5
  - Integrity
  - CRC32C
  - Ed25519
  - Signature
  - PKI
last_update:
  date: 2026-07-08
  author: Optimeas GmbH
license: CC-BY-4.0
copyright: © 2026 optiMEAS GmbH und optiMEAS Switzerland GmbH
---

🇩🇪 [German version](../../de/references/osf5_integrity.md)

# OSF5 Integrity Profile

This document is the **normative** specification of the optional **integrity
profile** for the Open Streaming Format version 5 (OSF5). It adds three layered
guarantees on top of the base format: block-accurate corruption detection
(CRC32C), streaming-capable tamper detection and provenance (a SHA-256 hash
chain with periodic Ed25519 signature anchors), and offline third-party
verifiability (an X.509 / PKI certificate model).

The profile is a **pure OSF5 property**. OSF4 files are not affected and never
carry any part of it. The design is fully backward compatible: OSF5 files
*without* an integrity declaration remain valid exactly as before.

The concept and rationale behind this profile are published as a concept paper
(Zenodo, DOI [10.5281/zenodo.21227942](https://doi.org/10.5281/zenodo.21227942),
also in this repository under [`docs/papers/`](../../papers/)). That paper is the
background; **this document is the normative reference** for implementers.

The base format (magic header, JSON metablock, self-contained data blocks) is
described in [`osf_general.md`](../osf_general.md) and
[`osf5.md`](osf5.md); this document only adds the integrity layer.

---

## 1. Three-level model

The profile is a **strictly ordered ladder**; each level contains the one below
it:

```
none  ⊂  crc  ⊂  signed
```

The level applies **per file** and is declared **exactly once**.

| Level | Protects against | Typical use |
|---|---|---|
| **none** | — | lab captures, intermediate files, minimal embedded writers |
| **crc** | corruption (bit errors, truncation, media faults) | standard for field data |
| **signed** | corruption **and** tampering; third-party-provable provenance | operational data with evidential value |

Two design rules prevent the format from fragmenting:

1. **Writers choose the level freely** — embedded systems keep the option of a
   minimal writer.
2. **Every conformant OSF5 reader MUST process all levels.** The result is one
   format with a profile, not three dialects.

The granularity is always the **whole file**: either all blocks carry the
mechanism, or none do.

---

## 2. Header-token grammar (normative)

The magic-header line is extended by zero or more optional tokens after the
metablock length:

```abnf
header-line     = identifier SP metablock-len *(SP token) LF
token           = key ":" value
key             = 1*(a-z / 0-9 / "-")          ; lowercase only
value           = 1*(VCHAR without SP)
```

- Exactly **one** `SP` (0x20) separates fields; there is **no trailing space**
  before the `LF`.
- Tokens are **"must understand"**: a reader that encounters a `key` it does not
  know **MUST reject the file**, with a diagnostic of the form
  `unknown header token '<key>'` — **not** as a number-parse error. (Today's
  parsers reject an unexpected token only incidentally, via a misleading
  "length is not a valid integer" message; see the migration appendix.)
- Header tokens are an **OSF5-only** feature. The OSF4 legacy identifiers
  (`OSF4`, `OCEAN_STREAM_FORMAT4`, `OCEAN_STREAMING_FORMAT4`) MUST NOT carry
  tokens; a token after an OSF4 identifier is a malformed file.

### Keys defined by this revision

| Key | Level | Value |
|---|---|---|
| `crc32c` | crc | `crc32c:<8 HEXDIG uppercase>` — the CRC32C (Castagnoli) over the **raw metablock bytes** exactly as they appear in the file |
| `ed25519` | signed | `ed25519:<keyid>` — `keyid` = 16 HEXDIG **lowercase** = the first 8 bytes of SHA-256 over the DER-encoded `SubjectPublicKeyInfo` of the device certificate |

- **Hex casing is strict**: `crc32c` uses **uppercase** hex; the `ed25519`
  `keyid` uses **lowercase** hex. Readers MUST check the casing strictly.
- The `ed25519` token is only permitted **in addition to** `crc32c`, and only in
  that order — `crc32c` first, `ed25519` second. `signed` therefore always
  implies `crc`.
- The metablock is the only part of a file that cannot protect itself; the
  `crc32c` token carries its checksum so the whole verification order is
  deterministic.

### Verification order (normative)

1. Read the header tokens.
2. Verify the metablock CRC (from the `crc32c` token).
3. Parse the metablock.
4. Verify the data blocks (frame CRCs, and — at level signed — the signature
   chain).

---

## 3. Frame CRC (level crc)

At level crc **every** block carries a **CRC32C** (Castagnoli; hardware
accelerated on x86/SSE4.2 and ARMv8) computed over the **entire frame**: channel
index, length field, control byte, and payload.

Including the **length field** in the scope is deliberate: a flipped length field
is the most severe single-bit error, because the reader loses all block
boundaries after it.

The CRC forms the **last four bytes of the data area** and is **counted in the
length field**, so framing stays intact for every reader. The effective payload
length is the block length **minus four**.

```
without profile:  [channel index][length field][control byte][ payload ................ ]
                                               |<------------- LEN ------------------>|

with profile:     [channel index][length field][control byte][ payload ....... ][CRC32C]
                                               |<------------- LEN ------------------>|
                  |<================ frame-CRC scope ========================>|
```

### Normative implementation requirement (fail-closed framing)

When the profile is active the frame CRC is **part of the framing** and MUST be
separated **before** the typed payload is decoded. A *post-hoc* "residual bytes
== 0" check after decoding is **not sufficient**: for variable-length payloads
(`string`, `binary`) an integrity-unaware reader cannot distinguish appended CRC
bytes from real payload and would deliver corrupted values. Fail-open is
therefore constructively excluded — the reader carves the trailing four bytes off
using the profile knowledge from the header/metablock, then verifies them.

### Error behaviour

| Condition | Reaction |
|---|---|
| Data-block CRC mismatch | the block is **invalid** → skip it, **count it in the reader statistics**, and continue (partial data beats no data) |
| Metablock CRC mismatch | **reject the file** (nothing is interpretable without a trustworthy metablock) |
| Unknown header token | **reject the file** (see §2) |

**Recommendation to implementations:** in addition to the mandatory carve-off,
apply a strict full-consumption check for **numeric** blocks (`N × sample_size
(+ header fields)` must equal the effective payload length). This is a diagnostic
quality improvement, not a correctness requirement.

---

## 4. Signature block `bcIntegritySignature = 9` (level signed)

Level signed adds a new control-byte type and a running hash chain.

### The block

- New control-byte value **9** (`bcIntegritySignature`); only valid when the file
  declares level **signed**; **Bit 7 = 0** (single-value semantics).
- **Channel index: the reserved value `0xFFFE`** — a file-wide integrity channel
  that is **not** declared in the metablock. Readers without level-signed support
  skip the block via its length field, exactly like any other unknown block type
  (see §5). `0xFFFE` is distinct from the `0xFFFF` info/trailer channel of OSF4.
- Blocks on the reserved channel `0xFFFE` **always use a 4-byte length field
  (`uint32`)**, regardless of channel declarations — analogous to the historical
  `0xFFFF` info block.
- Signature blocks themselves carry a **frame CRC** like every other block.

### Payload (little-endian; order is normative)

| # | Field | Type | Meaning |
|---|---|---|---|
| 1 | `anchor_seq` | `uint32` | anchor sequence number, consecutive from 0 |
| 2 | `signing_time_ns` | `int64` | time of signing (basis of the validity semantics) |
| 3 | `chain_hash` | `byte[32]` | H(i), the current chain hash |
| 4 | `signature` | `byte[64]` | Ed25519 over `SHA-256(anchor_seq ‖ signing_time_ns ‖ chain_hash)`, the fields in wire order / wire encoding |
| 5 | `keyid_len` + `keyid` | `uint8` + `byte[keyid_len]` | key/certificate reference; `keyid` as in the header token (8 bytes) |

### Hash chain

```
H(0) = SHA256(header-line ‖ metablock)
H(i) = SHA256(H(i−1) ‖ Frame_i)
```

- Frames enter the chain **including** their frame CRC.
- **Signature blocks themselves also enter the chain** (as a `Frame_i`).
- The **first anchor therefore also covers the header and the metablock**
  through H(0).

The chain detects, beyond per-block CRC checks, the **deletion**, **insertion**
and **reordering** of blocks; the anchor sequence numbers detect **replay within
the file**.

### Cadence

Configurable (time- or block-based). The **normative default is time-based, 10
seconds**. In addition, an anchor at **regular file close is mandatory**, so a
cleanly closed file is fully signed.

### Power-loss semantics (normative)

If recording aborts hard, the file is **signed up to the last valid anchor**; the
tail after it is **CRC-valid but unsigned**. The verification report MUST name
both (e.g. "signed up to timestamp X, remainder CRC-valid, unsigned").

---

## 5. Certificates and PKI (level signed)

To let **any third party** — not just the manufacturer — verify provenance,
device keys are certified by a certificate authority (X.509 with Ed25519 per RFC
8410), analogous to the HTTPS trust model but as a private, publicly documented
hierarchy: an offline hardware-protected **root CA**, an issuing CA (tied to
production / the device cloud), and device certificates whose subject carries the
device serial number.

### Embedding location: the metablock

The certificate chain is embedded once per file as a new **optional object at the
`osf` level** of the metablock:

```json
"integrity": {
  "certificates": ["<base64 DER device certificate>", "<base64 DER intermediate certificate>"]
}
```

- **Leaf first**; the **root is never embedded** — the trust anchor must, by
  construction, come from outside (the publicly published root certificate,
  shipped with the verification tools, listed on the website and in the open
  repository, each with a fingerprint).
- Because the object lives in the metablock, the certificates are covered by the
  **metablock CRC** and by **H(0)**.
- **Note:** this object is **data, not a profile declaration.** The declaration
  is the header token alone (§2). A file can carry an `integrity.certificates`
  object and still be at level crc if no `ed25519` header token is present.

### Validity: "valid at signing time"

Measurement data outlives certificate lifetimes. The verification semantics are
therefore: **was the certificate valid at the signing time (`signing_time_ns`)?**
A file recorded in 2027 stays positively verifiable in 2040. Together with long
device-certificate lifetimes this is fit for measurement data. The theoretical
residual weakness (back-dating with a stolen, expired device key) is documented
and can, if required, be closed with **RFC 3161 timestamps** without changing the
format — the timestamp attaches to the signature, not to the data blocks.

### Revocation

Revocation uses **signed revocation lists** with **best-effort** semantics; the
verification report states the revocation-check status explicitly.

### Certificate classes and transformation policy

- **Device certificates** and **organization / tool certificates** form
  **separate, distinguishable classes.**
- **Transformations end the signature domain — by design.** Merging, converting
  and exporting inherently produce files without a device signature. Tools behave
  in a defined way: the **result falls back to level crc** and records its
  **provenance** (source files with their verification status) in the metadata
  (`infos`). Optionally an organization may **re-sign** the result with its own
  certificate — that is a **different, correspondingly labelled statement** than
  the device signature.

---

## 6. Verification status vocabulary (normative for tools)

Verification tools report one of:

| Status | Meaning |
|---|---|
| `valid_signed` | fully signed and valid |
| `partially_signed` | signed up to anchor X; the remainder is CRC-valid but unsigned |
| `crc_valid` | CRC level holds; no (valid) signature |
| `invalid` | a CRC or signature check failed where it must hold |
| `signature_unverifiable` | signatures cannot be checked (e.g. crypto library or root certificate missing) — the file stays readable and a CRC-level statement remains possible |

---

## 7. Classification against DIN EN 50159 (informative)

> This section is **informative**. EN 50159 addresses safety-related
> communication in railway signalling transmission systems; the mapping is
> provided because the profile is relevant for railway environments, even though
> it derives from the Cyber Resilience Act and addresses **files at rest** rather
> than transmission channels.

| Threat (EN 50159) | Mechanism in the OSF5 profile |
|---|---|
| Corruption | frame CRC32C per block; cryptographic: hash chain |
| Deletion | hash chain (a missing block breaks the chain) |
| Insertion | hash chain and signature (a foreign block breaks the chain) |
| Re-sequencing | hash chain (order is part of the chain construction) |
| Masquerade | Ed25519 signature with device certificate and CA chain |
| Replay | anchor sequence numbers (within the file); file identity via a unique **file UUID** in the metablock as the anchor for system-level checking |
| Delay | not applicable to data at rest |

Two delimitations are essential:

1. The profile addresses **replay and deletion at the file level** (re-importing
   old files, whole files disappearing) deliberately only through the **file-UUID
   interface**. Gap-freeness across file boundaries is the task of the
   higher-level **system layer** (the measurement environment, its sequence
   monitoring and secure time source) and is solved there. The `file_uuid`
   metablock parameter is specified in [`osf5.md`](osf5.md) — mandatory at level
   signed, recommended otherwise.
2. The profile is a **security** mechanism (in the sense of a category-3 / open
   network environment) and makes **no safety claim**: it does not replace
   safety-directed transmission per EN 50159 and establishes no SIL suitability.

---

## 8. Migration notes (informative)

Condensed from the parser audit `AUDIT_INTEGRITY_O1.md` (repository root). Per
implementation, the work the profile implies:

**All four implementations (Rust, Delphi, C++, Java)**
- Separate the frame CRC **before** the typed parser runs, driven by the profile
  declaration (fail-closed framing, §3). Every reader currently either silently
  drops surplus numeric bytes or absorbs surplus into a string/binary value, so a
  naive trailing CRC would be ignored or corrupt the value.

**Delphi**
- **Tighten the header tokenizer** — it does **not** reject unknown trailing
  tokens today (it splits on space and silently discards everything after the
  length). Must reject per §2.
- **Switch unknown control bytes to skip-and-continue** — today an unknown block
  type is a soft abort mislabelled as "truncation"; it must skip a
  `bcIntegritySignature` block via its length field and continue.

**Rust / C++ / Java**
- Make the rejection of unknown header tokens **intentional** and improve the
  diagnostic ("unexpected trailing token / unknown header token" instead of a
  number-parse error). All three already skip unknown control bytes (byte 9)
  cleanly via the length field — no change needed there.

**Java**
- Plan the integrity work as a **full implementation** (the Java core is a
  complete OSF4+OSF5 reader, not a stub): header tokenizer rework (effort **M**),
  and the greedy string/binary payload path (effort **L**) for the CRC carve-off.

---

> This document is licensed under [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/). Attribution: optiMEAS GmbH and optiMEAS Switzerland GmbH.
