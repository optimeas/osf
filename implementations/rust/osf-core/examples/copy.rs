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

//! `copy` — load an OSF file via [`DataManager`] and write it back as
//! OSF5 via [`writer::write_to_file`]. Always emits OSF5 even if the
//! source was OSF4, per DECISIONS §6.
//!
//! Usage:
//!
//! ```text
//! cargo run --example copy -- <input.osf> <output.osf>
//! ```

use osf_core::{Channel, DataManager, writer};
use std::env;
use std::process::ExitCode;
use std::time::Instant;

fn main() -> ExitCode {
    env_logger::Builder::from_env(env_logger::Env::default().default_filter_or("warn"))
        .format_timestamp(None)
        .init();

    let mut args = env::args().skip(1);
    let (Some(src), Some(dst)) = (args.next(), args.next()) else {
        eprintln!("usage: copy <input.osf> <output.osf>");
        return ExitCode::from(2);
    };

    let load_start = Instant::now();
    let mgr = match DataManager::load_from_file(&src) {
        Ok(m) => m,
        Err(e) => {
            eprintln!("{src}: load failed: {e}");
            return ExitCode::FAILURE;
        }
    };
    let load_time = load_start.elapsed();

    println!("Source:          {src}");
    println!("Channels:        {}", mgr.channels().len());
    println!(
        "Total samples:   {}",
        mgr.channels()
            .iter()
            .map(Channel::sample_count)
            .sum::<usize>()
    );
    println!("Load time:       {} ms", load_time.as_millis());

    let write_start = Instant::now();
    if let Err(e) = writer::write_to_file(&mgr, &dst) {
        eprintln!("{dst}: write failed: {e}");
        return ExitCode::FAILURE;
    }
    let write_time = write_start.elapsed();

    let written_size = std::fs::metadata(&dst).map(|m| m.len()).unwrap_or(0);
    println!("Target:          {dst}");
    println!("Written size:    {} bytes", written_size);
    println!("Write time:      {} ms", write_time.as_millis());

    // Internal roundtrip-check: reload the written file and report
    // sample counts side-by-side. Any mismatch signals a writer bug.
    let verify_start = Instant::now();
    match DataManager::load_from_file(&dst) {
        Ok(reload) => {
            let verify_time = verify_start.elapsed();
            let ok = reload.channels().len() == mgr.channels().len()
                && mgr
                    .channels()
                    .iter()
                    .zip(reload.channels())
                    .all(|(a, b)| {
                        a.name() == b.name() && a.sample_count() == b.sample_count()
                    });
            println!(
                "Verify reload:   {} channels, {} ms ({})",
                reload.channels().len(),
                verify_time.as_millis(),
                if ok { "ok" } else { "MISMATCH" }
            );
            if !ok {
                return ExitCode::FAILURE;
            }
        }
        Err(e) => {
            eprintln!("{dst}: verify reload failed: {e}");
            return ExitCode::FAILURE;
        }
    }

    ExitCode::SUCCESS
}
