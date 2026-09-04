/* Minimal 8x8 bitmap-font text rendering onto a linear ABGR8888 pixel
 * buffer (see src/ps5/video.h). Uses the vendored public-domain font8x8
 * basic Latin table (third_party/font8x8) rather than a hand-transcribed
 * glyph set, so every character this project draws is byte-verified
 * against a real, published font instead of guessed pixel-by-pixel. Text
 * only, no anti-aliasing or kerning — this MVP's UI is explicitly allowed
 * to be visually basic (see docs/testing.md handoff notes).
 */
#ifndef ROMM_PS5_FONT_RENDER_H
#define ROMM_PS5_FONT_RENDER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FONT_GLYPH_SIZE 8

/* Draws `text` at (x, y) into `pixels` (a `pixel_width` x anything linear
 * ABGR8888 buffer, pitch = pixel_width) using `scale`x scaled 8x8 glyphs
 * (scale=1 draws each glyph at its native 8x8 size; scale=2 draws 16x16,
 * etc). `color` is ABGR8888 (0xAABBGGRR — matches SDL_PIXELFORMAT_ABGR8888
 * byte order, consistent with video.h's buffer format). Characters
 * outside the printable ASCII range (0x20-0x7E) are skipped (advance the
 * cursor but draw nothing) rather than guessing a glyph. Does not clip to
 * the buffer's height — callers must keep y within bounds themselves;
 * horizontal drawing past `pixel_width` is clipped per-glyph. */
void font_draw_text(uint32_t *pixels, uint32_t pixel_width, int x, int y,
                     const char *text, uint32_t color, int scale);

/* Pixel width/height a string would occupy at the given scale, for laying
 * out UI without actually drawing (e.g. right-aligning a byte count). */
void font_measure_text(const char *text, int scale, int *width_out,
                        int *height_out);

#ifdef __cplusplus
}
#endif

#endif /* ROMM_PS5_FONT_RENDER_H */
