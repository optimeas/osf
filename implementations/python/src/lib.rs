// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

//! PyO3 bindings for `osf-core`.
//!
//! The native module is loaded as `osf._osf`; `python/osf/__init__.py`
//! re-exports the public surface. The PyPI distribution name is
//! `osfdata`, but the Python import name stays `osf`.

use pyo3::prelude::*;

mod channel;
mod error;
mod manager;
mod numpy_convert;
mod writer;

use channel::{PyChannel, PySegment, PyStats};
use error::OsfError;
use manager::{PyDataManager, py_load};
use writer::{PyWriterBuilder, py_save};

#[pymodule]
fn _osf(m: &Bound<'_, PyModule>) -> PyResult<()> {
    m.add("__version__", env!("CARGO_PKG_VERSION"))?;
    m.add("OsfError", m.py().get_type_bound::<OsfError>())?;
    m.add_class::<PyDataManager>()?;
    m.add_class::<PyChannel>()?;
    m.add_class::<PySegment>()?;
    m.add_class::<PyStats>()?;
    m.add_class::<PyWriterBuilder>()?;
    m.add_function(wrap_pyfunction!(py_load, m)?)?;
    m.add_function(wrap_pyfunction!(py_save, m)?)?;
    Ok(())
}
