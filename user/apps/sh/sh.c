/*
 * sh: a small command interpreter.
 *
 * It reads lines from standard input, runs built-in commands itself and
 * starts other programs by name. It keeps a current directory of its own
 * (the kernel has no such notion): the built-ins resolve relative paths
 * against it.
 */
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define LINE_MAX 512
#define MAX_ARGUMENTS SD_ARG_MAX

static char current_directory[SD_PATH_MAX + 1] = "/";

/* ---------------------------------------------------------------- paths */

/*
 * Turns `path` (absolute, or relative to the current directory) into a
 * clean absolute path in `result`: no "." or ".." components, no doubled
 * or trailing slashes. Returns 0, or -1 if it does not fit.
 */
static int resolve_path(const char *path, char *result, size_t size)
{
    char combined[2 * SD_PATH_MAX + 2];
    if (path[0] == '/') {
        strlcpy(combined, path, sizeof(combined));
    } else {
        snprintf(combined, sizeof(combined), "%s/%s", current_directory, path);
    }

    size_t length = 0;
    result[0] = '\0';
    const char *cursor = combined;
    while (*cursor != '\0') {
        while (*cursor == '/') {
            cursor++;
        }
        const char *end = cursor;
        while (*end != '\0' && *end != '/') {
            end++;
        }
        size_t component = (size_t)(end - cursor);
        if (component == 2 && cursor[0] == '.' && cursor[1] == '.') {
            /* Drop the last component already written. */
            while (length > 0 && result[length - 1] != '/') {
                length--;
            }
            if (length > 0) {
                length--;
            }
        } else if (component > 0 && !(component == 1 && cursor[0] == '.')) {
            if (length + 1 + component >= size) {
                return -1;
            }
            result[length++] = '/';
            memcpy(result + length, cursor, component);
            length += component;
        }
        result[length] = '\0';
        cursor = end;
    }
    if (length == 0) {
        strlcpy(result, "/", size);
    }
    return 0;
}

/* -------------------------------------------------------------- built-ins */

static int builtin_help(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    puts("Built-in commands:");
    puts("  help            this list");
    puts("  ls [dir]        list a directory");
    puts("  cd [dir]        change the current directory");
    puts("  pwd             print the current directory");
    puts("  cat file...     print files");
    puts("  echo text...    print the arguments");
    puts("  clear           clear the screen");
    puts("  date            the date and time (UTC)");
    puts("  uptime          time since the system started");
    puts("  mem             memory and process counts");
    puts("  log text...     write a line to the system log");
    puts("  exit            leave the shell");
    puts("Anything else is run as a program from /bin (or by path).");
    return 0;
}

static int builtin_echo(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        printf("%s%s", i > 1 ? " " : "", argv[i]);
    }
    printf("\n");
    return 0;
}

static int builtin_pwd(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    puts(current_directory);
    return 0;
}

static int builtin_cd(int argc, char **argv)
{
    char path[SD_PATH_MAX + 1];
    struct sd_stat info;
    if (resolve_path(argc > 1 ? argv[1] : "/", path, sizeof(path)) != 0) {
        printf("cd: path too long\n");
        return 1;
    }
    if (stat(path, &info) != 0) {
        printf("cd: %s: %s\n", path, strerror(errno));
        return 1;
    }
    if (info.type != SD_TYPE_DIRECTORY) {
        printf("cd: %s: not a directory\n", path);
        return 1;
    }
    strlcpy(current_directory, path, sizeof(current_directory));
    return 0;
}

static int builtin_ls(int argc, char **argv)
{
    char path[SD_PATH_MAX + 1];
    if (resolve_path(argc > 1 ? argv[1] : ".", path, sizeof(path)) != 0) {
        printf("ls: path too long\n");
        return 1;
    }
    struct sd_dirent entry;
    if (readdir_at(path, 0, &entry) != 0 && errno != ENOENT) {
        printf("ls: %s: %s\n", path, strerror(errno));
        return 1;
    }
    for (size_t index = 0; readdir_at(path, index, &entry) == 0; index++) {
        char child[SD_PATH_MAX + SD_NAME_MAX + 2];
        struct sd_stat info = { 0 };
        snprintf(child, sizeof(child), "%s/%s", strcmp(path, "/") == 0 ? "" : path, entry.name);
        stat(child, &info);
        if (entry.type == SD_TYPE_DIRECTORY) {
            printf("%10s  %s/\n", "", entry.name);
        } else if (entry.type == SD_TYPE_DEVICE) {
            printf("%10s  %s\n", "device", entry.name);
        } else {
            printf("%10llu  %s\n", (unsigned long long)info.size, entry.name);
        }
    }
    return 0;
}

static int print_file(const char *argument)
{
    char path[SD_PATH_MAX + 1];
    if (resolve_path(argument, path, sizeof(path)) != 0) {
        printf("cat: path too long\n");
        return 1;
    }
    int descriptor = open(path);
    if (descriptor < 0) {
        printf("cat: %s: %s\n", path, strerror(errno));
        return 1;
    }
    char buffer[512];
    long count;
    while ((count = read(descriptor, buffer, sizeof(buffer))) > 0) {
        write(STDOUT_FILENO, buffer, (size_t)count);
    }
    close(descriptor);
    return 0;
}

static int builtin_cat(int argc, char **argv)
{
    int status = 0;
    if (argc < 2) {
        printf("cat: which file?\n");
        return 1;
    }
    for (int i = 1; i < argc; i++) {
        status |= print_file(argv[i]);
    }
    return status;
}

static int builtin_clear(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    /* Form feed: the terminal's "clear the screen" character. */
    write(STDOUT_FILENO, "\f", 1);
    return 0;
}

static int builtin_uptime(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    unsigned long long seconds = uptime_ms() / 1000;
    printf("up %llu min %llu s\n", seconds / 60, seconds % 60);
    return 0;
}

static int builtin_date(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    unsigned long long now = wall_time();
    unsigned long long second_of_day = now % 86400;
    printf("%llu days since 1970-01-01, %02llu:%02llu:%02llu UTC\n", now / 86400, second_of_day / 3600,
           second_of_day % 3600 / 60, second_of_day % 60);
    return 0;
}

static int builtin_mem(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    struct sd_sysinfo info;
    if (sysinfo(&info) != 0) {
        return 1;
    }
    printf("memory: %llu MiB free of %llu MiB\n", (unsigned long long)(info.memory_free >> 20),
           (unsigned long long)(info.memory_total >> 20));
    printf("kernel heap: %llu KiB in use\n", (unsigned long long)(info.kernel_heap_used >> 10));
    printf("processes: %u, threads: %u\n", info.processes, info.threads);
    return 0;
}

/* Writes straight to the console device, whatever standard output is. */
static int builtin_log(int argc, char **argv)
{
    int console = open("/dev/console");
    if (console < 0) {
        return 1;
    }
    dprintf(console, "sh:");
    for (int i = 1; i < argc; i++) {
        dprintf(console, " %s", argv[i]);
    }
    dprintf(console, "\n");
    close(console);
    return 0;
}

struct builtin {
    const char *name;
    int (*run)(int argc, char **argv);
};

static const struct builtin builtins[] = {
    { "help", builtin_help }, { "echo", builtin_echo },     { "pwd", builtin_pwd },   { "cd", builtin_cd },
    { "ls", builtin_ls },     { "cat", builtin_cat },       { "clear", builtin_clear },
    { "date", builtin_date }, { "uptime", builtin_uptime }, { "mem", builtin_mem },   { "log", builtin_log },
};

/* ------------------------------------------------------ external programs */

static int run_program(int argc, char **argv)
{
    (void)argc;
    char path[SD_PATH_MAX + 1];
    int pid = -1;

    if (strchr(argv[0], '/') != NULL) {
        if (resolve_path(argv[0], path, sizeof(path)) == 0) {
            pid = spawn(path, argv);
        }
    } else {
        /* A bare name is looked for in the program directory. */
        snprintf(path, sizeof(path), "/bin/%s", argv[0]);
        pid = spawn(path, argv);
    }
    if (pid < 0) {
        printf("%s: %s\n", argv[0], errno == ENOENT ? "command not found" : strerror(errno));
        return 127;
    }
    int exit_code = 0;
    waitpid(pid, &exit_code);
    if (exit_code != 0) {
        printf("[%s exited with code %d]\n", argv[0], exit_code);
    }
    return exit_code;
}

/* ------------------------------------------------------------- the loop */

/*
 * Splits a line into words in place. Words are separated by spaces;
 * double quotes group several words into one argument.
 */
static int split_line(char *line, char **argv)
{
    int argc = 0;
    char *cursor = line;
    while (argc < MAX_ARGUMENTS - 1) {
        while (*cursor == ' ' || *cursor == '\t' || *cursor == '\n') {
            cursor++;
        }
        if (*cursor == '\0') {
            break;
        }
        char *out = cursor;
        argv[argc++] = out;
        bool quoted = false;
        while (*cursor != '\0' && (quoted || (*cursor != ' ' && *cursor != '\t' && *cursor != '\n'))) {
            if (*cursor == '"') {
                quoted = !quoted;
            } else {
                *out++ = *cursor;
            }
            cursor++;
        }
        bool at_end = *cursor == '\0';
        *out = '\0';
        if (at_end) {
            break;
        }
        cursor++;
    }
    argv[argc] = NULL;
    return argc;
}

static int run_line(char *line)
{
    char *argv[MAX_ARGUMENTS];
    int argc = split_line(line, argv);
    if (argc == 0) {
        return 0;
    }
    if (strcmp(argv[0], "exit") == 0) {
        exit(0);
    }
    for (size_t i = 0; i < sizeof(builtins) / sizeof(builtins[0]); i++) {
        if (strcmp(argv[0], builtins[i].name) == 0) {
            return builtins[i].run(argc, argv);
        }
    }
    return run_program(argc, argv);
}

int main(void)
{
    char line[LINE_MAX];
    printf("Saddev shell. Type \"help\" for a list of commands.\n");
    for (;;) {
        printf("%s $ ", current_directory);
        if (fgets_stdin(line, sizeof(line)) == NULL) {
            return 0; /* end of input: the terminal went away */
        }
        run_line(line);
    }
}
