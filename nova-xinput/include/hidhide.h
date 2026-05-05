/*
 * hidhide.h -- Drive HidHide configuration via the official HidHideCLI.exe.
 *
 * HidHide's kernel driver requires Administrator to modify its allow-list /
 * hide-list. Rather than re-implement the IOCTL surface (which our previous
 * version did, and which hit ERROR_INVALID_PARAMETER on GET_WHITELIST), we
 * spawn HidHideCLI.exe — Nefarius's official maintained CLI tool that ships
 * with HidHide. It uses the right IOCTLs and bubbles errors clearly.
 *
 * All write operations spawn HidHideCLI elevated via ShellExecute("runas")
 * which triggers a UAC prompt. To avoid prompting the user multiple times,
 * use hidhide_configure_for_self_and_devices() which bundles every needed
 * change into a single elevated call.
 */

#ifndef HIDHIDE_H
#define HIDHIDE_H

#include <stdbool.h>
#include <wchar.h>

/* Returns true if HidHide is installed (HidHideCLI.exe found on disk). */
bool hidhide_is_installed(void);

/* Read-only check: does HidHide's allow-list already contain our exe?
 * Returns true if yes; false if no, error, or HidHide not installed.
 * This is a query only — does NOT require admin / does not prompt UAC. */
bool hidhide_query_self_allowed(void);

/* Bulk configure: register self + hide all `device_paths` (interface paths,
 * as returned by hid_enumerate) + activate cloaking. Single UAC prompt for
 * the whole operation. Returns true on success, false if user declined UAC
 * or HidHideCLI returned non-zero. */
bool hidhide_configure_for_self_and_devices(const wchar_t **device_paths, int n_devices);

/* Reverse of configure_for_self_and_devices: unregister self + un-hide
 * the specified devices. Does NOT touch the system-wide cloak-on/off state
 * (other apps may rely on it). Single UAC prompt. */
bool hidhide_unconfigure_for_self_and_devices(const wchar_t **device_paths, int n_devices);

#endif /* HIDHIDE_H */
