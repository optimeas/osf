// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

/// \file osf.hpp
/// Umbrella header for the OSF C++ library.
///
/// Including `<osf/osf.hpp>` brings in the complete public read + write
/// API. Individual headers can also be included directly if a
/// translation unit needs only a subset.
///
/// Two headers are deliberately NOT part of the umbrella:
/// - `<osf/throwing.hpp>` — the opt-in exception layer. Consumers who
///   stay on the `Result<T>` core never pull in exception machinery.
/// - `<osf/c_api.h>` — the pure-C99 ABI of the separate `osf-c` shared
///   library (built only with `OSF_BUILD_C_API=ON`); it is not part of
///   the C++ API surface.

#pragma once

#include <osf/binary_sample.hpp>
#include <osf/block.hpp>
#include <osf/block_writer.hpp>
#include <osf/data_channel.hpp>
#include <osf/error.hpp>
#include <osf/header.hpp>
#include <osf/manager.hpp>
#include <osf/metablock.hpp>
#include <osf/reader.hpp>
#include <osf/stats.hpp>
#include <osf/streaming_writer.hpp>
#include <osf/types.hpp>
#include <osf/version.hpp>
