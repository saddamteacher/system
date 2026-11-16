#ifndef DESKTOP_H
#define DESKTOP_H

#include <gui/gui.h>

/*
 * The desktop shell: one process that owns the wallpaper, the menu bar and
 * the dock. It is an ordinary client of the window server, with one extra:
 * it registers as the shell and is told about every application window.
 */

/* --------------------------------------------------------- window model */

#define MAX_TRACKED_WINDOWS 64

/* An application window, as reported by the window server. */
struct tracked_window {
    uint32_t id;
    unsigned state; /* GUI_SHELL_* */
    char app[GUI_APP_NAME_MAX + 1];
    char title[GUI_TITLE_MAX + 1];
};

extern struct tracked_window tracked_windows[MAX_TRACKED_WINDOWS];
extern int tracked_window_count;

/* Applies a GUI_MSG_SHELL_WINDOW event to the model. */
void model_update(const struct gui_event *event);
/* The application whose window has the focus, or NULL. */
const char *model_focused_app(void);

/* ------------------------------------------------------------ the parts */

#define MENU_BAR_HEIGHT 26

bool wallpaper_create(struct gui_connection *connection);

bool menubar_create(struct gui_connection *connection);
/* Redraws if the shown application name or the time changed. */
void menubar_update(void);
/* Milliseconds until the clock next needs redrawing. */
int64_t menubar_time_until_update(void);

bool dock_create(struct gui_connection *connection);
/* Redraws the dock; call after the window model changed. */
void dock_update(void);
bool dock_owns(const struct gui_window *window);
void dock_handle_pointer(const struct gui_event *event);

/* ---------------------------------------------------------------- icons */

enum icon_kind { ICON_DEMO_BLUE, ICON_DEMO_ORANGE, ICON_DEMO_GREEN, ICON_KIND_COUNT };

/* Icons are drawn once at this size and scaled down when shown. */
#define ICON_MASTER_SIZE 96

bool icons_init(void);
/* Draws an icon as a `size` pixel square at (x, y). */
void icon_draw(struct gfx_surface *surface, enum icon_kind kind, int x, int y, int size);

/* ---------------------------------------------------------------- fonts */

extern struct gfx_font *font_regular;
extern struct gfx_font *font_semibold;

#endif
