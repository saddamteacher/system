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
    damage_add(window_frame_rect(window));
}

static bool can_focus(const struct window *window)
{
    return window->kind == GUI_WINDOW_NORMAL && window->visible && (window->flags & GUI_CREATE_NO_FOCUS) == 0;
}

static void send_focus(struct window *window, bool focused)
{
    struct gui_message message = { .type = GUI_MSG_FOCUS, .window = window->id };
    message.focus.focused = focused ? 1 : 0;
    client_send(window->client, &message);
    /* The title bar looks different when focused. */
    damage_add(window_frame_rect(window));
}

void window_focus(struct window *window)
{
    if (window == focused_window) {
        return;
    }
    if (focused_window != NULL) {
        send_focus(focused_window, false);
    }
    focused_window = window;
    if (window != NULL) {
        send_focus(window, true);
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
    damage_add(window_frame_rect(window));
    window->x = x;
    window->y = y;
    damage_add(window_frame_rect(window));
}

struct window *window_at(int x, int y, enum hit_area *area)
{
    for (int i = window_stack_count - 1; i >= 0; i--) {
        struct window *window = window_stack[i];
        if (!window->visible) {
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

static void window_destroy(struct window *window)
{
    if (window->visible) {
        damage_add(window_frame_rect(window));
    }
    input_window_destroyed(window);
    stack_remove(window);
    shm_unmap(window->mapping, window->mapping_size);
    window->used = false;

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
static bool map_client_buffer(int handle, uint32_t width, uint32_t height, void **mapping, size_t *mapping_size)
{
    struct sd_stat info;
    if (handle == SD_NO_HANDLE || fstat(handle, &info) != 0 || info.type != SD_TYPE_SHM) {
        return false;
    }
    if (info.size < (uint64_t)width * height * sizeof(uint32_t)) {
        return false;
    }
    *mapping = shm_map(handle, mapping_size);
    return *mapping != NULL;
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
    window->x = request->create.x;
    window->y = request->create.y;
    if (request->create.flags & GUI_CREATE_CENTERED) {
        window->x = (screen.width - window->width) / 2;
        window->y = (screen.height - window->height) / 2;
    }
    if (window->kind == GUI_WINDOW_NORMAL) {
        /* Keep the title bar reachable: without it the window could never
         * be moved back. */
        if (window->y < TITLE_BAR_HEIGHT) {
            window->y = TITLE_BAR_HEIGHT;
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
    void *mapping;
    size_t mapping_size;

    if (window == NULL) {
        reply_error(client, ENOMEM);
        return;
    }
    if (request->create.kind > GUI_WINDOW_PANEL || !size_valid(width, height)
        || !map_client_buffer(handle, width, height, &mapping, &mapping_size)) {
        reply_error(client, EINVAL);
        return;
    }

    memset(window, 0, sizeof(*window));
    window->used = true;
    window->id = next_window_id++;
    window->client = client;
    window->kind = (enum gui_window_kind)request->create.kind;
    window->flags = request->create.flags;
    window->mapping = mapping;
    window->mapping_size = mapping_size;
    window->width = (int)width;
    window->height = (int)height;
    gfx_surface_init(&window->surface, mapping, window->width, window->height, window->width);
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
    struct gfx_rect area = gfx_rect_make(request->commit.x, request->commit.y, (int)request->commit.width,
                                         (int)request->commit.height);
    area = gfx_rect_intersect(area, gfx_surface_bounds(&window->surface));

    if (!window->visible) {
        /* The first commit is what makes a window appear: before it the
         * buffer holds nothing worth showing. */
        window->visible = true;
        damage_add(window_frame_rect(window));
        if (can_focus(window)) {
            window_focus(window);
        }
    } else {
        damage_add(gfx_rect_make(window->x + area.x, window->y + area.y, area.width, area.height));
    }
    if (request->commit.flags & GUI_COMMIT_WANT_FRAME) {
        window->frame_requested = true;
    }
}

static void handle_attach_buffer(struct client *client, struct window *window, const struct gui_message *request,
                                 int handle)
{
    void *mapping;
    size_t mapping_size;
    if (!size_valid(request->size.width, request->size.height)
        || !map_client_buffer(handle, request->size.width, request->size.height, &mapping, &mapping_size)) {
        reply_error(client, EINVAL);
        return;
    }
    damage_add(window_frame_rect(window));
    shm_unmap(window->mapping, window->mapping_size);
    window->mapping = mapping;
    window->mapping_size = mapping_size;
    window->width = (int)request->size.width;
    window->height = (int)request->size.height;
    gfx_surface_init(&window->surface, mapping, window->width, window->height, window->width);
    damage_add(window_frame_rect(window));
}

/* Returns false if the client broke the protocol. */
static bool handle_message(struct client *client, const struct gui_message *message, int handle)
{
    if (message->type == GUI_MSG_CREATE_WINDOW) {
        handle_create(client, message, handle);
        return true;
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
        return true;
    case GUI_MSG_ATTACH_BUFFER:
        handle_attach_buffer(client, window, message, handle);
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
