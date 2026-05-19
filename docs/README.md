# OSF Specification Documents

The formal specification for the Open Streaming Format is maintained in two
languages. English is the default; the German version is kept in sync.

| Language | Path |
|----------|------|
| English (default) | [`en/`](en/) |
| German            | [`de/`](de/) |

Each language directory follows the same layout:

| Path | Description |
|------|-------------|
| `index.md` | Format introduction and design overview |
| `osf_general.md` | Concepts common to all OSF versions: channel types, data block structure, time models, and design rationale |
| `examples/` | Example files and usage walkthroughs |
| `references/osf4.md` | OSF4 specification — XML metadata header, control byte layout, trailer structure |
| `references/osf5.md` | OSF5 specification — JSON metadata header, simplified control byte, no trailer, backward compatibility with OSF4 |
| `references/osf_vector_matrix.md` | Vector and matrix channel types (OSF5) |
| `media/` | Shared images for the documents in this language |

---

Specification documents are maintained by Optimeas GmbH and are licensed under the MIT License.
To propose a change to the specification, open an issue on GitHub with the label `specification`.
