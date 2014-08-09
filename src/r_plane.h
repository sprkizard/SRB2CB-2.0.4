// Emacs style mode select   -*- C++ -*-
//-----------------------------------------------------------------------------
//
// Copyright (C) 1993-1996 by id Software, Inc.
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
//-----------------------------------------------------------------------------
/// \file
/// \brief Refresh, visplane stuff (floor, ceilings)

#ifndef __R_PLANE__
#define __R_PLANE__

#include "screen.h" // needs MAXVIDWIDTH/MAXVIDHEIGHT
#include "r_data.h"
#include "p_polyobj.h"
#include "p_slopes.h"

//
// Now what is a visplane, anyway?
// Simple: kinda floor/ceiling polygon optimised for SRB2 rendering.
//
#ifdef SESLOPE
// SoM: Information used in texture mapping sloped planes
typedef struct
{
	v3float_t P, M, N;
	v3float_t A, B, C;
	float     zat, plight, shade;
} rslope_t; // SRB2CBTODO: USE ABC globally for slopes!
#endif

typedef struct visplane_s
{
	struct visplane_s *next;

	fixed_t height, viewz;
#ifdef SESLOPE
	fixed_t viewx, viewy;
#endif
	angle_t viewangle;
	angle_t plangle;
	long picnum;
	int lightlevel;
	int minx, maxx;

	// colormaps per sector
	extracolormap_t *extra_colormap;
#ifdef SESLOPE
	lighttable_t *fullcolormap;   // SoM: Used by slopes.
#endif

	// leave pads for [minx-1]/[maxx+1]

	// words are meh .. should get rid of that.. but eats memory
	// THIS IS UNSIGNED! VERY IMPORTANT!!
	USHORT pad1;
	USHORT top[MAXVIDWIDTH];
	USHORT pad2;
	USHORT pad3;
	USHORT bottom[MAXVIDWIDTH];
	USHORT pad4;

	int high, low; // R_PlaneBounds should set these.

	fixed_t xoffs, yoffs; // Scrolling flats.

	// SoM: frontscale should be stored in the first seg of the subsector
	// where the planes themselves are stored. I'm doing this now because
	// the old way caused trouble with the drawseg array was re-sized.
	int scaleseg;

	struct ffloor_s *ffloor;
#ifdef POLYOBJECTS_PLANESS // Software
	boolean polyobj;
#endif

#ifdef SESLOPE
	// Slopes!
	pslope_t *pslope;
	rslope_t rslope;	
#endif
	
} visplane_t;

extern visplane_t *floorplane;
extern visplane_t *ceilingplane;

#ifdef SESLOPE
boolean R_CompareSlopes(const pslope_t *s1, const pslope_t *s2);

extern int visplane_view;

typedef struct cb_span_s
	{
		int x1, x2, y;
		unsigned xfrac, yfrac, xstep, ystep;
		void *source;
		lighttable_t *colormap;
		unsigned int *fg2rgb, *bg2rgb; // haleyjd 06/20/08: tl lookups
		
		// SoM: some values for the generalizede span drawers
		unsigned int xshift, xmask, yshift, ymask;
	} cb_span_t;

typedef struct cb_plane_s
	{
		float xoffset, yoffset;
		float height;
		float pviewx, pviewy, pviewz, pviewsin, pviewcos;
		int   picnum;
		
		// SoM: we use different fixed point numbers for different flat sizes
		float fixedunitx, fixedunity;
		
		int lightlevel;
		float startmap;
		lighttable_t **planezlight;
		lighttable_t *colormap;
		lighttable_t *fixedcolormap;
		
		// SoM: Texture that covers the plane
		texture_t *tex;
		void      *source;
		
		// SoM: slopes.
		rslope_t *slope;
		
		void (*MapFunc)(int, int, int);
	} cb_plane_t;


typedef struct cb_slopespan_s
	{
		int y, x1, x2;
		
		double iufrac, ivfrac, idfrac;
		double iustep, ivstep, idstep;
		
		void *source;
		
		lighttable_t *colormap[MAXVIDWIDTH];
	} cb_slopespan_t;


extern cb_span_t  span;
extern cb_plane_t plane;
extern cb_slopespan_t slopespan;
#endif

// Visplane related.
extern short *lastopening, *openings;
extern size_t maxopenings;
typedef void (*planefunction_t)(int top, int bottom);

extern short floorclip[MAXVIDWIDTH], ceilingclip[MAXVIDWIDTH];
extern fixed_t frontscale[MAXVIDWIDTH], yslopetab[MAXVIDHEIGHT*4];
extern fixed_t cachedheight[MAXVIDHEIGHT];
extern fixed_t cacheddistance[MAXVIDHEIGHT];
extern fixed_t cachedxstep[MAXVIDHEIGHT];
extern fixed_t cachedystep[MAXVIDHEIGHT];
extern fixed_t basexscale, baseyscale;

extern fixed_t *yslope;
extern fixed_t distscale[MAXVIDWIDTH];

void R_ClearPlanes(void);

void R_MapPlane(int y, int x1, int x2);
void R_MakeSpans(int x, int t1, int b1, int t2, int b2);
#ifdef SESLOPE
void R_MakeSlopeSpans(int x, int t1, int b1, int t2, int b2);
#endif

void R_DrawPlanes(void);
#ifdef SESLOPE
visplane_t *R_FindPlane(fixed_t height, long picnum, int lightlevel, fixed_t xoff, fixed_t yoff, angle_t plangle,
						extracolormap_t *planecolormap, ffloor_t *ffloor, pslope_t *slope);
#else
visplane_t *R_FindPlane(fixed_t height, long picnum, int lightlevel, fixed_t xoff, fixed_t yoff, angle_t plangle,
	extracolormap_t *planecolormap, ffloor_t *ffloor);
#endif
visplane_t *R_CheckPlane(visplane_t *pl, int start, int stop);
void R_ExpandPlane(visplane_t *pl, int start, int stop);
void R_PlaneBounds(visplane_t *plane);

// Draws a single visplane.
void R_DrawSinglePlane(visplane_t *pl);

typedef struct planemgr_s
{
	visplane_t *plane;
	fixed_t height;
	boolean mark;
	fixed_t f_pos; // F for Front sector
	fixed_t b_pos; // B for Back sector
	fixed_t f_frac, f_step;
	fixed_t b_frac, b_step;
	short f_clip[MAXVIDWIDTH];
	short c_clip[MAXVIDWIDTH];

	struct ffloor_s *ffloor;
#ifdef POLYOBJECTS_PLANESS // Software
	polyobj_t *polyobj;
#endif
} planemgr_t;

extern planemgr_t ffloor[MAXFFLOORS];
extern int numffloors;
#endif
