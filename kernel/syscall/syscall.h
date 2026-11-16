#ifndef SYSCALL_SYSCALL_H
#define SYSCALL_SYSCALL_H

#include <stdint.h>

/*
 * System call implementation. The entry point kernel_syscall() is declared
 * in <arch/user.h> because the arch layer calls it; this header is for the
 * files of kernel/syscall.
 */

/* Every handler takes the six raw arguments and returns the result or a
 * negative error code. */
typedef int64_t (*syscall_handler_fn)(const uint64_t arguments[]);

/* sys_file.c */
int64_t sys_write(const uint64_t arguments[]);
int64_t sys_read(const uint64_t arguments[]);
int64_t sys_open(const uint64_t arguments[]);
int64_t sys_close(const uint64_t arguments[]);
int64_t sys_seek(const uint64_t arguments[]);
int64_t sys_stat(const uint64_t arguments[]);
int64_t sys_readdir(const uint64_t arguments[]);

/* sys_process.c */
int64_t sys_exit(const uint64_t arguments[]);
int64_t sys_mmap(const uint64_t arguments[]);
int64_t sys_munmap(const uint64_t arguments[]);
int64_t sys_spawn(const uint64_t arguments[]);
int64_t sys_wait(const uint64_t arguments[]);
int64_t sys_getpid(const uint64_t arguments[]);
int64_t sys_sleep_ms(const uint64_t arguments[]);
int64_t sys_yield(const uint64_t arguments[]);
int64_t sys_uptime_ms(const uint64_t arguments[]);
int64_t sys_clock_ns(const uint64_t arguments[]);
int64_t sys_wall_time(const uint64_t arguments[]);
int64_t sys_detach(const uint64_t arguments[]);
int64_t sys_sysinfo(const uint64_t arguments[]);

/* sys_ipc.c */
int64_t sys_fstat(const uint64_t arguments[]);
int64_t sys_poll(const uint64_t arguments[]);
int64_t sys_channel_create(const uint64_t arguments[]);
int64_t sys_port_create(const uint64_t arguments[]);
int64_t sys_port_accept(const uint64_t arguments[]);
int64_t sys_port_connect(const uint64_t arguments[]);
int64_t sys_send(const uint64_t arguments[]);
int64_t sys_recv(const uint64_t arguments[]);
int64_t sys_shm_create(const uint64_t arguments[]);
int64_t sys_shm_map(const uint64_t arguments[]);
int64_t sys_fb_info(const uint64_t arguments[]);
int64_t sys_fb_map(const uint64_t arguments[]);

#endif
