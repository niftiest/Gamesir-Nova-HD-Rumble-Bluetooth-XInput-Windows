/*
 * runloop.c -- Active-devices array + refresh/attach/detach + per-device input thread.
 */

#include "runloop.h"
#include "hid.h"
#include "gamepad.h"
#include "state_to_xusb.h"
#include "hidhide.h"
#include "log.h"

#include <windows.h>
#include <hidsdi.h>
#include <stdlib.h>
#include <string.h>
#include <tchar.h>

typedef struct {
    const profile_t   *profile;
    struct hid_device *device;
    PVIGEM_TARGET      target;
    HANDLE             thread;
    volatile LONG      active;          /* 1 = run, 0 = stop */
    LPTSTR             path;            /* malloc'd, lowercased copy for matching */
    volatile UCHAR     rumble_counter;  /* incremented on every output report sent */
    bool               rumble_registered;
} active_device_t;

static active_device_t g_devices[MAX_ACTIVE_DEVICES];
static int             g_count = 0;
static SRWLOCK         g_lock = SRWLOCK_INIT;
static PVIGEM_CLIENT   g_vigem = NULL;

#define MAX_PROFILES 16
static const profile_t *g_profiles[MAX_PROFILES];
static int              g_profile_count = 0;

bool runloop_init(PVIGEM_CLIENT vigem)
{
    g_vigem = vigem;
    memset(g_devices, 0, sizeof(g_devices));
    g_count = 0;
    g_profile_count = 0;
    return true;
}

void runloop_register_profile(const profile_t *p)
{
    if (g_profile_count >= MAX_PROFILES) { LOG_W("runloop: profile registry full, ignoring"); return; }
    g_profiles[g_profile_count++] = p;
}

/* Forward decl: defined further down. Needed by runloop_clear. */
static void detach_at(int idx);

int runloop_active_count(void) { return g_count; }

int runloop_snapshot_paths(wchar_t **out_paths, int max_n)
{
    AcquireSRWLockShared(&g_lock);
    int n = g_count < max_n ? g_count : max_n;
    for (int i = 0; i < n; i++) {
        out_paths[i] = _wcsdup(g_devices[i].path);
    }
    ReleaseSRWLockShared(&g_lock);
    return n;
}

void runloop_clear(void)
{
    AcquireSRWLockExclusive(&g_lock);
    while (g_count > 0) detach_at(g_count - 1);
    /* Clear the profile registry too — caller is about to profile_free() the
     * pointers we hold and any subsequent runloop_refresh() would dereference
     * freed memory. */
    for (int i = 0; i < g_profile_count; i++) g_profiles[i] = NULL;
    g_profile_count = 0;
    ReleaseSRWLockExclusive(&g_lock);
}

/* Build the upstream path filter strings for a profile (USB or BT or both). */
static void build_filters_for(const profile_t *p, LPTSTR *filters_out)
{
    static TCHAR buf_usb[64];
    static TCHAR buf_bt[64];
    int n = 0;
    if (p->transport == TRANSPORT_USB || p->transport == TRANSPORT_ANY) {
        _sntprintf_s(buf_usb, 64, _TRUNCATE, TEXT("VID_%04X&PID_%04X"), p->vid, p->pid);
        filters_out[n++] = buf_usb;
    }
    if (p->transport == TRANSPORT_BLUETOOTH || p->transport == TRANSPORT_ANY) {
        _sntprintf_s(buf_bt, 64, _TRUNCATE, TEXT("vid&02%04x_pid&%04x"), p->vid, p->pid);
        filters_out[n++] = buf_bt;
    }
    filters_out[n] = NULL;
}

/* Switch Pro rumble output report helpers.
 *
 * The controller accepts Nintendo Switch Pro-style output reports over HID.
 * We try both WriteFile (interrupt out) and HidD_SetOutputReport because the
 * Win32 HID stack can route these differently depending on Bluetooth driver stack.
 *
 * Counter: the low 4 bits of the counter byte must increment with each report
 * or the controller may silently discard duplicate-looking packets.
 */

/* Build and send the 0x01 + subcommand 0x48 0x01 "enable vibration" packet. */
static void send_switchpro_enable(struct hid_device *dev, volatile UCHAR *counter)
{
    uint8_t pkt[50];  /* report-id prefix byte + 49 payload bytes = 50 total for HidD_SetOutputReport */
    memset(pkt, 0, sizeof(pkt));

    /* Packet for WriteFile (no report-id prefix byte prepended by HID stack). */
    uint8_t raw[49];
    memset(raw, 0, sizeof(raw));
    raw[0] = 0x01;                              /* output report ID */
    raw[1] = (uint8_t)((*counter) & 0x0F);     /* counter low 4 bits */
    /* neutral rumble data (left + right, 4 bytes each) */
    raw[2] = 0x00; raw[3] = 0x01; raw[4] = 0x40; raw[5] = 0x40;
    raw[6] = 0x00; raw[7] = 0x01; raw[8] = 0x40; raw[9] = 0x40;
    raw[10] = 0x48;  /* subcommand: enable vibration */
    raw[11] = 0x01;  /* arg: on */
    (*counter)++;

    /* Try WriteFile (interrupt out). */
    hid_send_output_report(dev, raw, sizeof(raw), 50);

    /* Also try HidD_SetOutputReport (some Win32 BT stacks prefer this path).
     * HidD_SetOutputReport expects a buffer with report_id as byte 0 when
     * report IDs are used, so prepend the report ID. */
    pkt[0] = 0x01;
    memcpy(pkt + 1, raw + 1, 48);
    HidD_SetOutputReport(dev->handle, pkt, (ULONG)sizeof(pkt));
}

/* 4-byte rumble block for "strong" rumble (320Hz happy pattern, verified on this controller). */
static const uint8_t RUMBLE_STRONG[4] = { 0x28, 0x88, 0x60, 0x61 };
/* 4-byte rumble block for neutral (no buzz). */
static const uint8_t RUMBLE_NEUTRAL[4] = { 0x00, 0x01, 0x40, 0x40 };

/* Build and send a 0x10 rumble-only report.
 * If both large and small motors are 0, sends neutral; otherwise sends strong.
 * TODO: proper amplitude/frequency encoding per
 *   https://github.com/dekuNukem/Nintendo_Switch_Reverse_Engineering/blob/master/rumble_data_table.md
 */
static void send_switchpro_rumble(struct hid_device *dev, volatile UCHAR *counter,
                                   UCHAR large_motor, UCHAR small_motor)
{
    const uint8_t *block = (large_motor > 0 || small_motor > 0) ? RUMBLE_STRONG : RUMBLE_NEUTRAL;

    uint8_t raw[49];
    memset(raw, 0, sizeof(raw));
    raw[0] = 0x10;                              /* output report ID: rumble only */
    raw[1] = (uint8_t)((*counter) & 0x0F);     /* counter low 4 bits */
    memcpy(raw + 2, block, 4);                  /* left rumble */
    memcpy(raw + 6, block, 4);                  /* right rumble */
    (*counter)++;

    hid_send_output_report(dev, raw, sizeof(raw), 50);
}

/* ViGEm X360 rumble notification callback. Invoked from ViGEm's internal thread. */
static VOID CALLBACK on_rumble(PVIGEM_CLIENT client, PVIGEM_TARGET target,
                                UCHAR large_motor, UCHAR small_motor, UCHAR led, LPVOID user)
{
    (void)client; (void)target; (void)led;
    active_device_t *ad = (active_device_t *)user;
    if (!ad || !ad->device) return;
    if (ad->profile->output_report.protocol != OUTPUT_PROTO_SWITCHPRO_RUMBLE) return;
    send_switchpro_rumble(ad->device, &ad->rumble_counter, large_motor, small_motor);
}

/* Per-controller input thread: read reports, decode, push to ViGEm. */
static DWORD WINAPI input_thread(LPVOID param)
{
    active_device_t *ad = (active_device_t *)param;
    LOG_I("[+] %s -> ViGEm slot %d", ad->profile->name, ad - g_devices + 1);
    XUSB_REPORT report;
    while (InterlockedCompareExchange(&ad->active, 0, 0) == 1) {
        INT n = hid_get_input_report(ad->device, 100);
        if (n < 0) {
            DWORD e = GetLastError();
            if (e == ERROR_DEVICE_NOT_CONNECTED || e == ERROR_OPERATION_ABORTED) break;
            continue;
        }
        if (n == 0) continue;
        if (n < ad->profile->report_length) continue;
        if (ad->device->input_buffer[0] != ad->profile->report_id) continue;

        gamepad_state_t s;
        if (!apply_profile(ad->profile, ad->device->input_buffer, (size_t)n, &s)) continue;
        gamepad_state_to_xusb(&s, &report);
        VIGEM_ERROR ve = vigem_target_x360_update(g_vigem, ad->target, report);
        if (ve != VIGEM_ERROR_NONE) {
            LOG_E("vigem_target_x360_update failed: 0x%X", (unsigned)ve);
            break;
        }
    }
    LOG_I("[-] %s (input thread exiting)", ad->profile->name);
    return 0;
}

static bool device_already_active(LPCTSTR path)
{
    for (int i = 0; i < g_count; i++) {
        if (_tcsicmp(g_devices[i].path, path) == 0) return true;
    }
    return false;
}

static void detach_at(int idx)
{
    active_device_t *ad = &g_devices[idx];
    InterlockedExchange(&ad->active, 0);
    /* Cancel any pending I/O so the input thread wakes promptly. */
    if (ad->device) CancelIoEx(ad->device->handle, &ad->device->input_ol);
    if (ad->thread) {
        WaitForSingleObject(ad->thread, 3000);
        CloseHandle(ad->thread);
    }
    if (ad->target) {
        /* Unregister rumble callback before removing target to avoid use-after-free. */
        if (ad->rumble_registered) {
            vigem_target_x360_unregister_notification(ad->target);
            ad->rumble_registered = false;
        }
        vigem_target_remove(g_vigem, ad->target);
        vigem_target_free(ad->target);
    }
    if (ad->device) { hid_close_device(ad->device); hid_free_device(ad->device); }
    free(ad->path);
    /* Compact the array. */
    for (int j = idx; j < g_count - 1; j++) g_devices[j] = g_devices[j + 1];
    memset(&g_devices[g_count - 1], 0, sizeof(active_device_t));
    g_count--;
}

static bool attach(LPCTSTR path, const profile_t *profile)
{
    if (g_count >= MAX_ACTIVE_DEVICES) { LOG_W("runloop: at MAX_ACTIVE_DEVICES (%d), refusing", MAX_ACTIVE_DEVICES); return false; }
    /* Try exclusive first; fall back to shared if busy (per spec error-table). */
    struct hid_device *dev = hid_open_device((LPTSTR)path, TRUE, FALSE);
    if (!dev) {
        if (hid_reenable_device((LPTSTR)path)) dev = hid_open_device((LPTSTR)path, TRUE, FALSE);
        if (!dev) dev = hid_open_device((LPTSTR)path, TRUE, TRUE);
    }
    if (!dev) { LOG_E("hid_open_device failed for %s", path); return false; }

    /* HidHide config is handled out-of-band (one elevated call via the tray
     * "Configure HidHide" item or --configure-hidhide CLI). Calling per-device
     * here would UAC-prompt repeatedly, which is hostile UX. */

    PVIGEM_TARGET tgt = vigem_target_x360_alloc();
    if (!tgt) { hid_close_device(dev); hid_free_device(dev); LOG_E("vigem_target_x360_alloc failed"); return false; }
    VIGEM_ERROR ve = vigem_target_add(g_vigem, tgt);
    if (ve != VIGEM_ERROR_NONE) {
        vigem_target_free(tgt); hid_close_device(dev); hid_free_device(dev);
        LOG_E("vigem_target_add failed: 0x%X", (unsigned)ve);
        return false;
    }

    active_device_t *ad = &g_devices[g_count++];
    ad->profile = profile;
    ad->device  = dev;
    ad->target  = tgt;
    ad->path    = _tcsdup(path);
    ad->active  = 1;
    ad->rumble_counter    = 0;
    ad->rumble_registered = false;
    ad->thread  = CreateThread(NULL, 0, input_thread, ad, 0, NULL);
    if (!ad->thread) {
        LOG_E("CreateThread failed");
        ad->active = 0;
        vigem_target_remove(g_vigem, tgt); vigem_target_free(tgt);
        hid_close_device(dev); hid_free_device(dev);
        free(ad->path);
        memset(ad, 0, sizeof(*ad));
        g_count--;
        return false;
    }

    /* If the profile declares a Switch Pro rumble output channel, enable it now. */
    if (profile->output_report.present &&
        profile->output_report.protocol == OUTPUT_PROTO_SWITCHPRO_RUMBLE)
    {
        send_switchpro_enable(ad->device, &ad->rumble_counter);
        VIGEM_ERROR rve = vigem_target_x360_register_notification(g_vigem, ad->target, on_rumble, ad);
        if (rve == VIGEM_ERROR_NONE) {
            ad->rumble_registered = true;
            LOG_I("rumble: Switch Pro rumble enabled for %s", profile->name);
        } else {
            LOG_W("rumble: vigem_target_x360_register_notification failed: 0x%X (rumble unavailable)", (unsigned)rve);
        }
    }

    return true;
}

void runloop_refresh(void)
{
    AcquireSRWLockExclusive(&g_lock);

    /* Enumerate ALL HID devices once (path-filter skipped — Bluetooth HID
     * paths vary too much to match reliably by substring; check_vendor_and_product
     * uses HidD_GetAttributes which works for any transport). For each device,
     * check it against every registered profile by VID/PID. */
    struct hid_device_info *list = hid_enumerate(NULL);
    int enumerated = 0;
    for (struct hid_device_info *cur = list; cur != NULL; cur = cur->next) {
        enumerated++;
        for (int pi = 0; pi < g_profile_count; pi++) {
            const profile_t *p = g_profiles[pi];
            if (!check_vendor_and_product(cur->path, p->vid, p->pid)) continue;
            LOG_D("matched device for profile '%s': %s", p->name, cur->path);
            if (device_already_active(cur->path)) { LOG_D("  ...already active, skipping"); break; }
            attach(cur->path, p);
            break;  /* one device, at most one profile */
        }
    }
    LOG_D("enumerated %d HID devices, %d profiles registered, %d active", enumerated, g_profile_count, g_count);
    while (list) {
        struct hid_device_info *next = list->next;
        hid_free_device_info(list);
        list = next;
    }

    /* Remove devices that have disappeared. We re-enumerate and check membership. */
    for (int i = g_count - 1; i >= 0; i--) {
        bool still_present = false;
        struct hid_device_info *list = hid_enumerate(NULL);
        for (struct hid_device_info *cur = list; cur && !still_present; cur = cur->next) {
            if (_tcsicmp(cur->path, g_devices[i].path) == 0) still_present = true;
        }
        while (list) {
            struct hid_device_info *next = list->next;
            hid_free_device_info(list);
            list = next;
        }
        if (!still_present) detach_at(i);
    }

    ReleaseSRWLockExclusive(&g_lock);
}

void runloop_shutdown(void)
{
    AcquireSRWLockExclusive(&g_lock);
    while (g_count > 0) detach_at(g_count - 1);
    ReleaseSRWLockExclusive(&g_lock);
    g_profile_count = 0;
    g_vigem = NULL;
}
