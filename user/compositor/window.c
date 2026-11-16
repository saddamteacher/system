/*
 * Clients, windows and the stacking order; the server side of the protocol.
 *
 * Everything a client sends is untrusted: sizes, ids and buffers are
 * checked before use, and a client that breaks the protocol is dropped.
 */
#include "compositor.h"

#include <errno.h>
#include <ipc.h>
#include <string.h>
#include <unistd.h>

static struct client clients[MAX_CLIENTS];
static struct window windows[MAX_WINDOWS];
static uint32_t next_window_id = 1;
/* The client that registered as the desktop shell, if any. */
static struct client *shell_client;

struct window *window_stack[MAX_WINDOWS];
int window_stack_count;
struct window *focused_window;

void clients_init(void)
{
    memset(clients, 0, sizeof(clients));
    memset(windows, 0, sizeof(windows));
}

void client_send(struct client *client, const struct gui_message *message)
{
    /* Never wait for a client. If its queue is full it is not reading, and
     * the server must stay responsive for everyone else. */
    msg_send(client->channel, message, sizeof(*message), SD_NO_HANDLE);
}

bool window_is_shown(const struct window *window)
{
    return window->visible && !window->minimized;
}

bool window_is_drawn(const struct window *window)
{
    return window_is_shown(window) && !window->animation_hidden;
}

/* -------------------------------------------------------------- shell */

void shell_notify(const struct window *window)
{
    if (shell_client == NULL || window->kind != GUI_WINDOW_NORMAL) {
        return;
    }
    struct gui_message message = { .type = GUI_MSG_SHELL_WINDOW, .window = window->id };
    if (window->used && window->visible) {
        message.shell_window.state = GUI_SHELL_EXISTS | (window->minimized ? GUI_SHELL_MINIMIZED : 0)
                                   | (window == focused_window ? GUI_SHELL_FOCUSED : 0);
    }
    memcpy(message.shell_window.app, window->client->app, sizeof(message.shell_window.app));
    memcpy(message.shell_window.title, window->title, sizeof(message.shell_window.title));
    client_send(shell_client, &message);
}

/* ---------------------------------------------------------- stacking */

/* Desktop windows stay at the bottom and panels on top, whatever is
 * raised: the stack is kept sorted by this rank. */
static int kind_rank(enum gui_window_kind kind)
{
    switch (kind) {
    case GUI_WINDOW_DESKTOP:
        return 0;
    case GUI_WINDOW_PANEL:
        return 2;
    default:
        return 1;
    }
}

static void stack_remove(struct window *window)
{
    for (int i = 0; i < window_stack_count; i++) {
        if (window_stack[i] == window) {
            memmove(&window_stack[i], &window_stack[i + 1],
                    (size_t)(window_stack_count - i - 1) * sizeof(window_stack[0]));
            window_stack_count--;
            return;
        }
    }
}

/* Inserts at the top of the window's own rank. */
static void stack_insert(struct window *window)
{
    int position = window_stack_count;
    while (position > 0 && kind_rank(window_stack[position - 1]->kind) > kind_rank(window->kind)) {
        position--;
    }
    memmove(&window_stack[position + 1], &window_stack[position],
            (size_t)(window_stack_count - position) * sizeof(window_stack[0]));
    window_stack[position] = window;
    window_stack_count++;
}

void window_raise(struct window *window)
{
    stack_remove(window);
    stack_insert(window);
    damage_add(window_visual_rect(window));
}

static bool can_focus(const struct window *window)
{
    return window->kind == GUI_WINDOW_NORMAL && window_is_shown(window)
        && (window->flags & GUI_CREATE_NO_FOCUS) == 0;
}

static void focus_changed(struct window *window, bool focused)
{
    struct gui_message message = { .type = GUI_MSG_FOCUS, .window = window->id };
    message.focus.focused = focused ? 1 : 0;
    client_send(window->client, &message);
    /* The title bar and shadow look different when focused. */
    damage_add(window_visual_rect(window));
    shell_notify(window);
}

void window_focus(struct window *window)
{
    if (window == focused_window) {
        return;
    }
    struct window *previous = focused_window;
    focused_window = window;
    if (previous != NULL) {
        focus_changed(previous, false);
    }
    if (window != NULL) {
        focus_changed(window, true);
    }
}

/* After the focused window went away: focus the topmost one that can
 * take it. */
static void focus_topmost(void)
{
    for (int i = window_stack_count - 1; i >= 0; i--) {
        if (can_focus(window_stack[i])) {
            window_focus(window_stack[i]);
            return;
        }
    }
    window_focus(NULL);
}

void window_move(struct window *window, int x, int y)
{
    if (x == window->x && y == window->y) {
        return;
    }
    damage_add(window_visual_rect(window));
    window->x = x;
    window->y = y;
    damage_add(window_visual_rect(window));
}

void window_minimize(struct window *window)
{
    if (window->minimized) {
        return;
    }
    animate_minimize(window);
    damage_add(window_visual_rect(window));
    window->minimized = true;
    input_window_gone(window);
    compositor_log("window %u minimized", window->id);
    if (focused_window == window) {
        focused_window = NULL;
        focus_topmost();
    }
    shell_notify(window);
}

static void window_restore(struct window *window)
{
    if (window->minimized) {
        window->minimized = false;
        compositor_log("window %u restored", window->id);
        animate_restore(window);
    }
    window_raise(window);
    window_focus(window);
    shell_notify(window);
}

struct window *window_at(int x, int y, enum hit_area *area)
{
    for (int i = window_stack_count - 1; i >= 0; i--) {
        struct window *window = window_stack[i];
        if (!window_is_drawn(window)) {
            continue;
        }
        enum hit_area hit = window_hit_test(window, x, y);
        if (hit != HIT_NONE) {
            *area = hit;
            return window;
        }
    }
    *area = HIT_NONE;
    return NULL;
}

/* A panel's effective rectangle on screen. */
static struct gfx_rect panel_rect(const struct window *window)
{
    if (window->has_input_region) {
        return gfx_rect_make(window->x + window->input_region.x, window->y + window->input_region.y,
                             window->input_region.width, window->input_region.height);
    }
    return window_content_rect(window);
}

struct gfx_rect work_area(void)
{
    int top = 0, bottom = screen.height;
    for (int i = 0; i < window_stack_count; i++) {
        const struct window *window = window_stack[i];
        if (window->kind != GUI_WINDOW_PANEL || !window_is_shown(window)) {
            continue;
        }
        /* A panel reserves the strip between itself and the nearer of the
         * top and bottom screen edges (it may float a few pixels off it). */
        struct gfx_rect rect = panel_rect(window);
        bool in_upper_half = rect.y + rect.height / 2 < screen.height / 2;
        if (in_upper_half && rect.y + rect.height > top) {
            top = rect.y + rect.height;
        } else if (!in_upper_half && rect.y < bottom) {
            bottom = rect.y;
        }
    }
    return gfx_rect_make(0, top, screen.width, bottom - top);
}

/* ------------------------------------------------------------ resizing */

static void send_resize_request(struct window *window)
{
    struct gui_message message = { .type = GUI_MSG_RESIZE_REQUEST, .window = window->id };
    message.size.width = (uint32_t)window->wanted_width;
    message.size.height = (uint32_t)window->wanted_height;
    client_send(window->client, &message);
    window->resize_outstanding = true;
}

void window_request_size(struct window *window, int width, int height)
{
    if (width < WINDOW_MIN_WIDTH) {
        width = WINDOW_MIN_WIDTH;
    }
    if (height < WINDOW_MIN_HEIGHT) {
        height = WINDOW_MIN_HEIGHT;
    }
    if (width > GUI_MAX_WINDOW_SIZE) {
        width = GUI_MAX_WINDOW_SIZE;
    }
    if (height > GUI_MAX_WINDOW_SIZE) {
        height = GUI_MAX_WINDOW_SIZE;
    }
    window->wanted_width = width;
    window->wanted_height = height;
    /*
     * During a drag the wanted size changes with every mouse movement,
     * faster than a client can redraw. Only one request is in flight at a
     * time; when its answer arrives, the latest wanted size is asked for
     * (see window_show_pending_buffer). Intermediate sizes are skipped.
     */
    if (!window->resize_outstanding && (width != window->width || height != window->height)) {
        send_resize_request(window);
    }
}

void window_toggle_zoom(struct window *window)
{
    struct gfx_rect target;
    if (window->zoomed) {
        target = window->unzoomed_rect;
    } else {
        window->unzoomed_rect = window_content_rect(window);
        struct gfx_rect area = work_area();
        target = gfx_rect_make(area.x, area.y + TITLE_BAR_HEIGHT, area.width, area.height - TITLE_BAR_HEIGHT);
    }
    window->zoomed = !window->zoomed;
    /* Position and size must change in the same frame, so the move waits
     * for the client's new buffer. */
    window->has_pending_position = true;
    window->pending_x = target.x;
    window->pending_y = target.y;
    window_request_size(window, target.width, target.height);
    if (!window->resize_outstanding) {
        /* Already that size: nothing will arrive to trigger the move. */
        window->has_pending_position = false;
        window_move(window, target.x, target.y);
    }
}

static void buffer_release(struct buffer *buffer)
{
    shm_unmap(buffer->mapping, buffer->mapping_size);
    buffer->mapping = NULL;
}

/* The client has drawn into the buffer it attached: put it on screen. */
static void window_show_pending_buffer(struct window *window)
{
    damage_add(window_visual_rect(window));
    buffer_release(&window->buffer);
    window->buffer = window->pending_buffer;
    window->has_pending_buffer = false;
    window->width = window->buffer.surface.width;
    window->height = window->buffer.surface.height;
    if (window->has_pending_position) {
        window->x = window->pending_x;
        window->y = window->pending_y;
        window->has_pending_position = false;
    }
    damage_add(window_visual_rect(window));
    compositor_log("window %u resized to %dx%d at %d %d", window->id, window->width, window->height, window->x,
                   window->y);

    window->resize_outstanding = false;
    if (window->wanted_width != window->width || window->wanted_height != window->height) {
        send_resize_request(window);
    }
}

/* ---------------------------------------------------------- lifecycle */

static struct window *find_window(struct client *client, uint32_t id)
{
    for (int i = 0; i < MAX_WINDOWS; i++) {
        /* A client may only name its own windows. */
        if (windows[i].used && windows[i].id == id && windows[i].client == client) {
            return &windows[i];
        }
    }
    return NULL;
}

static struct window *find_any_window(uint32_t id)
{
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (windows[i].used && windows[i].id == id) {
            return &windows[i];
        }
    }
    return NULL;
}

static void window_destroy(struct window *window)
{
    animations_window_gone(window);
    if (window_is_shown(window)) {
        if (window->kind == GUI_WINDOW_NORMAL) {
            animate_close(window); /* takes its picture while it still exists */
        }
        damage_add(window_visual_rect(window));
    }
    input_window_gone(window);
    stack_remove(window);
    buffer_release(&window->buffer);
    if (window->has_pending_buffer) {
        buffer_release(&window->pending_buffer);
    }
    window->used = false;
    shell_notify(window); /* not `used` any more: reported as gone */

    if (focused_window == window) {
        focused_window = NULL;
        focus_topmost();
    }
}

/*
 * Maps a buffer a client sent. The client claims it holds width x height
 * pixels; the object's real size is checked, because the server will read
 * that many pixels from it.
 */
static bool map_client_buffer(int handle, uint32_t width, uint32_t height, struct buffer *buffer)
{
    struct sd_stat info;
    if (handle == SD_NO_HANDLE || fstat(handle, &info) != 0 || info.type != SD_TYPE_SHM) {
        return false;
    }
    if (info.size < (uint64_t)width * height * sizeof(uint32_t)) {
        return false;
    }
    buffer->mapping = shm_map(handle, &buffer->mapping_size);
    if (buffer->mapping == NULL) {
        return false;
    }
    gfx_surface_init(&buffer->surface, buffer->mapping, (int)width, (int)height, (int)width);
    return true;
}

static bool size_valid(uint32_t width, uint32_t height)
{
    return width >= 1 && height >= 1 && width <= GUI_MAX_WINDOW_SIZE && height <= GUI_MAX_WINDOW_SIZE;
}

static void reply_error(struct client *client, int code)
{
    struct gui_message reply = { .type = GUI_MSG_ERROR };
    reply.error.code = code;
    client_send(client, &reply);
}

static void place_window(struct window *window, const struct gui_message *request)
{
    struct gfx_rect area = work_area();
    window->x = request->create.x;
    window->y = request->create.y;
    if (request->create.flags & GUI_CREATE_CENTERED) {
        window->x = area.x + (area.width - window->width) / 2;
        window->y = area.y + (area.height - window->height + TITLE_BAR_HEIGHT) / 2;
    }
    if (window->kind == GUI_WINDOW_NORMAL) {
        /* Keep the title bar reachable: without it the window could never
         * be moved back. */
        if (window->y < area.y + TITLE_BAR_HEIGHT) {
            window->y = area.y + TITLE_BAR_HEIGHT;
        }
        if (window->y > screen.height - 8) {
            window->y = screen.height - 8;
        }
        if (window->x > screen.width - 40) {
            window->x = screen.width - 40;
        }
        if (window->x < 40 - window->width) {
            window->x = 40 - window->width;
        }
    }
}

static void handle_create(struct client *client, const struct gui_message *request, int handle)
{
    struct window *window = NULL;
    for (int i = 0; i < MAX_WINDOWS && window == NULL; i++) {
        if (!windows[i].used) {
            window = &windows[i];
        }
    }
    uint32_t width = request->create.width, height = request->create.height;
    struct buffer buffer;

    if (window == NULL) {
        reply_error(client, ENOMEM);
        return;
    }
    if (request->create.kind > GUI_WINDOW_PANEL || !size_valid(width, height)
        || !map_client_buffer(handle, width, height, &buffer)) {
        reply_error(client, EINVAL);
        return;
    }

    memset(window, 0, sizeof(*window));
    window->used = true;
    window->id = next_window_id++;
    window->client = client;
    window->kind = (enum gui_window_kind)request->create.kind;
    window->flags = request->create.flags;
    window->buffer = buffer;
    window->width = (int)width;
    window->height = (int)height;
    window->wanted_width = window->width;
    window->wanted_height = window->height;
    /* The title comes from the client: make sure it is terminated. */
    memcpy(window->title, request->create.title, GUI_TITLE_MAX);
    window->title[GUI_TITLE_MAX] = '\0';
    place_window(window, request);
    stack_insert(window);

    struct gui_message reply = { .type = GUI_MSG_WINDOW_CREATED, .window = window->id };
    client_send(client, &reply);
    compositor_log("window %u '%s' at %d %d size %dx%d", window->id, window->title, window->x, window->y,
                   window->width, window->height);
}

static void handle_commit(struct window *window, const struct gui_message *request)
{
    if (request->commit.flags & GUI_COMMIT_WANT_FRAME) {
        window->frame_requested = true;
    }
    if (window->has_pending_buffer) {
        /* The first commit after an attach: the whole new buffer is new. */
        window_show_pending_buffer(window);
        return;
    }
    struct gfx_rect area = gfx_rect_make(request->commit.x, request->commit.y, (int)request->commit.width,
                                         (int)request->commit.height);
    area = gfx_rect_intersect(area, gfx_surface_bounds(&window->buffer.surface));

    if (!window->visible) {
        /* The first commit is what makes a window appear: before it the
         * buffer holds nothing worth showing. */
        window->visible = true;
        damage_add(window_visual_rect(window));
        if (can_focus(window)) {
            window_focus(window);
        }
        if (window->kind == GUI_WINDOW_NORMAL) {
            animate_open(window); /* after focusing: the picture shows the focused look */
        }
        shell_notify(window);
    } else if (!window->minimized) {
        damage_add(gfx_rect_make(window->x + area.x, window->y + area.y, area.width, area.height));
    }
}

static void handle_attach_buffer(struct client *client, struct window *window, const struct gui_message *request,
                                 int handle)
{
    struct buffer buffer;
    if (!size_valid(request->size.width, request->size.height)
        || !map_client_buffer(handle, request->size.width, request->size.height, &buffer)) {
        reply_error(client, EINVAL);
        return;
    }
    if (window->has_pending_buffer) {
        buffer_release(&window->pending_buffer); /* replaced before it was ever shown */
    }
    window->pending_buffer = buffer;
    window->has_pending_buffer = true;
}

static void send_all_windows_to_shell(void)
{
    for (int i = 0; i < window_stack_count; i++) {
        if (window_stack[i]->visible) {
            shell_notify(window_stack[i]);
        }
    }
}

static struct gfx_rect region_from(const struct gui_message *message)
{
    return gfx_rect_make(message->region.x, message->region.y, (int)message->region.width,
                         (int)message->region.height);
}

/* Returns false if the client broke the protocol. */
static bool handle_message(struct client *client, const struct gui_message *message, int handle)
{
    switch (message->type) {
    case GUI_MSG_HELLO:
        memcpy(client->app, message->hello.app, GUI_APP_NAME_MAX);
        client->app[GUI_APP_NAME_MAX] = '\0';
        return true;
    case GUI_MSG_CREATE_WINDOW:
        handle_create(client, message, handle);
        return true;
    case GUI_MSG_SHELL_REGISTER:
        shell_client = client;
        send_all_windows_to_shell();
        return true;
    case GUI_MSG_SHELL_ACTIVATE:
    case GUI_MSG_SHELL_ICON_RECT: {
        /* Only the shell may act on windows that are not its own. */
        struct window *target = client == shell_client ? find_any_window(message->window) : NULL;
        if (target == NULL || target->kind != GUI_WINDOW_NORMAL) {
            return true;
        }
        if (message->type == GUI_MSG_SHELL_ACTIVATE) {
            window_restore(target);
        } else {
            target->has_icon_rect = true;
            target->icon_rect = region_from(message);
        }
        return true;
    }
    default:
        break;
    }

    struct window *window = find_window(client, message->window);
    if (window == NULL) {
        /* Not an offence: the window may have been destroyed while this
         * message was on its way. */
        return true;
    }
    switch (message->type) {
    case GUI_MSG_DESTROY_WINDOW:
        window_destroy(window);
        return true;
    case GUI_MSG_COMMIT:
        handle_commit(window, message);
        return true;
    case GUI_MSG_SET_TITLE:
        memcpy(window->title, message->title.title, GUI_TITLE_MAX);
        window->title[GUI_TITLE_MAX] = '\0';
        damage_add(window_frame_rect(window));
        shell_notify(window);
        return true;
    case GUI_MSG_ATTACH_BUFFER:
        handle_attach_buffer(client, window, message, handle);
        return true;
    case GUI_MSG_SET_INPUT_REGION:
        window->has_input_region = true;
        window->input_region = gfx_rect_intersect(region_from(message), gfx_rect_make(0, 0, window->width, window->height));
        damage_add(window_visual_rect(window));
        return true;
    default:
        return false;
    }
}

static void client_drop(struct client *client)
{
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (windows[i].used && windows[i].client == client) {
            window_destroy(&windows[i]);
        }
    }
    if (shell_client == client) {
        shell_client = NULL;
    }
    close(client->channel);
    client->used = false;
}

void client_service(struct client *client)
{
    /* Bounded, so one client flooding the server cannot starve the rest. */
    for (int handled = 0; handled < SD_CHANNEL_QUEUE_MAX; handled++) {
        struct sd_pollfd state = { .fd = client->channel, .events = SD_POLL_IN };
        if (poll_fds(&state, 1, 0) <= 0) {
            return;
        }
        struct gui_message message;
        int handle = SD_NO_HANDLE;
        long size = msg_recv(client->channel, &message, sizeof(message), &handle);

        bool ok = size == (long)sizeof(message) && handle_message(client, &message, handle);
        if (handle != SD_NO_HANDLE) {
            close(handle); /* mapped if it was wanted; the descriptor is not needed */
        }
        if (!ok) {
            /* Disconnected (size 0), or sent something malformed. */
            client_drop(client);
            return;
        }
    }
}

void clients_accept(int port)
{
    int channel = port_accept(port);
    if (channel < 0) {
        return;
    }
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!clients[i].used) {
            clients[i] = (struct client){ .used = true, .channel = channel };
            struct gui_message welcome = { .type = GUI_MSG_WELCOME };
            welcome.size.width = (uint32_t)screen.width;
            welcome.size.height = (uint32_t)screen.height;
            client_send(&clients[i], &welcome);
            return;
        }
    }
    close(channel); /* full: the client sees the connection close */
}

int clients_poll_descriptors(struct sd_pollfd *descriptors, struct client **owners, int capacity)
{
    int count = 0;
    for (int i = 0; i < MAX_CLIENTS && count < capacity; i++) {
        if (clients[i].used) {
            descriptors[count] = (struct sd_pollfd){ .fd = clients[i].channel, .events = SD_POLL_IN };
            owners[count] = &clients[i];
            count++;
        }
    }
    return count;
}

bool windows_any_frame_requested(void)
{
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (windows[i].used && windows[i].frame_requested) {
            return true;
        }
    }
    return false;
}

void windows_send_frame_events(uint64_t time_ms)
{
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (windows[i].used && windows[i].frame_requested) {
            windows[i].frame_requested = false;
            struct gui_message message = { .type = GUI_MSG_FRAME, .window = windows[i].id };
            message.frame.time_ms = time_ms;
            client_send(windows[i].client, &message);
        }
    }
}
