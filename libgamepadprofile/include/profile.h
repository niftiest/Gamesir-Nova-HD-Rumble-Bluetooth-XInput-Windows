/*
 * profile.h -- Load and validate gamepad profiles from JSON.
 */

#ifndef PROFILE_H
#define PROFILE_H

#include "gamepad.h"
#include <stdbool.h>

#define PROFILE_ERR_LEN 256

/*
 * Parse and validate a profile from a NUL-terminated JSON string.
 * On success: returns a malloc'd profile_t (caller frees via profile_free).
 * On failure: returns NULL and writes a human-readable reason into err_buf
 * (truncated to err_buf_size-1 bytes plus NUL).
 *
 * Validation rules (per spec §2):
 *   1. schema_version must equal 1.
 *   2. All referenced byte indices must be < input_report.length.
 *   3. Required: dpad mapping, A/B/X/Y, LB/RB, LT/RT, Start, Back. Missing
 *      any of these is a fatal error.
 *   4. Optional (warnings only, profile still loads): LS/RS, Guide, sticks.
 *   5. Unknown top-level keys are tolerated and ignored (forward-compat).
 *
 * No I/O. Pure parse + validate. Safe to unit-test with string fixtures.
 */
profile_t *profile_load_from_string(const char *json, char *err_buf, size_t err_buf_size);

/*
 * Convenience: read the file at `path` and call profile_load_from_string on
 * its contents. err_buf is also written for I/O errors. Returns NULL on
 * any failure.
 */
profile_t *profile_load_from_file(const char *path, char *err_buf, size_t err_buf_size);

/* Free a profile_t obtained from either loader. NULL-safe. */
void profile_free(profile_t *profile);

#endif /* PROFILE_H */
