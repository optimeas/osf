// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

/*
 * c_api.h — C ABI for the OSF C++ library (Phase 11, DECISIONS §23).
 *
 * A C-callable surface over the C++ core, exposed by the separate shared
 * library `osf-c` (built only when OSF_BUILD_C_API=ON). Pure C99: depends
 * only on <stdint.h> / <stddef.h> and is `extern "C"`-guarded for C++
 * consumers. Intended for cross-language consumption — Windows DLL /
 * ActiveX-OCX and future language bindings.
 *
 * Ownership & lifetime (see DECISIONS §23):
 *   - osf_manager is heap-owned; free it with osf_manager_free().
 *   - osf_channel handles and every `const char*` returned by a getter are
 *     BORROWED from the owning manager and valid only until that manager is
 *     freed. Copy if you need the value to outlive the handle.
 *   - Sample/timestamp readers COPY into a caller-provided buffer.
 *
 * Errors: fallible calls return osf_status (OSF_OK == 0). On failure a
 * thread-local message is set; read it with osf_last_error_message()
 * before the next osf_* call on the same thread. No C++ exception ever
 * crosses this boundary.
 */

#ifndef OSF_C_API_H
#define OSF_C_API_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32)
#  if defined(OSF_C_BUILDING)
#    define OSF_C_API __declspec(dllexport)
#  else
#    define OSF_C_API __declspec(dllimport)
#  endif
#else
#  define OSF_C_API __attribute__((visibility("default")))
#endif

/* Status codes — mirror osf::Error::Code (OSF_OK == 0). Append-only. */
typedef enum osf_status {
    OSF_OK = 0,
    OSF_ERR_UNKNOWN,
    OSF_ERR_INVALID_ARGUMENT,
    OSF_ERR_IO,
    OSF_ERR_PARSE,
    OSF_ERR_NOT_FOUND,
    OSF_ERR_INVALID_MAGIC_HEADER,
    OSF_ERR_UNSUPPORTED_VERSION,
    OSF_ERR_MAGIC_HEADER_TOO_LONG,
    OSF_ERR_INVALID_METABLOCK,
    OSF_ERR_REMOVED_IN_SPEC,
    OSF_ERR_JSON_PARSE,
    OSF_ERR_XML_PARSE,
    OSF_ERR_UNKNOWN_CHANNEL_INDEX,
    OSF_ERR_INVALID_BLOCK,
    OSF_ERR_CHANNEL_MIXED_BLOCK_TYPES,
    OSF_ERR_CONTINUED_WITHOUT_START,
    OSF_ERR_RELSTAMP_WITHOUT_ANCHOR,
    OSF_ERR_DATA_TYPE_MISMATCH
} osf_status;

/* Channel data types — mirror osf::DataType. Append-only. */
typedef enum osf_data_type {
    OSF_DT_BOOL = 0,
    OSF_DT_INT8,
    OSF_DT_INT16,
    OSF_DT_INT32,
    OSF_DT_INT64,
    OSF_DT_UINT8,
    OSF_DT_UINT16,
    OSF_DT_UINT32,
    OSF_DT_UINT64,
    OSF_DT_FLOAT,
    OSF_DT_DOUBLE,
    OSF_DT_GPS_LOCATION,
    OSF_DT_STRING,
    OSF_DT_BINARY,
    OSF_DT_UNSUPPORTED
} osf_data_type;

/* Detected compression on the source stream — mirror osf::CompressionFormat. */
typedef enum osf_compression_format {
    OSF_COMPRESSION_NONE = 0,
    OSF_COMPRESSION_ZLIB,
    OSF_COMPRESSION_GZIP
} osf_compression_format;

/* Opaque handles. osf_manager owns a DataManager; osf_channel is borrowed. */
typedef struct osf_manager osf_manager;
typedef struct osf_channel osf_channel;

/* Library version string (e.g. "0.1.0"). Static storage; never freed. */
OSF_C_API const char* osf_version(void);

/* Thread-local message for the most recent failing call on this thread.
   Valid until the next osf_* call on the same thread. Never NULL. */
OSF_C_API const char* osf_last_error_message(void);

/* ── Manager (read) ─────────────────────────────────────────────────── */

/* Open and fully load an OSF or OSFZ file. On OSF_OK, *out owns a manager
   the caller must release with osf_manager_free(). */
OSF_C_API osf_status osf_load_file(const char* path, osf_manager** out);

/* Release a manager and everything borrowed from it. NULL is a no-op. */
OSF_C_API void osf_manager_free(osf_manager* m);

/* Number of channels (metablock order). 0 if m is NULL. */
OSF_C_API size_t osf_manager_channel_count(const osf_manager* m);

/* Borrowed channel by position [0, count) or by name; NULL if absent. */
OSF_C_API const osf_channel* osf_manager_channel_at(const osf_manager* m, size_t index);
OSF_C_API const osf_channel* osf_manager_channel_by_name(const osf_manager* m, const char* name);

/* File-level metadata. */
OSF_C_API int                    osf_manager_is_compressed(const osf_manager* m);
OSF_C_API osf_compression_format osf_manager_compression_format(const osf_manager* m);
OSF_C_API const char*            osf_manager_creator(const osf_manager* m);     /* "" if unset */
OSF_C_API const char*            osf_manager_created_utc(const osf_manager* m); /* "" if unset */

/* ── Channel (read) — borrowed strings valid until osf_manager_free ──── */

OSF_C_API const char*   osf_channel_name(const osf_channel* c);
OSF_C_API uint16_t      osf_channel_index(const osf_channel* c);
OSF_C_API osf_data_type osf_channel_data_type(const osf_channel* c);
OSF_C_API const char*   osf_channel_physical_unit(const osf_channel* c); /* "" if unset */
OSF_C_API size_t        osf_channel_sample_count(const osf_channel* c);

/* Copy-out readers: write min(sample_count, cap) elements into `out` and
   return that count. Equidistant timestamps are reconstructed. The value
   readers convert any numeric/bool sample; they return 0 for
   string/binary/GPS channels (check osf_channel_data_type first). */
OSF_C_API size_t osf_channel_read_timestamps(const osf_channel* c, int64_t* out, size_t cap);
OSF_C_API size_t osf_channel_read_f64(const osf_channel* c, double* out, size_t cap);
OSF_C_API size_t osf_channel_read_i64(const osf_channel* c, int64_t* out, size_t cap);

/* GPS: writes 3 doubles (latitude, longitude, altitude) per sample into
   `out_lla`; `cap_samples` is the number of samples `out_lla` can hold
   (so out_lla must have room for 3 * cap_samples doubles). Returns the
   number of samples written. 0 for non-GPS channels. */
OSF_C_API size_t osf_channel_read_gps(const osf_channel* c, double* out_lla, size_t cap_samples);

/* Variable-length sample i, borrowed (valid until osf_manager_free).
   string → NUL-terminated UTF-8; binary → *out_len set, bytes may contain
   embedded NULs. NULL if i is out of range or the type does not match. */
OSF_C_API const char*    osf_channel_string_at(const osf_channel* c, size_t i);
OSF_C_API const uint8_t* osf_channel_binary_at(const osf_channel* c, size_t i, size_t* out_len);

/* ── Write (round-trip; always OSF5, DECISIONS §6) ──────────────────── */

/* Re-export a loaded manager to `path` as an OSF5 file. */
OSF_C_API osf_status osf_write_to_file(const osf_manager* m, const char* path);

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif /* OSF_C_API_H */
