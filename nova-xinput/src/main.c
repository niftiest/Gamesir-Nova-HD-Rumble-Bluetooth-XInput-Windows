/*
 * main.c -- gamesir-nova-hd-xinput entrypoint.
 *
 * Flow:
 *   1. Parse CLI args (--no-tray, --no-bootstrap, --no-hidhide, --no-hidhide-config, --debug)
 *   2. Acquire single-instance mutex
 *   3. Init logger
 *   4. Bootstrap ViGEmBus (required) + HidHide (optional)
 *   5. Connect ViGEm
 *   6. Auto-config HidHide (add self to allow-list)
 *   7. Load profiles from <exe_dir>\profiles\*.json
 *   8. Init runloop, register profiles
 *   9. If --no-tray: refresh devices, sleep until Ctrl+C, then shutdown
 *      Else: tray_init, runloop_refresh, tray_loop until quit
 */

#ifndef _DEBUG
#pragma comment(linker, "/SUBSYSTEM:windows /ENTRY:mainCRTStartup")
#endif

#include "tray.h"
#include "log.h"
#include "bootstrap.h"
#include "hidhide.h"
#include "runloop.h"
#include "state_to_xusb.h"
#include "profile.h"
#include "hid.h"

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma comment(lib, "advapi32.lib")

#define APP_VERSION       "1.0.0"
#define MUTEX_NAME        L"Global\\GamesirNovaHDRumbleXInput.Singleton"
#define RUN_KEY_PATH      L"Software\\Microsoft\\Windows\\CurrentVersion\\Run"
#define RUN_KEY_VALUE     L"GamesirNovaHDRumbleXInput"
#define SETTINGS_KEY_PATH L"Software\\GamesirNovaHDRumbleXInput"
#define SETTINGS_VAL_SWAP L"swap_face_buttons"
#define MAX_LOADED_PROFILES 16

typedef struct {
    bool no_tray;
    bool no_bootstrap;
    bool no_hidhide;
    bool no_hidhide_config;
    bool debug;
    bool configure_hidhide;  /* one-shot: configure HidHide via UAC then continue running */
    int  swap_face_buttons;  /* -1 = not specified, 0 = off, 1 = on (overrides registry) */
} cli_args_t;

static cli_args_t       g_args;
static PVIGEM_CLIENT    g_vigem = NULL;
static profile_t       *g_loaded[MAX_LOADED_PROFILES];
static int              g_loaded_count = 0;
static bool             g_running = true;

/* Forward */
static void rebuild_tray_menu(void);
static void cb_refresh(struct tray_menu *item);
static void cb_quit(struct tray_menu *item);
static void cb_open_log_folder(struct tray_menu *item);
static void cb_toggle_startup(struct tray_menu *item);
static void cb_toggle_swap_buttons(struct tray_menu *item);
static void cb_configure_hidhide(struct tray_menu *item);
static bool startup_entry_exists(void);
static void startup_entry_set(bool enabled);
static bool swap_setting_load(void);
static void swap_setting_save(bool enabled);
static void do_configure_hidhide(void);

static struct tray_menu mi_count    = { .text = (LPTSTR)L"0 devices connected", .disabled = TRUE };
static struct tray_menu mi_sep1     = { .text = (LPTSTR)L"-" };
static struct tray_menu mi_refresh  = { .text = (LPTSTR)L"Refresh", .cb = cb_refresh };
static struct tray_menu mi_swap     = { .text = (LPTSTR)L"Swap A/B and X/Y (Xbox layout)", .cb = cb_toggle_swap_buttons };
static struct tray_menu mi_hidhide  = { .text = (LPTSTR)L"Configure HidHide for this app (admin)", .cb = cb_configure_hidhide };
static struct tray_menu mi_startup  = { .text = (LPTSTR)L"Start with Windows", .cb = cb_toggle_startup };
static struct tray_menu mi_log      = { .text = (LPTSTR)L"Open log folder", .cb = cb_open_log_folder };
static struct tray_menu mi_sep2     = { .text = (LPTSTR)L"-" };
static struct tray_menu mi_quit     = { .text = (LPTSTR)L"Quit", .cb = cb_quit };
static struct tray_menu mi_term     = { .text = NULL };
static struct tray_menu menu_storage[10];
static struct tray g_tray = { .icon = (LPTSTR)L"APP_ICON", .tip = (LPTSTR)L"Gamesir Nova HD Rumble XInput", .menu = NULL };

static void parse_args(int argc, char **argv)
{
    memset(&g_args, 0, sizeof(g_args));
    g_args.swap_face_buttons = -1;  /* unspecified — fall back to registry */
    for (int i = 1; i < argc; i++) {
        if      (strcmp(argv[i], "--no-tray") == 0)            g_args.no_tray = true;
        else if (strcmp(argv[i], "--no-bootstrap") == 0)       g_args.no_bootstrap = true;
        else if (strcmp(argv[i], "--no-hidhide") == 0)         g_args.no_hidhide = true;
        else if (strcmp(argv[i], "--no-hidhide-config") == 0)  g_args.no_hidhide_config = true;
        else if (strcmp(argv[i], "--debug") == 0)              g_args.debug = true;
        else if (strcmp(argv[i], "--swap-face-buttons") == 0)  g_args.swap_face_buttons = 1;
        else if (strcmp(argv[i], "--no-swap-face-buttons") == 0) g_args.swap_face_buttons = 0;
        else if (strcmp(argv[i], "--configure-hidhide") == 0)   g_args.configure_hidhide = true;
    }
    char *env = getenv("NOVAXINPUT_DEBUG");
    if (env && strcmp(env, "1") == 0) g_args.debug = true;
}

static void exe_dir(wchar_t *out, size_t n)
{
    GetModuleFileNameW(NULL, out, (DWORD)n);
    wchar_t *slash = wcsrchr(out, L'\\');
    if (slash) *slash = L'\0';
}

static int load_all_profiles(void)
{
    wchar_t dir[MAX_PATH]; exe_dir(dir, MAX_PATH);
    wchar_t pattern[MAX_PATH];
    swprintf_s(pattern, MAX_PATH, L"%s\\profiles\\*.json", dir);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) {
        char dir_utf8[MAX_PATH * 2];
        WideCharToMultiByte(CP_UTF8, 0, dir, -1, dir_utf8, sizeof(dir_utf8), NULL, NULL);
        LOG_W("no profiles found at %s\\profiles", dir_utf8);
        return 0;
    }
    int loaded = 0;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        if (loaded >= MAX_LOADED_PROFILES) { LOG_W("profile cap (%d) reached", MAX_LOADED_PROFILES); break; }
        wchar_t fpathW[MAX_PATH];
        swprintf_s(fpathW, MAX_PATH, L"%s\\profiles\\%s", dir, fd.cFileName);
        char fpath[MAX_PATH];
        WideCharToMultiByte(CP_UTF8, 0, fpathW, -1, fpath, MAX_PATH, NULL, NULL);
        char err[PROFILE_ERR_LEN] = {0};
        profile_t *p = profile_load_from_file(fpath, err, sizeof(err));
        if (!p) { LOG_E("profile %s: %s", fpath, err); continue; }
        g_loaded[loaded++] = p;
        LOG_I("loaded profile: %s (vid=0x%04X pid=0x%04X)", p->name, p->vid, p->pid);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return loaded;
}

static void rebuild_tray_menu(void)
{
    wchar_t buf[64];
    swprintf_s(buf, 64, L"%d device(s) connected", runloop_active_count());
    /* tray.h takes a static pointer; we keep buf in static-storage to avoid lifetime issues. */
    static wchar_t s_count_text[64];
    wcscpy_s(s_count_text, 64, buf);
    mi_count.text = (LPTSTR)s_count_text;
    mi_startup.checked = startup_entry_exists() ? TRUE : FALSE;
    mi_swap.checked    = gamepad_state_swap_face_buttons ? TRUE : FALSE;
    mi_hidhide.checked = (hidhide_is_installed() && hidhide_query_self_allowed()) ? TRUE : FALSE;

    int i = 0;
    menu_storage[i++] = mi_count;
    menu_storage[i++] = mi_sep1;
    menu_storage[i++] = mi_refresh;
    menu_storage[i++] = mi_swap;
    if (hidhide_is_installed()) menu_storage[i++] = mi_hidhide;
    menu_storage[i++] = mi_startup;
    menu_storage[i++] = mi_log;
    menu_storage[i++] = mi_sep2;
    menu_storage[i++] = mi_quit;
    menu_storage[i++] = mi_term;
    g_tray.menu = menu_storage;
}

static void cb_refresh(struct tray_menu *item)
{
    (void)item;
    /* IMPORTANT: detach all active devices and clear the runloop's profile
     * registry BEFORE we free the profile pointers. Otherwise g_devices[].profile
     * and g_profiles[] hold dangling pointers and the next runloop_refresh()
     * dereferences freed memory (crashes the app). */
    runloop_clear();

    for (int i = 0; i < g_loaded_count; i++) profile_free(g_loaded[i]);
    g_loaded_count = 0;
    g_loaded_count = load_all_profiles();

    for (int i = 0; i < g_loaded_count; i++) runloop_register_profile(g_loaded[i]);
    runloop_refresh();
    rebuild_tray_menu();
    if (!g_args.no_tray) tray_update(&g_tray);
}

static void cb_quit(struct tray_menu *item)  { (void)item; tray_exit(); g_running = false; }
static void cb_open_log_folder(struct tray_menu *item)
{
    (void)item;
    const char *dir = log_directory();
    if (dir) ShellExecuteA(NULL, "open", dir, NULL, NULL, SW_SHOWNORMAL);
}

static bool startup_entry_exists(void)
{
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, RUN_KEY_PATH, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) return false;
    LONG r = RegQueryValueExW(key, RUN_KEY_VALUE, NULL, NULL, NULL, NULL);
    RegCloseKey(key);
    return r == ERROR_SUCCESS;
}

static void startup_entry_set(bool enabled)
{
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, RUN_KEY_PATH, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS) return;
    if (enabled) {
        wchar_t exe[MAX_PATH]; GetModuleFileNameW(NULL, exe, MAX_PATH);
        wchar_t quoted[MAX_PATH + 4]; swprintf_s(quoted, MAX_PATH + 4, L"\"%s\"", exe);
        RegSetValueExW(key, RUN_KEY_VALUE, 0, REG_SZ, (const BYTE *)quoted, (DWORD)((wcslen(quoted) + 1) * sizeof(wchar_t)));
    } else {
        RegDeleteValueW(key, RUN_KEY_VALUE);
    }
    RegCloseKey(key);
}

static void cb_toggle_startup(struct tray_menu *item)
{
    bool now = startup_entry_exists();
    startup_entry_set(!now);
    item->checked = !now ? TRUE : FALSE;
    tray_update(&g_tray);
}

static bool swap_setting_load(void)
{
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, SETTINGS_KEY_PATH, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) return false;
    DWORD val = 0, len = sizeof(val), type = 0;
    LONG r = RegQueryValueExW(key, SETTINGS_VAL_SWAP, NULL, &type, (LPBYTE)&val, &len);
    RegCloseKey(key);
    return (r == ERROR_SUCCESS && type == REG_DWORD && val != 0);
}

static void swap_setting_save(bool enabled)
{
    HKEY key;
    DWORD disp;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, SETTINGS_KEY_PATH, 0, NULL, 0, KEY_SET_VALUE, NULL, &key, &disp) != ERROR_SUCCESS) return;
    DWORD val = enabled ? 1 : 0;
    RegSetValueExW(key, SETTINGS_VAL_SWAP, 0, REG_DWORD, (const BYTE *)&val, sizeof(val));
    RegCloseKey(key);
}

static void cb_toggle_swap_buttons(struct tray_menu *item)
{
    gamepad_state_swap_face_buttons = !gamepad_state_swap_face_buttons;
    swap_setting_save(gamepad_state_swap_face_buttons);
    item->checked = gamepad_state_swap_face_buttons ? TRUE : FALSE;
    LOG_I("face-button swap: %s", gamepad_state_swap_face_buttons ? "ON" : "off");
    tray_update(&g_tray);
}

static void do_configure_hidhide(void)
{
    if (!hidhide_is_installed()) {
        LOG_W("HidHide not installed; cannot configure");
        return;
    }
    wchar_t *paths[MAX_ACTIVE_DEVICES] = {0};
    int n = runloop_snapshot_paths(paths, MAX_ACTIVE_DEVICES);
    if (n == 0) LOG_W("HidHide configure: no devices currently attached; will only register self");
    bool ok = hidhide_configure_for_self_and_devices((const wchar_t **)paths, n);
    for (int i = 0; i < n; i++) free(paths[i]);
    if (ok) LOG_I("HidHide configure: success — restart any games for hiding to take effect");
    else    LOG_W("HidHide configure: failed (UAC declined or HidHideCLI returned non-zero)");
}

static void do_unconfigure_hidhide(void)
{
    if (!hidhide_is_installed()) return;
    wchar_t *paths[MAX_ACTIVE_DEVICES] = {0};
    int n = runloop_snapshot_paths(paths, MAX_ACTIVE_DEVICES);
    bool ok = hidhide_unconfigure_for_self_and_devices((const wchar_t **)paths, n);
    for (int i = 0; i < n; i++) free(paths[i]);
    if (ok) LOG_I("HidHide unconfigure: success");
    else    LOG_W("HidHide unconfigure: failed (UAC declined or HidHideCLI returned non-zero)");
}

static void cb_configure_hidhide(struct tray_menu *item)
{
    (void)item;
    /* Toggle: if currently in HidHide allow-list, remove; otherwise add. */
    if (hidhide_is_installed() && hidhide_query_self_allowed()) {
        do_unconfigure_hidhide();
    } else {
        do_configure_hidhide();
    }
    rebuild_tray_menu();
    if (!g_args.no_tray) tray_update(&g_tray);
}

static void on_device_change(UINT op, LPTSTR path)
{
    (void)op; (void)path;
    runloop_refresh();
    rebuild_tray_menu();
    if (!g_args.no_tray) tray_update(&g_tray);
}

/* Ctrl+C / Ctrl+Break / console-close handler for --no-tray mode.
 * Returns TRUE = "we handled it" so the OS doesn't terminate the process
 * before our shutdown loop runs. */
static BOOL WINAPI console_ctrl_handler(DWORD ctrl_type)
{
    switch (ctrl_type) {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_CLOSE_EVENT:
    case CTRL_LOGOFF_EVENT:
    case CTRL_SHUTDOWN_EVENT:
        g_running = false;
        return TRUE;
    default:
        return FALSE;
    }
}

int main(int argc, char **argv)
{
    parse_args(argc, argv);

    /* 0. /SUBSYSTEM:windows means stdout/stderr are not connected to anything
     * by default. In --no-tray mode we want printf and the log mirror to show
     * up in the parent cmd window — attach to it now, BEFORE log_init, so all
     * subsequent LOG_* writes (which may mirror to stderr) reach the user. */
    if (g_args.no_tray) {
        if (!AttachConsole(ATTACH_PARENT_PROCESS)) AllocConsole();
        freopen("CONOUT$", "w", stdout);
        freopen("CONOUT$", "w", stderr);
    }

    /* 1. Single-instance mutex */
    HANDLE mutex = CreateMutexW(NULL, TRUE, MUTEX_NAME);
    if (!mutex || GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(NULL, L"Gamesir Nova HD Rumble XInput is already running.",
                    L"Already running", MB_OK | MB_ICONINFORMATION);
        return 1;
    }

    /* 2. Logger */
    log_init();
    if (g_args.debug) log_set_level(LOG_DEBUG);
    if (g_args.no_tray) log_set_mirror_stderr(true);

    LOG_I("gamesir-nova-hd-xinput v%s starting (no_tray=%d, no_bootstrap=%d)",
          APP_VERSION, g_args.no_tray, g_args.no_bootstrap);

    /* Face-button swap: CLI flag overrides registry; otherwise persist. */
    if (g_args.swap_face_buttons >= 0) {
        gamepad_state_swap_face_buttons = (g_args.swap_face_buttons != 0);
    } else {
        gamepad_state_swap_face_buttons = swap_setting_load();
    }
    LOG_I("face-button swap: %s", gamepad_state_swap_face_buttons ? "ON (Xbox layout)" : "off (controller's native layout)");

    /* Detect elevation — HidHide IOCTLs to modify allow-list / hide-list
     * require Administrator. Log it so users hitting HidHide errors know why. */
    {
        HANDLE token = NULL;
        TOKEN_ELEVATION elev = {0};
        DWORD len = 0;
        BOOL elevated = FALSE;
        if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token) &&
            GetTokenInformation(token, TokenElevation, &elev, sizeof(elev), &len)) {
            elevated = elev.TokenIsElevated;
        }
        if (token) CloseHandle(token);
        LOG_I("elevation: %s", elevated ? "Administrator" : "standard user");
    }

    /* 3. Bootstrap ViGEmBus (required) */
    if (bootstrap_ensure_vigembus(true, g_args.no_bootstrap) != BS_OK) {
        LOG_E("ViGEmBus unavailable; exiting.");
        log_shutdown();
        ReleaseMutex(mutex); CloseHandle(mutex);
        return 2;
    }

    /* 4. Bootstrap HidHide (optional) */
    if (!g_args.no_hidhide) bootstrap_ensure_hidhide(true, g_args.no_bootstrap);

    /* 5. Connect ViGEm */
    g_vigem = vigem_alloc();
    if (!g_vigem || vigem_connect(g_vigem) != VIGEM_ERROR_NONE) {
        LOG_E("vigem_connect failed unexpectedly after bootstrap; exiting.");
        if (g_vigem) vigem_free(g_vigem);
        log_shutdown();
        ReleaseMutex(mutex); CloseHandle(mutex);
        return 3;
    }

    /* 6. HidHide check — query only (no UAC). If not configured, hint to user. */
    if (!g_args.no_hidhide_config && hidhide_is_installed()) {
        if (hidhide_query_self_allowed()) {
            LOG_I("HidHide: this app is already in the allow-list (configured)");
        } else {
            LOG_W("HidHide: this app is NOT in the allow-list — you may see double inputs in some games. "
                  "Use the tray menu or --configure-hidhide to fix (one UAC prompt).");
        }
    }

    /* 7. Load profiles */
    g_loaded_count = load_all_profiles();
    LOG_I("Loaded profiles: %d", g_loaded_count);

    /* 8. Init runloop, register profiles, initial refresh */
    runloop_init(g_vigem);
    for (int i = 0; i < g_loaded_count; i++) runloop_register_profile(g_loaded[i]);
    runloop_refresh();

    /* 8b. One-shot --configure-hidhide: trigger the elevated configure now,
     * after refresh so we capture currently-attached devices. */
    if (g_args.configure_hidhide) do_configure_hidhide();

    /* 9. Tray vs no-tray */
    if (g_args.no_tray) {
        /* /SUBSYSTEM:windows means we have no console by default. Attach to
         * the parent process's console (cmd.exe / PowerShell) so printf and
         * the stderr-mirrored log writes are actually visible. If there is
         * no parent console (launched from Explorer, etc.), allocate one. */
        if (!AttachConsole(ATTACH_PARENT_PROCESS)) AllocConsole();
        freopen("CONOUT$", "w", stdout);
        freopen("CONOUT$", "w", stderr);
        printf("gamesir-nova-hd-xinput v%s\n", APP_VERSION);
        printf("ViGEmBus: connected\n");
        printf("HidHide:  %s\n", bootstrap_detect_hidhide() == BS_OK ? "configured" : "not installed");
        printf("Loaded profiles: %d\n", g_loaded_count);
        printf("Press Ctrl+C to quit.\n");
        SetConsoleCtrlHandler(console_ctrl_handler, TRUE);
        while (g_running) Sleep(500);
    } else {
        rebuild_tray_menu();
        if (tray_init(&g_tray) < 0) {
            LOG_E("tray_init failed; falling back to --no-tray");
            g_args.no_tray = true;
            log_set_mirror_stderr(true);
            while (g_running) Sleep(500);
        } else {
            tray_register_device_notification(hid_get_class(), on_device_change);
            while (g_running && tray_loop(TRUE) == 0) { /* loop */ }
        }
    }

    /* Shutdown */
    runloop_shutdown();
    if (g_vigem) { vigem_disconnect(g_vigem); vigem_free(g_vigem); }
    for (int i = 0; i < g_loaded_count; i++) profile_free(g_loaded[i]);
    log_shutdown();
    ReleaseMutex(mutex); CloseHandle(mutex);
    return 0;
}
