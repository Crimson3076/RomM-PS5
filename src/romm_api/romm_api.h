/* RomM API interface — kept separate from any transport or storage code so
 * the UI never talks to a concrete backend directly. Milestone 1 ships only
 * the mock implementation (romm_api_mock.c); a real HTTP-backed
 * implementation is deferred until the network/TLS spike in
 * docs/architecture.md is resolved. */
#ifndef ROMM_PS5_ROMM_API_H
#define ROMM_PS5_ROMM_API_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ROMM_TITLE_MAX 128
#define ROMM_TITLE_ID_MAX 16
#define ROMM_VERSION_MAX 32

typedef enum {
    ROMM_FORMAT_UNKNOWN = 0,
    ROMM_FORMAT_FOLDER,   /* extracted PS5 folder dump (sce_sys/param.json present) */
    ROMM_FORMAT_FFPKG,
    ROMM_FORMAT_EXFAT,
    ROMM_FORMAT_FFPFS,
    ROMM_FORMAT_FFPFSC,
} RommGameFormat;

typedef struct {
    int32_t id;
    char title[ROMM_TITLE_MAX];
    char fs_name[ROMM_TITLE_MAX]; /* RomM's on-disk name for this ROM — used
                                    * to build the /content/{file_name}
                                    * download URL; may differ from title */
    char title_id[ROMM_TITLE_ID_MAX]; /* empty string if unknown */
    char version[ROMM_VERSION_MAX];   /* empty string if unknown */
    RommGameFormat format;
    uint64_t fs_size_bytes; /* on-disk content size; NOT a byte-exact transfer
                              * size for folder games — see docs/architecture.md */
    bool has_cover_art;
} RommGame;

typedef struct {
    RommGame *items;
    size_t count;
    size_t total;    /* total matches on the server, for pagination */
    size_t offset;
} RommGamePage;

typedef enum {
    ROMM_OK = 0,
    ROMM_ERR_NETWORK,
    ROMM_ERR_AUTH,
    ROMM_ERR_NOT_FOUND,
    ROMM_ERR_UNSUPPORTED, /* not implemented by this backend yet */
} RommResult;

typedef enum {
    ROMM_SORT_TITLE_ASC = 0,
    ROMM_SORT_TITLE_DESC,
} RommSortOrder;

/* Abstract interface. Every backend (mock, HTTP) fills this in and the UI
 * layer only ever holds a `const RommApi *`. */
typedef struct RommApi {
    void *ctx;

    RommResult (*test_connection)(void *ctx);

    /* Fetch one page of the PS5 library. `search_term` may be NULL/empty.
     * `page_out` must be released with `list_ps5_games_free`. */
    RommResult (*list_ps5_games)(void *ctx, const char *search_term,
                                  RommSortOrder sort, size_t offset,
                                  size_t limit, RommGamePage *page_out);

    void (*list_ps5_games_free)(RommGamePage *page);
} RommApi;

#ifdef __cplusplus
}
#endif

#endif /* ROMM_PS5_ROMM_API_H */
