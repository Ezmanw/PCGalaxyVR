// The PC (Linux) program: platform/linux/main.cpp parses the command line
// and starts the headset app (xr_app.cpp) or the headless runner
// (platform/common/headless.c).
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct PortPcOptions {
    const char* gameDir;  // --game: the converted game files (else the setting, then the data folder)
    int mirror;           // show the left eye in a window on the desktop
} PortPcOptions;

int port_pc_main(const PortPcOptions* options);    // the headset app (xr_app.cpp)
int port_flat_main(const PortPcOptions* options);  // flat mode: a desktop window (platform/linux/flat_app.cpp)
int port_headless_main(int argc, char** argv);
// Compiles the renderer's and VR layer's programs and `count` generated GX
// programs in a headless context; the number that failed (-1: no context).
int port_headless_gl_selftest(int count);
void port_mem_set_reserved_window(uintptr_t base, size_t size);

#ifdef __cplusplus
}
#endif
