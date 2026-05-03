# OSF — LangChain / LlamaIndex Integration

![Status](https://img.shields.io/badge/status-planned-lightgrey.svg)

## What This Integration Provides

A Document Loader that converts OSF time-series data into documents suitable for retrieval-augmented generation (RAG) pipelines:

- **LangChain**: `OSFLoader` implementing `langchain.document_loaders.BaseLoader`
- **LlamaIndex**: `OSFReader` implementing `llama_index.core.readers.BaseReader`
- Configurable chunking strategies: fixed time windows, event boundaries, channel statistics
- Metadata preserved per document: channel name, time range, sample rate, units, file path

## Status

**Planned.** Implementation has not started.

## Dependencies

- `osf` Python package (`implementations/python/`)
- `langchain-core >= 0.1` and/or `llama-index-core >= 0.10`
- `numpy`

## Planned API

### LangChain

```python
from osf.langchain import OSFLoader

loader = OSFLoader(
    path="recording.osf5",
    channels=["temperature", "pressure"],
    window_seconds=60.0,   # one document per 60-second window
    include_statistics=True,
)

docs = loader.load()
# Each doc: page_content = statistical summary, metadata = {channel, t_start, t_end, ...}

vectorstore.add_documents(docs)
```

### LlamaIndex

```python
from osf.llamaindex import OSFReader

reader = OSFReader(window_seconds=60.0, include_statistics=True)
documents = reader.load_data("recording.osf5", channels=["temperature"])
index = VectorStoreIndex.from_documents(documents)
```

## Notes

Time-series data is not naturally expressed as prose documents, so this integration converts channel windows into structured statistical summaries (min, max, mean, trend direction, anomaly flags) that LLMs can reason about and retrieve semantically. Raw sample arrays are not embedded — only human-readable summaries are indexed.
