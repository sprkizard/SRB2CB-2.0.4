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
/// \brief 3D render mode functions

#ifndef __HWR_MAIN_H__
#define __HWR_MAIN_H__

#include "hw_data.h"
#include "hw_defs.h"

#include "../am_map.h"
#include "../d_player.h"
#include "../r_defs.h"

// Startup & Shutdown the hardware mode renderer
void HWR_Startup(void);
void HWR_Shutdown(void);
void HWR_Transform(float *cx, float *cy, float *cz, boolean sprite);

void HWR_clearAutomap(void);
void HWR_drawAMline(const fline_t *fl, int color);
void HWR_ScreenPolygon(ULONG color, int height, byte transparency);
void HWR_RenderPlayerView(player_t *player, boolean viewnumber);
// Linedef based mirror support, a linedef with a special action in the map itself can become a mirror
// SRB2CBTODO:!!
//#define MIRRORS
#ifdef MIRRORS
void HWR_RenderLineReflection(seg_t *mirrorwall, byte maxrecursions);
#endif
void HWR_DrawViewBorder(int clearlines);
void HWR_DrawFlatFill(int x, int y, int w, int h, lumpnum_t flatlumpnum);
byte *HWR_GetScreenshot(void);
boolean HWR_Screenshot(const char *lbmname);
void HWR_InitTextureMapping(void);
void HWR_SetViewSize(void);
void HWR_DrawPatch(GLPatch_t *gpatch, int x, int y, int option);
void HWR_DrawClippedPatch(GLPatch_t *gpatch, int x, int y, int option);
void HWR_DrawTranslucentPatch(GLPatch_t *gpatch, int x, int y, int option);
void HWR_DrawSmallPatch(GLPatch_t *gpatch, int x, int y, int option, const byte *colormap);
void HWR_DrawMappedPatch(GLPatch_t *gpatch, int x, int y, int option, const byte *colormap);
void HWR_MakePatch(const patch_t *patch, GLPatch_t *grPatch, GLMipmap_t *grMipmap);
GLTextureFormat_t HWR_LoadPNG(const char *filename, int *w, int *h, GLTexture_t *grMipmap);
void HWR_CreateGLBSP(int bspnum);
void HWR_PrepLevelCache(size_t pnumtextures);
void HWR_DrawFill(int x, int y, int w, int h, int color);
void HWR_DrawTransFill(int x, int y, int w, int h, int color, byte alpha);
void HWR_DrawPic(int x,int y,lumpnum_t lumpnum);

void HWR_AddCommands(void);
FBITFIELD HWR_TranstableToAlpha(int transtablenum, FSurfaceInfo *pSurf);
void HWR_SetPaletteColor(int palcolor);
int HWR_GetTextureUsed(void);

// For general in-world wall drawing
void HWR_ProjectWall(wallVert3D   * wallVerts,
					 FSurfaceInfo * pSurf,
					 FBITFIELD blendmode);

// Duplicate commands for the OpenGL interface
void HWR_DoPostProcessor(ULONG type);
void HWR_StartScreenWipe(void);
void HWR_EndScreenWipe(void);
void HWR_DoScreenWipe(void);
void HWR_PrepFadeToBlack(boolean white);
void HWR_DrawIntermissionBG(void);

extern CV_PossibleValue_t granisotropicmode_cons_t[];

extern consvar_t cv_grfov;
extern consvar_t cv_grmd2;
extern consvar_t cv_grfog;
extern consvar_t cv_grfogdensity;
extern consvar_t cv_grgammared;
extern consvar_t cv_grgammagreen;
extern consvar_t cv_grgammablue;
extern consvar_t cv_grfiltermode;
extern consvar_t cv_granisotropicmode;
extern consvar_t cv_grfogcolor;
extern consvar_t cv_grcompat;

extern consvar_t cv_grtest;

extern consvar_t cv_motionblur;

extern float gr_viewwidth, gr_viewheight, gr_baseviewwindowy;

extern float gr_viewwindowx, grfovadjust, gr_basewindowcentery;

// For new dynamic light casting on map geometry in OpenGL
extern fixed_t *hwlightbbox;

extern FTransform atransform;

// Transparent plane info
typedef struct
{
	const sector_t *sector;
	struct extrasubsector_s *xsub;
	fixed_t fixedheight;
	byte lightlevel;
	lumpnum_t lumpnum;
	byte alpha;
	sector_t *FOFSector;
	size_t drawcount;
	extracolormap_t *planecolormap;
	FBITFIELD     blendmode;
} transplaneinfo_t;

// Transparent wall
typedef struct
{
	wallVert3D    wallVerts[4];
	FSurfaceInfo  Surf;
	int           texnum;
	FBITFIELD     blendmode;
	size_t           drawcount;
	float walltop;
	float wallbottom;
	const sector_t *sector;
	boolean fogwall;
} transwallinfo_t;

#endif
