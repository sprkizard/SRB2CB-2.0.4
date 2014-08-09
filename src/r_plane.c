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
//
// DESCRIPTION:
//
//
//-----------------------------------------------------------------------------
/// \file
/// \brief Here is a core component: drawing the floors and ceilings,
///	while maintaining a per column clipping list only.
///	Moreover, the sky areas have to be determined.

#include "doomdef.h"
#include "console.h"
#include "g_game.h"
#include "r_data.h"
#include "r_local.h"
#include "r_state.h"
#include "r_splats.h" // faB(21jan):testing
#include "r_sky.h"
#include "v_video.h"
#include "w_wad.h"
#include "z_zone.h"
#include "p_tick.h"

#include "p_setup.h" // levelflats

#ifdef SESLOPE
#include "m_vector.h"
cb_slopespan_t slopespan;
float slopevis; // SoM: used in slope lighting
#endif

//
// opening
//

// Quincunx antialiasing of flats!
//#define QUINCUNX

//SoM: 3/23/2000: Use Boom visplane hashing.
#define MAXVISPLANES 512

static visplane_t *visplanes[MAXVISPLANES];
static visplane_t *freetail;
static visplane_t **freehead = &freetail;

visplane_t *floorplane;
visplane_t *ceilingplane;
static visplane_t *currentplane;

planemgr_t ffloor[MAXFFLOORS];
int numffloors;

//SoM: 3/23/2000: Boom visplane hashing routine.
#define visplane_hash(picnum,lightlevel,height) \
  ((unsigned int)((picnum)*3+(lightlevel)+(height)*7) & (MAXVISPLANES-1))

//SoM: 3/23/2000: Use boom opening limit removal
size_t maxopenings;
short *openings, *lastopening; /// \todo free leak

//
// Clip values are the solid pixel bounding the range.
//  floorclip starts out SCREENHEIGHT
//  ceilingclip starts out -1
//
short floorclip[MAXVIDWIDTH], ceilingclip[MAXVIDWIDTH];
fixed_t frontscale[MAXVIDWIDTH];

//
// spanstart holds the start of a plane span
// initialized to 0 at start
//
static int spanstart[MAXVIDHEIGHT];

//
// texture mapping
//
static lighttable_t **planezlight;
static fixed_t planeheight;

//added : 10-02-98: yslopetab is what yslope used to be,
//                yslope points somewhere into yslopetab,
//                now (viewheight/2) slopes are calculated above and
//                below the original viewheight for mouselook
//                (this is to calculate yslopes only when really needed)
//                (when mouselookin', yslope is moving into yslopetab)
//                Check R_SetupFrame, R_SetViewSize for more...
fixed_t yslopetab[MAXVIDHEIGHT*4];
fixed_t *yslope;

fixed_t distscale[MAXVIDWIDTH];
fixed_t basexscale, baseyscale;

fixed_t cachedheight[MAXVIDHEIGHT];
fixed_t cacheddistance[MAXVIDHEIGHT];
fixed_t cachedxstep[MAXVIDHEIGHT];
fixed_t cachedystep[MAXVIDHEIGHT];

static fixed_t xoffs, yoffs;


#ifdef SESLOPE

#endif

//
// R_MapPlane
//
// Uses global vars:
//  planeheight
//  ds_source
//  basexscale
//  baseyscale
//  viewx
//  viewy
//  xoffs
//  yoffs
//  planeangle
//
// BASIC PRIMITIVE
//
static int bgofs;
static int wtofs=0;
static int waterofs;
static boolean itswater;

#ifdef __mips__
//#define NOWATER
#endif

#ifndef NOWATER
static void R_DrawTranslucentWaterSpan_8(void)
{
	unsigned int xposition;
	unsigned int yposition;
	unsigned int xstep, ystep;

	byte *source;
	byte *colormap;
	byte *dest;
	byte *dsrc;

	unsigned int count;

	// SoM: we only need 6 bits for the integer part (0 thru 63) so the rest
	// can be used for the fraction part. This allows calculation of the memory address in the
	// texture with two shifts, an OR and one AND. (see below)
	// for texture sizes > 64 the amount of precision we can allow will decrease, but only by one
	// bit per power of two (obviously)
	// Ok, because I was able to eliminate the variable spot below, this function is now FASTER
	// than the original span renderer. Whodathunkit?
	xposition = ds_xfrac << nflatshiftup; yposition = (ds_yfrac + waterofs) << nflatshiftup;
	xstep = ds_xstep << nflatshiftup; ystep = ds_ystep << nflatshiftup;

	source = ds_source;
	colormap = ds_colormap;
	dest = ylookup[ds_y] + columnofs[ds_x1];
	dsrc = screens[1] + (ds_y+bgofs)*vid.width + ds_x1;
	count = ds_x2 - ds_x1 + 1;

	while (count >= 8)
	{
		// SoM: Why didn't I see this earlier? the spot variable is a waste now because we don't
		// have the uber complicated math to calculate it now, so that was a memory write we didn't
		// need!
		dest[0] = colormap[*(ds_transmap + (source[((yposition >> nflatyshift) & nflatmask) | (xposition >> nflatxshift)] << 8) + *dsrc++)];
		xposition += xstep;
		yposition += ystep;

		dest[1] = colormap[*(ds_transmap + (source[((yposition >> nflatyshift) & nflatmask) | (xposition >> nflatxshift)] << 8) + *dsrc++)];
		xposition += xstep;
		yposition += ystep;

		dest[2] = colormap[*(ds_transmap + (source[((yposition >> nflatyshift) & nflatmask) | (xposition >> nflatxshift)] << 8) + *dsrc++)];
		xposition += xstep;
		yposition += ystep;

		dest[3] = colormap[*(ds_transmap + (source[((yposition >> nflatyshift) & nflatmask) | (xposition >> nflatxshift)] << 8) + *dsrc++)];
		xposition += xstep;
		yposition += ystep;

		dest[4] = colormap[*(ds_transmap + (source[((yposition >> nflatyshift) & nflatmask) | (xposition >> nflatxshift)] << 8) + *dsrc++)];
		xposition += xstep;
		yposition += ystep;

		dest[5] = colormap[*(ds_transmap + (source[((yposition >> nflatyshift) & nflatmask) | (xposition >> nflatxshift)] << 8) + *dsrc++)];
		xposition += xstep;
		yposition += ystep;

		dest[6] = colormap[*(ds_transmap + (source[((yposition >> nflatyshift) & nflatmask) | (xposition >> nflatxshift)] << 8) + *dsrc++)];
		xposition += xstep;
		yposition += ystep;

		dest[7] = colormap[*(ds_transmap + (source[((yposition >> nflatyshift) & nflatmask) | (xposition >> nflatxshift)] << 8) + *dsrc++)];
		xposition += xstep;
		yposition += ystep;

		dest += 8;
		count -= 8;
	}
	while (count--)
	{
		*dest++ = colormap[*(ds_transmap + (source[((yposition >> nflatyshift) & nflatmask) | (xposition >> nflatxshift)] << 8) + *dsrc++)];
		xposition += xstep;
		yposition += ystep;
	}
}
#endif

#ifdef SESLOPE

// BIG FLATS
static void R_Throw(void)
{
	I_Error("R_Throw called.\n");
}

void (*slopefunc)(void) = R_Throw;

cb_span_t      span;
cb_plane_t     plane;
cb_slopespan_t slopespan;


//
// R_SlopeLights
//
static void R_SlopeLights(int len, double startmap, double endmap)
{
	int i;
	fixed_t map, map2, step;
	
	if(plane.fixedcolormap)
	{
		for(i = 0; i < len; i++)
			slopespan.colormap[i] = plane.fixedcolormap;
		return;
	}
	
	map = FLOAT_TO_FIXED((startmap / 256.0 * NUMCOLORMAPS));
	map2 = FLOAT_TO_FIXED((endmap / 256.0 * NUMCOLORMAPS));
	
	if(len > 1)
		step = (map2 - map) / (len - 1);
	else
		step = 0;
	
	for(i = 0; i < len; i++)
	{
		int index = (int)(map >> FRACBITS) + 1;
		
		if(index < 0)
			slopespan.colormap[i] = (byte *)(plane.colormap);
		else if(index >= NUMCOLORMAPS)
			slopespan.colormap[i] = (byte *)(plane.colormap + ((NUMCOLORMAPS - 1) * 256));
		else
			slopespan.colormap[i] = (byte *)(plane.colormap + (index * 256));
		
		map += step;
	}
}

//
// R_MapSlope
//
static void R_MapSlope(int y, int x1, int x2)
{
#if 1
	rslope_t *slope = plane.slope;
	int count = x2 - x1;
	v3float_t s;
	double map1, map2;
	
	s.x = x1 - FIXED_TO_FLOAT(viewwidth) * 0.5f; // view.xcenter
	s.y = y - FIXED_TO_FLOAT(viewheight) * 0.5f + 1; // view.ycenter
	float vfov = 90.0f * M_PI / 180.0f; // view.fov
	float vtan = (float)tan(vfov / 2);
	s.z = FIXED_TO_FLOAT(viewwidth) * 0.5f / vtan; // view.xfoc = view.xcenter/ view.tan
	
	slopespan.iufrac = M_DotVec3f(&s, &slope->A) * (float)plane.tex->width;
	slopespan.ivfrac = M_DotVec3f(&s, &slope->B) * (float)plane.tex->height;
	slopespan.idfrac = M_DotVec3f(&s, &slope->C);
	
	slopespan.iustep = slope->A.x * (float)plane.tex->width;
	slopespan.ivstep = slope->B.x * (float)plane.tex->height;
	slopespan.idstep = slope->C.x;
	
	slopespan.source = plane.source;
	slopespan.x1 = x1;
	slopespan.x2 = x2;
	slopespan.y = y;
	
	// Setup lighting
	
	map1 = 256.0 - (slope->shade - slope->plight * slopespan.idfrac);
	if(count > 0)
	{
		double id = slopespan.idfrac + slopespan.idstep * (x2 - x1);
		map2 = 256.0 - (slope->shade - slope->plight * id);
	}
	else
		map2 = map1;
	
	R_SlopeLights(x2 - x1 + 1, (256.0 - map1), (256.0 - map2));
	
	slopefunc();
#else
	angle_t angle;
	fixed_t distance, length;
	size_t pindex;
	
	// from r_splats's R_RenderFloorSplat
	if (x1 >= vid.width) x1 = vid.width - 1;
	
	if (planeheight != cachedheight[y])
	{
		cachedheight[y] = planeheight;
		distance = cacheddistance[y] = FixedMul(planeheight, yslope[y]);
		ds_xstep = cachedxstep[y] = FixedMul(distance, basexscale);
		ds_ystep = cachedystep[y] = FixedMul(distance, baseyscale);
	}
	else
	{
		distance = cacheddistance[y];
		ds_xstep = cachedxstep[y];
		ds_ystep = cachedystep[y];
	}
	
	length = FixedMul (distance,distscale[x1]);
	angle = (currentplane->viewangle + xtoviewangle[x1])>>ANGLETOFINESHIFT;
	/// \note Wouldn't it be faster just to add viewx and viewy
	// to the plane's x/yoffs anyway??
	
	ds_xfrac = FixedMul(FINECOSINE(angle), length) + xoffs;
	ds_yfrac = yoffs - FixedMul(FINESINE(angle), length);
	
	pindex = distance >> LIGHTZSHIFT;
	
	if (pindex >= MAXLIGHTZ)
		pindex = MAXLIGHTZ - 1;
	
	ds_colormap = planezlight[pindex];
	
	if (currentplane->extra_colormap)
		ds_colormap = currentplane->extra_colormap->colormap + (ds_colormap - colormaps);
	
	ds_y = y;
	ds_x1 = x1;
	ds_x2 = x2;
	
	spanfunc();
#endif
}


#define CompFloats(x, y) (fabs(x - y) < 0.001f)

//
// R_CompareSlopes
// 
// SoM: Returns true if the texture spaces of the give slope structs are the
// same.
//
boolean R_CompareSlopes(const pslope_t *s1, const pslope_t *s2)
{
	return 
	(s1 == s2) ||                 // both are equal, including both NULL; OR:
	(s1 && s2 &&                 // both are valid and...
	 CompFloats(s1->normalf.x, s2->normalf.x) &&  // components are equal and...
	 CompFloats(s1->normalf.y, s2->normalf.y) &&
	 CompFloats(s1->normalf.z, s2->normalf.z) &&
	 fabs(P_DistFromPlanef(&s2->of, &s1->of, &s1->normalf)) < 0.001f); // this.
}

#undef CompFloats


//
// R_CalcSlope
//
// SoM: Calculates the rslope info from the OHV vectors and rotation/offset 
// information in the plane struct
//
static void R_CalcSlope(visplane_t *pl)
{
	// This is the major part of the software slopped plane rendering
	double         xl, yl, tsin, tcos;
	double         ixscale, iyscale;
	rslope_t       *rslope = &pl->rslope;
	texture_t      *tex = textures[pl->picnum];
	
	if(!pl->pslope)
		return;
	
	
	tsin = sin(pl->plangle); // SESLOPE - different from viewangle?
	tcos = cos(pl->plangle);
	
	xl = tex->width;
	yl = tex->height;
	
	// SoM: To change the origin of rotation, add an offset to P.x and P.z
	// SoM: Add offsets? YAH!
	rslope->P.x = -FIXED_TO_FLOAT(pl->xoffs) * tcos - FIXED_TO_FLOAT(pl->yoffs) * tsin;
	rslope->P.z = -FIXED_TO_FLOAT(pl->xoffs) * tsin + FIXED_TO_FLOAT(pl->yoffs) * tcos;
	rslope->P.y = P_GetZAtf(pl->pslope, (float)rslope->P.x, (float)rslope->P.z);
	
	rslope->M.x = rslope->P.x - xl * tsin;
	rslope->M.z = rslope->P.z + xl * tcos;
	rslope->M.y = P_GetZAtf(pl->pslope, (float)rslope->M.x, (float)rslope->M.z);
	
	rslope->N.x = rslope->P.x + yl * tcos;
	rslope->N.z = rslope->P.z + yl * tsin;
	rslope->N.y = P_GetZAtf(pl->pslope, (float)rslope->N.x, (float)rslope->N.z);
	
	M_TranslateVec3f(&rslope->P);
	M_TranslateVec3f(&rslope->M);
	M_TranslateVec3f(&rslope->N);
	
	M_SubVec3f(&rslope->M, &rslope->M, &rslope->P);
	M_SubVec3f(&rslope->N, &rslope->N, &rslope->P);
	
	M_CrossProduct3f(&rslope->A, &rslope->P, &rslope->N);
	M_CrossProduct3f(&rslope->B, &rslope->P, &rslope->M);
	M_CrossProduct3f(&rslope->C, &rslope->M, &rslope->N);
	
	// This is helpful for removing some of the muls when calculating light.
	
	float ratio = 1.0f;
	
	if(vid.height == 200 || vid.height == 400)
		ratio = 1.0f;
	else 
		ratio = 1.2f;
	
	float viewfov = 90.0f * M_PI / 180.0f;
	float vtan = (float)tan(viewfov / 2);
	float viewtan = vtan;
	float viewxcenter = FIXED_TO_FLOAT(viewwidth) * 0.5f;
	float viewxfoc = viewxcenter / vtan;
	float viewyfoc = viewxfoc * ratio;
	float viewfocratio = viewyfoc / viewxfoc;
	
	rslope->A.x *= 0.5f;
	rslope->A.y *= 0.5f / viewfocratio; // view.focratio = 
	rslope->A.z *= 0.5f;
	
	rslope->B.x *= 0.5f;
	rslope->B.y *= 0.5f / viewfocratio;
	rslope->B.z *= 0.5f;
	
	rslope->C.x *= 0.5f;
	rslope->C.y *= 0.5f / viewfocratio;
	rslope->C.z *= 0.5f;
	
	rslope->zat = P_GetZAtf(pl->pslope, FIXED_TO_FLOAT(pl->viewx), FIXED_TO_FLOAT(pl->viewy)); // pl->viewx , pl->viewy
	
	// More help from randy. I was totally lost on this... 
	ixscale = viewtan / (float)xl;
	iyscale = viewtan / (float)yl;
	
	rslope->plight = (slopevis * ixscale * iyscale) / (rslope->zat - FIXED_TO_FLOAT(pl->viewz)); // pl->viewzf
	rslope->shade = 256.0f * 2.0f - (pl->lightlevel + 16.0f) * 256.0f / 128.0f;
}



//==============================================================================
//
// Slope span drawers
//

#define SPANJUMP 16
#define INTERPSTEP (0.0625f)

fixed_t mapindex = 0;


void R_DrawSlope_8_64(void)
{
	double iu = slopespan.iufrac, iv = slopespan.ivfrac;
	double ius = slopespan.iustep, ivs = slopespan.ivstep;
	double id = slopespan.idfrac, ids = slopespan.idstep;
	
	byte *src, *dest;
	byte *colormap;
	int count;
	//fixed_t mapindex = 0;
	
	if((count = slopespan.x2 - slopespan.x1 + 1) < 0)
		return;
	
	src = ds_source;//(byte *)slopespan.source;
	dest = (byte *)(void *)(ylookup[slopespan.y] + columnofs[slopespan.x1]);
	
#if 0
	// Perfect *slow* render
	while(count--)
	{
		float mul = 1.0f / id;
		
		int u = (int)(iu * mul);
		int v = (int)(iv * mul);
		unsigned texl = (v & 63) * 64 + (u & 63);
		
		*dest++ = src[texl];
		dest++;
		
		iu += ius;
		iv += ivs;
		id += ids;
	}
#else
	while(count >= SPANJUMP)
	{
		double ustart, uend;
		double vstart, vend;
		double mulstart, mulend;
		unsigned int ustep, vstep, ufrac, vfrac;
		int incount;
		
		// TODO: 
		mulstart = 65536.0f / id;
		id += ids * SPANJUMP;
		mulend = 65536.0f / id;
		
		ufrac = (int)(ustart = iu * mulstart);
		vfrac = (int)(vstart = iv * mulstart);
		iu += ius * SPANJUMP;
		iv += ivs * SPANJUMP;
		uend = iu * mulend;
		vend = iv * mulend;
		
		ustep = (int)((uend - ustart) * INTERPSTEP);
		vstep = (int)((vend - vstart) * INTERPSTEP);
		
		incount = SPANJUMP;
		while(incount--)
		{
			colormap = ds_colormap;//slopespan.colormap[mapindex ++];
			*dest++ = colormap[src[((vfrac >> 10) & 0xFC0) | ((ufrac >> 16) & 63)]];
			ufrac += ustep;
			vfrac += vstep;
		}
		
		count -= SPANJUMP;
	}
	if(count > 0)
	{
		double ustart, uend;
		double vstart, vend;
		double mulstart, mulend;
		unsigned int ustep, vstep, ufrac, vfrac;
		int incount;
		
		mulstart = 65536.0f / id;
		id += ids * count;
		mulend = 65536.0f / id;
		
		ufrac = (int)(ustart = iu * mulstart);
		vfrac = (int)(vstart = iv * mulstart);
		iu += ius * count;
		iv += ivs * count;
		uend = iu * mulend;
		vend = iv * mulend;
		
		ustep = (int)((uend - ustart) / count);
		vstep = (int)((vend - vstart) / count);
		
		incount = count;
		while(incount--)
		{
			colormap = ds_colormap;//slopespan.colormap[mapindex ++];
			*dest++ = colormap[src[((vfrac >> 10) & 0xFC0) | ((ufrac >> 16) & 63)]];
			ufrac += ustep;
			vfrac += vstep;
		}
	}
#endif // Slow render vs fast render
}



void R_DrawSlope_8_128(void)
{
	double iu = slopespan.iufrac, iv = slopespan.ivfrac;
	double ius = slopespan.iustep, ivs = slopespan.ivstep;
	double id = slopespan.idfrac, ids = slopespan.idstep;
	
	byte *src, *dest, *colormap;
	int count;
	//fixed_t mapindex = 0;
	
	if((count = slopespan.x2 - slopespan.x1 + 1) < 0)
		return;
	
	src = (byte *)slopespan.source;
	dest = ylookup[slopespan.y] + columnofs[slopespan.x1];
	
	while(count >= SPANJUMP)
	{
		double ustart, uend;
		double vstart, vend;
		double mulstart, mulend;
		unsigned int ustep, vstep, ufrac, vfrac;
		int incount;
		
		// TODO: 
		mulstart = 65536.0f / id;
		id += ids * SPANJUMP;
		mulend = 65536.0f / id;
		
		ufrac = (int)(ustart = iu * mulstart);
		vfrac = (int)(vstart = iv * mulstart);
		iu += ius * SPANJUMP;
		iv += ivs * SPANJUMP;
		uend = iu * mulend;
		vend = iv * mulend;
		
		ustep = (int)((uend - ustart) * INTERPSTEP);
		vstep = (int)((vend - vstart) * INTERPSTEP);
		
		incount = SPANJUMP;
		while(incount--)
		{
			colormap = ds_colormap;//slopespan.colormap[mapindex ++];
			*dest++ = colormap[src[((vfrac >> 9) & 0x3F80) | ((ufrac >> 16) & 0x7F)]];
			ufrac += ustep;
			vfrac += vstep;
		}
		
		count -= SPANJUMP;
	}
	if(count > 0)
	{
		double ustart, uend;
		double vstart, vend;
		double mulstart, mulend;
		unsigned int ustep, vstep, ufrac, vfrac;
		int incount;
		
		mulstart = 65536.0f / id;
		id += ids * count;
		mulend = 65536.0f / id;
		
		ufrac = (int)(ustart = iu * mulstart);
		vfrac = (int)(vstart = iv * mulstart);
		iu += ius * count;
		iv += ivs * count;
		uend = iu * mulend;
		vend = iv * mulend;
		
		ustep = (int)((uend - ustart) / count);
		vstep = (int)((vend - vstart) / count);
		
		incount = count;
		while(incount--)
		{
			colormap = ds_colormap;//slopespan.colormap[mapindex ++];
			*dest++ = colormap[src[((vfrac >> 9) & 0x3F80) | ((ufrac >> 16) & 0x7F)]];
			ufrac += ustep;
			vfrac += vstep;
		}
	}
}

void R_DrawSlope_8_256(void)
{
	double iu = slopespan.iufrac, iv = slopespan.ivfrac;
	double ius = slopespan.iustep, ivs = slopespan.ivstep;
	double id = slopespan.idfrac, ids = slopespan.idstep;
	
	byte *src, *dest, *colormap;
	int count;
	//fixed_t mapindex = 0;
	
	if((count = slopespan.x2 - slopespan.x1 + 1) < 0)
		return;
	
	src = (byte *)slopespan.source;
	dest = ylookup[slopespan.y] + columnofs[slopespan.x1];
	
	while(count >= SPANJUMP)
	{
		double ustart, uend;
		double vstart, vend;
		double mulstart, mulend;
		unsigned int ustep, vstep, ufrac, vfrac;
		int incount;
		
		// TODO: 
		mulstart = 65536.0f / id;
		id += ids * SPANJUMP;
		mulend = 65536.0f / id;
		
		ufrac = (int)(ustart = iu * mulstart);
		vfrac = (int)(vstart = iv * mulstart);
		iu += ius * SPANJUMP;
		iv += ivs * SPANJUMP;
		uend = iu * mulend;
		vend = iv * mulend;
		
		ustep = (int)((uend - ustart) * INTERPSTEP);
		vstep = (int)((vend - vstart) * INTERPSTEP);
		
		incount = SPANJUMP;
		while(incount--)
		{
			colormap = ds_colormap;//slopespan.colormap[mapindex ++];
			*dest++ = colormap[src[((vfrac >> 8) & 0xFF00) | ((ufrac >> 16) & 0xFF)]];
			ufrac += ustep;
			vfrac += vstep;
		}
		
		count -= SPANJUMP;
	}
	if(count > 0)
	{
		double ustart, uend;
		double vstart, vend;
		double mulstart, mulend;
		unsigned int ustep, vstep, ufrac, vfrac;
		int incount;
		
		mulstart = 65536.0f / id;
		id += ids * count;
		mulend = 65536.0f / id;
		
		ufrac = (int)(ustart = iu * mulstart);
		vfrac = (int)(vstart = iv * mulstart);
		iu += ius * count;
		iv += ivs * count;
		uend = iu * mulend;
		vend = iv * mulend;
		
		ustep = (int)((uend - ustart) / count);
		vstep = (int)((vend - vstart) / count);
		
		incount = count;
		while(incount--)
		{
			colormap = ds_colormap;//slopespan.colormap[mapindex ++];
			*dest++ = colormap[src[((vfrac >> 8) & 0xFF00) | ((ufrac >> 16) & 0xFF)]];
			ufrac += ustep;
			vfrac += vstep;
		}
	}
}

void R_DrawSlope_8_512(void)
{
	double iu = slopespan.iufrac, iv = slopespan.ivfrac;
	double ius = slopespan.iustep, ivs = slopespan.ivstep;
	double id = slopespan.idfrac, ids = slopespan.idstep;
	
	byte *src, *dest, *colormap;
	int count;
	//fixed_t mapindex = 0;
	
	if((count = slopespan.x2 - slopespan.x1 + 1) < 0)
		return;
	
	src = (byte *)slopespan.source;
	dest = ylookup[slopespan.y] + columnofs[slopespan.x1];
	
	while(count >= SPANJUMP)
	{
		double ustart, uend;
		double vstart, vend;
		double mulstart, mulend;
		unsigned int ustep, vstep, ufrac, vfrac;
		int incount;
		
		// TODO: 
		mulstart = 65536.0f / id;
		id += ids * SPANJUMP;
		mulend = 65536.0f / id;
		
		ufrac = (int)(ustart = iu * mulstart);
		vfrac = (int)(vstart = iv * mulstart);
		iu += ius * SPANJUMP;
		iv += ivs * SPANJUMP;
		uend = iu * mulend;
		vend = iv * mulend;
		
		ustep = (int)((uend - ustart) * INTERPSTEP);
		vstep = (int)((vend - vstart) * INTERPSTEP);
		
		incount = SPANJUMP;
		while(incount--)
		{
			colormap = ds_colormap;//slopespan.colormap[mapindex ++];
			*dest++ = colormap[src[((vfrac >> 7) & 0x3FE00) | ((ufrac >> 16) & 0x1FF)]];
			ufrac += ustep;
			vfrac += vstep;
		}
		
		count -= SPANJUMP;
	}
	if(count > 0)
	{
		double ustart, uend;
		double vstart, vend;
		double mulstart, mulend;
		unsigned int ustep, vstep, ufrac, vfrac;
		int incount;
		
		mulstart = 65536.0f / id;
		id += ids * count;
		mulend = 65536.0f / id;
		
		ufrac = (int)(ustart = iu * mulstart);
		vfrac = (int)(vstart = iv * mulstart);
		iu += ius * count;
		iv += ivs * count;
		uend = iu * mulend;
		vend = iv * mulend;
		
		ustep = (int)((uend - ustart) / count);
		vstep = (int)((vend - vstart) / count);
		
		incount = count;
		while(incount--)
		{
			colormap = ds_colormap;//slopespan.colormap[mapindex ++];
			*dest++ = colormap[src[((vfrac >> 7) & 0x3FE00) | ((ufrac >> 16) & 0x1FF)]];
			ufrac += ustep;
			vfrac += vstep;
		}
	}
}



void R_DrawSlope_8_GEN(void)
{
	double iu = slopespan.iufrac, iv = slopespan.ivfrac;
	double ius = slopespan.iustep, ivs = slopespan.ivstep;
	double id = slopespan.idfrac, ids = slopespan.idstep;
	
	byte *src, *dest, *colormap;
	int count;
	//fixed_t mapindex = 0;
	
	if((count = slopespan.x2 - slopespan.x1 + 1) < 0)
		return;
	
	src = (byte *)slopespan.source;
	dest = ylookup[slopespan.y] + columnofs[slopespan.x1];
	
	while(count >= SPANJUMP)
	{
		double ustart, uend;
		double vstart, vend;
		double mulstart, mulend;
		unsigned int ustep, vstep, ufrac, vfrac;
		int incount;
		
		// TODO: 
		mulstart = 65536.0f / id;
		id += ids * SPANJUMP;
		mulend = 65536.0f / id;
		
		ufrac = (int)(ustart = iu * mulstart);
		vfrac = (int)(vstart = iv * mulstart);
		iu += ius * SPANJUMP;
		iv += ivs * SPANJUMP;
		uend = iu * mulend;
		vend = iv * mulend;
		
		ustep = (int)((uend - ustart) * INTERPSTEP);
		vstep = (int)((vend - vstart) * INTERPSTEP);
		
		incount = SPANJUMP;
		while(incount--)
		{
			colormap = ds_colormap;//slopespan.colormap[mapindex ++];
			*dest++ = colormap[src[((vfrac >> span.xshift) & span.xmask) | ((ufrac >> 16) & span.ymask)]];
			ufrac += ustep;
			vfrac += vstep;
		}
		
		count -= SPANJUMP;
	}
	if(count > 0)
	{
		double ustart, uend;
		double vstart, vend;
		double mulstart, mulend;
		unsigned int ustep, vstep, ufrac, vfrac;
		int incount;
		
		mulstart = 65536.0f / id;
		id += ids * count;
		mulend = 65536.0f / id;
		
		ufrac = (int)(ustart = iu * mulstart);
		vfrac = (int)(vstart = iv * mulstart);
		iu += ius * count;
		iv += ivs * count;
		uend = iu * mulend;
		vend = iv * mulend;
		
		ustep = (int)((uend - ustart) / count);
		vstep = (int)((vend - vstart) / count);
		
		incount = count;
		while(incount--)
		{
			colormap = ds_colormap;//slopespan.colormap[mapindex ++];
			*dest++ = colormap[src[((vfrac >> span.xshift) & span.xmask) | ((ufrac >> 16) & span.ymask)]];
			ufrac += ustep;
			vfrac += vstep;
		}
	}
}




#endif // SESLOPE

void R_MapPlane(int y, int x1, int x2)
{
	angle_t angle;
	fixed_t distance, length;
	size_t pindex;

#ifdef RANGECHECK
	if (x2 < x1 || x1 < 0 || x2 >= viewwidth || y > viewheight)
		I_Error("R_MapPlane: %d, %d at %d", x1, x2, y);
#endif

	// from r_splats's R_RenderFloorSplat
	if (x1 >= vid.width) x1 = vid.width - 1;

	if (planeheight != cachedheight[y])
	{
		cachedheight[y] = planeheight;
		distance = cacheddistance[y] = FixedMul(planeheight, yslope[y]);
		ds_xstep = cachedxstep[y] = FixedMul(distance, basexscale);
		ds_ystep = cachedystep[y] = FixedMul(distance, baseyscale);
	}
	else
	{
		distance = cacheddistance[y];
		ds_xstep = cachedxstep[y];
		ds_ystep = cachedystep[y];
	}

	length = FixedMul (distance,distscale[x1]);
	angle = (currentplane->viewangle + xtoviewangle[x1])>>ANGLETOFINESHIFT;
	/// \note Wouldn't it be faster just to add viewx and viewy
	// to the plane's x/yoffs anyway??

	ds_xfrac = FixedMul(FINECOSINE(angle), length) + xoffs;
	ds_yfrac = yoffs - FixedMul(FINESINE(angle), length);

	if (itswater)
	{
		const int yay = (wtofs + (distance>>10) ) & 8191;
		// ripples da water texture
		bgofs = FixedDiv(FINESINE(yay),distance>>9)>>FRACBITS;

		angle = (angle + 2048) & 8191;  //90¯
		ds_xfrac += FixedMul(FINECOSINE(angle), (bgofs<<FRACBITS));
		ds_yfrac += FixedMul(FINESINE(angle), (bgofs<<FRACBITS));

		if (y+bgofs>=viewheight)
			bgofs = viewheight-y-1;
		if (y+bgofs<0)
			bgofs = -y;
	}

	pindex = distance >> LIGHTZSHIFT;

	if (pindex >= MAXLIGHTZ)
		pindex = MAXLIGHTZ - 1;

	ds_colormap = planezlight[pindex];

	if (currentplane->extra_colormap)
		ds_colormap = currentplane->extra_colormap->colormap + (ds_colormap - colormaps);

	ds_y = y;
	ds_x1 = x1;
	ds_x2 = x2;

	spanfunc();
}

//
// R_ClearPlanes
// At begining of frame.
//
// NOTE: Uses con_clipviewtop, so that when console is on,
//       we don't draw the part of the view hidden under the console.
void R_ClearPlanes(void)
{
	int i, p;
	angle_t angle;

	// opening / clipping determination
	for (i = 0; i < viewwidth; i++)
	{
		floorclip[i] = (short)viewheight;
		ceilingclip[i] = (short)con_clipviewtop;
		frontscale[i] = MAXINT;
		for (p = 0; p < MAXFFLOORS; p++)
		{
			ffloor[p].f_clip[i] = (short)viewheight;
			ffloor[p].c_clip[i] = (short)con_clipviewtop;
		}
	}

	numffloors = 0;

	for (i = 0; i < MAXVISPLANES; i++)
	for (*freehead = visplanes[i], visplanes[i] = NULL;
		freehead && *freehead ;)
	{
		freehead = &(*freehead)->next;
	}

	lastopening = openings;

	// texture calculation
	memset(cachedheight, 0, sizeof (cachedheight));

	// left to right mapping
	angle = (viewangle-ANG90)>>ANGLETOFINESHIFT;

	// scale will be unit scale at SCREENWIDTH/2 distance
	basexscale = FixedDiv (FINECOSINE(angle),centerxfrac);
	baseyscale = -FixedDiv (FINESINE(angle),centerxfrac);
}

static visplane_t *new_visplane(unsigned int hash)
{
	visplane_t *check = freetail;
	if (!check)
	{
		check = calloc(1, sizeof (*check));
		if (check == NULL) I_Error("out of memory"); // FIXME: ugly
	}
	else
	{
		freetail = freetail->next;
		if (!freetail)
			freehead = &freetail;
	}
	check->next = visplanes[hash];
	visplanes[hash] = check;
	return check;
}

//
// R_FindPlane: Seek a visplane having the identical values:
//              Same height, same flattexture, same lightlevel.
//              If not, allocates another of them.
//
visplane_t *R_FindPlane(fixed_t height, long picnum, int lightlevel,
	fixed_t xoff, fixed_t yoff, angle_t plangle, extracolormap_t *planecolormap,
	ffloor_t *pfloor 
#ifdef SESLOPE
						,pslope_t *slope
#endif
)
{
	visplane_t *check;
	unsigned int hash;
#ifdef SESLOPE
	float tsin, tcos;
#endif

	// JTEFIX
	if (plangle != 0)
	{
		// Add the view offset, rotated by the plane angle.
		angle_t angle = plangle>>ANGLETOFINESHIFT;
		// SRB2CBTODO: This might need to be used for polyobjects
		xoff += FixedMul(viewx,FINECOSINE(angle))-FixedMul(viewy,FINESINE(angle));
		yoff += -FixedMul(viewx,FINESINE(angle))-FixedMul(viewy,FINECOSINE(angle));
	}
	else
	{
		xoff += viewx;
		yoff -= viewy;
	}

	// This appears to fix the Nimbus Ruins sky bug.
	if (picnum == skyflatnum && pfloor)
	{
		height = 0; // all skies map together
		lightlevel = 0;
	}

	// New visplane algorithm uses hash table
	hash = visplane_hash(picnum, lightlevel, height);

	for (check = visplanes[hash]; check; check = check->next)
	{
		if (height == check->height && picnum == check->picnum
			&& lightlevel == check->lightlevel
			&& xoff == check->xoffs && yoff == check->yoffs
			&& planecolormap == check->extra_colormap
			&& !pfloor && !check->ffloor
			&& check->viewz == viewz
			&& check->viewangle == viewangle
#ifdef SESLOPE
			&& R_CompareSlopes(check->pslope, slope)
#endif
			)
		{
			return check;
		}
	}

	check = new_visplane(hash);

	check->height = height;
	check->picnum = picnum;
	check->lightlevel = lightlevel;
	check->minx = vid.width;
	check->maxx = -1;
	check->xoffs = xoff;
	check->yoffs = yoff;
	check->extra_colormap = planecolormap;
	check->ffloor = pfloor;
	check->viewz = viewz;
#ifdef SESLOPE
	check->viewx = viewx;
	check->viewy = viewy;
#endif
	check->viewangle = viewangle + plangle;
	check->plangle = plangle;
	
#ifdef SESLOPE
	// SoM: set up slope type stuff
	check->pslope = slope;
	if(slope)
		R_CalcSlope(check);
	else
	{
		// haleyjd 01/05/08: rotate viewpoint by flat angle.
		// note that the signs of the sine terms must be reversed in order to flip
		// the y-axis of the flat relative to world coordinates
		
		tsin = (float) sin(check->plangle);
		tcos = (float) cos(check->plangle);
		check->viewx =  viewx * tcos + viewy * tsin;
		check->viewy = -viewx * tsin + viewy * tcos;
		check->viewz =  viewz;
	}
#endif
	
	memset(check->top, 0xff, sizeof (check->top));
	memset(check->bottom, 0x00, sizeof (check->bottom));

	return check;
}

//
// R_CheckPlane: return same visplane or alloc a new one if needed
//
visplane_t *R_CheckPlane(visplane_t *pl, int start, int stop)
{
	int intrl, intrh;
	int unionl, unionh;
	int x;

	if (start < pl->minx)
	{
		intrl = pl->minx;
		unionl = start;
	}
	else
	{
		unionl = pl->minx;
		intrl = start;
	}

	if (stop > pl->maxx)
	{
		intrh = pl->maxx;
		unionh = stop;
	}
	else
	{
		unionh = pl->maxx;
		intrh = stop;
	}

	// 0xff is not equal to -1 with shorts...
	for (x = intrl; x <= intrh; x++)
		if (pl->top[x] != 0xffff || pl->bottom[x] != 0x0000)
			break;

	if (x > intrh) /* Can use existing plane; extend range */
	{
		pl->minx = unionl;
		pl->maxx = unionh;
	}
	else /* Cannot use existing plane; create a new one */
	{
		unsigned int hash =
			visplane_hash(pl->picnum, pl->lightlevel, pl->height);
		visplane_t *new_pl = new_visplane(hash);

		new_pl->height = pl->height;
		new_pl->picnum = pl->picnum;
		new_pl->lightlevel = pl->lightlevel;
		new_pl->xoffs = pl->xoffs;
		new_pl->yoffs = pl->yoffs;
		new_pl->extra_colormap = pl->extra_colormap;
		new_pl->ffloor = pl->ffloor;
		new_pl->viewz = pl->viewz;
		new_pl->viewangle = pl->viewangle;
		new_pl->plangle = pl->plangle;
#ifdef POLYOBJECTS_PLANESS // Software
		new_pl->polyobj = pl->polyobj;
#endif
#ifdef SESLOPE
		new_pl->pslope = pl->pslope;
		memcpy(&new_pl->rslope, &pl->rslope, sizeof(rslope_t));
		new_pl->fullcolormap = pl->fullcolormap;
#endif
		
		
		
		pl = new_pl;
		pl->minx = start;
		pl->maxx = stop;
		memset(pl->top, 0xff, sizeof pl->top);
		memset(pl->bottom, 0x00, sizeof pl->bottom);
	}
	return pl;
}


//
// R_ExpandPlane
//
// This function basically expands the visplane or I_Errors.
// The reason for this is that when creating 3D floor planes, there is no
// need to create new ones with R_CheckPlane, because 3D floor planes
// are created by subsector and there is no way a subsector can graphically
// overlap.
void R_ExpandPlane(visplane_t *pl, int start, int stop)
{
	int intrl, intrh;
	int unionl, unionh;
//	int x;

#ifdef POLYOBJECTS_PLANESS // Software
	// Don't expand polyobject planes here - we do that on our own.
	//if (pl->polyobj)
	//	return;
#endif

	if (start < pl->minx)
	{
		intrl = pl->minx;
		unionl = start;
	}
	else
	{
		unionl = pl->minx;
		intrl = start;
	}

	if (stop > pl->maxx)
	{
		intrh = pl->maxx;
		unionh = stop;
	}
	else
	{
		unionh = pl->maxx;
		intrh = stop;
	}
/*
	for (x = start; x <= stop; x++)
		if (pl->top[x] != 0xffff || pl->bottom[x] != 0x0000)
			break;

	if (x <= stop)
		I_Error("R_ExpandPlane: planes in same subsector overlap?!\nminx: %d, maxx: %d, start: %d, stop: %d\n", pl->minx, pl->maxx, start, stop);
*/
	pl->minx = unionl, pl->maxx = unionh;
}

//
// R_MakeSpans
//
void R_MakeSpans(int x, int t1, int b1, int t2, int b2)
{	
	//    Alam: from r_splats's R_RenderFloorSplat
	if (t1 >= vid.height) t1 = vid.height-1;
	if (b1 >= vid.height) b1 = vid.height-1;
	if (t2 >= vid.height) t2 = vid.height-1;
	if (b2 >= vid.height) b2 = vid.height-1;
	if (x-1 >= vid.width) x = vid.width;

	while (t1 < t2 && t1 <= b1)
	{
		R_MapPlane(t1, spanstart[t1], x - 1);
		t1++;
	}
	while (b1 > b2 && b1 >= t1)
	{
		R_MapPlane(b1, spanstart[b1], x - 1);
		b1--;
	}

	while (t2 < t1 && t2 <= b2)
		spanstart[t2++] = x;
	while (b2 > b1 && b2 >= t2)
		spanstart[b2--] = x;
}

#ifdef SESLOPE
//
// R_MakeSlopeSpans
//
void R_MakeSlopeSpans(int x, int t1, int b1, int t2, int b2)
{
	//    Alam: from r_splats's R_RenderFloorSplat
	if (t1 >= vid.height) t1 = vid.height-1;
	if (b1 >= vid.height) b1 = vid.height-1;
	if (t2 >= vid.height) t2 = vid.height-1;
	if (b2 >= vid.height) b2 = vid.height-1;
	if (x-1 >= vid.width) x = vid.width;
	
	while (t1 < t2 && t1 <= b1)
	{
		// SESLOPE RIGHT HERE!!!! Use R_MapSlope, check pl
		R_MapSlope(t1, spanstart[t1], x - 1);
		t1++;
	}
	while (b1 > b2 && b1 >= t1)
	{
		R_MapSlope(b1, spanstart[b1], x - 1);
		b1--;
	}
	
	while (t2 < t1 && t2 <= b2)
		spanstart[t2++] = x;
	while (b2 > b1 && b2 >= t2)
		spanstart[b2--] = x;
}
#endif

void R_DrawPlanes(void)
{
	visplane_t *pl;
	int x;
	int angle;
	int i;

	spanfunc = basespanfunc;
	wallcolfunc = walldrawerfunc;
#ifdef SESLOPE
	slopefunc = R_DrawSlope_8_64;
#endif

	for (i = 0; i < MAXVISPLANES; i++, pl++)
	{
		for (pl = visplanes[i]; pl; pl = pl->next)
		{
			// sky flat, // SKWALL : this also draws the sky clipping walls
			if (pl->picnum == skyflatnum)
			{
				// use correct aspect ratio scale
				dc_iscale = skyscale;

				// Sky is always drawn full bright,
				//  i.e. colormaps[0] is used.
				// Because of this hack, sky is not affected
				//  by INVUL inverse mapping.
				dc_colormap = colormaps;
				dc_texturemid = skytexturemid;
				dc_texheight = textureheight[skytexture]
					>>FRACBITS;
				for (x = pl->minx; x <= pl->maxx; x++)
				{
					dc_yl = pl->top[x];
					dc_yh = pl->bottom[x];

					if (dc_yl <= dc_yh)
					{
						angle = (viewangle + xtoviewangle[x])>>ANGLETOSKYSHIFT;
						dc_x = x;
						dc_source =
							R_GetColumn(skytexture,
								angle);
						wallcolfunc();
					}
				}
				continue;
			}

			if (pl->ffloor != NULL)
				continue;

			R_DrawSinglePlane(pl);
		}
	}
	waterofs = (leveltime & 1*NEWTICRATERATIO)*16384;
	wtofs = leveltime * 76/NEWTICRATERATIO;
}

void R_DrawSinglePlane(visplane_t *pl) // this is do_draw_plane in Eternity
{
	int light = 0;
	int x;
	int stop, angle;
	size_t size;

	if (!(pl->minx <= pl->maxx))
		return;

	itswater = false;
	spanfunc = basespanfunc;
	if (pl->ffloor)
	{
		if (pl->ffloor->flags & FF_TRANSLUCENT)
		{
			spanfunc = R_DrawTranslucentSpan_8;

			// Hacked up support for alpha value in software mode
			if (pl->ffloor->alpha < 12)
				return; // Don't even draw it
			else if (pl->ffloor->alpha < 38)
				ds_transmap = ((tr_trans90)<<FF_TRANSSHIFT) - 0x10000 + transtables;
			else if (pl->ffloor->alpha < 64)
				ds_transmap = ((tr_trans80)<<FF_TRANSSHIFT) - 0x10000 + transtables;
			else if (pl->ffloor->alpha < 89)
				ds_transmap = ((tr_trans70)<<FF_TRANSSHIFT) - 0x10000 + transtables;
			else if (pl->ffloor->alpha < 115)
				ds_transmap = ((tr_trans60)<<FF_TRANSSHIFT) - 0x10000 + transtables;
			else if (pl->ffloor->alpha < 140)
				ds_transmap = ((tr_trans50)<<FF_TRANSSHIFT) - 0x10000 + transtables;
			else if (pl->ffloor->alpha < 166)
				ds_transmap = ((tr_trans40)<<FF_TRANSSHIFT) - 0x10000 + transtables;
			else if (pl->ffloor->alpha < 192)
				ds_transmap = ((tr_trans30)<<FF_TRANSSHIFT) - 0x10000 + transtables;
			else if (pl->ffloor->alpha < 217)
				ds_transmap = ((tr_trans20)<<FF_TRANSSHIFT) - 0x10000 + transtables;
			else if (pl->ffloor->alpha < 243)
				ds_transmap = ((tr_trans10)<<FF_TRANSSHIFT) - 0x10000 + transtables;
			else // Opaque, but allow transparent flat pixels
				spanfunc = splatfunc;

			if (pl->extra_colormap && pl->extra_colormap->fog)
				light = (pl->lightlevel >> LIGHTSEGSHIFT);
			else
				light = LIGHTLEVELS-1;
		}
		else if (pl->ffloor->flags & FF_FOG)
		{
			spanfunc = R_DrawFogSpan_8;
			light = (pl->lightlevel >> LIGHTSEGSHIFT);
		}
		else light = (pl->lightlevel >> LIGHTSEGSHIFT);

#ifndef NOWATER
		if (pl->ffloor->flags & FF_RIPPLE)
		{
			int top, bottom;

			itswater = true;
			if (spanfunc == R_DrawTranslucentSpan_8)
			{
				spanfunc = R_DrawTranslucentWaterSpan_8;

				// Copy the current scene
				top = pl->high-8;
				bottom = pl->low+8;

				if (top < 0)
					top = 0;
				if (bottom > vid.height)
					bottom = vid.height;

				// Only copy the part of the screen we need
				VID_BlitLinearScreen((splitscreen && viewplayer == &players[secondarydisplayplayer]) ? screens[0] + (top+(vid.height>>1))*vid.width : screens[0]+((top)*vid.width), screens[1]+((top)*vid.width),
				                     vid.width, bottom-top,
				                     vid.width, vid.width);
			}
		}
#endif
	}
	else light = (pl->lightlevel >> LIGHTSEGSHIFT);

	if (viewangle != pl->viewangle)
	{
		memset(cachedheight, 0, sizeof (cachedheight));
		angle = (pl->viewangle-ANG90)>>ANGLETOFINESHIFT;
		basexscale = FixedDiv(FINECOSINE(angle),centerxfrac);
		baseyscale = -FixedDiv(FINESINE(angle),centerxfrac);
		viewangle = pl->viewangle;
	}

	currentplane = pl;

	ds_source = (byte *)
		W_CacheLumpNum(levelflats[pl->picnum].lumpnum,
			PU_STATIC); // Stay here until Z_ChangeTag

	size = W_LumpLength(levelflats[pl->picnum].lumpnum);

	switch (size)
	{
		case 4194304: // 2048x2048 lump
			nflatmask = 0x3FF800;
			nflatxshift = 21;
			nflatyshift = 10;
			nflatshiftup = 5;
			break;
		case 1048576: // 1024x1024 lump
			nflatmask = 0xFFC00;
			nflatxshift = 22;
			nflatyshift = 12;
			nflatshiftup = 6;
			break;
		case 262144:// 512x512 lump'
			nflatmask = 0x3FE00;
			nflatxshift = 23;
			nflatyshift = 14;
			nflatshiftup = 7;
			break;
		case 65536: // 256x256 lump
			nflatmask = 0xFF00;
			nflatxshift = 24;
			nflatyshift = 16;
			nflatshiftup = 8;
			break;
		case 16384: // 128x128 lump
			nflatmask = 0x3F80;
			nflatxshift = 25;
			nflatyshift = 18;
			nflatshiftup = 9;
			break;
		case 1024: // 32x32 lump
			nflatmask = 0x3E0;
			nflatxshift = 27;
			nflatyshift = 22;
			nflatshiftup = 11;
			break;
		default: // 64x64 lump
			nflatmask = 0xFC0;
			nflatxshift = 26;
			nflatyshift = 20;
			nflatshiftup = 10;
			break;
	}

	xoffs = pl->xoffs;
	yoffs = pl->yoffs;
	planeheight = abs(pl->height - pl->viewz);

	if (light >= LIGHTLEVELS)
		light = LIGHTLEVELS-1;

	if (light < 0)
		light = 0;

	planezlight = zlight[light];

	// set the maximum value for unsigned
	pl->top[pl->maxx+1] = 0xffff;
	pl->top[pl->minx-1] = 0xffff;
	pl->bottom[pl->maxx+1] = 0x0000;
	pl->bottom[pl->minx-1] = 0x0000;

	stop = pl->maxx + 1;
	
	
#ifdef SESLOPE
	// SoM: Handled outside
	plane.tex = W_CacheLumpNum(levelflats[pl->picnum].lumpnum,
							   PU_STATIC);//R_CacheTexture(pl->picnum);
	plane.source = (byte *)W_CacheLumpNum(levelflats[pl->picnum].lumpnum, PU_STATIC);//plane.tex->buffer; // SESLOPE: Fix this!
	plane.colormap = pl->fullcolormap;
	// haleyjd 10/16/06
	plane.fixedcolormap = pl->fullcolormap; // SESLOPE: Temporary, should be fixedcolormap, not full
	
	// SoM: slopes
	plane.slope = pl->pslope ? &pl->rslope : NULL;
	plane.lightlevel = pl->lightlevel;
	
	//R_PlaneLight();
#endif

#ifdef SESLOPE
	if (pl->pslope)
	{
		for (x = pl->minx; x <= stop; x++)
		{
			R_MakeSlopeSpans(x, pl->top[x-1], pl->bottom[x-1],
						pl->top[x], pl->bottom[x]);
		}
	}
	else
#endif
	{
		for (x = pl->minx; x <= stop; x++)
		{
			R_MakeSpans(x, pl->top[x-1], pl->bottom[x-1],
							 pl->top[x], pl->bottom[x]);
		}
	}

/*
	QUINCUNX anti-aliasing technique (sort of)
	 
	 Normally, Quincunx antialiasing staggers pixels
	 in a 5-die pattern like so:
	 
	 o   o
	 o
	 o   o
	 
	 To simulate this, we offset the plane by
	 FRACUNIT/4 in each direction, and draw
	 at 50% translucency. The result is
	 a 'smoothing' of the texture while
	 using the palette colors. 
 */
	// So basically, this is an attempt at anti-anilising planes in
#ifdef QUINCUNX
	if (spanfunc == R_DrawSpan_8)
	{
		int i;
		ds_transmap = ((tr_trans50)<<FF_TRANSSHIFT) - 0x10000 + transtables;
		spanfunc = R_DrawTranslucentSpan_8;
		for (i = 0; i < 4; i++)
		{
			xoffs = pl->xoffs;
			yoffs = pl->yoffs;

			switch(i)
			{
				case 0:
					xoffs -= FRACUNIT/4;
					yoffs -= FRACUNIT/4;
					break;
				case 1:
					xoffs -= FRACUNIT/4;
					yoffs += FRACUNIT/4;
					break;
				case 2:
					xoffs += FRACUNIT/4;
					yoffs -= FRACUNIT/4;
					break;
				case 3:
					xoffs += FRACUNIT/4;
					yoffs += FRACUNIT/4;
					break;
			}
			planeheight = abs(pl->height - pl->viewz);

			if (light >= LIGHTLEVELS)
				light = LIGHTLEVELS-1;

			if (light < 0)
				light = 0;

			planezlight = zlight[light];

			// set the maximum value for unsigned
			pl->top[pl->maxx+1] = 0xffff;
			pl->top[pl->minx-1] = 0xffff;
			pl->bottom[pl->maxx+1] = 0x0000;
			pl->bottom[pl->minx-1] = 0x0000;

			stop = pl->maxx + 1;

			for (x = pl->minx; x <= stop; x++)
				R_MakeSpans(x, pl->top[x-1], pl->bottom[x-1],
					pl->top[x], pl->bottom[x]);
		}
	}
#endif

	Z_ChangeTag(ds_source, PU_CACHE);
}

void R_PlaneBounds(visplane_t *plane)
{
	int i;
	int hi, low;

	hi = plane->top[plane->minx];
	low = plane->bottom[plane->minx];

	for (i = plane->minx + 1; i <= plane->maxx; i++)
	{
		if (plane->top[i] < hi)
		hi = plane->top[i];
		if (plane->bottom[i] > low)
		low = plane->bottom[i];
	}
	plane->high = hi;
	plane->low = low;
}
