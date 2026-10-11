// OpenGL ES / desktop OpenGL differences (see gl_context.h).
#include "gl_context.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <string.h>

#include <string>

#include "port/port.h"

#if !PORT_GL_ES
#include <GL/glx.h>
#endif

namespace glctx {

namespace {
Api sApi = Api::Egl;
}

void setApi(Api api) { sApi = api; }
Api api() { return sApi; }

void* proc(const char* name) {
#if !PORT_GL_ES
    if (sApi == Api::Glx) {
        return (void*)glXGetProcAddressARB((const GLubyte*)name);
    }
#endif
    return (void*)eglGetProcAddress(name);
}

void shaderSource(GLuint shader, const char* source) {
#if !PORT_GL_ES
    static const char kEs[] = "#version 320 es";
    if (strncmp(source, kEs, sizeof(kEs) - 1) == 0) {
        std::string s = std::string("#version 450 core") + (source + sizeof(kEs) - 1);
        const char* p = s.c_str();
        glShaderSource(shader, 1, &p, nullptr);
        return;
    }
#endif
    glShaderSource(shader, 1, &source, nullptr);
}

void applyDefaults() {
#if !PORT_GL_ES
    glEnable(GL_FRAMEBUFFER_SRGB);
    glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
    GLint profile = 0;
    glGetIntegerv(GL_CONTEXT_PROFILE_MASK, &profile);
    if (profile & GL_CONTEXT_CORE_PROFILE_BIT) {
        // A core profile has no vertex array object 0, which GLES draws with
        // when none is bound: bind one for good.
        GLuint vao = 0;
        glGenVertexArrays(1, &vao);
        glBindVertexArray(vao);
    }
    port_log("gl: desktop OpenGL %s (%s profile), GLSL %s", (const char*)glGetString(GL_VERSION),
             (profile & GL_CONTEXT_CORE_PROFILE_BIT) ? "core" : "compatibility", (const char*)glGetString(GL_SHADING_LANGUAGE_VERSION));
#endif
}

bool hasTimerQueries() {
#if PORT_GL_ES
    const char* ext = (const char*)glGetString(GL_EXTENSIONS);
    return ext && strstr(ext, "GL_EXT_disjoint_timer_query");
#else
    return true;
#endif
}

bool timerDisjoint() {
#if PORT_GL_ES
    const GLenum kGpuDisjoint = 0x8FBB;  // GL_GPU_DISJOINT_EXT
    GLint disjoint = 0;
    glGetIntegerv(kGpuDisjoint, &disjoint);
    return disjoint != 0;
#else
    return false;
#endif
}

// ---------------------------------------------------------------------------
// Shared contexts
// ---------------------------------------------------------------------------
struct SharedContext {
    Api api = Api::Egl;
    EGLDisplay eglDisplay = EGL_NO_DISPLAY;
    EGLContext eglContext = EGL_NO_CONTEXT;
    EGLSurface eglSurface = EGL_NO_SURFACE;
#if !PORT_GL_ES
    Display* xDisplay = nullptr;
    GLXContext glxContext = nullptr;
    GLXPbuffer glxPbuffer = 0;
#endif
};

namespace {

SharedContext* createSharedEgl() {
    EGLDisplay dpy = eglGetCurrentDisplay();
    EGLContext share = eglGetCurrentContext();
    if (dpy == EGL_NO_DISPLAY || share == EGL_NO_CONTEXT) {
        return nullptr;
    }
    EGLint cfgId = 0, n = 0;
    eglQueryContext(dpy, share, EGL_CONFIG_ID, &cfgId);
    const EGLint cfgAttr[] = {EGL_CONFIG_ID, cfgId, EGL_NONE};
    EGLConfig cfg = nullptr;
    eglChooseConfig(dpy, cfgAttr, &cfg, 1, &n);
#if PORT_GL_ES
    const EGLint ctxAttr[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 2, EGL_NONE};
#else
    GLint profile = 0;
    glGetIntegerv(GL_CONTEXT_PROFILE_MASK, &profile);
    const EGLint ctxAttr[] = {EGL_CONTEXT_MAJOR_VERSION,
                              4,
                              EGL_CONTEXT_MINOR_VERSION,
                              5,
                              EGL_CONTEXT_OPENGL_PROFILE_MASK,
                              (profile & GL_CONTEXT_CORE_PROFILE_BIT) ? EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT
                                                                      : EGL_CONTEXT_OPENGL_COMPATIBILITY_PROFILE_BIT,
                              EGL_NONE};
#endif
    EGLContext ctx = eglCreateContext(dpy, cfg, share, ctxAttr);
    if (ctx == EGL_NO_CONTEXT) {
        port_log("gl: no shared EGL context (0x%x)", eglGetError());
        return nullptr;
    }
    // A pbuffer to be current on, or none where the display has no pbuffers
    // (Mesa's surfaceless platform): the renderer only draws to framebuffers.
    const EGLint pb[] = {EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};
    EGLSurface surf = eglCreatePbufferSurface(dpy, cfg, pb);
    SharedContext* s = new SharedContext;
    s->api = Api::Egl;
    s->eglDisplay = dpy;
    s->eglContext = ctx;
    s->eglSurface = surf;
    return s;
}

#if !PORT_GL_ES
SharedContext* createSharedGlx() {
    Display* dpy = glXGetCurrentDisplay();
    GLXContext share = glXGetCurrentContext();
    if (!dpy || !share) {
        return nullptr;
    }
    int cfgId = 0;
    glXQueryContext(dpy, share, GLX_FBCONFIG_ID, &cfgId);
    const int cfgAttr[] = {GLX_FBCONFIG_ID, cfgId, None};
    int n = 0;
    GLXFBConfig* cfgs = glXChooseFBConfig(dpy, DefaultScreen(dpy), cfgAttr, &n);
    if (!cfgs || n == 0) {
        port_log("gl: no GLX config for a shared context");
        return nullptr;
    }
    GLXFBConfig cfg = cfgs[0];
    XFree(cfgs);
    typedef GLXContext (*CreateContextAttribs)(Display*, GLXFBConfig, GLXContext, Bool, const int*);
    CreateContextAttribs create = (CreateContextAttribs)glXGetProcAddressARB((const GLubyte*)"glXCreateContextAttribsARB");
    if (!create) {
        return nullptr;
    }
    GLint profile = 0;
    glGetIntegerv(GL_CONTEXT_PROFILE_MASK, &profile);
    const int ctxAttr[] = {GLX_CONTEXT_MAJOR_VERSION_ARB,
                           4,
                           GLX_CONTEXT_MINOR_VERSION_ARB,
                           5,
                           GLX_CONTEXT_PROFILE_MASK_ARB,
                           (profile & GL_CONTEXT_CORE_PROFILE_BIT) ? GLX_CONTEXT_CORE_PROFILE_BIT_ARB : GLX_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB,
                           None};
    GLXContext ctx = create(dpy, cfg, share, True, ctxAttr);
    if (!ctx) {
        port_log("gl: no shared GLX context");
        return nullptr;
    }
    int drawableType = 0;
    glXGetFBConfigAttrib(dpy, cfg, GLX_DRAWABLE_TYPE, &drawableType);
    GLXPbuffer pbuf = 0;
    if (drawableType & GLX_PBUFFER_BIT) {
        const int pbAttr[] = {GLX_PBUFFER_WIDTH, 1, GLX_PBUFFER_HEIGHT, 1, None};
        pbuf = glXCreatePbuffer(dpy, cfg, pbAttr);
    }
    SharedContext* s = new SharedContext;
    s->api = Api::Glx;
    s->xDisplay = dpy;
    s->glxContext = ctx;
    s->glxPbuffer = pbuf;
    return s;
}
#endif

}  // namespace

SharedContext* createShared() {
#if !PORT_GL_ES
    if (sApi == Api::Glx) {
        return createSharedGlx();
    }
#endif
    return createSharedEgl();
}

bool makeCurrent(SharedContext* s) {
    if (!s) {
        return false;
    }
#if !PORT_GL_ES
    if (s->api == Api::Glx) {
        // Without a pbuffer, GL 3.0+ contexts may be current on no drawable
        // (GLX_ARB_create_context).
        return glXMakeContextCurrent(s->xDisplay, s->glxPbuffer, s->glxPbuffer, s->glxContext);
    }
    eglBindAPI(EGL_OPENGL_API);
#endif
    return eglMakeCurrent(s->eglDisplay, s->eglSurface, s->eglSurface, s->eglContext);
}

}  // namespace glctx
