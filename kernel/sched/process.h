#ifndef SCHED_PROCESS_H
#define SCHED_PROCESS_H

#include <stddef.h>
#include <stdint.h>

/*
 * Processes: a running user program, i.e. an address space, one thread
 * that executes in it, and its open files.
 */

struct file;
struct address_space;
struct process;

#define PROCESS_MAX_FILES 16

/* Installs the handler that ends a process on a CPU exception. Call once. */
void process_init(void);

/*
 * Starts the program at `path` with the given argument strings
 * (`argv[argc]` need not be NULL). Its standard files (descriptors 0-2)
 * are, in order of preference: the three files in `stdio` if it is not
 * NULL; those of `parent`; the console. Returns the new process id or a
 * negative error code.
 */
int process_spawn(const char *path, int argc, const char *const argv[], struct process *parent,
                  struct file *const stdio[3]);

/* Blocks until process `pid` has ended, stores its exit code and forgets
 * the process. Returns 0, or -ECHILD if there is no such process. */
int process_wait(int pid, int *exit_code);

/* Declares that nobody will wait for process `pid`: it is forgotten as soon
 * as it ends (at once, if it already has). Returns 0 or -ESRCH. */
int process_detach(int pid);

/* Ends the calling process. */
_Noreturn void process_exit(int exit_code);

/* The process the calling thread belongs to; NULL for kernel threads. */
struct process *process_current(void);

int process_id(const struct process *process);
struct address_space *process_address_space(struct process *process);

/*
 * The descriptor table of a process: small integers naming open files.
 * install: takes over the caller's reference; returns the descriptor or
 *          -EMFILE.
 * get:     the file, or NULL for an unused descriptor. The table keeps its
 *          reference; the result is valid while the process does not close
 *          it (a process has one thread, so it cannot do so concurrently).
 * remove:  closes a descriptor; -EBADF if it was not open.
 */
int process_file_install(struct process *process, struct file *file);
struct file *process_file_get(struct process *process, int descriptor);
int process_file_remove(struct process *process, int descriptor);

/* Number of processes that exist (running or not yet waited for). */
size_t process_count(void);

#endif
