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

//! `osf-core` — core library for the Open Streaming Format (OSF).
//!
//! This crate is the Rust foundation for OSF tooling. It provides parsing,
//! reading, and (eventually) writing of OSF4 and OSF5 files. The Python
//! bindings in `implementations/python/` build on top of this crate via
//! PyO3 (see DECISIONS.md §18).
//!
//! Current revision implements:
//! - Magic-header detection (OSF4 vs. OSF5, including legacy identifiers).
//! - Shared metablock data model and validation helpers
//!   ([`MetaBlock`], [`parse_data_type`], [`parse_channel_type`]).
//!
//! OSF4 (XML) and OSF5 (JSON) metablock parsers, block-stream reading,
//! and writing follow in subsequent sessions.

pub mod error;
pub mod header;
pub mod meta;
pub mod meta_json;
pub mod types;

pub use error::OsfError;
pub use header::{MagicHeader, OsfVersion, parse_magic_header};
pub use meta::{
    Channel, FileInfo, Info, MetaBlock, SpectrumType, parse_channel_type, parse_data_type,
};
pub use meta_json::parse_metablock_json;
pub use types::{BlockContent, ChannelType, DataType};
