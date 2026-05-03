# OSF — Integrations

This directory contains bridges between OSF and existing data-science and AI ecosystems.

## Implementations vs. Integrations

| Concept | What it provides | Where to look |
|---|---|---|
| **Implementation** | A standalone OSF reader/writer for a specific programming language | `implementations/` |
| **Integration** | A bridge that connects OSF to an existing framework or ecosystem | `integrations/` (this directory) |

An implementation lets you read and write OSF files in your language of choice. An integration lets an existing framework — a training loop, a query engine, an LLM — consume OSF data through the framework's native interface, without requiring callers to know anything about the OSF format.

## Available Integrations

| Integration | Ecosystem | Description | Status |
|---|---|---|---|
| [arrow](arrow/) | Apache Arrow / Parquet | Convert OSF channels to Arrow RecordBatches; read with DuckDB, Polars, HuggingFace | Planned |
| [pytorch](pytorch/) | PyTorch | `OSFDataset` implementing `torch.utils.data.Dataset` for training pipelines | Planned |
| [tensorflow](tensorflow/) | TensorFlow | `tf.data` connector for streaming OSF data into TensorFlow training workflows | Planned |
| [mcp](mcp/) | Model Context Protocol | MCP server that lets LLMs (Claude, GPT-4, etc.) read and reason about OSF files | Planned |
| [langchain](langchain/) | LangChain / LlamaIndex | Document Loader that exposes OSF time-series data as documents for RAG pipelines | Planned |

## Common Dependency

All integrations depend on the Python OSF implementation (`implementations/python/`). That package must be installed before any integration can be used.
