/*
 * The archive format is "ustar", the POSIX tar format: a sequence of
 * 512-byte header blocks, each followed by the file's data padded to a
 * multiple of 512 bytes. Numbers are stored as octal text. Two all-zero
 * blocks end the archive.
 *
 * State: the entry index is built once in tarfs_create() and read-only
 * afterwards, so no locking is needed.
 */
#include <fs/tarfs.h>

#include <stdbool.h>
#include <stdint.h>

#include <saddev/errno.h>

#include <core/log.h>
#include <core/string.h>
#include <fs/vfs.h>
#include <mm/heap.h>

#define TAR_BLOCK_SIZE 512
#define TAR_NAME_LENGTH 100
#define TAR_TYPE_FILE '0'
#define TAR_TYPE_FILE_OLD '\0'
#define TAR_TYPE_DIRECTORY '5'

struct tar_header {
    char name[TAR_NAME_LENGTH];
    char mode[8];
    char uid[8];
    char gid[8];
    char size[12]; /* octal */
    char mtime[12];
    char checksum[8];
    char type;
    char link_name[100];
    char magic[6]; /* "ustar" */
    char version[2];
    char padding[TAR_BLOCK_SIZE - 265];
};

_Static_assert(sizeof(struct tar_header) == TAR_BLOCK_SIZE, "tar header must be one block");

#define ENTRY_PATH_LENGTH (TAR_NAME_LENGTH + 2)

struct tar_entry {
    char path[ENTRY_PATH_LENGTH]; /* absolute, no trailing slash */
    const uint8_t *data;
    uint64_t size;
    bool directory;
};

struct tarfs {
    struct tar_entry *entries;
    size_t entry_count;
};

static uint64_t parse_octal(const char *text, size_t length)
{
    uint64_t value = 0;
    for (size_t i = 0; i < length && text[i] >= '0' && text[i] <= '7'; i++) {
        value = value * 8 + (uint64_t)(text[i] - '0');
    }
    return value;
}

static bool block_is_zero(const uint8_t *block)
{
    for (size_t i = 0; i < TAR_BLOCK_SIZE; i++) {
        if (block[i] != 0) {
            return false;
        }
    }
    return true;
}

/* Archives name members "bin/init" or "./bin/init", directories with a
 * trailing slash. We want "/bin/init" and "/bin". */
static void normalize_path(char *destination, const char *name)
{
    size_t length = strnlen(name, TAR_NAME_LENGTH);
    if (length >= 2 && name[0] == '.' && name[1] == '/') {
        name += 2;
        length -= 2;
    } else if (length == 1 && name[0] == '.') {
        length = 0;
    }
    while (length > 0 && name[length - 1] == '/') {
        length--;
    }
    destination[0] = '/';
    memcpy(destination + 1, name, length);
    destination[1 + length] = '\0';
}

/* Calls `visit` for each member; returns false if the archive is cut off. */
typedef void (*visit_fn)(const struct tar_header *header, const uint8_t *data, uint64_t size, void *context);

static bool walk_archive(const uint8_t *archive, size_t size, visit_fn visit, void *context)
{
    size_t offset = 0;
    while (offset + TAR_BLOCK_SIZE <= size && !block_is_zero(archive + offset)) {
        const struct tar_header *header = (const struct tar_header *)(archive + offset);
        if (memcmp(header->magic, "ustar", 5) != 0) {
            return false;
        }
        uint64_t file_size = parse_octal(header->size, sizeof(header->size));
        uint64_t data_blocks = (file_size + TAR_BLOCK_SIZE - 1) / TAR_BLOCK_SIZE;
        if (offset + TAR_BLOCK_SIZE + data_blocks * TAR_BLOCK_SIZE > size) {
            return false;
        }
        visit(header, archive + offset + TAR_BLOCK_SIZE, file_size, context);
        offset += TAR_BLOCK_SIZE + data_blocks * TAR_BLOCK_SIZE;
    }
    return true;
}

static bool is_indexable(const struct tar_header *header)
{
    return header->type == TAR_TYPE_FILE || header->type == TAR_TYPE_FILE_OLD || header->type == TAR_TYPE_DIRECTORY;
}

static void count_entry(const struct tar_header *header, const uint8_t *data, uint64_t size, void *context)
{
    (void)data;
    (void)size;
    if (is_indexable(header)) {
        (*(size_t *)context)++;
    }
}

static void add_entry(const struct tar_header *header, const uint8_t *data, uint64_t size, void *context)
{
    struct tarfs *fs = context;
    if (!is_indexable(header)) {
        return;
    }
    struct tar_entry *entry = &fs->entries[fs->entry_count++];
    normalize_path(entry->path, header->name);
    entry->directory = header->type == TAR_TYPE_DIRECTORY;
    entry->data = data;
    entry->size = entry->directory ? 0 : size;
}

static const struct tar_entry *find_entry(const struct tarfs *fs, const char *path)
{
    for (size_t i = 0; i < fs->entry_count; i++) {
        if (strcmp(fs->entries[i].path, path) == 0) {
            return &fs->entries[i];
        }
    }
    return NULL;
}

static bool is_root(const char *path)
{
    return path[0] == '/' && path[1] == '\0';
}

/*
 * If `path` names something directly inside `directory`, returns its name
 * (the last component); otherwise NULL.
 */
static const char *name_inside(const char *path, const char *directory)
{
    size_t length = is_root(directory) ? 0 : strlen(directory);
    if (strncmp(path, directory, length) != 0 || path[length] != '/') {
        return NULL;
    }
    const char *name = path + length + 1;
    if (name[0] == '\0' || strchr(name, '/') != NULL) {
        return NULL; /* the directory itself, or something deeper */
    }
    return name;
}

static int64_t tarfs_read(struct file *file, void *buffer, size_t size)
{
    const struct tar_entry *entry = file->context;
    uint64_t remaining = entry->size - file->offset;
    if (size > remaining) {
        size = remaining;
    }
    memcpy(buffer, entry->data + file->offset, size);
    return (int64_t)size;
}

static int64_t tarfs_write(struct file *file, const void *buffer, size_t size)
{
    (void)file;
    (void)buffer;
    (void)size;
    return -EROFS;
}

static const struct file_ops tarfs_file_ops = {
    .read = tarfs_read,
    .write = tarfs_write,
};

static int tarfs_stat(void *context, const char *path, struct sd_stat *stat)
{
    const struct tarfs *fs = context;
    if (is_root(path)) {
        stat->type = SD_TYPE_DIRECTORY;
        return 0;
    }
    const struct tar_entry *entry = find_entry(fs, path);
    if (entry == NULL) {
        return -ENOENT;
    }
    stat->type = entry->directory ? SD_TYPE_DIRECTORY : SD_TYPE_FILE;
    stat->size = entry->size;
    return 0;
}

static int tarfs_open(void *context, const char *path, struct file *file)
{
    const struct tarfs *fs = context;
    const struct tar_entry *entry = find_entry(fs, path);
    if (entry == NULL) {
        return is_root(path) ? -EISDIR : -ENOENT;
    }
    if (entry->directory) {
        return -EISDIR;
    }
    file->ops = &tarfs_file_ops;
    /* The index is never modified, so pointing into it is safe. */
    file->context = (void *)(uintptr_t)entry;
    file->type = SD_TYPE_FILE;
    file->size = entry->size;
    return 0;
}

static int tarfs_readdir(void *context, const char *path, size_t index, struct sd_dirent *result)
{
    const struct tarfs *fs = context;
    if (!is_root(path)) {
        const struct tar_entry *directory = find_entry(fs, path);
        if (directory == NULL) {
            return -ENOENT;
        }
        if (!directory->directory) {
            return -ENOTDIR;
        }
    }
    for (size_t i = 0; i < fs->entry_count; i++) {
        const char *name = name_inside(fs->entries[i].path, path);
        if (name == NULL) {
            continue;
        }
        if (index-- == 0) {
            result->type = fs->entries[i].directory ? SD_TYPE_DIRECTORY : SD_TYPE_FILE;
            size_t length = strlen(name);
            if (length > SD_NAME_MAX) {
                length = SD_NAME_MAX;
            }
            memcpy(result->name, name, length);
            result->name[length] = '\0';
            return 0;
        }
    }
    return -ENOENT;
}

const struct filesystem *tarfs_create(const void *archive, size_t size)
{
    size_t count = 0;
    if (!walk_archive(archive, size, count_entry, &count)) {
        log_error("tarfs: archive is damaged or not in ustar format");
        return NULL;
    }

    struct tarfs *fs = kzalloc(sizeof(*fs));
    struct filesystem *filesystem = kzalloc(sizeof(*filesystem));
    struct tar_entry *entries = kzalloc((count > 0 ? count : 1) * sizeof(*entries));
    if (fs == NULL || filesystem == NULL || entries == NULL) {
        kfree(fs);
        kfree(filesystem);
        kfree(entries);
        return NULL;
    }
    fs->entries = entries;
    walk_archive(archive, size, add_entry, fs);

    *filesystem = (struct filesystem){
        .name = "tarfs",
        .context = fs,
        .open = tarfs_open,
        .stat = tarfs_stat,
        .readdir = tarfs_readdir,
    };
    log_info("tarfs: %zu entries in a %zu KiB archive", fs->entry_count, size / 1024);
    return filesystem;
}
