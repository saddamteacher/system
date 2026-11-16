#include <display.h>

#include <errno.h>
#include <stddef.h>
#include <sys/syscall.h>

int display_info(struct sd_fb_info *info)
{
    long result = syscall6(SYS_FB_INFO, (long)info, 0, 0, 0, 0, 0);
    if (result < 0) {
        errno = (int)-result;
        return -1;
    }
    return 0;
}

void *display_map(void)
{
    long result = syscall6(SYS_FB_MAP, 0, 0, 0, 0, 0, 0);
    if (result < 0) {
        errno = (int)-result;
        return NULL;
    }
    return (void *)result;
}
