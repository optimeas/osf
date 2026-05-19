// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

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
