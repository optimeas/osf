// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

//! `stats` — read an OSF file end-to-end and print [`ReaderStats`]
//! plus the top-N channels by sample count.
//!
//! Usage:
//!
//! ```text
//! cargo run --example stats -- <path-to-osf-file> [top_n]
//! ```
//!
//! Example output for an OSF4 field file:
//!
//! ```text
//! File:            ../../examples/steam_loco.osf
//! File size:       12.4 MB
//! Header:          24 B
//! Metablock:       25.66 KB
//! Data section:    12.36 MB
//! Read in:         450 ms
//!
//! Channels total:        123
//! With data:             123
//! Unsupported:           0
//!
//! Blocks total:          15234
//! Read:                  15234
//! Skipped (unsupp.):     0
//! Skipped (deprec.):     0
//! Skipped (status ev.):  0
//! Skipped (reserved):    0
//! Skipped (zero-len):    0
//! Truncated:             0
//!
//! Top 10 channels by sample count:
//!    index  name                                  samples       bytes  segments  time range (ns)
//!    -----  ----------------------------------    --------  --------  --------  -----------------
//!       42  Engine/RPM                                4500    36.0 KB         3  …
//! ```

use osf_core::ChannelStats;
use std::env;
use std::process::ExitCode;

fn main() -> ExitCode {
    env_logger::Builder::from_env(env_logger::Env::default().default_filter_or("warn"))
        .format_timestamp(None)
        .init();

    let mut args = env::args().skip(1);
    let Some(path) = args.next() else {
        eprintln!("usage: stats <path-to-osf-file> [top_n]");
        return ExitCode::from(2);
    };
    let top_n: usize = args
        .next()
        .as_deref()
        .map(str::parse)
        .transpose()
        .unwrap_or(Some(10))
        .unwrap_or(10);

    let p = std::path::Path::new(&path);
    let (_meta, _blocks, stats) = match osf_core::read_file(p) {
        Ok(v) => v,
        Err(e) => {
            eprintln!("{path}: {e}");
            return ExitCode::FAILURE;
        }
    };

    println!("File:            {path}");
    print!("{stats}");
    println!();
    print_top_channels(&stats.per_channel, top_n);

    ExitCode::SUCCESS
}

fn print_top_channels(per_channel: &std::collections::HashMap<u16, ChannelStats>, top_n: usize) {
    if per_channel.is_empty() {
        return;
    }

    let mut sorted: Vec<(u16, &ChannelStats)> =
        per_channel.iter().map(|(k, v)| (*k, v)).collect();
    sorted.sort_by(|a, b| {
        b.1.samples_total
            .cmp(&a.1.samples_total)
            .then_with(|| a.0.cmp(&b.0))
    });
    let take = top_n.min(sorted.len());

    println!("Top {take} channels by sample count:");
    println!(
        "   {index:>5}  {name:<40}  {samples:>10}  {bytes:>9}  {segments:>8}  time range (ns)",
        index = "index",
        name = "name",
        samples = "samples",
        bytes = "bytes",
        segments = "segments",
    );
    println!(
        "   {dash5:->5}  {dash40:-<40}  {dash10:->10}  {dash9:->9}  {dash8:->8}  {dash20:->20}",
        dash5 = "",
        dash40 = "",
        dash10 = "",
        dash9 = "",
        dash8 = "",
        dash20 = "",
    );
    for (idx, cs) in sorted.into_iter().take(take) {
        let name = if cs.name.chars().count() > 40 {
            let mut s: String = cs.name.chars().take(39).collect();
            s.push('…');
            s
        } else {
            cs.name.clone()
        };
        let ts = cs
            .time_range_ns
            .map_or_else(|| "-".to_string(), |(a, b)| format!("{a}..{b}"));
        println!(
            "   {idx:>5}  {name:<40}  {samples:>10}  {bytes:>9}  {segments:>8}  {ts}",
            samples = cs.samples_total,
            bytes = format_bytes(cs.bytes_payload),
            segments = cs.segments,
        );
    }
}

fn format_bytes(b: u64) -> String {
    const KB: u64 = 1024;
    const MB: u64 = 1024 * KB;
    if b >= MB {
        format!("{:.2} MB", b as f64 / MB as f64)
    } else if b >= KB {
        format!("{:.2} KB", b as f64 / KB as f64)
    } else {
        format!("{b} B")
    }
}
