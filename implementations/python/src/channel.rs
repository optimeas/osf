// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

//! `PyChannel`, `PySegment`, `PyStats` — Python wrappers for the
//! manager-side typed channel and the read-time stats.
//!
//! Every wrapper owns its inner Rust value (`Channel`, `Segment`,
//! `ReaderStats`) so the Python side can hold them past the lifetime
//! of the source `DataManager`. Cloning a `Channel` clones its
//! sample vector once; for the typical channel sizes shipped with
//! the reference set (~10 KB to ~150 KB) the cost is unmeasurable
//! against the surrounding NumPy work. Profiling can graduate to
//! `Arc<Channel>` later if a real workload demands it.

use crate::numpy_convert::{
    binaries_to_pylist, numeric_values_to_pyobject, strings_to_pylist, timestamps_ns_to_pyobject,
};
use osf_core::data_channel::{Channel, EquidistantChannel};
use osf_core::stats::{CompressionFormat, ReaderStats};
use osf_core::types::DataType;
use pyo3::prelude::*;

/// One equidistant segment, exposed as `osf.Segment`.
#[pyclass(name = "Segment", module = "osf")]
#[derive(Clone)]
pub struct PySegment {
    /// Absolute start timestamp of this segment in nanoseconds.
    #[pyo3(get)]
    pub start_timestamp_ns: i64,
    /// Sample rate in Hz, valid until the next segment of this
    /// channel.
    #[pyo3(get)]
    pub sample_rate_hz: f64,
    /// Number of samples belonging to this segment.
    #[pyo3(get)]
    pub sample_count: usize,
}

#[pymethods]
impl PySegment {
    fn __repr__(&self) -> String {
        format!(
            "Segment(start_timestamp_ns={}, sample_rate_hz={}, sample_count={})",
            self.start_timestamp_ns, self.sample_rate_hz, self.sample_count
        )
    }
}

/// Typed in-memory channel, exposed as `osf.Channel`.
#[pyclass(name = "Channel", module = "osf")]
pub struct PyChannel {
    pub(crate) inner: Channel,
}

#[pymethods]
impl PyChannel {
    /// Channel index from the metablock.
    #[getter]
    fn index(&self) -> u16 {
        self.inner.index()
    }

    /// Fully qualified channel name.
    #[getter]
    fn name(&self) -> &str {
        self.inner.name()
    }

    /// Datatype as the on-disk wire spelling: `"double"`, `"int32"`,
    /// `"string"`, `"binary"`, `"gpslocation"`, …
    #[getter]
    fn data_type(&self) -> &'static str {
        data_type_to_str(&self.inner.data_type())
    }

    /// Storage classification: `"equidistant"`, `"timestamped"`, or
    /// `"variable"`. This is how the manager grouped the channel,
    /// not the on-disk `channeltype` attribute (which is usually
    /// `"scalar"` for everything).
    #[getter]
    fn channel_type(&self) -> &'static str {
        match &self.inner {
            Channel::Equidistant(_) => "equidistant",
            Channel::Timestamped(_) => "timestamped",
            Channel::Variable(_) => "variable",
        }
    }

    /// Total sample count across all segments (for equidistant).
    #[getter]
    fn sample_count(&self) -> usize {
        self.inner.sample_count()
    }

    /// Optional physical-unit string (e.g. `"°C"`, `"bar"`).
    #[getter]
    fn physical_unit(&self) -> Option<String> {
        self.inner.physical_unit().map(str::to_string)
    }

    /// Optional display name.
    #[getter]
    fn display_name(&self) -> Option<String> {
        self.inner.display_name().map(str::to_string)
    }

    /// `True` when the channel holds zero samples.
    #[getter]
    fn is_empty(&self) -> bool {
        self.inner.is_empty()
    }

    /// Equidistant-segment list. Empty list for non-equidistant
    /// channels.
    #[getter]
    fn segments(&self) -> Vec<PySegment> {
        match &self.inner {
            Channel::Equidistant(eq) => eq
                .segments()
                .iter()
                .map(|s| PySegment {
                    start_timestamp_ns: s.start_timestamp_ns,
                    sample_rate_hz: s.sample_rate_hz,
                    sample_count: s.sample_count,
                })
                .collect(),
            _ => Vec::new(),
        }
    }

    /// Sample values as a NumPy array (numeric / GPS) or a Python
    /// list (string / binary).
    fn samples(&self, py: Python<'_>) -> PyObject {
        match &self.inner {
            Channel::Equidistant(eq) => numeric_values_to_pyobject(py, eq.values()),
            Channel::Timestamped(ts) => numeric_values_to_pyobject(py, ts.values()),
            Channel::Variable(var) => {
                if let Ok(strings) = var.as_strings() {
                    strings_to_pylist(py, strings)
                } else if let Ok(binaries) = var.as_binaries() {
                    binaries_to_pylist(py, binaries)
                } else {
                    py.None()
                }
            }
        }
    }

    /// `int64` NumPy array of per-sample timestamps in nanoseconds.
    /// For equidistant channels the timestamps are reconstructed
    /// from segments on the fly; for timestamped / variable channels
    /// the stored timestamps are returned directly.
    fn timestamps_ns(&self, py: Python<'_>) -> PyObject {
        match &self.inner {
            Channel::Equidistant(eq) => {
                let ts = equidistant_timestamps(eq);
                timestamps_ns_to_pyobject(py, &ts)
            }
            Channel::Timestamped(ts) => timestamps_ns_to_pyobject(py, ts.timestamps_ns()),
            Channel::Variable(var) => timestamps_ns_to_pyobject(py, var.timestamps_ns()),
        }
    }

    fn __repr__(&self) -> String {
        format!(
            "Channel(index={}, name={:?}, data_type={:?}, channel_type={:?}, sample_count={})",
            self.inner.index(),
            self.inner.name(),
            data_type_to_str(&self.inner.data_type()),
            match &self.inner {
                Channel::Equidistant(_) => "equidistant",
                Channel::Timestamped(_) => "timestamped",
                Channel::Variable(_) => "variable",
            },
            self.inner.sample_count(),
        )
    }
}

/// `ReaderStats` wrapper — exposes the read-side telemetry to Python.
#[pyclass(name = "ReaderStats", module = "osf")]
pub struct PyStats {
    pub(crate) inner: ReaderStats,
}

#[pymethods]
impl PyStats {
    #[getter]
    fn compressed(&self) -> bool {
        self.inner.compressed
    }

    #[getter]
    fn compression_format(&self) -> Option<&'static str> {
        match self.inner.compression_format {
            CompressionFormat::None => None,
            CompressionFormat::Zlib => Some("zlib"),
            CompressionFormat::Gzip => Some("gzip"),
        }
    }

    #[getter]
    fn channels_total(&self) -> usize {
        self.inner.channels_total
    }

    #[getter]
    fn channels_with_data(&self) -> usize {
        self.inner.channels_with_data
    }

    #[getter]
    fn blocks_total(&self) -> u64 {
        self.inner.blocks_total
    }

    #[getter]
    fn blocks_read(&self) -> u64 {
        self.inner.blocks_read
    }

    #[getter]
    fn blocks_truncated(&self) -> u64 {
        self.inner.blocks_truncated
    }

    #[getter]
    fn elapsed_ms(&self) -> f64 {
        self.inner.elapsed.as_secs_f64() * 1000.0
    }

    #[getter]
    fn file_size_bytes(&self) -> Option<u64> {
        self.inner.file_size_bytes
    }

    #[getter]
    fn header_size_bytes(&self) -> u64 {
        self.inner.header_size_bytes
    }

    #[getter]
    fn metablock_size_bytes(&self) -> u64 {
        self.inner.metablock_size_bytes
    }

    #[getter]
    fn data_section_size_bytes(&self) -> u64 {
        self.inner.data_section_size_bytes
    }

    #[getter]
    fn trailer_seen(&self) -> bool {
        self.inner.trailer_seen
    }

    fn __repr__(&self) -> String {
        format!(
            "ReaderStats(channels_total={}, blocks_total={}, elapsed_ms={:.2}, compressed={})",
            self.inner.channels_total,
            self.inner.blocks_total,
            self.inner.elapsed.as_secs_f64() * 1000.0,
            self.inner.compressed,
        )
    }

    fn __str__(&self) -> String {
        format!("{}", self.inner)
    }
}

/// Reconstruct per-sample timestamps for an equidistant channel,
/// stitching every segment's run into a single flat `Vec<i64>`.
fn equidistant_timestamps(eq: &EquidistantChannel) -> Vec<i64> {
    let total = eq.values().len();
    let mut out = Vec::with_capacity(total);
    for seg in eq.segments() {
        for i in 0..seg.sample_count {
            let ts = if seg.sample_rate_hz > 0.0 && i > 0 {
                let offset = ((i as f64) * 1.0e9 / seg.sample_rate_hz) as i64;
                seg.start_timestamp_ns.saturating_add(offset)
            } else {
                seg.start_timestamp_ns
            };
            out.push(ts);
        }
    }
    out
}

fn data_type_to_str(dt: &DataType) -> &'static str {
    match dt {
        DataType::Bool => "bool",
        DataType::Int8 => "int8",
        DataType::Int16 => "int16",
        DataType::Int32 => "int32",
        DataType::Int64 => "int64",
        DataType::UInt8 => "uint8",
        DataType::UInt16 => "uint16",
        DataType::UInt32 => "uint32",
        DataType::UInt64 => "uint64",
        DataType::Float => "float",
        DataType::Double => "double",
        DataType::String => "string",
        DataType::Binary | DataType::ByteArray => "binary",
        DataType::GpsLocation => "gpslocation",
        DataType::Unsupported(_) => "unsupported",
    }
}
