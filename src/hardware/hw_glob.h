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
/// \brief globals (shared data & code) for hw_ modules

#ifndef _HWR_GLOB_H_
#define _HWR_GLOB_H_

#include "hw_defs.h"
#include "hw_main.h"

// The original aspect ratio isn't square
#define ORIGINAL_ASPECT (320.0f/200.0f)

// -----------
// structures
// -----------

// Global view of the OpenGL world // SRB2CBTODO: Allow global flat values
float gr_viewx, gr_viewy, gr_viewz;
float gr_viewsin, gr_viewcos;

float gr_viewludsin, gr_viewludcos;
float gr_fovlud, gr_fov;

// A vertex of a sector's 'plane' polygon
typedef struct
{
	float x;
	float y;
	float z; // For slopeness
} polyvertex_t;

#ifdef _MSC_VER
#pragma warning(disable :  4200)
#endif

// A convex 'plane' polygon, clockwise order
typedef struct
{
	size_t numpts;
	polyvertex_t pts[0]; // SRB2CBTODO: ** pts?
} poly_t;

#ifdef _MSC_VER
#pragma warning(default :  4200)
#endif

// holds extra info for 3D render, for each subsector in subsectors[]
typedef struct extrasubsector_s
{
	poly_t *planepoly;  // the generated convex polygon
	// SRB2CBTODO: POLYOBJECT planepoly
} extrasubsector_t;

// Needed for sprite rendering,
// equivalent of the software renderer's vissprites
typedef struct gr_vissprite_s
{
	// Doubly linked list
	struct gr_vissprite_s *prev;
	struct gr_vissprite_s *next;
	float x1, x2;
	float tz, ty;
	lumpnum_t patchlumpnum;
	boolean flip;
	mobj_t *mobj;
	boolean precip;
	boolean vflip;
	byte *colormap;

	// SRB2CBTODO: sprite cutting
	short sz, szt;
	fixed_t gx, gy; // for line side calculation
	fixed_t gz, gzt; // global bottom/top for silhouette clipping
	fixed_t pz, pzt; // physical bottom/top for sorting with 3D floors

	ULONG mobjflags;
	long heightsec; // height sector for underwater/fake ceiling support

	fixed_t thingheight; // The actual height of the thing (for 3D floors)
	sector_t *sector; // The sector containing the thing.
	// PAPERMARIO
#ifdef PAPERMARIO
	float y1, y2;
	float x, y, z;
	float ul, vt, vb, ur;
#endif
	size_t drawcount;

} gr_vissprite_t;

// --------
// hw_bsp.c
// --------
extern extrasubsector_t *extrasubsectors;

// --------
// hw_cache.c
// --------
void HWR_InitTextureCache(void);
void HWR_FreeTextureCache(void);
void HWR_FreeExtraSubsectors(void);
poly_t *GLBSP_PolyObjectCutOut(seg_t *lseg, int count, poly_t *poly); // SRB2CBTODO: Remove this

void HWR_GetFlat(lumpnum_t flatlumpnum, boolean anisotropic);
GLTexture_t *HWR_GetTexture(int tex, boolean anisotropic);
void HWR_GetPatch(GLPatch_t *gpatch);
void HWR_GetMappedPatch(GLPatch_t *gpatch, const byte *colormap);
GLPatch_t *HWR_GetPic(lumpnum_t lumpnum);
void HWR_SetPalette(RGBA_t *palette);

int HWR_GetFlatRez(lumpnum_t flatlumpnum);

// --------
// hw_draw.c
// --------
extern float gr_patch_scalex;
extern float gr_patch_scaley;

extern int patchformat;
extern int textureformat;

#endif //_HW_GLOB_
