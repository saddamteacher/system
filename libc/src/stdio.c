#include <stdio.h>

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

enum length_modifier { LENGTH_INT, LENGTH_LONG, LENGTH_LONG_LONG, LENGTH_SIZE };

struct conversion {
    bool left_align;
    bool zero_pad;
    bool show_plus; /* '+': print a sign for positive numbers too */
    int width;
    int precision; /* -1 when not given */
    enum length_modifier length;
};

/* Where formatted characters go: a bounded buffer, or a file descriptor
 * fed through a small chunk so one printf costs few system calls. */
struct output {
    char *buffer;     /* snprintf target, or the chunk */
    size_t capacity;
    size_t used;      /* characters stored */
    size_t total;     /* characters produced overall */
    int descriptor;   /* -1 for snprintf */
};

static void output_flush(struct output *out)
{
    if (out->descriptor >= 0 && out->used > 0) {
        write(out->descriptor, out->buffer, out->used);
        out->used = 0;
    }
}

static void emit(struct output *out, char character)
{
    out->total++;
    if (out->descriptor >= 0) {
        if (out->used == out->capacity) {
            output_flush(out);
        }
        out->buffer[out->used++] = character;
    } else if (out->used + 1 < out->capacity) {
        /* Bounded buffer: drop what does not fit, keep room for the NUL. */
        out->buffer[out->used++] = character;
    }
}

static void emit_repeated(struct output *out, char character, int count)
{
    for (int i = 0; i < count; i++) {
        emit(out, character);
    }
}

static void emit_bytes(struct output *out, const char *text, size_t length)
{
    for (size_t i = 0; i < length; i++) {
        emit(out, text[i]);
    }
}

/* Emits prefix + body padded to the field width. Zero padding goes between
 * the two ("-0042"), space padding around both. */
static void emit_field(struct output *out, const struct conversion *conv, const char *prefix, const char *body,
                       size_t body_length, bool allow_zero_pad)
{
    size_t prefix_length = strlen(prefix);
    int content = (int)(prefix_length + body_length);
    int padding = conv->width > content ? conv->width - content : 0;
    bool zero_pad = allow_zero_pad && conv->zero_pad && !conv->left_align;

    if (!conv->left_align && !zero_pad) {
        emit_repeated(out, ' ', padding);
    }
    emit_bytes(out, prefix, prefix_length);
    if (zero_pad) {
        emit_repeated(out, '0', padding);
    }
    emit_bytes(out, body, body_length);
    if (conv->left_align) {
        emit_repeated(out, ' ', padding);
    }
}

/* Writes the digits of `value` at the END of `digits` (size 24) and returns
 * where they start. At least `minimum_digits` are produced. */
static char *format_unsigned(char *digits, uint64_t value, unsigned base, bool uppercase, int minimum_digits)
{
    const char *alphabet = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
    char *cursor = digits + 24;
    do {
        *--cursor = alphabet[value % base];
        value /= base;
        minimum_digits--;
    } while (value != 0 || (minimum_digits > 0 && cursor > digits));
    return cursor;
}

static void emit_integer(struct output *out, const struct conversion *conv, uint64_t value, unsigned base,
                         bool uppercase, const char *prefix)
{
    char digits[24];
    int minimum = conv->precision > 0 ? conv->precision : 1;
    if (minimum > 24) {
        minimum = 24;
    }
    char *start = format_unsigned(digits, value, base, uppercase, minimum);
    /* An explicit precision disables zero padding, as in standard printf. */
    emit_field(out, conv, prefix, start, (size_t)(digits + 24 - start), conv->precision < 0);
}

#define DOUBLE_MAX_PRECISION 15

static void emit_double(struct output *out, const struct conversion *conv, double value)
{
    const char *prefix = conv->show_plus ? "+" : "";
    if (value < 0) {
        prefix = "-";
        value = -value;
    }
    if (value != value) {
        emit_field(out, conv, "", "nan", 3, false);
        return;
    }
    /* Beyond this the integer part does not fit in 64 bits. */
    if (value > 1.8e19) {
        emit_field(out, conv, prefix, "inf", 3, false);
        return;
    }

    int precision = conv->precision < 0 ? 6 : conv->precision;
    if (precision > DOUBLE_MAX_PRECISION) {
        precision = DOUBLE_MAX_PRECISION;
    }
    uint64_t scale = 1;
    for (int i = 0; i < precision; i++) {
        scale *= 10;
    }
    uint64_t whole = (uint64_t)value;
    uint64_t fraction = (uint64_t)((value - (double)whole) * (double)scale + 0.5);
    if (fraction >= scale) { /* rounding carried into the integer part */
        whole++;
        fraction -= scale;
    }

    char text[24 + 1 + DOUBLE_MAX_PRECISION];
    char digits[24];
    char *start = format_unsigned(digits, whole, 10, false, 1);
    size_t length = (size_t)(digits + 24 - start);
    memcpy(text, start, length);
    if (precision > 0) {
        text[length++] = '.';
        start = format_unsigned(digits, fraction, 10, false, precision);
        memcpy(text + length, start, (size_t)precision);
        length += (size_t)precision;
    }
    emit_field(out, conv, prefix, text, length, true);
}

static uint64_t next_unsigned(va_list *arguments, enum length_modifier length)
{
    switch (length) {
    case LENGTH_LONG:
        return va_arg(*arguments, unsigned long);
    case LENGTH_LONG_LONG:
        return va_arg(*arguments, unsigned long long);
    case LENGTH_SIZE:
        return va_arg(*arguments, size_t);
    default:
        return va_arg(*arguments, unsigned int);
    }
}

static int64_t next_signed(va_list *arguments, enum length_modifier length)
{
    switch (length) {
    case LENGTH_LONG:
        return va_arg(*arguments, long);
    case LENGTH_LONG_LONG:
        return va_arg(*arguments, long long);
    case LENGTH_SIZE:
        return va_arg(*arguments, ptrdiff_t);
    default:
        return va_arg(*arguments, int);
    }
}

static const char *parse_number(const char *cursor, int *value)
{
    *value = 0;
    while (*cursor >= '0' && *cursor <= '9') {
        *value = *value * 10 + (*cursor - '0');
        cursor++;
    }
    return cursor;
}

/* Parses "[flags][width][.precision][length]"; returns the position after. */
static const char *parse_conversion(const char *cursor, struct conversion *conv, va_list *arguments)
{
    *conv = (struct conversion){ .precision = -1 };

    for (;; cursor++) {
        if (*cursor == '-') {
            conv->left_align = true;
        } else if (*cursor == '0') {
            conv->zero_pad = true;
        } else if (*cursor == '+') {
            conv->show_plus = true;
        } else {
            break;
        }
    }
    cursor = parse_number(cursor, &conv->width);
    if (*cursor == '.') {
        cursor++;
        if (*cursor == '*') {
            conv->precision = va_arg(*arguments, int);
            cursor++;
        } else {
            cursor = parse_number(cursor, &conv->precision);
        }
    }
    if (*cursor == 'l') {
        cursor++;
        conv->length = LENGTH_LONG;
        if (*cursor == 'l') {
            cursor++;
            conv->length = LENGTH_LONG_LONG;
        }
    } else if (*cursor == 'z') {
        cursor++;
        conv->length = LENGTH_SIZE;
    }
    return cursor;
}

static void emit_conversion(struct output *out, const struct conversion *conv, char specifier, va_list *arguments)
{
    switch (specifier) {
    case 'c': {
        char character = (char)va_arg(*arguments, int);
        emit_field(out, conv, "", &character, 1, false);
        break;
    }
    case 's': {
        const char *text = va_arg(*arguments, const char *);
        if (text == NULL) {
            text = "(null)";
        }
        /* A precision limits how much of the string is printed, and makes
         * it legal to pass text that is not NUL-terminated. */
        size_t length = conv->precision >= 0 ? strnlen(text, (size_t)conv->precision) : strlen(text);
        emit_field(out, conv, "", text, length, false);
        break;
    }
    case 'd':
    case 'i': {
        int64_t value = next_signed(arguments, conv->length);
        if (value < 0) {
            emit_integer(out, conv, 0 - (uint64_t)value, 10, false, "-");
        } else {
            emit_integer(out, conv, (uint64_t)value, 10, false, conv->show_plus ? "+" : "");
        }
        break;
    }
    case 'u':
        emit_integer(out, conv, next_unsigned(arguments, conv->length), 10, false, "");
        break;
    case 'x':
        emit_integer(out, conv, next_unsigned(arguments, conv->length), 16, false, "");
        break;
    case 'X':
        emit_integer(out, conv, next_unsigned(arguments, conv->length), 16, true, "");
        break;
    case 'p': {
        struct conversion pointer = *conv;
        pointer.precision = 2 * (int)sizeof(void *);
        emit_integer(out, &pointer, (uintptr_t)va_arg(*arguments, void *), 16, false, "0x");
        break;
    }
    case 'f':
        emit_double(out, conv, va_arg(*arguments, double));
        break;
    case '%':
        emit(out, '%');
        break;
    default:
        emit(out, '%');
        emit(out, specifier);
        break;
    }
}

static void format(struct output *out, const char *format_text, va_list arguments)
{
    va_list remaining;
    va_copy(remaining, arguments);

    for (const char *cursor = format_text; *cursor != '\0'; cursor++) {
        if (*cursor != '%') {
            emit(out, *cursor);
            continue;
        }
        struct conversion conv;
        cursor = parse_conversion(cursor + 1, &conv, &remaining);
        if (*cursor == '\0') {
            emit(out, '%');
            break;
        }
        emit_conversion(out, &conv, *cursor, &remaining);
    }
    va_end(remaining);
}

int vsnprintf(char *buffer, size_t size, const char *format_text, va_list arguments)
{
    struct output out = { .buffer = buffer, .capacity = size, .descriptor = -1 };
    format(&out, format_text, arguments);
    if (size > 0) {
        buffer[out.used] = '\0';
    }
    return (int)out.total;
}

int snprintf(char *buffer, size_t size, const char *format_text, ...)
{
    va_list arguments;
    va_start(arguments, format_text);
    int length = vsnprintf(buffer, size, format_text, arguments);
    va_end(arguments);
    return length;
}

static int vdprintf_chunked(int descriptor, const char *format_text, va_list arguments)
{
    char chunk[256];
    struct output out = { .buffer = chunk, .capacity = sizeof(chunk), .descriptor = descriptor };
    format(&out, format_text, arguments);
    output_flush(&out);
    return (int)out.total;
}

int vprintf(const char *format_text, va_list arguments)
{
    return vdprintf_chunked(STDOUT_FILENO, format_text, arguments);
}

int printf(const char *format_text, ...)
{
    va_list arguments;
    va_start(arguments, format_text);
    int length = vdprintf_chunked(STDOUT_FILENO, format_text, arguments);
    va_end(arguments);
    return length;
}

int dprintf(int descriptor, const char *format_text, ...)
{
    va_list arguments;
    va_start(arguments, format_text);
    int length = vdprintf_chunked(descriptor, format_text, arguments);
    va_end(arguments);
    return length;
}

int puts(const char *text)
{
    return printf("%s\n", text);
}

int putchar(int character)
{
    char byte = (char)character;
    return write(STDOUT_FILENO, &byte, 1) == 1 ? character : EOF;
}

/*
 * Input is buffered: when standard input is a channel, each read returns a
 * whole message (typically a line), which would not fit a one-byte request.
 */
static char input_buffer[4096];
static long input_length;
static long input_position;

int getchar(void)
{
    if (input_position >= input_length) {
        input_length = read(STDIN_FILENO, input_buffer, sizeof(input_buffer));
        input_position = 0;
        if (input_length <= 0) {
            input_length = 0;
            return EOF;
        }
    }
    return (unsigned char)input_buffer[input_position++];
}

char *fgets_stdin(char *buffer, int size)
{
    int length = 0;
    while (length < size - 1) {
        int character = getchar();
        if (character == EOF) {
            break;
        }
        buffer[length++] = (char)character;
        if (character == '\n') {
            break;
        }
    }
    if (length == 0) {
        return NULL;
    }
    buffer[length] = '\0';
    return buffer;
}
