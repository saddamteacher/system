#include <gui/gfx.h>

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "font_format.h"
#include <gui/pixel.h>

struct gfx_font {
    struct font_file_header header;
    const struct font_file_glyph *glyphs; /* into `data` */
    const uint8_t *atlas;                 /* into `data` */
    uint8_t *data;                        /* the whole file */
};

#define REPLACEMENT_CODEPOINT '?'
/* Pen positions are kept in 1/64 pixel, like glyph advances, so rounding
 * errors do not add up along a line. */
#define SUBPIXELS 64

static bool read_fully(int descriptor, void *buffer, size_t size)
{
    size_t done = 0;
    while (done < size) {
        long count = read(descriptor, (char *)buffer + done, size - done);
        if (count <= 0) {
            return false;
        }
        done += (size_t)count;
    }
    return true;
}

static bool font_is_valid(const struct font_file_header *header, size_t file_size)
{
    if (memcmp(header->magic, FONT_FILE_MAGIC, 4) != 0 || header->version != FONT_FILE_VERSION) {
        return false;
    }
    size_t expected = sizeof(*header) + (size_t)header->glyph_count * sizeof(struct font_file_glyph)
                    + (size_t)header->atlas_width * header->atlas_height;
    return file_size == expected;
}

struct gfx_font *gfx_font_load(const char *path)
{
    struct sd_stat info;
    if (stat(path, &info) < 0 || info.size < sizeof(struct font_file_header)) {
        return NULL;
    }
    int descriptor = open(path);
    if (descriptor < 0) {
        return NULL;
    }
    struct gfx_font *font = malloc(sizeof(*font));
    uint8_t *data = malloc(info.size);
    bool ok = font != NULL && data != NULL && read_fully(descriptor, data, info.size);
    close(descriptor);

    if (ok) {
        memcpy(&font->header, data, sizeof(font->header));
        ok = font_is_valid(&font->header, info.size);
    }
    if (!ok) {
        free(font);
        free(data);
        return NULL;
    }
    font->data = data;
    font->glyphs = (const struct font_file_glyph *)(data + sizeof(font->header));
    font->atlas = data + sizeof(font->header) + (size_t)font->header.glyph_count * sizeof(struct font_file_glyph);
    return font;
}

void gfx_font_destroy(struct gfx_font *font)
{
    if (font != NULL) {
        free(font->data);
        free(font);
    }
}

int gfx_font_ascent(const struct gfx_font *font)
{
    return font->header.ascent;
}

int gfx_font_descent(const struct gfx_font *font)
{
    return font->header.descent;
}

int gfx_font_line_height(const struct gfx_font *font)
{
    return font->header.line_height;
}

/* The glyph table is sorted, so a binary search finds a codepoint. */
static const struct font_file_glyph *find_glyph(const struct gfx_font *font, uint32_t codepoint)
{
    size_t low = 0, high = font->header.glyph_count;
    while (low < high) {
        size_t middle = low + (high - low) / 2;
        if (font->glyphs[middle].codepoint < codepoint) {
            low = middle + 1;
        } else {
            high = middle;
        }
    }
    if (low < font->header.glyph_count && font->glyphs[low].codepoint == codepoint) {
        return &font->glyphs[low];
    }
    return NULL;
}

static const struct font_file_glyph *glyph_for(const struct gfx_font *font, uint32_t codepoint)
{
    const struct font_file_glyph *glyph = find_glyph(font, codepoint);
    return glyph != NULL ? glyph : find_glyph(font, REPLACEMENT_CODEPOINT);
}

/*
 * Decodes one UTF-8 character starting at *cursor (which has `remaining`
 * bytes left) and advances the cursor. Malformed input yields the
 * replacement character and skips one byte, so decoding always makes
 * progress.
 */
static uint32_t next_codepoint(const char **cursor, size_t remaining)
{
    const unsigned char *bytes = (const unsigned char *)*cursor;
    unsigned char first = bytes[0];
    size_t length;
    uint32_t codepoint;

    if (first < 0x80) {
        *cursor += 1;
        return first;
    } else if ((first & 0xE0) == 0xC0) {
        length = 2;
        codepoint = first & 0x1F;
    } else if ((first & 0xF0) == 0xE0) {
        length = 3;
        codepoint = first & 0x0F;
    } else if ((first & 0xF8) == 0xF0) {
        length = 4;
        codepoint = first & 0x07;
    } else {
        *cursor += 1;
        return REPLACEMENT_CODEPOINT;
    }
    if (length > remaining) {
        *cursor += 1;
        return REPLACEMENT_CODEPOINT;
    }
    for (size_t i = 1; i < length; i++) {
        if ((bytes[i] & 0xC0) != 0x80) { /* not a continuation byte */
            *cursor += 1;
            return REPLACEMENT_CODEPOINT;
        }
        codepoint = (codepoint << 6) | (bytes[i] & 0x3F);
    }
    *cursor += length;
    return codepoint;
}

static size_t text_length(const char *text, int length)
{
    return length < 0 ? strlen(text) : (size_t)length;
}

int gfx_text_width(const struct gfx_font *font, const char *text, int length)
{
    const char *end = text + text_length(text, length);
    int pen = 0;
    while (text < end) {
        const struct font_file_glyph *glyph = glyph_for(font, next_codepoint(&text, (size_t)(end - text)));
        if (glyph != NULL) {
            pen += glyph->advance;
        }
    }
    return (pen + SUBPIXELS / 2) / SUBPIXELS;
}

static void draw_glyph(struct gfx_surface *surface, const struct gfx_font *font,
                       const struct font_file_glyph *glyph, int x, int baseline_y, gfx_color color)
{
    struct gfx_rect target = gfx_rect_make(x + glyph->bearing_x, baseline_y - glyph->bearing_y, glyph->width,
                                           glyph->height);
    struct gfx_rect visible = gfx_rect_intersect(target, surface->clip);

    for (int y = visible.y; y < visible.y + visible.height; y++) {
        const uint8_t *coverage = font->atlas + (size_t)(glyph->atlas_y + (y - target.y)) * font->header.atlas_width
                                + glyph->atlas_x + (visible.x - target.x);
        uint32_t *row = surface->pixels + (size_t)y * (size_t)surface->stride + visible.x;
        for (int i = 0; i < visible.width; i++) {
            if (coverage[i] != 0) {
                row[i] = blend_over(row[i], premultiply(color, coverage[i]));
            }
        }
    }
}

int gfx_draw_text(struct gfx_surface *surface, const struct gfx_font *font, int x, int baseline_y,
                  const char *text, int length, gfx_color color)
{
    const char *end = text + text_length(text, length);
    int pen = x * SUBPIXELS;

    while (text < end) {
        const struct font_file_glyph *glyph = glyph_for(font, next_codepoint(&text, (size_t)(end - text)));
        if (glyph == NULL) {
            continue;
        }
        /* Glyph bitmaps were rasterized on whole pixels, so each is placed
         * at the nearest whole pixel to the exact pen position. */
        draw_glyph(surface, font, glyph, (pen + SUBPIXELS / 2) / SUBPIXELS, baseline_y, color);
        pen += glyph->advance;
    }
    return (pen + SUBPIXELS / 2) / SUBPIXELS;
}
