/*
 * A complete graphical session that ends by itself: the window server, the
 * desktop shell, an animated client, and a client that creates and
 * destroys windows. The kernel runs it twice and requires the second run
 * to leave no memory behind (see test_user_session_leak).
 */
#include <gui/protocol.h>
#include <ipc.h>
#include <stdio.h>
#include <unistd.h>

#define SESSION_MS "6000"
#define CHURN_RUNS 3

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
    char *compositor[] = { "/bin/compositor", "--run-for=" SESSION_MS, NULL };
    char *desktop[] = { "/bin/desktop", "--demo", NULL };
    char *demo[] = { "/test/guidemo", "Alpha", "200", "160", "0", NULL };
    char *churn[] = { "/test/leak", "windows", NULL };

    int server = spawn(compositor[0], compositor);
    if (server < 0 || !wait_for_server()) {
        printf("leak-session: the window server did not come up\n");
        return 1;
    }
    int shell = spawn(desktop[0], desktop);
    int client = spawn(demo[0], demo);
    if (shell < 0 || client < 0) {
        return 1;
    }
    /* The same work several times over: the server compares its memory
     * use between the repetitions (see compositor/leakwatch.c). */
    int churn_exit = 0, server_exit = -1, other = -1;
    for (int run = 0; run < CHURN_RUNS && churn_exit == 0; run++) {
        int churner = spawn(churn[0], churn);
        if (churner < 0 || waitpid(churner, &churn_exit) < 0) {
            return 1;
        }
        sleep_ms(400); /* let the last closing animation finish */
    }
    waitpid(server, &server_exit);
    /* The rest notice the closed connection and exit by themselves. */
    waitpid(shell, &other);
    waitpid(client, &other);
    printf("leak-session: window churn exited with %d, server with %d\n", churn_exit, server_exit);
    return churn_exit == 0 && server_exit == 0 ? 0 : 1;
}
