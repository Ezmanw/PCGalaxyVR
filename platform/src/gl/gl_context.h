// What differs between OpenGL ES (Quest) and desktop OpenGL (PC) beyond the
// header (port/gl.h): the window-system API that made the context (EGL or
// GLX), function lookup, the GLSL version line, state GLES always has on,
// and a second context for the renderer's shader compile thread.
#pragma once

#include "port/gl.h"

namespace glctx {

enum class Api { Egl, Glx };

// Which window-system API made the current context (EGL unless told
// otherwise: the headset app on Linux uses GLX, see xr_app.cpp).
void setApi(Api api);
Api api();

// True for desktop OpenGL, false for OpenGL ES.
constexpr bool desktop() { return !PORT_GL_ES; }

// The address of a GL function, for extensions that may be missing.
void* proc(const char* name);

// glShaderSource for a GLSL ES 3.20 source: on desktop GL its version line
// becomes `#version 450 core` (precision qualifiers are legal there).
void shaderSource(GLuint shader, const char* source);

// State GLES has on that desktop GL does not: sRGB encoding of writes to
// sRGB images, seamless cube maps.  Called once the main context is current.
void applyDefaults();

// GPU timer queries (GL_TIME_ELAPSED): core on desktop GL, an extension on
// GLES, where the disjoint flag must be checked too.
bool hasTimerQueries();
bool timerDisjoint();  // reading it clears it

// A context sharing objects with the current one, for another thread:
// made on the thread whose context is current, then made current on the
// other thread.  Null when the window system cannot make one.
struct SharedContext;
SharedContext* createShared();
bool makeCurrent(SharedContext* ctx);

}  // namespace glctx
