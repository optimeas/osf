// Copyright 2026 Optimeas GmbH
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

//! Conversion helpers from `osf-core` typed payloads to NumPy arrays.
//!
//! Strategy is "clone-then-move": every helper clones the underlying
//! `Vec<T>` and hands the clone to `IntoPyArray`, which then owns the
//! buffer. The clone makes repeated calls (`samples()` called twice
//! on the same channel) work without surprises; profiling can replace
//! it with a single-shot consumer later if a real workload calls for
//! it.

use numpy::IntoPyArray;
use numpy::ndarray::Array2;
use osf_core::data_channel::NumericValues;
use pyo3::prelude::*;
use pyo3::types::{PyBytes, PyList};

/// Build a NumPy array from a [`NumericValues`] slice. The returned
/// shape depends on the variant:
///
/// - All scalar numeric types and `bool` → `ndarray.shape == (N,)`,
///   matching the Rust storage exactly.
/// - `GpsLocation` → `ndarray.shape == (N, 3)` with columns
///   `[latitude, longitude, altitude]`. Structured-array layouts are
///   error-prone for NumPy newcomers; a 2D float array is what most
///   downstream code (pandas, plotting libraries) expects.
pub(crate) fn numeric_values_to_pyobject(py: Python<'_>, values: &NumericValues) -> PyObject {
    match values {
        NumericValues::Bool(v) => v.clone().into_pyarray_bound(py).into_py(py),
        NumericValues::Int8(v) => v.clone().into_pyarray_bound(py).into_py(py),
        NumericValues::Int16(v) => v.clone().into_pyarray_bound(py).into_py(py),
        NumericValues::Int32(v) => v.clone().into_pyarray_bound(py).into_py(py),
        NumericValues::Int64(v) => v.clone().into_pyarray_bound(py).into_py(py),
        NumericValues::UInt8(v) => v.clone().into_pyarray_bound(py).into_py(py),
        NumericValues::UInt16(v) => v.clone().into_pyarray_bound(py).into_py(py),
        NumericValues::UInt32(v) => v.clone().into_pyarray_bound(py).into_py(py),
        NumericValues::UInt64(v) => v.clone().into_pyarray_bound(py).into_py(py),
        NumericValues::Float(v) => v.clone().into_pyarray_bound(py).into_py(py),
        NumericValues::Double(v) => v.clone().into_pyarray_bound(py).into_py(py),
        NumericValues::GpsLocation(v) => {
            let mut arr = Array2::<f64>::zeros((v.len(), 3));
            for (i, g) in v.iter().enumerate() {
                arr[[i, 0]] = g.latitude;
                arr[[i, 1]] = g.longitude;
                arr[[i, 2]] = g.altitude;
            }
            arr.into_pyarray_bound(py).into_py(py)
        }
    }
}

/// Build a NumPy `int64` array from a slice of timestamps in
/// nanoseconds.
pub(crate) fn timestamps_ns_to_pyobject(py: Python<'_>, ts: &[i64]) -> PyObject {
    ts.to_vec().into_pyarray_bound(py).into_py(py)
}

/// Build a Python `list[str]` from a slice of strings.
pub(crate) fn strings_to_pylist(py: Python<'_>, strings: &[String]) -> PyObject {
    let list = PyList::new_bound(py, strings.iter().map(String::as_str));
    list.into_py(py)
}

/// Build a Python `list[bytes]` from a slice of binary payloads.
pub(crate) fn binaries_to_pylist(py: Python<'_>, binaries: &[Vec<u8>]) -> PyObject {
    let list = PyList::new_bound(py, binaries.iter().map(|b| PyBytes::new_bound(py, b)));
    list.into_py(py)
}
