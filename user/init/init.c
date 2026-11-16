/*
 * init: the first user process, started by the kernel once it has booted.
 * It brings up the graphical session and then stays alive as the ancestor
 * of everything that runs later.
 *
 * With the argument "--once" it only reports and exits, which the boot
 * check uses.
 */
#include <gui/protocol.h>
#include <ipc.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static void print_file(const char *path)
{
    char buffer[128];
    int descriptor = open(path);
    if (descriptor < 0) {
        printf("init: cannot open %s\n", path);
        return;
    }
    long count;
    while ((count = read(descriptor, buffer, sizeof(buffer))) > 0) {
        write(STDOUT_FILENO, buffer, (size_t)count);
    }
    close(descriptor);
}

static int start(char *const arguments[])
{
    int pid = spawn(arguments[0], arguments);
    if (pid < 0) {
        printf("init: cannot start %s\n", arguments[0]);
    } else {
        detach(pid); /* init does not wait for the programs it starts */
    }
    return pid;
}

/* The window server is ready once its port exists. */
static int wait_for_window_server(void)
{
    for (int attempt = 0; attempt < 300; attempt++) {
        int channel = port_connect(GUI_SERVER_PORT);
        if (channel >= 0) {
            close(channel);
            return 1;
        }
        sleep_ms(10);
    }
    return 0;
}

static void start_session(void)
{
    char *compositor[] = { "/bin/compositor", NULL };
    char *desktop[] = { "/bin/desktop", NULL };

    if (start(compositor) < 0 || !wait_for_window_server()) {
        printf("init: no graphical session (the window server did not start)\n");
        return;
    }
    start(desktop);
}

int main(int argc, char **argv)
{
    printf("init: hello from user space, pid %d, up %llu ms\n", getpid(), (unsigned long long)uptime_ms());
    print_file("/etc/motd");

    if (argc > 1 && strcmp(argv[1], "--once") == 0) {
        return 0;
    }
    start_session();
    for (;;) {
        sleep_ms(60000);
    }
}
