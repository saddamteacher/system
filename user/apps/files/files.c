/*
 * Files: browses directories and shows the contents of text files.
 *
 * The window has a toolbar (a Back button and the current path) and below
 * it either a list of the directory's entries or, after a file was
 * clicked, that file's text.
 */
#include <errno.h>
#include <gui/gui.h>
#include <gui/ui.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define WIDTH 600
#define HEIGHT 420
#define TOOLBAR_HEIGHT 40
#define ROW_HEIGHT 26
#define MAX_ENTRIES 256
#define MAX_FILE_SIZE (256 * 1024)
#define TEXT_PADDING 12

struct entry {
    char name[SD_NAME_MAX + 1];
    uint32_t type;
    uint64_t size;
};

static struct gui_window *window;
static char path[SD_PATH_MAX + 1] = "/";
static struct entry entries[MAX_ENTRIES];
static int entry_count;
static int hovered = -1;
/* Text of the file being shown, or NULL while listing a directory. */
static char *file_text;
static size_t file_size;
static int scroll; /* rows (list) or lines (text) scrolled off the top */
static bool back_pressed;

static struct gfx_rect back_button(void)
{
    return gfx_rect_make(10, 7, 64, 26);
}

static struct gfx_rect content_area(void)
{
    return gfx_rect_make(0, TOOLBAR_HEIGHT, window->width, window->height - TOOLBAR_HEIGHT);
}

/* ----------------------------------------------------------------- model */

static void join_path(char *result, size_t size, const char *directory, const char *name)
{
    snprintf(result, size, "%s%s%s", directory, strcmp(directory, "/") == 0 ? "" : "/", name);
}

static int compare_entries(const struct entry *a, const struct entry *b)
{
    /* Directories first, then by name. */
    bool a_directory = a->type == SD_TYPE_DIRECTORY, b_directory = b->type == SD_TYPE_DIRECTORY;
    if (a_directory != b_directory) {
        return a_directory ? -1 : 1;
    }
    return strcmp(a->name, b->name);
}

static void sort_entries(void)
{
    /* Insertion sort: a directory holds at most a few hundred entries. */
    for (int i = 1; i < entry_count; i++) {
        struct entry moving = entries[i];
        int position = i;
        while (position > 0 && compare_entries(&moving, &entries[position - 1]) < 0) {
            entries[position] = entries[position - 1];
            position--;
        }
        entries[position] = moving;
    }
}

static void load_directory(void)
{
    struct sd_dirent item;
    entry_count = 0;
    for (size_t index = 0; entry_count < MAX_ENTRIES && readdir_at(path, index, &item) == 0; index++) {
        struct entry *entry = &entries[entry_count++];
        char child[SD_PATH_MAX + SD_NAME_MAX + 2];
        struct sd_stat info = { 0 };
        strlcpy(entry->name, item.name, sizeof(entry->name));
        entry->type = item.type;
        join_path(child, sizeof(child), path, item.name);
        stat(child, &info);
        entry->size = info.size;
    }
    sort_entries();
    scroll = 0;
    hovered = -1;
    gui_window_set_title(window, path);
}

static void close_file(void)
{
    free(file_text);
    file_text = NULL;
    scroll = 0;
}

static void open_file(const char *file_path)
{
    struct sd_stat info;
    if (stat(file_path, &info) != 0 || info.size > MAX_FILE_SIZE) {
        printf("files: cannot show %s\n", file_path);
        return;
    }
    int descriptor = open(file_path);
    char *text = malloc(info.size + 1);
    if (descriptor < 0 || text == NULL) {
        free(text);
        if (descriptor >= 0) {
            close(descriptor);
        }
        return;
    }
    size_t done = 0;
    long count;
    while (done < info.size && (count = read(descriptor, text + done, info.size - done)) > 0) {
        done += (size_t)count;
    }
    close(descriptor);
    text[done] = '\0';

    close_file();
    file_text = text;
    file_size = done;
    gui_window_set_title(window, file_path);
    printf("files: opened %s (%zu bytes)\n", file_path, done);
}

static void go_up(void)
{
    if (file_text != NULL) {
        close_file();
        gui_window_set_title(window, path);
        return;
    }
    char *slash = strrchr(path, '/');
    if (slash == NULL || strcmp(path, "/") == 0) {
        return;
    }
    /* "/etc" becomes "/", "/a/b" becomes "/a". */
    if (slash == path) {
        path[1] = '\0';
    } else {
        *slash = '\0';
    }
    load_directory();
}

static void activate(const struct entry *entry)
{
    char target[SD_PATH_MAX + 1];
    join_path(target, sizeof(target), path, entry->name);
    if (entry->type == SD_TYPE_DIRECTORY) {
        strlcpy(path, target, sizeof(path));
        load_directory();
        printf("files: entered %s (%d entries)\n", path, entry_count);
    } else if (entry->type == SD_TYPE_FILE) {
        open_file(target);
    }
}

/* --------------------------------------------------------------- drawing */

/* A small folder or document picture, 16 pixels square. */
static void draw_kind_icon(struct gfx_surface *surface, int x, int y, uint32_t type)
{
    if (type == SD_TYPE_DIRECTORY) {
        gfx_fill_rounded_rect(surface, gfx_rect_make(x, y + 2, 8, 5), 2, GFX_RGB(86, 160, 250));
        gfx_fill_rounded_rect(surface, gfx_rect_make(x, y + 4, 16, 11), 2, GFX_RGB(110, 180, 255));
    } else {
        gfx_fill_rounded_rect(surface, gfx_rect_make(x + 2, y, 12, 16), 2, GFX_RGB(170, 170, 180));
        gfx_fill_rect(surface, gfx_rect_make(x + 3, y + 1, 10, 14), GFX_WHITE);
        for (int line = 0; line < 3; line++) {
            gfx_fill_rect(surface, gfx_rect_make(x + 5, y + 4 + line * 3, 6, 1), GFX_RGB(180, 180, 190));
        }
    }
}

static void format_size(char *text, size_t size, const struct entry *entry)
{
    if (entry->type == SD_TYPE_DIRECTORY) {
        strlcpy(text, "Folder", size);
    } else if (entry->size < 1024) {
        snprintf(text, size, "%llu bytes", (unsigned long long)entry->size);
    } else {
        snprintf(text, size, "%.1f KB", (double)entry->size / 1024.0);
    }
}

static void draw_list(struct gfx_surface *surface, struct gfx_rect area)
{
    gfx_set_clip(surface, area);
    for (int index = scroll; index < entry_count; index++) {
        struct gfx_rect row = gfx_rect_make(0, area.y + (index - scroll) * ROW_HEIGHT, window->width, ROW_HEIGHT);
        if (row.y >= area.y + area.height) {
            break;
        }
        if (index == hovered) {
            gfx_fill_rect(surface, row, UI_COLOR_SELECTION);
        }
        draw_kind_icon(surface, 16, row.y + 5, entries[index].type);
        ui_text(surface, UI_FONT_BODY, gfx_rect_make(42, row.y, window->width - 190, ROW_HEIGHT), entries[index].name,
                UI_COLOR_TEXT);
        char size_text[32];
        format_size(size_text, sizeof(size_text), &entries[index]);
        ui_text_right(surface, UI_FONT_BODY, gfx_rect_make(window->width - 140, row.y, 124, ROW_HEIGHT), size_text,
                      UI_COLOR_SECONDARY);
    }
    if (entry_count == 0) {
        ui_text_centered(surface, UI_FONT_BODY, gfx_rect_make(0, area.y + 40, window->width, 24),
                         "This folder is empty.", UI_COLOR_SECONDARY);
    }
    gfx_reset_clip(surface);
}

static void draw_text(struct gfx_surface *surface, struct gfx_rect area)
{
    struct gfx_font *font = ui_font(UI_FONT_MONO);
    if (font == NULL) {
        return;
    }
    int line_height = gfx_font_line_height(font);
    int line_number = 0;
    gfx_set_clip(surface, area);

    const char *cursor = file_text;
    const char *end = file_text + file_size;
    while (cursor < end) {
        const char *line_end = memchr(cursor, '\n', (size_t)(end - cursor));
        if (line_end == NULL) {
            line_end = end;
        }
        int y = area.y + TEXT_PADDING + (line_number - scroll) * line_height;
        if (y >= area.y + area.height) {
            break;
        }
        if (line_number >= scroll) {
            gfx_draw_text(surface, font, TEXT_PADDING, y + gfx_font_ascent(font), cursor, (int)(line_end - cursor),
                          UI_COLOR_TEXT);
        }
        line_number++;
        cursor = line_end + 1;
    }
    gfx_reset_clip(surface);
}

static void draw(void)
{
    struct gfx_surface *surface = &window->surface;
    gfx_clear(surface, gfx_surface_bounds(surface), UI_COLOR_PANEL);

    gfx_fill_rect(surface, gfx_rect_make(0, 0, window->width, TOOLBAR_HEIGHT), UI_COLOR_WINDOW);
    gfx_fill_rect(surface, gfx_rect_make(0, TOOLBAR_HEIGHT - 1, window->width, 1), UI_COLOR_SEPARATOR);
    ui_button(surface, back_button(), "Back", back_pressed, false);
    char shown[SD_PATH_MAX + SD_NAME_MAX + 2];
    strlcpy(shown, path, sizeof(shown));
    ui_text(surface, UI_FONT_STRONG, gfx_rect_make(90, 0, window->width - 100, TOOLBAR_HEIGHT), shown, UI_COLOR_TEXT);

    if (file_text != NULL) {
        draw_text(surface, content_area());
    } else {
        draw_list(surface, content_area());
    }
    gui_window_commit(window, gfx_surface_bounds(surface), false);
}

/* ----------------------------------------------------------------- input */

static int row_at(int y)
{
    if (file_text != NULL || y < TOOLBAR_HEIGHT) {
        return -1;
    }
    int index = scroll + (y - TOOLBAR_HEIGHT) / ROW_HEIGHT;
    return index < entry_count ? index : -1;
}

/* Returns true if the window needs redrawing. */
static bool handle_pointer(const struct gui_event *event)
{
    int x = event->pointer.x, y = event->pointer.y;
    bool on_back = gfx_rect_contains(back_button(), x, y);

    switch (event->pointer.kind) {
    case GUI_POINTER_MOVE: {
        int row = row_at(y);
        if (row == hovered) {
            return false;
        }
        hovered = row;
        return true;
    }
    case GUI_POINTER_LEAVE:
        hovered = -1;
        back_pressed = false;
        return true;
    case GUI_POINTER_DOWN:
        back_pressed = on_back;
        return on_back;
    case GUI_POINTER_UP:
        if (back_pressed && on_back) {
            go_up();
        } else if (row_at(y) >= 0) {
            activate(&entries[row_at(y)]);
        }
        back_pressed = false;
        return true;
    case GUI_POINTER_SCROLL:
        scroll += event->pointer.wheel * 2;
        if (scroll < 0) {
            scroll = 0;
        }
        return true;
    default:
        return false;
    }
}

int main(void)
{
    struct gui_connection *connection = gui_connect("Files");
    if (connection == NULL) {
        return 1;
    }
    window = gui_window_create(connection, GUI_WINDOW_NORMAL, 0, 0, WIDTH, HEIGHT, "/", GUI_CREATE_CENTERED);
    if (window == NULL) {
        return 1;
    }
    load_directory();
    draw();
    printf("files: showing %s (%d entries)\n", path, entry_count);

    for (;;) {
        struct gui_event event;
        if (gui_next_event(connection, &event, -1) < 0 || event.type == GUI_MSG_CLOSE_REQUEST) {
            break;
        }
        bool redraw = false;
        if (event.type == GUI_MSG_POINTER) {
            redraw = handle_pointer(&event);
        } else if (event.type == GUI_MSG_RESIZE_REQUEST) {
            redraw = gui_window_resize(window, event.resize.width, event.resize.height);
        }
        if (redraw) {
            draw();
        }
    }
    gui_disconnect(connection);
    return 0;
}
