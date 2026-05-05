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

//! `inspect` — minimal CLI that prints what `osf-core` knows about a file.
//!
//! Usage:
//!
//! ```text
//! cargo run --example inspect -- <path-to-osf-file>
//! ```
//!
//! In this revision the example only reports the magic header. Metablock
//! and block-stream inspection follow in later sessions.

use osf_core::parse_magic_header;
use std::env;
use std::fs::File;
use std::io::BufReader;
use std::process::ExitCode;

fn main() -> ExitCode {
    let mut args = env::args().skip(1);
    let Some(path) = args.next() else {
        eprintln!("usage: inspect <path-to-osf-file>");
        return ExitCode::from(2);
    };

    let file = match File::open(&path) {
        Ok(f) => f,
        Err(e) => {
            eprintln!("cannot open {path}: {e}");
            return ExitCode::FAILURE;
        }
    };

    let mut reader = BufReader::new(file);
    match parse_magic_header(&mut reader) {
        Ok(header) => {
            println!("path:           {path}");
            println!("version:        {:?}", header.version);
            println!("metablock_len:  {} bytes", header.metablock_len);
            ExitCode::SUCCESS
        }
        Err(e) => {
            eprintln!("{path}: {e}");
            ExitCode::FAILURE
        }
    }
}
