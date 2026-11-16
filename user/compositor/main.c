/*
 * The window server's event loop and frame timing.
 *
 * usage: compositor [--test] [--stats]
 *   --test   log window and pointer activity; F12 quits
 *   --stats  log frame statistics once per second
 *   --animation-scale=N  make window animations N times slower
 */
#include "compositor.h"

#include <ipc.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* 60 frames per second. */
#define FRAME_INTERVAL_NS 16666667ull
#define NS_PER_MS 1000000ull

bool option_test_mode;
static bool option_stats;
static bool quit_requested;

void compositor_log(const char *format, ...)
{
    if (!option_test_mode) {
        return;
    }
    char line[160];
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(line, sizeof(line), format, arguments);
    va_end(arguments);
    printf("compositor: %s\n", line);
}

void compositor_quit(void)
{
    quit_requested = true;
}

/* ------------------------------------------------------- statistics */

static struct {
    uint64_t period_start_ns; /* start time of the period's first frame */
    uint64_t last_start_ns;   /* start time of the most recent frame */
    uint64_t last_work_ns;    /* how long that frame took to draw */
    unsigned frames;          /* frames started in the period, first included */
    uint64_t work_total_ns;
    uint64_t work_max_ns;
} stats;

/*
 * Called for every frame with the times its composition started and
 * ended. Once a second it reports the frame rate and how long frames took.
 */
static void stats_record(uint64_t frame_start_ns, uint64_t frame_end_ns)
{
    /*
     * The server only draws when something changed, so a gap between two
     * frames usually means the screen was simply still (or changes came
     * slower than the frame rate, like a slowly moved mouse). Counting
     * such gaps would report a meaninglessly low rate, so a period ends
     * at the first one. But a gap caused by the previous frame taking
     * long to draw is exactly what the statistic exists to show, and
     * does count.
     */
    uint64_t allowed_gap = FRAME_INTERVAL_NS + FRAME_INTERVAL_NS / 2;
    if (stats.last_work_ns + FRAME_INTERVAL_NS / 2 > allowed_gap) {
        allowed_gap = stats.last_work_ns + FRAME_INTERVAL_NS / 2;
    }
    if (stats.frames > 0 && frame_start_ns - stats.last_start_ns > allowed_gap) {
        stats.frames = 0;
        stats.work_total_ns = 0;
        stats.work_max_ns = 0;
    }
    stats.last_start_ns = frame_start_ns;

    if (stats.frames > 0 && frame_start_ns - stats.period_start_ns >= 1000 * NS_PER_MS) {
        if (option_stats) {
            /* N frame starts span N - 1 intervals; this frame's start ends
             * the last one. Rate in tenths of a frame per second. */
            uint64_t elapsed = frame_start_ns - stats.period_start_ns;
            unsigned long long fps_tenths = (unsigned long long)stats.frames * 10000000000ull / elapsed;
            printf("compositor: %llu.%llu fps, frame time avg %llu us, max %llu us\n", fps_tenths / 10,
                   fps_tenths % 10, (unsigned long long)(stats.work_total_ns / stats.frames / 1000),
                   (unsigned long long)(stats.work_max_ns / 1000));
        }
        stats.frames = 0;
        stats.work_total_ns = 0;
        stats.work_max_ns = 0;
    }
    if (stats.frames == 0) {
        stats.period_start_ns = frame_start_ns;
    }
    uint64_t work = frame_end_ns - frame_start_ns;
    stats.last_work_ns = work;
    stats.frames++;
    stats.work_total_ns += work;
    if (work > stats.work_max_ns) {
        stats.work_max_ns = work;
    }
}

/* ---------------------------------------------------------- the loop */

/*
 * How long to sleep before the next frame is due. The kernel rounds
 * timeouts up to whole ticks and adds one, so asking for the full time
 * would overshoot. Ask for less and finish the wait with short yields: the
 * frame then starts on time, and the average rate is exactly the target.
 */
static int64_t timeout_until(uint64_t deadline_ns)
{
    uint64_t now = clock_ns();
    if (now >= deadline_ns) {
        return 0;
    }
    uint64_t remaining_ms = (deadline_ns - now) / NS_PER_MS;
    return remaining_ms >= 2 ? (int64_t)(remaining_ms - 1) : 0;
}

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        option_test_mode |= strcmp(argv[i], "--test") == 0;
        option_stats |= strcmp(argv[i], "--stats") == 0;
        if (strncmp(argv[i], "--animation-scale=", 18) == 0) {
            animations_set_scale((unsigned)atoi(argv[i] + 18));
        }
    }

    if (!render_init() || !input_init()) {
        return 1;
    }
    clients_init();
    decor_init();
    int port = port_create(GUI_SERVER_PORT);
    if (port < 0) {
        printf("compositor: cannot create the server port (is another one running?)\n");
        return 1;
    }
    damage_add(gfx_surface_bounds(&screen));
    printf("compositor: ready, %dx%d\n", screen.width, screen.height);

    uint64_t next_frame_ns = clock_ns();

    while (!quit_requested) {
        struct sd_pollfd descriptors[3 + MAX_CLIENTS];
        struct client *owners[MAX_CLIENTS];
        descriptors[0] = (struct sd_pollfd){ .fd = port, .events = SD_POLL_IN };
        descriptors[1] = (struct sd_pollfd){ .fd = input_mouse_descriptor(), .events = SD_POLL_IN };
        descriptors[2] = (struct sd_pollfd){ .fd = input_keyboard_descriptor(), .events = SD_POLL_IN };
        int client_count = clients_poll_descriptors(&descriptors[3], owners, MAX_CLIENTS);

        /* With nothing to draw there is no frame to wait for: sleep until
         * something happens. */
        bool frame_needed = damage_pending() || windows_any_frame_requested() || animations_active();
        int64_t timeout = frame_needed ? timeout_until(next_frame_ns) : -1;

        int ready = poll_fds(descriptors, (size_t)(3 + client_count), timeout);
        if (ready > 0) {
            if (descriptors[1].revents & SD_POLL_IN) {
                input_handle_mouse();
            }
            if (descriptors[2].revents & SD_POLL_IN) {
                input_handle_keyboard();
            }
            for (int i = 0; i < client_count; i++) {
                if (descriptors[3 + i].revents != 0) {
                    client_service(owners[i]);
                }
            }
            if (descriptors[0].revents & SD_POLL_IN) {
                clients_accept(port);
            }
        }

        frame_needed = damage_pending() || windows_any_frame_requested() || animations_active();
        uint64_t now = clock_ns();
        if (!frame_needed) {
            /* Idle: the next change is shown immediately, not at a frame
             * boundary scheduled long ago. */
            next_frame_ns = now;
            continue;
        }
        if (now < next_frame_ns) {
            if (ready == 0 && timeout == 0) {
                sched_yield(); /* the last stretch before the deadline */
            }
            continue;
        }

        animations_update(now);
        render_frame();
        uint64_t finished = clock_ns();
        windows_send_frame_events(finished / NS_PER_MS);
        stats_record(now, finished);

        next_frame_ns += FRAME_INTERVAL_NS;
        if (next_frame_ns < finished) {
            /* We fell behind (a frame took longer than the interval).
             * Skip the missed frames instead of trying to catch up. */
            next_frame_ns = finished;
        }
    }

    printf("compositor: exiting\n");
    return 0;
}
