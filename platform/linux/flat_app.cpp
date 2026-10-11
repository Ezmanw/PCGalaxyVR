// Flat mode (`pcgalaxyvr --flat`): the game in a window on the desktop, from
// its own camera as on a TV, without a headset.  To try the game files and
// the port before putting a headset on, or to play without one.
//
// The picture is the giant screen's: the frame replayed into a 16:9 target
// (the game draws its 16:9 picture squeezed into its 640x456 frame) the size
// of the window's 16:9 part, then copied into the window, black bars round
// it.  The window is vsynced; with a display at a multiple of 60 Hz the
// game's retrace follows the display (each game frame shown the same number
// of refreshes), otherwise the game keeps its own 59.94 Hz clock.
//
// Input: a gamepad, or the keyboard and mouse, as the Wii Remote and Nunchuk
// (port/pc_gamepad.h).  The mouse is the pointer; with a gamepad the right
// stick moves it.  The left stick also tilts the remote, which the game reads
// where it steers by tilt (the Star Ball, Ray surfing) and nowhere else.
#include <SDL.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <string>
#include <vector>

#include "port/heap_routing.h"
#include "port/input.h"
#include "port/paths.h"
#include "port/pc_gamepad.h"
#include "port/pc_app.h"
#include "port/port.h"
#include "../src/gl/gl_context.h"
#include "../src/gx/gl_renderer.h"
#include "../src/xr/vr_renderer.h"

#if !PORT_GL_ES
#include <GL/glx.h>
#endif

extern "C" void port_boot(const char* dataRoot, const char* saveRoot);

namespace {

struct Flat {
    SDL_Window* window = nullptr;
    SDL_GLContext gl = nullptr;
    gpu::EfbTarget target;
    int viewX = 0, viewY = 0, viewW = 0, viewH = 0;  // the 16:9 picture in the window (pixels, top-left origin)
    // The pointer, -1..1 across the picture: the mouse's position, or moved
    // by the right stick.
    float pointerX = 0.0f, pointerY = 0.0f;
    bool pointerShown = false;
    int refreshes = 1;  // display refreshes per game frame (0: the game keeps its own clock)
    int refreshCount = 0;
};

Flat gFlat;

// The window's 16:9 part, and a target that size for the picture.
void layout(Flat& f) {
    int w = 0, h = 0;
    SDL_GL_GetDrawableSize(f.window, &w, &h);
    if (w <= 0 || h <= 0) return;
    int vw = w, vh = w * 9 / 16;
    if (vh > h) {
        vh = h;
        vw = h * 16 / 9;
    }
    f.viewX = (w - vw) / 2;
    f.viewY = (h - vh) / 2;
    f.viewW = vw;
    f.viewH = vh;
    // At most 4K: beyond it the picture only costs more.
    int tw = vw > 3840 ? 3840 : vw, th = tw * 9 / 16;
    if (f.target.width != tw || f.target.height != th) {
        if (f.target.fbo) gpu::renderer().destroyTarget(f.target);
        f.target = gpu::renderer().createTarget(tw, th);
        port_log("flat: picture %dx%d in a %dx%d window", tw, th, w, h);
    }
}

void updateInput(Flat& f, float dt) {
    PortPadState pad{};
    pad.connected = 1;
    pad.pointerDist = 2.0f;
    pcpad::merge(&pad, true);
    pcpad::tilt(&pad);

    // Pointer: the mouse over the picture (mouseMoved), or the right stick
    // moving it.
    float rx = 0.0f, ry = 0.0f;
    pcpad::rightStick(&rx, &ry);
    if (rx != 0.0f || ry != 0.0f) {
        f.pointerX = fminf(1.0f, fmaxf(-1.0f, f.pointerX + rx * 1.6f * dt));
        f.pointerY = fminf(1.0f, fmaxf(-1.0f, f.pointerY + ry * 1.6f * 16.0f / 9.0f * dt));
        f.pointerShown = true;
    }
    pad.pointerValid = f.pointerShown;
    pad.pointerX = f.pointerX;
    pad.pointerY = f.pointerY;
    port_input_set(0, &pad);
    pcpad::rumble(port_input_rumble(0));
}

void mouseMoved(Flat& f, int x, int y) {
    // Window coordinates to drawable pixels (they differ with scaling).
    int ww = 0, wh = 0, dw = 0, dh = 0;
    SDL_GetWindowSize(f.window, &ww, &wh);
    SDL_GL_GetDrawableSize(f.window, &dw, &dh);
    if (ww <= 0 || wh <= 0 || f.viewW <= 0) return;
    float px = x * (float)dw / ww, py = y * (float)dh / wh;
    float nx = (px - f.viewX) / f.viewW * 2.0f - 1.0f, ny = (py - f.viewY) / f.viewH * 2.0f - 1.0f;
    f.pointerShown = fabsf(nx) <= 1.0f && fabsf(ny) <= 1.0f;
    f.pointerX = fminf(1.0f, fmaxf(-1.0f, nx));
    f.pointerY = fminf(1.0f, fmaxf(-1.0f, ny));
}

void draw(Flat& f) {
    gpu::Renderer& r = gpu::renderer();
    bool fresh = r.update();
    if (fresh && r.hasFrame() && f.target.fbo) {
        r.render(f.target, nullptr, nullptr, gpu::HudMode::Inline);
    }
    int w = 0, h = 0;
    SDL_GL_GetDrawableSize(f.window, &w, &h);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, w, h);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    if (r.hasFrame() && f.target.fbo) {
        // The picture as it is (the game's colours are already for the
        // display): no sRGB conversion on the way.
        glDisable(GL_FRAMEBUFFER_SRGB);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, f.target.fbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        int y0 = h - f.viewY - f.viewH;  // GL's origin is at the bottom
        glBlitFramebuffer(0, 0, f.target.width, f.target.height, f.viewX, y0, f.viewX + f.viewW, y0 + f.viewH, GL_COLOR_BUFFER_BIT, GL_LINEAR);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glEnable(GL_FRAMEBUFFER_SRGB);
    }
}

// Display refreshes per game frame: the display's rate when it is a
// multiple of 60 Hz (within 1%), else 0 (the game keeps its own clock).
int refreshesPerFrame(SDL_Window* w) {
    SDL_DisplayMode mode{};
    if (SDL_GetWindowDisplayMode(w, &mode) != 0 || mode.refresh_rate <= 0) return 0;
    int n = (int)lroundf(mode.refresh_rate / 60.0f);
    if (n >= 1 && fabsf(mode.refresh_rate - 60.0f * n) <= 0.6f * n) return n;
    return 0;
}

}  // namespace

extern "C" int port_flat_main(const PortPcOptions* options) {
    PortHostAllocScope scope;
    Flat& f = gFlat;

    std::string data = port_data_dir();
    std::string saveRoot = data + "/nand";
    port_make_dirs(saveRoot);
    port_log_file((data + "/petari_log.txt").c_str());
    port_log("PCGalaxyVR %s, flat mode; log %s/petari_log.txt", PCGVR_VERSION, data.c_str());
    vr::loadSettings((port_config_dir() + "/petari_vr.ini").c_str());
    port_input_set_camera_inverted(vr::invertCamera());

    const char* only = options->gameDir ? options->gameDir : getenv("PETARI_GAME_ROOT");
    std::string dataRoot;
    bool ready = false;
    std::vector<std::string> tries = only ? std::vector<std::string>{only} : std::vector<std::string>{vr::gamePath(), data + "/game"};
    for (const std::string& dir : tries) {
        if (!dir.empty() && vr::isGameFolder(dir, &ready) && ready) {
            dataRoot = dir;
            break;
        }
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMECONTROLLER) != 0) {
        port_fatal("flat: cannot open a window: %s", SDL_GetError());
    }
    if (dataRoot.empty()) {
        std::string msg = "No converted game files were found in " + (only ? std::string(only) : data + "/game") +
                          ".\n\nConvert your disc with:\n  python3 tools/install_pc.py <your disc image>\n\nor start the game with --game <folder>.";
        port_log("flat: %s", msg.c_str());
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "PCGalaxyVR", msg.c_str(), nullptr);
        return 1;
    }

    for (int profile : {SDL_GL_CONTEXT_PROFILE_COMPATIBILITY, SDL_GL_CONTEXT_PROFILE_CORE}) {
        SDL_GL_ResetAttributes();
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 5);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, profile);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
        SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0);
        f.window = SDL_CreateWindow("PCGalaxyVR", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720,
                                    SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
        if (f.window) {
            f.gl = SDL_GL_CreateContext(f.window);
            if (f.gl) break;
            SDL_DestroyWindow(f.window);
            f.window = nullptr;
        }
    }
    if (!f.gl) {
        port_fatal("flat: cannot make an OpenGL 4.5 context: %s", SDL_GetError());
    }
    SDL_GL_MakeCurrent(f.window, f.gl);
    if (SDL_GL_SetSwapInterval(-1) != 0) SDL_GL_SetSwapInterval(1);  // adaptive vsync where there is one
#if !PORT_GL_ES
    glctx::setApi(glXGetCurrentContext() ? glctx::Api::Glx : glctx::Api::Egl);  // SDL picks GLX on X11, EGL on Wayland
#endif
    port_log("GL: %s / %s / %s", glGetString(GL_VENDOR), glGetString(GL_RENDERER), glGetString(GL_VERSION));
    glctx::applyDefaults();
    gpu::setShaderCachePath((port_cache_dir() + "/shaders.bin").c_str());
    gpu::renderer().init();
    layout(f);
    pcpad::init();
    f.refreshes = refreshesPerFrame(f.window);
    port_log("flat: %s", f.refreshes ? "the game's frames follow the display" : "the display is not at a multiple of 60 Hz: the game keeps its own clock");

    port_log("game files: %s", dataRoot.c_str());
    port_boot(dataRoot.c_str(), saveRoot.c_str());

    int64_t last = port_host_time_ns();
    bool quit = false, minimized = false;
    while (!quit) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
            case SDL_QUIT:
                quit = true;
                break;
            case SDL_CONTROLLERDEVICEADDED:
            case SDL_CONTROLLERDEVICEREMOVED:
                pcpad::handleEvent(e);
                break;
            case SDL_MOUSEMOTION:
                mouseMoved(f, e.motion.x, e.motion.y);
                break;
            case SDL_KEYDOWN:
                // F11 or Alt+Enter: full screen.
                if (e.key.keysym.sym == SDLK_F11 || (e.key.keysym.sym == SDLK_RETURN && (e.key.keysym.mod & KMOD_ALT))) {
                    bool full = SDL_GetWindowFlags(f.window) & SDL_WINDOW_FULLSCREEN_DESKTOP;
                    SDL_SetWindowFullscreen(f.window, full ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
                }
                break;
            case SDL_WINDOWEVENT:
                if (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                    layout(f);
                } else if (e.window.event == SDL_WINDOWEVENT_MINIMIZED || e.window.event == SDL_WINDOWEVENT_RESTORED) {
                    // Minimized, the game waits, as when the headset is taken off.
                    minimized = e.window.event == SDL_WINDOWEVENT_MINIMIZED;
                    port_set_paused(minimized);
                } else if (e.window.event == SDL_WINDOWEVENT_DISPLAY_CHANGED) {
                    f.refreshes = refreshesPerFrame(f.window);
                }
                break;
            default:
                break;
            }
        }
        int64_t now = port_host_time_ns();
        float dt = fminf(0.1f, (now - last) / 1e9f);
        last = now;
        updateInput(f, dt);
        if (minimized) {
            port_host_sleep_ns(50000000);
            continue;
        }
        draw(f);
        SDL_GL_SwapWindow(f.window);
        // The game's next frame starts on the refresh it is due.
        if (f.refreshes > 0 && ++f.refreshCount >= f.refreshes) {
            f.refreshCount = 0;
            port_vi_retrace();
        }
    }
    port_log("exiting");
    fflush(nullptr);
    _exit(0);
}
