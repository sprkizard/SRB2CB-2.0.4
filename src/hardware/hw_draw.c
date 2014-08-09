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
//-----------------------------------------------------------------------------
/// \file
/// \brief miscellaneous drawing (mainly 2d)

#ifdef __GNUC__
#include <unistd.h>
#endif

#include "../doomdef.h"

#ifdef HWRENDER
#include "hw_glob.h"
#include "hw_drv.h"

#include "../r_draw.h" // viewborderlump
#include "../m_misc.h"
#include "../r_main.h"
#include "../w_wad.h"
#include "../z_zone.h"
#include "../v_video.h"
#include "../st_stuff.h"

#if !(defined(__unix__) || defined(UNIXLIKE))
#ifdef __GNUC__
#include <sys/unistd.h>
#endif
#if !(defined(macintosh) || defined(__APPLE__))
#include <io.h>
#endif
#else
#endif // normalunix

#include <fcntl.h>
#include "../i_video.h"

float gr_patch_scalex;
float gr_patch_scaley;

typedef unsigned char GLRGB[3];

byte hudtrans = 255;

//
// -----------------+
// HWR_DrawPatch    : Draw a 'tile' graphic
// Notes            : x,y : positions relative to the original Doom resolution
//                  : textes(console+score) + menus + status bar
// -----------------+
void HWR_DrawPatch(GLPatch_t *gpatch, int x, int y, int option)
{
	FOutVector v[4];
	FBITFIELD flags;

	//  3--2
	//  | /|
	//  |/ |
	//  0--1
	float sdupx = vid.fdupx*2;
	float sdupy = vid.fdupy*2;
	float pdupx = vid.fdupx*2;
	float pdupy = vid.fdupy*2;

	// make patch ready in hardware cache
	HWR_GetPatch(gpatch);

#ifdef CONSCALE
	switch (option & V_SCALEPATCHMASK)
	{
		case V_NOSCALEPATCH:
			pdupx = pdupy = 2.0f;
			break;
		case V_SMALLSCALEPATCH:
			pdupx = 2.0f * vid.fsmalldupx;
			pdupy = 2.0f * vid.fsmalldupy;
			break;
		case V_MEDSCALEPATCH:
			pdupx = 2.0f * vid.fmeddupx;
			pdupy = 2.0f * vid.fmeddupy;
			break;
	}
#else
	if (option & V_NOSCALEPATCH)
		pdupx = pdupy = 2.0f;
#endif
	if (option & V_NOSCALESTART)
		sdupx = sdupy = 2.0f;

	v[0].x = v[3].x = (x*sdupx-gpatch->leftoffset*pdupx)/vid.width - 1;
	v[2].x = v[1].x = (x*sdupx+(gpatch->width-gpatch->leftoffset)*pdupx)/vid.width - 1;
	v[0].y = v[1].y = 1-(y*sdupy-gpatch->topoffset*pdupy)/vid.height;
	v[2].y = v[3].y = 1-(y*sdupy+(gpatch->height-gpatch->topoffset)*pdupy)/vid.height;

	v[0].z = v[1].z = v[2].z = v[3].z = 1.0f;

	v[0].sow = v[3].sow = 0.0f;
	v[2].sow = v[1].sow = gpatch->max_s;
	v[0].tow = v[1].tow = 0.0f;
	v[2].tow = v[3].tow = gpatch->max_t;

	flags = PF_Translucent|PF_NoDepthTest;

	if (option & V_WRAPX)
		flags |= PF_ForceWrapX;
	if (option & V_WRAPY)
		flags |= PF_ForceWrapY;

	if (option & V_TRANSLUCENT)
	{
		FSurfaceInfo Surf;
		Surf.FlatColor.s.red = Surf.FlatColor.s.green = Surf.FlatColor.s.blue = 0xff;
		Surf.FlatColor.s.alpha = hudtrans;
		flags |= PF_Modulated;
		GL_DrawPolygon(&Surf, v, 4, flags, 0);
	}
	else
		GL_DrawPolygon(NULL, v, 4, flags, 0);
}

void HWR_DrawClippedPatch (GLPatch_t *gpatch, int x, int y, int option)
{
	// Hardware clips the patch quite nicely anyway :)
	HWR_DrawPatch(gpatch, x, y, option); /// \todo // SRB2CBTODO: do real cliping
}

// Alam_GBC: Why? you could not get a FSurfaceInfo to set the alpha channel?
void HWR_DrawTranslucentPatch (GLPatch_t *gpatch, int x, int y, int option)
{
	FOutVector      v[4];
	FBITFIELD blendmode;

//  3--2
//  | /|
//  |/ |
//  0--1
	float sdupx = vid.fdupx*2;
	float sdupy = vid.fdupy*2;
	float pdupx = vid.fdupx*2;
	float pdupy = vid.fdupy*2;
	FSurfaceInfo Surf;

	// make patch ready in hardware cache
	HWR_GetPatch (gpatch);

#ifdef CONSCALE
	switch (option & V_SCALEPATCHMASK)
	{
		case V_NOSCALEPATCH:
			pdupx = pdupy = 2.0f;
			break;
		case V_SMALLSCALEPATCH:
			pdupx = 2.0f * vid.fsmalldupx;
			pdupy = 2.0f * vid.fsmalldupy;
			break;
		case V_MEDSCALEPATCH:
			pdupx = 2.0f * vid.fmeddupx;
			pdupy = 2.0f * vid.fmeddupy;
			break;
	}
#else
	if (option & V_NOSCALEPATCH)
		pdupx = pdupy = 2.0f;
#endif
	if (option & V_NOSCALESTART)
		sdupx = sdupy = 2.0f;

	v[0].x = v[3].x = (x*sdupx-gpatch->leftoffset*pdupx)/vid.width - 1;
	v[2].x = v[1].x = (x*sdupx+(gpatch->width-gpatch->leftoffset)*pdupx)/vid.width - 1;
	v[0].y = v[1].y = 1-(y*sdupy-gpatch->topoffset*pdupy)/vid.height;
	v[2].y = v[3].y = 1-(y*sdupy+(gpatch->height-gpatch->topoffset)*pdupy)/vid.height;

	v[0].z = v[1].z = v[2].z = v[3].z = 1.0f;

	v[0].sow = v[3].sow = 0.0f;
	v[2].sow = v[1].sow = gpatch->max_s;
	v[0].tow = v[1].tow = 0.0f;
	v[2].tow = v[3].tow = gpatch->max_t;

	blendmode = PF_Modulated|PF_Translucent|PF_NoDepthTest;

	if (option & V_WRAPX)
		blendmode |= PF_ForceWrapX;
	if (option & V_WRAPY)
		blendmode |= PF_ForceWrapY;

	Surf.FlatColor.s.red = Surf.FlatColor.s.green = Surf.FlatColor.s.blue = 0xff;

	if ((option & V_TRANSLUCENT) && hudtrans < 255)
		Surf.FlatColor.s.alpha = (hudtrans/2.0f);
	else
		Surf.FlatColor.s.alpha = 127;

	GL_DrawPolygon(&Surf, v, 4, blendmode, 0);
}

// Draws a patch 2x as small
void HWR_DrawSmallPatch (GLPatch_t *gpatch, int x, int y, int option, const byte *colormap)
{
	FOutVector      v[4];
	FBITFIELD flags;

	float sdupx = vid.fdupx;
	float sdupy = vid.fdupy;
	float pdupx = vid.fdupx;
	float pdupy = vid.fdupy;

	// make patch ready in hardware cache
	HWR_GetMappedPatch (gpatch, colormap);

	if (option & V_NOSCALEPATCH)
		pdupx = pdupy = 2.0f;
	if (option & V_NOSCALESTART)
		sdupx = sdupy = 2.0f;

	v[0].x = v[3].x = (x*sdupx-gpatch->leftoffset*pdupx)/vid.width - 1;
	v[2].x = v[1].x = (x*sdupx+(gpatch->width-gpatch->leftoffset)*pdupx)/vid.width - 1;
	v[0].y = v[1].y = 1-(y*sdupy-gpatch->topoffset*pdupy)/vid.height;
	v[2].y = v[3].y = 1-(y*sdupy+(gpatch->height-gpatch->topoffset)*pdupy)/vid.height;

	v[0].z = v[1].z = v[2].z = v[3].z = 1.0f;

	v[0].sow = v[3].sow = 0.0f;
	v[2].sow = v[1].sow = gpatch->max_s;
	v[0].tow = v[1].tow = 0.0f;
	v[2].tow = v[3].tow = gpatch->max_t;

	flags = PF_Translucent | PF_NoDepthTest;

	if (option & V_WRAPX)
		flags |= PF_ForceWrapX;
	if (option & V_WRAPY)
		flags |= PF_ForceWrapY;

	if (option & V_TRANSLUCENT)
	{
		FSurfaceInfo Surf;
		Surf.FlatColor.s.red = Surf.FlatColor.s.green = Surf.FlatColor.s.blue = 0xff;
		Surf.FlatColor.s.alpha = hudtrans;
		flags |= PF_Modulated;
		GL_DrawPolygon(&Surf, v, 4, flags, 0);
	}
	else
		GL_DrawPolygon(NULL, v, 4, flags, 0);
}

//
// HWR_DrawMappedPatch(): Like HWR_DrawPatch but with translated color
//
void HWR_DrawMappedPatch (GLPatch_t *gpatch, int x, int y, int option, const byte *colormap)
{
	FOutVector      v[4];
	FBITFIELD flags;

	float sdupx = vid.fdupx*2;
	float sdupy = vid.fdupy*2;
	float pdupx = vid.fdupx*2;
	float pdupy = vid.fdupy*2;

	// make patch ready in hardware cache
	HWR_GetMappedPatch (gpatch, colormap);

#ifdef CONSCALE
	switch (option & V_SCALEPATCHMASK)
	{
		case V_NOSCALEPATCH:
			pdupx = pdupy = 2.0f;
			break;
		case V_SMALLSCALEPATCH:
			pdupx = 2.0f * vid.fsmalldupx;
			pdupy = 2.0f * vid.fsmalldupy;
			break;
		case V_MEDSCALEPATCH:
			pdupx = 2.0f * vid.fmeddupx;
			pdupy = 2.0f * vid.fmeddupy;
			break;
	}
#else
	if (option & V_NOSCALEPATCH)
		pdupx = pdupy = 2.0f;
#endif
	if (option & V_NOSCALESTART)
		sdupx = sdupy = 2.0f;

	v[0].x = v[3].x = (x*sdupx-gpatch->leftoffset*pdupx)/vid.width - 1;
	v[2].x = v[1].x = (x*sdupx+(gpatch->width-gpatch->leftoffset)*pdupx)/vid.width - 1;
	v[0].y = v[1].y = 1-(y*sdupy-gpatch->topoffset*pdupy)/vid.height;
	v[2].y = v[3].y = 1-(y*sdupy+(gpatch->height-gpatch->topoffset)*pdupy)/vid.height;

	v[0].z = v[1].z = v[2].z = v[3].z = 1.0f;

	v[0].sow = v[3].sow = 0.0f;
	v[2].sow = v[1].sow = gpatch->max_s;
	v[0].tow = v[1].tow = 0.0f;
	v[2].tow = v[3].tow = gpatch->max_t;

	flags = PF_Translucent | PF_NoDepthTest;

	if (option & V_WRAPX)
		flags |= PF_ForceWrapX;
	if (option & V_WRAPY)
		flags |= PF_ForceWrapY;

	if (option & V_TRANSLUCENT)
	{
		FSurfaceInfo Surf;
		Surf.FlatColor.s.red = Surf.FlatColor.s.green = Surf.FlatColor.s.blue = 0xff;
		Surf.FlatColor.s.alpha = hudtrans;
		flags |= PF_Modulated;
		GL_DrawPolygon(&Surf, v, 4, flags, 0);
	}
	else
		GL_DrawPolygon(NULL, v, 4, flags, 0);
}

void HWR_DrawPic(int x, int y, lumpnum_t lumpnum)
{
	FOutVector      v[4];
	const GLPatch_t    *patch;

	// Make the picture ready in the hardware cache
	patch = HWR_GetPic(lumpnum);

	//  3--2
	//  | /|
	//  |/ |
	//  0--1

	v[0].x = v[3].x = (float)2.0 * (float)x/vid.width - 1;
	v[2].x = v[1].x = (float)2.0 * (float)(x + patch->width*vid.fdupx)/vid.width - 1;
	v[0].y = v[1].y = 1 - (float)2.0 * (float)y/vid.height;
	v[2].y = v[3].y = 1 - (float)2.0 * (float)(y + patch->height*vid.fdupy)/vid.height;

	v[0].z = v[1].z = v[2].z = v[3].z = 1.0f;

	v[0].sow = v[3].sow =  0;
	v[2].sow = v[1].sow =  patch->max_s;
	v[0].tow = v[1].tow =  0;
	v[2].tow = v[3].tow =  patch->max_t;

	if (hudtrans < 255)
	{
		FSurfaceInfo Surf;
		Surf.FlatColor.s.red = Surf.FlatColor.s.green = Surf.FlatColor.s.blue = 0xff;
		Surf.FlatColor.s.alpha = hudtrans;
		GL_DrawPolygon(&Surf, v, 4, PF_Modulated | PF_Translucent | PF_NoDepthTest, 0);
	}
	else
		GL_DrawPolygon(NULL, v, 4, PF_Translucent | PF_NoDepthTest, 0);
}

// ==========================================================================
//                                                            V_VIDEO.C STUFF
// ==========================================================================


// --------------------------------------------------------------------------
// Fills a box of pixels using a flat texture as a pattern
// --------------------------------------------------------------------------
void HWR_DrawFlatFill(int x, int y, int w, int h, lumpnum_t flatlumpnum)
{
	FOutVector  v[4];
	double dflatsize;
	int flatflag;
	const size_t len = W_LumpLength(flatlumpnum);

	switch (len)
	{
		case 4194304: // 2048x2048 lump
			dflatsize = 2048.0f;
			flatflag = 2047;
			break;
		case 1048576: // 1024x1024 lump
			dflatsize = 1024.0f;
			flatflag = 1023;
			break;
		case 262144:// 512x512 lump
			dflatsize = 512.0f;
			flatflag = 511;
			break;
		case 65536: // 256x256 lump
			dflatsize = 256.0f;
			flatflag = 255;
			break;
		case 16384: // 128x128 lump
			dflatsize = 128.0f;
			flatflag = 127;
			break;
		case 1024: // 32x32 lump
			dflatsize = 32.0f;
			flatflag = 31;
			break;
		default: // 64x64 lump
			dflatsize = 64.0f;
			flatflag = 63;
			break;
	}

	//  3--2
	//  | /|
	//  |/ |
	//  0--1

	v[0].x = v[3].x = (x - 160.0f)/160.0f;
	v[2].x = v[1].x = ((x+w) - 160.0f)/160.0f;
	v[0].y = v[1].y = -(y - 100.0f)/100.0f;
	v[2].y = v[3].y = -((y+h) - 100.0f)/100.0f;

	v[0].z = v[1].z = v[2].z = v[3].z = 1.0f;

	// flat is 64x64 lod and texture offsets are [0.0, 1.0]
	v[0].sow = v[3].sow = (float)((x & flatflag)/dflatsize);
	v[2].sow = v[1].sow = (float)(v[0].sow + w/dflatsize);
	v[0].tow = v[1].tow = (float)((y & flatflag)/dflatsize);
	v[2].tow = v[3].tow = (float)(v[0].tow + h/dflatsize);

	HWR_GetFlat(flatlumpnum, false);

	GL_DrawPolygon(NULL, v, 4, PF_NoDepthTest, 0);
}


// --------------------------------------------------------------------------
// Draw a polygon that covers all or part of the screen, used for the menu, the console, etc.
// --------------------------------------------------------------------------
//  3--2
//  | /|
//  |/ |
//  0--1
void HWR_ScreenPolygon(ULONG color, int height, byte transparency)
{
	FOutVector  v[4];
	FSurfaceInfo Surf;

	// Setup some neat-o translucency effects
	if (!height) // 0 height means full height
		height = vid.height;

	v[0].x = v[3].x = -1.0f;
	v[2].x = v[1].x =  1.0f;
	v[0].y = v[1].y =  (1.0f-((height<<1)/(float)vid.height));
	v[2].y = v[3].y =  1.0f;
	v[0].z = v[1].z = v[2].z = v[3].z = 1.0f;

	v->sow = v->tow = 0.0f; // No texture, so 0.0f texture coords

	Surf.FlatColor.rgba = UINT2RGBA(color);
	Surf.FlatColor.s.alpha = transparency;
	GL_DrawPolygon(&Surf, v, 4, PF_NoTexture|PF_Modulated|PF_Translucent|PF_NoDepthTest, 0);
}


// ==========================================================================
//                                                             R_DRAW.C STUFF
// ==========================================================================

// ------------------
// HWR_DrawViewBorder
// Fill the space around the view window with a flat texture
// 'clearlines' is useful to clear the heads up messages, when the view
// window is reduced, it doesn't refresh all the view borders.
// ------------------
void HWR_DrawViewBorder(int clearlines)
{
	int x, y;
	int top, side;
	int baseviewwidth, baseviewheight;
	int basewindowx, basewindowy;
	GLPatch_t *patch;

	if (!clearlines)
		clearlines = BASEVIDHEIGHT; // refresh all

	// calc view size based on original game resolution
	baseviewwidth = (int)(gr_viewwidth/vid.fdupx);
	baseviewheight = (int)(gr_viewheight/vid.fdupy);
	top = (int)(gr_baseviewwindowy/vid.fdupy);
	side = (int)(gr_viewwindowx/vid.fdupx);

	// top
	HWR_DrawFlatFill(0, 0,
		BASEVIDWIDTH, (top < clearlines ? top : clearlines),
		st_borderpatchnum);

	// left
	if (top < clearlines)
		HWR_DrawFlatFill(0, top, side,
			(clearlines-top < baseviewheight ? clearlines-top : baseviewheight),
			st_borderpatchnum);

	// right
	if (top < clearlines)
		HWR_DrawFlatFill(side + baseviewwidth, top, side,
			(clearlines-top < baseviewheight ? clearlines-top : baseviewheight),
			st_borderpatchnum);

	// bottom
	if (top + baseviewheight < clearlines)
		HWR_DrawFlatFill(0, top + baseviewheight,
			BASEVIDWIDTH, BASEVIDHEIGHT, st_borderpatchnum);

	//
	// draw the view borders
	//

	basewindowx = (BASEVIDWIDTH - baseviewwidth)>>1;
	if (baseviewwidth == BASEVIDWIDTH)
		basewindowy = 0;
	else
		basewindowy = top;

	// top edge
	if (clearlines > basewindowy - 8)
	{
		patch = W_CachePatchNum(viewborderlump[BRDR_T], PU_CACHE);
		for (x = 0; x < baseviewwidth; x += 8)
			HWR_DrawPatch(patch, basewindowx + x, basewindowy - 8,
				0);
	}

	// bottom edge
	if (clearlines > basewindowy + baseviewheight)
	{
		patch = W_CachePatchNum(viewborderlump[BRDR_B], PU_CACHE);
		for (x = 0; x < baseviewwidth; x += 8)
			HWR_DrawPatch(patch, basewindowx + x,
				basewindowy + baseviewheight, 0);
	}

	// left edge
	if (clearlines > basewindowy)
	{
		patch = W_CachePatchNum(viewborderlump[BRDR_L], PU_CACHE);
		for (y = 0; y < baseviewheight && basewindowy + y < clearlines;
			y += 8)
		{
			HWR_DrawPatch(patch, basewindowx - 8, basewindowy + y,
				0);
		}
	}

	// right edge
	if (clearlines > basewindowy)
	{
		patch = W_CachePatchNum(viewborderlump[BRDR_R], PU_CACHE);
		for (y = 0; y < baseviewheight && basewindowy+y < clearlines;
			y += 8)
		{
			HWR_DrawPatch(patch, basewindowx + baseviewwidth,
				basewindowy + y, 0);
		}
	}

	// Draw beveled corners.
	if (clearlines > basewindowy - 8)
		HWR_DrawPatch(W_CachePatchNum(viewborderlump[BRDR_TL],
				PU_CACHE),
			basewindowx - 8, basewindowy - 8, 0);

	if (clearlines > basewindowy - 8)
		HWR_DrawPatch(W_CachePatchNum(viewborderlump[BRDR_TR],
				PU_CACHE),
			basewindowx + baseviewwidth, basewindowy - 8, 0);

	if (clearlines > basewindowy+baseviewheight)
		HWR_DrawPatch(W_CachePatchNum(viewborderlump[BRDR_BL],
				PU_CACHE),
			basewindowx - 8, basewindowy + baseviewheight, 0);

	if (clearlines > basewindowy + baseviewheight)
		HWR_DrawPatch(W_CachePatchNum(viewborderlump[BRDR_BR],
				PU_CACHE),
			basewindowx + baseviewwidth,
			basewindowy + baseviewheight, 0);
}


// ==========================================================================
//                                                     AM_MAP.C DRAWING STUFF
// ==========================================================================

// Clear the automap part of the screen
void HWR_clearAutomap(void)
{
	FRGBAFloat fColor = {0, 0, 0, 1};
	
	// minx, miny, maxx, maxy
	GL_GClipRect(0, 0, vid.width, vid.height, NZCLIP_PLANE);
	GL_ClearBuffer(true, true, &fColor);
}


// -----------------+
// HWR_drawAMline   : draw a line of the automap (the clipping is already done in automap code)
// Arg              : color is a RGB 888 value
// -----------------+
void HWR_drawAMline(const fline_t *fl, int color)
{
	F2DCoord v1, v2;
	RGBA_t color_rgba;

	color_rgba = V_GetColor(color);

	v1.x = ((float)fl->a.x-(vid.width/2.0f))*(2.0f/vid.width);
	v1.y = ((float)fl->a.y-(vid.height/2.0f))*(2.0f/vid.height);

	v2.x = ((float)fl->b.x-(vid.width/2.0f))*(2.0f/vid.width);
	v2.y = ((float)fl->b.y-(vid.height/2.0f))*(2.0f/vid.height);

	GL_Draw2DLine(&v1, &v2, color_rgba);
}


// -----------------+
// HWR_DrawFill     : draw flat coloured rectangle, with no texture
// -----------------+
void HWR_DrawFill(int x, int y, int w, int h, int color)
{
	FOutVector v[4];
	FSurfaceInfo Surf;

	//  3--2
	//  | /|
	//  |/ |
	//  0--1

	v[0].x = v[3].x = (x - 160.0f)/160.0f;
	v[2].x = v[1].x = ((x+w) - 160.0f)/160.0f;
	v[0].y = v[1].y = -(y - 100.0f)/100.0f;
	v[2].y = v[3].y = -((y+h) - 100.0f)/100.0f;

	v[0].z = v[1].z = v[2].z = v[3].z = 1.0f;

	v[0].sow = v[3].sow = 0.0f;
	v[2].sow = v[1].sow = 1.0f;
	v[0].tow = v[1].tow = 0.0f;
	v[2].tow = v[3].tow = 1.0f;

	Surf.FlatColor = V_GetColor(color);

	GL_DrawPolygon(&Surf, v, 4,
		PF_Modulated|PF_NoTexture|PF_NoDepthTest, 0);
}

// -----------------+
// HWR_DrawTransFill     : draw flat coloured rectangle, with no texture
// -----------------+
void HWR_DrawTransFill(int x, int y, int w, int h, int color, byte alpha)
{
	FOutVector v[4];
	FSurfaceInfo Surf;
	
	//  3--2
	//  | /|
	//  |/ |
	//  0--1
	
	v[0].x = v[3].x = (x - 160.0f)/160.0f;
	v[2].x = v[1].x = ((x+w) - 160.0f)/160.0f;
	v[0].y = v[1].y = -(y - 100.0f)/100.0f;
	v[2].y = v[3].y = -((y+h) - 100.0f)/100.0f;

	v[0].z = v[1].z = v[2].z = v[3].z = 1.0f;
	
	v[0].sow = v[3].sow = 0.0f;
	v[2].sow = v[1].sow = 1.0f;
	v[0].tow = v[1].tow = 0.0f;
	v[2].tow = v[3].tow = 1.0f;
	
	Surf.FlatColor = V_GetColor(color);
	Surf.FlatColor.s.alpha = alpha;
	
	GL_DrawPolygon(&Surf, v, 4,
				   PF_Modulated|PF_NoTexture|PF_NoDepthTest|PF_Translucent, 0);
}

// --------------------------------------------------------------------------
// screen shot
// --------------------------------------------------------------------------

byte *HWR_GetScreenshot(void)
{
	byte *buf = malloc(vid.width * vid.height * 3 * sizeof (*buf));

	if (!buf)
		return NULL;
	// returns 24bit 888 RGB
	GL_ReadRect(0, 0, vid.width, vid.height, vid.width * 3, (void *)buf);
	return buf;
}

boolean HWR_Screenshot(const char *lbmname)
{
	boolean ret;
	byte *buf = malloc(vid.width * vid.height * 3 * sizeof (*buf));

	if (!buf)
		return false;

	// returns 24bit 888 RGB
	GL_ReadRect(0, 0, vid.width, vid.height, vid.width * 3, (void *)buf);

#ifdef HAVE_PNG
	ret = M_SavePNG(lbmname, buf, vid.width, vid.height, NULL);
#else
	ret = false;
#endif

	free(buf);
	return ret;
}

#endif //HWRENDER
