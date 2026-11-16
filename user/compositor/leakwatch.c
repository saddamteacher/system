/*
 * Watches the server's own memory for leaks while it runs unattended.
 *
 * The server lives as long as the session, so memory it loses for every
 * window or client adds up. The rule checked here: whenever the server is
 * in the same situation again (the same number of clients and windows, no
 * animation running), it must not be using more memory than before.
 *
 * The first two sightings of a situation are not held against it: caches
 * that are filled once and then kept (a panel's blurred backdrop, a
 * window's shadow) may still be growing then.
 */
#include "compositor.h"

#include <stdio.h>
#include <stdlib.h>

#define MAX_SITUATIONS 64
#define WARM_UP_SIGHTINGS 2

struct situation {
    int clients, windows;
    unsigned sightings;
    size_t settled_bytes; /* in use at the last warm-up sighting */
    size_t latest_bytes;
};

/* Owned by the main loop's thread, like all server state. */
static struct situation situations[MAX_SITUATIONS];
static int situation_count;

static struct situation *situation_for(int clients, int windows)
{
    for (int i = 0; i < situation_count; i++) {
        if (situations[i].clients == clients && situations[i].windows == windows) {
            return &situations[i];
        }
    }
    if (situation_count == MAX_SITUATIONS) {
        return NULL; /* more variety than this check was made for: not watched */
    }
    situations[situation_count] = (struct situation){ .clients = clients, .windows = windows };
    return &situations[situation_count++];
}

void leakwatch_sample(void)
{
    struct situation *situation = situation_for(clients_count(), window_stack_count);
    if (situation == NULL) {
        return;
    }
    situation->latest_bytes = malloc_bytes_in_use();
    if (situation->sightings < WARM_UP_SIGHTINGS) {
        situation->settled_bytes = situation->latest_bytes;
    }
    situation->sightings++;
}

bool leakwatch_report(void)
{
    bool clean = true;
    unsigned compared = 0;
    for (int i = 0; i < situation_count; i++) {
        const struct situation *situation = &situations[i];
        if (situation->sightings <= WARM_UP_SIGHTINGS) {
            continue;
        }
        compared++;
        if (situation->latest_bytes > situation->settled_bytes) {
            printf("compositor: LEAK: with %d clients and %d windows memory grew from %zu to %zu bytes\n",
                   situation->clients, situation->windows, situation->settled_bytes, situation->latest_bytes);
            clean = false;
        }
    }
    if (clean) {
        printf("compositor: no memory growth in %u recurring situations\n", compared);
    }
    return clean && compared > 0;
}
