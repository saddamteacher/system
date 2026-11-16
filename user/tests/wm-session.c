/*
 * Starts a window server and two demo clients, and waits for the server to
 * end. Used by the compositor check; tools/qemu-wm-check.py drives the
 * session from outside.
 */
#include <gui/protocol.h>
#include <ipc.h>
#include <stdio.h>
#include <unistd.h>

static int start(char *const arguments[])
{
    int pid = spawn(arguments[0], arguments);
    if (pid < 0) {
        printf("wm-session: cannot start %s\n", arguments[0]);
    }
    return pid;
}

/* The server is ready once its port exists. */
static int wait_for_server(void)
{
    for (int attempt = 0; attempt < 200; attempt++) {
        int channel = port_connect(GUI_SERVER_PORT);
        if (channel >= 0) {
            close(channel);
            return 1;
        }
        sleep_ms(10);
    }
    return 0;
}

int main(void)
{
    char *compositor[] = { "/bin/compositor", "--test", "--stats", NULL };
    char *first[] = { "/test/guidemo", "Alpha", "200", "180", "0", NULL };
    char *second[] = { "/test/guidemo", "Beta", "420", "300", "1", NULL };

    int server = start(compositor);
    if (server < 0 || !wait_for_server()) {
        printf("wm-session: the window server did not come up\n");
        return 1;
    }
    int first_pid = start(first);
    /* Start them one after the other so the stacking order is known. */
    sleep_ms(300);
    int second_pid = start(second);
    if (first_pid < 0 || second_pid < 0) {
        return 1;
    }

    int server_exit = -1, client_exit = -1;
    waitpid(server, &server_exit);
    /* The clients notice the closed connection and exit by themselves. */
    int clients_ok = waitpid(first_pid, &client_exit) == 0 && client_exit == 0;
    clients_ok &= waitpid(second_pid, &client_exit) == 0 && client_exit == 0;

    printf("wm-session: server exited with %d, clients %s\n", server_exit, clients_ok ? "ok" : "FAILED");
    return server_exit == 0 && clients_ok ? 0 : 1;
}
