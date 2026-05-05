/*
 * runloop.h -- Active-devices array + refresh/attach/detach + per-device input thread.
 */

#ifndef RUNLOOP_H
#define RUNLOOP_H

#include "profile.h"
#include "vigem_compat.h"

#define MAX_ACTIVE_DEVICES 4

bool runloop_init(PVIGEM_CLIENT vigem);
void runloop_shutdown(void);

/* Register a profile so subsequent refreshes will attach matching devices.
 * Profile pointer is borrowed (owned by caller — keep alive for runloop's lifetime). */
void runloop_register_profile(const profile_t *profile);

/* Enumerate connected HID devices and attach/detach to match registered
 * profiles. Idempotent. Safe to call repeatedly. */
void runloop_refresh(void);

/* Returns how many devices are currently attached + active. */
int  runloop_active_count(void);

/* Snapshot the active devices' HID interface paths into out_paths (caller
 * supplies the array, capacity max_n). Each path is malloc'd and must be
 * freed by caller. Returns the number of paths written (<= max_n). */
int  runloop_snapshot_paths(wchar_t **out_paths, int max_n);

/* Detach all active devices and clear the profile registry, but leave the
 * runloop itself initialized (vigem client retained). MUST be called before
 * profile_free()-ing any registered profiles, otherwise dangling profile
 * pointers in g_devices[]/g_profiles[] are a use-after-free crash on the next
 * runloop_refresh(). */
void runloop_clear(void);

#endif /* RUNLOOP_H */
