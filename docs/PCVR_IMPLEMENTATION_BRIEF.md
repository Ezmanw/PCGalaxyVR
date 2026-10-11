# PCGalaxyVR: implementation brief

How GalaxyQuest, the native Meta Quest port of Super Mario Galaxy, becomes a
PC VR game. Linux (x86-64) first; Windows later. This is the plan the work in
this repository follows, and the record of why each decision was made.

## Goal

Run the same game (the Petari decompilation plus the platform layer) as a
native Linux x86-64 program that presents in any PC VR headset through
OpenXR, keeping every feature of the Quest version that does not depend on
Meta's hardware: the diorama, the giant screen and its stereoscopic 3D, the
HUD and settings panels, cutscene skipping, the controller pointer, the
setup screen, saves.

Out of scope for the first release: Windows, macOS, Vulkan.

Added along the way: a **flat mode** (`--flat`), the game in a desktop
window from its own camera with a gamepad or keyboard and mouse, so the
game files and the port can be checked without a headset (and played
without one).

## Where we start

The Quest port is already most of a PC port:

| Piece | State on Quest | What PC needs |
|---|---|---|
| `decomp/` | Built with the NDK's clang and libc++ for arm64, little-endian and 64-bit fixes done | Same compiler family (clang) and libc++; glibc and case-sensitive file system fixes |
| Memory window (`os_mem.cpp`) | memfd + `mmap` at the Wii's addresses; Linux-ready | The game image itself must sit at 0x98000000 (see below) |
| Threads, alarms, DVD, NAND, input, DSP mixer, THP | Portable C++ and POSIX | Nothing, or log/path changes |
| Renderer (`gl_renderer.cpp`, `gl_shadergen.cpp`) | OpenGL ES 3.2 | Desktop OpenGL 4.5: a few GLSL headers, state enables, extension names |
| VR presentation (`vr_game.cpp`, `vr_rig.cpp`, panels) | OpenXR + GLES | Same code on desktop GL |
| OpenXR app (`xr_app.cpp`) | Android loader, EGL, Touch Plus bindings, 120 Hz pacing | GLX binding, PC controller bindings, any refresh rate, desktop main loop |
| Audio out (`aaudio_out.cpp`) | AAudio | SDL2 audio |
| Launcher (`platform/android/`) | Reserves the window, `android_dlopen_ext` at a fixed address | A fixed-address executable |
| Data conversion (`tools/cook/`) | Python, cross-platform | Unchanged; write straight into the PC data folder instead of `adb push` |

## Decisions

### Runtimes: SteamVR, Monado, WiVRn

The Linux OpenXR runtimes players actually use are SteamVR (Valve Index,
Vive, ALVR streaming to Quest/Pico), Monado (native drivers) and WiVRn
(Monado-based streaming to Quest/Pico). The port targets the OpenXR core
plus the graphics binding they all share, and treats every vendor extension
as optional.

### Graphics: desktop OpenGL 4.5 through GLX (`XR_KHR_opengl_enable`)

- On Linux no runtime offers `XR_KHR_opengl_es_enable` (its binding is
  Android-only). Monado's `XR_MNDX_egl_enable` would take an EGL context but
  SteamVR does not have it.
- `XR_KHR_opengl_enable` with the Xlib binding works on SteamVR and Monado
  (and so WiVRn). It needs a GLX context: X11, or XWayland under Wayland.
- Desktop GL 4.5 is a superset of what the renderer uses from GLES 3.2
  (storage buffers, program binaries, `glInvalidateFramebuffer`,
  `glTexStorage`). A **compatibility profile** is requested so GLES-like
  defaults hold (vertex array object 0, unsized formats); core profile is
  the fallback.
- Differences handled in one place (`platform/include/port/gl.h` and
  `platform/src/gl/`): GLSL `#version 320 es` becomes `#version 450 core`
  (precision qualifiers are legal there); `GL_FRAMEBUFFER_SRGB` and seamless
  cube maps are enabled once, because GLES always does both; timer queries
  use `GL_ARB_timer_query` instead of `GL_EXT_disjoint_timer_query`; Qualcomm
  foveation is skipped; `eglGetProcAddress` becomes a lookup that works for
  EGL or GLX.
- The shader compile thread's shared context comes from whichever API made
  the main context (EGL headless, GLX in the headset).
- Vulkan was rejected for now: it means rewriting a 3,000-line renderer.
  Fallback if a runtime ever drops GL: render in GL into Vulkan-allocated
  images (`GL_EXT_memory_object_fd`).

### Window, audio and desktop input: SDL2

SDL2 is in every distribution, makes the GLX context (forced to its X11
driver), opens a mirror window on the desktop showing the left eye, and
plays audio (replacing AAudio with the same ring buffer and callback model).
A desktop window also gives the session something to close.

### Toolchain: clang and libc++, linked statically

The decomp was written against the NDK's clang and libc++. libstdc++
still defines `std::mem_fun`, `binder2nd` and friends, which the decomp's
MSL headers define again; libc++ in C++20 mode does not. So the Linux build
uses clang (17 or newer) with `-stdlib=libc++`, linked statically so the
binary needs no libc++ at run time.

### Address space: a fixed-address executable

Game code keeps pointers in 32-bit fields, so MEM1/MEM2 live at their Wii
addresses (0x80000000/0x90000000) and the game's own code and data at
0x98000000. On Android a launcher reserves the window and loads `libgame.so`
there with `android_dlopen_ext`; glibc's `dlopen` cannot place a library.

On Linux the game is one non-PIE executable (`pcgalaxyvr`), compiled as
position-independent code (so nothing needs 32-bit absolute relocations
above 2 GB) and linked with its image at 0x98000000. A zero-fill section
placed at 0xA0000000 runs to 0xE0010000, covering the low pool, the
uncached mirrors and the locked cache. Because it is part of the image, the
kernel puts the `brk` heap after it, and nothing else can land in the window
before `main`. At start-up that section is remapped as an uncommitted
`PROT_NONE` reservation, and `os_mem.cpp` maps into it as it does into the
Android launcher's reservation.

### Files: XDG folders

| What | Quest | Linux |
|---|---|---|
| Converted game files | `/sdcard/Android/data/com.galaxy.quest/files/game` | `$XDG_DATA_HOME/PCGalaxyVR/game` (`~/.local/share/PCGalaxyVR/game`), or `--game DIR` |
| Saves | app-private `nand/` | `$XDG_DATA_HOME/PCGalaxyVR/nand` |
| Settings (`petari_vr.ini`), log, shader cache | app files | `$XDG_CONFIG_HOME/PCGalaxyVR/`, `$XDG_CACHE_HOME/PCGalaxyVR/` |
| Debug variables (`petari_debug.env`) | app files | the data folder, or ordinary environment variables |

The setup screen (shown when no converted files are found) searches the
data folder and the home folder a few levels deep instead of Android's
shared storage; there is no storage permission to ask for.

### Controllers

Bindings are suggested for every common PC controller; the action set and
the Wii mapping stay as on Quest.

| Action (Wii) | Touch / Touch Plus | Valve Index | HTC Vive | WMR / Reverb | Simple controller |
|---|---|---|---|---|---|
| Move (Nunchuk stick) | L stick | L stick | L trackpad | L stick | – |
| Jump (A) | A | R A | R trackpad click | R trackpad click | R select |
| Spin (shake) | B, Y, flick | R B, L B, flick | flick, R grip | R grip, flick | flick |
| Shoot / cancel (B) | R trigger | R trigger | R trigger | R trigger | – |
| Crouch (Z) | L trigger | L trigger | L trigger | L trigger | – |
| Camera (C) | L grip | L grip | L grip | L grip | – |
| Pause (+/−) | Menu, X | L A | menu (either) | menu (either) | menu |
| D-pad / turn | R stick | R stick | R trackpad | R stick | – |
| Pointer | R aim | R aim | R aim | R aim | R aim |

Recentring is the runtime's own (SteamVR dashboard, Monado/WiVRn recentre).

### Frame pacing on PC refresh rates

The Quest pacing has three modes: 120 Hz paired refreshes, SpaceWarp, and
"anything else" (both eyes every refresh, the game on its own 59.94 Hz
clock). On PC the paired mode is kept for 120 Hz (Index, Quest over
WiVRn/ALVR); 90 and 144 Hz use the free-running mode, which shows some
judder on moving things, so the README recommends 120 Hz. A desktop GPU
draws both eyes in a fraction of a refresh, so the per-eye splitting the
Quest needed is harmless but not required.

### Meta-only features

SpaceWarp (`XR_FB_space_warp`), passthrough (`XR_FB_passthrough`), Super
Resolution (`XR_FB_composition_layer_settings`), performance counters and
levels, and refresh rate control are all queried at start-up and switch
themselves off when the runtime lacks them, as they already do on older
Quest runtimes. Their settings are hidden or inert. (Monado does offer
`XR_FB_display_refresh_rate`.)

## Work plan

Each phase ends with something that can be checked.

1. **Rebrand.** README, docs and CI describe PCGalaxyVR, a Linux PC VR fork
   of GalaxyQuest. The Quest-only installer workflow goes; a Linux build
   workflow comes.
2. **Compile on Linux.** All decomp and platform sources build with clang and
   libc++ on x86-64: case-sensitive includes, glibc's `wchar.h` and
   `float.h`, `-fsigned-char`, no Android headers outside
   `#ifdef __ANDROID__`. *Check:* `ninja` builds every object.
3. **Desktop GL.** The `port/gl.h` header and `platform/src/gl/` helpers;
   renderer, VR presentation and panels compile and run on GL 4.5. *Check:*
   shaders compile and link on Mesa (llvmpipe) and a real GPU.
4. **Executable and headless runner.** `pcgalaxyvr` links at 0x98000000 with
   the window reservation; `pcgalaxyvr --headless DATA SAVES SECONDS` runs the
   game without a headset through EGL (surfaceless) and writes PNG captures,
   like the Quest's `petari_headless`. *Check:* boots to the strap screen
   with game files; with none, exits with a clear message.
5. **OpenXR app.** `pcgalaxyvr` opens an SDL2/GLX context and a mirror
   window, creates the OpenXR session with the Xlib binding, swapchains
   (`XrSwapchainImageOpenGLKHR`), PC controller bindings, the frame loop at
   any refresh rate, and quits cleanly on window close or session exit.
   *Check:* runs on Monado's simulated headset and on real hardware.
6. **Audio, paths, setup screen.** SDL2 audio output; XDG folders; setup
   screen search on the PC file system.
7. **Flat mode.** `pcgalaxyvr --flat`: the giant screen's picture in an SDL
   window, vsynced, the game's retrace following displays at multiples of
   60 Hz; gamepad, keyboard and mouse as the Wii Remote and Nunchuk
   (`platform/linux/flat_app.cpp`).
8. **Install tooling.** `tools/install_pc.py` converts a disc image or
   extracted disc (via `cook.py`) straight into the data folder; no adb.
9. **Packaging.** Release tarball with the binary and the converter; an
   AppImage later.
10. **Windows (future).** WGL binding (`XrGraphicsBindingOpenGLWin32KHR`),
   `VirtualAlloc` for the window, `/BASE:0x98000000 /FIXED` for the image,
   clang-cl or MinGW with libc++, WASAPI through SDL2. Nothing in phases 1-8
   should make this harder: platform code goes behind small interfaces
   (`port/gl.h`, the context helpers, the audio backend, the paths).

## Risks

- **No headset or game files in the development environment.** The cloud
  container that does this work has no VR hardware and, rightly, no
  Nintendo data. Compilation, linking, the address window and start-up up
  to "no game files" can be checked there; rendering, sound and play cannot.
  Every phase that needs them is marked as needing a test by a player.
- **GLSL ES vs desktop GLSL** edge cases, and driver differences (NVIDIA,
  AMD and Intel on Mesa). The shader generator's output is checked on
  llvmpipe at least.
- **SteamVR's OpenGL path on Linux** is less used than its Vulkan path;
  bugs there may need workarounds (or the GL-on-Vulkan-images fallback).
- **Address window collisions**: kernels with unusual `mmap` layouts, or
  strict overcommit (`vm.overcommit_memory=2`) refusing the reservation
  section at exec. The executable checks the window at start-up and says
  what is in the way.
- **Judder at 90 and 144 Hz** until the pacing learns non-multiples of 60.

## Verification

Done so far, in the development container (no headset, no game files):

- the program builds with clang 18 and libc++ and links at 0x98000000;
- `--gl-selftest` builds the VR layer's and the renderer's programs and
  256 generated GX programs on Mesa's desktop OpenGL 4.5 (llvmpipe);
- the headless runner maps MEM1, MEM2 and the uncached mirrors at the
  Wii's addresses and stops at the disc check without game files;
- against Monado 21 (simulated headset, null compositor, Xvfb for GLX):
  instance, session and swapchains come up, the runtime accepts the
  bindings of all five controller profiles, the session reaches
  FOCUSED, the frame loop runs and draws, the setup screen appears, and
  closing the window ends the session cleanly;
- flat mode opens and, without game files, says how to convert them.

From a player's log and SteamVR's own logs (SteamVR 2.18.3, PS VR2,
RTX 3060, 90 Hz only): SteamVR turns quad layers into OpenVR overlays,
which cannot be limited to one eye; the stereo screen's pair of layers
looked wrong there, so SteamVR gets the panels and screen in the eye
images (see TECHNICAL.md, "Quad layers and SteamVR").

Still to check, by a player: everything with game files, in a real
headset (SteamVR, Monado, WiVRn), and the 120 Hz pacing.

Planned checks:

- CI: build on Ubuntu with clang and libc++; run `pcgalaxyvr --version` and
  the headless runner's no-data path.
- Locally with game files: `pcgalaxyvr --headless` captures (as
  `tools/galaxy_tour.sh` does on Quest).
- In a headset: Monado/WiVRn and SteamVR; the README's checklist (title,
  file select, prologue, diorama, giant screen, settings panel, audio).
