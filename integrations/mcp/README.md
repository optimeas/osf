# OSF — Model Context Protocol (MCP) Integration

![Status](https://img.shields.io/badge/status-planned-lightgrey.svg)

## What This Integration Provides

An MCP server that exposes OSF files as resources and tools, enabling LLMs such as Claude to read, summarize, and reason about time-series measurement data without custom tooling on the client side.

**Resources exposed:**

- OSF file metadata (channels, sample rates, duration, data types)
- Channel data as structured JSON or CSV snippets
- Statistical summaries (min, max, mean, stddev per channel per time window)

**Tools exposed:**

- `osf_info` — return the header and channel list of an OSF file
- `osf_read_window` — read a time window from one or more channels
- `osf_statistics` — compute statistics for a channel over a time range
- `osf_search` — find files matching metadata criteria in a directory

## Status

**Planned.** Implementation has not started.

## Dependencies

- `osf` Python package (`implementations/python/`)
- `mcp` Python SDK (`pip install mcp`)
- Python 3.10+

## Planned Usage

```bash
# Start the MCP server, exposing a directory of OSF files
osf-mcp-server --root /data/recordings --port 5173
```

Configure Claude Desktop or any MCP-compatible client to connect to `http://localhost:5173`. Claude can then answer questions such as:

- "What channels are in run_2026-04-15.osf5?"
- "Show me the temperature trend for the first 60 seconds of this recording."
- "Which recording has the highest peak vibration amplitude?"

## Notes

The MCP integration is the primary interface between OSF data and AI workflows. It requires no changes to the LLM or the client application — OSF files become first-class context that any MCP-aware model can inspect and reason about.
