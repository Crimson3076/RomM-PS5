#include "ps5/screens.h"
#include "ps5/font_render.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/* ABGR8888 (matches video.h's buffer format): bits 31-24 alpha, 23-16
 * blue, 15-8 green, 7-0 red. */
#define COLOR_ARGB(a, r, g, b)                                               \
    (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(g) << 8) |  \
     (uint32_t)(r))

#define COLOR_BG COLOR_ARGB(255, 18, 18, 24)
#define COLOR_TEXT COLOR_ARGB(255, 230, 230, 235)
#define COLOR_TEXT_DIM COLOR_ARGB(255, 150, 150, 160)
#define COLOR_FOCUS_BG COLOR_ARGB(255, 60, 110, 220)
#define COLOR_ROW_BG COLOR_ARGB(255, 40, 40, 52)
#define COLOR_ERROR COLOR_ARGB(255, 230, 80, 80)
#define COLOR_OK COLOR_ARGB(255, 90, 200, 120)
#define COLOR_BAR_BG COLOR_ARGB(255, 40, 40, 52)
#define COLOR_BAR_FILL COLOR_ARGB(255, 90, 170, 240)

#define MARGIN_X 48
#define MARGIN_TOP 40
#define ROW_HEIGHT 44
#define ROW_GAP 6
#define TEXT_SCALE 2

static void clear(uint32_t *pixels, uint32_t color) {
    for (size_t i = 0; i < (size_t)VIDEO_WIDTH * VIDEO_HEIGHT; i++) {
        pixels[i] = color;
    }
}

static void fill_rect(uint32_t *pixels, int x, int y, int w, int h,
                       uint32_t color) {
    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (w <= 0 || h <= 0) {
        return;
    }
    int max_x = x + w;
    int max_y = y + h;
    if (max_x > VIDEO_WIDTH) {
        max_x = VIDEO_WIDTH;
    }
    if (max_y > VIDEO_HEIGHT) {
        max_y = VIDEO_HEIGHT;
    }
    for (int py = y; py < max_y; py++) {
        for (int px = x; px < max_x; px++) {
            pixels[(size_t)py * VIDEO_WIDTH + (size_t)px] = color;
        }
    }
}

static const char *format_label(RommGameFormat format) {
    switch (format) {
    case ROMM_FORMAT_FOLDER:
        return "folder";
    case ROMM_FORMAT_FFPKG:
        return "ffpkg";
    case ROMM_FORMAT_EXFAT:
        return "exfat";
    case ROMM_FORMAT_FFPFS:
        return "ffpfs";
    case ROMM_FORMAT_FFPFSC:
        return "ffpfsc";
    default:
        return "unknown";
    }
}

void screen_draw_message(Video *video, const char *title, const char *body) {
    uint32_t *pixels = video_pixels(video);
    clear(pixels, COLOR_BG);

    font_draw_text(pixels, VIDEO_WIDTH, MARGIN_X, MARGIN_TOP, title,
                    COLOR_TEXT, TEXT_SCALE + 1);

    if (body != NULL) {
        font_draw_text(pixels, VIDEO_WIDTH, MARGIN_X, MARGIN_TOP + 64, body,
                        COLOR_TEXT_DIM, TEXT_SCALE);
    }

    video_present(video);
}

void screen_draw_library(Video *video, const RommGame *games, size_t count,
                          size_t focused_index, const char *status_line) {
    uint32_t *pixels = video_pixels(video);
    clear(pixels, COLOR_BG);

    font_draw_text(pixels, VIDEO_WIDTH, MARGIN_X, MARGIN_TOP,
                    "RomM-PS5   Up/Down: select   X: download   O: quit",
                    COLOR_TEXT_DIM, 1);

    int list_top = MARGIN_TOP + 40;
    int max_visible_rows = (VIDEO_HEIGHT - list_top - 60) / (ROW_HEIGHT + ROW_GAP);
    if (max_visible_rows < 1) {
        max_visible_rows = 1;
    }

    size_t first_visible = 0;
    if (focused_index >= (size_t)max_visible_rows) {
        first_visible = focused_index - (size_t)max_visible_rows + 1;
    }

    size_t last_visible = first_visible + (size_t)max_visible_rows;
    if (last_visible > count) {
        last_visible = count;
    }

    for (size_t i = first_visible; i < last_visible; i++) {
        int row_y = list_top + (int)(i - first_visible) * (ROW_HEIGHT + ROW_GAP);
        bool focused = (i == focused_index);

        fill_rect(pixels, MARGIN_X, row_y, VIDEO_WIDTH - 2 * MARGIN_X,
                  ROW_HEIGHT, focused ? COLOR_FOCUS_BG : COLOR_ROW_BG);

        char line[196];
        snprintf(line, sizeof(line), "%s  [%s]", games[i].title,
                  format_label(games[i].format));
        font_draw_text(pixels, VIDEO_WIDTH, MARGIN_X + 12,
                        row_y + (ROW_HEIGHT - FONT_GLYPH_SIZE * TEXT_SCALE) / 2,
                        line, COLOR_TEXT, TEXT_SCALE);
    }

    if (count == 0) {
        font_draw_text(pixels, VIDEO_WIDTH, MARGIN_X, list_top,
                        "No games found.", COLOR_TEXT_DIM, TEXT_SCALE);
    }

    if (status_line != NULL && status_line[0] != '\0') {
        font_draw_text(pixels, VIDEO_WIDTH, MARGIN_X, VIDEO_HEIGHT - 44,
                        status_line, COLOR_ERROR, TEXT_SCALE);
    }

    video_present(video);
}

static const char *state_label(DownloadState state) {
    switch (state) {
    case DL_STATE_IDLE:
        return "Idle";
    case DL_STATE_DOWNLOADING:
        return "Downloading";
    case DL_STATE_EXTRACTING:
        return "Extracting";
    case DL_STATE_VALIDATING:
        return "Validating";
    case DL_STATE_COMPLETED:
        return "Completed";
    case DL_STATE_FAILED:
        return "Failed";
    case DL_STATE_CANCELLED:
        return "Cancelled";
    default:
        return "Unknown";
    }
}

static void format_bytes(uint64_t bytes, char *out, size_t out_capacity) {
    const char *unit = "B";
    double value = (double)bytes;
    if (value >= 1024.0 * 1024.0 * 1024.0) {
        value /= 1024.0 * 1024.0 * 1024.0;
        unit = "GB";
    } else if (value >= 1024.0 * 1024.0) {
        value /= 1024.0 * 1024.0;
        unit = "MB";
    } else if (value >= 1024.0) {
        value /= 1024.0;
        unit = "KB";
    }
    snprintf(out, out_capacity, "%.1f %s", value, unit);
}

void screen_draw_download(Video *video, const RommGame *game,
                           const DownloadProgress *progress) {
    uint32_t *pixels = video_pixels(video);
    clear(pixels, COLOR_BG);

    char title_line[196];
    snprintf(title_line, sizeof(title_line), "Downloading: %s", game->title);
    font_draw_text(pixels, VIDEO_WIDTH, MARGIN_X, MARGIN_TOP, title_line,
                    COLOR_TEXT, TEXT_SCALE + 1);

    char state_line[64];
    snprintf(state_line, sizeof(state_line), "State: %s",
              state_label(progress->state));
    font_draw_text(pixels, VIDEO_WIDTH, MARGIN_X, MARGIN_TOP + 56, state_line,
                    COLOR_TEXT_DIM, TEXT_SCALE);

    int bar_y = MARGIN_TOP + 120;
    int bar_w = VIDEO_WIDTH - 2 * MARGIN_X;
    int bar_h = 36;
    fill_rect(pixels, MARGIN_X, bar_y, bar_w, bar_h, COLOR_BAR_BG);

    if (progress->bytes_total > 0) {
        double fraction = (double)progress->bytes_transferred /
                           (double)progress->bytes_total;
        if (fraction > 1.0) {
            fraction = 1.0;
        }
        fill_rect(pixels, MARGIN_X, bar_y, (int)(bar_w * fraction), bar_h,
                  COLOR_BAR_FILL);
    }

    char transferred_str[32];
    char total_str[32];
    char speed_str[32];
    format_bytes(progress->bytes_transferred, transferred_str,
                 sizeof(transferred_str));
    format_bytes(progress->bytes_total, total_str, sizeof(total_str));
    format_bytes(progress->speed_bytes_per_sec, speed_str, sizeof(speed_str));

    char detail_line[160];
    snprintf(detail_line, sizeof(detail_line), "%s / %s   (%s/s)",
              transferred_str,
              progress->bytes_total > 0 ? total_str : "?", speed_str);
    font_draw_text(pixels, VIDEO_WIDTH, MARGIN_X, bar_y + bar_h + 20,
                    detail_line, COLOR_TEXT, TEXT_SCALE);

    if (progress->state == DL_STATE_FAILED &&
        progress->failure_reason[0] != '\0') {
        font_draw_text(pixels, VIDEO_WIDTH, MARGIN_X, bar_y + bar_h + 70,
                        progress->failure_reason, COLOR_ERROR, TEXT_SCALE);
    } else if (progress->state == DL_STATE_COMPLETED) {
        font_draw_text(pixels, VIDEO_WIDTH, MARGIN_X, bar_y + bar_h + 70,
                        "Done. Press X to return to the list.", COLOR_OK,
                        TEXT_SCALE);
    } else {
        font_draw_text(pixels, VIDEO_WIDTH, MARGIN_X, bar_y + bar_h + 70,
                        "Press O to cancel.", COLOR_TEXT_DIM, TEXT_SCALE);
    }

    video_present(video);
}
