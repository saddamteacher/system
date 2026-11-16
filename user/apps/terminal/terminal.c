/*
 * Terminal: a window that shows the text output of a program and sends it
 * what the user types. It starts the shell (/bin/sh) connected to itself
 * through a channel.
 *
 * The terminal does the line editing: characters are collected and echoed
 * here, and a whole line is sent to the program when Enter is pressed.
 */
#include <gui/gui.h>
#include <gui/ui.h>
#include <ipc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define INITIAL_COLUMNS 82
#define INITIAL_ROWS 25
#define MAX_COLUMNS 240
#define SCROLLBACK_LINES 600
#define PADDING 6
#define TAB_WIDTH 8
#define LINE_MAX 480
#define HISTORY_SIZE 16

#define COLOR_BACKGROUND GFX_RGB(28, 29, 36)
#define COLOR_TEXT       GFX_RGB(222, 224, 230)
#define COLOR_CURSOR     GFX_RGB(120, 190, 255)

/* One line of the screen. Characters are stored as Unicode code points. */
struct line {
    uint16_t cells[MAX_COLUMNS];
    int length;
};

static struct gui_connection *connection;
static struct gui_window *window;
static struct gfx_font *font;
static int cell_width, cell_height;
static int columns, rows;

/* All lines ever printed, newest last, in a ring: when it is full the
 * oldest line is overwritten. */
static struct line *lines;
static int first_line;  /* ring index of the oldest line */
static int line_count;
static int cursor_column;
/* How many lines the view is scrolled back from the bottom. */
static int scroll_offset;
static bool focused = true;
static bool dirty = true;

static int shell_channel = -1;

/* The line being typed, not yet sent. */
static char input[LINE_MAX];
static int input_length;
static char history[HISTORY_SIZE][LINE_MAX];
static int history_count;
static int history_position; /* history_count = editing a new line */

/* A UTF-8 sequence split across two messages is completed here. */
static uint32_t utf8_pending;
static int utf8_remaining;

/* ---------------------------------------------------------------- screen */

static struct line *line_at(int index)
{
    return &lines[(first_line + index) % SCROLLBACK_LINES];
}

static struct line *current_line(void)
{
    return line_at(line_count - 1);
}

static void new_line(void)
{
    if (line_count == SCROLLBACK_LINES) {
        first_line = (first_line + 1) % SCROLLBACK_LINES; /* forget the oldest */
    } else {
        line_count++;
    }
    current_line()->length = 0;
    cursor_column = 0;
}

static void clear_screen(void)
{
    first_line = 0;
    line_count = 0;
    scroll_offset = 0;
    new_line();
}

static void put_codepoint(uint32_t codepoint)
{
    struct line *line = current_line();
    switch (codepoint) {
    case '\n':
        new_line();
        return;
    case '\r':
        cursor_column = 0;
        return;
    case '\b':
        if (cursor_column > 0) {
            cursor_column--;
        }
        return;
    case '\f':
        clear_screen();
        return;
    case '\t': {
        int next_stop = (cursor_column / TAB_WIDTH + 1) * TAB_WIDTH;
        while (cursor_column < next_stop) {
            put_codepoint(' ');
        }
        return;
    }
    default:
        break;
    }
    if (codepoint < ' ') {
        return; /* other control characters are not interpreted */
    }
    if (cursor_column >= columns || cursor_column >= MAX_COLUMNS) {
        new_line(); /* wrap */
        line = current_line();
    }
    /* Writing past the current end (after a carriage return or backspace
     * moved the cursor) fills the gap with spaces. */
    while (line->length < cursor_column) {
        line->cells[line->length++] = ' ';
    }
    line->cells[cursor_column++] = (uint16_t)(codepoint <= 0xFFFF ? codepoint : '?');
    if (line->length < cursor_column) {
        line->length = cursor_column;
    }
}

/* Feeds program output to the screen, decoding UTF-8 as it goes. */
static void put_bytes(const char *bytes, long count)
{
    for (long i = 0; i < count; i++) {
        unsigned char byte = (unsigned char)bytes[i];
        if (utf8_remaining > 0 && (byte & 0xC0) == 0x80) {
            utf8_pending = (utf8_pending << 6) | (byte & 0x3F);
            if (--utf8_remaining == 0) {
                put_codepoint(utf8_pending);
            }
        } else if (byte < 0x80) {
            utf8_remaining = 0;
            put_codepoint(byte);
        } else if ((byte & 0xE0) == 0xC0) {
            utf8_pending = byte & 0x1F;
            utf8_remaining = 1;
        } else if ((byte & 0xF0) == 0xE0) {
            utf8_pending = byte & 0x0F;
            utf8_remaining = 2;
        } else if ((byte & 0xF8) == 0xF0) {
            utf8_pending = byte & 0x07;
            utf8_remaining = 3;
        } else {
            utf8_remaining = 0;
            put_codepoint('?');
        }
    }
    scroll_offset = 0; /* new output brings the view back to the bottom */
    dirty = true;
}

static void put_text(const char *text)
{
    put_bytes(text, (long)strlen(text));
}

/* --------------------------------------------------------------- drawing */

static void encode_utf8(uint16_t codepoint, char *out)
{
    if (codepoint < 0x80) {
        out[0] = (char)codepoint;
        out[1] = '\0';
    } else if (codepoint < 0x800) {
        out[0] = (char)(0xC0 | (codepoint >> 6));
        out[1] = (char)(0x80 | (codepoint & 0x3F));
        out[2] = '\0';
    } else {
        out[0] = (char)(0xE0 | (codepoint >> 12));
        out[1] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        out[2] = (char)(0x80 | (codepoint & 0x3F));
        out[3] = '\0';
    }
}

static void draw(void)
{
    struct gfx_surface *surface = &window->surface;
    gfx_clear(surface, gfx_surface_bounds(surface), COLOR_BACKGROUND);

    /* The last `rows` lines, moved up by the scroll offset. */
    int bottom = line_count - scroll_offset;
    int top = bottom - rows < 0 ? 0 : bottom - rows;
    for (int index = top; index < bottom; index++) {
        const struct line *line = line_at(index);
        int y = PADDING + (index - top) * cell_height;
        int baseline = y + gfx_font_ascent(font);
        for (int column = 0; column < line->length && column < columns; column++) {
            if (line->cells[column] != ' ') {
                char glyph[4];
                encode_utf8(line->cells[column], glyph);
                /* Every character is placed on the grid, whatever its own
                 * width: that is what keeps columns aligned. */
                gfx_draw_text(surface, font, PADDING + column * cell_width, baseline, glyph, -1, COLOR_TEXT);
            }
        }
    }
    if (scroll_offset == 0) {
        struct gfx_rect cursor = gfx_rect_make(PADDING + cursor_column * cell_width,
                                               PADDING + (line_count - 1 - top) * cell_height, cell_width, cell_height);
        if (focused) {
            gfx_fill_rect(surface, cursor, GFX_WITH_ALPHA(COLOR_CURSOR, 200));
        } else {
            gfx_stroke_rect(surface, cursor, COLOR_CURSOR);
        }
    }
    gui_window_commit(window, gfx_surface_bounds(surface), true);
    dirty = false;
}

static void update_grid_size(void)
{
    columns = (window->width - 2 * PADDING) / cell_width;
    rows = (window->height - 2 * PADDING) / cell_height;
    columns = columns < 10 ? 10 : (columns > MAX_COLUMNS ? MAX_COLUMNS : columns);
    rows = rows < 2 ? 2 : rows;
}

/* ----------------------------------------------------------------- input */

/* Replaces the line being typed, on screen and in the buffer. */
static void set_input(const char *text)
{
    while (input_length > 0) {
        put_text("\b \b");
        input_length--;
    }
    strlcpy(input, text, sizeof(input));
    input_length = (int)strlen(input);
    put_text(input);
}

static void remember(const char *text)
{
    if (text[0] == '\0') {
        return;
    }
    if (history_count == HISTORY_SIZE) {
        memmove(history[0], history[1], sizeof(history[0]) * (HISTORY_SIZE - 1));
        history_count--;
    }
    strlcpy(history[history_count++], text, sizeof(history[0]));
}

static void submit_line(void)
{
    input[input_length] = '\0';
    remember(input);
    history_position = history_count;
    put_text("\n");

    input[input_length++] = '\n';
    msg_send(shell_channel, input, (size_t)input_length, SD_NO_HANDLE);
    input_length = 0;
}

static void handle_key(const struct sd_key_event *key)
{
    if (!key->pressed) {
        return;
    }
    bool control = (key->modifiers & KEY_MODIFIER_CONTROL) != 0;

    if (key->code == KEY_ENTER) {
        submit_line();
    } else if (key->code == KEY_BACKSPACE) {
        if (input_length > 0) {
            input_length--;
            put_text("\b \b"); /* step back, blank the character, step back */
        }
    } else if (key->code == KEY_UP) {
        if (history_position > 0) {
            set_input(history[--history_position]);
        }
    } else if (key->code == KEY_DOWN) {
        if (history_position < history_count) {
            history_position++;
            set_input(history_position < history_count ? history[history_position] : "");
        }
    } else if (control && key->code == KEY_L) {
        clear_screen();
        dirty = true;
    } else if (control && key->code == KEY_D) {
        if (input_length == 0) {
            /* End of input: closing our end makes the shell's read return 0. */
            close(shell_channel);
            shell_channel = -1;
        }
    } else if (key->character >= ' ' && key->character <= '~' && !control && input_length < LINE_MAX - 2) {
        input[input_length++] = key->character;
        char text[2] = { key->character, '\0' };
        put_text(text);
    }
}

static void handle_scroll(int wheel)
{
    int limit = line_count - rows;
    scroll_offset -= wheel * 3;
    if (scroll_offset > limit) {
        scroll_offset = limit;
    }
    if (scroll_offset < 0) {
        scroll_offset = 0;
    }
    dirty = true;
}

/* Returns false when the window should close. */
static bool handle_event(const struct gui_event *event)
{
    switch (event->type) {
    case GUI_MSG_CLOSE_REQUEST:
        return false;
    case GUI_MSG_KEY:
        handle_key(&event->key);
        break;
    case GUI_MSG_POINTER:
        if (event->pointer.kind == GUI_POINTER_SCROLL) {
            handle_scroll(event->pointer.wheel);
        }
        break;
    case GUI_MSG_FOCUS:
        focused = event->focused;
        dirty = true;
        break;
    case GUI_MSG_RESIZE_REQUEST:
        if (gui_window_resize(window, event->resize.width, event->resize.height)) {
            update_grid_size();
            dirty = true;
        }
        break;
    default:
        break; /* GUI_MSG_FRAME: handled by the redraw below */
    }
    return true;
}

/* ------------------------------------------------------------------ main */

static bool start_shell(void)
{
    int ends[2];
    if (channel_create(ends) != 0) {
        return false;
    }
    /* The shell reads its input from, and writes its output to, one end of
     * the channel; we keep the other. */
    char *arguments[] = { "/bin/sh", NULL };
    int pid = spawn_with_io("/bin/sh", arguments, ends[1], ends[1], ends[1]);
    close(ends[1]);
    if (pid < 0) {
        close(ends[0]);
        return false;
    }
    detach(pid);
    shell_channel = ends[0];
    return true;
}

int main(void)
{
    connection = gui_connect("Terminal");
    font = ui_font(UI_FONT_MONO);
    lines = calloc(SCROLLBACK_LINES, sizeof(struct line));
    if (connection == NULL || font == NULL || lines == NULL) {
        printf("terminal: cannot start (no window server, font or memory)\n");
        return 1;
    }
    cell_width = gfx_text_width(font, "M", -1);
    cell_height = gfx_font_line_height(font);

    window = gui_window_create(connection, GUI_WINDOW_NORMAL, 0, 0, INITIAL_COLUMNS * cell_width + 2 * PADDING,
                               INITIAL_ROWS * cell_height + 2 * PADDING, "Terminal", GUI_CREATE_CENTERED);
    if (window == NULL) {
        return 1;
    }
    update_grid_size();
    clear_screen();
    if (!start_shell()) {
        put_text("terminal: cannot start /bin/sh\n");
    }
    printf("terminal: started, %d columns by %d rows\n", columns, rows);

    bool running = true;
    while (running) {
        struct sd_pollfd sources[2] = {
            { .fd = gui_connection_descriptor(connection), .events = SD_POLL_IN },
            { .fd = shell_channel, .events = SD_POLL_IN },
        };
        if (poll_fds(sources, shell_channel >= 0 ? 2 : 1, -1) < 0) {
            break;
        }
        if (shell_channel >= 0 && sources[1].revents != 0) {
            char output[SD_MESSAGE_MAX];
            long size = msg_recv(shell_channel, output, sizeof(output), NULL);
            if (size <= 0) {
                break; /* the shell exited: so do we */
            }
            put_bytes(output, size);
        }
        if (sources[0].revents != 0) {
            struct gui_event event;
            int status;
            /* Take everything that is waiting before redrawing once. */
            while (running && (status = gui_next_event(connection, &event, 0)) != 0) {
                running = status > 0 && handle_event(&event);
            }
        }
        /* While a frame is on its way to the screen the server may be
         * reading the surface, so wait for its confirmation; the pending
         * change is drawn when that arrives. */
        if (running && dirty && !window->frame_pending) {
            draw();
        }
    }
    gui_disconnect(connection);
    return 0;
}
