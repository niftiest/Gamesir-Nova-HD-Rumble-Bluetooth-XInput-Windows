/*
 * test_gamepad.c -- Unit tests for apply_profile().
 *
 * Run as a standalone exe. assert() aborts on failure (non-zero exit).
 * Build via Build.ps1 once Task 18 wires it in.
 */

#include "gamepad.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Helper: build a minimal valid profile_t for tests, all maps "absent" by default. */
static profile_t make_blank_profile(void)
{
    profile_t p;
    memset(&p, 0, sizeof(p));
    p.schema_version = 1;
    p.report_id = 0x01;
    p.report_length = 16;
    p.dpad.byte = 0;
    p.dpad.encoding = DPAD_8DIRECTION;
    return p;
}

/* Helper: build a 16-byte input buffer with report_id = 0x01 in byte 0. */
static void make_buf(uint8_t *buf, size_t len)
{
    memset(buf, 0, len);
    buf[0] = 0x01;
}

/* === D-pad 8direction === */
/* dpad encoding: byte value 0..7 = N/NE/E/SE/S/SW/W/NW, anything else = neutral.
 * Standard HID gamepad convention. The dpad byte itself is at profile.dpad.byte. */
static void test_dpad_8direction_north(void)
{
    profile_t p = make_blank_profile();
    p.dpad.byte = 1;
    uint8_t buf[16];
    make_buf(buf, sizeof(buf));
    buf[1] = 0;  /* N */
    gamepad_state_t s;
    assert(apply_profile(&p, buf, sizeof(buf), &s));
    assert((s.buttons & GP_BUTTON_DPAD_UP) != 0);
    assert((s.buttons & (GP_BUTTON_DPAD_DOWN | GP_BUTTON_DPAD_LEFT | GP_BUTTON_DPAD_RIGHT)) == 0);
    printf("PASS test_dpad_8direction_north\n");
}

static void test_dpad_8direction_northeast(void)
{
    profile_t p = make_blank_profile();
    p.dpad.byte = 1;
    uint8_t buf[16];
    make_buf(buf, sizeof(buf));
    buf[1] = 1;  /* NE */
    gamepad_state_t s;
    assert(apply_profile(&p, buf, sizeof(buf), &s));
    assert((s.buttons & GP_BUTTON_DPAD_UP) != 0);
    assert((s.buttons & GP_BUTTON_DPAD_RIGHT) != 0);
    assert((s.buttons & (GP_BUTTON_DPAD_DOWN | GP_BUTTON_DPAD_LEFT)) == 0);
    printf("PASS test_dpad_8direction_northeast\n");
}

static void test_dpad_8direction_neutral(void)
{
    profile_t p = make_blank_profile();
    p.dpad.byte = 1;
    uint8_t buf[16];
    make_buf(buf, sizeof(buf));
    buf[1] = 8;  /* anything >= 8 = neutral */
    gamepad_state_t s;
    assert(apply_profile(&p, buf, sizeof(buf), &s));
    assert((s.buttons & (GP_BUTTON_DPAD_UP | GP_BUTTON_DPAD_DOWN | GP_BUTTON_DPAD_LEFT | GP_BUTTON_DPAD_RIGHT)) == 0);
    printf("PASS test_dpad_8direction_neutral\n");
}

/* Buffer too short: should return false. */
static void test_buffer_too_short(void)
{
    profile_t p = make_blank_profile();
    p.report_length = 16;
    uint8_t buf[4];
    make_buf(buf, sizeof(buf));
    gamepad_state_t s;
    assert(apply_profile(&p, buf, sizeof(buf), &s) == false);
    printf("PASS test_buffer_too_short\n");
}

/* Wrong report ID: should return false. */
static void test_wrong_report_id(void)
{
    profile_t p = make_blank_profile();
    p.report_id = 0x03;
    uint8_t buf[16];
    make_buf(buf, sizeof(buf));  /* sets buf[0] = 0x01, not 0x03 */
    gamepad_state_t s;
    assert(apply_profile(&p, buf, sizeof(buf), &s) == false);
    printf("PASS test_wrong_report_id\n");
}

/* === Buttons (bit extraction) === */
static void test_button_a_set(void)
{
    profile_t p = make_blank_profile();
    p.a.byte = 3; p.a.bit = 0; p.a.present = true;
    uint8_t buf[16];
    make_buf(buf, sizeof(buf));
    buf[3] = 0x01;  /* bit 0 set */
    gamepad_state_t s;
    assert(apply_profile(&p, buf, sizeof(buf), &s));
    assert((s.buttons & GP_BUTTON_A) != 0);
    printf("PASS test_button_a_set\n");
}

static void test_button_a_clear(void)
{
    profile_t p = make_blank_profile();
    p.a.byte = 3; p.a.bit = 0; p.a.present = true;
    uint8_t buf[16];
    make_buf(buf, sizeof(buf));
    buf[3] = 0x00;
    gamepad_state_t s;
    assert(apply_profile(&p, buf, sizeof(buf), &s));
    assert((s.buttons & GP_BUTTON_A) == 0);
    printf("PASS test_button_a_clear\n");
}

static void test_button_high_bit(void)
{
    profile_t p = make_blank_profile();
    p.start.byte = 2; p.start.bit = 7; p.start.present = true;
    uint8_t buf[16];
    make_buf(buf, sizeof(buf));
    buf[2] = 0x80;  /* bit 7 */
    gamepad_state_t s;
    assert(apply_profile(&p, buf, sizeof(buf), &s));
    assert((s.buttons & GP_BUTTON_START) != 0);
    printf("PASS test_button_high_bit\n");
}

static void test_button_absent_ignored(void)
{
    profile_t p = make_blank_profile();
    /* p.guide.present = false (default) — should not appear in output */
    uint8_t buf[16];
    make_buf(buf, sizeof(buf));
    buf[2] = 0xFF;  /* all bits set */
    gamepad_state_t s;
    assert(apply_profile(&p, buf, sizeof(buf), &s));
    assert((s.buttons & GP_BUTTON_GUIDE) == 0);
    printf("PASS test_button_absent_ignored\n");
}

/* === D-pad 8direction_ccw === */
/* CCW encoding: 0=N, 2=W, 4=S, 6=E, 8=neutral; diagonals 1=NW, 3=SW, 5=SE, 7=NE */
static void test_dpad_8direction_ccw_north(void)
{
    profile_t p = make_blank_profile();
    p.dpad.byte = 1;
    p.dpad.encoding = DPAD_8DIRECTION_CCW;
    uint8_t buf[16];
    make_buf(buf, sizeof(buf));
    buf[1] = 0;  /* N */
    gamepad_state_t s;
    assert(apply_profile(&p, buf, sizeof(buf), &s));
    assert((s.buttons & GP_BUTTON_DPAD_UP) != 0);
    assert((s.buttons & (GP_BUTTON_DPAD_DOWN | GP_BUTTON_DPAD_LEFT | GP_BUTTON_DPAD_RIGHT)) == 0);
    printf("PASS test_dpad_8direction_ccw_north\n");
}

static void test_dpad_8direction_ccw_west(void)
{
    profile_t p = make_blank_profile();
    p.dpad.byte = 1;
    p.dpad.encoding = DPAD_8DIRECTION_CCW;
    uint8_t buf[16];
    make_buf(buf, sizeof(buf));
    buf[1] = 2;  /* W (CCW: 2=W, not E like CW) */
    gamepad_state_t s;
    assert(apply_profile(&p, buf, sizeof(buf), &s));
    assert((s.buttons & GP_BUTTON_DPAD_LEFT) != 0);
    assert((s.buttons & (GP_BUTTON_DPAD_UP | GP_BUTTON_DPAD_DOWN | GP_BUTTON_DPAD_RIGHT)) == 0);
    printf("PASS test_dpad_8direction_ccw_west\n");
}

static void test_dpad_8direction_ccw_south(void)
{
    profile_t p = make_blank_profile();
    p.dpad.byte = 1;
    p.dpad.encoding = DPAD_8DIRECTION_CCW;
    uint8_t buf[16];
    make_buf(buf, sizeof(buf));
    buf[1] = 4;  /* S */
    gamepad_state_t s;
    assert(apply_profile(&p, buf, sizeof(buf), &s));
    assert((s.buttons & GP_BUTTON_DPAD_DOWN) != 0);
    assert((s.buttons & (GP_BUTTON_DPAD_UP | GP_BUTTON_DPAD_LEFT | GP_BUTTON_DPAD_RIGHT)) == 0);
    printf("PASS test_dpad_8direction_ccw_south\n");
}

static void test_dpad_8direction_ccw_east(void)
{
    profile_t p = make_blank_profile();
    p.dpad.byte = 1;
    p.dpad.encoding = DPAD_8DIRECTION_CCW;
    uint8_t buf[16];
    make_buf(buf, sizeof(buf));
    buf[1] = 6;  /* E (CCW: 6=E, not W like CW) */
    gamepad_state_t s;
    assert(apply_profile(&p, buf, sizeof(buf), &s));
    assert((s.buttons & GP_BUTTON_DPAD_RIGHT) != 0);
    assert((s.buttons & (GP_BUTTON_DPAD_UP | GP_BUTTON_DPAD_DOWN | GP_BUTTON_DPAD_LEFT)) == 0);
    printf("PASS test_dpad_8direction_ccw_east\n");
}

static void test_dpad_8direction_ccw_nw_diagonal(void)
{
    profile_t p = make_blank_profile();
    p.dpad.byte = 1;
    p.dpad.encoding = DPAD_8DIRECTION_CCW;
    uint8_t buf[16];
    make_buf(buf, sizeof(buf));
    buf[1] = 1;  /* NW diagonal in CCW encoding */
    gamepad_state_t s;
    assert(apply_profile(&p, buf, sizeof(buf), &s));
    assert((s.buttons & GP_BUTTON_DPAD_UP) != 0);
    assert((s.buttons & GP_BUTTON_DPAD_LEFT) != 0);
    assert((s.buttons & (GP_BUTTON_DPAD_DOWN | GP_BUTTON_DPAD_RIGHT)) == 0);
    printf("PASS test_dpad_8direction_ccw_nw_diagonal\n");
}

static void test_dpad_8direction_ccw_neutral(void)
{
    profile_t p = make_blank_profile();
    p.dpad.byte = 1;
    p.dpad.encoding = DPAD_8DIRECTION_CCW;
    uint8_t buf[16];
    make_buf(buf, sizeof(buf));
    buf[1] = 8;  /* 8 = neutral */
    gamepad_state_t s;
    assert(apply_profile(&p, buf, sizeof(buf), &s));
    assert((s.buttons & (GP_BUTTON_DPAD_UP | GP_BUTTON_DPAD_DOWN | GP_BUTTON_DPAD_LEFT | GP_BUTTON_DPAD_RIGHT)) == 0);
    printf("PASS test_dpad_8direction_ccw_neutral\n");
}

/* === D-pad individual_bits === */
static void test_dpad_individual_bits_up_only(void)
{
    profile_t p = make_blank_profile();
    p.dpad.encoding = DPAD_INDIVIDUAL_BITS;
    p.dpad.up.byte    = 2; p.dpad.up.bit    = 0; p.dpad.up.present    = true;
    p.dpad.down.byte  = 2; p.dpad.down.bit  = 1; p.dpad.down.present  = true;
    p.dpad.left.byte  = 2; p.dpad.left.bit  = 2; p.dpad.left.present  = true;
    p.dpad.right.byte = 2; p.dpad.right.bit = 3; p.dpad.right.present = true;
    uint8_t buf[16];
    make_buf(buf, sizeof(buf));
    buf[2] = 0x01;  /* up only */
    gamepad_state_t s;
    assert(apply_profile(&p, buf, sizeof(buf), &s));
    assert((s.buttons & GP_BUTTON_DPAD_UP) != 0);
    assert((s.buttons & (GP_BUTTON_DPAD_DOWN | GP_BUTTON_DPAD_LEFT | GP_BUTTON_DPAD_RIGHT)) == 0);
    printf("PASS test_dpad_individual_bits_up_only\n");
}

static void test_dpad_individual_bits_diagonal(void)
{
    profile_t p = make_blank_profile();
    p.dpad.encoding = DPAD_INDIVIDUAL_BITS;
    p.dpad.up.byte    = 2; p.dpad.up.bit    = 0; p.dpad.up.present    = true;
    p.dpad.right.byte = 2; p.dpad.right.bit = 3; p.dpad.right.present = true;
    uint8_t buf[16];
    make_buf(buf, sizeof(buf));
    buf[2] = (1u << 0) | (1u << 3);  /* up+right */
    gamepad_state_t s;
    assert(apply_profile(&p, buf, sizeof(buf), &s));
    assert((s.buttons & GP_BUTTON_DPAD_UP) != 0);
    assert((s.buttons & GP_BUTTON_DPAD_RIGHT) != 0);
    printf("PASS test_dpad_individual_bits_diagonal\n");
}

/* === Sticks === */
static void test_stick_u8_centered(void)
{
    profile_t p = make_blank_profile();
    p.lx.byte = 4; p.lx.type = AXIS_U8; p.lx.center = 128; p.lx.invert = false; p.lx.present = true;
    uint8_t buf[16];
    make_buf(buf, sizeof(buf));
    buf[4] = 128;  /* centered */
    gamepad_state_t s;
    assert(apply_profile(&p, buf, sizeof(buf), &s));
    assert(s.lx == 0);
    printf("PASS test_stick_u8_centered\n");
}

static void test_stick_u8_full_positive(void)
{
    profile_t p = make_blank_profile();
    p.lx.byte = 4; p.lx.type = AXIS_U8; p.lx.center = 128; p.lx.invert = false; p.lx.present = true;
    uint8_t buf[16];
    make_buf(buf, sizeof(buf));
    buf[4] = 255;
    gamepad_state_t s;
    assert(apply_profile(&p, buf, sizeof(buf), &s));
    assert(s.lx > 32000);  /* near max positive */
    printf("PASS test_stick_u8_full_positive\n");
}

static void test_stick_u8_full_negative(void)
{
    profile_t p = make_blank_profile();
    p.lx.byte = 4; p.lx.type = AXIS_U8; p.lx.center = 128; p.lx.invert = false; p.lx.present = true;
    uint8_t buf[16];
    make_buf(buf, sizeof(buf));
    buf[4] = 0;
    gamepad_state_t s;
    assert(apply_profile(&p, buf, sizeof(buf), &s));
    assert(s.lx < -32000);
    printf("PASS test_stick_u8_full_negative\n");
}

static void test_stick_u8_inverted(void)
{
    profile_t p = make_blank_profile();
    p.ly.byte = 5; p.ly.type = AXIS_U8; p.ly.center = 128; p.ly.invert = true; p.ly.present = true;
    uint8_t buf[16];
    make_buf(buf, sizeof(buf));
    buf[5] = 0;        /* HID Y-down → after invert → Y-up positive */
    gamepad_state_t s;
    assert(apply_profile(&p, buf, sizeof(buf), &s));
    assert(s.ly > 32000);
    printf("PASS test_stick_u8_inverted\n");
}

static void test_stick_u16le_centered(void)
{
    profile_t p = make_blank_profile();
    p.rx.byte = 6; p.rx.type = AXIS_U16LE; p.rx.center = 32768; p.rx.invert = false; p.rx.present = true;
    uint8_t buf[16];
    make_buf(buf, sizeof(buf));
    /* 32768 = 0x8000, little-endian: low byte 0x00, high byte 0x80 */
    buf[6] = 0x00;
    buf[7] = 0x80;
    gamepad_state_t s;
    assert(apply_profile(&p, buf, sizeof(buf), &s));
    assert(s.rx == 0);
    printf("PASS test_stick_u16le_centered\n");
}

static void test_stick_i16le_zero_center(void)
{
    profile_t p = make_blank_profile();
    p.ry.byte = 8; p.ry.type = AXIS_I16LE; p.ry.center = 0; p.ry.invert = false; p.ry.present = true;
    uint8_t buf[16];
    make_buf(buf, sizeof(buf));
    /* signed value 0x4000 = +16384, low 0x00 high 0x40 */
    buf[8] = 0x00;
    buf[9] = 0x40;
    gamepad_state_t s;
    assert(apply_profile(&p, buf, sizeof(buf), &s));
    assert(s.ry == 16384);
    printf("PASS test_stick_i16le_zero_center\n");
}

static void test_stick_i16le_negative(void)
{
    profile_t p = make_blank_profile();
    p.ry.byte = 8; p.ry.type = AXIS_I16LE; p.ry.center = 0; p.ry.invert = false; p.ry.present = true;
    uint8_t buf[16];
    make_buf(buf, sizeof(buf));
    /* -16384 = 0xC000, two's complement little-endian: low 0x00 high 0xC0 */
    buf[8] = 0x00;
    buf[9] = 0xC0;
    gamepad_state_t s;
    assert(apply_profile(&p, buf, sizeof(buf), &s));
    assert(s.ry == -16384);
    printf("PASS test_stick_i16le_negative\n");
}

static void test_stick_absent_zero(void)
{
    profile_t p = make_blank_profile();
    /* lx absent (default) — output must remain 0 */
    uint8_t buf[16];
    make_buf(buf, sizeof(buf));
    buf[4] = 255;  /* would be max if present */
    gamepad_state_t s;
    assert(apply_profile(&p, buf, sizeof(buf), &s));
    assert(s.lx == 0);
    printf("PASS test_stick_absent_zero\n");
}

/* === Triggers === */
static void test_trigger_u8_full(void)
{
    profile_t p = make_blank_profile();
    p.lt.byte = 10; p.lt.type = AXIS_U8; p.lt.present = true;
    uint8_t buf[16];
    make_buf(buf, sizeof(buf));
    buf[10] = 255;
    gamepad_state_t s;
    assert(apply_profile(&p, buf, sizeof(buf), &s));
    assert(s.lt == 255);
    printf("PASS test_trigger_u8_full\n");
}

static void test_trigger_u8_zero(void)
{
    profile_t p = make_blank_profile();
    p.rt.byte = 11; p.rt.type = AXIS_U8; p.rt.present = true;
    uint8_t buf[16];
    make_buf(buf, sizeof(buf));
    buf[11] = 0;
    gamepad_state_t s;
    assert(apply_profile(&p, buf, sizeof(buf), &s));
    assert(s.rt == 0);
    printf("PASS test_trigger_u8_zero\n");
}

static void test_trigger_u16le_scaled(void)
{
    profile_t p = make_blank_profile();
    p.lt.byte = 10; p.lt.type = AXIS_U16LE; p.lt.present = true;
    uint8_t buf[16];
    make_buf(buf, sizeof(buf));
    /* Half range = 32767, low 0xFF high 0x7F */
    buf[10] = 0xFF;
    buf[11] = 0x7F;
    gamepad_state_t s;
    assert(apply_profile(&p, buf, sizeof(buf), &s));
    /* 32767 / 65535 * 255 ~= 127 — accept 126..128 */
    assert(s.lt >= 126 && s.lt <= 128);
    printf("PASS test_trigger_u16le_scaled\n");
}

static void test_trigger_absent_zero(void)
{
    profile_t p = make_blank_profile();
    /* lt absent (default) */
    uint8_t buf[16];
    make_buf(buf, sizeof(buf));
    buf[10] = 255;  /* would be full if present */
    gamepad_state_t s;
    assert(apply_profile(&p, buf, sizeof(buf), &s));
    assert(s.lt == 0);
    printf("PASS test_trigger_absent_zero\n");
}

/* === D-pad DPAD_BITS === */
/* Each direction is an independent bit spec. Directions can be mixed freely. */
static void test_dpad_bits_up_only(void)
{
    profile_t p = make_blank_profile();
    p.dpad.encoding = DPAD_BITS;
    p.dpad.up.byte    = 5; p.dpad.up.bit    = 1; p.dpad.up.present    = true;
    p.dpad.down.byte  = 5; p.dpad.down.bit  = 0; p.dpad.down.present  = true;
    p.dpad.left.byte  = 5; p.dpad.left.bit  = 3; p.dpad.left.present  = true;
    p.dpad.right.byte = 5; p.dpad.right.bit = 2; p.dpad.right.present = true;
    uint8_t buf[16];
    make_buf(buf, sizeof(buf));
    buf[5] = (1u << 1); /* up bit only */
    gamepad_state_t s;
    assert(apply_profile(&p, buf, sizeof(buf), &s));
    assert((s.buttons & GP_BUTTON_DPAD_UP) != 0);
    assert((s.buttons & (GP_BUTTON_DPAD_DOWN | GP_BUTTON_DPAD_LEFT | GP_BUTTON_DPAD_RIGHT)) == 0);
    printf("PASS test_dpad_bits_up_only\n");
}

static void test_dpad_bits_down_only(void)
{
    profile_t p = make_blank_profile();
    p.dpad.encoding = DPAD_BITS;
    p.dpad.up.byte    = 5; p.dpad.up.bit    = 1; p.dpad.up.present    = true;
    p.dpad.down.byte  = 5; p.dpad.down.bit  = 0; p.dpad.down.present  = true;
    p.dpad.left.byte  = 5; p.dpad.left.bit  = 3; p.dpad.left.present  = true;
    p.dpad.right.byte = 5; p.dpad.right.bit = 2; p.dpad.right.present = true;
    uint8_t buf[16];
    make_buf(buf, sizeof(buf));
    buf[5] = (1u << 0); /* down bit only */
    gamepad_state_t s;
    assert(apply_profile(&p, buf, sizeof(buf), &s));
    assert((s.buttons & GP_BUTTON_DPAD_DOWN) != 0);
    assert((s.buttons & (GP_BUTTON_DPAD_UP | GP_BUTTON_DPAD_LEFT | GP_BUTTON_DPAD_RIGHT)) == 0);
    printf("PASS test_dpad_bits_down_only\n");
}

static void test_dpad_bits_left_only(void)
{
    profile_t p = make_blank_profile();
    p.dpad.encoding = DPAD_BITS;
    p.dpad.up.byte    = 5; p.dpad.up.bit    = 1; p.dpad.up.present    = true;
    p.dpad.down.byte  = 5; p.dpad.down.bit  = 0; p.dpad.down.present  = true;
    p.dpad.left.byte  = 5; p.dpad.left.bit  = 3; p.dpad.left.present  = true;
    p.dpad.right.byte = 5; p.dpad.right.bit = 2; p.dpad.right.present = true;
    uint8_t buf[16];
    make_buf(buf, sizeof(buf));
    buf[5] = (1u << 3); /* left bit only */
    gamepad_state_t s;
    assert(apply_profile(&p, buf, sizeof(buf), &s));
    assert((s.buttons & GP_BUTTON_DPAD_LEFT) != 0);
    assert((s.buttons & (GP_BUTTON_DPAD_UP | GP_BUTTON_DPAD_DOWN | GP_BUTTON_DPAD_RIGHT)) == 0);
    printf("PASS test_dpad_bits_left_only\n");
}

static void test_dpad_bits_right_only(void)
{
    profile_t p = make_blank_profile();
    p.dpad.encoding = DPAD_BITS;
    p.dpad.up.byte    = 5; p.dpad.up.bit    = 1; p.dpad.up.present    = true;
    p.dpad.down.byte  = 5; p.dpad.down.bit  = 0; p.dpad.down.present  = true;
    p.dpad.left.byte  = 5; p.dpad.left.bit  = 3; p.dpad.left.present  = true;
    p.dpad.right.byte = 5; p.dpad.right.bit = 2; p.dpad.right.present = true;
    uint8_t buf[16];
    make_buf(buf, sizeof(buf));
    buf[5] = (1u << 2); /* right bit only */
    gamepad_state_t s;
    assert(apply_profile(&p, buf, sizeof(buf), &s));
    assert((s.buttons & GP_BUTTON_DPAD_RIGHT) != 0);
    assert((s.buttons & (GP_BUTTON_DPAD_UP | GP_BUTTON_DPAD_DOWN | GP_BUTTON_DPAD_LEFT)) == 0);
    printf("PASS test_dpad_bits_right_only\n");
}

static void test_dpad_bits_diagonal_up_right(void)
{
    profile_t p = make_blank_profile();
    p.dpad.encoding = DPAD_BITS;
    p.dpad.up.byte    = 5; p.dpad.up.bit    = 1; p.dpad.up.present    = true;
    p.dpad.down.byte  = 5; p.dpad.down.bit  = 0; p.dpad.down.present  = true;
    p.dpad.left.byte  = 5; p.dpad.left.bit  = 3; p.dpad.left.present  = true;
    p.dpad.right.byte = 5; p.dpad.right.bit = 2; p.dpad.right.present = true;
    uint8_t buf[16];
    make_buf(buf, sizeof(buf));
    buf[5] = (1u << 1) | (1u << 2); /* up + right */
    gamepad_state_t s;
    assert(apply_profile(&p, buf, sizeof(buf), &s));
    assert((s.buttons & GP_BUTTON_DPAD_UP) != 0);
    assert((s.buttons & GP_BUTTON_DPAD_RIGHT) != 0);
    assert((s.buttons & (GP_BUTTON_DPAD_DOWN | GP_BUTTON_DPAD_LEFT)) == 0);
    printf("PASS test_dpad_bits_diagonal_up_right\n");
}

static void test_dpad_bits_neutral(void)
{
    profile_t p = make_blank_profile();
    p.dpad.encoding = DPAD_BITS;
    p.dpad.up.byte    = 5; p.dpad.up.bit    = 1; p.dpad.up.present    = true;
    p.dpad.down.byte  = 5; p.dpad.down.bit  = 0; p.dpad.down.present  = true;
    p.dpad.left.byte  = 5; p.dpad.left.bit  = 3; p.dpad.left.present  = true;
    p.dpad.right.byte = 5; p.dpad.right.bit = 2; p.dpad.right.present = true;
    uint8_t buf[16];
    make_buf(buf, sizeof(buf));
    buf[5] = 0x00; /* no bits set = neutral */
    gamepad_state_t s;
    assert(apply_profile(&p, buf, sizeof(buf), &s));
    assert((s.buttons & (GP_BUTTON_DPAD_UP | GP_BUTTON_DPAD_DOWN | GP_BUTTON_DPAD_LEFT | GP_BUTTON_DPAD_RIGHT)) == 0);
    printf("PASS test_dpad_bits_neutral\n");
}

/* === Sticks AXIS_U12LE_LOW === */
/* LX: low 8 bits at byte, high 4 bits = (byte+1) & 0x0F. Range 0..4095, center 2048. */
static void test_stick_u12le_low_center(void)
{
    profile_t p = make_blank_profile();
    p.lx.byte = 6; p.lx.type = AXIS_U12LE_LOW; p.lx.center = 2048; p.lx.invert = false; p.lx.present = true;
    uint8_t buf[16];
    make_buf(buf, sizeof(buf));
    /* 2048 = 0x800: low byte 0x00, high nibble of byte+1 = 0x08 -> byte+1 = 0x08 */
    buf[6] = 0x00;
    buf[7] = 0x08;
    gamepad_state_t s;
    assert(apply_profile(&p, buf, sizeof(buf), &s));
    assert(s.lx == 0);
    printf("PASS test_stick_u12le_low_center\n");
}

static void test_stick_u12le_low_max(void)
{
    profile_t p = make_blank_profile();
    p.lx.byte = 6; p.lx.type = AXIS_U12LE_LOW; p.lx.center = 2048; p.lx.invert = false; p.lx.present = true;
    uint8_t buf[16];
    make_buf(buf, sizeof(buf));
    /* 4095 = 0xFFF: low byte 0xFF, high nibble of byte+1 = 0x0F -> byte+1 = 0x0F */
    buf[6] = 0xFF;
    buf[7] = 0x0F;
    gamepad_state_t s;
    assert(apply_profile(&p, buf, sizeof(buf), &s));
    assert(s.lx > 32000); /* near max positive */
    printf("PASS test_stick_u12le_low_max\n");
}

static void test_stick_u12le_low_min(void)
{
    profile_t p = make_blank_profile();
    p.lx.byte = 6; p.lx.type = AXIS_U12LE_LOW; p.lx.center = 2048; p.lx.invert = false; p.lx.present = true;
    uint8_t buf[16];
    make_buf(buf, sizeof(buf));
    /* 0 = 0x000: low byte 0x00, byte+1 low nibble = 0x00 */
    buf[6] = 0x00;
    buf[7] = 0x00;
    gamepad_state_t s;
    assert(apply_profile(&p, buf, sizeof(buf), &s));
    assert(s.lx < -32000); /* near max negative */
    printf("PASS test_stick_u12le_low_min\n");
}

static void test_stick_u12le_low_inverted(void)
{
    profile_t p = make_blank_profile();
    p.ly.byte = 6; p.ly.type = AXIS_U12LE_LOW; p.ly.center = 2048; p.ly.invert = true; p.ly.present = true;
    uint8_t buf[16];
    make_buf(buf, sizeof(buf));
    /* raw 0 (min) with invert should produce large positive */
    buf[6] = 0x00;
    buf[7] = 0x00;
    gamepad_state_t s;
    assert(apply_profile(&p, buf, sizeof(buf), &s));
    assert(s.ly > 32000);
    printf("PASS test_stick_u12le_low_inverted\n");
}

/* === Sticks AXIS_U12LE_HIGH === */
/* LY: low 4 bits = byte >> 4, high 8 bits at byte+1. Range 0..4095, center 2048. */
static void test_stick_u12le_high_center(void)
{
    profile_t p = make_blank_profile();
    p.ly.byte = 7; p.ly.type = AXIS_U12LE_HIGH; p.ly.center = 2048; p.ly.invert = false; p.ly.present = true;
    uint8_t buf[16];
    make_buf(buf, sizeof(buf));
    /* 2048 = 0x800: high nibble of byte = 0x8 -> byte = 0x80, byte+1 = 0x00 ... wait:
     * v = (buf[7] >> 4) | (buf[8] << 4)
     * For v=2048=0x800: low nibble of result = 0x0 => (buf[7] >> 4) = 0, buf[8] = 0x80 */
    buf[7] = 0x00; /* low 4 bits of result = 0x0 */
    buf[8] = 0x80; /* high 8 bits = 0x80, so (0x80 << 4) = 0x800 */
    gamepad_state_t s;
    assert(apply_profile(&p, buf, sizeof(buf), &s));
    assert(s.ly == 0);
    printf("PASS test_stick_u12le_high_center\n");
}

static void test_stick_u12le_high_max(void)
{
    profile_t p = make_blank_profile();
    p.ly.byte = 7; p.ly.type = AXIS_U12LE_HIGH; p.ly.center = 2048; p.ly.invert = false; p.ly.present = true;
    uint8_t buf[16];
    make_buf(buf, sizeof(buf));
    /* 4095 = 0xFFF: (buf[7] >> 4) | (buf[8] << 4) = 0xFFF
     * => (buf[7] >> 4) = 0xF, buf[8] = 0xFF -> (0xFF << 4) = 0xFF0, | 0xF = 0xFFF */
    buf[7] = 0xF0; /* high nibble = 0xF */
    buf[8] = 0xFF;
    gamepad_state_t s;
    assert(apply_profile(&p, buf, sizeof(buf), &s));
    assert(s.ly > 32000);
    printf("PASS test_stick_u12le_high_max\n");
}

static void test_stick_u12le_high_min(void)
{
    profile_t p = make_blank_profile();
    p.ly.byte = 7; p.ly.type = AXIS_U12LE_HIGH; p.ly.center = 2048; p.ly.invert = false; p.ly.present = true;
    uint8_t buf[16];
    make_buf(buf, sizeof(buf));
    buf[7] = 0x00;
    buf[8] = 0x00;
    gamepad_state_t s;
    assert(apply_profile(&p, buf, sizeof(buf), &s));
    assert(s.ly < -32000);
    printf("PASS test_stick_u12le_high_min\n");
}

static void test_stick_u12le_high_inverted(void)
{
    profile_t p = make_blank_profile();
    p.ly.byte = 7; p.ly.type = AXIS_U12LE_HIGH; p.ly.center = 2048; p.ly.invert = true; p.ly.present = true;
    uint8_t buf[16];
    make_buf(buf, sizeof(buf));
    /* raw 0 with invert should produce large positive */
    buf[7] = 0x00;
    buf[8] = 0x00;
    gamepad_state_t s;
    assert(apply_profile(&p, buf, sizeof(buf), &s));
    assert(s.ly > 32000);
    printf("PASS test_stick_u12le_high_inverted\n");
}

/* === Digital triggers (AXIS_BIT) === */
static void test_trigger_bit_pressed(void)
{
    profile_t p = make_blank_profile();
    p.lt.byte = 2; p.lt.type = AXIS_BIT; p.lt.bit = 0; p.lt.present = true;
    uint8_t buf[16];
    make_buf(buf, sizeof(buf));
    buf[2] = 0x01;
    gamepad_state_t s;
    assert(apply_profile(&p, buf, sizeof(buf), &s));
    assert(s.lt == 255);
    printf("PASS test_trigger_bit_pressed\n");
}

static void test_trigger_bit_unpressed(void)
{
    profile_t p = make_blank_profile();
    p.lt.byte = 2; p.lt.type = AXIS_BIT; p.lt.bit = 0; p.lt.present = true;
    uint8_t buf[16];
    make_buf(buf, sizeof(buf));
    buf[2] = 0x00;
    gamepad_state_t s;
    assert(apply_profile(&p, buf, sizeof(buf), &s));
    assert(s.lt == 0);
    printf("PASS test_trigger_bit_unpressed\n");
}

static void test_trigger_bit_other_bits_ignored(void)
{
    profile_t p = make_blank_profile();
    p.lt.byte = 2; p.lt.type = AXIS_BIT; p.lt.bit = 0; p.lt.present = true;
    uint8_t buf[16];
    make_buf(buf, sizeof(buf));
    buf[2] = 0xFE;  /* every bit set EXCEPT bit 0 */
    gamepad_state_t s;
    assert(apply_profile(&p, buf, sizeof(buf), &s));
    assert(s.lt == 0);
    printf("PASS test_trigger_bit_other_bits_ignored\n");
}

int main(void)
{
    test_dpad_8direction_north();
    test_dpad_8direction_northeast();
    test_dpad_8direction_neutral();
    test_dpad_8direction_ccw_north();
    test_dpad_8direction_ccw_west();
    test_dpad_8direction_ccw_south();
    test_dpad_8direction_ccw_east();
    test_dpad_8direction_ccw_nw_diagonal();
    test_dpad_8direction_ccw_neutral();
    test_buffer_too_short();
    test_wrong_report_id();
    test_button_a_set();
    test_button_a_clear();
    test_button_high_bit();
    test_button_absent_ignored();
    test_dpad_individual_bits_up_only();
    test_dpad_individual_bits_diagonal();
    test_dpad_bits_up_only();
    test_dpad_bits_down_only();
    test_dpad_bits_left_only();
    test_dpad_bits_right_only();
    test_dpad_bits_diagonal_up_right();
    test_dpad_bits_neutral();
    test_stick_u8_centered();
    test_stick_u8_full_positive();
    test_stick_u8_full_negative();
    test_stick_u8_inverted();
    test_stick_u16le_centered();
    test_stick_i16le_zero_center();
    test_stick_i16le_negative();
    test_stick_absent_zero();
    test_stick_u12le_low_center();
    test_stick_u12le_low_max();
    test_stick_u12le_low_min();
    test_stick_u12le_low_inverted();
    test_stick_u12le_high_center();
    test_stick_u12le_high_max();
    test_stick_u12le_high_min();
    test_stick_u12le_high_inverted();
    test_trigger_u8_full();
    test_trigger_u8_zero();
    test_trigger_u16le_scaled();
    test_trigger_absent_zero();
    test_trigger_bit_pressed();
    test_trigger_bit_unpressed();
    test_trigger_bit_other_bits_ignored();
    printf("ALL test_gamepad PASSED\n");
    return 0;
}
