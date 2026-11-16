/*
 * State: the list of ports and every port's pending queue are protected by
 * port_lock. Lock order: port_lock, then the scheduler lock. poll_notify()
 * is called after releasing port_lock.
 */
#include <ipc/port.h>

#include <saddev/errno.h>
#include <saddev/syscall.h>

#include <core/list.h>
#include <core/spinlock.h>
#include <core/string.h>
#include <fs/poll.h>
#include <fs/vfs.h>
#include <ipc/channel.h>
#include <mm/heap.h>
#include <sched/wait_queue.h>

struct pending_connection {
    struct list_node link;
    struct file *server_end;
};

struct port {
    struct list_node link;
    char name[SD_PORT_NAME_MAX + 1];
    struct list_node pending; /* connections not yet accepted */
    struct wait_queue acceptors;
};

static struct spinlock port_lock = SPINLOCK_INIT;
static struct list_node ports = { &ports, &ports };

static struct port *find_port_locked(const char *name)
{
    list_for_each (node, &ports) {
        struct port *port = container_of(node, struct port, link);
        if (strcmp(port->name, name) == 0) {
            return port;
        }
    }
    return NULL;
}

static int64_t port_file_read(struct file *file, void *buffer, size_t size)
{
    (void)file;
    (void)buffer;
    (void)size;
    return -EINVAL; /* a port carries connections, not bytes */
}

static unsigned port_file_poll(struct file *file)
{
    struct port *port = file->context;
    arch_irq_state_t irq_state = spinlock_acquire(&port_lock);
    unsigned state = list_is_empty(&port->pending) ? 0 : SD_POLL_IN;
    spinlock_release(&port_lock, irq_state);
    return state;
}

static void port_file_close(struct file *file)
{
    struct port *port = file->context;

    arch_irq_state_t irq_state = spinlock_acquire(&port_lock);
    /* Off the list first: no new connection can find the port. */
    list_remove(&port->link);
    spinlock_release(&port_lock, irq_state);

    /* Clients that connected but were never accepted see their channel
     * close. */
    while (!list_is_empty(&port->pending)) {
        struct pending_connection *connection =
            container_of(port->pending.next, struct pending_connection, link);
        list_remove(&connection->link);
        vfs_close(connection->server_end);
        kfree(connection);
    }
    kfree(port);
}

static const struct file_ops port_ops = {
    .read = port_file_read,
    .write = NULL,
    .close = port_file_close,
    .poll = port_file_poll,
};

int port_create(const char *name, struct file **result)
{
    size_t length = strlen(name);
    if (length == 0) {
        return -EINVAL;
    }
    if (length > SD_PORT_NAME_MAX) {
        return -ENAMETOOLONG;
    }
    struct port *port = kzalloc(sizeof(*port));
    struct file *file = port != NULL ? vfs_file_create(&port_ops, port, SD_TYPE_PORT) : NULL;
    if (file == NULL) {
        kfree(port);
        return -ENOMEM;
    }
    memcpy(port->name, name, length + 1);
    list_init(&port->pending);
    wait_queue_init(&port->acceptors);

    arch_irq_state_t irq_state = spinlock_acquire(&port_lock);
    bool taken = find_port_locked(name) != NULL;
    if (!taken) {
        list_push_back(&ports, &port->link);
    }
    spinlock_release(&port_lock, irq_state);

    if (taken) {
        kfree(file);
        kfree(port);
        return -EEXIST;
    }
    *result = file;
    return 0;
}

int port_connect(const char *name, struct file **channel_end)
{
    struct file *client_end, *server_end;
    struct pending_connection *connection = kmalloc(sizeof(*connection));
    if (connection == NULL) {
        return -ENOMEM;
    }
    int status = channel_create(&client_end, &server_end);
    if (status < 0) {
        kfree(connection);
        return status;
    }
    connection->server_end = server_end;

    arch_irq_state_t irq_state = spinlock_acquire(&port_lock);
    struct port *port = find_port_locked(name);
    if (port != NULL) {
        list_push_back(&port->pending, &connection->link);
        wait_queue_wake_one(&port->acceptors);
    }
    spinlock_release(&port_lock, irq_state);

    if (port == NULL) {
        vfs_close(client_end);
        vfs_close(server_end);
        kfree(connection);
        return -ENOENT;
    }
    poll_notify();
    *channel_end = client_end;
    return 0;
}

int port_accept(struct file *file, struct file **channel_end)
{
    struct port *port = file->context;

    arch_irq_state_t irq_state = spinlock_acquire(&port_lock);
    while (list_is_empty(&port->pending)) {
        wait_queue_wait(&port->acceptors, &port_lock);
    }
    struct pending_connection *connection = container_of(port->pending.next, struct pending_connection, link);
    list_remove(&connection->link);
    spinlock_release(&port_lock, irq_state);

    *channel_end = connection->server_end;
    kfree(connection);
    return 0;
}
