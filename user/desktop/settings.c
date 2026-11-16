/*
 * The shell's settings and the port through which other programs (the
 * Settings application) read and change them. See <gui/shell_settings.h>.
 */
#include "desktop.h"

#include <ipc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CONFIG_PATH "/etc/saddev.conf"
#define MAX_SETTINGS_CLIENTS 4

struct shell_settings shell_settings;

static int port = -1;
static int clients[MAX_SETTINGS_CLIENTS];
static int client_count;

/* Reads "timezone_minutes=<n>" from the configuration file, if present. */
static int read_timezone(void)
{
    char text[256];
    int descriptor = open(CONFIG_PATH);
    if (descriptor < 0) {
        return 0;
    }
    long size = read(descriptor, text, sizeof(text) - 1);
    close(descriptor);
    if (size <= 0) {
        return 0;
    }
    text[size] = '\0';
    const char *setting = strstr(text, "timezone_minutes=");
    return setting != NULL ? atoi(setting + strlen("timezone_minutes=")) : 0;
}

bool settings_init(void)
{
    shell_settings = (struct shell_settings){
        .dock_magnification = 1,
        .clock_24_hour = 1,
        .timezone_minutes = read_timezone(),
        .wallpaper = SHELL_WALLPAPER_DUSK,
    };
    port = port_create(SHELL_SETTINGS_PORT);
    return port >= 0;
}

int settings_poll_descriptors(struct sd_pollfd *descriptors, int capacity)
{
    int count = 0;
    if (port >= 0 && count < capacity) {
        descriptors[count++] = (struct sd_pollfd){ .fd = port, .events = SD_POLL_IN };
    }
    for (int i = 0; i < client_count && count < capacity; i++) {
        descriptors[count++] = (struct sd_pollfd){ .fd = clients[i], .events = SD_POLL_IN };
    }
    return count;
}

static void accept_client(void)
{
    int channel = port_accept(port);
    if (channel < 0) {
        return;
    }
    if (client_count == MAX_SETTINGS_CLIENTS) {
        close(channel);
        return;
    }
    /* A new client starts by learning the current values. */
    msg_send(channel, &shell_settings, sizeof(shell_settings), SD_NO_HANDLE);
    clients[client_count++] = channel;
}

/* What a client sends is not trusted: every field is brought into range. */
static struct shell_settings sanitized(struct shell_settings wanted)
{
    wanted.dock_magnification = wanted.dock_magnification != 0;
    wanted.clock_24_hour = wanted.clock_24_hour != 0;
    if (wanted.timezone_minutes < SHELL_TIMEZONE_MIN) {
        wanted.timezone_minutes = SHELL_TIMEZONE_MIN;
    }
    if (wanted.timezone_minutes > SHELL_TIMEZONE_MAX) {
        wanted.timezone_minutes = SHELL_TIMEZONE_MAX;
    }
    if (wanted.wallpaper >= SHELL_WALLPAPER_COUNT) {
        wanted.wallpaper = SHELL_WALLPAPER_DUSK;
    }
    return wanted;
}

/* Returns true if the client changed something; closes it if it left. */
static bool service_client(int index)
{
    struct shell_settings wanted;
    long size = msg_recv(clients[index], &wanted, sizeof(wanted), NULL);
    if (size != (long)sizeof(wanted)) {
        close(clients[index]);
        clients[index] = clients[--client_count];
        return false;
    }
    wanted = sanitized(wanted);
    if (memcmp(&wanted, &shell_settings, sizeof(wanted)) == 0) {
        return false;
    }
    shell_settings = wanted;
    static const char *const wallpapers[SHELL_WALLPAPER_COUNT] = { "dusk", "ocean", "forest" };
    printf("desktop: settings: magnification %s, %s-hour clock, UTC%+d min, wallpaper %s\n",
           wanted.dock_magnification ? "on" : "off", wanted.clock_24_hour ? "24" : "12", wanted.timezone_minutes,
           wallpapers[wanted.wallpaper]);
    return true;
}

bool settings_service(const struct sd_pollfd *descriptors, int count)
{
    bool changed = false;
    /* Clients first, last to first: handling one may remove it, which
     * moves the last client into its slot. */
    for (int i = count - 1; i >= 1; i--) {
        if (descriptors[i].revents != 0 && i - 1 < client_count) {
            changed |= service_client(i - 1);
        }
    }
    if (count > 0 && (descriptors[0].revents & SD_POLL_IN)) {
        accept_client();
    }
    return changed;
}
