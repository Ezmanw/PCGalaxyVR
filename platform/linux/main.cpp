// PCGalaxyVR on Linux: the program's entry point.
//
// The game keeps pointers in 32-bit fields, so its memory sits at the Wii's
// addresses (port/port.h): MEM1 at 0x80000000, MEM2 at 0x90000000, this
// executable's own code and data at 0x98000000 (it is linked there), the low
// pool at 0xA0000000, the uncached mirrors at 0xC0000000 and 0xD0000000 and
// the locked cache at 0xE0000000.  Everything from the low pool up is a
// zero-fill section of the executable (window.S, placed by window.ld), so
// nothing else, the brk heap included, can be there by the time main runs;
// main turns it into an uncommitted reservation that os_mem.cpp maps into,
// as it does into the Quest launcher's.
#include <X11/Xlib.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "port/pc_app.h"
#include "port/port.h"

#ifndef PCGVR_VERSION
#define PCGVR_VERSION "dev"
#endif

extern "C" char pcgvr_window[];  // window.S

namespace {

const uintptr_t kTailBase = PORT_LOWPOOL_BASE;
const uintptr_t kTailEnd = 0xE0010000u;  // the locked cache's 16 KiB, rounded up

void reserveWindow() {
    uintptr_t image = (uintptr_t)&reserveWindow;
    if (image < PORT_LIB_BASE || image >= PORT_LIB_BASE + PORT_LIB_SIZE) {
        fprintf(stderr, "pcgalaxyvr: the program is not at its address (code at %p, wants 0x%08x): it was not linked with "
                        "platform/linux/window.ld\n",
                (void*)image, PORT_LIB_BASE);
        exit(1);
    }
    if ((uintptr_t)pcgvr_window != kTailBase) {
        fprintf(stderr, "pcgalaxyvr: the address window section is at %p instead of 0x%08lx\n", (void*)pcgvr_window, (unsigned long)kTailBase);
        exit(1);
    }
    // The section is ordinary zero-fill memory, which counts against the
    // system's commit limit: a reservation without access does not.
    void* p = mmap(pcgvr_window, kTailEnd - kTailBase, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED, -1, 0);
    if (p != (void*)kTailBase) {
        fprintf(stderr, "pcgalaxyvr: cannot reserve the game's address window at 0x%08lx\n", (unsigned long)kTailBase);
        exit(1);
    }
    port_mem_set_reserved_window(kTailBase, kTailEnd - kTailBase);
}

void usage(FILE* out) {
    fprintf(out,
            "PCGalaxyVR %s: Super Mario Galaxy in PC VR (OpenXR)\n"
            "\n"
            "usage: pcgalaxyvr [--game DIR] [--no-mirror] [--flat]\n"
            "       pcgalaxyvr --headless DATA SAVES [SECONDS]\n"
            "\n"
            "  --game DIR     play from the converted game files in DIR (the folder\n"
            "                 holding sys and files); by default the folder chosen\n"
            "                 on the setup screen, else ~/.local/share/PCGalaxyVR/game\n"
            "  --no-mirror    no desktop window showing the headset's view\n"
            "  --flat         play in a window on the desktop, without a headset\n"
            "                 (gamepad, or keyboard and mouse; F11: full screen)\n"
            "  --headless     run without a headset, saving captures to SAVES/../shots\n"
            "                 (see docs/TECHNICAL.md)\n"
            "  --gl-selftest [N]  build the renderer's shaders and N generated ones\n"
            "                 without a headset or game files, then exit\n"
            "  --version      print the version\n",
            PCGVR_VERSION);
}

}  // namespace

int main(int argc, char** argv) {
    reserveWindow();

    PortPcOptions options{};
    options.mirror = 1;
    bool flat = false;
    for (int i = 1; i < argc; i++) {
        const char* a = argv[i];
        if (!strcmp(a, "--headless")) {
            // The runner takes the rest of the command line as its own.
            argv[i] = argv[0];
            return port_headless_main(argc - i, argv + i);
        } else if (!strcmp(a, "--gl-selftest")) {
            int failed = port_headless_gl_selftest(i + 1 < argc ? atoi(argv[i + 1]) : 256);
            printf("gl-selftest: %s\n", failed == 0 ? "ok" : failed < 0 ? "no OpenGL 4.5 context" : "programs failed to build (see above)");
            fflush(nullptr);
            _exit(failed == 0 ? 0 : 1);
        } else if (!strcmp(a, "--game") && i + 1 < argc) {
            options.gameDir = argv[++i];
        } else if (!strcmp(a, "--flat")) {
            flat = true;
        } else if (!strcmp(a, "--no-mirror")) {
            options.mirror = 0;
        } else if (!strcmp(a, "--version")) {
            printf("PCGalaxyVR %s\n", PCGVR_VERSION);
            return 0;
        } else if (!strcmp(a, "--help") || !strcmp(a, "-h")) {
            usage(stdout);
            return 0;
        } else {
            fprintf(stderr, "pcgalaxyvr: unknown option %s\n\n", a);
            usage(stderr);
            return 2;
        }
    }
    // The renderer's shader compile thread has a GLX context of its own.
    XInitThreads();
    return flat ? port_flat_main(&options) : port_pc_main(&options);
}
