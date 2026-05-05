/*
 * log.h -- Simple thread-safe logger.
 * Writes to %LOCALAPPDATA%\gamesir-nova-hd-xinput\log.txt with size-based rotation
 * (keeps log.txt + log.1.txt + log.2.txt + log.3.txt). Also mirrors to stderr if
 * log_set_mirror_stderr(true) was called (--no-tray mode).
 */

#ifndef LOG_H
#define LOG_H

/* MSVC doesn't have stdbool.h; use our own bool definition */
#ifndef __cplusplus
#ifndef bool
#define bool int
#define true 1
#define false 0
#endif
#endif

typedef enum { LOG_ERROR = 0, LOG_WARN = 1, LOG_INFO = 2, LOG_DEBUG = 3 } log_level_t;

bool log_init(void);                                /* opens log file; idempotent. returns false on fail. */
void log_shutdown(void);
void log_set_level(log_level_t max_level);          /* default LOG_INFO */
void log_set_mirror_stderr(bool enabled);
void log_write(log_level_t level, const char *fmt, ...);

#define LOG_E(...) log_write(LOG_ERROR, __VA_ARGS__)
#define LOG_W(...) log_write(LOG_WARN,  __VA_ARGS__)
#define LOG_I(...) log_write(LOG_INFO,  __VA_ARGS__)
#define LOG_D(...) log_write(LOG_DEBUG, __VA_ARGS__)

/* Returns the path to the log directory (NUL-terminated, owned by logger).
 * Used by tray "Open log folder" menu item. NULL until log_init() succeeds. */
const char *log_directory(void);

#endif /* LOG_H */
