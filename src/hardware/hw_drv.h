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
/// \brief imports/exports for the 3D hardware low-level interface API

#ifndef __HWR_DRV_H__
#define __HWR_DRV_H__

#include "../screen.h"
#include "hw_data.h"
#include "hw_defs.h"
#include "hw_md2.h"
#include "r_opengl/r_opengl.h"

#include "hw_dll.h"

// ==========================================================================
//                                                       STANDARD DLL EXPORTS
// ==========================================================================

#ifdef SDL
#undef VID_X11
#endif

boolean GL_Init(I_Error_t ErrorFunction);
#ifndef SDL
void GL_Shutdown(void); // Shutdown the OpenGL system
#endif
#ifdef _WINDOWS
void GetModeList(vmode_t **pvidmodes, int *numvidmodes);
#endif
#ifdef VID_X11
Window HookXwin(Display *, int, int, boolean);
#endif
#if defined (PURESDL) || defined (macintosh)
void GL_SetPalette(int *, RGBA_t *gamma);
#else
void GL_SetPalette(RGBA_t *ppal, RGBA_t *pgamma);
#endif
void GL_FinishUpdate(int waitvbl); // SDL used OglSDLFinishUpdate
void GL_Draw2DLine(F2DCoord *v1, F2DCoord *v2, RGBA_t Color);
void GL_DrawPolygon(FSurfaceInfo *pSurf, FOutVector *pOutVerts, FUINT iNumPts, FBITFIELD PolyFlags, FBITFIELD PolyFlags2);
void GL_SetBlend(FBITFIELD PolyFlags);
void GL_ClearBuffer(FBOOLEAN ColorMask, FBOOLEAN DepthMask, FRGBAFloat *ClearColor);
void GL_SetTexture(FTextureInfo *TexInfo, boolean anisotropic);
void GL_ReadRect(int x, int y, int width, int height, int dst_stride, USHORT *dst_data);
void GL_GClipRect(int minx, int miny, int maxx, int maxy, float nearclip);
void GL_ClearMipMapCache(void);

// Multi-purpose function to set an OpenGL state such as fog, or texture filtering
void GL_SetSpecialState(hwdspecialstate_t IdState, int Value);

void GL_DrawMD2(int *gl_cmd_buffer, md2_frame_t *frame, ULONG duration, ULONG tics,
				md2_frame_t *nextframe, FTransform *pos, float scale, byte *color);
void GL_BuildMD2Lists(int *gl_cmd_buffer, md2_t* md2);
void GL_DrawMD2Shadow(int *gl_cmd_buffer, md2_frame_t *frame, ULONG duration,
					  ULONG tics, md2_frame_t *nextframe, FTransform *pos, float scale,
					  fixed_t height, fixed_t light, fixed_t offset, mobj_t *mobj);

void GL_SetTransform(FTransform *ptransform);
void GL_DoFTransform(FTransform *stransform, GLfloat pixdx, GLfloat pixdy, 
					 GLfloat eyedx, GLfloat eyedy, GLfloat focus);
int GL_GetTextureUsed(void);
int GL_GetRenderVersion(void);

#ifdef VID_X11 // ifdef to be removed as soon as Windows supports that as well
// Added for Voodoo detection
char *GetRenderer(void);
#endif

// OpenGL functions for post processing
#define SCREENVERTS 10
void GL_PostImgRedraw(float points[SCREENVERTS][SCREENVERTS][2]);
void GL_DrawIntermissionBG(void);
void GL_MakeScreenTexture(ULONG texturenum, boolean grayscale);
void GL_BindTexture(ULONG texturenum);

// OpenGL screen wiping
void GL_StartScreenWipe(void);
void GL_EndScreenWipe(void);
void GL_DoScreenWipe(float alpha);
// ==========================================================================
//                                      HWR DRIVER OBJECT, FOR CLIENT PROGRAM
// ==========================================================================

#if !defined (_CREATE_DLL_)

#define HWD hwdriver

#endif //not defined _CREATE_DLL_

#endif //__HWR_DRV_H__

