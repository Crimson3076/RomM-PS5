#include "ps5/font_render.h"

#include <string.h>

/* Defined in third_party/font8x8/font8x8_data.c — see that file for why
 * this isn't just `#include "font8x8_basic.h"` here directly. */
extern char font8x8_basic[128][8];

static void draw_glyph(uint32_t *pixels, uint32_t pixel_width, int x, int y,
                        unsigned char ch, uint32_t color, int scale) {
    if (ch >= 128) {
        return;
    }
    const char *rows = font8x8_basic[ch];

    for (int row = 0; row < FONT_GLYPH_SIZE; row++) {
        unsigned char bits = (unsigned char)rows[row];
        for (int col = 0; col < FONT_GLYPH_SIZE; col++) {
            if (((bits >> col) & 1) == 0) {
                continue;
            }
            for (int sy = 0; sy < scale; sy++) {
                int py = y + row * scale + sy;
                if (py < 0) {
                    continue;
                }
                for (int sx = 0; sx < scale; sx++) {
                    int px = x + col * scale + sx;
                    if (px < 0 || (uint32_t)px >= pixel_width) {
                        continue;
                    }
                    pixels[(size_t)py * pixel_width + (size_t)px] = color;
                }
            }
        }
    }
}

void font_draw_text(uint32_t *pixels, uint32_t pixel_width, int x, int y,
                     const char *text, uint32_t color, int scale) {
    if (scale < 1) {
        scale = 1;
    }
    int cursor_x = x;
    for (const char *p = text; *p != '\0'; p++) {
        unsigned char ch = (unsigned char)*p;
        if (ch >= 0x20 && ch <= 0x7E) {
            draw_glyph(pixels, pixel_width, cursor_x, y, ch, color, scale);
        }
        cursor_x += FONT_GLYPH_SIZE * scale;
    }
}

void font_measure_text(const char *text, int scale, int *width_out,
                        int *height_out) {
    if (scale < 1) {
        scale = 1;
    }
    if (width_out != NULL) {
        *width_out = (int)strlen(text) * FONT_GLYPH_SIZE * scale;
    }
    if (height_out != NULL) {
        *height_out = FONT_GLYPH_SIZE * scale;
    }
}
