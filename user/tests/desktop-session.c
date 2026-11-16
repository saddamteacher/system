/*
 * Starts the window server and the desktop shell, and waits for the server
 * to end. Used by the desktop and animation checks, whose scripts drive the
 * session from outside.
 */
#include <gui/protocol.h>
#include <ipc.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

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

/* With the argument "slow", window animations run six times slower, so
 * that a check can look at them half-way through. */
int main(int argc, char **argv)
{
    char *compositor[] = { "/bin/compositor", "--test", "--stats", NULL, NULL };
    if (argc > 1 && strcmp(argv[1], "slow") == 0) {
        compositor[3] = "--animation-scale=6";
    }
    char *desktop[] = { "/bin/desktop", NULL };

    int server = spawn(compositor[0], compositor);
    if (server < 0 || !wait_for_server()) {
        printf("desktop-session: the window server did not come up\n");
        return 1;
    }
    int shell = spawn(desktop[0], desktop);
    if (shell < 0) {
        return 1;
    }

    int server_exit = -1, shell_exit = -1;
    waitpid(server, &server_exit);
    /* The shell notices the closed connection and exits by itself. */
    waitpid(shell, &shell_exit);
    printf("desktop-session: server exited with %d, shell with %d\n", server_exit, shell_exit);
    return server_exit == 0 && shell_exit == 0 ? 0 : 1;
}
