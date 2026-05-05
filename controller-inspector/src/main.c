/*
 * controller-inspector -- Open a HID device by VID/PID and dump every
 * input report it sends, with a delta indicator showing which bytes changed
 * since the previous report.
 *
 * Usage: controller-inspector.exe <VID-hex> <PID-hex> [--transport=usb|bluetooth|any]
 *
 * Profile-authoring workflow:
 *   1. Run with your controller's VID/PID.
 *   2. Press exactly one button or move one stick at a time.
 *   3. Watch which byte/bit changed (highlighted with '*').
 *   4. Record the byte+bit (or byte+type for sticks/triggers) into your profile.json.
 */

#include "hid.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tchar.h>
#include <windows.h>
#include <hidsdi.h>

static void print_usage(void)
{
    printf("Usage: controller-inspector.exe <VID-hex> <PID-hex> [--transport=usb|bluetooth|any]\n");
    printf("Example: controller-inspector.exe 0x3537 0x1046 --transport=bluetooth\n");
}

static bool parse_hex_u16(const char *s, USHORT *out)
{
    if (!s) return false;
    const char *start = s;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) start = s + 2;
    char *end = NULL;
    unsigned long v = strtoul(start, &end, 16);
    if (end == start || *end != '\0' || v > 0xFFFF) return false;
    *out = (USHORT)v;
    return true;
}

/* Permissive variant of upstream check_vendor_and_product: opens with
 * desiredAccess=0 (HidD_GetAttributes needs no specific access) and shares
 * read+write so we coexist with whatever process already holds the device. */
static bool check_vid_pid_permissive(LPTSTR path, USHORT vid, USHORT pid)
{
    HANDLE h = CreateFile(path, 0,
                          FILE_SHARE_READ | FILE_SHARE_WRITE,
                          NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    if (h == INVALID_HANDLE_VALUE) return false;
    HIDD_ATTRIBUTES attr = { .Size = sizeof(attr) };
    bool ok = false;
    if (HidD_GetAttributes(h, &attr)) {
        ok = (attr.VendorID == vid) && (attr.ProductID == pid);
    }
    CloseHandle(h);
    return ok;
}

/* Truncate the dump to this many bytes (the Nova HD's 362-byte reports
 * carry meaningful data only in the leading bytes; rest is constant zero). */
#define DUMP_BYTES 256

static void print_report(const BYTE *buf, int len, const BYTE *prev_buf, bool have_prev)
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    int n = len < DUMP_BYTES ? len : DUMP_BYTES;
    printf("[%02d:%02d:%02d.%03d] %3d bytes (showing %d)  ",
           st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, len, n);
    for (int i = 0; i < n; i++) {
        bool changed = have_prev && prev_buf[i] != buf[i];
        printf("%c%02X ", changed ? '*' : ' ', buf[i]);
    }
    printf("\n");
}

int main(int argc, char **argv)
{
    if (argc < 3) { print_usage(); return 1; }

    USHORT vid, pid;
    if (!parse_hex_u16(argv[1], &vid)) { fprintf(stderr, "bad VID: %s\n", argv[1]); print_usage(); return 1; }
    if (!parse_hex_u16(argv[2], &pid)) { fprintf(stderr, "bad PID: %s\n", argv[2]); print_usage(); return 1; }

    /* Optional transport filter -- not enforced strictly; only used for the path filter. */
    LPTSTR filter = NULL;
    TCHAR filter_buf[64];
    for (int i = 3; i < argc; i++) {
        if (strncmp(argv[i], "--transport=", 12) == 0) {
            const char *t = argv[i] + 12;
            if (strcmp(t, "bluetooth") == 0) {
                _sntprintf_s(filter_buf, 64, _TRUNCATE, TEXT("vid&02%04x_pid&%04x"), vid, pid);
                filter = filter_buf;
            } else if (strcmp(t, "usb") == 0) {
                _sntprintf_s(filter_buf, 64, _TRUNCATE, TEXT("VID_%04X&PID_%04X"), vid, pid);
                filter = filter_buf;
            } else if (strcmp(t, "any") != 0) {
                fprintf(stderr, "bad --transport: %s (expected usb|bluetooth|any)\n", t); return 1;
            }
        }
    }

    printf("controller-inspector: looking for VID 0x%04X PID 0x%04X\n", vid, pid);
    if (filter) _tprintf(TEXT("  path filter: %s\n"), filter);

    LPTSTR filters[2] = { filter, NULL };
    struct hid_device_info *devices = hid_enumerate(filter ? filters : NULL);

    /* Walk and pick the first device matching VID/PID. Use the permissive
     * checker so we don't lose to a sharing violation when other processes
     * (Steam Input, GameSir software) already hold the device. */
    LPTSTR matched_path = NULL;
    int    enum_count  = 0;
    for (struct hid_device_info *cur = devices; cur != NULL; cur = cur->next) {
        enum_count++;
        if (check_vid_pid_permissive(cur->path, vid, pid)) {
            matched_path = _tcsdup(cur->path);
            break;
        }
    }

    if (!matched_path) {
        /* Diagnostic: re-walk and dump every device's path + actual VID/PID. */
        fprintf(stderr, "no device matching VID 0x%04X PID 0x%04X (enumerated %d HID devices).\n", vid, pid, enum_count);
        fprintf(stderr, "diagnostic dump (path / open-result / actual VID/PID):\n");
        for (struct hid_device_info *cur = devices; cur != NULL; cur = cur->next) {
            HANDLE h = CreateFile(cur->path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
            if (h == INVALID_HANDLE_VALUE) {
                _ftprintf(stderr, TEXT("  [open=fail err=%lu] %s\n"), (unsigned long)GetLastError(), cur->path);
            } else {
                HIDD_ATTRIBUTES a = { .Size = sizeof(a) };
                if (HidD_GetAttributes(h, &a)) {
                    _ftprintf(stderr, TEXT("  [VID=0x%04X PID=0x%04X] %s\n"), a.VendorID, a.ProductID, cur->path);
                } else {
                    _ftprintf(stderr, TEXT("  [HidD_GetAttributes fail] %s\n"), cur->path);
                }
                CloseHandle(h);
            }
        }
        /* Free the enumeration list. */
        while (devices) {
            struct hid_device_info *next = devices->next;
            hid_free_device_info(devices);
            devices = next;
        }
        return 2;
    }

    /* Free the enumeration list (success path). */
    while (devices) {
        struct hid_device_info *next = devices->next;
        hid_free_device_info(devices);
        devices = next;
    }

    _tprintf(TEXT("opening %s\n"), matched_path);

    struct hid_device *dev = hid_open_device(matched_path, TRUE, TRUE);   /* read+write, shared */
    free(matched_path);
    if (!dev) {
        fprintf(stderr, "hid_open_device failed: %lu\n", (unsigned long)GetLastError());
        return 3;
    }
    printf("opened. input report size: %u bytes. press buttons one at a time; '*' marks changed bytes.\n",
           dev->input_report_size);
    printf("press Ctrl+C to exit.\n\n");

    BYTE *prev = (BYTE *)calloc(dev->input_report_size, 1);
    if (!prev) { hid_close_device(dev); hid_free_device(dev); return 4; }
    bool have_prev = false;

    for (;;) {
        INT n = hid_get_input_report(dev, 1000);  /* 1 sec timeout = liveliness check */
        if (n < 0) { fprintf(stderr, "hid_get_input_report error: %lu\n", (unsigned long)GetLastError()); break; }
        if (n == 0) continue;
        /* Suppress duplicate reports: only print when something in the first
         * DUMP_BYTES has actually changed since the previous report. */
        int compare_n = n < DUMP_BYTES ? n : DUMP_BYTES;
        if (have_prev && memcmp(prev, dev->input_buffer, (size_t)compare_n) == 0) continue;
        print_report(dev->input_buffer, n, prev, have_prev);
        memcpy(prev, dev->input_buffer, (size_t)n);
        have_prev = true;
    }

    free(prev);
    hid_close_device(dev);
    hid_free_device(dev);
    return 0;
}
