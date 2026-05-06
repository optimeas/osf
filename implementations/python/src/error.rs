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

//! Error-type bridge from Rust [`osf_core::error::OsfError`] into a
//! single Python exception class.
//!
//! Per the brief we keep a flat hierarchy for now — one `OsfError`
//! Python class derived from `Exception`. Subclasses can be added
//! later if a concrete consumer needs to discriminate. The Display
//! message of the Rust error is preserved verbatim, so the original
//! variant name stays visible in tracebacks (e.g. `"channel 7
//! produced bcContinuedData without a preceding bcStartData"`).

use osf_core::error::OsfError as CoreError;
use pyo3::exceptions::PyException;
use pyo3::prelude::*;

pyo3::create_exception!(osf, OsfError, PyException);

/// Convert a Rust [`CoreError`] into a [`PyErr`] carrying the same
/// message as the Rust `Display` rendering. Used at every binding
/// boundary that may surface a core-library error to Python.
#[allow(dead_code)] // consumed by manager.rs / writer.rs in subsequent commits
pub(crate) fn convert_error(err: CoreError) -> PyErr {
    OsfError::new_err(err.to_string())
}
