// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

/// \file osf.h
/// Umbrella header for the OSF C++ library.
///
/// Including `<osf/osf.h>` brings in the complete public read + write
/// API. Individual headers can also be included directly if a
/// translation unit needs only a subset.
///
/// Two headers are deliberately NOT part of the umbrella:
/// - `<osf/throwing.h>` — the opt-in exception layer. Consumers who
///   stay on the `Result<T>` core never pull in exception machinery.
/// - `<osf/capi.h>` — the pure-C99 ABI of the separate `osf-c` shared
///   library (built only with `OSF_BUILD_C_API=ON`); it is not part of
///   the C++ API surface.

#pragma once

#include <osf/binarysample.h>
#include <osf/block.h>
#include <osf/blockwriter.h>
#include <osf/datachannel.h>
#include <osf/error.h>
#include <osf/header.h>
#include <osf/manager.h>
#include <osf/metablock.h>
#include <osf/reader.h>
#include <osf/stats.h>
#include <osf/streamingwriter.h>
#include <osf/types.h>
#include <osf/version.h>
