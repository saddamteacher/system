/* Reading files and directories from the initial RAM file system. */
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            printf("files: line %d: failed: %s\n", __LINE__, #condition); \
            return 1; \
        } \
    } while (0)

static int directory_contains(const char *path, const char *name, unsigned type)
{
    struct sd_dirent entry;
    for (size_t index = 0; readdir_at(path, index, &entry) == 0; index++) {
        if (strcmp(entry.name, name) == 0 && entry.type == type) {
            return 1;
        }
    }
    return 0;
}

int main(void)
{
    static const char expected[] = "Welcome to Saddev.\n";
    char buffer[64];
    struct sd_stat info;

    CHECK(stat("/etc/motd", &info) == 0);
    CHECK(info.type == SD_TYPE_FILE && info.size > sizeof(expected));
    CHECK(stat("/etc", &info) == 0 && info.type == SD_TYPE_DIRECTORY);
    CHECK(stat("/", &info) == 0 && info.type == SD_TYPE_DIRECTORY);
    CHECK(stat("/dev/console", &info) == 0 && info.type == SD_TYPE_DEVICE);

    int descriptor = open("/etc/motd");
    CHECK(descriptor >= 3); /* 0-2 are the standard files */

    size_t length = sizeof(expected) - 1;
    CHECK(read(descriptor, buffer, length) == (long)length);
    CHECK(memcmp(buffer, expected, length) == 0);

    /* The position advanced; seeking moves it. */
    CHECK(lseek(descriptor, 0, SEEK_CUR) == (long)length);
    CHECK(lseek(descriptor, 11, SEEK_SET) == 11);
    CHECK(read(descriptor, buffer, 6) == 6 && memcmp(buffer, "Saddev", 6) == 0);

    /* Reading at the end returns 0, not an error. */
    CHECK(lseek(descriptor, 0, SEEK_END) > 0);
    CHECK(read(descriptor, buffer, sizeof(buffer)) == 0);
    CHECK(close(descriptor) == 0);

    CHECK(directory_contains("/", "bin", SD_TYPE_DIRECTORY));
    CHECK(directory_contains("/", "etc", SD_TYPE_DIRECTORY));
    CHECK(directory_contains("/etc", "motd", SD_TYPE_FILE));
    CHECK(directory_contains("/bin", "init", SD_TYPE_FILE));
    CHECK(directory_contains("/dev", "console", SD_TYPE_DEVICE));
    CHECK(!directory_contains("/", "motd", SD_TYPE_FILE)); /* not a direct child */

    printf("files: all checks passed\n");
    return 0;
}
