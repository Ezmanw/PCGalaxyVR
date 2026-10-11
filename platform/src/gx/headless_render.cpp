// Offscreen rendering for bring-up: renders the latest recorded frame with
// the GL renderer into a pbuffer-backed context and writes it as a PNG.
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <string>
#include <vector>

#include "../gl/gl_context.h"
#include "gl_renderer.h"
#include "gl_shadergen.h"
#include "../xr/vr_renderer.h"
#include "../xr/vr_rig.h"
#include "gpu.h"
#include "port/paths.h"
#include "port/port.h"

namespace {

EGLDisplay sDisplay = EGL_NO_DISPLAY;
EGLContext sContext = EGL_NO_CONTEXT;
EGLSurface sSurface = EGL_NO_SURFACE;
gpu::EfbTarget sTarget;

// The headless GL context, current on the calling thread.
bool makeContext() {
    if (sContext != EGL_NO_CONTEXT) {
        return true;
    }
#if PORT_GL_ES
    sDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (!eglInitialize(sDisplay, nullptr, nullptr)) {
        port_log("headless: eglInitialize failed");
        return false;
    }
    const EGLint cfgAttr[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8,
                              EGL_BLUE_SIZE,       8,                  EGL_ALPHA_SIZE,   8,               EGL_NONE};
    EGLConfig cfg;
    EGLint n = 0;
    if (!eglChooseConfig(sDisplay, cfgAttr, &cfg, 1, &n) || n == 0) {
        port_log("headless: no EGL config");
        return false;
    }
    const EGLint ctxAttr[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 2, EGL_CONTEXT_OPENGL_DEBUG, EGL_TRUE, EGL_NONE};
    sContext = eglCreateContext(sDisplay, cfg, EGL_NO_CONTEXT, ctxAttr);
    const EGLint pbAttr[] = {EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE};
    sSurface = eglCreatePbufferSurface(sDisplay, cfg, pbAttr);
    if (sContext == EGL_NO_CONTEXT || !eglMakeCurrent(sDisplay, sSurface, sSurface, sContext)) {
        port_log("headless: cannot create/make current a GLES 3.2 context (0x%x)", eglGetError());
        return false;
    }
#else
    // Desktop OpenGL 4.5 through EGL, without a window: Mesa's surfaceless
    // platform when there is one (no X or Wayland display needed), else the
    // default display.
    PFNEGLGETPLATFORMDISPLAYEXTPROC getPlatformDisplay = (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
    const char* clientExts = eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);
    if (getPlatformDisplay && clientExts && strstr(clientExts, "EGL_MESA_platform_surfaceless")) {
        sDisplay = getPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr);
    }
    if (sDisplay == EGL_NO_DISPLAY) {
        sDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    }
    if (!eglInitialize(sDisplay, nullptr, nullptr) || !eglBindAPI(EGL_OPENGL_API)) {
        port_log("headless: eglInitialize failed (0x%x)", eglGetError());
        return false;
    }
    // Pbuffer configs (the default asks for window ones, which the
    // surfaceless platform has none of), else any.
    EGLConfig cfg;
    EGLint n = 0;
    for (EGLint surface : {EGL_PBUFFER_BIT, EGL_DONT_CARE}) {
        const EGLint cfgAttr[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT, EGL_SURFACE_TYPE, surface, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8,
                                  EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE};
        if (eglChooseConfig(sDisplay, cfgAttr, &cfg, 1, &n) && n > 0) break;
        n = 0;
    }
    if (n == 0) {
        port_log("headless: no EGL config for desktop OpenGL");
        return false;
    }
    for (EGLint profile : {EGL_CONTEXT_OPENGL_COMPATIBILITY_PROFILE_BIT, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT}) {
        const EGLint ctxAttr[] = {EGL_CONTEXT_MAJOR_VERSION, 4, EGL_CONTEXT_MINOR_VERSION, 5, EGL_CONTEXT_OPENGL_PROFILE_MASK, profile,
                                  EGL_CONTEXT_OPENGL_DEBUG, EGL_TRUE, EGL_NONE};
        sContext = eglCreateContext(sDisplay, cfg, EGL_NO_CONTEXT, ctxAttr);
        if (sContext != EGL_NO_CONTEXT) break;
    }
    EGLint surfaceType = 0;
    eglGetConfigAttrib(sDisplay, cfg, EGL_SURFACE_TYPE, &surfaceType);
    if (surfaceType & EGL_PBUFFER_BIT) {
        const EGLint pbAttr[] = {EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE};
        sSurface = eglCreatePbufferSurface(sDisplay, cfg, pbAttr);
    }
    if (sContext == EGL_NO_CONTEXT || !eglMakeCurrent(sDisplay, sSurface, sSurface, sContext)) {
        port_log("headless: cannot create/make current an OpenGL 4.5 context (0x%x)", eglGetError());
        return false;
    }
    glctx::setApi(glctx::Api::Egl);
    glctx::applyDefaults();
#endif
    port_log("headless: GL %s / %s", (const char*)glGetString(GL_VERSION), (const char*)glGetString(GL_RENDERER));
    glEnable(GL_DEBUG_OUTPUT);
    glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
    glDebugMessageCallback(
        [](GLenum, GLenum type, GLuint, GLenum severity, GLsizei, const GLchar* msg, const void*) {
            if (severity != GL_DEBUG_SEVERITY_NOTIFICATION) {
                static int count;
                if (count++ < 50) port_log("gl debug (type 0x%x sev 0x%x): %s", type, severity, msg);
            }
        },
        nullptr);
    return true;
}

bool initContext() {
    static bool ready = false;
    if (ready) {
        return true;
    }
    if (!makeContext()) {
        return false;
    }
#ifdef __ANDROID__
    const char* defaultCache = "/data/local/tmp/petari/shaders.bin";
#else
    std::string cache = port_cache_dir() + "/shaders-headless.bin";
    const char* defaultCache = cache.c_str();
#endif
    gpu::setShaderCachePath(getenv("PETARI_SHADER_CACHE") ? getenv("PETARI_SHADER_CACHE") : defaultCache);
    ready = gpu::renderer().init();
    return ready;
}

// Minimal PNG writer (stored deflate blocks).
unsigned crcTable[256];
void initCrc() {
    for (unsigned n = 0; n < 256; n++) {
        unsigned c = n;
        for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        crcTable[n] = c;
    }
}
unsigned crc(const unsigned char* p, size_t n, unsigned c = 0xFFFFFFFFu) {
    for (size_t i = 0; i < n; i++) c = crcTable[(c ^ p[i]) & 255] ^ (c >> 8);
    return c;
}
void put32(std::vector<unsigned char>& v, unsigned x) {
    v.push_back(x >> 24);
    v.push_back(x >> 16);
    v.push_back(x >> 8);
    v.push_back(x);
}
void chunk(FILE* f, const char* type, const std::vector<unsigned char>& data) {
    std::vector<unsigned char> buf;
    put32(buf, (unsigned)data.size());
    buf.insert(buf.end(), type, type + 4);
    buf.insert(buf.end(), data.begin(), data.end());
    unsigned c = crc(buf.data() + 4, buf.size() - 4) ^ 0xFFFFFFFFu;
    put32(buf, c);
    fwrite(buf.data(), 1, buf.size(), f);
}

bool writePng(const char* path, const unsigned char* rgba, int w, int h) {
    static bool crcReady;
    if (!crcReady) {
        initCrc();
        crcReady = true;
    }
    FILE* f = fopen(path, "wb");
    if (!f) {
        return false;
    }
    const unsigned char sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    fwrite(sig, 1, 8, f);
    std::vector<unsigned char> ihdr;
    put32(ihdr, w);
    put32(ihdr, h);
    ihdr.push_back(8);  // bit depth
    ihdr.push_back(6);  // RGBA
    ihdr.push_back(0);
    ihdr.push_back(0);
    ihdr.push_back(0);
    chunk(f, "IHDR", ihdr);

    // Raw scanlines (filter 0), rows flipped (GL origin is bottom-left).
    std::vector<unsigned char> raw;
    raw.reserve((size_t)(w * 4 + 1) * h);
    for (int y = h - 1; y >= 0; y--) {
        raw.push_back(0);
        raw.insert(raw.end(), rgba + (size_t)y * w * 4, rgba + (size_t)(y + 1) * w * 4);
    }
    std::vector<unsigned char> z;
    z.push_back(0x78);
    z.push_back(0x01);
    size_t pos = 0;
    while (pos < raw.size()) {
        size_t n = raw.size() - pos;
        if (n > 65535) n = 65535;
        z.push_back(pos + n == raw.size() ? 1 : 0);
        z.push_back(n & 255);
        z.push_back(n >> 8);
        z.push_back(~n & 255);
        z.push_back((~n >> 8) & 255);
        z.insert(z.end(), raw.begin() + pos, raw.begin() + pos + n);
        pos += n;
    }
    unsigned a = 1, b = 0;
    for (unsigned char c : raw) {
        a = (a + c) % 65521;
        b = (b + a) % 65521;
    }
    put32(z, (b << 16) | a);
    chunk(f, "IDAT", z);
    chunk(f, "IEND", {});
    fclose(f);
    return true;
}

}  // namespace

// Logs a summary of a recorded frame (draws, state, first vertices) and
// writes its textures next to `dir`.
extern "C" void port_headless_write_png(const char* path, const unsigned char* rgba, int w, int h) { writePng(path, rgba, w, h); }

static void dumpFrame(const gpu::Frame& f, const char* dir) {
    using namespace gpu;
    u32 bp[256], xf[256], treg[8], tk[8];
    memcpy(treg, f.startTevReg, sizeof(treg));
    memcpy(tk, f.startTevKonst, sizeof(tk));
    memcpy(bp, f.startBp, sizeof(bp));
    memcpy(xf, f.startXfRegs, sizeof(xf));
    const u32* c = f.cmds.data();
    const u32* end = c + f.cmds.size();
    int draws = 0;
    while (c < end) {
        u32 op = *c++;
        switch (op) {
        case CMD_XF: {
            u32 addr = c[0], count = c[1];
            for (u32 i = 0; i < count; i++) {
                if (addr + i >= 0x1000 && addr + i < 0x1100) xf[addr + i - 0x1000] = c[2 + i];
            }
            c += 2 + count;
            break;
        }
        case CMD_BP:
            bp[*c >> 24] = *c & 0xFFFFFF;
            trackTevColorWrite(treg, tk, *c >> 24, *c & 0xFFFFFF);
            if (draws < 3) {
                port_log("dump:   bp %02x = %06x", *c >> 24, *c & 0xFFFFFF);
            }
            c++;
            break;
        case CMD_TEX:
            port_log("dump:   tex unit %u <- image %u", c[0], c[1]);
            c += 2;
            break;
        case CMD_TEX_EFB:
            port_log("dump:   tex unit %u <- efb copy %u (%ux%u at %08x)", c[0], c[1], c[2], c[3], c[4]);
            c += 5;
            break;
        case CMD_EFB_COPY:
            port_log("dump:   efb copy id %u src %06x/%06x dst %06x ctrl %06x clear %06x %06x", c[0], c[1], c[2], c[3], c[9], c[6], c[7]);
            c += 10;
            break;
        case CMD_MARKER:
            port_log("dump:   marker %u", *c++);
            break;
        case CMD_DRAW: {
            u32 prim = c[0], flags = c[1], first = c[2], count = c[3];
            c += 4;
            if (draws++ < 40) {
                float p[3];
                memcpy(p, &f.verts[first], 12);
                port_log("dump: draw %d prim %02x flags %05x count %u v0 (%.2f %.2f %.2f) tev %u gen %06x cc0 %06x ac0 %06x order %06x proj %u "
                         "cmode %06x zmode %06x numtex %u",
                         draws - 1, prim, flags, count, p[0], p[1], p[2], ((bp[0] >> 10) & 15) + 1, bp[0], bp[0xC0], bp[0xC1], bp[0x28],
                         xf[0x26] & 1, bp[0x41], bp[0x40], xf[0x3F]);
                u32 words = vtxWords(flags);
                for (u32 v = 0; v < count && v < 4; v++) {
                    char line[512];
                    int n = 0;
                    for (u32 w = 0; w < words && w < 16 && n < 480; w++) {
                        u32 x = f.verts[first + v * words + w];
                        float fx;
                        memcpy(&fx, &x, 4);
                        n += snprintf(line + n, sizeof(line) - n, " %08x(%.3g)", x, fx);
                    }
                    port_log("dump:   v%u:%s", v, line);
                }
                float pr[6], vpv[6];
                for (int i = 0; i < 6; i++) {
                    memcpy(&pr[i], &xf[0x20 + i], 4);
                    memcpy(&vpv[i], &xf[0x1A + i], 4);
                }
                port_log("dump:   texgen %05x %05x %05x post %03x %03x dual %u mtxidx %08x %08x chans %u cc %04x %04x ac %04x", xf[0x40], xf[0x41], xf[0x42],
                         xf[0x50], xf[0x51], xf[0x12], xf[0x18], xf[0x19], xf[0x09], xf[0x0E], xf[0x0F], xf[0x10]);
                port_log("dump:   tevreg %06x %06x | %06x %06x | %06x %06x | %06x %06x  konst %06x %06x %06x %06x", treg[0], treg[1],
                         treg[2], treg[3], treg[4], treg[5], treg[6], treg[7], tk[0], tk[1], tk[2], tk[3]);
                port_log("dump:   proj %g %g %g %g %g %g  vp %g %g %g %g %g %g  mtxidx %06x", pr[0], pr[1], pr[2], pr[3], pr[4], pr[5], vpv[0],
                         vpv[1], vpv[2], vpv[3], vpv[4], vpv[5], xf[0x18]);
            }
            break;
        }
        default:
            c = end;
            break;
        }
    }
    // PETARI_DUMP=<n>: the first n textures (default 16).
    size_t maxTextures = atoi(getenv("PETARI_DUMP") ? getenv("PETARI_DUMP") : "") > 1 ? (size_t)atoi(getenv("PETARI_DUMP")) : 16;
    for (size_t i = 0; i < f.textures.size() && i < maxTextures; i++) {
        const TexImage& t = *f.textures[i];
        if (t.rgba.size() < (size_t)t.width * t.height) {
            continue;  // pixels already released after upload
        }
        char path[512];
        snprintf(path, sizeof(path), "%s_tex%02zu.png", dir, i);
        // PNG rows are written bottom-up; flip into a temporary.
        std::vector<unsigned char> px((size_t)t.width * t.height * 4);
        for (u32 y = 0; y < t.height; y++) {
            memcpy(&px[(size_t)(t.height - 1 - y) * t.width * 4], &t.rgba[(size_t)y * t.width], t.width * 4);
        }
        writePng(path, px.data(), (int)t.width, (int)t.height);
        port_log("dump: texture %zu: %ux%u levels %u -> %s", i, t.width, t.height, t.levels, path);
    }
}

extern "C" void port_headless_dump(const char* prefix) {
    auto frame = gpu::frameQueue().latest();
    if (frame) {
        dumpFrame(*frame, prefix);
    }
}

// Renders the latest recorded frame at `scale` x the native EFB size and
// writes `path` (PNG).  Returns the frame number, or 0 if nothing was drawn.
extern "C" unsigned long long port_headless_render(const char* path, int scale) {
    if (!initContext()) {
        return 0;
    }
    auto frame = gpu::frameQueue().latest();
    if (!frame) {
        return 0;
    }
    gpu::Renderer& r = gpu::renderer();
    auto msSince = [](const timespec& a) {
        timespec b;
        clock_gettime(CLOCK_MONOTONIC, &b);
        return (b.tv_sec - a.tv_sec) * 1000.0 + (b.tv_nsec - a.tv_nsec) / 1e6;
    };
    timespec t0;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    if (gpu::debugEnv("PETARI_ASYNC")) {
        // Exercise the worker-thread path the headset uses.
        for (int i = 0; i < 200 && !r.update(); i++) {
            usleep(5000);
        }
    } else {
        r.setFrame(frame);
    }
    glFinish();
    double prepMs = msSince(t0);
    int w = 640 * scale, h = 456 * scale;
    if (sTarget.width != w || sTarget.height != h) {
        if (sTarget.fbo) {
            r.destroyTarget(sTarget);
        }
        sTarget = r.createTarget(w, h);
    }
    // PETARI_GLSTEPS=a,b,...: also write <path>_s<N>.png stopped after N draws.
    if (const char* steps = gpu::debugEnv("PETARI_GLSTEPS")) {
        std::vector<unsigned char> sp((size_t)w * h * 4);
        for (const char* p = steps; *p;) {
            int n = atoi(p);
            gpu::gDebugStopAfterDraws = n;
            r.render(sTarget, nullptr, nullptr);
            glBindFramebuffer(GL_FRAMEBUFFER, sTarget.fbo);
            glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, sp.data());
            for (size_t i = 3; i < sp.size(); i += 4) sp[i] = 255;
            std::string sp_path = std::string(path).substr(0, strlen(path) - 4) + "_s" + std::to_string(n) + ".png";
            writePng(sp_path.c_str(), sp.data(), w, h);
            while (*p && *p != ',') p++;
            if (*p == ',') p++;
        }
        gpu::gDebugStopAfterDraws = -1;
    }
    // PETARI_VRTEST: also render <path>_vr.png through the VR rig, from a
    // head at the stage origin (PETARI_VRHEAD="x,y,z,yawDeg,pitchDeg").
    if (gpu::debugEnv("PETARI_VRTEST") && r.camera().valid) {
        vr::RigState rig;
        vr::RigParams params;
        xm::Mat4 stageFromView = vr::updateRig(rig, params, r.camera(), 0.0f);
        float hx = 0, hy = 0, hz = 0, yaw = 0, pitch = 0;
        if (const char* hp = gpu::debugEnv("PETARI_VRHEAD")) {
            sscanf(hp, "%f,%f,%f,%f,%f", &hx, &hy, &hz, &yaw, &pitch);
        }
        float cy = cosf(yaw * 0.5f * 3.14159265f / 180.0f), sy = sinf(yaw * 0.5f * 3.14159265f / 180.0f);
        float cp = cosf(pitch * 0.5f * 3.14159265f / 180.0f), sp = sinf(pitch * 0.5f * 3.14159265f / 180.0f);
        xm::Quat qy{0, sy, 0, cy}, qp{sp, 0, 0, cp};
        xm::Quat q{qy.w * qp.x, qy.y * qp.w, -qy.y * qp.x, qy.w * qp.w};  // yaw * pitch
        xm::Mat4 eyeFromStage = xm::inversePose(q, {hx, hy, hz});
        float tv = (float)h / (float)w;
        xm::Mat4 proj = xm::projectionFov(-0.7853982f, 0.7853982f, atanf(tv), -atanf(tv), 0.05f, 1000.0f);
        gpu::EyeView ev;
        vr::toRowMajor(eyeFromStage * stageFromView, ev.view);
        vr::toRowMajor(proj, ev.proj);
        bool cutTest = gpu::debugEnv("PETARI_CUTTEST") != nullptr;
        vr::setCutaway(ev, eyeFromStage * stageFromView, r.camera(), cutTest || (r.camera().flags & PORT_GX_CAMERA_OCCLUDED) ? 1.0f : 0.0f,
                       cutTest ? 3.0f : 1.0f);
        // PETARI_VRSIZE=WxH[,WxH...]: time the eye render at headset
        // resolutions; PETARI_FOVEATE=gain[,gain...] also times each size
        // with fixed foveation at those gains (0 = none).
        if (const char* vs = gpu::debugEnv("PETARI_VRSIZE")) {
            struct Timed {
                int w, h;
                float gain;
                gpu::EfbTarget t;
            };
            static std::vector<Timed> sEyeTs;
            std::vector<float> gains;
            if (const char* fv = gpu::debugEnv("PETARI_FOVEATE")) {
                for (const char* p = fv; *p;) {
                    gains.push_back((float)atof(p));
                    while (*p && *p != ',') p++;
                    if (*p == ',') p++;
                }
            } else {
                gains.push_back(0.0f);
            }
            for (const char* p = vs; *p;) {
                int ew = 0, eh = 0;
                sscanf(p, "%dx%d", &ew, &eh);
                while (*p && *p != ',') p++;
                if (*p == ',') p++;
                if (ew <= 0 || eh <= 0) continue;
                for (float gain : gains) {
                    Timed* tm = nullptr;
                    for (Timed& x : sEyeTs) {
                        if (x.w == ew && x.h == eh && x.gain == gain) tm = &x;
                    }
                    if (!tm) {
                        sEyeTs.push_back({ew, eh, gain, r.createTarget(ew, eh)});
                        tm = &sEyeTs.back();
                        if (gain > 0.0f && !r.setFoveation(tm->t, 0.0f, 0.0f, gain, 0.0f)) {
                            port_log("headless: foveation unsupported");
                        }
                    }
                    r.render(tm->t, &ev, nullptr, gpu::HudMode::Skip);
                    glFinish();
                    timespec ta;
                    clock_gettime(CLOCK_MONOTONIC, &ta);
                    for (int i = 0; i < 4; i++) r.render(tm->t, &ev, nullptr, gpu::HudMode::Skip);
                    glFinish();
                    long clk = 0;
                    if (FILE* g = fopen("/sys/class/kgsl/kgsl-3d0/gpuclk", "r")) {
                        if (fscanf(g, "%ld", &clk) != 1) clk = 0;
                        fclose(g);
                    }
                    port_log("headless: VR eye %dx%d foveation %.1f replay %.2f ms (GPU %ld MHz)", ew, eh, gain, msSince(ta) / 4.0, clk / 1000000);
                }
            }
        }
        // PETARI_GLSTEPS also applies here: <path>_vr_s<N>.png.
        if (const char* steps = gpu::debugEnv("PETARI_GLSTEPS")) {
            std::vector<unsigned char> sp((size_t)w * h * 4);
            for (const char* p = steps; *p;) {
                int n = atoi(p);
                gpu::gDebugStopAfterDraws = n;
                r.render(sTarget, &ev, nullptr, gpu::HudMode::Skip);
                glBindFramebuffer(GL_FRAMEBUFFER, sTarget.fbo);
                glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, sp.data());
                for (size_t i = 3; i < sp.size(); i += 4) sp[i] = 255;
                std::string sp_path = std::string(path).substr(0, strlen(path) - 4) + "_vr_s" + std::to_string(n) + ".png";
                writePng(sp_path.c_str(), sp.data(), w, h);
                while (*p && *p != ',') p++;
                if (*p == ',') p++;
            }
            gpu::gDebugStopAfterDraws = -1;
        }
        r.render(sTarget, &ev, nullptr, gpu::HudMode::Skip);
        std::vector<unsigned char> vp((size_t)w * h * 4);
        glBindFramebuffer(GL_FRAMEBUFFER, sTarget.fbo);
        glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, vp.data());
        for (size_t i = 3; i < vp.size(); i += 4) vp[i] = 255;
        std::string vpath = std::string(path).substr(0, strlen(path) - 4) + "_vr.png";
        writePng(vpath.c_str(), vp.data(), w, h);
        const gpu::CameraInfo& c = r.camera();
        // The player's centre in stage space (metres), to place PETARI_VRHEAD.
        const float* v = c.view;
        const float* pl = c.player;
        xm::Vec3 inView = {v[0] * pl[0] + v[1] * pl[1] + v[2] * pl[2] + v[3], v[4] * pl[0] + v[5] * pl[1] + v[6] * pl[2] + v[7],
                           v[8] * pl[0] + v[9] * pl[1] + v[10] * pl[2] + v[11]};
        xm::Vec3 ps = xm::transformPoint(stageFromView, inView);
        port_log("headless: VR view -> %s (flags %x aspect %.2f fovy %.1f, watch %.0f %.0f %.0f, up %.2f %.2f %.2f, player at stage %.2f %.2f %.2f)", vpath.c_str(),
                 c.flags, c.aspect, c.fovy, c.watch[0], c.watch[1], c.watch[2], c.watchUp[0], c.watchUp[1], c.watchUp[2], ps.x, ps.y, ps.z);
    }
    // Time a few replays of the prepared frame (the per-eye cost in VR).
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int i = 0; i < 3; i++) {
        r.render(sTarget, nullptr, nullptr);
    }
    glFinish();
    double renderMs = msSince(t0) / 3.0;
    port_log("headless: prepare %.2f ms, replay %.2f ms", prepMs, renderMs);
    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        port_log("headless: GL error 0x%x after render", err);
    }
    std::vector<unsigned char> px((size_t)w * h * 4);
    glBindFramebuffer(GL_FRAMEBUFFER, sTarget.fbo);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    for (size_t i = 3; i < px.size(); i += 4) {
        px[i] = 255;  // the EFB alpha channel is not part of the image
    }
    writePng(path, px.data(), w, h);
    port_log("headless: frame %llu (%u draws, %zu textures) -> %s", (unsigned long long)frame->number, frame->draws, frame->textures.size(), path);
    return frame->number;
}

// `pcgalaxyvr --gl-selftest` (platform/linux/main.cpp; CI): without game
// files, makes the headless context and compiles what the game would: the VR
// layer's and the renderer's own programs (vr::init, which stops the program
// on a failure), then the generated GX programs for `count` pseudo-random
// register states.  Returns how many of those failed to compile or link.
extern "C" int port_headless_gl_selftest(int count) {
    if (!makeContext()) {
        return -1;
    }
    gpu::setShaderCachePath("");
    vr::init();
    uint32_t seed = 0x5eed1234u;
    auto rnd = [&seed]() {
        seed = seed * 1664525u + 1013904223u;
        return seed ^ (seed >> 15);
    };
    std::vector<uint32_t> bp(0x100), xf(0x1000);
    int failed = 0;
    for (int i = 0; i < count; i++) {
        for (uint32_t& w : bp) w = rnd();
        for (uint32_t& w : xf) w = rnd();
        xf[0x3F] = rnd() % 9;  // texture coordinate generators, as the game sets them
        xf[0x09] = rnd() % 3;  // color channels
        gpu::ShaderUid uid = gpu::makeShaderUid(bp.data(), xf.data(), rnd() & 0xFFFF, (i & 7) == 7);
        uid.cutaway = (i % 5) == 0;
        std::string vsSrc = gpu::genVertexShader(uid), fsSrc = gpu::genFragmentShader(uid);
        GLuint prog = glCreateProgram();
        bool ok = true;
        for (int k = 0; k < 2; k++) {
            GLuint sh = glCreateShader(k ? GL_FRAGMENT_SHADER : GL_VERTEX_SHADER);
            glctx::shaderSource(sh, (k ? fsSrc : vsSrc).c_str());
            glCompileShader(sh);
            GLint status = 0;
            glGetShaderiv(sh, GL_COMPILE_STATUS, &status);
            if (!status) {
                char log[2048];
                glGetShaderInfoLog(sh, sizeof(log), nullptr, log);
                port_log("selftest: program %d: %s shader: %s", i, k ? "fragment" : "vertex", log);
                ok = false;
            }
            glAttachShader(prog, sh);
            glDeleteShader(sh);
        }
        if (ok) {
            glLinkProgram(prog);
            GLint status = 0;
            glGetProgramiv(prog, GL_LINK_STATUS, &status);
            if (!status) {
                char log[2048];
                glGetProgramInfoLog(prog, sizeof(log), nullptr, log);
                port_log("selftest: program %d: link: %s", i, log);
                ok = false;
            }
        }
        glDeleteProgram(prog);
        failed += ok ? 0 : 1;
    }
    port_log("selftest: %d of %d generated GX programs built%s", count - failed, count, failed ? "" : "; the VR layer's programs too");
    return failed;
}
