/*
 * Floating point state must be private to each process and survive context
 * switches.
 *
 * The rounding mode is the part of that state that is easy to observe: it
 * stays set until changed, across function calls and system calls. Four
 * workers each select a different mode and keep yielding the CPU to each
 * other. If the kernel did not save and restore the state on a switch, a
 * worker would find another worker's mode after a yield.
 *
 * (Values in the data registers cannot be used for this in C: the calling
 * convention lets any call clobber them, so the compiler never keeps one
 * live across the yield.)
 */
#include <fenv.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define WORKERS 4
#define YIELDS 400

static const int modes[WORKERS] = { FE_TONEAREST, FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO };

/* 1/3 is not exactly representable, so the rounding mode decides between
 * the two nearest doubles. */
static double one_third(void)
{
    volatile double one = 1.0, three = 3.0;
    return one / three;
}

static int worker_main(int index)
{
    /* A new process starts with default state, whatever its parent set. */
    if (fegetround() != FE_TONEAREST) {
        printf("fpu: worker %d did not start with the default rounding mode\n", index);
        return 1;
    }
    double nearest = one_third();

    if (fesetround(modes[index]) != 0) {
        return 1;
    }
    double mine = one_third();

    for (int i = 0; i < YIELDS; i++) {
        sched_yield();
        if (fegetround() != modes[index]) {
            printf("fpu: worker %d lost its rounding mode after %d yields\n", index, i + 1);
            return 1;
        }
        /* The mode must also still be in effect for arithmetic. */
        if (one_third() != mine) {
            printf("fpu: worker %d computes differently after %d yields\n", index, i + 1);
            return 1;
        }
    }
    /* Sanity: rounding up really gives a larger result than rounding down. */
    if (modes[index] == FE_UPWARD && !(mine > nearest)) {
        return 1;
    }
    if (modes[index] == FE_DOWNWARD && !(mine <= nearest)) {
        return 1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    if (argc > 2 && strcmp(argv[1], "worker") == 0) {
        return worker_main(atoi(argv[2]));
    }

    /* The parent uses a non-default mode too, to prove children do not
     * inherit it. */
    fesetround(FE_TOWARDZERO);

    int pids[WORKERS];
    static char indices[WORKERS][4];
    for (int i = 0; i < WORKERS; i++) {
        snprintf(indices[i], sizeof(indices[i]), "%d", i);
        char *arguments[] = { "/test/fpu", "worker", indices[i], NULL };
        pids[i] = spawn("/test/fpu", arguments);
        if (pids[i] < 0) {
            return 1;
        }
    }
    int failures = 0;
    for (int i = 0; i < WORKERS; i++) {
        int exit_code = -1;
        if (waitpid(pids[i], &exit_code) != 0 || exit_code != 0) {
            failures++;
        }
    }
    printf("fpu: %d of %d workers failed\n", failures, WORKERS);
    return failures == 0 ? 0 : 1;
}
