#include "ps5/video.h"
#include "ps5/tilemap.h"
#include "log/log.h"

#include <errno.h>
#include <inttypes.h>
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
#define VIDEO_MEM_ALIGN 0x20000
#define VIDEO_BUFFER_COUNT 2

struct Video {
    int handle;
    VideoBuf vbuf[2];
    struct kevent *evt_queue;
    intptr_t paddr;
    size_t buffer_stride;
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

    int rc = sceSystemServiceHideSplashScreen();
    log_info("sceSystemServiceHideSplashScreen returned 0x%08x (%d)",
             (unsigned int)rc, rc);

    video->handle = sceVideoOutOpen(0xff, 0, 0, NULL);
    if (video->handle < 0) {
        int saved_errno = errno;
        log_error("sceVideoOutOpen failed: rc=0x%08x (%d), errno=%d (%s)",
                  (unsigned int)video->handle, video->handle, saved_errno,
                  strerror(saved_errno));
        free(video);
        return NULL;
    }
    log_info("sceVideoOutOpen returned handle=%d", video->handle);

    if (!tilemap_buffer_layout(VIDEO_WIDTH, VIDEO_HEIGHT, VIDEO_MEM_ALIGN,
                               VIDEO_BUFFER_COUNT, &video->buffer_stride,
                               &video->memsize)) {
        log_error("video_init: invalid framebuffer allocation layout for %dx%d",
                  VIDEO_WIDTH, VIDEO_HEIGHT);
        sceVideoOutClose(video->handle);
        free(video);
        return NULL;
    }
    log_info("Video direct-memory request: %zu bytes total, %zu bytes per "
             "buffer, alignment=%u, buffers=%d",
             video->memsize, video->buffer_stride,
             (unsigned int)VIDEO_MEM_ALIGN, VIDEO_BUFFER_COUNT);

    errno = 0;
    rc = sceKernelAllocateMainDirectMemory(video->memsize, VIDEO_MEM_ALIGN, 3,
                                            &video->paddr);
    if (rc != 0) {
        int saved_errno = errno;
        log_error("sceKernelAllocateMainDirectMemory failed: rc=0x%08x (%d), "
                  "errno=%d (%s), size=%zu, alignment=%u, type=3",
                  (unsigned int)rc, rc, saved_errno, strerror(saved_errno),
                  video->memsize, (unsigned int)VIDEO_MEM_ALIGN);
        sceVideoOutClose(video->handle);
        free(video);
        return NULL;
    }
    log_info("sceKernelAllocateMainDirectMemory succeeded: paddr=0x%" PRIxPTR,
             (uintptr_t)video->paddr);

    void *vaddr = NULL;
    errno = 0;
    rc = sceKernelMapDirectMemory(&vaddr, video->memsize, 0x33, 0,
                                   video->paddr, VIDEO_MEM_ALIGN);
    if (rc != 0) {
        int saved_errno = errno;
        log_error("sceKernelMapDirectMemory failed: rc=0x%08x (%d), errno=%d "
                  "(%s), size=%zu, alignment=%u",
                  (unsigned int)rc, rc, saved_errno, strerror(saved_errno),
                  video->memsize, (unsigned int)VIDEO_MEM_ALIGN);
        sceKernelReleaseDirectMemory(video->paddr, video->memsize);
        sceVideoOutClose(video->handle);
        free(video);
        return NULL;
    }
    log_info("sceKernelMapDirectMemory succeeded: vaddr=%p", vaddr);

    video->vbuf[0].data = vaddr;
    video->vbuf[1].data = (uint8_t *)vaddr + video->buffer_stride;

    errno = 0;
    rc = sceKernelCreateEqueue(&video->evt_queue, "rommps5 flip queue");
    if (rc != 0) {
        int saved_errno = errno;
        log_error("sceKernelCreateEqueue failed: rc=0x%08x (%d), errno=%d "
                  "(%s)",
                  (unsigned int)rc, rc, saved_errno, strerror(saved_errno));
        sceKernelReleaseDirectMemory(video->paddr, video->memsize);
        sceVideoOutClose(video->handle);
        free(video);
        return NULL;
    }
    log_info("sceKernelCreateEqueue succeeded");

    errno = 0;
    rc = sceVideoOutAddFlipEvent(video->evt_queue, video->handle, NULL);
    if (rc != 0) {
        int saved_errno = errno;
        log_error("sceVideoOutAddFlipEvent failed: rc=0x%08x (%d), errno=%d "
                  "(%s)",
                  (unsigned int)rc, rc, saved_errno, strerror(saved_errno));
        sceKernelDeleteEqueue(video->evt_queue);
        sceKernelReleaseDirectMemory(video->paddr, video->memsize);
        sceVideoOutClose(video->handle);
        free(video);
        return NULL;
    }
    log_info("sceVideoOutAddFlipEvent succeeded");

    errno = 0;
    rc = sceVideoOutSetFlipRate(video->handle, 0);
    if (rc != 0) {
        int saved_errno = errno;
        log_warn("sceVideoOutSetFlipRate failed: rc=0x%08x (%d), errno=%d "
                 "(%s)",
                 (unsigned int)rc, rc, saved_errno, strerror(saved_errno));
    } else {
        log_info("sceVideoOutSetFlipRate succeeded");
    }

    VideoAttr vattr;
    memset(&vattr, 0, sizeof(vattr));
    sceVideoOutSetBufferAttribute2(&vattr, VIDEO_BUFFER_ATTR_MAGIC, 0,
                                    VIDEO_WIDTH, VIDEO_HEIGHT, 0, 0, 0);

    errno = 0;
    rc = sceVideoOutRegisterBuffers2(video->handle, 0, 0, video->vbuf,
                                      VIDEO_BUFFER_COUNT, &vattr, 0, NULL);
    if (rc != 0) {
        int saved_errno = errno;
        log_error("sceVideoOutRegisterBuffers2 failed: rc=0x%08x (%d), "
                  "errno=%d (%s)",
                  (unsigned int)rc, rc, saved_errno, strerror(saved_errno));
        sceVideoOutDeleteFlipEvent(video->evt_queue, video->handle);
        sceKernelDeleteEqueue(video->evt_queue);
        sceKernelReleaseDirectMemory(video->paddr, video->memsize);
        sceVideoOutClose(video->handle);
        free(video);
        return NULL;
    }
    log_info("sceVideoOutRegisterBuffers2 succeeded");

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
