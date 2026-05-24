# C++ Phase 7a — State Snapshot

Saved state from the C++ implementation Phase 7a architecture discussion,
paused 2026-05-24 during the cross-implementation null-terminator cleanup.

This file is the resume point. When work on C++ Phase 7 resumes (after the
spec-tightening session completes), read this file plus DECISIONS §7/§20/§21,
BACKLOG.md, and the latest spec docs, then continue with Q5 (Error-Handling)
in a new architecture discussion.

---

C++ Phase 6 finished 2026-05-23. Coverage-tests for non-Double
equidistant types added separately (commit `8ddea15`). Phase 7a
architecture discussion started 2026-05-24, paused mid-Q5 with all
prerequisite commits pushed.

**Code state — Phase 6 complete, plus coverage tests:**

- Phases 1, 2 (a+b), 3, 4, 5, 6 complete.
- **155/155 ctest** cases green locally on Windows (MSVC 19.50.35730 /
  VS 18 / CMake 4.2.3); 0 `/W4 /permissive-` warnings.
- Public API as of Phase 6 (see previous memory snapshot for
  the detailed listing — Foundation, Block, Reader, Stats,
  DataChannel/DataManager). No code changes since then.

**Spec and policy state (relevant for the writer):**

The writer architecture and spec were aligned in 2026-05-24 before
Phase 7a-implementation. Five commits to know about:

| Commit | What |
|---|---|
| `6f386a9` | DECISIONS §7 revised — C++ serves both worlds with `StreamingWriter` + `BlockWriter`; read is uniform in-memory across all implementations |
| `a17b900` | New `BACKLOG.md` for parked ideas (e.g. OSF6 null-terminator removal, documentation strategy) |
| `1deaeb0` + `a3d6015` | DECISIONS §21 — Java implementation architecture (parallel implementation, full OpenJDK baseline) |
| `f847b20` | BACKLOG — Java-specific deferred items (GraalVM Native Image, micro-JVM, mmap/DirectBuffer) |
| `4bad442` | **Spec clarification (critical for the writer):** Bit 7 uniform optionality (any datatype, including string/binary, can use Bit 7 = 0 with implicit N=1); null-terminator now version-dependent (OSF4 may, OSF5 must not, readers always strip if present); multi-sample variable-length blocks documented as non-standard |
| `b69992b` | BACKLOG entry — Delphi multi-sample string/binary uses non-spec per-sample uint32-length-prefix layout (latent incompatibility, fix planned in Delphi maintenance pass) |

**Phase 7a architecture decisions taken so far** (six of seven
questions answered; Q5 my proposal awaiting user response; Q6 + Q7
+ Header-Outline pending):

Phase 7 is split into four sub-phases:
- **7a (current):** private block-encoder library, header-only
  with explicit instantiations, stateless free functions.
  Shared by 7b + 7c. **Design-only in this session, no code.**
- **7b:** `StreamingWriter` (embedded, sample-by-sample,
  immediate flush).
- **7c:** `BlockWriter` (analyst, accumulates, writes at end).
- **7d:** `StaleValueGuard` (optional 100-second-repeat layer
  over `StreamingWriter` for timestamped channels).

**Q1 — Header location:** `src/block_encode.hpp` + `src/block_encode.cpp`.
Private to `osf_core` target, NOT in `include/osf/`. Strict
encapsulation, mirrors Rust `pub(crate) binary_write.rs`.
Test access via `target_include_directories(test_block_encode
PRIVATE ../src)`. Namespace: `osf::detail`.

**Q2 — Equidistant naming:** `encode_start_data` and
`encode_continued_data`. Snake_case of `BlockKind` variants
`StartData` / `ContinuedData`, no `_block` suffix.

**Q3 — Timestamped naming:** `encode_abs_timestamp_data`.
Same rule applied to `AbsTimestampData` variant.
Note: `bcContinuedRelStampData` is read-only — no encoder
function needed (writer never produces it, per Rust precedent).

**Q3.5 — Overload pattern instead of suffix-named functions:**
Six total symbol names in the encoder:
- `encode_start_data<T>` — 11 numeric template instantiations
- `encode_continued_data<T>` — 11 numeric template instantiations
- `encode_abs_timestamp_data<T>` — 11 numeric template
  instantiations (Bool, Int8/16/32/64, UInt8/16/32/64, Float,
  Double)
- `encode_abs_timestamp_data(... std::string_view)` —
  string overload, single-sample signature
- `encode_abs_timestamp_data(... BinarySample)` —
  binary overload, single-sample signature
- `encode_abs_timestamp_data_gps` — own function for GPS
  (not template-with-`if constexpr` — cleaner separation,
  symbol-greppable)

`BinarySample` is a tiny non-owning view struct (C++17 substitute
for `std::span<uint8_t const>`). Explicit constructor only, NO
implicit conversion from `std::vector<uint8_t>` (lifetime trap —
temporary would die at statement end). Static factory
`BinarySample::from_vector(v)` for ergonomic call sites.

**Q4 — Multi-sample selection:** Automatic by `count` parameter.
With the spec clarification in `4bad442` applied uniformly:
- `count == 0` → Error
- `count == 1` → Bit 7 = 0, no `uint32 N`-prefix (write
  optimisation, 4 bytes saved)
- `count > 1` → Bit 7 = 1, `uint32 N`-prefix
String/binary overloads have fixed single-sample signature
(no `count` parameter), emit Bit 7 = 0, no N-prefix, **no
trailing `0x00`** (OSF5-conformant per `4bad442`). GPS function
follows the numeric rule with `count` parameter.

**Q5 — Error handling (PAUSED HERE, awaiting user response):**

My proposal — Option A: all encoder functions return
`Result<void>`. Three error conditions:
- Payload too large for `sizeoflengthvalue` →
  `Error::Code::InvalidBlock` (data-dependent; Writer reacts
  with auto-bump 2 → 4)
- `count == 0` → `Error::Code::InvalidArgument`
  (caller violated the API)
- `sizeoflengthvalue` not 2 or 4 → `Error::Code::InvalidArgument`
  (defensive; should not happen in normal flow)

Rationale: consistency with the rest of the `Result<T>`-based API,
Writer needs the error visibility for auto-bump logic, single
unified error path beats split between assert + Result.

**Q6 — Endianness discipline (pending):** Likely Option A —
own `write_le_u16` / `write_le_u32` / `write_le_u64` /
`write_le_f32` / `write_le_f64` / `write_le_i*` helpers,
manual byte-shifts (or `std::memcpy` for floats). Lives in
`src/binary_io.hpp` as file-private implementation (mirror of
Rust `binary_write.rs`). If `src/binary_io.hpp` already exists
for the reader side, reuse it; otherwise create it new.

**Q7 — Test strategy (pending):** Likely Option A —
`tests/unit/test_block_encode.cpp` with byte-exact assertions
on the output `std::vector<std::uint8_t>`. Direct
spec-conformance verification at the bit-pattern level.

**After Q7:** Header-file outline for `src/block_encode.hpp` —
function signatures plus brief Doxygen comments, no
implementation bodies. Outline is design material, NOT committed.

**Implementation-phase notes (for the eventual 7a-impl session):**

1. **Doxygen file-header in `src/block_encode.hpp`** should
   document the C++-specific architecture divergence from Rust:
   "This module is C++-specific. Rust embeds encode logic into
   writer.rs directly. The split here serves the two-writer-
   classes architecture documented in DECISIONS.md §7."
2. **Simpler encoder logic post-spec-clarification.** No
   type-specific Bit-7-quirks, no null-terminator suffix on
   string/binary. The Rust writer has a `write_variable_header`
   helper that emits `0x88 + u32 1` even for single-sample
   string/binary — the C++ encoder doesn't need that branch.
3. **Cross-implementation wire validation.** The Java
   implementation runs in a parallel chat; Java was synced with
   the same spec clarifications. C++ output should match Java
   output bit-for-bit for round-trip tests once both writers
   land.

**Phase 7a-implementation prerequisites (all done):**
- Spec clarification committed (`4bad442`) ✓
- Delphi backlog entry committed (`b69992b`) ✓
- §7 revision committed (`6f386a9`) ✓

**Pending in the design discussion before implementation:**
- Q5 user response
- Q6 user response
- Q7 user response
- Header-file outline (no commit)

**How to resume:**

1. Read this file.
2. Read `BACKLOG.md` (new — context for parked items).
3. Read `DECISIONS.md` §7 + §20 + §21 (§7 revised, §21 new).
4. Re-establish the architecture discussion in a fresh chat, or
   continue in the paused one. Q5 proposal is on the table; user's
   answer triggers Q6.
5. Working tree is clean (only `.idea/` untracked, normal).

Rust references for the encoder:
- `implementations/rust/osf-core/src/binary_write.rs` (LE helpers)
- `implementations/rust/osf-core/src/writer.rs` (block composition)
