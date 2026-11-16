/*
 * Two processes exchange messages and share a buffer.
 *
 * The parent is a server: it publishes a port and answers requests. The
 * child is a client: it connects by name, sends text messages and checks
 * the replies, then creates a shared memory object, sends it over the
 * channel, and both sides read and write the same memory.
 */
#include <errno.h>
#include <ipc.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define PORT_NAME "test.echo"
#define SHARED_SIZE (64 * 1024)
#define ROUNDS 50

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            printf("ipc[%d]: line %d: failed: %s (errno %d)\n", getpid(), __LINE__, #condition, errno); \
            return 1; \
        } \
    } while (0)

enum { REQUEST_UPPERCASE = 1, REQUEST_SHARE = 2 };

struct request {
    int type;
    int sequence;
    char text[32];
};

/* ------------------------------------------------------------- client */

static int client_main(void)
{
    int server = port_connect(PORT_NAME);
    CHECK(server >= 0);

    /* Messages arrive whole and in order. */
    for (int i = 0; i < ROUNDS; i++) {
        struct request request = { .type = REQUEST_UPPERCASE, .sequence = i };
        snprintf(request.text, sizeof(request.text), "message %d", i);
        CHECK(msg_send(server, &request, sizeof(request), SD_NO_HANDLE) == 0);

        struct request reply;
        CHECK(msg_recv(server, &reply, sizeof(reply), NULL) == (long)sizeof(reply));
        char expected[32];
        snprintf(expected, sizeof(expected), "MESSAGE %d", i);
        CHECK(reply.sequence == i && strcmp(reply.text, expected) == 0);
    }

    /* Share a buffer: create it, fill it, send the descriptor. */
    int shared = shm_create(SHARED_SIZE);
    CHECK(shared >= 0);
    size_t size = 0;
    unsigned char *memory = shm_map(shared, &size);
    CHECK(memory != NULL && size == SHARED_SIZE);
    for (size_t i = 0; i < size; i++) {
        CHECK(memory[i] == 0); /* fresh shared memory is zero-filled */
        memory[i] = (unsigned char)(i * 7);
    }
    struct request share = { .type = REQUEST_SHARE };
    CHECK(msg_send(server, &share, sizeof(share), shared) == 0);
    /* Our descriptor is no longer needed: the mapping keeps the memory. */
    CHECK(close(shared) == 0);

    /* The server verified our pattern and wrote its own. No data was sent
     * back in the reply: we see the change through the shared memory. */
    struct request reply;
    CHECK(msg_recv(server, &reply, sizeof(reply), NULL) == (long)sizeof(reply));
    CHECK(strcmp(reply.text, "pattern ok") == 0);
    for (size_t i = 0; i < size; i++) {
        CHECK(memory[i] == (unsigned char)(i * 13 + 1));
    }
    CHECK(shm_unmap(memory, size) == 0);

    printf("ipc[%d]: client done\n", getpid());
    return 0; /* exiting closes the channel, which the server notices */
}

/* ------------------------------------------------------------- server */

static void uppercase(char *text)
{
    for (; *text != '\0'; text++) {
        if (*text >= 'a' && *text <= 'z') {
            *text = (char)(*text - 'a' + 'A');
        }
    }
}

static int serve_shared(int client, int handle, struct request *request)
{
    size_t size = 0;
    unsigned char *memory = shm_map(handle, &size);
    CHECK(memory != NULL && size == SHARED_SIZE);
    CHECK(close(handle) == 0);

    int pattern_ok = 1;
    for (size_t i = 0; i < size; i++) {
        if (memory[i] != (unsigned char)(i * 7)) {
            pattern_ok = 0;
        }
        memory[i] = (unsigned char)(i * 13 + 1);
    }
    CHECK(shm_unmap(memory, size) == 0);

    strlcpy(request->text, pattern_ok ? "pattern ok" : "pattern wrong", sizeof(request->text));
    CHECK(msg_send(client, request, sizeof(*request), SD_NO_HANDLE) == 0);
    return 0;
}

static int server_main(void)
{
    int port = port_create(PORT_NAME);
    CHECK(port >= 0);
    CHECK(port_create(PORT_NAME) == -1 && errno == EEXIST);

    char *arguments[] = { "/test/ipc", "client", NULL };
    int child = spawn("/test/ipc", arguments);
    CHECK(child > 0);

    int client = port_accept(port);
    CHECK(client >= 0);

    int served = 0, shared_done = 0;
    for (;;) {
        struct request request;
        int handle = SD_NO_HANDLE;
        long size = msg_recv(client, &request, sizeof(request), &handle);
        if (size == 0) {
            break; /* the client closed its end */
        }
        CHECK(size == (long)sizeof(request));

        if (request.type == REQUEST_UPPERCASE) {
            CHECK(handle == SD_NO_HANDLE && request.sequence == served);
            uppercase(request.text);
            CHECK(msg_send(client, &request, sizeof(request), SD_NO_HANDLE) == 0);
            served++;
        } else if (request.type == REQUEST_SHARE) {
            CHECK(handle != SD_NO_HANDLE);
            if (serve_shared(client, handle, &request) != 0) {
                return 1;
            }
            shared_done = 1;
        }
    }
    CHECK(served == ROUNDS && shared_done);

    /* Sending to a closed peer fails cleanly. */
    CHECK(msg_send(client, "x", 1, SD_NO_HANDLE) == -1 && errno == EPIPE);
    CHECK(close(client) == 0);

    int exit_code = -1;
    CHECK(waitpid(child, &exit_code) == 0 && exit_code == 0);

    /* Closing the port frees its name. */
    CHECK(close(port) == 0);
    CHECK(port_connect(PORT_NAME) == -1 && errno == ENOENT);

    printf("ipc[%d]: server done, %d messages and one shared buffer\n", getpid(), served);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc > 1 && strcmp(argv[1], "client") == 0) {
        return client_main();
    }
    return server_main();
}
