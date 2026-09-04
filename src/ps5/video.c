#include "ps5/video.h"
#include "ps5/tilemap.h"
#include "log/log.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h> /* u_short/u_int, needed by sys/event.h below but not
                         * pulled in by it automatically on this SDK's
                         * FreeBSD-derived headers */
#include <sys/event.h>

/* --- Verified against ps5-payload-dev/SDL's SDL_ps5video.c/.h (see
 * video.h) --- */
int sceSystemServiceHideSplashScreen(void);

int sceKernelAllocateMainDirectMemory(size_t, size_t, int, intptr_t *);
int sceKernelMapDirectMemory(void **, size_t, int, int, intptr_t, size_t);
int sceKernelReleaseDirectMemory(intptr_t, size_t);

int sceKernelCreateEqueue(struct kevent **, const char *);
int sceKernelWaitEqueue(struct kevent *, struct kevent *, int, int *,
                         unsigned int *);
int sceKernelDeleteEqueue(struct kevent *);

int sceVideoOutOpen(int, int, int, const void *);
void sceVideoOutClose(int);

int sceVideoOutAddFlipEvent(struct kevent *, int, void *);
int sceVideoOutSetFlipRate(int, int);
int sceVideoOutSubmitFlip(int, int, uint32_t, int64_t);
int sceVideoOutDeleteFlipEvent(struct kevent *, int);

/* Byte-for-byte the same layout as SDL's PS5_VideoBuf/PS5_VideoAttr —
 * opaque "junk" padding whose real field meanings are undocumented, but
 * whose SIZE (and thus struct layout for the pointer-passing ABI below)
 * is exactly what the verified reference uses. Do not resize. */
typedef struct {
    void *data;
    uint64_t junk0[3];
} VideoBuf;

typedef struct {
    uint8_t junk0[80];
} VideoAttr;

void sceVideoOutSetBufferAttribute2(VideoAttr *, uint64_t, uint32_t, uint32_t,
                                     uint32_t, uint64_t, uint32_t, uint64_t);
int sceVideoOutRegisterBuffers2(int, int, int, VideoBuf *, int, VideoAttr *,
                                 int, void *);

#define VIDEO_BUFFER_ATTR_MAGIC 0x8000000022000000UL
#define VIDEO_MEMSIZE 0x4000000
#define VIDEO_MEM_ALIGN 0x20000

struct Video {
    int handle;
    VideoBuf vbuf[2];
    struct kevent *evt_queue;
    intptr_t paddr;
    size_t memsize;
    Tilemap *tmap;
    uint32_t *pixels; /* linear CPU-side ABGR8888 draw buffer */
    uint32_t frame_id;
};

Video *video_init(void) {
    Video *video = calloc(1, sizeof(Video));
    if (video == NULL) {
        log_error("video_init: allocation failed");
        return NULL;
    }
    video->handle = -1;

    sceSystemServiceHideSplashScreen();

    video->handle = sceVideoOutOpen(0xff, 0, 0, NULL);
    if (video->handle < 0) {
        log_error("sceVideoOutOpen failed: %d", video->handle);
        free(video);
        return NULL;
    }

    video->memsize = VIDEO_MEMSIZE;
    if (sceKernelAllocateMainDirectMemory(video->memsize, VIDEO_MEM_ALIGN, 3,
                                           &video->paddr)) {
        log_error("sceKernelAllocateMainDirectMemory failed: %s",
                  strerror(errno));
        sceVideoOutClose(video->handle);
        free(video);
        return NULL;
    }

    void *vaddr = NULL;
    if (sceKernelMapDirectMemory(&vaddr, video->memsize, 0x33, 0,
                                  video->paddr, VIDEO_MEM_ALIGN)) {
        log_error("sceKernelMapDirectMemory failed: %s", strerror(errno));
        sceKernelReleaseDirectMemory(video->paddr, video->memsize);
        sceVideoOutClose(video->handle);
        free(video);
        return NULL;
    }

    video->vbuf[0].data = vaddr;
    video->vbuf[1].data = (uint8_t *)vaddr + (video->memsize / 2);

    if (sceKernelCreateEqueue(&video->evt_queue, "rommps5 flip queue")) {
        log_error("sceKernelCreateEqueue failed: %s", strerror(errno));
        sceKernelReleaseDirectMemory(video->paddr, video->memsize);
        sceVideoOutClose(video->handle);
        free(video);
        return NULL;
    }

    if (sceVideoOutAddFlipEvent(video->evt_queue, video->handle, NULL)) {
        log_error("sceVideoOutAddFlipEvent failed: %s", strerror(errno));
        sceKernelDeleteEqueue(video->evt_queue);
        sceKernelReleaseDirectMemory(video->paddr, video->memsize);
        sceVideoOutClose(video->handle);
        free(video);
        return NULL;
    }
    if (sceVideoOutSetFlipRate(video->handle, 0)) {
        log_warn("sceVideoOutSetFlipRate failed: %s", strerror(errno));
    }

    VideoAttr vattr;
    memset(&vattr, 0, sizeof(vattr));
    sceVideoOutSetBufferAttribute2(&vattr, VIDEO_BUFFER_ATTR_MAGIC, 0,
                                    VIDEO_WIDTH, VIDEO_HEIGHT, 0, 0, 0);

    if (sceVideoOutRegisterBuffers2(video->handle, 0, 0, video->vbuf, 2,
                                     &vattr, 0, NULL)) {
        log_error("sceVideoOutRegisterBuffers2 failed: %s", strerror(errno));
        sceVideoOutDeleteFlipEvent(video->evt_queue, video->handle);
        sceKernelDeleteEqueue(video->evt_queue);
        sceKernelReleaseDirectMemory(video->paddr, video->memsize);
        sceVideoOutClose(video->handle);
        free(video);
        return NULL;
    }

    video->tmap = tilemap_create(VIDEO_WIDTH, VIDEO_HEIGHT);
    video->pixels = calloc((size_t)VIDEO_WIDTH * VIDEO_HEIGHT, sizeof(uint32_t));
    if (video->tmap == NULL || video->pixels == NULL) {
        log_error("video_init: out of memory allocating draw buffer");
        video_shutdown(video);
        return NULL;
    }

    log_info("PS5 video output initialized (%dx%d, handle=%d)", VIDEO_WIDTH,
             VIDEO_HEIGHT, video->handle);
    return video;
}

void video_shutdown(Video *video) {
    if (video == NULL) {
        return;
    }
    if (video->evt_queue != NULL) {
        if (video->handle >= 0) {
            sceVideoOutDeleteFlipEvent(video->evt_queue, video->handle);
        }
        sceKernelDeleteEqueue(video->evt_queue);
    }
    if (video->paddr != 0) {
        sceKernelReleaseDirectMemory(video->paddr, video->memsize);
    }
    if (video->handle >= 0) {
        sceVideoOutClose(video->handle);
    }
    tilemap_destroy(video->tmap);
    free(video->pixels);
    free(video);
}

uint32_t *video_pixels(Video *video) {
    return video->pixels;
}

bool video_present(Video *video) {
    uint8_t idx = (uint8_t)(video->frame_id % 2);

    tilemap_blit_full(video->tmap, video->pixels, video->vbuf[idx].data);

    if (sceVideoOutSubmitFlip(video->handle, idx, 1, (int64_t)video->frame_id)) {
        log_error("sceVideoOutSubmitFlip failed: %s", strerror(errno));
        return false;
    }

    struct kevent evt;
    int triggered = 0;
    if (sceKernelWaitEqueue(video->evt_queue, &evt, 1, &triggered, NULL)) {
        log_error("sceKernelWaitEqueue failed: %s", strerror(errno));
        return false;
    }

    video->frame_id++;
    return true;
}
