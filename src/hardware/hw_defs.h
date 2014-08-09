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
/// \brief 3D hardware renderer definitions

#ifndef _HWR_DEFS_
#define _HWR_DEFS_
#include "../doomtype.h"

#define ZCLIP_PLANE 4.0f
// SRB2CBTODO: The closer the near clipping plane is to 0 causes more
// noticeable changes in precision of the zbuffer
#define NZCLIP_PLANE 0.9f

// ==========================================================================
//                                                               SIMPLE TYPES
// ==========================================================================

typedef long            FINT;
typedef unsigned long   FUINT;
typedef unsigned char   FUBYTE;
typedef unsigned long   FBITFIELD;
#ifndef __MINGW32__
typedef float           FLOAT;
#endif
typedef unsigned char   FBOOLEAN;

// ==========================================================================
//                                                                     COLORS
// ==========================================================================

// byte value for paletted graphics, which represent the transparent color
#define HWR_PATCHES_CHROMAKEY_COLORINDEX   247
#define HWR_CHROMAKEY_EQUIVALENTCOLORINDEX  31

// the chroma key color shows on border sprites, set it to black
#define HWR_PATCHES_CHROMAKEY_COLORVALUE     (0x00000000)    //RGBA format as in grSstWinOpen()

// RGBA Color components with float type ranging [ 0 ... 1 ]
struct FRGBAFloat
{
	FLOAT   red;
	FLOAT   green;
	FLOAT   blue;
	FLOAT   alpha;
};
typedef struct FRGBAFloat FRGBAFloat;

struct FColorARGB
{
	FUBYTE  alpha;
	FUBYTE  red;
	FUBYTE  green;
	FUBYTE  blue;
};
typedef struct FColorARGB ARGB_t;
typedef struct FColorARGB FColorARGB;



// ==========================================================================
//                                                                    VECTORS
// ==========================================================================

// Simple 2D coordinate
typedef struct
{
	FLOAT x,y;
} F2DCoord, v2d_t;

// Simple 3D vector
typedef struct FVector
{
	FLOAT x,y,z;
} FVector;

// 3D model vector (coords + texture coords)
typedef struct
{
	//FVector     Point;
	FLOAT       x,y,z;
	FLOAT       s,t,w;            // Texture coordinates
} v3d_t, wallVert3D;

// Transform (coords + angles)
// Transform order : scale(rotation_x(rotation_y(translation(v))))
typedef struct
{
	FLOAT       x,y,z;           // position
	FLOAT       anglex, angley;   // viewpitch / viewangle
	FLOAT       glrollangle; // SRB2CBTODO: Roll of the camera, impossible in the game's old renderer!!
	FLOAT       scalex, scaley, scalez;
	FLOAT       fovxangle, fovyangle;
	int			splitscreen; // SRB2CBTODO: !!!BOOLEAN
} FTransform;

// Transformed vector, as passed to HWR API
// IMPORTANT NOTE: Adding/removing any of the structs here requires
// all the v[4] things in the code to be removed too!
typedef struct
{
	FLOAT       x,y,z;
	// SRB2CBTODO: This is a free slot in this struct,
	// it can be changed without modifying anything else
	// but removing or adding any additional entries in this struct requires
	// modifying other things in the code 
	FUINT       argb;
	FLOAT       sow;            // s texture ordinate (s over w) SRB2CBTODO: Y vertical?
	FLOAT       tow;            // t texture ordinate (t over w) SRB2CBTODO: X horizontal?
} FOutVector;


// ==========================================================================
//                                                               RENDER MODES
// ==========================================================================

// Flags describing how to render a polygon
// You pass a combination of these flags to DrawPolygon()
enum EPolyFlags
{
	// These first 5 are mutually exclusive
	PF_Masked           = 0x00000001,   // Poly is alpha scaled and 0 alpha pels are discarded (holes in texture)
	PF_Translucent      = 0x00000002,   // Poly is transparent, alpha = level of transparency
	PF_Additive         = 0x00000024,   // Poly is added to the frame buffer
	PF_Environment      = 0x00000008,   // Poly should be drawn environment mapped, this is used for textures that have transparent holes,
	                                    // also used for text drawing
	PF_Substractive     = 0x00000010,   // for splat textures
	PF_NoAlphaTest      = 0x00000020,   // Special marker param
	PF_Blending         = (PF_Environment|PF_Additive|PF_Translucent|PF_Masked|PF_Substractive)&~PF_NoAlphaTest,

	// Other flag bits
	PF_Occlude          = 0x00000100,   // Update the depth buffer
	PF_NoDepthTest      = 0x00000200,   // Disable the depth test mode
	PF_Invisible        = 0x00000400,   // Disable write to color buffer
	PF_Decal            = 0x00000800,   // Enable polygon offset
	PF_Modulated        = 0x00001000,   // Modulation (multiply output with constant ARGB)
	                                    // When set, pass the color constant into the FSurfaceInfo -> FlatColor
	PF_NoTexture        = 0x00002000,   // Use the small white texture
	PF_Rotate           = 0x00004000,   // Rotate the coordinates of the polygon
	PF_MD2              = 0x00008000,   // Tell the rendrer we are drawing an MD2
	PF_RemoveYWrap      = 0x00010000,   // Force clamp texture on Y
	PF_ForceWrapX       = 0x00020000,   // Force repeat texture on X
	PF_ForceWrapY       = 0x00040000,   // Force repeat texture on Y
	PF_TexRotate        = 0x20000000,   // SRB2CBTODO: Rotate the texture coordinates of the polygon to be rotated, add this to surf not here
	PF_Corona           = 0x40000000,   // Draw a special 'light shine' polygon
	PF_TexScale         = 0x80000000    // Scale up/down just the texture on an object
};

enum EPolyFlags2
{
	PF2_Lighted    =     0x00000001,   // Enable lighting in a polygon, 
									   // ambient light is taken from its surf color, it is independently
	                                   //lighted unless a specific lightsource is used elsewhere 
	PF2_Silhouette =     0x00000002,   // Enable lighting in a polygon and remove all illumination to make an isolated silhouette
	PF2_Distort    =     0x00000004,   // Special environment map like mode
	PF2_CullBack   =     0x00000008,
	PF2_CullFront   =    0x00000010,
};

enum ETextureFlags
{
	TF_WRAPX       = 0x00000001,        // Wrap around X
	TF_WRAPY       = 0x00000002,        // Wrap around Y
	TF_WRAPXY      = TF_WRAPY|TF_WRAPX, // Very common, so use a combo flag
	TF_CHROMAKEYED = 0x00000010,
	TF_TRANSPARENT = 0x00000040,        // Texture with parts of it having 0 alpha, aka holes in texture
	TF_NOFILTER    = 0x00000080         // Don't use any filtering on this texture
};

#ifdef TODO // SRB2CBTODO: ? FTextureInfo
struct FTextureInfo
{
	FUINT       Width;              // Pixels
	FUINT       Height;             // Pixels
	FUBYTE     *TextureData;        // Image data
	FUINT       Format;             // FORMAT_RGB, ALPHA ...
	FBITFIELD   Flags;              // Flags to tell driver about texture (see ETextureFlags)
	void        DriverExtra;        // (OpenGL texture object nr, ...)
	                                // chromakey enabled,...

	struct FTextureInfo *Next;      // Manage list of downloaded textures.
};
#else
typedef struct GLMipmap_s FTextureInfo;
#endif

// Description of a renderable surface
struct FSurfaceInfo
{
	FUINT    PolyFlags;          // Surface flags, extra parameters for rendering
	RGBA_t   FlatColor;          // Flat-shaded color used with PF_Modulated mode
	FLOAT    TexRotate;          // SRB2CBTODO: New! Rotate the polygon's texture coordinates!
	FLOAT    PolyRotate;         // SRB2CBTODO: New! Rotate the polygon's coordinates!
	FLOAT    TexScale;           // SRB2CBTODO: New! Scaling support for a texture!
};
typedef struct FSurfaceInfo FSurfaceInfo;

// Enum to access the options of OpenGL for GL_SetSpecialState
enum hwdGL_SetSpecialState
{
	HWD_SET_FOG_MODE,
	HWD_SET_FOG_COLOR,
	HWD_SET_FOG_DENSITY,
	HWD_SET_FOG_START,
	HWD_SET_FOG_END,
	HWD_SET_PALETTECOLOR,
	HWD_SET_TEXTUREFILTERMODE,
	HWD_SET_TEXTUREANISOTROPICMODE,
	HWD_NUMSTATE
};

typedef enum hwdGL_SetSpecialState hwdspecialstate_t;

enum hwdfiltermode
{
	HWD_SET_TEXTUREFILTER_POINTSAMPLED,
	HWD_SET_TEXTUREFILTER_MIXED1
};


#endif //_HWR_DEFS_
