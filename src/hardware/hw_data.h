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
/// \brief defines structures and exports for the standard 3D driver DLL used by SRB2

#ifndef _HWR_DATA_
#define _HWR_DATA_

#if (defined (_WIN32) || defined (_WIN64)) && !defined (__CYGWIN__) && !defined (_XBOX)
//#define WIN32_LEAN_AND_MEAN
#define RPC_NO_WINDOWS_H
#include <windows.h>
#endif

#if defined (VID_X11) && !defined (SDL)
#include <GL/glx.h>
#endif

#include "../doomdef.h"
#include "../screen.h"


// ==========================================================================
//                                                               TEXTURE INFO
// ==========================================================================

// glInfo.data holds the address of the graphics data cached in heap memory,
// NULL if the texture is not in SRB2's heap cache.

typedef long GLTextureFormat_t;
#define GR_TEXFMT_ALPHA_8               0x2 /* (0..0xFF) alpha     */
#define GR_TEXFMT_INTENSITY_8           0x3 /* (0..0xFF) intensity */
#define GR_TEXFMT_ALPHA_INTENSITY_44    0x4
#define GR_TEXFMT_P_8                   0x5 /* 8-bit palette */
#define GR_TEXFMT_RGB_565               0xa
#define GR_TEXFMT_ARGB_1555             0xb
#define GR_TEXFMT_ARGB_4444             0xc
#define GR_TEXFMT_ALPHA_INTENSITY_88    0xd
#define GR_TEXFMT_AP_88                 0xe /* 8-bit alpha 8-bit palette */
#define GR_RGBA                         0x6 /* 32 bit RGBA */

typedef struct
{
	GLTextureFormat_t format;
	void              *data;
} GLTexInfo;

struct GLMipmap_s
{
	GLTexInfo       glInfo;         // for TexDownloadMipMap
	ULONG   flags;
	USHORT  height;
	USHORT  width;
	unsigned int    downloaded;     // the dll driver have it in there cache ?

	struct GLMipmap_s    *nextcolormap;
	const byte              *colormap;

	struct GLMipmap_s *nextmipmap; // List of all textures in OpenGL driver
};
typedef struct GLMipmap_s GLMipmap_t;


//
// SRB2 texture info, as cached for hardware rendering
//
struct GLTexture_s
{
	GLMipmap_t mipmap;
	float       scaleX;             //used for scaling textures on walls
	float       scaleY;
};
typedef struct GLTexture_s GLTexture_t;


// a cached patch as converted to hardware format, holding the original patch_t
// header so that the existing code can retrieve ->width, ->height as usual
// This is returned by W_CachePatchNum()/W_CachePatchName(), when rendermode
// is 'render_opengl'. Else it returns the normal patch_t data.

struct GLPatch_s
{
	// the 4 first fields come right away from the original patch_t
	short               width;
	short               height;
	short               leftoffset;     // pixels to the left of origin
	short               topoffset;      // pixels below the origin
	//
	float               max_s,max_t;
	lumpnum_t           patchlump;      // the software patch lump num for when the hardware patch
	                                    // was flushed, and we need to re-create it
	GLMipmap_t       mipmap;
};
typedef struct GLPatch_s GLPatch_t;

#endif //_HWR_DATA_
