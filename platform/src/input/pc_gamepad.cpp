// Gamepad, keyboard and mouse as the Wii Remote and Nunchuk (see
// port/pc_gamepad.h).  PC only.
#ifndef __ANDROID__
#include "port/pc_gamepad.h"

#include <math.h>

#include "port/port.h"

namespace pcpad {

namespace {

enum : uint32_t {
    W_LEFT = 0x0001, W_RIGHT = 0x0002, W_DOWN = 0x0004, W_UP = 0x0008, W_PLUS = 0x0010, W_2 = 0x0100, W_1 = 0x0200,
    W_B = 0x0400, W_A = 0x0800, W_MINUS = 0x1000, W_Z = 0x2000, W_C = 0x4000
};

SDL_GameController* sPad = nullptr;
int sSpinFrames = 0;
bool sSpinHeld = false;
int sRumble = 0;

float deadzone(float v) {
    const float dz = 0.18f;
    float a = fabsf(v);
    return a < dz ? 0.0f : copysignf((a - dz) / (1.0f - dz), v);
}

float axis(SDL_GameControllerAxis a) { return sPad ? SDL_GameControllerGetAxis(sPad, a) / 32767.0f : 0.0f; }
bool button(SDL_GameControllerButton b) { return sPad && SDL_GameControllerGetButton(sPad, b); }

void open() {
    if (sPad) return;
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        if (SDL_IsGameController(i) && (sPad = SDL_GameControllerOpen(i)) != nullptr) {
            port_log("input: gamepad %s", SDL_GameControllerName(sPad));
            return;
        }
    }
}

}  // namespace

void init() { open(); }

void handleEvent(const SDL_Event& e) {
    if (e.type == SDL_CONTROLLERDEVICEADDED) {
        open();
    } else if (e.type == SDL_CONTROLLERDEVICEREMOVED && sPad) {
        SDL_Joystick* j = SDL_GameControllerGetJoystick(sPad);
        if (!j || SDL_JoystickInstanceID(j) == e.cdevice.which) {
            port_log("input: gamepad unplugged");
            SDL_GameControllerClose(sPad);
            sPad = nullptr;
            sRumble = 0;
            open();
        }
    }
}

bool connected() { return sPad != nullptr; }

void merge(PortPadState* pad, bool keyboard) {
    const Uint8* key = keyboard ? SDL_GetKeyboardState(nullptr) : nullptr;
    auto down = [key](SDL_Scancode s) { return key && key[s]; };

    // The Nunchuk stick, where the headset's controllers give none.
    float sx = deadzone(axis(SDL_CONTROLLER_AXIS_LEFTX)), sy = -deadzone(axis(SDL_CONTROLLER_AXIS_LEFTY));
    if (down(SDL_SCANCODE_A)) sx -= 1.0f;
    if (down(SDL_SCANCODE_D)) sx += 1.0f;
    if (down(SDL_SCANCODE_W)) sy += 1.0f;
    if (down(SDL_SCANCODE_S)) sy -= 1.0f;
    float len = sqrtf(sx * sx + sy * sy);
    if (len > 1.0f) {
        sx /= len;
        sy /= len;
    }
    if (len > 0.0f && fabsf(pad->stickX) + fabsf(pad->stickY) < 0.05f) {
        pad->stickX = sx;
        pad->stickY = sy;
    }

    Uint32 mouse = keyboard ? SDL_GetMouseState(nullptr, nullptr) : 0;
    uint32_t b = 0;
    if (button(SDL_CONTROLLER_BUTTON_A) || down(SDL_SCANCODE_SPACE) || (mouse & SDL_BUTTON_LMASK)) b |= W_A;
    if (axis(SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 0.5f || (mouse & SDL_BUTTON_RMASK)) b |= W_B;
    if (axis(SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 0.5f || down(SDL_SCANCODE_LSHIFT) || down(SDL_SCANCODE_RSHIFT)) b |= W_Z;
    if (button(SDL_CONTROLLER_BUTTON_LEFTSHOULDER) || down(SDL_SCANCODE_Q)) b |= W_C;
    if (button(SDL_CONTROLLER_BUTTON_START) || down(SDL_SCANCODE_ESCAPE) || down(SDL_SCANCODE_RETURN)) b |= W_PLUS;
    if (button(SDL_CONTROLLER_BUTTON_BACK) || down(SDL_SCANCODE_TAB)) b |= W_MINUS;
    if (button(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) || down(SDL_SCANCODE_1)) b |= W_1;
    if (down(SDL_SCANCODE_2)) b |= W_2;
    if (button(SDL_CONTROLLER_BUTTON_DPAD_LEFT) || down(SDL_SCANCODE_LEFT)) b |= W_LEFT;
    if (button(SDL_CONTROLLER_BUTTON_DPAD_RIGHT) || down(SDL_SCANCODE_RIGHT)) b |= W_RIGHT;
    if (button(SDL_CONTROLLER_BUTTON_DPAD_UP) || button(SDL_CONTROLLER_BUTTON_RIGHTSTICK) || down(SDL_SCANCODE_UP)) b |= W_UP;
    if (button(SDL_CONTROLLER_BUTTON_DPAD_DOWN) || down(SDL_SCANCODE_DOWN)) b |= W_DOWN;
    pad->buttons |= b;

    // Spin: the acceleration spike of a shaken remote, for a few updates
    // from the press (as the headset's flick of a controller).
    bool spin = button(SDL_CONTROLLER_BUTTON_X) || button(SDL_CONTROLLER_BUTTON_B) || down(SDL_SCANCODE_E);
    if (spin && !sSpinHeld) sSpinFrames = 6;
    sSpinHeld = spin;
    if (sSpinFrames > 0) {
        float s = (sSpinFrames & 1) ? 2.5f : -2.5f;
        pad->accX = s;
        pad->accY = s;
        sSpinFrames--;
    }
}

void tilt(PortPadState* pad) {
    // x right, y up, z where the remote points (port/input.h): pushing the
    // stick forward points the remote down, pushing it right rolls it right.
    float roll = pad->stickX * 0.785f, pitch = pad->stickY * 0.785f;
    pad->tilted = 1;
    pad->downX = sinf(roll);
    pad->downZ = sinf(pitch);
    pad->downY = -sqrtf(fmaxf(0.0f, 1.0f - pad->downX * pad->downX - pad->downZ * pad->downZ));
}

void rightStick(float* x, float* y) {
    *x = deadzone(axis(SDL_CONTROLLER_AXIS_RIGHTX));
    *y = deadzone(axis(SDL_CONTROLLER_AXIS_RIGHTY));
}

void rumble(int on) {
    if (sPad && on != sRumble) {
        SDL_GameControllerRumble(sPad, on ? 0x6000 : 0, on ? 0x6000 : 0, on ? 10000 : 0);
        sRumble = on;
    }
}

}  // namespace pcpad
#endif
