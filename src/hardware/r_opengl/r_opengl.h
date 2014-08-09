// Emacs style mode select   -*- C++ -*-
//-----------------------------------------------------------------------------
//
// Copyright (C) 1998-2000 by DooM Legacy Team.
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
//-----------------------------------------------------------------------------
/// \file
/// \brief OpenGL API for Gem Engine

#ifndef _R_OPENGL_H_
#define _R_OPENGL_H_

#ifdef SDL

#ifdef _MSC_VER
#pragma warning(disable : 4214 4244)
#endif

#ifdef _WIN32
#include <SDL/SDL_opengl.h>
#else
#include <SDL_opengl.h>
#endif

#ifdef _MSC_VER
#pragma warning(default : 4214 4244)
#endif

#else
#include <GL/gl.h>
#include <GL/glu.h>
#endif

#define  _CREATE_DLL_  // necessary for Unix AND Windows
#include "../../doomdef.h"
#include "../hw_drv.h"

// ==========================================================================
//                                                                DEFINITIONS
// ==========================================================================

#define MIN(x,y) (((x)<(y)) ? (x) : (y))
#define MAX(x,y) (((x)>(y)) ? (x) : (y))

#ifndef DRIVER_STRING
//    #define USE_PALETTED_TEXTURE
#define DRIVER_STRING "SRB2CB OpenGL Renderer 2.0" // Kalaron
#endif

// ==========================================================================
//                                                                     PROTOS
// ==========================================================================

void GL_Flush(void);
int isExtAvailable(const char *extension, const GLubyte *start);
boolean SetupPixelFormat(int WantColorBits, int WantStencilBits, int WantDepthBits);
void SetModelView(GLint w, GLint h);
void SetStates(void);
FUNCMATH float byteasfloat(byte fbyte);
#ifdef USE_PALETTED_TEXTURE
extern PFNGLCOLORTABLEEXTPROC glColorTableEXT;
extern GLubyte                palette_tex[256*3];
#endif

#ifndef GL_EXT_texture_filter_anisotropic
#define GL_TEXTURE_MAX_ANISOTROPY_EXT     0x84FE
#define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT 0x84FF
#endif

#ifdef USE_WGL_SWAP
typedef void (APIENTRY *PFNWGLEXTSWAPCONTROLPROC) (int);
typedef int (APIENTRY *PFNWGLEXTGETSWAPINTERVALPROC) (void);
extern PFNWGLEXTSWAPCONTROLPROC wglSwapIntervalEXT;
extern PFNWGLEXTGETSWAPINTERVALPROC wglGetSwapIntervalEXT;
#endif

// ==========================================================================
//                                                                     GLOBAL
// ==========================================================================

extern const GLubyte    *gl_extensions;
extern RGBA_t           myPaletteData[];
#ifndef SDL
#ifdef _WINDOWS
extern HANDLE           logstream;
#else
extern FILE             *logstream;
#endif
#endif
extern GLint            screen_width;
extern GLint            screen_height;
extern GLbyte           screen_depth;
extern GLint            anisotropy;
extern GLuint           playerviewscreentex;
extern GLuint           skyviewscreentex;

extern float NEAR_CLIPPING_PLANE;

/**	\brief OpenGL flags for video driver
*/
extern int              oglflags;
extern GLint            textureformatGL;

typedef enum
{
	GLF_NOZBUFREAD = 0x01,
	GLF_NOTEXENV   = 0x02,
} oglflags_t;

#endif
