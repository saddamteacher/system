#ifndef GUI_SHELL_SETTINGS_H
#define GUI_SHELL_SETTINGS_H

#include <stdint.h>

/*
 * Settings of the desktop shell, and how another program changes them.
 *
 * The shell listens on the named port SHELL_SETTINGS_PORT. When a program
 * connects, the shell sends it the current `struct shell_settings`. The
 * program may send back a changed copy at any time; the shell applies it
 * at once. Settings last until the system is restarted: there is no
 * writable disk to keep them on.
 */
#define SHELL_SETTINGS_PORT "saddev.settings"

enum shell_wallpaper { SHELL_WALLPAPER_DUSK = 0, SHELL_WALLPAPER_OCEAN, SHELL_WALLPAPER_FOREST, SHELL_WALLPAPER_COUNT };

struct shell_settings {
    uint32_t dock_magnification; /* 1 = icons grow under the pointer */
    uint32_t clock_24_hour;      /* 1 = 17:05, 0 = 5:05 PM */
    int32_t timezone_minutes;    /* local time = UTC + this */
    uint32_t wallpaper;          /* enum shell_wallpaper */
};

#define SHELL_TIMEZONE_MIN (-12 * 60)
#define SHELL_TIMEZONE_MAX (14 * 60)

#endif
