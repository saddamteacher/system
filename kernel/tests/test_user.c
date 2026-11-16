/* Tests that run programs in user mode (the files in user/tests). */
#include "tests_internal.h"

#include <stddef.h>

#include <saddev/syscall.h>

#include <core/log.h>
#include <mm/heap.h>
#include <mm/pmm.h>
#include <sched/process.h>
#include <sched/thread.h>

#define PAGE_FAULT_VECTOR 14

/* Runs a program to completion and compares its exit code. */
static bool run_program(const char *path, const char *argument, int expected_exit_code)
{
    const char *argv[2] = { path, argument };
    int pid = process_spawn(path, argument != NULL ? 2 : 1, argv, NULL);
    if (pid < 0) {
        log_error("user test: cannot start %s: error %d", path, -pid);
        return false;
    }
    int exit_code = -1;
    if (process_wait(pid, &exit_code) != 0) {
        log_error("user test: lost track of %s", path);
        return false;
    }
    if (exit_code != expected_exit_code) {
        log_error("user test: %s exited with %d, expected %d", path, exit_code, expected_exit_code);
        return false;
    }
    return true;
}

bool test_boot(void)
{
    return run_program("/bin/init", "--once", 0);
}

bool test_user_hello(void)
{
    return run_program("/test/hello", NULL, 0);
}

bool test_user_syscalls(void)
{
    return run_program("/test/syscalls", NULL, 0);
}

bool test_user_files(void)
{
    return run_program("/test/files", NULL, 0);
}

bool test_user_malloc(void)
{
    return run_program("/test/malloc", NULL, 0);
}

bool test_user_spawn(void)
{
    return run_program("/test/spawn", NULL, 0);
}

bool test_user_fpu(void)
{
    return run_program("/test/fpu", NULL, 0);
}

bool test_user_sleep(void)
{
    return run_program("/test/sleep", NULL, 0);
}

bool test_user_gfx(void)
{
    return run_program("/test/gfx", NULL, 0);
}

/* A crashing program is ended by the kernel, which then keeps working:
 * the proof is that a normal program still runs afterwards. */
bool test_user_fault(void)
{
    return run_program("/test/fault", NULL, SD_EXIT_FAULT_BASE + PAGE_FAULT_VECTOR)
        && run_program("/test/fault", "kernel", SD_EXIT_FAULT_BASE + PAGE_FAULT_VECTOR)
        && run_program("/test/hello", NULL, 0);
}

/*
 * Runs a program twice and requires the second run to leave the kernel's
 * memory exactly as it found it. The first run is a warm-up: it creates
 * page tables that are kept for reuse (see test_thread_churn).
 */
static bool run_program_leak_checked(const char *path)
{
    if (!run_program(path, NULL, 0)) {
        return false;
    }
    thread_sleep_ms(50); /* let the idle thread free ended threads */
    size_t frames_before = pmm_stats().free_frames;
    size_t heap_before = heap_stats().used_bytes;

    if (!run_program(path, NULL, 0)) {
        return false;
    }
    thread_sleep_ms(50);
    size_t frames_after = pmm_stats().free_frames;
    size_t heap_after = heap_stats().used_bytes;
    if (frames_after != frames_before || heap_after != heap_before || process_count() != 0) {
        log_error("user test: %s leaked: frames %zu -> %zu, heap %zu -> %zu, processes %zu", path, frames_before,
                  frames_after, heap_before, heap_after, process_count());
        return false;
    }
    return true;
}

bool test_user_ipc(void)
{
    return run_program_leak_checked("/test/ipc");
}

bool test_user_ipc_edge(void)
{
    return run_program_leak_checked("/test/ipc-edge");
}

#define CHURN_ROUNDS 20

static bool churn_once(void)
{
    for (int round = 0; round < CHURN_ROUNDS; round++) {
        /* Alternate a clean exit with a crash: both must free everything. */
        bool ok = round % 2 == 0 ? run_program("/test/hello", NULL, 0)
                                 : run_program("/test/fault", NULL, SD_EXIT_FAULT_BASE + PAGE_FAULT_VECTOR);
        if (!ok) {
            return false;
        }
    }
    /* The threads of ended processes are freed by the idle thread. */
    thread_sleep_ms(50);
    return true;
}

bool test_user_churn(void)
{
    /* First pass warms up (page tables, see test_thread_churn). */
    if (!churn_once()) {
        return false;
    }
    size_t frames_before = pmm_stats().free_frames;
    size_t heap_before = heap_stats().used_bytes;

    if (!churn_once()) {
        return false;
    }
    size_t frames_after = pmm_stats().free_frames;
    size_t heap_after = heap_stats().used_bytes;
    log_info("user churn: frames %zu -> %zu, heap %zu -> %zu, processes left %zu", frames_before, frames_after,
             heap_before, heap_after, process_count());

    return frames_after == frames_before && heap_after == heap_before && process_count() == 0;
}
