#include <core/printf.h>

#include <stdbool.h>
#include <stdint.h>

#include <core/string.h>

enum length_modifier { LENGTH_INT, LENGTH_LONG, LENGTH_LONG_LONG, LENGTH_SIZE };

struct conversion {
    bool left_align;
    bool zero_pad;
    bool show_plus; /* '+': print a sign for positive numbers too */
    int width;
    enum length_modifier length;
};

struct output {
    format_emit_fn emit;
    void *context;
};

static void emit_repeated(const struct output *out, char character, int count)
{
    for (int i = 0; i < count; i++) {
        out->emit(out->context, character);
    }
}

static void emit_bytes(const struct output *out, const char *text, size_t length)
{
    for (size_t i = 0; i < length; i++) {
        out->emit(out->context, text[i]);
    }
}

static void emit_text(const struct output *out, const struct conversion *conv, const char *text, size_t length)
{
    int padding = conv->width > (int)length ? conv->width - (int)length : 0;

    if (!conv->left_align) {
        emit_repeated(out, ' ', padding);
    }
    emit_bytes(out, text, length);
    if (conv->left_align) {
        emit_repeated(out, ' ', padding);
    }
}

/*
 * Prints `prefix` (a sign or "0x") followed by the digits of `value`.
 * Zero padding goes between prefix and digits ("-0042"), space padding goes
 * outside ("  -42").
 */
static void emit_number(const struct output *out, const struct conversion *conv, uint64_t value,
                        unsigned base, bool uppercase, const char *prefix)
{
    /* Digits are produced least significant first, so fill from the end.
     * 20 digits is enough for 2^64 in base 10. */
    char digits[20];
    size_t start = sizeof(digits);
    const char *alphabet = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";

    do {
        digits[--start] = alphabet[value % base];
        value /= base;
    } while (value != 0);

    size_t digit_count = sizeof(digits) - start;
    size_t prefix_length = strlen(prefix);
    int content = (int)(prefix_length + digit_count);
    int padding = conv->width > content ? conv->width - content : 0;
    bool zero_pad = conv->zero_pad && !conv->left_align;

    if (!conv->left_align && !zero_pad) {
        emit_repeated(out, ' ', padding);
    }
    emit_bytes(out, prefix, prefix_length);
    if (zero_pad) {
        emit_repeated(out, '0', padding);
    }
    emit_bytes(out, &digits[start], digit_count);
    if (conv->left_align) {
        emit_repeated(out, ' ', padding);
    }
}

/* Arguments narrower than int are promoted to int by the caller, so int is
 * the smallest type va_arg may be asked for. */
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

static void emit_signed(const struct output *out, const struct conversion *conv, int64_t value)
{
    if (value < 0) {
        /* Negate in unsigned arithmetic: -INT64_MIN does not fit in int64_t. */
        emit_number(out, conv, 0 - (uint64_t)value, 10, false, "-");
    } else {
        emit_number(out, conv, (uint64_t)value, 10, false, conv->show_plus ? "+" : "");
    }
}

static void emit_pointer(const struct output *out, const struct conversion *conv, uintptr_t value)
{
    /* Always full width, so addresses line up in dumps. */
    struct conversion pointer = *conv;
    pointer.zero_pad = true;
    pointer.left_align = false;
    pointer.width = 2 + 2 * (int)sizeof(uintptr_t);
    emit_number(out, &pointer, value, 16, false, "0x");
}

/* Parses "[flags][width][length]" and returns the position after it. */
static const char *parse_conversion(const char *cursor, struct conversion *conv)
{
    *conv = (struct conversion){ 0 };

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
    while (*cursor >= '0' && *cursor <= '9') {
        conv->width = conv->width * 10 + (*cursor - '0');
        cursor++;
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

static void emit_conversion(const struct output *out, const struct conversion *conv, char specifier,
                            va_list *arguments)
{
    switch (specifier) {
    case 'c': {
        char character = (char)va_arg(*arguments, int);
        emit_text(out, conv, &character, 1);
        break;
    }
    case 's': {
        const char *text = va_arg(*arguments, const char *);
        if (text == NULL) {
            text = "(null)";
        }
        emit_text(out, conv, text, strlen(text));
        break;
    }
    case 'd':
    case 'i':
        emit_signed(out, conv, next_signed(arguments, conv->length));
        break;
    case 'u':
        emit_number(out, conv, next_unsigned(arguments, conv->length), 10, false, "");
        break;
    case 'x':
        emit_number(out, conv, next_unsigned(arguments, conv->length), 16, false, "");
        break;
    case 'X':
        emit_number(out, conv, next_unsigned(arguments, conv->length), 16, true, "");
        break;
    case 'p':
        emit_pointer(out, conv, (uintptr_t)va_arg(*arguments, void *));
        break;
    case '%':
        out->emit(out->context, '%');
        break;
    default:
        out->emit(out->context, '%');
        out->emit(out->context, specifier);
        break;
    }
}

void kvformat(format_emit_fn emit, void *context, const char *format, va_list arguments)
{
    struct output out = { .emit = emit, .context = context };

    /* A va_list may be an array type, which cannot be passed by address
     * portably; a copy can. */
    va_list remaining;
    va_copy(remaining, arguments);

    for (const char *cursor = format; *cursor != '\0'; cursor++) {
        if (*cursor != '%') {
            emit(context, *cursor);
            continue;
        }
        struct conversion conv;
        cursor = parse_conversion(cursor + 1, &conv);
        if (*cursor == '\0') {
            /* Format ends in the middle of a conversion. */
            emit(context, '%');
            break;
        }
        emit_conversion(&out, &conv, *cursor, &remaining);
    }

    va_end(remaining);
}

struct bounded_buffer {
    char *data;
    size_t size;
    size_t length; /* characters produced, including those that did not fit */
};

static void bounded_buffer_emit(void *context, char character)
{
    struct bounded_buffer *buffer = context;
    /* Keep one byte free for the terminating NUL. */
    if (buffer->length + 1 < buffer->size) {
        buffer->data[buffer->length] = character;
    }
    buffer->length++;
}

int kvsnprintf(char *buffer, size_t size, const char *format, va_list arguments)
{
    struct bounded_buffer bounded = { .data = buffer, .size = size };
    kvformat(bounded_buffer_emit, &bounded, format, arguments);

    if (size > 0) {
        size_t end = bounded.length < size ? bounded.length : size - 1;
        buffer[end] = '\0';
    }
    return (int)bounded.length;
}

int ksnprintf(char *buffer, size_t size, const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    int length = kvsnprintf(buffer, size, format, arguments);
    va_end(arguments);
    return length;
}
