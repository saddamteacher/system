/* Starting other programs, passing arguments, collecting exit codes. */
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            printf("spawn: line %d: failed: %s (errno %d)\n", __LINE__, #condition, errno); \
            return 1; \
        } \
    } while (0)

/* As a child: verify the arguments and report through the exit code. */
static int child_main(int argc, char **argv)
{
    if (argc != 4 || strcmp(argv[2], "alpha") != 0 || strcmp(argv[3], "two words") != 0 || argv[4] != NULL) {
        return 1;
    }
    printf("spawn: child %d got its arguments\n", getpid());
    return 42;
}

int main(int argc, char **argv)
{
    if (argc > 1 && strcmp(argv[1], "child") == 0) {
        return child_main(argc, argv);
    }

    char *child_arguments[] = { "/test/spawn", "child", "alpha", "two words", NULL };
    int first = spawn("/test/spawn", child_arguments);
    int second = spawn("/test/spawn", child_arguments);
    CHECK(first > 0 && second > 0 && first != second && first != getpid());

    /* Collected in the opposite order of creation: wait must not care. */
    int exit_code = -1;
    CHECK(waitpid(second, &exit_code) == 0 && exit_code == 42);
    exit_code = -1;
    CHECK(waitpid(first, &exit_code) == 0 && exit_code == 42);
    /* A process can be waited for only once. */
    CHECK(waitpid(first, &exit_code) == -1 && errno == ECHILD);

    char *plain[] = { "x", NULL };
    CHECK(spawn("/no/such/program", plain) == -1 && errno == ENOENT);
    CHECK(spawn("/etc/motd", plain) == -1 && errno == ENOEXEC); /* a file, but not a program */
    CHECK(spawn("/etc", plain) == -1 && errno == EISDIR);
    CHECK(spawn("/test/spawn", (char *const *)0x10) == -1 && errno == EFAULT);

    char *too_many[SD_ARG_MAX + 2];
    for (int i = 0; i <= SD_ARG_MAX; i++) {
        too_many[i] = "a";
    }
    too_many[SD_ARG_MAX + 1] = NULL;
    CHECK(spawn("/test/spawn", too_many) == -1 && errno == EINVAL);

    printf("spawn: all checks passed\n");
    return 0;
}
