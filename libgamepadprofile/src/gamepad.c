/*
 * gamepad.c -- Apply a profile to raw HID bytes to produce a gamepad_state.
 */

#include "gamepad.h"
#include <string.h>

/* 8direction lookup: byte value 0..7 -> set of dpad bits.
 * Clockwise (standard): 0=N, 1=NE, 2=E, 3=SE, 4=S, 5=SW, 6=W, 7=NW. Anything else = neutral. */
static const uint16_t DPAD_8DIRECTION_LUT[8] = {
    GP_BUTTON_DPAD_UP,                          /* 0 N  */
    GP_BUTTON_DPAD_UP    | GP_BUTTON_DPAD_RIGHT,/* 1 NE */
    GP_BUTTON_DPAD_RIGHT,                       /* 2 E  */
    GP_BUTTON_DPAD_DOWN  | GP_BUTTON_DPAD_RIGHT,/* 3 SE */
    GP_BUTTON_DPAD_DOWN,                        /* 4 S  */
    GP_BUTTON_DPAD_DOWN  | GP_BUTTON_DPAD_LEFT, /* 5 SW */
    GP_BUTTON_DPAD_LEFT,                        /* 6 W  */
    GP_BUTTON_DPAD_UP    | GP_BUTTON_DPAD_LEFT  /* 7 NW */
};

/* 8direction_ccw lookup: byte value 0..7 -> set of dpad bits.
 * Counter-clockwise (Switch Pro controller in Pro mode):
 *   0=N, 1=NW, 2=W, 3=SW, 4=S, 5=SE, 6=E, 7=NE. Anything else = neutral. */
static const uint16_t DPAD_8DIRECTION_CCW_LUT[8] = {
    GP_BUTTON_DPAD_UP,                          /* 0 N  */
    GP_BUTTON_DPAD_UP    | GP_BUTTON_DPAD_LEFT, /* 1 NW */
    GP_BUTTON_DPAD_LEFT,                        /* 2 W  */
    GP_BUTTON_DPAD_DOWN  | GP_BUTTON_DPAD_LEFT, /* 3 SW */
    GP_BUTTON_DPAD_DOWN,                        /* 4 S  */
    GP_BUTTON_DPAD_DOWN  | GP_BUTTON_DPAD_RIGHT,/* 5 SE */
    GP_BUTTON_DPAD_RIGHT,                       /* 6 E  */
    GP_BUTTON_DPAD_UP    | GP_BUTTON_DPAD_RIGHT /* 7 NE */
};

static inline void apply_button(uint16_t *out, const button_map_t *m,
                                const uint8_t *buf, uint16_t bit_mask)
{
    if (!m->present) return;
    if (buf[m->byte] & (1u << m->bit)) *out |= bit_mask;
}

/* Read a stick raw value per axis_type_t. Returns the raw integer value as int32 (signed). */
static int32_t read_axis_raw(const stick_map_t *m, const uint8_t *buf)
{
    switch (m->type) {
    case AXIS_U8:    return (int32_t)buf[m->byte];
    case AXIS_U16LE: return (int32_t)((uint32_t)buf[m->byte] | ((uint32_t)buf[m->byte + 1] << 8));
    case AXIS_I16LE: {
        uint16_t u = (uint16_t)((uint16_t)buf[m->byte] | ((uint16_t)buf[m->byte + 1] << 8));
        return (int32_t)(int16_t)u;
    }
    case AXIS_U12LE_LOW:
        /* low 8 bits at byte, high 4 bits = low nibble of (byte+1) */
        return (int32_t)((uint32_t)buf[m->byte] | (((uint32_t)buf[m->byte + 1] & 0x0Fu) << 8));
    case AXIS_U12LE_HIGH:
        /* low 4 bits = high nibble of byte, high 8 bits at (byte+1) */
        return (int32_t)(((uint32_t)buf[m->byte] >> 4) | ((uint32_t)buf[m->byte + 1] << 4));
    case AXIS_BIT:
        return 0; /* not used for sticks */
    }
    return 0;
}

/* Default center value for an axis_type_t (used when profile sets center == -1). */
static int axis_default_center(axis_type_t t)
{
    switch (t) {
    case AXIS_U8:         return 128;
    case AXIS_U16LE:      return 32768;
    case AXIS_I16LE:      return 0;
    case AXIS_U12LE_LOW:  return 2048;
    case AXIS_U12LE_HIGH: return 2048;
    case AXIS_BIT:        return 0;
    }
    return 0;
}

/* Maximum |raw - center| for an axis_type_t (used to scale to int16 range). */
static int axis_half_range(axis_type_t t, int center)
{
    switch (t) {
    case AXIS_U8:         return (center > 128)  ? center : (255 - center);
    case AXIS_U16LE:      return (center > 32768) ? center : (65535 - center);
    case AXIS_I16LE:      return 32767;
    case AXIS_U12LE_LOW:
    case AXIS_U12LE_HIGH: return (center > 2048)  ? center : (4095 - center);
    case AXIS_BIT:        return 1;
    }
    return 1;
}

/* Map a raw stick reading to int16_t (-32768..32767), with optional invert. */
static int16_t stick_to_i16(const stick_map_t *m, const uint8_t *buf)
{
    int center = (m->center >= 0) ? m->center : axis_default_center(m->type);
    int32_t raw = read_axis_raw(m, buf);
    int32_t centered = raw - center;
    int half = axis_half_range(m->type, center);
    if (half <= 0) half = 1;
    /* Scale to -32767..32767 (symmetric to keep math simple). */
    int32_t scaled = (centered * 32767) / half;
    if (scaled >  32767) scaled =  32767;
    if (scaled < -32767) scaled = -32767;
    if (m->invert) scaled = -scaled;
    return (int16_t)scaled;
}

static inline void apply_stick(int16_t *out, const stick_map_t *m, const uint8_t *buf)
{
    if (!m->present) { *out = 0; return; }
    *out = stick_to_i16(m, buf);
}

/* Triggers are unipolar 0..max → output 0..255. */
static uint8_t trigger_to_u8(const trigger_map_t *m, const uint8_t *buf)
{
    switch (m->type) {
    case AXIS_U8:    return buf[m->byte];
    case AXIS_U16LE: {
        uint32_t v = (uint32_t)buf[m->byte] | ((uint32_t)buf[m->byte + 1] << 8);
        return (uint8_t)((v * 255u) / 65535u);
    }
    case AXIS_BIT:
        return (buf[m->byte] & (1u << m->bit)) ? 255 : 0;
    case AXIS_I16LE: /* not supported for triggers in v1 */
    default:
        return 0;
    }
}

static inline void apply_trigger(uint8_t *out, const trigger_map_t *m, const uint8_t *buf)
{
    if (!m->present) { *out = 0; return; }
    *out = trigger_to_u8(m, buf);
}

bool apply_profile(const profile_t *profile, const uint8_t *buf, size_t len, gamepad_state_t *state)
{
    if (len < (size_t)profile->report_length) return false;
    if (buf[0] != profile->report_id)         return false;

    memset(state, 0, sizeof(*state));

    /* D-pad */
    if (profile->dpad.encoding == DPAD_8DIRECTION) {
        uint8_t v = buf[profile->dpad.byte];
        if (v < 8) state->buttons |= DPAD_8DIRECTION_LUT[v];
    } else if (profile->dpad.encoding == DPAD_8DIRECTION_CCW) {
        uint8_t v = buf[profile->dpad.byte];
        if (v < 8) state->buttons |= DPAD_8DIRECTION_CCW_LUT[v];
    } else if (profile->dpad.encoding == DPAD_INDIVIDUAL_BITS) {
        apply_button(&state->buttons, &profile->dpad.up,    buf, GP_BUTTON_DPAD_UP);
        apply_button(&state->buttons, &profile->dpad.down,  buf, GP_BUTTON_DPAD_DOWN);
        apply_button(&state->buttons, &profile->dpad.left,  buf, GP_BUTTON_DPAD_LEFT);
        apply_button(&state->buttons, &profile->dpad.right, buf, GP_BUTTON_DPAD_RIGHT);
    } else if (profile->dpad.encoding == DPAD_BITS) {
        apply_button(&state->buttons, &profile->dpad.up,    buf, GP_BUTTON_DPAD_UP);
        apply_button(&state->buttons, &profile->dpad.down,  buf, GP_BUTTON_DPAD_DOWN);
        apply_button(&state->buttons, &profile->dpad.left,  buf, GP_BUTTON_DPAD_LEFT);
        apply_button(&state->buttons, &profile->dpad.right, buf, GP_BUTTON_DPAD_RIGHT);
    }

    /* Buttons */
    apply_button(&state->buttons, &profile->a,     buf, GP_BUTTON_A);
    apply_button(&state->buttons, &profile->b,     buf, GP_BUTTON_B);
    apply_button(&state->buttons, &profile->x,     buf, GP_BUTTON_X);
    apply_button(&state->buttons, &profile->y,     buf, GP_BUTTON_Y);
    apply_button(&state->buttons, &profile->lb,    buf, GP_BUTTON_LB);
    apply_button(&state->buttons, &profile->rb,    buf, GP_BUTTON_RB);
    apply_button(&state->buttons, &profile->ls,    buf, GP_BUTTON_LS);
    apply_button(&state->buttons, &profile->rs,    buf, GP_BUTTON_RS);
    apply_button(&state->buttons, &profile->back,  buf, GP_BUTTON_BACK);
    apply_button(&state->buttons, &profile->start, buf, GP_BUTTON_START);
    apply_button(&state->buttons, &profile->guide, buf, GP_BUTTON_GUIDE);

    /* Sticks */
    apply_stick(&state->lx, &profile->lx, buf);
    apply_stick(&state->ly, &profile->ly, buf);
    apply_stick(&state->rx, &profile->rx, buf);
    apply_stick(&state->ry, &profile->ry, buf);

    /* Triggers */
    apply_trigger(&state->lt, &profile->lt, buf);
    apply_trigger(&state->rt, &profile->rt, buf);

    return true;
}
