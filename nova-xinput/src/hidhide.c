/*
 * hidhide.c -- HidHide configuration via HidHideCLI.exe (the official tool).
 *
 * See hidhide.h for the design rationale (TL;DR: we previously hand-rolled
 * IOCTLs and they failed with ERROR_INVALID_PARAMETER; HidHideCLI.exe gets
 * them right and is maintained alongside the driver).
 */

#include "hidhide.h"
#include "log.h"

#include <windows.h>
#include <shellapi.h>
#include <wchar.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const wchar_t *CLI_PATHS[] = {
    L"C:\\Program Files\\Nefarius Software Solutions\\HidHide\\x64\\HidHideCLI.exe",
    L"C:\\Program Files (x86)\\Nefarius Software Solutions\\HidHide\\x64\\HidHideCLI.exe",
    NULL
};

static bool find_cli_path(wchar_t *out, size_t out_chars)
{
    for (int i = 0; CLI_PATHS[i] != NULL; i++) {
        if (GetFileAttributesW(CLI_PATHS[i]) != INVALID_FILE_ATTRIBUTES) {
            wcsncpy_s(out, out_chars, CLI_PATHS[i], _TRUNCATE);
            return true;
        }
    }
    return false;
}

bool hidhide_is_installed(void)
{
    wchar_t cli[MAX_PATH];
    return find_cli_path(cli, _countof(cli));
}

/* Run HidHideCLI as a non-elevated child, capture exit + stdout (small).
 * Returns exit code via *out_exit, and copies up to out_chars-1 wide chars
 * of stdout into *out (UTF-8 decoded from CP_OEM); returns true if the
 * process ran (regardless of exit code). */
static bool run_cli_capture(const wchar_t *args, DWORD *out_exit, wchar_t *out, size_t out_chars)
{
    wchar_t cli[MAX_PATH];
    if (!find_cli_path(cli, _countof(cli))) return false;

    HANDLE r = NULL, w = NULL;
    SECURITY_ATTRIBUTES sa = { .nLength = sizeof(sa), .bInheritHandle = TRUE };
    if (!CreatePipe(&r, &w, &sa, 0)) return false;
    SetHandleInformation(r, HANDLE_FLAG_INHERIT, 0);

    wchar_t cmdline[MAX_PATH * 8];
    swprintf_s(cmdline, _countof(cmdline), L"\"%s\" %s", cli, args);

    STARTUPINFOW si = { .cb = sizeof(si) };
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = w;
    si.hStdError  = w;
    si.hStdInput  = GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION pi = {0};
    BOOL ok = CreateProcessW(NULL, cmdline, NULL, NULL, TRUE,
                              CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    CloseHandle(w);
    if (!ok) {
        CloseHandle(r);
        LOG_W("CreateProcess HidHideCLI failed: err=%lu", GetLastError());
        return false;
    }

    /* Drain stdout. */
    char buf[4096] = {0};
    DWORD total = 0, got = 0;
    while (total < sizeof(buf) - 1 &&
           ReadFile(r, buf + total, (DWORD)(sizeof(buf) - 1 - total), &got, NULL) && got > 0) {
        total += got;
    }
    buf[total] = '\0';
    CloseHandle(r);

    WaitForSingleObject(pi.hProcess, 10000);
    DWORD exit_code = 1;
    GetExitCodeProcess(pi.hProcess, &exit_code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    if (out_exit) *out_exit = exit_code;
    if (out && out_chars > 0) {
        MultiByteToWideChar(CP_OEMCP, 0, buf, -1, out, (int)out_chars);
        out[out_chars - 1] = L'\0';
    }
    return true;
}

/* Run HidHideCLI elevated via ShellExecute("runas"). Pops UAC. */
static bool run_cli_elevated(const wchar_t *args)
{
    wchar_t cli[MAX_PATH];
    if (!find_cli_path(cli, _countof(cli))) {
        LOG_W("HidHideCLI.exe not found");
        return false;
    }
    SHELLEXECUTEINFOW sei = { .cbSize = sizeof(sei) };
    sei.lpVerb       = L"runas";  /* triggers UAC */
    sei.lpFile       = cli;
    sei.lpParameters = args;
    sei.nShow        = SW_HIDE;
    sei.fMask        = SEE_MASK_NOCLOSEPROCESS;
    if (!ShellExecuteExW(&sei)) {
        DWORD err = GetLastError();
        if (err == ERROR_CANCELLED) {
            LOG_I("HidHide config: user declined UAC prompt");
        } else {
            LOG_W("ShellExecute HidHideCLI failed: err=%lu", err);
        }
        return false;
    }
    bool ok = false;
    if (sei.hProcess) {
        WaitForSingleObject(sei.hProcess, 30000);
        DWORD exit_code = 1;
        GetExitCodeProcess(sei.hProcess, &exit_code);
        ok = (exit_code == 0);
        if (!ok) LOG_W("HidHideCLI exited with code %lu", exit_code);
        CloseHandle(sei.hProcess);
    }
    return ok;
}

/* Convert a HID device interface path:
 *   \\?\HID#{guid}_VID&XXXX_PID&XXXX#instance#{interface_class_guid}
 * to a device instance ID:
 *   HID\{guid}_VID&XXXX_PID&XXXX\instance
 * by stripping the \\?\ prefix, the trailing #{class_guid}, and replacing
 * remaining # with \. */
static bool path_to_instance_id(const wchar_t *path, wchar_t *out, size_t out_chars)
{
    if (!path || !out || out_chars == 0) return false;
    const wchar_t *p = path;
    if (wcsncmp(p, L"\\\\?\\", 4) == 0) p += 4;
    else if (wcsncmp(p, L"\\\\.\\", 4) == 0) p += 4;
    wcsncpy_s(out, out_chars, p, _TRUNCATE);

    /* Strip trailing #{...} (interface class GUID). */
    wchar_t *suffix = wcsrchr(out, L'#');
    if (suffix && suffix[1] == L'{') *suffix = L'\0';

    /* Replace remaining # with \. */
    for (wchar_t *q = out; *q; q++) {
        if (*q == L'#') *q = L'\\';
    }
    return true;
}

bool hidhide_query_self_allowed(void)
{
    wchar_t exe[MAX_PATH];
    if (!GetModuleFileNameW(NULL, exe, MAX_PATH)) return false;

    wchar_t output[8192];
    DWORD exit_code = 1;
    if (!run_cli_capture(L"--app-list", &exit_code, output, _countof(output))) {
        return false;
    }
    if (exit_code != 0) return false;

    /* Case-insensitive substring search for our exe path. */
    wchar_t lower_exe[MAX_PATH];
    wcsncpy_s(lower_exe, _countof(lower_exe), exe, _TRUNCATE);
    _wcslwr_s(lower_exe, _countof(lower_exe));
    _wcslwr_s(output, _countof(output));
    return wcsstr(output, lower_exe) != NULL;
}

bool hidhide_configure_for_self_and_devices(const wchar_t **device_paths, int n_devices)
{
    wchar_t exe[MAX_PATH];
    if (!GetModuleFileNameW(NULL, exe, MAX_PATH)) return false;

    /* Build sequenced command line: --app-reg "exe" --dev-hide "id1" ... --cloak-on */
    wchar_t args[16384];
    int written = swprintf_s(args, _countof(args), L"--app-reg \"%s\"", exe);
    if (written < 0) return false;

    int hidden = 0;
    for (int i = 0; i < n_devices; i++) {
        wchar_t instance[MAX_PATH];
        if (!path_to_instance_id(device_paths[i], instance, _countof(instance))) continue;
        int n = swprintf_s(args + written, _countof(args) - written,
                           L" --dev-hide \"%s\"", instance);
        if (n < 0) break;
        written += n;
        hidden++;
    }
    swprintf_s(args + written, _countof(args) - written, L" --cloak-on");

    LOG_I("HidHide: requesting elevated configure (self + %d device(s))", hidden);
    bool ok = run_cli_elevated(args);
    if (ok) LOG_I("HidHide configured successfully (will persist across reboots)");
    return ok;
}

bool hidhide_unconfigure_for_self_and_devices(const wchar_t **device_paths, int n_devices)
{
    wchar_t exe[MAX_PATH];
    if (!GetModuleFileNameW(NULL, exe, MAX_PATH)) return false;

    /* Build sequenced command line: --app-unreg "exe" --dev-unhide "id1" ... */
    wchar_t args[16384];
    int written = swprintf_s(args, _countof(args), L"--app-unreg \"%s\"", exe);
    if (written < 0) return false;

    int unhidden = 0;
    for (int i = 0; i < n_devices; i++) {
        wchar_t instance[MAX_PATH];
        if (!path_to_instance_id(device_paths[i], instance, _countof(instance))) continue;
        int n = swprintf_s(args + written, _countof(args) - written,
                           L" --dev-unhide \"%s\"", instance);
        if (n < 0) break;
        written += n;
        unhidden++;
    }
    /* Intentionally NOT adding --cloak-off: that's a system-wide kill switch
     * that could disrupt other tools (DS4Windows, BetterJoy, etc.) using
     * HidHide. Removing our entries from the lists is enough. */

    LOG_I("HidHide: requesting elevated unconfigure (self + %d device(s))", unhidden);
    bool ok = run_cli_elevated(args);
    if (ok) LOG_I("HidHide unconfigured for this app (cloak-on left untouched)");
    return ok;
}
