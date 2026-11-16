#!/usr/bin/env python3
"""Rasterizes a TrueType font at one pixel size into a glyph atlas.

usage: mkfont.py <font.ttf> <pixel-size> <output.sdf>

The OS has no font rasterizer. Instead, every glyph is drawn here, at build
time, into an 8-bit coverage bitmap (0 = empty, 255 = fully covered); at run
time text is drawn by blending those bitmaps. The output file layout is
documented in user/libgui/src/font_format.h, which must be kept in step.
"""
import struct
import sys

from PIL import ImageFont

# Printable ASCII, Latin-1, and a few typographic characters, including the
# modifier letters used in Uzbek Latin orthography (o' and g').
CODEPOINTS = (
    list(range(0x20, 0x7F))
    + list(range(0xA0, 0x100))
    + [0x02BB, 0x02BC, 0x2013, 0x2014, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2026, 0x2190, 0x2191, 0x2192,
       0x2193, 0x2318, 0x2713]
)

MAGIC = b"SDFN"
VERSION = 1
ATLAS_WIDTH = 256
PADDING = 1  # empty pixels between glyphs, so neighbours never bleed


def rasterize(font, codepoint):
    """Returns (width, height, bearing_x, bearing_y, advance_64ths, pixels)."""
    character = chr(codepoint)
    advance = round(font.getlength(character) * 64)
    mask, (offset_x, offset_y) = font.getmask2(character, mode="L")
    width, height = mask.size
    ascent, _ = font.getmetrics()
    pixels = bytes(mask) if width and height else b""
    # offset_y is measured down from the ascender line; store the distance
    # from the baseline up to the top of the bitmap instead.
    return width, height, offset_x, ascent - offset_y, advance, pixels


def main():
    font_path, size_text, output_path = sys.argv[1:]
    size = int(size_text)
    font = ImageFont.truetype(font_path, size, layout_engine=ImageFont.Layout.BASIC)
    ascent, descent = font.getmetrics()

    # Simple shelf packing: fill rows left to right, start a new row when
    # the current one is full.
    glyphs = []
    rows = []  # (y, height, [(x, width, pixels)])
    cursor_x, cursor_y, row_height = 0, 0, 0
    placements = []
    for codepoint in CODEPOINTS:
        width, height, bearing_x, bearing_y, advance, pixels = rasterize(font, codepoint)
        if width > 255 or height > 255:
            sys.exit(f"mkfont: glyph U+{codepoint:04X} is too large at size {size}")
        if cursor_x + width + PADDING > ATLAS_WIDTH:
            cursor_x = 0
            cursor_y += row_height + PADDING
            row_height = 0
        placements.append((cursor_x, cursor_y, width, height, pixels))
        glyphs.append((codepoint, cursor_x, cursor_y, width, height, bearing_x, bearing_y, advance))
        cursor_x += width + PADDING
        row_height = max(row_height, height)
    atlas_height = cursor_y + row_height

    atlas = bytearray(ATLAS_WIDTH * atlas_height)
    for x, y, width, height, pixels in placements:
        for row in range(height):
            start = (y + row) * ATLAS_WIDTH + x
            atlas[start:start + width] = pixels[row * width:(row + 1) * width]

    with open(output_path, "wb") as output:
        output.write(struct.pack("<4sHHhhHHII", MAGIC, VERSION, size, ascent, descent, ascent + descent,
                                 len(glyphs), ATLAS_WIDTH, atlas_height))
        for codepoint, x, y, width, height, bearing_x, bearing_y, advance in sorted(glyphs):
            output.write(struct.pack("<IHHBBbbH", codepoint, x, y, width, height, bearing_x, bearing_y, advance))
        output.write(atlas)


if __name__ == "__main__":
    main()
