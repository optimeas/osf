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

use channel::{PyChannel, PySegment, PyStats};
use error::OsfError;
use manager::{PyDataManager, py_load};

#[pymodule]
fn _osf(m: &Bound<'_, PyModule>) -> PyResult<()> {
    m.add("__version__", env!("CARGO_PKG_VERSION"))?;
    m.add("OsfError", m.py().get_type_bound::<OsfError>())?;
    m.add_class::<PyDataManager>()?;
    m.add_class::<PyChannel>()?;
    m.add_class::<PySegment>()?;
    m.add_class::<PyStats>()?;
    m.add_function(wrap_pyfunction!(py_load, m)?)?;
    Ok(())
}
