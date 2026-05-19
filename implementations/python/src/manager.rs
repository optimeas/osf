// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

//! `PyDataManager` — Python wrapper around `osf_core::DataManager`,
//! plus the top-level `osf.load(path)` convenience.
//!
//! Channel access methods clone the corresponding `Channel` out of
//! the manager for hand-off to Python — see channel.rs for the
//! lifetime rationale.

use crate::channel::{PyChannel, PyStats};
use crate::error::convert_error;
use osf_core::DataManager;
use pyo3::prelude::*;
use std::path::PathBuf;

/// `osf.DataManager` — read-only, in-memory view of a parsed OSF file.
///
/// Construct via [`load`]. The underlying `DataManager` lives behind
/// the wrapper; channel access methods clone the relevant
/// `Channel` for hand-off to Python.
#[pyclass(name = "DataManager", module = "osf")]
pub struct PyDataManager {
    pub(crate) inner: DataManager,
}

#[pymethods]
impl PyDataManager {
    /// Look up a channel by its fully qualified name. Returns `None`
    /// if no channel by that name exists. Channel-by-name access is
    /// the documented mandatory form per DECISIONS §10.
    fn channel(&self, name: &str) -> Option<PyChannel> {
        self.inner
            .channel(name)
            .cloned()
            .map(|c| PyChannel { inner: c })
    }

    /// Look up a channel by its on-disk index. Returns `None` if no
    /// channel uses that index. Optional access form per DECISIONS §10.
    fn channel_by_index(&self, index: u16) -> Option<PyChannel> {
        self.inner
            .channel_by_index(index)
            .cloned()
            .map(|c| PyChannel { inner: c })
    }

    /// Full list of channels in metablock order.
    #[getter]
    fn channels(&self) -> Vec<PyChannel> {
        self.inner
            .channels()
            .iter()
            .cloned()
            .map(|c| PyChannel { inner: c })
            .collect()
    }

    /// Read-time statistics: file/section sizes, elapsed time,
    /// compression status, per-channel counters.
    #[getter]
    fn stats(&self) -> PyStats {
        PyStats {
            inner: self.inner.stats.clone(),
        }
    }

    fn __len__(&self) -> usize {
        self.inner.channels().len()
    }

    fn __repr__(&self) -> String {
        format!(
            "DataManager(channels={}, compressed={})",
            self.inner.channels().len(),
            self.inner.stats.compressed,
        )
    }
}

/// `osf.load(path)` — open an OSF file (plain or OSFZ) and assemble
/// the `DataManager`. Detects gzip / zlib compression transparently.
///
/// Releases the GIL during file I/O and parsing so other Python
/// threads stay responsive on large files.
#[pyfunction]
#[pyo3(name = "load")]
pub fn py_load(py: Python<'_>, path: &str) -> PyResult<PyDataManager> {
    let path = PathBuf::from(path);
    let mgr = py
        .allow_threads(|| DataManager::load_from_file(&path))
        .map_err(convert_error)?;
    Ok(PyDataManager { inner: mgr })
}
