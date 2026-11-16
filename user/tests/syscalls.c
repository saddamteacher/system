/* The kernel must reject bad arguments with an error, never crash on them
 * or act on them. */
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

static int failures;

static void expect_error(const char *what, long result, int expected)
{
    if (result == -1 && errno == expected) {
        return;
    }
    printf("syscalls: %s: expected error %d, got result %ld errno %d\n", what, expected, result, errno);
    failures++;
}

int main(void)
{
    static const char constant[] = "read-only data";
    char buffer[16];

    expect_error("write to closed descriptor", write(9, "x", 1), EBADF);
    expect_error("write to huge descriptor", write(1 << 30, "x", 1), EBADF);
    expect_error("write to negative descriptor", write(-1, "x", 1), EBADF);

    expect_error("write from NULL", write(STDOUT_FILENO, NULL, 4), EFAULT);
    expect_error("write from kernel memory", write(STDOUT_FILENO, (void *)0xffffffff80000000UL, 16), EFAULT);
    expect_error("write past the end of memory", write(STDOUT_FILENO, (void *)0x00007ffffffff000UL, 16), EFAULT);
    /* Starts in valid memory, runs off into an unmapped page. */
    expect_error("write crossing into unmapped memory", write(STDOUT_FILENO, buffer, 64UL << 20), EFAULT);
    /* address + length wraps around zero. */
    expect_error("write with wrapping range", write(STDOUT_FILENO, (void *)-8L, 64), EFAULT);

    int descriptor = open("/etc/motd");
    if (descriptor < 0) {
        printf("syscalls: cannot open /etc/motd\n");
        return 1;
    }
    /* Reading INTO read-only memory: the kernel may not write there for us. */
    expect_error("read into read-only memory", read(descriptor, (void *)(unsigned long)constant, 4), EFAULT);
    expect_error("read into kernel memory", read(descriptor, (void *)0xffffffff80000000UL, 4), EFAULT);
    expect_error("write to a read-only file", write(descriptor, "x", 1), EROFS);
    expect_error("seek before the start", lseek(descriptor, -1, SEEK_SET), EINVAL);
    close(descriptor);
    expect_error("close twice", close(descriptor), EBADF);

    expect_error("open missing file", open("/no/such/file"), ENOENT);
    expect_error("open a directory", open("/etc"), EISDIR);
    expect_error("open with a bad pointer", open((const char *)0x10), EFAULT);
    expect_error("open relative path", open("etc/motd"), ENOENT);

    expect_error("munmap of nothing", munmap_pages((void *)0x500000000UL, 4096), EINVAL);
    expect_error("mmap of zero bytes", (long)(mmap_pages(0) == NULL ? -1 : 0), EINVAL);
    expect_error("wait for unknown process", waitpid(99999, NULL), ECHILD);

    long unknown = syscall6(4242, 0, 0, 0, 0, 0, 0);
    if (unknown != -ENOSYS) {
        printf("syscalls: unknown system call returned %ld\n", unknown);
        failures++;
    }

    /* The constant must be untouched after all of the above. */
    if (strcmp(constant, "read-only data") != 0) {
        failures++;
    }
    printf("syscalls: %d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
