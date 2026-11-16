/*
 * State and ownership:
 *   sink        written once by log_set_sink() during single-threaded boot,
 *               read-only afterwards.
 *   panic_mode  set once by log_enter_panic_mode(), never cleared.
 *   lock        serialises all writes to the sink.
 */
#include <core/log.h>

#include <stdatomic.h>
#include <stdbool.h>

#include <core/printf.h>
#include <core/spinlock.h>

static log_sink_fn sink;
static atomic_bool panic_mode;
static struct spinlock lock = SPINLOCK_INIT;

/* Collects formatted characters so the sink is called per chunk rather than
 * per character. Lives on the caller's stack, so it needs no lock itself. */
struct chunk {
    char data[128];
    size_t used;
};

static void chunk_flush(struct chunk *chunk)
{
    if (sink != NULL && chunk->used > 0) {
        sink(chunk->data, chunk->used);
    }
    chunk->used = 0;
}

static void chunk_emit(void *context, char character)
{
    struct chunk *chunk = context;
    if (chunk->used == sizeof(chunk->data)) {
        chunk_flush(chunk);
    }
    chunk->data[chunk->used++] = character;
}

static void chunk_emit_text(struct chunk *chunk, const char *text)
{
    for (; *text != '\0'; text++) {
        chunk_emit(chunk, *text);
    }
}

/* prefix and suffix may be NULL. */
static void write_locked(const char *prefix, const char *format, va_list arguments, const char *suffix)
{
    bool use_lock = !atomic_load(&panic_mode);
    arch_irq_state_t irq_state = 0;
    if (use_lock) {
        irq_state = spinlock_acquire(&lock);
    }

    struct chunk chunk;
    chunk.used = 0;
    if (prefix != NULL) {
        chunk_emit_text(&chunk, prefix);
    }
    kvformat(chunk_emit, &chunk, format, arguments);
    if (suffix != NULL) {
        chunk_emit_text(&chunk, suffix);
    }
    chunk_flush(&chunk);

    if (use_lock) {
        spinlock_release(&lock, irq_state);
    }
}

void log_set_sink(log_sink_fn new_sink)
{
    sink = new_sink;
}

void log_enter_panic_mode(void)
{
    atomic_store(&panic_mode, true);
}

void kvprintf(const char *format, va_list arguments)
{
    write_locked(NULL, format, arguments, NULL);
}

void kprintf(const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    kvprintf(format, arguments);
    va_end(arguments);
}

void log_write_raw(const char *text, size_t length)
{
    bool use_lock = !atomic_load(&panic_mode);
    arch_irq_state_t irq_state = 0;
    if (use_lock) {
        irq_state = spinlock_acquire(&lock);
    }
    if (sink != NULL && length > 0) {
        sink(text, length);
    }
    if (use_lock) {
        spinlock_release(&lock, irq_state);
    }
}

static const char *level_prefix(enum log_level level)
{
    switch (level) {
    case LOG_DEBUG:
        return "[debug] ";
    case LOG_INFO:
        return "[info]  ";
    case LOG_WARN:
        return "[warn]  ";
    case LOG_ERROR:
        return "[error] ";
    }
    return "[?]     ";
}

void log_write(enum log_level level, const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    write_locked(level_prefix(level), format, arguments, "\n");
    va_end(arguments);
}
