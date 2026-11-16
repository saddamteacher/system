#include <stdlib.h>

int main(int argc, char **argv);

/* Called from _start (crt0.asm). */
_Noreturn void __libc_start(int argc, char **argv);

_Noreturn void __libc_start(int argc, char **argv)
{
    exit(main(argc, argv));
}
