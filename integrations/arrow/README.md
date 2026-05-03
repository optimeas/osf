# OSF — Apache Arrow / Parquet Integration

![Status](https://img.shields.io/badge/status-planned-lightgrey.svg)

## What This Integration Provides

A bridge between OSF and the Apache Arrow ecosystem:

- Convert OSF channels to Arrow `RecordBatch` and `Table` objects
- Write OSF files from Arrow tables
- Export OSF data to Parquet files
- Read OSF files via DuckDB SQL queries
- Load OSF channels into Polars DataFrames
- Stream OSF data into HuggingFace `datasets.Dataset`

## Status

**Planned.** Implementation has not started.

## Dependencies

- `osf` Python package (`implementations/python/`)
- `pyarrow >= 12.0`
- Optional: `duckdb`, `polars`, `datasets` (HuggingFace)

## Planned API

```python
import osf.arrow as osf_arrow

# OSF → Arrow Table
table = osf_arrow.read_table("recording.osf5", channels=["temperature", "pressure"])

# Arrow Table → Parquet
osf_arrow.to_parquet("recording.osf5", "recording.parquet")

# OSF → Polars DataFrame
import polars as pl
df = osf_arrow.read_polars("recording.osf5")

# DuckDB: query OSF files directly
import duckdb
duckdb.execute("SELECT * FROM osf_scan('recording.osf5') WHERE channel = 'temperature'")
```

## Notes

Arrow is the integration layer for the entire analytical ecosystem. Once OSF channels are in Arrow format, they are consumable by DuckDB, Polars, pandas, Spark, and HuggingFace without any further conversion. Parquet export provides a permanent columnar representation suitable for long-term archival and batch analytics.
