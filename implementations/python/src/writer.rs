// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

//! `PyWriterBuilder` + `osf.save(mgr, path)` — Python wrappers for the
//! OSF5 block writer.
//!
//! The Python builder mirrors the Rust API but with a single `add_*`
//! pair that dispatches over the NumPy array's `dtype` instead of one
//! method per type. This keeps the Python call sites idiomatic
//! (`builder.add_timestamped_samples(idx, ts, values)`) without
//! exposing twelve near-identical methods.

use crate::error::convert_error;
use crate::manager::PyDataManager;
use numpy::{PyArray1, PyArrayMethods};
use osf_core::types::{ChannelType, DataType};
use osf_core::writer::{ChannelDef, WriterBuilder};
use pyo3::exceptions::PyValueError;
use pyo3::prelude::*;

/// `osf.WriterBuilder` — accumulator for OSF5 file construction.
///
/// All builder-style methods return `self` for chaining. `add_*`
/// methods mutate in place and return `None`. `write_to_file`
/// consumes the builder.
#[pyclass(name = "WriterBuilder", module = "osf")]
pub struct PyWriterBuilder {
    inner: Option<WriterBuilder>,
}

#[pymethods]
impl PyWriterBuilder {
    #[new]
    fn new() -> Self {
        Self {
            inner: Some(WriterBuilder::new()),
        }
    }

    /// Set the `creator` field.
    fn creator<'py>(
        mut slf: PyRefMut<'py, Self>,
        value: &str,
    ) -> PyResult<PyRefMut<'py, Self>> {
        let inner = slf.take_inner()?;
        slf.inner = Some(inner.creator(value));
        Ok(slf)
    }

    /// Set the `tag` field.
    fn tag<'py>(mut slf: PyRefMut<'py, Self>, value: &str) -> PyResult<PyRefMut<'py, Self>> {
        let inner = slf.take_inner()?;
        slf.inner = Some(inner.tag(value));
        Ok(slf)
    }

    /// Set the `reason` field.
    fn reason<'py>(
        mut slf: PyRefMut<'py, Self>,
        value: &str,
    ) -> PyResult<PyRefMut<'py, Self>> {
        let inner = slf.take_inner()?;
        slf.inner = Some(inner.reason(value));
        Ok(slf)
    }

    /// Set GPS location (latitude / longitude in decimal degrees,
    /// altitude in meters).
    fn location<'py>(
        mut slf: PyRefMut<'py, Self>,
        lat: f64,
        lon: f64,
        alt: f64,
    ) -> PyResult<PyRefMut<'py, Self>> {
        let inner = slf.take_inner()?;
        slf.inner = Some(inner.location(lat, lon, alt));
        Ok(slf)
    }

    /// Set the namespace separator (default `.`).
    fn namespace_sep<'py>(
        mut slf: PyRefMut<'py, Self>,
        value: &str,
    ) -> PyResult<PyRefMut<'py, Self>> {
        let inner = slf.take_inner()?;
        slf.inner = Some(inner.namespace_sep(value));
        Ok(slf)
    }

    /// Set the free-form `comment` field.
    fn comment<'py>(
        mut slf: PyRefMut<'py, Self>,
        value: &str,
    ) -> PyResult<PyRefMut<'py, Self>> {
        let inner = slf.take_inner()?;
        slf.inner = Some(inner.comment(value));
        Ok(slf)
    }

    /// Register a channel definition. Returns the channel index that
    /// must be passed to subsequent `add_*` calls.
    #[pyo3(signature = (
        name, data_type, channel_type,
        *,
        physical_unit = None,
        physical_dimension = None,
        display_name = None,
        mime_type = None,
        reference = None,
        comment = None,
        size_of_length_value = 2,
        time_increment_ns = None,
    ))]
    #[allow(clippy::too_many_arguments)]
    fn add_channel(
        &mut self,
        name: String,
        data_type: &str,
        channel_type: &str,
        physical_unit: Option<String>,
        physical_dimension: Option<String>,
        display_name: Option<String>,
        mime_type: Option<String>,
        reference: Option<String>,
        comment: Option<String>,
        size_of_length_value: u8,
        time_increment_ns: Option<i64>,
    ) -> PyResult<u16> {
        let dt = parse_data_type_str(data_type)?;
        let ct = parse_channel_type_str(channel_type)?;
        let def = ChannelDef {
            name,
            data_type: dt,
            channel_type: ct,
            size_of_length_value,
            physical_unit,
            physical_dimension,
            display_name,
            mime_type,
            reference,
            comment,
            time_increment_ns,
            spectrum_type: None,
        };
        let inner = self.inner.as_mut().ok_or_else(builder_consumed)?;
        inner.add_channel(def).map_err(convert_error)
    }

    /// Append an equidistant segment. `values` must be a 1D NumPy
    /// array of `float32` or `float64` (spec rev 2026-05-04 limits
    /// equidistant blocks to those two types).
    fn add_equidistant_segment(
        &mut self,
        channel: u16,
        start_ns: i64,
        sample_rate_hz: f64,
        values: &Bound<'_, PyAny>,
    ) -> PyResult<()> {
        let inner = self.inner.as_mut().ok_or_else(builder_consumed)?;

        if let Ok(arr) = values.downcast::<PyArray1<f64>>() {
            let readonly = arr.readonly();
            let slice = readonly.as_slice()?;
            inner
                .add_equidistant_segment_f64(channel, start_ns, sample_rate_hz, slice)
                .map_err(convert_error)?;
            return Ok(());
        }
        if let Ok(arr) = values.downcast::<PyArray1<f32>>() {
            let readonly = arr.readonly();
            let slice = readonly.as_slice()?;
            inner
                .add_equidistant_segment_f32(channel, start_ns, sample_rate_hz, slice)
                .map_err(convert_error)?;
            return Ok(());
        }
        Err(PyValueError::new_err(
            "add_equidistant_segment expects a 1D float32 or float64 numpy array",
        ))
    }

    /// Append timestamped numeric samples. `timestamps_ns` must be a
    /// 1D `int64` array; `values` may be any of the supported
    /// numeric dtypes; the channel's declared data type decides
    /// which one.
    fn add_timestamped_samples(
        &mut self,
        channel: u16,
        timestamps_ns: &Bound<'_, PyAny>,
        values: &Bound<'_, PyAny>,
    ) -> PyResult<()> {
        let inner = self.inner.as_mut().ok_or_else(builder_consumed)?;
        let ts_array = timestamps_ns
            .downcast::<PyArray1<i64>>()
            .map_err(|_| PyValueError::new_err("timestamps_ns must be a 1D int64 numpy array"))?;
        let ts_readonly = ts_array.readonly();
        let ts_slice = ts_readonly.as_slice()?;

        macro_rules! try_dispatch {
            ($($ty:ty => $method:ident),* $(,)?) => {
                $(
                    if let Ok(arr) = values.downcast::<PyArray1<$ty>>() {
                        let readonly = arr.readonly();
                        let slice = readonly.as_slice()?;
                        return inner
                            .$method(channel, ts_slice, slice)
                            .map_err(convert_error);
                    }
                )*
            };
        }
        try_dispatch! {
            bool => add_timestamped_samples_bool,
            i8 => add_timestamped_samples_i8,
            i16 => add_timestamped_samples_i16,
            i32 => add_timestamped_samples_i32,
            i64 => add_timestamped_samples_i64,
            u8 => add_timestamped_samples_u8,
            u16 => add_timestamped_samples_u16,
            u32 => add_timestamped_samples_u32,
            u64 => add_timestamped_samples_u64,
            f32 => add_timestamped_samples_f32,
            f64 => add_timestamped_samples_f64,
        }

        Err(PyValueError::new_err(
            "add_timestamped_samples expects a 1D numeric numpy array (bool, int8..int64, \
             uint8..uint64, float32, or float64)",
        ))
    }

    /// Append timestamped string samples.
    fn add_string_samples(
        &mut self,
        channel: u16,
        timestamps_ns: &Bound<'_, PyAny>,
        values: Vec<String>,
    ) -> PyResult<()> {
        let inner = self.inner.as_mut().ok_or_else(builder_consumed)?;
        let ts_array = timestamps_ns
            .downcast::<PyArray1<i64>>()
            .map_err(|_| PyValueError::new_err("timestamps_ns must be a 1D int64 numpy array"))?;
        let ts_readonly = ts_array.readonly();
        let ts_slice = ts_readonly.as_slice()?;
        inner
            .add_string_samples(channel, ts_slice, &values)
            .map_err(convert_error)
    }

    /// Append timestamped binary samples (each sample is a `bytes`
    /// object on the Python side).
    fn add_binary_samples(
        &mut self,
        channel: u16,
        timestamps_ns: &Bound<'_, PyAny>,
        values: Vec<Vec<u8>>,
    ) -> PyResult<()> {
        let inner = self.inner.as_mut().ok_or_else(builder_consumed)?;
        let ts_array = timestamps_ns
            .downcast::<PyArray1<i64>>()
            .map_err(|_| PyValueError::new_err("timestamps_ns must be a 1D int64 numpy array"))?;
        let ts_readonly = ts_array.readonly();
        let ts_slice = ts_readonly.as_slice()?;
        inner
            .add_binary_samples(channel, ts_slice, &values)
            .map_err(convert_error)
    }

    /// Serialise the file to `path`. Consumes the builder; subsequent
    /// calls raise `OsfError`.
    fn write_to_file(&mut self, py: Python<'_>, path: &str) -> PyResult<()> {
        let inner = self.inner.take().ok_or_else(builder_consumed)?;
        let path = path.to_string();
        py.allow_threads(|| inner.write_to_file(path))
            .map_err(convert_error)
    }

    fn __repr__(&self) -> String {
        match &self.inner {
            Some(inner) => format!("WriterBuilder(channels={})", inner.channel_count()),
            None => "WriterBuilder(consumed)".to_string(),
        }
    }
}

impl PyWriterBuilder {
    /// Internal helper: take the builder out for builder-style chains.
    /// Re-storing into `slf.inner` is the caller's responsibility.
    fn take_inner(&mut self) -> PyResult<WriterBuilder> {
        self.inner.take().ok_or_else(builder_consumed)
    }
}

fn builder_consumed() -> PyErr {
    crate::error::OsfError::new_err("WriterBuilder already consumed by write_to_file()")
}

fn parse_data_type_str(s: &str) -> PyResult<DataType> {
    Ok(match s {
        "bool" => DataType::Bool,
        "int8" => DataType::Int8,
        "int16" => DataType::Int16,
        "int32" => DataType::Int32,
        "int64" => DataType::Int64,
        "uint8" => DataType::UInt8,
        "uint16" => DataType::UInt16,
        "uint32" => DataType::UInt32,
        "uint64" => DataType::UInt64,
        "float" => DataType::Float,
        "double" => DataType::Double,
        "string" => DataType::String,
        "binary" => DataType::Binary,
        "gpslocation" => DataType::GpsLocation,
        other => {
            return Err(PyValueError::new_err(format!(
                "unknown data_type {other:?}; expected one of bool, int8..int64, \
                 uint8..uint64, float, double, string, binary, gpslocation"
            )));
        }
    })
}

fn parse_channel_type_str(s: &str) -> PyResult<ChannelType> {
    Ok(match s {
        "scalar" => ChannelType::Scalar,
        "equidistant" => ChannelType::Equidistant,
        "timestamped" => ChannelType::Timestamped,
        other => {
            return Err(PyValueError::new_err(format!(
                "unknown channel_type {other:?}; expected scalar, equidistant, or timestamped"
            )));
        }
    })
}

/// `osf.save(mgr, path)` — convenience equivalent to
/// `osf_core::writer::write_to_file(&mgr, path)`. Always emits OSF5
/// per DECISIONS §6, even when `mgr` was loaded from an OSF4 source.
#[pyfunction]
#[pyo3(name = "save")]
pub fn py_save(py: Python<'_>, manager: &PyDataManager, path: &str) -> PyResult<()> {
    let path = path.to_string();
    py.allow_threads(|| osf_core::writer::write_to_file(&manager.inner, path))
        .map_err(convert_error)
}
