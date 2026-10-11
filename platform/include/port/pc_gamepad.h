// A gamepad (SDL's game controller mapping, Xbox names) and the keyboard and
// mouse buttons as the Wii Remote and Nunchuk: flat mode's controls, and a
// gamepad's in the headset (platform/src/input/pc_gamepad.cpp).  PC only.
//
//   Gamepad                     Keyboard and mouse       Wii
//   left stick                  W A S D                  Nunchuk stick (and the remote's tilt)
//   A                           Space, left button       A
//   X or B                      E                        shake (spin)
//   right trigger               right button             B
//   left trigger                Shift                    Z
//   left shoulder               Q                        C
//   Start                       Esc, Enter               +
//   Back                        Tab                      -
//   right shoulder              1                        1
//                               2                        2
//   D-pad, right stick click    arrow keys               D-pad (the click: up)
#pragma once

#include <SDL.h>

#include "port/input.h"

namespace pcpad {

// After SDL_Init with SDL_INIT_GAMECONTROLLER: opens the first gamepad.
void init();
// Gamepads plugged in and out.
void handleEvent(const SDL_Event& e);
bool connected();

// Adds the gamepad's controls (and with `keyboard`, the keyboard's and the
// mouse buttons') to `pad`: buttons, the Nunchuk stick where `pad` has none,
// and a spin's shake.  Called once per input update.
void merge(PortPadState* pad, bool keyboard);
// The remote tipped by the left stick (up to 45 degrees each way), for where
// the game steers by tilt.
void tilt(PortPadState* pad);
// The right stick, past its dead zone: x right, y down, -1..1.
void rightStick(float* x, float* y);
// The game's rumble, on the gamepad.
void rumble(int on);

}  // namespace pcpad
