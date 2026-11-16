/* Limits, errors and waiting in the IPC interface, within one process. */
#include <errno.h>
#include <ipc.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            printf("ipc-edge: line %d: failed: %s (errno %d)\n", __LINE__, #condition, errno); \
            return 1; \
        } \
    } while (0)

static int test_limits(void)
{
    static char big[SD_MESSAGE_MAX + 1];
    char buffer[64];
    int ends[2];
    CHECK(channel_create(ends) == 0);

    /* The largest message passes, one byte more does not. */
    memset(big, 'x', sizeof(big));
    CHECK(msg_send(ends[0], big, SD_MESSAGE_MAX, SD_NO_HANDLE) == 0);
    CHECK(msg_send(ends[0], big, SD_MESSAGE_MAX + 1, SD_NO_HANDLE) == -1 && errno == EMSGSIZE);

    /* A buffer that is too small leaves the message in the queue. */
    CHECK(msg_recv(ends[1], buffer, sizeof(buffer), NULL) == -1 && errno == EMSGSIZE);
    static char large[SD_MESSAGE_MAX];
    CHECK(msg_recv(ends[1], large, sizeof(large), NULL) == SD_MESSAGE_MAX);
    CHECK(large[0] == 'x' && large[SD_MESSAGE_MAX - 1] == 'x');

    /* An empty message is a message too. */
    CHECK(msg_send(ends[0], "", 0, SD_NO_HANDLE) == 0);
    CHECK(msg_send(ends[0], "after", 6, SD_NO_HANDLE) == 0);
    CHECK(msg_recv(ends[1], buffer, sizeof(buffer), NULL) == 0);
    CHECK(msg_recv(ends[1], buffer, sizeof(buffer), NULL) == 6 && strcmp(buffer, "after") == 0);

    /* The queue is bounded: the sender is told, not blocked. */
    for (int i = 0; i < SD_CHANNEL_QUEUE_MAX; i++) {
        CHECK(msg_send(ends[0], &i, sizeof(i), SD_NO_HANDLE) == 0);
    }
    CHECK(msg_send(ends[0], "x", 1, SD_NO_HANDLE) == -1 && errno == EAGAIN);
    for (int i = 0; i < SD_CHANNEL_QUEUE_MAX; i++) {
        int value = -1;
        CHECK(msg_recv(ends[1], &value, sizeof(value), NULL) == (long)sizeof(value) && value == i);
    }

    /* Channels also work with plain read and write. */
    CHECK(write(ends[1], "ping", 5) == 5);
    CHECK(read(ends[0], buffer, sizeof(buffer)) == 5 && strcmp(buffer, "ping") == 0);

    /* Messages sent before a close are still delivered, then end of stream. */
    CHECK(msg_send(ends[0], "last", 5, SD_NO_HANDLE) == 0);
    CHECK(close(ends[0]) == 0);
    CHECK(msg_recv(ends[1], buffer, sizeof(buffer), NULL) == 5 && strcmp(buffer, "last") == 0);
    CHECK(msg_recv(ends[1], buffer, sizeof(buffer), NULL) == 0);
    CHECK(msg_send(ends[1], "x", 1, SD_NO_HANDLE) == -1 && errno == EPIPE);
    CHECK(close(ends[1]) == 0);
    return 0;
}

static int test_errors(void)
{
    char buffer[16];
    int ends[2];
    CHECK(channel_create(ends) == 0);

    CHECK(msg_send(99, "x", 1, SD_NO_HANDLE) == -1 && errno == EBADF);
    CHECK(msg_send(STDOUT_FILENO, "x", 1, SD_NO_HANDLE) == -1 && errno == EBADF); /* not a channel */
    CHECK(msg_send(ends[0], "x", 1, 99) == -1 && errno == EBADF);                 /* bad attachment */
    CHECK(msg_send(ends[0], (void *)0xffffffff80000000UL, 8, SD_NO_HANDLE) == -1 && errno == EFAULT);
    CHECK(msg_recv(ends[0], (void *)0xffffffff80000000UL, 8, NULL) == -1 && errno == EFAULT);
    CHECK(msg_recv(ends[0], buffer, sizeof(buffer), (int *)0x10) == -1 && errno == EFAULT);
    CHECK(channel_create((int *)0x10) == -1 && errno == EFAULT);

    CHECK(port_connect("no.such.port") == -1 && errno == ENOENT);
    CHECK(port_create("") == -1 && errno == EINVAL);
    CHECK(port_create("a-name-that-is-longer-than-the-limit-allows") == -1 && errno == ENAMETOOLONG);
    CHECK(port_accept(ends[0]) == -1 && errno == EBADF); /* a channel is not a port */

    CHECK(shm_create(0) == -1 && errno == EINVAL);
    CHECK(shm_create((size_t)SD_SHM_MAX_SIZE + 1) == -1 && errno == EINVAL);
    CHECK(shm_map(ends[0], NULL) == NULL && errno == EBADF);

    CHECK(close(ends[0]) == 0 && close(ends[1]) == 0);
    return 0;
}

/* A descriptor sent over a channel is a new reference to the same object. */
static int test_handle_passing(void)
{
    int outer[2], inner[2];
    char buffer[16];
    CHECK(channel_create(outer) == 0 && channel_create(inner) == 0);

    /* Send one end of `inner` through `outer`, then close our copy. */
    CHECK(msg_send(outer[0], "take", 5, inner[1]) == 0);
    CHECK(close(inner[1]) == 0);

    int received = SD_NO_HANDLE;
    CHECK(msg_recv(outer[1], buffer, sizeof(buffer), &received) == 5);
    CHECK(received != SD_NO_HANDLE);

    /* The received descriptor is the live other end of `inner`. */
    CHECK(msg_send(inner[0], "through", 8, SD_NO_HANDLE) == 0);
    CHECK(msg_recv(received, buffer, sizeof(buffer), NULL) == 8 && strcmp(buffer, "through") == 0);

    /* A message with an attachment that nobody receives must not leak it:
     * closing the channel releases the queued shared memory. */
    int shared = shm_create(8192);
    CHECK(shared >= 0);
    CHECK(msg_send(outer[0], "orphan", 7, shared) == 0);
    CHECK(close(shared) == 0);

    CHECK(close(received) == 0 && close(inner[0]) == 0);
    CHECK(close(outer[0]) == 0 && close(outer[1]) == 0);
    return 0;
}

static int test_poll(void)
{
    int first[2], second[2];
    char buffer[16];
    CHECK(channel_create(first) == 0 && channel_create(second) == 0);

    struct sd_pollfd watched[2] = {
        { .fd = first[1], .events = SD_POLL_IN },
        { .fd = second[1], .events = SD_POLL_IN },
    };

    /* Nothing pending: a zero timeout returns at once, a real one waits. */
    CHECK(poll_fds(watched, 2, 0) == 0);
    unsigned long long before = uptime_ms();
    CHECK(poll_fds(watched, 2, 60) == 0);
    unsigned long long waited = uptime_ms() - before;
    CHECK(waited >= 60 && waited <= 80);

    /* Only the channel that has a message is reported. */
    CHECK(msg_send(second[0], "data", 5, SD_NO_HANDLE) == 0);
    CHECK(poll_fds(watched, 2, -1) == 1);
    CHECK(watched[0].revents == 0 && (watched[1].revents & SD_POLL_IN));
    CHECK(msg_recv(second[1], buffer, sizeof(buffer), NULL) == 5);
    CHECK(poll_fds(watched, 2, 0) == 0);

    /* A closed peer is reported even though only input was asked for. */
    CHECK(close(first[0]) == 0);
    CHECK(poll_fds(watched, 2, -1) == 1);
    CHECK(watched[0].revents & SD_POLL_HUP);

    /* An invalid descriptor is never ready, and does not fail the call. */
    watched[0].fd = 99;
    CHECK(poll_fds(watched, 2, 0) == 0);
    CHECK(poll_fds(watched, SD_POLL_MAX + 1, 0) == -1 && errno == EINVAL);

    CHECK(close(first[1]) == 0 && close(second[0]) == 0 && close(second[1]) == 0);
    return 0;
}

int main(void)
{
    if (test_limits() != 0 || test_errors() != 0 || test_handle_passing() != 0 || test_poll() != 0) {
        return 1;
    }
    printf("ipc-edge: all checks passed\n");
    return 0;
}
