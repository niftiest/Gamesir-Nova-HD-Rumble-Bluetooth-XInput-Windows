/*
 * test_state_to_xusb.c -- Unit tests for gamepad_state_to_xusb().
 */

#include "state_to_xusb.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_buttons_round_trip(void)
{
    gamepad_state_t s; memset(&s, 0, sizeof(s));
    s.buttons = GP_BUTTON_A | GP_BUTTON_START | GP_BUTTON_DPAD_UP;
    XUSB_REPORT r; gamepad_state_to_xusb(&s, &r);
    assert(r.wButtons & XUSB_GAMEPAD_A);
    assert(r.wButtons & XUSB_GAMEPAD_START);
    assert(r.wButtons & XUSB_GAMEPAD_DPAD_UP);
    assert((r.wButtons & XUSB_GAMEPAD_B) == 0);
    printf("PASS test_buttons_round_trip\n");
}

static void test_sticks_round_trip(void)
{
    gamepad_state_t s; memset(&s, 0, sizeof(s));
    s.lx =  12345; s.ly = -23456;
    s.rx =  32767; s.ry = -32768;
    XUSB_REPORT r; gamepad_state_to_xusb(&s, &r);
    assert(r.sThumbLX ==  12345);
    assert(r.sThumbLY == -23456);
    assert(r.sThumbRX ==  32767);
    assert(r.sThumbRY == -32768);
    printf("PASS test_sticks_round_trip\n");
}

static void test_triggers_round_trip(void)
{
    gamepad_state_t s; memset(&s, 0, sizeof(s));
    s.lt = 200; s.rt = 50;
    XUSB_REPORT r; gamepad_state_to_xusb(&s, &r);
    assert(r.bLeftTrigger  == 200);
    assert(r.bRightTrigger == 50);
    printf("PASS test_triggers_round_trip\n");
}

static void test_empty_state(void)
{
    gamepad_state_t s; memset(&s, 0, sizeof(s));
    XUSB_REPORT r; memset(&r, 0xFF, sizeof(r));  /* poison */
    gamepad_state_to_xusb(&s, &r);
    assert(r.wButtons == 0);
    assert(r.sThumbLX == 0 && r.sThumbLY == 0);
    assert(r.sThumbRX == 0 && r.sThumbRY == 0);
    assert(r.bLeftTrigger == 0 && r.bRightTrigger == 0);
    printf("PASS test_empty_state\n");
}

int main(void)
{
    test_buttons_round_trip();
    test_sticks_round_trip();
    test_triggers_round_trip();
    test_empty_state();
    printf("ALL test_state_to_xusb PASSED\n");
    return 0;
}
