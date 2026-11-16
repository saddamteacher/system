/* Tests for kernel/sched. */
#include "tests_internal.h"

#include <stdatomic.h>
#include <stddef.h>

#include <core/log.h>
#include <core/time.h>
#include <mm/heap.h>
#include <mm/pmm.h>
#include <sched/mutex.h>
#include <sched/thread.h>

#define EXPECT(condition) \
    do { \
        if (!(condition)) { \
            log_error("%s:%d: expectation failed: %s", __FILE__, __LINE__, #condition); \
            return false; \
        } \
    } while (0)

/* ---------------------------------------------------------- preemption */

#define SPINNER_COUNT 3
#define SPIN_TIME_MS 300

struct spinner {
    int id;
    atomic_ulong iterations;
    atomic_ulong times_scheduled;
};

static atomic_bool spinners_stop;
static atomic_int last_spinner;

/* Never sleeps, blocks or yields. If the others get to run anyway, the
 * timer interrupt took the CPU away: that is preemption. */
static void spinner_thread(void *argument)
{
    struct spinner *self = argument;

    while (!atomic_load(&spinners_stop)) {
        atomic_fetch_add(&self->iterations, 1);
        /* Count how often the CPU changed hands between spinners. */
        if (atomic_exchange(&last_spinner, self->id) != self->id) {
            atomic_fetch_add(&self->times_scheduled, 1);
        }
    }
}

bool test_threads(void)
{
    static struct spinner spinners[SPINNER_COUNT];
    struct thread *threads[SPINNER_COUNT];

    atomic_store(&spinners_stop, false);
    atomic_store(&last_spinner, -1);
    for (int i = 0; i < SPINNER_COUNT; i++) {
        spinners[i].id = i;
        atomic_store(&spinners[i].iterations, 0);
        atomic_store(&spinners[i].times_scheduled, 0);
        threads[i] = thread_create("spinner", spinner_thread, &spinners[i]);
        EXPECT(threads[i] != NULL);
    }

    thread_sleep_ms(SPIN_TIME_MS);
    atomic_store(&spinners_stop, true);
    for (int i = 0; i < SPINNER_COUNT; i++) {
        thread_join(threads[i]);
    }

    unsigned long total = 0;
    for (int i = 0; i < SPINNER_COUNT; i++) {
        total += atomic_load(&spinners[i].iterations);
    }
    for (int i = 0; i < SPINNER_COUNT; i++) {
        unsigned long iterations = atomic_load(&spinners[i].iterations);
        unsigned long scheduled = atomic_load(&spinners[i].times_scheduled);
        log_info("threads test: spinner %d ran %lu iterations in %lu turns", i, iterations, scheduled);

        /* Round-robin: each of the three got about a third. Allow slack
         * for uneven emulation speed, but none may be starved. */
        EXPECT(iterations > total / (SPINNER_COUNT * 2));
        /* 300 ms / (3 threads * 10 ms slices) = 10 turns each; require half. */
        EXPECT(scheduled >= 5);
    }
    return true;
}

/* --------------------------------------------------------------- sleep */

#define SLEEPER_COUNT 3

static struct mutex wake_order_mutex;
static int wake_order[SLEEPER_COUNT];
static int wake_count;

static void sleeper_thread(void *argument)
{
    int milliseconds = (int)(uintptr_t)argument;
    thread_sleep_ms((uint64_t)milliseconds);

    mutex_lock(&wake_order_mutex);
    wake_order[wake_count++] = milliseconds;
    mutex_unlock(&wake_order_mutex);
}

bool test_sleep(void)
{
    /* A sleep lasts at least as long as asked, and not much longer. */
    uint64_t before = time_now_ms();
    thread_sleep_ms(50);
    uint64_t elapsed = time_now_ms() - before;
    log_info("sleep test: asked for 50 ms, slept %llu ms", (unsigned long long)elapsed);
    EXPECT(elapsed >= 50 && elapsed <= 60);

    /* Threads started in one order wake in the order of their deadlines. */
    static const int durations[SLEEPER_COUNT] = { 60, 20, 40 };
    struct thread *threads[SLEEPER_COUNT];

    mutex_init(&wake_order_mutex);
    wake_count = 0;
    for (int i = 0; i < SLEEPER_COUNT; i++) {
        threads[i] = thread_create("sleeper", sleeper_thread, (void *)(uintptr_t)durations[i]);
        EXPECT(threads[i] != NULL);
    }
    for (int i = 0; i < SLEEPER_COUNT; i++) {
        thread_join(threads[i]);
    }
    EXPECT(wake_count == SLEEPER_COUNT);
    EXPECT(wake_order[0] == 20 && wake_order[1] == 40 && wake_order[2] == 60);
    return true;
}

/* --------------------------------------------------------------- mutex */

#define WORKER_COUNT 4
#define INCREMENTS_PER_WORKER 500

static struct mutex counter_mutex;
static volatile unsigned long shared_counter;
static bool use_mutex;

/* Read, give the CPU away, write back. Without mutual exclusion another
 * worker updates the counter in between and its update is overwritten. */
static void counter_thread(void *argument)
{
    (void)argument;
    for (int i = 0; i < INCREMENTS_PER_WORKER; i++) {
        if (use_mutex) {
            mutex_lock(&counter_mutex);
        }
        unsigned long value = shared_counter;
        thread_yield();
        shared_counter = value + 1;
        if (use_mutex) {
            mutex_unlock(&counter_mutex);
        }
    }
}

static bool run_counter_workers(bool with_mutex, unsigned long *result)
{
    struct thread *threads[WORKER_COUNT];

    use_mutex = with_mutex;
    shared_counter = 0;
    for (int i = 0; i < WORKER_COUNT; i++) {
        threads[i] = thread_create("counter", counter_thread, NULL);
        EXPECT(threads[i] != NULL);
    }
    for (int i = 0; i < WORKER_COUNT; i++) {
        thread_join(threads[i]);
    }
    *result = shared_counter;
    return true;
}

bool test_mutex(void)
{
    unsigned long expected = WORKER_COUNT * INCREMENTS_PER_WORKER;
    unsigned long unprotected, protected;

    mutex_init(&counter_mutex);

    /* First show that the race is real, so that the second half proves
     * something: without the mutex updates must get lost. */
    EXPECT(run_counter_workers(false, &unprotected));
    EXPECT(run_counter_workers(true, &protected));
    log_info("mutex test: expected %lu, without mutex %lu, with mutex %lu", expected, unprotected, protected);

    EXPECT(unprotected < expected);
    EXPECT(protected == expected);
    return true;
}

/* --------------------------------------------------- create and release */

#define CHURN_ROUNDS 200

static atomic_ulong churn_ran;

static void churn_thread(void *argument)
{
    (void)argument;
    atomic_fetch_add(&churn_ran, 1);
}

static bool churn_once(void)
{
    atomic_store(&churn_ran, 0);
    for (int round = 0; round < CHURN_ROUNDS; round++) {
        struct thread *thread = thread_create("churn", churn_thread, NULL);
        EXPECT(thread != NULL);
        /* Alternate between the two ways a thread's memory is released. */
        if (round % 2 == 0) {
            thread_join(thread);
        } else {
            thread_detach(thread);
        }
    }
    /* Detached threads are freed by the idle thread, which has not run
     * while we were busy; give it time. */
    thread_sleep_ms(50);
    EXPECT(atomic_load(&churn_ran) == CHURN_ROUNDS);
    return true;
}

bool test_thread_churn(void)
{
    /*
     * The first pass is a warm-up. Up to CHURN_ROUNDS / 2 ended threads
     * wait for the idle thread at once, so their stacks spread over more
     * address space than usual and new page tables get created. Page
     * tables are reused but never freed (docs/02-memory.md), so they would
     * show up as "lost" frames. The second pass reuses them and must
     * therefore balance exactly.
     */
    EXPECT(churn_once());

    size_t frames_before = pmm_stats().free_frames;
    size_t heap_before = heap_stats().used_bytes;
    uint64_t threads_before = sched_stats().live_threads;

    EXPECT(churn_once());

    EXPECT(sched_stats().live_threads == threads_before);
    EXPECT(heap_stats().used_bytes == heap_before);
    EXPECT(pmm_stats().free_frames == frames_before);
    return true;
}
