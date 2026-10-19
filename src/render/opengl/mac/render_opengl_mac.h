// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

#ifndef RENDER_OPENGL_MAC_H
#define RENDER_OPENGL_MAC_H

// The renderer owns its GL procedure pointers. Apple's legacy header provides
// constants and types without declaring conflicting entrypoint symbols.
#define GL_GLEXT_LEGACY 1
#define GL_GLEXT_FUNCTION_POINTERS 1
#include <OpenGL/gl.h>
#undef GL_GLEXT_FUNCTION_POINTERS
#undef GL_GLEXT_LEGACY
#include <dlfcn.h>

// Other platforms link these GL 1.x entrypoints directly.
#define R_OGL_CoreProcedureXList \
X(glGenTextures, void, (GLsizei n, GLuint *textures))\
X(glDeleteTextures, void, (GLsizei n, const GLuint *textures))\
X(glBindTexture, void, (GLenum target, GLuint texture))\
X(glTexImage2D, void, (GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const void *pixels))\
X(glTexSubImage2D, void, (GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const void *pixels))\
X(glTexParameteri, void, (GLenum target, GLenum pname, GLint param))\
X(glEnable, void, (GLenum cap))\
X(glDisable, void, (GLenum cap))\
X(glScissor, void, (GLint x, GLint y, GLsizei width, GLsizei height))\
X(glViewport, void, (GLint x, GLint y, GLsizei width, GLsizei height))\
X(glClearColor, void, (GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha))\
X(glClear, void, (GLbitfield mask))\
X(glBlendFunc, void, (GLenum sfactor, GLenum dfactor))\
X(glDrawBuffer, void, (GLenum mode))\
X(glReadBuffer, void, (GLenum mode))\
X(glDrawArrays, void, (GLenum mode, GLint first, GLsizei count))

global NSOpenGLContext *r_ogl_mac_ctx = nil;

#endif // RENDER_OPENGL_MAC_H
