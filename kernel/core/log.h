#ifndef CORE_LOG_H
#define CORE_LOG_H

#include <stdarg.h>
#include <stddef.h>

/*
 * Kernel log. Output goes to a single sink installed at boot (the serial
 * driver). Each call is written atomically: lines from different contexts
 * never interleave.
 */

typedef void (*log_sink_fn)(const char *text, size_t length);

/* Installs the output device. Call once, early in boot, before any other
 * CPU or interrupt can log. Until then all output is discarded. */
void log_set_sink(log_sink_fn sink);

/* Raw formatted output, no prefix and no automatic newline. */
void kprintf(const char *format, ...) __attribute__((format(printf, 1, 2)));
void kvprintf(const char *format, va_list arguments);

/* Writes bytes as they are, with no formatting. */
void log_write_raw(const char *text, size_t length);

enum log_level { LOG_DEBUG, LOG_INFO, LOG_WARN, LOG_ERROR };

/* One log line: "[level] message\n". */
void log_write(enum log_level level, const char *format, ...) __attribute__((format(printf, 2, 3)));

#define log_debug(...) log_write(LOG_DEBUG, __VA_ARGS__)
#define log_info(...)  log_write(LOG_INFO, __VA_ARGS__)
#define log_warn(...)  log_write(LOG_WARN, __VA_ARGS__)
#define log_error(...) log_write(LOG_ERROR, __VA_ARGS__)

/*
 * Makes all later output bypass the log lock. Only for panic(): the lock may
 * be held by the very code that crashed, and a panic must still get its
 * message out. Irreversible.
 */
void log_enter_panic_mode(void);

#endif
