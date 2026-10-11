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
#include <execinfo.h>
#include <signal.h>
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

// A crash prints where it happened, to the terminal and the log: the program
// is at a fixed address, so the addresses map to functions in the same build
// (nm, addr2line, or gdb on the release's binary).
void onCrash(int sig, siginfo_t* info, void* context) {
    const ucontext_t* uc = (const ucontext_t*)context;
    void* pc = (void*)uc->uc_mcontext.gregs[REG_RIP];
    port_log("pcgalaxyvr %s crashed: signal %d at address %p, instruction %p; backtrace:", PCGVR_VERSION, sig, info->si_addr, pc);
    void* frames[48];
    int n = backtrace(frames, 48);
    for (int i = 0; i < n; i++) {
        port_log("  #%d %p", i, frames[i]);
    }
    port_log("(the log: ~/.local/share/PCGalaxyVR/petari_log.txt; please attach it to a bug report)");
    signal(sig, SIG_DFL);
    raise(sig);
}

// An integer division by zero (or INT_MIN / -1) traps on x86.  The Wii's
// PowerPC and the Quest's ARM go on with a result instead, and the game
// relies on it here and there (a particle volume dividing by its count
// less one, with one).  So the trap is answered the ARM way: the quotient
// is 0 (INT_MIN for INT_MIN / -1) and the remainder the dividend (0), and
// the program goes on after the division.  Returns false for anything but
// a DIV or IDIV it can decode.
bool resumeDivision(ucontext_t* uc) {
    greg_t* r = uc->uc_mcontext.gregs;
    const uint8_t* ip = (const uint8_t*)r[REG_RIP];
    const uint8_t* p = ip;
    bool opsize16 = false;
    uint8_t rex = 0;
    for (;; p++) {
        if (*p == 0x66) {
            opsize16 = true;
        } else if (*p == 0x67 || *p == 0xF2 || *p == 0xF3 || *p == 0x2E || *p == 0x36 || *p == 0x3E || *p == 0x26 || *p == 0x64 || *p == 0x65) {
        } else {
            break;
        }
    }
    if ((*p & 0xF0) == 0x40) rex = *p++;
    uint8_t op = *p++;
    if (op != 0xF6 && op != 0xF7) return false;
    uint8_t modrm = *p++;
    int reg = (modrm >> 3) & 7, mod = modrm >> 6, rm = modrm & 7;
    if (reg != 6 && reg != 7) return false;  // DIV, IDIV
    if (mod != 3 && rm == 4) {
        uint8_t sib = *p++;
        if (mod == 0 && (sib & 7) == 5) p += 4;
    } else if (mod == 0 && rm == 5) {
        p += 4;  // RIP-relative
    }
    if (mod == 1) p += 1;
    if (mod == 2) p += 4;
    bool isSigned = reg == 7;
    if (op == 0xF6) {
        // 8 bits: AX / r8 -> AL, AH.
        int16_t ax = (int16_t)(r[REG_RAX] & 0xFFFF);
        bool ovf = isSigned && ax == -32768;
        uint8_t q = ovf ? 0x80 : 0, rem = ovf ? 0 : (uint8_t)ax;
        r[REG_RAX] = (r[REG_RAX] & ~(greg_t)0xFFFF) | (greg_t)((rem << 8) | q);
    } else if (rex & 8) {
        int64_t a = (int64_t)r[REG_RAX];
        bool ovf = isSigned && a == INT64_MIN;
        r[REG_RAX] = ovf ? a : 0;
        r[REG_RDX] = ovf ? 0 : a;
    } else if (opsize16) {
        int16_t a = (int16_t)(r[REG_RAX] & 0xFFFF);
        bool ovf = isSigned && a == INT16_MIN;
        r[REG_RAX] = (r[REG_RAX] & ~(greg_t)0xFFFF) | (greg_t)(uint16_t)(ovf ? a : 0);
        r[REG_RDX] = (r[REG_RDX] & ~(greg_t)0xFFFF) | (greg_t)(uint16_t)(ovf ? 0 : a);
    } else {
        // 32 bits (the usual): writing a 32-bit register clears the upper half.
        int32_t a = (int32_t)(r[REG_RAX] & 0xFFFFFFFF);
        bool ovf = isSigned && a == INT32_MIN;
        r[REG_RAX] = (greg_t)(uint32_t)(ovf ? a : 0);
        r[REG_RDX] = (greg_t)(uint32_t)(ovf ? 0 : a);
    }
    r[REG_RIP] = (greg_t)p;
    static int logged = 0;
    if (logged < 20) {
        logged++;
        port_log("cpu: integer division by zero at %p, answered as ARM would (0)", (const void*)ip);
    }
    return true;
}

void onDivision(int sig, siginfo_t* info, void* context) {
    if ((info->si_code == FPE_INTDIV || info->si_code == FPE_INTOVF) && resumeDivision((ucontext_t*)context)) {
        return;
    }
    onCrash(sig, info, context);
}

// `pcgalaxyvr --div-selftest` (CI): divisions that trap on x86, answered
// the ARM way.  Exit status 0 when all of them gave ARM's results.
__attribute__((noinline)) int div32(volatile int a, volatile int b) { return a / b; }
__attribute__((noinline)) int mod32(volatile int a, volatile int b) { return a % b; }
__attribute__((noinline)) long long div64(volatile long long a, volatile long long b) { return a / b; }
__attribute__((noinline)) unsigned udiv32(volatile unsigned a, volatile unsigned b) { return a / b; }
int divSelfTest() {
    struct {
        const char* what;
        long long got, want;
    } t[] = {
        {"7 / 0", div32(7, 0), 0},
        {"7 % 0", mod32(7, 0), 7},
        {"-7 / 0 (64-bit)", div64(-7, 0), 0},
        {"7u / 0", udiv32(7, 0), 0},
        {"INT_MIN / -1", div32(INT32_MIN, -1), INT32_MIN},
        {"9 / 3", div32(9, 3), 3},
    };
    int bad = 0;
    for (auto& c : t) {
        printf("div-selftest: %-18s = %lld%s\n", c.what, c.got, c.got == c.want ? "" : "  WRONG");
        bad += c.got != c.want;
    }
    return bad ? 1 : 0;
}

void installCrashHandler() {
    static char altStack[64 * 1024];
    stack_t ss{};
    ss.ss_sp = altStack;
    ss.ss_size = sizeof(altStack);
    sigaltstack(&ss, nullptr);
    struct sigaction sa {};
    sa.sa_sigaction = onCrash;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&sa.sa_mask);
    for (int sig : {SIGSEGV, SIGBUS, SIGILL}) {
        sigaction(sig, &sa, nullptr);
    }
    sa.sa_sigaction = onDivision;
    sigaction(SIGFPE, &sa, nullptr);
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
    installCrashHandler();

    PortPcOptions options{};
    options.mirror = 1;
    bool flat = false;
    for (int i = 1; i < argc; i++) {
        const char* a = argv[i];
        if (!strcmp(a, "--div-selftest")) {
            return divSelfTest();
        } else if (!strcmp(a, "--headless")) {
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
