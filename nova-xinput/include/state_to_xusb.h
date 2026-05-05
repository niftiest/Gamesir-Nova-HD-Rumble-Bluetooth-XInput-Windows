/*
 * state_to_xusb.h -- Translate the generic gamepad_state_t into ViGEmBus's
 * XUSB_REPORT (Xbox 360 wire format). Pure function.
 */

#ifndef STATE_TO_XUSB_H
#define STATE_TO_XUSB_H

#include "gamepad.h"
#include <windows.h>
#include <stdbool.h>
#include <ViGEm/Common.h>

/* When true, A<->B and X<->Y are swapped on output. Use this to make
 * Nintendo-layout controllers (where the physical A/B and X/Y positions are
 * inverted vs Xbox) feel correct in Xbox-layout games. Mutated at runtime
 * by --swap-face-buttons CLI flag and the tray menu toggle. */
extern bool gamepad_state_swap_face_buttons;

void gamepad_state_to_xusb(const gamepad_state_t *state, XUSB_REPORT *out);

#endif
