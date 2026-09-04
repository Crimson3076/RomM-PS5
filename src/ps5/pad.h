/* Real DualSense input via the console's own ScePad system library.
 *
 * The struct layout, constants, and function signatures below are copied
 * from ps5-payload-dev/SDL's src/joystick/ps5/SDL_ps5joystick.h (zlib
 * licensed — see third_party/THIRD_PARTY_LICENSES.md), which is a real,
 * maintained SDL2 joystick backend that reads ScePad state on PS5. That
 * header is the verified reference this project needed and didn't have
 * on its own: this SDK's sce_stubs only export the bare symbol names,
 * with no signatures, no struct definitions, and no documentation at all.
 * Copying a working implementation's own struct was judged far safer
 * than guessing one, which is what an earlier milestone in this project
 * deliberately avoided doing (see docs/testing.md's SceUserService/ScePad
 * history) — this is that gap now closed with an actual reference instead
 * of a guess.
 *
 * `pad_get_user_id()` resolves a real user id via
 * `sceUserServiceGetLoginUserIdList` (also from that same SDL header) —
 * the function this project previously reached for
 * (`sceUserServiceGetInitialUser`) was never demonstrated anywhere and
 * failed on real hardware; this one comes from a working reference and
 * falls back to `PAD_USER_ID_SYSTEM` if no user is logged in, rather than
 * refusing to open a pad at all.
 *
 * The initialization and first open attempt have run on real hardware.
 * No usable handle or input read has been confirmed yet; see
 * docs/testing.md.
 */
#ifndef ROMM_PS5_PAD_H
#define ROMM_PS5_PAD_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PAD_PORT_TYPE_STANDARD 0
#define PAD_USER_ID_SYSTEM 0xff
#define PAD_ERROR_ALREADY_OPENED 0x80920004u
/* Observed on hardware and decoded by public Orbis Device Service error
 * tables. This is not one of the ScePad-local 0x8092xxxx errors. */
#define PAD_ERROR_USER_NOT_LOGIN 0x809b0081u

#define PAD_BUTTON_L3 0x0002
#define PAD_BUTTON_R3 0x0004
#define PAD_BUTTON_OPTIONS 0x0008
#define PAD_BUTTON_UP 0x0010
#define PAD_BUTTON_RIGHT 0x0020
#define PAD_BUTTON_DOWN 0x0040
#define PAD_BUTTON_LEFT 0x0080
#define PAD_BUTTON_L2 0x0100
#define PAD_BUTTON_R2 0x0200
#define PAD_BUTTON_L1 0x0400
#define PAD_BUTTON_R1 0x0800
#define PAD_BUTTON_TRIANGLE 0x1000
#define PAD_BUTTON_CIRCLE 0x2000
#define PAD_BUTTON_CROSS 0x4000
#define PAD_BUTTON_SQUARE 0x8000
#define PAD_BUTTON_TOUCH_PAD 0x100000

typedef struct PadTouch {
    uint16_t x;
    uint16_t y;
    uint8_t finger;
    uint8_t pad[3];
} PadTouch;

typedef struct PadTouchData {
    uint8_t fingers;
    uint8_t pad1[3];
    uint32_t pad2;
    PadTouch touch[2];
} PadTouchData;

/* Byte-for-byte the same layout as ps5-payload-dev/SDL's PS5_PadData —
 * do not reorder, resize, or "clean up" any field here. */
typedef struct PadData {
    uint32_t buttons;
    struct {
        uint8_t x;
        uint8_t y;
    } leftStick;
    struct {
        uint8_t x;
        uint8_t y;
    } rightStick;
    struct {
        uint8_t l2;
        uint8_t r2;
    } analogButtons;
    uint16_t padding;
    struct {
        float x;
        float y;
        float z;
        float w;
    } quat;
    struct {
        float x;
        float y;
        float z;
    } vel;
    struct {
        float x;
        float y;
        float z;
    } acell;
    PadTouchData touch;
    uint8_t connected;
    uint64_t timestamp;
    uint8_t ext[16];
    uint8_t count;
    uint8_t unknown[15];
} PadData;

typedef struct {
    int32_t handle;   /* < 0 if no pad is currently open */
    int32_t user_id;
    bool available;   /* true once pad_init() has been called successfully */
} PadState;

/* Calls scePadInit() once. Safe to call more than once. Returns true if
 * the underlying call reported success. */
bool pad_init(PadState *state);

/* Enumerates every user id returned by
 * sceUserServiceGetLoginUserIdList and tries to obtain a standard
 * controller handle for each one. It also checks scePadGetHandle after
 * an open failure in case the pad is already owned by the system. Falls
 * back to PAD_USER_ID_SYSTEM only if none of the reported users works.
 * Returns true and fills state->handle/user_id on success. */
bool pad_open(PadState *state);

/* Reads the current controller state into *out. Returns false (and
 * leaves *out unchanged) if no pad is open or the read itself fails. */
bool pad_read(PadState *state, PadData *out);

void pad_close(PadState *state);

#ifdef __cplusplus
}
#endif

#endif /* ROMM_PS5_PAD_H */
