/*
 * gamepad.h -- Generic gamepad data structures and the apply_profile() transform.
 *
 * `gamepad_state` is profile-independent. `profile_t` describes how to read raw
 * HID bytes into a `gamepad_state`. Button bit layout chosen to match XInput's
 * XUSB_GAMEPAD_* values bit-for-bit, but the translation is still done
 * explicitly by the consumer (state_to_xusb.c) — do not assume ABI compat.
 */

#ifndef GAMEPAD_H
#define GAMEPAD_H

#include <wtypes.h>
#include <stdbool.h>
#include <stdint.h>

/* Generic button bits (intentionally identical layout to XUSB_GAMEPAD_*
 * for ease of translation; the translation is still explicit, not memcpy'd). */
#define GP_BUTTON_DPAD_UP    0x0001
#define GP_BUTTON_DPAD_DOWN  0x0002
#define GP_BUTTON_DPAD_LEFT  0x0004
#define GP_BUTTON_DPAD_RIGHT 0x0008
#define GP_BUTTON_START      0x0010
#define GP_BUTTON_BACK       0x0020
#define GP_BUTTON_LS         0x0040
#define GP_BUTTON_RS         0x0080
#define GP_BUTTON_LB         0x0100
#define GP_BUTTON_RB         0x0200
#define GP_BUTTON_GUIDE      0x0400
#define GP_BUTTON_A          0x1000
#define GP_BUTTON_B          0x2000
#define GP_BUTTON_X          0x4000
#define GP_BUTTON_Y          0x8000

/* Snapshot of gamepad inputs at a single point in time. Profile-independent. */
typedef struct {
    uint16_t buttons;       /* GP_BUTTON_* bitmask */
    int16_t  lx, ly;        /* Left stick, signed -32768..32767, 0 = center, +Y = up (Xbox convention) */
    int16_t  rx, ry;        /* Right stick */
    uint8_t  lt, rt;        /* Triggers, 0..255 */
} gamepad_state_t;

/* Profile schema enums */
typedef enum {
    DPAD_8DIRECTION,        /* one byte, 0..7 = N/NE/E/SE/S/SW/W/NW, anything else = neutral (clockwise) */
    DPAD_INDIVIDUAL_BITS,   /* four button bits, Xbox style */
    DPAD_8DIRECTION_CCW,    /* one byte, counter-clockwise: 0=N,2=W,4=S,6=E,8=neutral; diagonals 1=NW,3=SW,5=SE,7=NE */
    DPAD_BITS               /* four independent bit specs, one per direction */
} dpad_encoding_t;

/* Output report (host -> controller) protocol selection. */
typedef enum {
    OUTPUT_PROTO_NONE = 0,
    OUTPUT_PROTO_SWITCHPRO_RUMBLE = 1   /* Switch Pro-style 0x10 rumble + 0x01/0x48 enable subcommand */
} output_protocol_t;

/* Optional output report declaration on a profile.
 * If present == false, the profile does not declare output capability. */
typedef struct {
    int               report_id;
    output_protocol_t protocol;
    bool              present;
} output_report_t;

typedef enum {
    AXIS_U8,                /* unsigned 8-bit */
    AXIS_U16LE,             /* unsigned 16-bit, little-endian, 2 bytes starting at `byte` */
    AXIS_I16LE,             /* signed 16-bit, little-endian */
    AXIS_BIT,               /* digital trigger only - single bit at `byte`+`bit`; output 0 or 255 */
    AXIS_U12LE_LOW,         /* 12-bit value: low 8 bits at byte, high 4 bits = (byte+1) & 0x0F */
    AXIS_U12LE_HIGH         /* 12-bit value: low 4 bits = byte >> 4, high 8 bits at byte+1 */
} axis_type_t;

typedef enum {
    TRANSPORT_USB,
    TRANSPORT_BLUETOOTH,
    TRANSPORT_ANY
} transport_t;

/* A single button's location in the input report. */
typedef struct {
    int  byte;
    int  bit;       /* 0..7 */
    bool present;   /* true once set during profile load */
} button_map_t;

/* D-pad layout.
 * For 8DIRECTION / 8DIRECTION_CCW: `byte` and `encoding` are used.
 * For INDIVIDUAL_BITS: `up/down/left/right` button_map_t entries are used.
 * For DPAD_BITS: `up/down/left/right` button_map_t entries are used (each
 *   has its own byte + bit; the top-level `byte` field is unused). */
typedef struct {
    int             byte;
    dpad_encoding_t encoding;
    button_map_t    up, down, left, right;
} dpad_map_t;

/* Stick axis layout. For AXIS_U16LE / AXIS_I16LE the value spans bytes
 * `byte` (low) and `byte+1` (high). For AXIS_U8 it's a single byte. */
typedef struct {
    int         byte;
    axis_type_t type;
    int         center;     /* raw value treated as zero. -1 means use type default (128 / 32768 / 0) */
    bool        invert;     /* if true, output is negated (HID Y-up = -1 → Xbox Y-up = +1 needs invert) */
    bool        present;
} stick_map_t;

/* Trigger axis layout. Triggers are unipolar 0..max, no center concept.
 * For type AXIS_BIT: single-bit digital trigger, output is 0 or 255.
 *   `bit` is 0..7 (LSB to MSB). */
typedef struct {
    int         byte;
    axis_type_t type;       /* AXIS_U8, AXIS_U16LE, or AXIS_BIT in v1; AXIS_I16LE not supported for triggers */
    int         bit;        /* only used when type == AXIS_BIT */
    bool        present;
} trigger_map_t;

/* The fully-loaded profile. Owned by the caller; freed via profile_free. */
typedef struct {
    int         schema_version;
    char       *name;       /* malloc'd, UTF-8 */
    uint16_t    vid;
    uint16_t    pid;
    transport_t transport;
    uint8_t     report_id;
    int         report_length;
    dpad_map_t  dpad;

    button_map_t  a, b, x, y, lb, rb, ls, rs, back, start, guide;
    stick_map_t   lx, ly, rx, ry;
    trigger_map_t lt, rt;
    output_report_t output_report;
} profile_t;

/*
 * Apply `profile` to the raw HID input buffer `buf` (length `len`),
 * filling `*state`. Returns true on success, false if the buffer is too
 * short or its first byte does not match `profile->report_id`.
 *
 * Pure function: no I/O, no allocation, no globals. Safe to unit-test.
 */
bool apply_profile(const profile_t *profile, const uint8_t *buf, size_t len, gamepad_state_t *state);

#endif /* GAMEPAD_H */
