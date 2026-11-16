#ifndef GUI_FONT_FORMAT_H
#define GUI_FONT_FORMAT_H

#include <stdint.h>

/*
 * Layout of a font atlas file, as written by tools/mkfont.py:
 *
 *   struct font_file_header
 *   struct font_file_glyph[glyph_count]   sorted by codepoint
 *   uint8_t atlas[atlas_width * atlas_height]
 *
 * The atlas is one image holding every glyph; each byte says how much of
 * that pixel the glyph covers (0 = none, 255 = all). All numbers are
 * little-endian.
 */
#define FONT_FILE_MAGIC "SDFN"
#define FONT_FILE_VERSION 1

struct font_file_header {
    char magic[4];
    uint16_t version;
    uint16_t pixel_size;
    int16_t ascent;      /* baseline to top of tall letters */
    int16_t descent;     /* baseline to bottom of descenders, positive */
    uint16_t line_height;
    uint16_t glyph_count;
    uint32_t atlas_width;
    uint32_t atlas_height;
} __attribute__((packed));

struct font_file_glyph {
    uint32_t codepoint;
    uint16_t atlas_x, atlas_y; /* top-left corner of the bitmap in the atlas */
    uint8_t width, height;     /* size of the bitmap */
    int8_t bearing_x;          /* pen position to left edge of the bitmap */
    int8_t bearing_y;          /* baseline up to top edge of the bitmap */
    uint16_t advance;          /* how far the pen moves, in 1/64 pixel */
} __attribute__((packed));

#endif
