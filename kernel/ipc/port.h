#ifndef IPC_PORT_H
#define IPC_PORT_H

/*
 * Named ports: how a client finds a server. A server creates a port under a
 * well-known name; a client connects to that name and gets one end of a new
 * channel, while the server accepts the other end from the port.
 */

struct file;

/* Creates a listening port. Closing the returned file removes the name.
 *   -EEXIST        the name is taken
 *   -ENAMETOOLONG  longer than SD_PORT_NAME_MAX
 *   -EINVAL        empty name */
int port_create(const char *name, struct file **port);

/* Connects to a port; returns the client end of a new channel. The client
 * may send at once: messages wait until the server accepts.
 *   -ENOENT  no such port */
int port_connect(const char *name, struct file **channel_end);

/* Takes the next pending connection, blocking until there is one; returns
 * the server end of its channel. */
int port_accept(struct file *port, struct file **channel_end);

#endif
