# 06 - IPC: channels, ports, shared memory, poll

Processes are isolated; this phase gives them controlled ways to cooperate.
The design is driven by what the window server will need: many clients
talking to one server, large pixel buffers shared without copying, and a
server that waits on everything at once and is never blocked by one client.

All IPC objects are open files. They live in the descriptor table, close
with `close`, can be waited on with `poll`, and can themselves be sent
through a channel.

## Channels

A channel is a two-way connection with two ends. What one end sends, the
other receives as **whole messages in order**: never merged, never split,
at most `SD_MESSAGE_MAX` (4096) bytes each.

- `send` never blocks. If the receiver already has 64 messages waiting it
  fails with `EAGAIN`. A server must not hang because one client stopped
  reading; the sender decides what to do.
- `recv` blocks until a message arrives. It returns 0 once the other end is
  closed *and* the queue is empty: messages sent before a close are still
  delivered.
- A receive buffer that is too small fails with `EMSGSIZE` and leaves the
  message queued.
- Plain `read` and `write` work too.

### Passing descriptors

A message can carry one open file. The message holds its own reference
while queued; on receipt it becomes a descriptor in the receiver's table.
The sender keeps its descriptor. If the message is never received (the
channel is closed first) the reference is dropped with it.

This is how shared memory gets from one process to another, and how a
server can hand a client a private channel.

## Named ports

A client needs a way to find a server. A server calls `port_create("name")`
and gets a listening descriptor; a client calls `port_connect("name")` and
gets one end of a fresh channel; the server takes the other end with
`port_accept`. The client may send immediately: messages wait in the channel
until the server accepts.

Closing the port removes the name and closes connections that were never
accepted.

## Shared memory

`shm_create(size)` makes a zero-filled object backed by frames of its own;
`shm_map(fd)` maps those same frames into the caller's address space. Two
processes that map the object see each other's writes at once, with no
system call per access.

Lifetime is by reference count: one reference per open file and one per
mapping. The frames are freed when the last one goes, in any order. A client
can therefore close the descriptor right after mapping.

For this `kernel/mm/aspace` gained regions of *borrowed* frames:
`aspace_map_frames` maps frames it does not own and, on unmap or address
space destruction, calls a release function instead of freeing them.

## poll

`poll(fds, count, timeout)` blocks until at least one descriptor is ready or
the timeout expires. Each file type reports its own readiness
(`file_ops.poll`):

| File | `SD_POLL_IN` when | `SD_POLL_HUP` when |
|---|---|---|
| channel end | a message is waiting | the other end is closed |
| port | a connection is pending | - |
| console | a key event is queued | - |
| regular file | always | - |

A hang-up is reported even if not asked for, so nobody waits forever on a
dead peer.

### The lost wake-up, and how it is prevented

A poller looks at its files, finds nothing, and goes to sleep. If a file
became ready between the look and the sleep, a naive notification would find
nobody asleep and be lost.

`kernel/fs/poll.c` holds `poll_lock` from the look until the thread is
safely on the wait queue (`wait_queue_wait` releases the lock only then).
`poll_notify()` takes the same lock before waking, so it cannot run in the
gap.

There is one queue for all pollers: any change wakes all of them and each
re-checks its own files. Spurious wake-ups are the price of simplicity.

Timeouts use `wait_queue_wait_timeout` (see
[04-scheduling.md](04-scheduling.md)).

## System calls added

`fstat, poll, channel_create, port_create, port_accept, port_connect, send,
recv, shm_create, shm_map`. libc wrappers are in `<ipc.h>`.

## Checks

| Check | What it proves |
|---|---|
| `user-ipc` | a server and a client process exchange 50 request/reply pairs through a named port; the client creates shared memory and sends it; each side sees what the other wrote |
| `user-ipc-edge` | size limits, queue limit, too-small buffers, empty messages, end of stream, 14 error cases, descriptor passing (including a channel end, and an attachment nobody receives), poll timing, readiness and hang-up |

Both run twice and require the second run to leave free frames, heap usage
and the process table unchanged. The leak check was verified: with the
release of queued attachments removed, it reports the two lost frames.

## Known limits

- One attachment per message.
- No credentials: any process can connect to any port.
- `poll` reports input readiness only; sends never block, so there is no
  "writable" event.
- Shared memory is always mapped read/write.
