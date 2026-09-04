/* Real on-console PS5 graphics via direct sceVideoOut framebuffer
 * scanout — no SDL2 (see src/ps5/tilemap.h for why SDL2 was rejected).
 *
 * The whole init/flip/quit call sequence below (sceVideoOutOpen,
 * sceKernelAllocateMainDirectMemory, sceKernelMapDirectMemory,
 * sceKernelCreateEqueue, sceVideoOutAddFlipEvent, sceVideoOutSetFlipRate,
 * sceVideoOutSetBufferAttribute2, sceVideoOutRegisterBuffers2,
 * sceVideoOutSubmitFlip, sceKernelWaitEqueue, and their matching
 * Close/Release/Delete calls) is copied from ps5-payload-dev/SDL's real,
 * working PS5 video backend (src/video/ps5/SDL_ps5video.c, zlib licensed
 * — see third_party/THIRD_PARTY_LICENSES.md), including the exact
 * argument values that source uses (buffer attribute magic constant
 * 0x8000000022000000UL, 0x4000000-byte/2-buffer allocation, 1920x1080
 * ABGR8888) — not guessed or re-derived. This project only replaces
 * SDL's window/surface machinery with a single fixed-size CPU-side pixel
 * buffer that the caller draws into directly and presents with
 * video_present().
 *
 * Compiled for PS5 and NOT yet run on real hardware — see docs/testing.md.
 */
#ifndef ROMM_PS5_VIDEO_H
#define ROMM_PS5_VIDEO_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VIDEO_WIDTH 1920
#define VIDEO_HEIGHT 1080

typedef struct Video Video;

/* Opens the video output, allocates and maps the two scanout buffers, and
 * allocates a CPU-side linear pixel buffer the caller draws into. Returns
 * NULL on failure (logs via log_error). */
Video *video_init(void);

/* Closes/releases/frees everything video_init() set up. Safe to call with
 * NULL. */
void video_shutdown(Video *video);

/* The CPU-side linear ABGR8888 pixel buffer, VIDEO_WIDTH x VIDEO_HEIGHT,
 * pitch = VIDEO_WIDTH (no row padding). The caller draws into this
 * directly between frames; video_present() converts and flips it. */
uint32_t *video_pixels(Video *video);

/* Tile-swizzles the current pixel buffer into the next scanout buffer,
 * submits the flip, and blocks until it completes (via the flip event
 * queue) — matches the verified SDL reference's own synchronous
 * present, so there is no separate vsync/pacing call. Returns false on
 * failure (logs via log_error); the caller may keep running with a
 * frozen display rather than treating this as fatal. */
bool video_present(Video *video);

#ifdef __cplusplus
}
#endif

#endif /* ROMM_PS5_VIDEO_H */
