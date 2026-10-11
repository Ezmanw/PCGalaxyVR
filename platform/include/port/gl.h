// The OpenGL API the renderer draws with: OpenGL ES 3.2 on Android (Quest),
// desktop OpenGL 4.5 elsewhere.  The renderer is written against GLES 3.2,
// which desktop GL 4.5 contains; the few differences between the two (the
// GLSL version line, state GLES has on by default, extension names, shared
// contexts) go through platform/src/gl/gl_context.h.
#pragma once

#if defined(__ANDROID__)
#include <GLES3/gl32.h>
#define PORT_GL_ES 1
#else
// Core and extension entry points are linked directly (libglvnd's libOpenGL
// exports them all); the context is GL 4.5.
#ifndef GL_GLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES 1
#endif
#include <GL/gl.h>
#include <GL/glext.h>
#define PORT_GL_ES 0
#endif
