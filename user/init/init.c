/*
 * init: the first user process, started by the kernel once it has booted.
 *
 * With the argument "--once" it reports and exits, which the boot check
 * uses. Without it init stays alive: it is the ancestor of everything that
 * runs later.
 */
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

int main(int argc, char **argv)
{
    printf("init: hello from user space, pid %d, up %llu ms\n", getpid(), (unsigned long long)uptime_ms());
    print_file("/etc/motd");

    if (argc > 1 && strcmp(argv[1], "--once") == 0) {
        return 0;
    }
    for (;;) {
        sleep_ms(60000);
    }
}
