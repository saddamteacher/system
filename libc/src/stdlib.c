#include <stdlib.h>

#include <unistd.h>

_Noreturn void exit(int status)
{
    _exit(status);
}

_Noreturn void abort(void)
{
    static const char message[] = "abort() called\n";
    write(STDERR_FILENO, message, sizeof(message) - 1);
    _exit(134);
}

int abs(int value)
{
    return value < 0 ? -value : value;
}

static int digit_value(char character)
{
    if (character >= '0' && character <= '9') {
        return character - '0';
    }
    if (character >= 'a' && character <= 'z') {
        return character - 'a' + 10;
    }
    if (character >= 'A' && character <= 'Z') {
        return character - 'A' + 10;
    }
    return 99; /* larger than any base */
}

long strtol(const char *text, char **end, int base)
{
    while (*text == ' ' || *text == '\t' || *text == '\n') {
        text++;
    }
    int negative = 0;
    if (*text == '-' || *text == '+') {
        negative = *text == '-';
        text++;
    }
    if ((base == 0 || base == 16) && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        text += 2;
        base = 16;
    } else if (base == 0) {
        base = text[0] == '0' ? 8 : 10;
    }

    long value = 0;
    while (digit_value(*text) < base) {
        value = value * base + digit_value(*text);
        text++;
    }
    if (end != NULL) {
        *end = (char *)(unsigned long)text;
    }
    return negative ? -value : value;
}

int atoi(const char *text)
{
    return (int)strtol(text, NULL, 10);
}
