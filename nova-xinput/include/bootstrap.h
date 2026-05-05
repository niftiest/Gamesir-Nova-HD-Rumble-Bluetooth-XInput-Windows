/*
 * bootstrap.h -- Detect and (optionally) install ViGEmBus + HidHide.
 *
 * Detection works by trying the IPC: vigem_connect for ViGEmBus,
 * CreateFile("\\.\HidHide", ...) for HidHide. Detection is cheap and
 * idempotent; safe to call on every launch.
 *
 * Install fetches the latest signed installer from the upstream GitHub
 * Releases API via WinHTTP, runs it via CreateProcess (UAC-elevated by
 * the installer's own manifest), waits for exit, then re-detects.
 */

#ifndef BOOTSTRAP_H
#define BOOTSTRAP_H

#include <stdbool.h>

typedef enum {
    BS_OK,                  /* present and reachable */
    BS_NOT_INSTALLED,       /* IPC says not installed */
    BS_VERSION_MISMATCH,    /* present but reports incompatible version */
    BS_NETWORK_ERROR,       /* download attempt failed */
    BS_INSTALL_FAILED,      /* installer ran but post-install detect still fails */
    BS_USER_CANCELLED       /* user clicked Cancel on the install prompt */
} bootstrap_status_t;

bootstrap_status_t bootstrap_detect_vigembus(void);
bootstrap_status_t bootstrap_detect_hidhide(void);

/* Interactive: shows MessageBox, downloads + installs, re-detects.
 * `silent_if_present` = true skips the dialog if already installed and just returns BS_OK.
 * `force_no_install` = true skips the install attempt and just reports detection state
 * (used by --no-bootstrap). */
bootstrap_status_t bootstrap_ensure_vigembus(bool silent_if_present, bool force_no_install);
bootstrap_status_t bootstrap_ensure_hidhide(bool silent_if_present, bool force_no_install);

#endif /* BOOTSTRAP_H */
