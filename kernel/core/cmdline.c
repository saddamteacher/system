#include <core/cmdline.h>

#include <core/string.h>

static const char *skip_spaces(const char *cursor)
{
    while (*cursor == ' ') {
        cursor++;
    }
    return cursor;
}

static const char *word_end(const char *cursor)
{
    while (*cursor != '\0' && *cursor != ' ') {
        cursor++;
    }
    return cursor;
}

static void copy_truncated(char *destination, size_t size, const char *source, size_t length)
{
    if (length >= size) {
        length = size - 1;
    }
    memcpy(destination, source, length);
    destination[length] = '\0';
}

bool cmdline_get(const char *cmdline, const char *key, char *value, size_t size)
{
    size_t key_length = strlen(key);

    for (const char *word = skip_spaces(cmdline); *word != '\0';) {
        const char *end = word_end(word);
        size_t word_length = (size_t)(end - word);

        /* A match is "key=" at the start of a word; a longer key that merely
         * begins with `key` must not match. */
        if (word_length > key_length && strncmp(word, key, key_length) == 0 && word[key_length] == '=') {
            const char *start = word + key_length + 1;
            copy_truncated(value, size, start, (size_t)(end - start));
            return true;
        }
        word = skip_spaces(end);
    }
    return false;
}
