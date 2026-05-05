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

//! `dump` — load an OSF file via [`DataManager`] and print a
//! per-channel summary.
//!
//! Usage:
//!
//! ```text
//! cargo run --example dump -- <path-to-osf-file> [top_n]
//! ```
//!
//! Default `top_n` is 10. Like the `stats` example, diagnostics flow
//! through `env_logger`; default `RUST_LOG=warn`.

use osf_core::{Channel, DataManager, NumericValueRef, Sample, VariableValueRef};
use std::env;
use std::process::ExitCode;
use std::time::Instant;

fn main() -> ExitCode {
    env_logger::Builder::from_env(env_logger::Env::default().default_filter_or("warn"))
        .format_timestamp(None)
        .init();

    let mut args = env::args().skip(1);
    let Some(path) = args.next() else {
        eprintln!("usage: dump <path-to-osf-file> [top_n]");
        return ExitCode::from(2);
    };
    let top_n: usize = args
        .next()
        .as_deref()
        .map(str::parse)
        .transpose()
        .unwrap_or(Some(10))
        .unwrap_or(10);

    let started = Instant::now();
    let mgr = match DataManager::load_from_file(&path) {
        Ok(m) => m,
        Err(e) => {
            eprintln!("{path}: {e}");
            return ExitCode::FAILURE;
        }
    };
    let load_time = started.elapsed();

    println!("File:            {path}");
    println!(
        "Channels:        {} ({} with data, {} unsupported)",
        mgr.channels().len(),
        mgr.channels().iter().filter(|c| !c.is_empty()).count(),
        mgr.stats.channels_unsupported,
    );
    println!("Load time:       {} ms", load_time.as_millis());
    println!();

    print_top_channels(&mgr, top_n);

    if let Some(first) = mgr.channels().iter().find(|c| !c.is_empty()) {
        println!();
        print_first_channel_detail(first);
    }

    ExitCode::SUCCESS
}

fn print_top_channels(mgr: &DataManager, top_n: usize) {
    if mgr.channels().is_empty() {
        return;
    }

    let mut sorted: Vec<&Channel> = mgr.channels().iter().collect();
    sorted.sort_by(|a, b| {
        b.sample_count()
            .cmp(&a.sample_count())
            .then_with(|| a.index().cmp(&b.index()))
    });
    let take = top_n.min(sorted.len());

    println!("Top {take} channels by sample count:");
    println!(
        "   {idx:>5}  {name:<40}  {kind:<11}  {samples:>10}  {segments:>8}  unit",
        idx = "index",
        name = "name",
        kind = "type",
        samples = "samples",
        segments = "segments",
    );
    println!(
        "   {dash5:->5}  {dash40:-<40}  {dash11:-<11}  {dash10:->10}  {dash8:->8}  ----",
        dash5 = "",
        dash40 = "",
        dash11 = "",
        dash10 = "",
        dash8 = "",
    );
    for chan in sorted.into_iter().take(take) {
        println!(
            "   {idx:>5}  {name:<40}  {kind:<11}  {samples:>10}  {segments:>8}  {unit}",
            idx = chan.index(),
            name = truncate_to(chan.name(), 40),
            kind = channel_kind(chan),
            samples = chan.sample_count(),
            segments = segment_count(chan),
            unit = chan.physical_unit().unwrap_or("-"),
        );
    }
}

fn print_first_channel_detail(chan: &Channel) {
    println!("First channel detail:");
    println!("   name:           {}", chan.name());
    println!("   data type:      {:?}", chan.data_type());
    println!("   sample count:   {}", chan.sample_count());

    match chan {
        Channel::Equidistant(c) => {
            println!("   segments:       {}", c.segments().len());
            for (i, seg) in c.segments().iter().enumerate() {
                println!(
                    "     [{i}] start={start_ts} ns  rate={rate} Hz  count={count}",
                    start_ts = seg.start_timestamp_ns,
                    rate = seg.sample_rate_hz,
                    count = seg.sample_count,
                );
            }
            println!("   first 5 samples:");
            for (i, sample) in c.samples_with_time().take(5).enumerate() {
                println!("     {i}:  ts={}  value={}", sample.timestamp_ns, format_numeric(sample.value));
            }
        }
        Channel::Timestamped(c) => {
            println!("   first 5 samples:");
            for (i, sample) in c.samples_with_time().take(5).enumerate() {
                println!("     {i}:  ts={}  value={}", sample.timestamp_ns, format_numeric(sample.value));
            }
        }
        Channel::Variable(c) => {
            println!("   first 5 samples:");
            for (i, sample) in c.samples_with_time().take(5).enumerate() {
                let preview = format_variable(sample);
                println!("     {i}:  ts={}  value={preview}", sample.timestamp_ns);
            }
        }
    }
}

fn channel_kind(chan: &Channel) -> &'static str {
    match chan {
        Channel::Equidistant(_) => "equidistant",
        Channel::Timestamped(_) => "timestamped",
        Channel::Variable(_) => "variable",
    }
}

fn segment_count(chan: &Channel) -> usize {
    match chan {
        Channel::Equidistant(c) => c.segments().len(),
        _ => 0,
    }
}

fn format_numeric(value: NumericValueRef<'_>) -> String {
    match value {
        NumericValueRef::Bool(v) => v.to_string(),
        NumericValueRef::Int8(v) => v.to_string(),
        NumericValueRef::Int16(v) => v.to_string(),
        NumericValueRef::Int32(v) => v.to_string(),
        NumericValueRef::Int64(v) => v.to_string(),
        NumericValueRef::UInt8(v) => v.to_string(),
        NumericValueRef::UInt16(v) => v.to_string(),
        NumericValueRef::UInt32(v) => v.to_string(),
        NumericValueRef::UInt64(v) => v.to_string(),
        NumericValueRef::Float(v) => format!("{v}"),
        NumericValueRef::Double(v) => format!("{v}"),
        NumericValueRef::GpsLocation(g) => {
            format!("({lat}, {lon}, {alt})", lat = g.latitude, lon = g.longitude, alt = g.altitude)
        }
    }
}

fn format_variable(sample: Sample<VariableValueRef<'_>>) -> String {
    match sample.value {
        VariableValueRef::String(s) => format!("{:?}", truncate_to(s, 60)),
        VariableValueRef::Binary(b) => format!("<{} bytes>", b.len()),
    }
}

fn truncate_to(s: &str, max: usize) -> String {
    if s.chars().count() <= max {
        s.to_string()
    } else {
        let mut out: String = s.chars().take(max.saturating_sub(1)).collect();
        out.push('…');
        out
    }
}
