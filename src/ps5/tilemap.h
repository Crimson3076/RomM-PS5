/* PS5 GPU scanout buffers need pixels in a swizzled/tiled memory layout,
 * not plain row-major order. This is a from-scratch reimplementation of
 * that address math, adapted (SDL types replaced with plain C) from
 * ps5-payload-dev/SDL's src/video/ps5/SDL_ps5tilemap.c (zlib licensed) —
 * see third_party/THIRD_PARTY_LICENSES.md. The tiling algorithm itself
 * (tile_offset) is unchanged from that source: it encodes a real hardware
 * requirement this project has no way to independently verify without a
 * console, so reusing a maintained implementation was judged safer than
 * re-deriving the bit-interleaving pattern from scratch.
 */
#ifndef ROMM_PS5_TILEMAP_H
#define ROMM_PS5_TILEMAP_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Tilemap Tilemap;

/* Bytes needed for a tiled buffer holding a width x height ABGR8888 image. */
size_t tilemap_buffer_size(uint32_t width, uint32_t height);

Tilemap *tilemap_create(uint32_t width, uint32_t height);
void tilemap_destroy(Tilemap *tmap);

/* Converts the full `width x height` linear ABGR8888 buffer at `pixels`
 * (pitch = width, i.e. no row padding) into the tiled layout at `tiled`.
 * Always does a full-buffer conversion (no damage-rect tracking) — this
 * project redraws whole, simple screens rather than partial updates, so
 * the extra simplicity was judged worth more than the copied reference's
 * partial-redraw optimization. */
void tilemap_blit_full(const Tilemap *tmap, const uint32_t *pixels,
                        uint32_t *tiled);

#ifdef __cplusplus
}
#endif

#endif /* ROMM_PS5_TILEMAP_H */
