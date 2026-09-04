#include "ps5/tilemap.h"

#include <stdlib.h>

/* --- Tiling constants and address math: copied from
 * ps5-payload-dev/SDL's src/video/ps5/SDL_ps5tilemap.c, renamed to plain
 * C types. Do not "simplify" the bit-interleaving in tile_offset() — it
 * encodes the PS5 GPU's real tile-swizzle layout and this project has no
 * way to re-derive or verify it independently. What IS simplified
 * relative to that source: no damage-rect tracking, no AVX2 SIMD batching
 * — this project only redraws whole, simple, low-frequency UI screens, so
 * a plain per-pixel loop was judged a better risk/complexity trade than
 * porting the SIMD path. --- */

#define TILE_WIDTH 512
#define TILE_HEIGHT 128
#define TILE_SIZE (TILE_WIDTH * TILE_HEIGHT)

struct Tilemap {
    uint32_t width;
    uint32_t height;
};

static uint32_t tile_offset(uint32_t x, uint32_t y) {
    return (((x) & 1u) << 0 | ((x >> 1) & 1u) << 1 | ((y >> 0) & 1u) << 2 |
            ((y >> 1) & 1u) << 3 | ((y >> 2) & 1u) << 4 |
            ((x >> 2) & 1u) << 5 | (((x >> 3) ^ (y >> 3)) & 1u) << 6 |
            (((x >> 4) ^ (y >> 4)) & 1u) << 7 |
            (((x >> 6) ^ (y >> 5)) & 1u) << 8 |
            (((x >> 5) ^ (y >> 6)) & 1u) << 9 | ((y >> 3) & 1u) << 10 |
            ((x >> 4) & 1u) << 11 | ((y >> 6) & 1u) << 12 |
            ((x >> 6) & 1u) << 13 | ((x >> 7) & 1u) << 14 |
            ((x >> 8) & 1u) << 15);
}

static uint32_t tile_pixel(uint32_t x, uint32_t y, uint32_t width) {
    return (x / TILE_WIDTH) * TILE_SIZE + (y / TILE_HEIGHT) * (TILE_HEIGHT * width) +
           (tile_offset(x % TILE_WIDTH, 0) ^ tile_offset(0, y % TILE_HEIGHT));
}

size_t tilemap_buffer_size(uint32_t width, uint32_t height) {
    uint32_t last_band = ((height - 1) / TILE_HEIGHT) * (TILE_HEIGHT * width);
    uint32_t last_tile = ((width - 1) / TILE_WIDTH) * TILE_SIZE;
    return (size_t)(last_band + last_tile + TILE_SIZE) * sizeof(uint32_t);
}

Tilemap *tilemap_create(uint32_t width, uint32_t height) {
    Tilemap *tmap = malloc(sizeof(Tilemap));
    if (tmap == NULL) {
        return NULL;
    }
    tmap->width = width;
    tmap->height = height;
    return tmap;
}

void tilemap_destroy(Tilemap *tmap) {
    free(tmap);
}

void tilemap_blit_full(const Tilemap *tmap, const uint32_t *pixels,
                        uint32_t *tiled) {
    for (uint32_t y = 0; y < tmap->height; y++) {
        const uint32_t *row = pixels + (size_t)y * tmap->width;
        for (uint32_t x = 0; x < tmap->width; x++) {
            tiled[tile_pixel(x, y, tmap->width)] = row[x];
        }
    }
}
