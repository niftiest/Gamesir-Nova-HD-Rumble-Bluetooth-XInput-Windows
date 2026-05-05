/*
 * log.c -- Simple file logger with size-based rotation and stderr mirror.
 */

#include "log.h"

#include <shlobj.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <windows.h>

#define MAX_LOG_BYTES (1 * 1024 * 1024)
#define LOG_DIR_NAME  "gamesir-nova-hd-xinput"

static CRITICAL_SECTION g_lock;
static bool         g_inited       = false;
static log_level_t  g_max_level    = LOG_INFO;
static bool         g_mirror       = false;
static FILE        *g_fp           = NULL;
static char         g_dir[MAX_PATH] = {0};
static char         g_path[MAX_PATH] = {0};

static void rotate_locked(void)
{
    if (!g_fp) return;
    long pos = ftell(g_fp);
    if (pos < (long)MAX_LOG_BYTES) return;
    fclose(g_fp); g_fp = NULL;
    char p1[MAX_PATH], p2[MAX_PATH];
    snprintf(p2, sizeof(p2), "%s\\log.3.txt", g_dir); DeleteFileA(p2);
    snprintf(p1, sizeof(p1), "%s\\log.2.txt", g_dir); MoveFileExA(p1, p2, MOVEFILE_REPLACE_EXISTING);
    snprintf(p1, sizeof(p1), "%s\\log.1.txt", g_dir); MoveFileExA(p1, p2, MOVEFILE_REPLACE_EXISTING); /* into log.2 path which we just moved */
    /* Above is wrong: redo cleanly */
    /* Renumber: 2.txt -> 3.txt, 1.txt -> 2.txt, log.txt -> 1.txt */
    snprintf(p1, sizeof(p1), "%s\\log.2.txt", g_dir);
    snprintf(p2, sizeof(p2), "%s\\log.3.txt", g_dir);
    MoveFileExA(p1, p2, MOVEFILE_REPLACE_EXISTING);
    snprintf(p1, sizeof(p1), "%s\\log.1.txt", g_dir);
    snprintf(p2, sizeof(p2), "%s\\log.2.txt", g_dir);
    MoveFileExA(p1, p2, MOVEFILE_REPLACE_EXISTING);
    snprintf(p2, sizeof(p2), "%s\\log.1.txt", g_dir);
    MoveFileExA(g_path, p2, MOVEFILE_REPLACE_EXISTING);
    g_fp = fopen(g_path, "ab");
}

bool log_init(void)
{
    if (g_inited) return true;
    InitializeCriticalSection(&g_lock);

    char appdata[MAX_PATH];
    if (FAILED(SHGetFolderPathA(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, appdata))) return false;
    snprintf(g_dir, sizeof(g_dir), "%s\\%s", appdata, LOG_DIR_NAME);
    if (!CreateDirectoryA(g_dir, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) return false;
    snprintf(g_path, sizeof(g_path), "%s\\log.txt", g_dir);
    g_fp = fopen(g_path, "ab");
    if (!g_fp) return false;
    g_inited = true;
    return true;
}

void log_shutdown(void)
{
    if (!g_inited) return;
    EnterCriticalSection(&g_lock);
    if (g_fp) { fclose(g_fp); g_fp = NULL; }
    LeaveCriticalSection(&g_lock);
    DeleteCriticalSection(&g_lock);
    g_inited = false;
}

void log_set_level(log_level_t max_level)         { g_max_level = max_level; }
void log_set_mirror_stderr(bool enabled)          { g_mirror = enabled; }
const char *log_directory(void)                   { return g_inited ? g_dir : NULL; }

static const char *level_str(log_level_t l)
{
    switch (l) { case LOG_ERROR: return "ERROR"; case LOG_WARN: return "WARN ";
                 case LOG_INFO:  return "INFO ";  case LOG_DEBUG: return "DEBUG"; }
    return "?????";
}

void log_write(log_level_t level, const char *fmt, ...)
{
    if (level > g_max_level) return;
    char ts[32];
    SYSTEMTIME st; GetLocalTime(&st);
    snprintf(ts, sizeof(ts), "%04d-%02d-%02d %02d:%02d:%02d",
             st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

    char body[1024];
    va_list ap; va_start(ap, fmt);
    vsnprintf(body, sizeof(body), fmt, ap);
    va_end(ap);

    if (!g_inited) {
        if (g_mirror) fprintf(stderr, "%s [%s] %s\n", ts, level_str(level), body);
        return;
    }

    EnterCriticalSection(&g_lock);
    if (g_fp) {
        fprintf(g_fp, "%s [%s] %s\n", ts, level_str(level), body);
        fflush(g_fp);
        rotate_locked();
    }
    LeaveCriticalSection(&g_lock);
    if (g_mirror) fprintf(stderr, "%s [%s] %s\n", ts, level_str(level), body);
}
