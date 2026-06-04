/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Optimeas GmbH */

/*
 * C ABI smoke test (Phase 11). Compiled as C99 to prove osf/c_api.h is
 * C-compatible and the osf-c shared library links + works end-to-end.
 * Exercises load, channel enumeration + metadata, sample/timestamp
 * readers, a round-trip write, and the error path. Returns non-zero on
 * the first failure.
 */

#include <osf/c_api.h>

#include <assert.h>
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

    osf_manager_free(m);
    osf_manager_free(NULL); /* no-op */

    printf("test_c_api: OK (%zu channels, %zu samples on '%s')\n", count, sc,
           name);
    return 0;
}
