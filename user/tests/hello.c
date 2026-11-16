/* The smallest useful program: output through the raw system call wrapper
 * and through printf, and the arguments it was started with. */
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    static const char message[] = "hello: written with write()\n";
    if (write(STDOUT_FILENO, message, sizeof(message) - 1) != (long)(sizeof(message) - 1)) {
        return 1;
    }
    printf("hello: printf says %d + %d = %d, pi is about %.3f\n", 2, 2, 2 + 2, 3.14159);

    /* Flags, width and precision, and that each conversion takes exactly
     * its own argument. */
    char text[64];
    snprintf(text, sizeof(text), "%+d|%+d|%s|%05d|%-4d|%.2f|%.3s", 7, -7, "ok", 42, 3, 2.5, "abcdef");
    if (strcmp(text, "+7|-7|ok|00042|3   |2.50|abc") != 0) {
        printf("hello: snprintf produced \"%s\"\n", text);
        return 3;
    }

    if (argc != 1 || strcmp(argv[0], "/test/hello") != 0 || argv[1] != NULL) {
        printf("hello: unexpected arguments\n");
        return 2;
    }
    return 0;
}
