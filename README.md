> **Disclaimer: this project is made by AI.** PCGalaxyVR, like
> [GalaxyQuest](https://github.com/bigmak94/GalaxyQuest) that it is forked
> from, was written by AI in Claude Code: its code, the changes it makes to
> the decompilation, its tools and this documentation. The Super Mario
> Galaxy decompilation it is built on is the work of the people of the
> Petari team.

<p align="center">
  <img src="docs/images/cover.jpg" alt="PCGalaxyVR: a spiral galaxy" width="380">
</p>

# PCGalaxyVR

**Play Super Mario Galaxy in virtual reality on your PC: a native Linux port
for any OpenXR headset.** (Windows support is planned.)

PCGalaxyVR is the PC VR version of
[GalaxyQuest](https://github.com/bigmak94/GalaxyQuest), the native Meta
Quest port. Mario's universe becomes a living diorama in front of you:
planets float at arm's length, you lean in to look around them, and you aim
at star bits with your own hand. If you prefer the original camera, the
whole game also plays on a giant virtual screen, just like on a TV.

This is a **native port, not an emulator**. The game's own code, recreated
in C++ by the [Petari decompilation project](https://github.com/SMGCommunity/Petari),
is compiled for your PC's processor. A platform layer stands in for the
Wii's hardware (graphics, sound, controllers, saves) and presents the game
in VR through OpenXR, so it runs on SteamVR, Monado and WiVRn: Valve Index,
HTC Vive, Windows Mixed Reality headsets, and Quest or Pico headsets
streaming from the PC (WiVRn, ALVR).

![Mario on a pirate ship in the diorama view](docs/images/diorama.jpg)

> **You need your own copy of Super Mario Galaxy.** Neither this repository
> nor the program contains any game data. You convert your own disc once (its
> files, and the few pieces of data its program holds), and the game reads
> the result from your computer.

## Status: early, help wanted

The PC port is new and in progress. Its code compiles on Linux;
**it has not been played in a headset yet.** Everything below the platform
layer (the game, the diorama, the giant screen, the menus) is the Quest
version's, which plays from start to finish. If you try it, please report
what happens in the [issues](../../issues), with the log
(`~/.local/share/PCGalaxyVR/petari_log.txt`). The plan, and what is left,
is in [docs/PCVR_IMPLEMENTATION_BRIEF.md](docs/PCVR_IMPLEMENTATION_BRIEF.md).

## What's different from the Wii version

| | On the Wii | In this port |
|---|---|---|
| Presentation | TV picture from the game camera | A 3D diorama in front of you, or a giant 16:9 virtual screen |
| Picture | 640x456 at 60 frames a second | Each eye rendered at about the headset's own resolution (it adapts to the load) |
| Pointing | Wii Remote pointer on the TV | A laser from the right controller that reaches into the world |
| Spin | Shake the Wii Remote | A button, or a flick of either controller |
| Star Ball and Ray | Tilt the Wii Remote | Tilt the right controller |
| Cutscenes and dialogues | Skippable in some places | Hold A to skip any cutscene or a whole dialogue |
| Luigi | After collecting all 120 stars | On any save file from the start |
| Miis | Can be save file icons | Not available: pick one of the game's icons |

**The diorama.** Each level is shown at 1/500 scale on an invisible
tabletop, with Mario about 1.5 m in front of you and a little below eye
level. The world follows him smoothly and turns with his gravity, so he
always stands upright, even when running round a tiny planet. You look
around with your own head, and the right stick turns the view in 45 degree
steps. When a wall comes between you and Mario, the part in the way fades
out. Galaxy intros, launch star flights and a few special views play on a
big virtual screen, and the pause menu and the HUD float on a panel in front
of you.

**The giant screen.** The game starts on a giant 16:9 screen, from the
game's own camera, as it did on a TV; switch *Giant screen* off in the VR
settings (next to the pause menu) for the diorama. The settings panel also
sets how far away the screen is. Switch *Stereoscopic 3D* on there and each
eye gets its own picture, as in a 3D cinema: the game's world gets real
depth, far behind the screen and out in front of it.

**Comfort.** Turning happens in steps behind a short blink, sudden changes
of gravity also happen behind a blink, and the edges of the view darken
while the world turns. How far away Mario stands, how quickly the world
follows him, which way the right stick turns the camera and more can be set
on the VR settings panel: see [docs/CONTROLS.md](docs/CONTROLS.md).

**Smoothness.** The game runs at 60 frames a second. At 120 Hz each game
frame is shown for exactly two refreshes, which looks smoothest; at 90 or
144 Hz moving things judder a little. Set your headset to 120 Hz if it can.

**Languages.** The game plays in any language your disc has (English,
French, German, Spanish or Italian on the European disc; English, French or
Spanish on the American one): pick it on the *Game* tab of the VR settings.

**Everything else is the original game.** Levels, physics, enemies, music,
story and saves all come from the game's own code and your own game files.

Some Quest features need Meta's runtime and are not on PC: Application
SpaceWarp, passthrough around the giant screen, and Meta Quest Super
Resolution. Their settings do nothing here.

![The pause menu with the VR settings panel](docs/images/pause_settings.jpg)

## What you need

- A **Linux** PC (x86-64) with a graphics card that does **OpenGL 4.5**
  (any NVIDIA, AMD or Intel card of the last ten years, with Mesa or
  NVIDIA's driver), running **X11**, or Wayland with XWayland (the usual
  setup).
- A **PC VR headset** and an **OpenXR runtime** for it:
  [SteamVR](https://store.steampowered.com/app/250820/SteamVR/) (Index,
  Vive, ALVR), [Monado](https://monado.freedesktop.org/) or
  [WiVRn](https://github.com/WiVRn/WiVRn) (Quest and Pico over Wi-Fi).
- **Your Super Mario Galaxy disc**, dumped as an ISO, RVZ or WBFS image (for
  example with [CleanRip](https://wiibrew.org/wiki/CleanRip) on a Wii), or
  already extracted. The European (RMGP01) and American (RMGE01) discs are
  tested; the Japanese and Korean ones have not been tried yet.
- [Dolphin](https://dolphin-emu.org/) (its `dolphin-tool`) to extract a disc
  image, and Python 3.8 or newer for the converter.
- About 4 GB of free space (7 GB while converting).

## Installing

There are no release downloads yet: build the program from source (below;
a few commands), or take the `PCGalaxyVR-linux-x86_64` archive from the
latest run of the repository's *Linux build* workflow (under *Actions*).

### 1. Convert your disc

```
python3 tools/install_pc.py "Super Mario Galaxy.rvz"
```

This extracts the disc with Dolphin's `dolphin-tool` (found on the `PATH`,
or in the Dolphin Flatpak; `--dolphin-tool PATH` otherwise), converts the
game files and puts them in `~/.local/share/PCGalaxyVR/game`. Allow a few
minutes. If your disc is already extracted (Dolphin: *Properties*,
*Filesystem*, right-click the disc, *Extract Entire Disc*), give that
folder instead. `--without-movies` leaves out the prologue and ending
movies (2.3 GB less; they play as a black screen).

The converter alone is `tools/cook/cook.py EXTRACTED OUTPUT --with-movies`,
if you prefer to put the files somewhere else (then start the game with
`--game OUTPUT`, or pick the folder on its setup screen).

### 2. Play

Start your OpenXR runtime (SteamVR, Monado or WiVRn) with the headset
connected, then:

```
build/pcgalaxyvr
```

The game starts on a giant screen in front of you, and a window on the
desktop shows what the left eye sees (`--no-mirror` keeps it out of the
way). Menu or X (left A on an Index) opens the pause menu, with the VR
settings beside it.

If the game finds no converted files, it shows a **setup screen** in the
headset listing the folders in your home folder that hold them: aim at one
and press A to play from it (the game remembers it).

### Without a headset: flat mode

```
build/pcgalaxyvr --flat
```

plays the game in a window on the desktop, from the game's own camera as on
a TV, with a gamepad or the keyboard and mouse (F11 or Alt+Enter: full
screen). It is the quickest way to check that your converted files work, and
a way to play without a headset. The VR settings panel is the headset's, so
in flat mode settings come from `~/.config/PCGalaxyVR/petari_vr.ini` (the
language, the cutscene skip, the camera's turns).

| Wii | Gamepad (Xbox names) | Keyboard and mouse |
|---|---|---|
| Nunchuk stick | Left stick | W A S D |
| A | A | Space, left mouse button |
| Shake (spin) | X or B | E |
| B | Right trigger | Right mouse button |
| Pointer | Right stick moves it | The mouse |
| Z | Left trigger | Shift |
| C | Left shoulder | Q |
| + (pause) | Start | Esc or Enter |
| − | Back | Tab |
| 1 / 2 | Right shoulder / – | 1 / 2 |
| D-pad | D-pad (right stick click: up) | Arrow keys |
| Tilt (Star Ball, Ray surfing) | Left stick | W A S D |

### Where things are

| What | Where |
|---|---|
| Converted game files | `~/.local/share/PCGalaxyVR/game` (or `--game DIR`) |
| Saves | `~/.local/share/PCGalaxyVR/nand` |
| Log | `~/.local/share/PCGalaxyVR/petari_log.txt` |
| VR settings | `~/.config/PCGalaxyVR/petari_vr.ini` |
| Shader cache | `~/.cache/PCGalaxyVR/shaders.bin` |

(`$XDG_DATA_HOME`, `$XDG_CONFIG_HOME` and `$XDG_CACHE_HOME` move them.)

### If something goes wrong

- **"no OpenXR runtime answered"**: start SteamVR, Monado or WiVRn first.
  The runtime the game talks to is the one
  `~/.config/openxr/1/active_runtime.json` (or
  `/etc/xdg/openxr/1/active_runtime.json`) points to; SteamVR sets itself
  there from its settings (*OpenXR*, *Set SteamVR as OpenXR runtime*).
- **"cannot open a window on the X11 display"**: the game needs X11 or
  XWayland (OpenXR's OpenGL support on Linux goes through X11).
- **The window says "waiting for the headset"**: the runtime runs but sees
  no headset: connect it, or wake it up.
- **A setup screen instead of the game**: no converted files where the game
  looks: run step 1, or pick the folder on that screen.

## Controls

| Wii | Touch / Quest | Valve Index | Vive wands | WMR | In the game |
|---|---|---|---|---|---|
| Nunchuk stick | Left stick | Left stick | Left trackpad | Left stick | Move |
| A | A | Right A | Right trackpad click | Right trackpad click | Jump, talk, confirm. Hold to float as Boo Mario or skip a cutscene or a dialogue |
| Shake | B or Y, or a flick | Right B or left B, or a flick | Right grip, or a flick | Right grip, or a flick | Spin |
| B | Right trigger | Right trigger | Right trigger | Right trigger | Shoot star bits, cancel |
| Pointer | Right aim | Right aim | Right aim | Right aim | Collect star bits, grab Pull Stars, point at menus |
| Z | Left trigger | Left trigger | Left trigger | Left trigger | Crouch, ground pound, long and back flip jumps |
| C | Left grip | Left grip | Left grip | Left grip | Put the game camera behind Mario |
| − and + | X or Menu | Left A | Menu | Menu | Pause menu (with the VR settings) |
| D-pad | Right stick | Right stick | Right trackpad | Right stick | Turn the view round Mario (left/right), first-person look (up) |

To recentre the view, use your runtime's own recentre (SteamVR's dashboard,
or the headset's button with WiVRn). SteamVR can rebind everything in its
controller settings. All the details, and the settings file, are in
[docs/CONTROLS.md](docs/CONTROLS.md).

## Building from source

You need clang and lld 17 or newer with LLVM's libc++, CMake 3.24 or newer,
Ninja, git, and the SDL2, OpenGL/EGL and X11 development files:

- **Ubuntu / Debian:** `sudo apt install clang lld libc++-dev libc++abi-dev cmake ninja-build git libsdl2-dev libgl-dev libopengl-dev libegl-dev libx11-dev`
- **Fedora:** `sudo dnf install clang lld libcxx-devel libcxxabi-devel cmake ninja-build git SDL2-devel mesa-libGL-devel mesa-libEGL-devel libX11-devel`
- **Arch:** `sudo pacman -S clang lld libc++ cmake ninja git sdl2 libglvnd libx11`

```
git clone https://github.com/Ezmanw/PCGalaxyVR.git
cd PCGalaxyVR
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
cmake --build build
```

The first configure fetches the Khronos OpenXR SDK with git (its loader is
built into the program); `-DPCGVR_SYSTEM_OPENXR=ON` uses an installed OpenXR
SDK 1.1 instead. The build takes a while: the game is about 1,800 source
files. `build/pcgalaxyvr --gl-selftest` checks, without a headset or game
files, that your graphics driver builds all of the renderer's shaders.

## How it works

- `decomp/` holds the [Petari](https://github.com/SMGCommunity/Petari)
  sources with the changes the port needs: little-endian and 64-bit fixes
  and the hooks of the VR presentation, all marked `TARGET_PC`.
  `git diff 4b395ef -- decomp` shows them against the original (the commit
  that imported Petari unchanged). Left out of this repository: the few
  pieces of game data Petari keeps from the game's executable (the error
  screens' archive and two tables), which the converter takes from your own
  disc instead.
- `platform/` replaces the Wii: memory laid out as on the console, threads,
  the disc and saves, a graphics processor that turns the game's GX
  commands into OpenGL (desktop OpenGL 4.5 on PC, OpenGL ES 3.2 on Quest),
  the DSP that mixes the sound, controllers, and the OpenXR app with the VR
  presentation (`platform/src/xr/`). `platform/linux/` is the PC program's
  entry point; `platform/android/` is the Quest's, from GalaxyQuest.
- The game keeps pointers in 32-bit fields, so the program is linked at a
  fixed address (0x98000000) next to the Wii's memory (0x80000000 and
  0x90000000); see `platform/linux/main.cpp`.
- `tools/cook/` converts the game's files to the layout the port reads.

[docs/TECHNICAL.md](docs/TECHNICAL.md) goes through it all in detail, with
the development tools (the headless runner and its switches).

## Known gaps

- Not yet played in a headset on PC (see *Status* above).
- Linux only; Windows is planned.
- Miis are not available (there is no console Mii database): you pick an
  icon for a save file instead.
- The HOME menu is not implemented; the runtime's own menu takes its place.
- The sun's lens flare doesn't show (it reads the picture back, which the
  port's renderer can't).
- The Japanese and Korean discs have not been tested.

## Thanks

PCGalaxyVR is a fork of [GalaxyQuest](https://github.com/bigmak94/GalaxyQuest),
whose Quest port is the whole of the game, the VR presentation and the tools
here: thank you!

Both stand entirely on the work of the
**[Petari](https://github.com/SMGCommunity/Petari) team** and the
**SMGCommunity**, who spent years decompiling Super Mario Galaxy back into
readable source code and gave it to everyone under CC0. Without their
patient work none of this would exist: thank you!

Thanks as well to:

- the [Dolphin](https://dolphin-emu.org/) team, whose documentation of the
  Wii's graphics and audio hardware guided the platform layer, and whose
  tools extract the disc;
- AMD for [FidelityFX CAS](https://github.com/GPUOpen-Effects/FidelityFX-CAS),
  the Inter Project for the [Inter](https://github.com/rsms/inter) typeface
  of the VR panels, the Khronos Group for the OpenXR SDK, and the SDL
  project (their licenses are in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md));
- Nintendo, for Super Mario Galaxy.

Super Mario Galaxy, Mario and their logos are trademarks of Nintendo.
PCGalaxyVR is an unofficial fan project, not affiliated with or endorsed
by Nintendo, and it includes no Nintendo game data.

## License

PCGalaxyVR is open source and free for everyone: its code, tools and
documentation are released into the public domain under
[The Unlicense](LICENSE). Copy them, change them, use them, share them or
sell them as you please, for any purpose, without asking anyone.

The decompilation in `decomp/` is the Petari team's work, dedicated to the
public domain under CC0 1.0 ([decomp/LICENSE-Petari.txt](decomp/LICENSE-Petari.txt));
the port's changes to it are under the Unlicense like the rest. The few
third-party parts keep their own licenses: see
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
