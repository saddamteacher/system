#ifndef FS_POLL_H
#define FS_POLL_H

#include <stddef.h>
#include <stdint.h>

#include <saddev/syscall.h>

/*
 * Waiting for any of several files to become ready.
 *
 * Every file reports its readiness on demand (file_ops.poll). Whoever
 * changes a file's readiness (a message arrives, a peer closes, a key is
 * pressed) calls poll_notify(), and sleeping pollers look again.
 */

struct file;

struct poll_entry {
    struct file *file; /* NULL entries are skipped */
    uint16_t events;   /* SD_POLL_* the caller asks about */
    uint16_t revents;  /* result */
};

/*
 * Blocks until at least one entry has a requested event (or SD_POLL_HUP),
 * or `timeout_ms` passed (negative: no limit, 0: just look). Returns the
 * number of entries with a non-zero revents.
 */
int poll_wait(struct poll_entry *entries, size_t count, int64_t timeout_ms);

/* Wakes all pollers to re-examine their files. Safe from interrupt context.
 * Must not be called while holding a lock that a file's poll operation
 * takes. */
void poll_notify(void);

#endif
