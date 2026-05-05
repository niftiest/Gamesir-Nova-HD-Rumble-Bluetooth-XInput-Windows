/*
 * bootstrap.c -- Detect ViGEmBus + HidHide. Install via WinHTTP + CreateProcess.
 */

#include "bootstrap.h"
#include "log.h"

#include <windows.h>
#include <winhttp.h>
#include <shellapi.h>
#include <stdio.h>
#include <string.h>

/*
 * Workaround: ViGEm/Client.h (v1.21.222.0) uses C++-only `using` type-alias
 * syntax throughout its header, which MSVC rejects when compiling as C.
 * Instead of including Client.h directly we pull in Common.h (which is clean C)
 * and forward-declare the opaque handle plus the error enum that we need.
 */
#include <ViGEm/Common.h>

/* Forward-declare the C-callable API we use, since Client.h has C++-only
 * `using` syntax in this version that the C compiler rejects. */
typedef struct _VIGEM_CLIENT_T *PVIGEM_CLIENT;

typedef enum _VIGEM_ERRORS {
    VIGEM_ERROR_NONE                = 0x20000000,
    VIGEM_ERROR_BUS_NOT_FOUND       = 0xE0000001,
    VIGEM_ERROR_NO_FREE_SLOT        = 0xE0000002,
    VIGEM_ERROR_INVALID_TARGET      = 0xE0000003,
    VIGEM_ERROR_REMOVAL_FAILED      = 0xE0000004,
    VIGEM_ERROR_ALREADY_CONNECTED   = 0xE0000005,
    VIGEM_ERROR_TARGET_UNINITIALIZED= 0xE0000006,
    VIGEM_ERROR_TARGET_NOT_PLUGGED_IN = 0xE0000007,
    VIGEM_ERROR_BUS_VERSION_MISMATCH= 0xE0000008,
    VIGEM_ERROR_BUS_ACCESS_FAILED   = 0xE0000009,
    VIGEM_ERROR_BUS_ALREADY_CONNECTED = 0xE0000012,
} VIGEM_ERROR;

PVIGEM_CLIENT vigem_alloc(void);
VIGEM_ERROR   vigem_connect(PVIGEM_CLIENT vigem);
void          vigem_disconnect(PVIGEM_CLIENT vigem);
void          vigem_free(PVIGEM_CLIENT vigem);

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "shell32.lib")

/* === Detection === */

bootstrap_status_t bootstrap_detect_vigembus(void)
{
    PVIGEM_CLIENT client = vigem_alloc();
    if (!client) return BS_NOT_INSTALLED;
    VIGEM_ERROR err = vigem_connect(client);
    bootstrap_status_t status;
    if (err == VIGEM_ERROR_NONE) status = BS_OK;
    else if (err == VIGEM_ERROR_BUS_NOT_FOUND) status = BS_NOT_INSTALLED;
    else if (err == VIGEM_ERROR_BUS_VERSION_MISMATCH) status = BS_VERSION_MISMATCH;
    else status = BS_NOT_INSTALLED;
    if (status == BS_OK) vigem_disconnect(client);
    vigem_free(client);
    return status;
}

bootstrap_status_t bootstrap_detect_hidhide(void)
{
    HANDLE h = CreateFileW(L"\\\\.\\HidHide", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        DWORD e = GetLastError();
        /* ERROR_FILE_NOT_FOUND (2) / ERROR_PATH_NOT_FOUND (3) genuinely mean
         * not installed. Other errors (busy, share violation, etc.) mean it
         * IS installed but currently unreachable — log and treat as installed
         * to avoid prompting the user to "install" something that's already
         * there. */
        if (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND) return BS_NOT_INSTALLED;
        LOG_W("HidHide device open failed (err=%lu); assuming installed but busy", e);
        return BS_OK;
    }
    CloseHandle(h);
    return BS_OK;
}

/* === Install support === */

#define VIGEMBUS_REPO_OWNER  L"ViGEm"
#define VIGEMBUS_REPO_NAME   L"ViGEmBus"
#define HIDHIDE_REPO_OWNER   L"nefarius"
#define HIDHIDE_REPO_NAME    L"HidHide"

#define VIGEMBUS_FALLBACK_URL  L"https://github.com/ViGEm/ViGEmBus/releases/download/v1.22.0/ViGEmBus_1.22.0_x64_x86_arm64.exe"
#define HIDHIDE_FALLBACK_URL   L"https://github.com/nefarius/HidHide/releases/download/v1.5.230.0/HidHide_1.5.230_x64.exe"

/* Fetch a URL into a malloc'd buffer (caller frees). Returns NULL on error. */
static char *http_get_text(const wchar_t *host, const wchar_t *path, DWORD *out_len)
{
    HINTERNET hSession = WinHttpOpen(L"gamesir-nova-hd-xinput/0.1",
                                     WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return NULL;
    HINTERNET hConnect = WinHttpConnect(hSession, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return NULL; }
    HINTERNET hReq = WinHttpOpenRequest(hConnect, L"GET", path, NULL,
                                        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                        WINHTTP_FLAG_SECURE);
    if (!hReq) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return NULL; }
    /* Add Accept header for GitHub API */
    WinHttpAddRequestHeaders(hReq, L"Accept: application/vnd.github+json\r\n", (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD);
    if (!WinHttpSendRequest(hReq, WINHTTP_NO_ADDITIONAL_HEADERS, 0, NULL, 0, 0, 0) ||
        !WinHttpReceiveResponse(hReq, NULL)) {
        WinHttpCloseHandle(hReq); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
        return NULL;
    }

    char *buf = NULL; DWORD total = 0; DWORD avail = 0;
    do {
        if (!WinHttpQueryDataAvailable(hReq, &avail)) break;
        if (avail == 0) break;
        char *nb = realloc(buf, total + avail + 1);
        if (!nb) { free(buf); buf = NULL; break; }
        buf = nb;
        DWORD got = 0;
        if (!WinHttpReadData(hReq, buf + total, avail, &got)) { free(buf); buf = NULL; break; }
        total += got;
        buf[total] = '\0';
    } while (avail > 0);

    WinHttpCloseHandle(hReq); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
    if (out_len) *out_len = total;
    return buf;
}

/* Download a binary URL into a temp file. Returns true on success and writes path into out_path. */
static bool http_download_to_temp(const wchar_t *host, const wchar_t *path, wchar_t *out_path, size_t out_path_len)
{
    HINTERNET hSession = WinHttpOpen(L"gamesir-nova-hd-xinput/0.1",
                                     WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return false;
    HINTERNET hConnect = WinHttpConnect(hSession, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return false; }
    HINTERNET hReq = WinHttpOpenRequest(hConnect, L"GET", path, NULL,
                                        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                        WINHTTP_FLAG_SECURE);
    if (!hReq) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return false; }
    if (!WinHttpSendRequest(hReq, WINHTTP_NO_ADDITIONAL_HEADERS, 0, NULL, 0, 0, 0) ||
        !WinHttpReceiveResponse(hReq, NULL)) {
        WinHttpCloseHandle(hReq); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
        return false;
    }

    wchar_t tmpdir[MAX_PATH];
    GetTempPathW(MAX_PATH, tmpdir);
    swprintf_s(out_path, out_path_len, L"%snova-xinput-bootstrap-%lu.exe", tmpdir, GetTickCount());

    HANDLE hFile = CreateFileW(out_path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        WinHttpCloseHandle(hReq); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
        return false;
    }

    char buf[16384]; DWORD got = 0; bool ok = true;
    for (;;) {
        if (!WinHttpReadData(hReq, buf, sizeof(buf), &got)) { ok = false; break; }
        if (got == 0) break;
        DWORD wrote = 0;
        if (!WriteFile(hFile, buf, got, &wrote, NULL) || wrote != got) { ok = false; break; }
    }
    CloseHandle(hFile);
    WinHttpCloseHandle(hReq); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
    if (!ok) DeleteFileW(out_path);
    return ok;
}

/* Try to find an .exe asset URL inside a GitHub Releases JSON blob.
 * VERY simple substring scan rather than a full JSON parse — keeps the file dependency-free
 * for bootstrap. Returns true on match; copies HOST + PATH out. */
static bool find_exe_asset(const char *json, wchar_t *host, size_t host_len, wchar_t *path, size_t path_len)
{
    /* Look for "browser_download_url":"https://github.com/.../*.exe" */
    const char *p = json;
    while ((p = strstr(p, "\"browser_download_url\"")) != NULL) {
        const char *q = strchr(p, '"'); if (!q) break;
        q = strchr(q + 1, '"'); if (!q) break;
        q = strchr(q + 1, '"'); if (!q) break;
        const char *url_start = q + 1;
        const char *url_end = strchr(url_start, '"'); if (!url_end) break;
        size_t url_len = (size_t)(url_end - url_start);
        if (url_len < 12 || strncmp(url_end - 4, ".exe", 4) != 0) { p = url_end; continue; }
        /* Convert to wide and split into host/path */
        char url8[1024]; if (url_len >= sizeof(url8)) { p = url_end; continue; }
        memcpy(url8, url_start, url_len); url8[url_len] = '\0';
        const char *https = "https://"; if (strncmp(url8, https, 8) != 0) { p = url_end; continue; }
        char *slash = strchr(url8 + 8, '/'); if (!slash) { p = url_end; continue; }
        *slash = '\0';
        MultiByteToWideChar(CP_UTF8, 0, url8 + 8, -1, host, (int)host_len);
        MultiByteToWideChar(CP_UTF8, 0, slash + 1, -1, path + 1, (int)path_len - 1);
        path[0] = L'/';
        return true;
    }
    return false;
}

/* Resolve latest release asset; on any failure, fall back to FALLBACK_URL. */
static bool resolve_installer_url(const wchar_t *owner, const wchar_t *repo, const wchar_t *fallback,
                                   wchar_t *host, size_t host_len, wchar_t *path, size_t path_len)
{
    wchar_t api_path[256];
    swprintf_s(api_path, 256, L"/repos/%s/%s/releases/latest", owner, repo);
    DWORD len = 0;
    char *json = http_get_text(L"api.github.com", api_path, &len);
    if (json) {
        bool ok = find_exe_asset(json, host, host_len, path, path_len);
        free(json);
        if (ok) return true;
    }
    /* Fallback: parse fallback URL */
    wchar_t f[1024]; wcscpy_s(f, 1024, fallback);
    if (wcsncmp(f, L"https://", 8) != 0) return false;
    wchar_t *slash = wcschr(f + 8, L'/');
    if (!slash) return false;
    *slash = L'\0';
    wcscpy_s(host, host_len, f + 8);
    path[0] = L'/';
    wcscpy_s(path + 1, path_len - 1, slash + 1);
    return true;
}

/* Spawn an installer .exe and wait for exit. Returns true if the process exited
 * with status 0 (or 3010 = "needs reboot"). */
static bool run_installer(const wchar_t *exe_path)
{
    SHELLEXECUTEINFOW sei = { sizeof(sei) };
    sei.fMask  = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb = L"runas";  /* triggers UAC */
    sei.lpFile = exe_path;
    sei.nShow  = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&sei)) return false;
    if (!sei.hProcess) return false;
    WaitForSingleObject(sei.hProcess, INFINITE);
    DWORD code = 1; GetExitCodeProcess(sei.hProcess, &code);
    CloseHandle(sei.hProcess);
    return code == 0 || code == 3010;
}

static bootstrap_status_t install_dep(const wchar_t *owner, const wchar_t *repo,
                                      const wchar_t *fallback_url,
                                      const wchar_t *prompt_title, const wchar_t *prompt_text,
                                      bootstrap_status_t (*detect_fn)(void),
                                      bool optional)
{
    int btn = MessageBoxW(NULL, prompt_text, prompt_title,
                          (optional ? MB_YESNO : MB_OKCANCEL) | MB_ICONQUESTION);
    if (optional && btn == IDNO) { LOG_W("user declined optional dependency: %ls", repo); return BS_USER_CANCELLED; }
    if (!optional && btn != IDOK) { LOG_E("user cancelled required dependency: %ls", repo); return BS_USER_CANCELLED; }

    wchar_t host[256] = {0}, path[1024] = {0}, dl_path[MAX_PATH] = {0};
    if (!resolve_installer_url(owner, repo, fallback_url, host, 256, path, 1024)) {
        LOG_E("could not resolve installer URL for %ls (offline?)", repo);
        return BS_NETWORK_ERROR;
    }
    LOG_I("downloading %ls from https://%ls%ls", repo, host, path);
    if (!http_download_to_temp(host, path, dl_path, MAX_PATH)) {
        LOG_E("download failed for %ls", repo);
        return BS_NETWORK_ERROR;
    }
    LOG_I("running installer: %ls", dl_path);
    bool ran = run_installer(dl_path);
    DeleteFileW(dl_path);
    if (!ran) { LOG_E("installer for %ls returned non-success exit", repo); return BS_INSTALL_FAILED; }
    bootstrap_status_t after = detect_fn();
    if (after != BS_OK) { LOG_E("post-install detect failed for %ls (status %d)", repo, (int)after); return BS_INSTALL_FAILED; }
    LOG_I("%ls installed successfully", repo);
    return BS_OK;
}

bootstrap_status_t bootstrap_ensure_vigembus(bool silent_if_present, bool force_no_install)
{
    bootstrap_status_t s = bootstrap_detect_vigembus();
    if (s == BS_OK) { if (!silent_if_present) LOG_I("ViGEmBus: already installed"); return BS_OK; }
    if (force_no_install) {
        LOG_E("ViGEmBus not installed and --no-bootstrap specified. Install from: https://github.com/ViGEm/ViGEmBus/releases");
        MessageBoxW(NULL,
            L"ViGEmBus is required but not installed, and --no-bootstrap was given.\n\n"
            L"Install from: https://github.com/ViGEm/ViGEmBus/releases",
            L"Gamesir Nova HD Rumble XInput", MB_OK | MB_ICONERROR);
        return s;
    }
    return install_dep(VIGEMBUS_REPO_OWNER, VIGEMBUS_REPO_NAME, VIGEMBUS_FALLBACK_URL,
        L"Gamesir Nova HD Rumble XInput - First-run setup",
        L"This program needs the ViGEmBus driver from the ViGEm Project (open source).\n\n"
        L"Click OK to download and install it now (a UAC prompt will appear). "
        L"Cancel to exit and install it manually from "
        L"https://github.com/ViGEm/ViGEmBus/releases.",
        bootstrap_detect_vigembus, false);
}

bootstrap_status_t bootstrap_ensure_hidhide(bool silent_if_present, bool force_no_install)
{
    bootstrap_status_t s = bootstrap_detect_hidhide();
    if (s == BS_OK) { if (!silent_if_present) LOG_I("HidHide: already installed"); return BS_OK; }
    if (force_no_install) {
        LOG_W("HidHide not installed; double-input may occur. Install from: https://github.com/nefarius/HidHide/releases");
        return s;
    }
    return install_dep(HIDHIDE_REPO_OWNER, HIDHIDE_REPO_NAME, HIDHIDE_FALLBACK_URL,
        L"Gamesir Nova HD Rumble XInput - Optional setup",
        L"Optional: HidHide hides the real controller from games, so they only see the "
        L"virtual XInput pad we create. Without it, some games may register every input twice.\n\n"
        L"Install HidHide now? (UAC prompt will appear.) Cancel to skip - your controller "
        L"will still work, but you may experience double-input.",
        bootstrap_detect_hidhide, true);
}
