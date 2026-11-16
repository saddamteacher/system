#include <gui/gui.h>

#include <errno.h>
#include <ipc.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define SEND_RETRIES 200
#define PENDING_CAPACITY 16

struct gui_connection {
    int channel;
    int screen_width, screen_height;
    struct gui_window *windows;
    /* Events that arrived while the library was waiting for a reply to a
     * request; gui_next_event() returns them first. */
    struct gui_message pending[PENDING_CAPACITY];
    int pending_count;
};

/* Sends are non-blocking by design of the kernel's channels. The server
 * drains its queue every frame, so a full queue clears within
 * milliseconds; wait briefly rather than lose a request. */
static bool send_message(struct gui_connection *connection, const struct gui_message *message, int attachment)
{
    for (int attempt = 0; attempt < SEND_RETRIES; attempt++) {
        if (msg_send(connection->channel, message, sizeof(*message), attachment) == 0) {
            return true;
        }
        if (errno != EAGAIN) {
            return false;
        }
        sleep_ms(1);
    }
    return false;
}

/* 1 = got one, 0 = timeout, -1 = connection lost. */
static int receive_message(struct gui_connection *connection, struct gui_message *message, int64_t timeout_ms)
{
    struct sd_pollfd descriptor = { .fd = connection->channel, .events = SD_POLL_IN };
    if (poll_fds(&descriptor, 1, timeout_ms) <= 0) {
        return 0;
    }
    long size = msg_recv(connection->channel, message, sizeof(*message), NULL);
    return size == (long)sizeof(*message) ? 1 : -1;
}

/* Waits for the answer to a request. Anything else that arrives meanwhile
 * is an event for the application and is kept for later. */
static bool wait_for_reply(struct gui_connection *connection, struct gui_message *reply)
{
    for (;;) {
        if (receive_message(connection, reply, -1) != 1) {
            return false;
        }
        if (reply->type == GUI_MSG_WINDOW_CREATED || reply->type == GUI_MSG_ERROR) {
            return true;
        }
        if (connection->pending_count < PENDING_CAPACITY) {
            connection->pending[connection->pending_count++] = *reply;
        }
    }
}

struct gui_connection *gui_connect(const char *app_name)
{
    struct gui_connection *connection = calloc(1, sizeof(*connection));
    if (connection == NULL) {
        return NULL;
    }
    connection->channel = port_connect(GUI_SERVER_PORT);
    struct gui_message welcome;
    if (connection->channel < 0 || receive_message(connection, &welcome, 5000) != 1
        || welcome.type != GUI_MSG_WELCOME) {
        if (connection->channel >= 0) {
            close(connection->channel);
        }
        free(connection);
        return NULL;
    }
    connection->screen_width = (int)welcome.size.width;
    connection->screen_height = (int)welcome.size.height;

    struct gui_message hello = { .type = GUI_MSG_HELLO };
    strlcpy(hello.hello.app, app_name != NULL ? app_name : "", sizeof(hello.hello.app));
    send_message(connection, &hello, SD_NO_HANDLE);
    return connection;
}

int gui_screen_width(const struct gui_connection *connection)
{
    return connection->screen_width;
}

int gui_screen_height(const struct gui_connection *connection)
{
    return connection->screen_height;
}

int gui_connection_descriptor(const struct gui_connection *connection)
{
    return connection->channel;
}

static void copy_title(char *destination, const char *title)
{
    strlcpy(destination, title != NULL ? title : "", GUI_TITLE_MAX + 1);
}

/* Creates and maps a shared buffer for width x height pixels. */
static int create_buffer(int width, int height, void **mapping, size_t *mapping_size)
{
    int shared = shm_create((size_t)width * (size_t)height * sizeof(uint32_t));
    if (shared < 0) {
        return -1;
    }
    *mapping = shm_map(shared, mapping_size);
    if (*mapping == NULL) {
        close(shared);
        return -1;
    }
    return shared;
}

struct gui_window *gui_window_create(struct gui_connection *connection, enum gui_window_kind kind, int x, int y,
                                     int width, int height, const char *title, unsigned flags)
{
    if (width <= 0 || height <= 0 || width > GUI_MAX_WINDOW_SIZE || height > GUI_MAX_WINDOW_SIZE) {
        return NULL;
    }
    struct gui_window *window = calloc(1, sizeof(*window));
    if (window == NULL) {
        return NULL;
    }
    int shared = create_buffer(width, height, &window->mapping, &window->mapping_size);
    if (shared < 0) {
        free(window);
        return NULL;
    }

    struct gui_message request = { .type = GUI_MSG_CREATE_WINDOW };
    request.create.kind = kind;
    request.create.flags = flags;
    request.create.x = x;
    request.create.y = y;
    request.create.width = (uint32_t)width;
    request.create.height = (uint32_t)height;
    copy_title(request.create.title, title);

    struct gui_message reply;
    bool created = send_message(connection, &request, shared) && wait_for_reply(connection, &reply)
                && reply.type == GUI_MSG_WINDOW_CREATED;
    /* The server has its own reference now (or never will). */
    close(shared);
    if (!created) {
        shm_unmap(window->mapping, window->mapping_size);
        free(window);
        return NULL;
    }

    window->connection = connection;
    window->id = reply.window;
    window->width = width;
    window->height = height;
    window->x = x;
    window->y = y;
    gfx_surface_init(&window->surface, window->mapping, width, height, width);
    window->next = connection->windows;
    connection->windows = window;
    return window;
}

static void unlink_window(struct gui_window *window)
{
    struct gui_window **link = &window->connection->windows;
    while (*link != NULL && *link != window) {
        link = &(*link)->next;
    }
    if (*link != NULL) {
        *link = window->next;
    }
}

void gui_window_destroy(struct gui_window *window)
{
    struct gui_message request = { .type = GUI_MSG_DESTROY_WINDOW, .window = window->id };
    send_message(window->connection, &request, SD_NO_HANDLE);
    unlink_window(window);
    shm_unmap(window->mapping, window->mapping_size);
    free(window);
}

void gui_window_set_title(struct gui_window *window, const char *title)
{
    struct gui_message request = { .type = GUI_MSG_SET_TITLE, .window = window->id };
    copy_title(request.title.title, title);
    send_message(window->connection, &request, SD_NO_HANDLE);
}

static void send_region(struct gui_connection *connection, uint32_t type, uint32_t window_id,
                        struct gfx_rect rect)
{
    struct gui_message request = { .type = type, .window = window_id };
    request.region.x = rect.x;
    request.region.y = rect.y;
    request.region.width = (uint32_t)(rect.width > 0 ? rect.width : 0);
    request.region.height = (uint32_t)(rect.height > 0 ? rect.height : 0);
    send_message(connection, &request, SD_NO_HANDLE);
}

void gui_window_set_input_region(struct gui_window *window, struct gfx_rect region)
{
    send_region(window->connection, GUI_MSG_SET_INPUT_REGION, window->id, region);
}

void gui_shell_register(struct gui_connection *connection)
{
    struct gui_message request = { .type = GUI_MSG_SHELL_REGISTER };
    send_message(connection, &request, SD_NO_HANDLE);
}

void gui_shell_activate(struct gui_connection *connection, uint32_t window_id)
{
    struct gui_message request = { .type = GUI_MSG_SHELL_ACTIVATE, .window = window_id };
    send_message(connection, &request, SD_NO_HANDLE);
}

void gui_shell_set_icon_rect(struct gui_connection *connection, uint32_t window_id, struct gfx_rect rect)
{
    send_region(connection, GUI_MSG_SHELL_ICON_RECT, window_id, rect);
}

void gui_window_commit(struct gui_window *window, struct gfx_rect area, bool want_frame)
{
    area = gfx_rect_intersect(area, gfx_surface_bounds(&window->surface));
    struct gui_message request = { .type = GUI_MSG_COMMIT, .window = window->id };
    request.commit.x = area.x;
    request.commit.y = area.y;
    request.commit.width = (uint32_t)area.width;
    request.commit.height = (uint32_t)area.height;
    request.commit.flags = want_frame ? GUI_COMMIT_WANT_FRAME : 0;
    if (send_message(window->connection, &request, SD_NO_HANDLE) && want_frame) {
        window->frame_pending = true;
    }
}

bool gui_window_resize(struct gui_window *window, int width, int height)
{
    if (width <= 0 || height <= 0 || width > GUI_MAX_WINDOW_SIZE || height > GUI_MAX_WINDOW_SIZE) {
        return false;
    }
    void *mapping;
    size_t mapping_size;
    int shared = create_buffer(width, height, &mapping, &mapping_size);
    if (shared < 0) {
        return false;
    }
    struct gui_message request = { .type = GUI_MSG_ATTACH_BUFFER, .window = window->id };
    request.size.width = (uint32_t)width;
    request.size.height = (uint32_t)height;
    bool sent = send_message(window->connection, &request, shared);
    close(shared);
    if (!sent) {
        shm_unmap(mapping, mapping_size);
        return false;
    }
    shm_unmap(window->mapping, window->mapping_size);
    window->mapping = mapping;
    window->mapping_size = mapping_size;
    window->width = width;
    window->height = height;
    gfx_surface_init(&window->surface, mapping, width, height, width);
    /* The server stopped using the old buffer, so no read is in progress. */
    window->frame_pending = false;
    return true;
}

static struct gui_window *find_window(struct gui_connection *connection, uint32_t id)
{
    for (struct gui_window *window = connection->windows; window != NULL; window = window->next) {
        if (window->id == id) {
            return window;
        }
    }
    return NULL;
}

static void translate(struct gui_connection *connection, const struct gui_message *message, struct gui_event *event)
{
    memset(event, 0, sizeof(*event));
    event->type = message->type;
    event->window = find_window(connection, message->window);

    switch (message->type) {
    case GUI_MSG_KEY:
        event->key = message->key;
        break;
    case GUI_MSG_POINTER:
        event->pointer.x = message->pointer.x;
        event->pointer.y = message->pointer.y;
        event->pointer.buttons = message->pointer.buttons;
        event->pointer.kind = message->pointer.kind;
        event->pointer.wheel = message->pointer.wheel;
        break;
    case GUI_MSG_FOCUS:
        event->focused = message->focus.focused != 0;
        break;
    case GUI_MSG_RESIZE_REQUEST:
        event->resize.width = (int)message->size.width;
        event->resize.height = (int)message->size.height;
        break;
    case GUI_MSG_SHELL_WINDOW:
        /* Describes another application's window, not one of ours. */
        event->window = NULL;
        event->shell_window.id = message->window;
        event->shell_window.state = message->shell_window.state;
        memcpy(event->shell_window.app, message->shell_window.app, GUI_APP_NAME_MAX);
        memcpy(event->shell_window.title, message->shell_window.title, GUI_TITLE_MAX);
        break;
    case GUI_MSG_FRAME:
        event->frame_time_ms = message->frame.time_ms;
        if (event->window != NULL) {
            event->window->frame_pending = false;
        }
        break;
    default:
        break;
    }
}

int gui_next_event(struct gui_connection *connection, struct gui_event *event, int64_t timeout_ms)
{
    struct gui_message message;
    if (connection->pending_count > 0) {
        message = connection->pending[0];
        connection->pending_count--;
        memmove(&connection->pending[0], &connection->pending[1],
                (size_t)connection->pending_count * sizeof(message));
    } else {
        int status = receive_message(connection, &message, timeout_ms);
        if (status != 1) {
            return status;
        }
    }
    translate(connection, &message, event);
    return 1;
}

void gui_disconnect(struct gui_connection *connection)
{
    while (connection->windows != NULL) {
        struct gui_window *window = connection->windows;
        connection->windows = window->next;
        shm_unmap(window->mapping, window->mapping_size);
        free(window);
    }
    /* Closing the channel tells the server to remove our windows. */
    close(connection->channel);
    free(connection);
}
