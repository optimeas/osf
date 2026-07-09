/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Optimeas GmbH */

/*
 * C ABI smoke test. Compiled as C99 to prove osf/capi.h is
 * C-compatible and the osf-c shared library links + works end-to-end.
 * Exercises load, channel enumeration + metadata, sample/timestamp
 * readers, the integrity-profile accessors, a round-trip write, and the
 * error path. Returns non-zero on the first failure.
 */

#include <osf/capi.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond, msg)                                                  \
    do {                                                                  \
        if (!(cond)) {                                                    \
            fprintf(stderr, "FAIL: %s (%s:%d)\n", (msg), __FILE__,        \
                    __LINE__);                                            \
            return 1;                                                     \
        }                                                                 \
    } while (0)

int main(void) {
    const char* ver = osf_version();
    CHECK(ver != NULL && strlen(ver) > 0, "osf_version non-empty");

    const char* path = OSF_EXAMPLES_DIR "/generated/osf5_equidistant.osf";

    osf_manager* m = NULL;
    osf_status st = osf_load_file(path, &m);
    CHECK(st == OSF_OK, "osf_load_file OSF_OK");
    CHECK(m != NULL, "manager handle non-null");

    size_t count = osf_manager_channel_count(m);
    CHECK(count > 0, "channel_count > 0");

    const osf_channel* ch = osf_manager_channel_at(m, 0);
    CHECK(ch != NULL, "channel_at(0) non-null");

    const char* name = osf_channel_name(ch);
    CHECK(name != NULL && strlen(name) > 0, "channel name non-empty");

    /* by-name lookup returns the same borrowed handle */
    const osf_channel* by_name = osf_manager_channel_by_name(m, name);
    CHECK(by_name == ch, "channel_by_name matches channel_at(0)");

    /* unknown name -> NULL */
    CHECK(osf_manager_channel_by_name(m, "no/such/channel") == NULL,
          "unknown channel name -> NULL");

    osf_data_type dt = osf_channel_data_type(ch);
    size_t sc = osf_channel_sample_count(ch);
    CHECK(sc > 0, "sample_count > 0");

    /* timestamps copy-out */
    int64_t ts[512];
    size_t want = sc < 512 ? sc : 512;
    size_t nt = osf_channel_read_timestamps(ch, ts, 512);
    CHECK(nt == want, "read_timestamps count");

    /* osf5_equidistant.osf is a numeric (double) channel */
    CHECK(dt == OSF_DT_DOUBLE, "equidistant channel is double");
    double vals[512];
    size_t nv = osf_channel_read_f64(ch, vals, 512);
    CHECK(nv == want, "read_f64 count");

    /* integrity accessors on a plain (no-profile) file */
    CHECK(osf_manager_integrity(m) == OSF_INTEGRITY_NONE,
          "plain file integrity == none");
    CHECK(osf_manager_blocks_crc_failed(m) == 0, "plain file 0 crc failures");
    CHECK(osf_manager_blocks_signature_skipped(m) == 0,
          "plain file 0 signature skips");
    CHECK(strcmp(osf_manager_verification_status(m), "none") == 0,
          "plain file verification_status == none");

    /* integrity accessors on a crc32c file (Rust reference) */
    osf_manager* mc = NULL;
    st = osf_load_file(
        OSF_EXAMPLES_DIR "/generated/integrity/osf5_crc_equidistant.osf", &mc);
    CHECK(st == OSF_OK, "load crc file OSF_OK");
    CHECK(osf_manager_integrity(mc) == OSF_INTEGRITY_CRC32C,
          "crc file integrity == crc32c");
    CHECK(osf_manager_blocks_crc_failed(mc) == 0, "crc file 0 crc failures");
    CHECK(strcmp(osf_manager_verification_status(mc), "crc_valid") == 0,
          "crc file verification_status == crc_valid");
    osf_manager_free(mc);

    /* round-trip: write the loaded manager out as OSF5, reload, compare */
    const char* out_path = "osf_c_api_test_out.osf";
    st = osf_write_to_file(m, out_path);
    CHECK(st == OSF_OK, "osf_write_to_file OSF_OK");

    osf_manager* m2 = NULL;
    st = osf_load_file(out_path, &m2);
    CHECK(st == OSF_OK, "reload round-trip OSF_OK");
    CHECK(osf_manager_channel_count(m2) == count, "round-trip channel count");
    osf_manager_free(m2);
    remove(out_path);

    /* error path: missing file -> non-OK + non-empty last error */
    osf_manager* m3 = NULL;
    st = osf_load_file("no_such_file_xyz.osf", &m3);
    CHECK(st != OSF_OK, "missing file -> non-OK");
    CHECK(m3 == NULL, "missing file -> null handle");
    CHECK(strlen(osf_last_error_message()) > 0, "last error non-empty");

    /* `name` is borrowed from `m`; print the summary before releasing m so the
       pointer stays valid. */
    printf("test_c_api: OK (%zu channels, %zu samples on '%s')\n", count, sc,
           name);

    osf_manager_free(m);
    osf_manager_free(NULL); /* no-op */
    return 0;
}
