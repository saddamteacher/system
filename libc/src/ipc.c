#include <ipc.h>

#include <errno.h>
#include <sys/syscall.h>
#include <unistd.h>

static long check(long result)
{
    if (result < 0) {
        errno = (int)-result;
        return -1;
    }
    return result;
}

int channel_create(int descriptors[2])
{
    return (int)check(syscall6(SYS_CHANNEL_CREATE, (long)descriptors, 0, 0, 0, 0, 0));
}

int port_create(const char *name)
{
    return (int)check(syscall6(SYS_PORT_CREATE, (long)name, 0, 0, 0, 0, 0));
}

int port_accept(int port)
{
    return (int)check(syscall6(SYS_PORT_ACCEPT, port, 0, 0, 0, 0, 0));
}

int port_connect(const char *name)
{
    return (int)check(syscall6(SYS_PORT_CONNECT, (long)name, 0, 0, 0, 0, 0));
}

int msg_send(int channel, const void *message, size_t size, int handle)
{
    return (int)check(syscall6(SYS_SEND, channel, (long)message, (long)size, handle, 0, 0));
}

long msg_recv(int channel, void *buffer, size_t capacity, int *handle)
{
    return check(syscall6(SYS_RECV, channel, (long)buffer, (long)capacity, (long)handle, 0, 0));
}

int poll_fds(struct sd_pollfd *descriptors, size_t count, int64_t timeout_ms)
{
    return (int)check(syscall6(SYS_POLL, (long)descriptors, (long)count, timeout_ms, 0, 0, 0));
}

int shm_create(size_t size)
{
    return (int)check(syscall6(SYS_SHM_CREATE, (long)size, 0, 0, 0, 0, 0));
}

int fstat(int descriptor, struct sd_stat *result)
{
    return (int)check(syscall6(SYS_FSTAT, descriptor, (long)result, 0, 0, 0, 0));
}

void *shm_map(int descriptor, size_t *size)
{
    struct sd_stat info;
    if (size != NULL) {
        if (fstat(descriptor, &info) < 0) {
            return NULL;
        }
        *size = info.size;
    }
    long address = check(syscall6(SYS_SHM_MAP, descriptor, 0, 0, 0, 0, 0));
    return address == -1 ? NULL : (void *)address;
}

int shm_unmap(void *address, size_t size)
{
    return munmap_pages(address, size);
}
