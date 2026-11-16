/* Deliberately crashes. The kernel must end this process and carry on. */
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
    /* volatile: the compiler must really perform the access. */
    volatile unsigned char *volatile target = NULL;

    if (argc > 1 && strcmp(argv[1], "kernel") == 0) {
        /* The kernel's own memory: mapped, but not for user mode. */
        target = (volatile unsigned char *)0xffffffff80000000UL;
    }
    printf("fault: touching %p\n", (void *)target);
    volatile unsigned char value = *target;

    printf("fault: still alive after reading %u; that is a bug\n", value);
    return 0;
}
