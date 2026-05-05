/*
 * state_to_xusb.c -- Translate gamepad_state_t to ViGEmBus XUSB_REPORT.
 *
 * Bit layouts of GP_BUTTON_* and XUSB_GAMEPAD_* happen to match but the
 * translation is explicit so the mapping survives changes to either side.
 */

#include "state_to_xusb.h"
#include <string.h>

bool gamepad_state_swap_face_buttons = false;

void gamepad_state_to_xusb(const gamepad_state_t *state, XUSB_REPORT *out)
{
    memset(out, 0, sizeof(*out));
    if (state->buttons & GP_BUTTON_DPAD_UP)    out->wButtons |= XUSB_GAMEPAD_DPAD_UP;
    if (state->buttons & GP_BUTTON_DPAD_DOWN)  out->wButtons |= XUSB_GAMEPAD_DPAD_DOWN;
    if (state->buttons & GP_BUTTON_DPAD_LEFT)  out->wButtons |= XUSB_GAMEPAD_DPAD_LEFT;
    if (state->buttons & GP_BUTTON_DPAD_RIGHT) out->wButtons |= XUSB_GAMEPAD_DPAD_RIGHT;
    if (state->buttons & GP_BUTTON_START)      out->wButtons |= XUSB_GAMEPAD_START;
    if (state->buttons & GP_BUTTON_BACK)       out->wButtons |= XUSB_GAMEPAD_BACK;
    if (state->buttons & GP_BUTTON_LS)         out->wButtons |= XUSB_GAMEPAD_LEFT_THUMB;
    if (state->buttons & GP_BUTTON_RS)         out->wButtons |= XUSB_GAMEPAD_RIGHT_THUMB;
    if (state->buttons & GP_BUTTON_LB)         out->wButtons |= XUSB_GAMEPAD_LEFT_SHOULDER;
    if (state->buttons & GP_BUTTON_RB)         out->wButtons |= XUSB_GAMEPAD_RIGHT_SHOULDER;
    if (state->buttons & GP_BUTTON_GUIDE)      out->wButtons |= XUSB_GAMEPAD_GUIDE;
    if (gamepad_state_swap_face_buttons) {
        if (state->buttons & GP_BUTTON_A)      out->wButtons |= XUSB_GAMEPAD_B;
        if (state->buttons & GP_BUTTON_B)      out->wButtons |= XUSB_GAMEPAD_A;
        if (state->buttons & GP_BUTTON_X)      out->wButtons |= XUSB_GAMEPAD_Y;
        if (state->buttons & GP_BUTTON_Y)      out->wButtons |= XUSB_GAMEPAD_X;
    } else {
        if (state->buttons & GP_BUTTON_A)      out->wButtons |= XUSB_GAMEPAD_A;
        if (state->buttons & GP_BUTTON_B)      out->wButtons |= XUSB_GAMEPAD_B;
        if (state->buttons & GP_BUTTON_X)      out->wButtons |= XUSB_GAMEPAD_X;
        if (state->buttons & GP_BUTTON_Y)      out->wButtons |= XUSB_GAMEPAD_Y;
    }
    out->sThumbLX     = state->lx;
    out->sThumbLY     = state->ly;
    out->sThumbRX     = state->rx;
    out->sThumbRY     = state->ry;
    out->bLeftTrigger  = state->lt;
    out->bRightTrigger = state->rt;
}
