// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

//! `inspect` — minimal CLI that prints what `osf-core` knows about a file.
//!
//! Usage:
//!
//! ```text
//! cargo run --example inspect -- <path-to-osf-file>
//! ```
//!
//! Prints the magic header, the file-level metadata recorded in the
//! metablock, and a one-line summary per channel. Block-stream
//! inspection follows in later sessions.
//!
//! Run with `RUST_LOG=debug cargo run --example inspect -- <path>` to
//! see diagnostics about deprecated fields, accepted alternatives, and
//! unknown attributes.

use osf_core::{
    ChannelType, DataType, MetaBlock, MetaChannel, compression, parse_magic_header,
    parse_metablock,
};
use std::env;
use std::fs::File;
use std::io::Read;
use std::process::ExitCode;

fn main() -> ExitCode {
    env_logger::Builder::from_env(env_logger::Env::default().default_filter_or("warn"))
        .format_timestamp(None)
        .init();

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

    // OSFZ-aware: detect gzip / zlib wrappers transparently. Plain
    // OSF files come through as MaybeCompressed::Plain and continue
    // unchanged.
    let mut stream = match compression::detect_and_wrap(file) {
        Ok(s) => s,
        Err(e) => {
            eprintln!("{path}: cannot detect compression: {e}");
            return ExitCode::FAILURE;
        }
    };
    let compression_format = stream.detected_format();

    let header = match parse_magic_header(&mut stream) {
        Ok(h) => h,
        Err(e) => {
            eprintln!("{path}: {e}");
            return ExitCode::FAILURE;
        }
    };

    let mut body = vec![0u8; header.metablock_len as usize];
    if let Err(e) = stream.read_exact(&mut body) {
        eprintln!("{path}: cannot read metablock: {e}");
        return ExitCode::FAILURE;
    }

    let metablock = match parse_metablock(header.version, &body) {
        Ok(mb) => mb,
        Err(e) => {
            eprintln!("{path}: metablock parse failed: {e}");
            return ExitCode::FAILURE;
        }
    };

    print_summary(&path, &header, &metablock, compression_format);
    ExitCode::SUCCESS
}

fn print_summary(
    path: &str,
    header: &osf_core::MagicHeader,
    mb: &MetaBlock,
    compression_format: compression::CompressionFormat,
) {
    use compression::CompressionFormat::{Gzip, None as Plain, Zlib};
    println!("path:           {path}");
    let compressed_label = match compression_format {
        Plain => "no",
        Zlib => "yes (zlib)",
        Gzip => "yes (gzip)",
    };
    println!("compressed:     {compressed_label}");
    println!("version:        {:?}", header.version);
    println!("metablock_len:  {} bytes", header.metablock_len);
    println!(
        "created_utc:    {}",
        mb.file_info.created_utc.as_deref().unwrap_or("-")
    );
    println!(
        "creator:        {}",
        mb.file_info.creator.as_deref().unwrap_or("-")
    );
    println!("channels:       {}", mb.channels.len());

    if !mb.channels.is_empty() {
        let max_name = mb
            .channels
            .iter()
            .map(|c| c.name.chars().count())
            .max()
            .unwrap_or(0)
            .min(48);
        for chan in &mb.channels {
            println!("   {}", format_channel(chan, max_name));
        }
    }

    println!("infos:          {}", mb.infos.len());
}

fn format_channel(chan: &MetaChannel, name_width: usize) -> String {
    let display_name = truncate_to(&chan.name, name_width);
    let ct = format_channel_type(&chan.channel_type);
    let dt = format_data_type(&chan.data_type);
    let unit = chan
        .physical_unit
        .as_deref()
        .filter(|u| !u.is_empty())
        .unwrap_or("-");
    let incr = chan
        .time_increment_ns
        .map(|n| format!("incr_ns={n}"))
        .unwrap_or_else(|| "incr_ns=-".to_string());
    format!(
        "[{:>3}] {:<width$}  {:<11}  {:<8}  unit={:<8}  {}",
        chan.index,
        display_name,
        ct,
        dt,
        unit,
        incr,
        width = name_width
    )
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

fn format_channel_type(ct: &ChannelType) -> String {
    match ct {
        ChannelType::Scalar => "scalar".into(),
        ChannelType::Vector => "vector".into(),
        ChannelType::Matrix => "matrix".into(),
        ChannelType::Binary => "binary".into(),
        ChannelType::Unsupported(s) => format!("?{s}"),
    }
}

fn format_data_type(dt: &DataType) -> String {
    match dt {
        DataType::Bool => "bool".into(),
        DataType::Int8 => "int8".into(),
        DataType::Int16 => "int16".into(),
        DataType::Int32 => "int32".into(),
        DataType::Int64 => "int64".into(),
        DataType::UInt8 => "uint8".into(),
        DataType::UInt16 => "uint16".into(),
        DataType::UInt32 => "uint32".into(),
        DataType::UInt64 => "uint64".into(),
        DataType::Float => "float".into(),
        DataType::Double => "double".into(),
        DataType::String => "string".into(),
        DataType::Binary | DataType::ByteArray => "binary".into(),
        DataType::GpsLocation => "gpsloc".into(),
        DataType::Unsupported(s) => format!("?{s}"),
    }
}
