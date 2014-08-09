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
/// \brief span / column drawer functions, for 8bpp and 16bpp
///
///	All drawing to the view buffer is accomplished in this file.
///	The other refresh files only know about ccordinates,
///	not the architecture of the frame buffer.
///	The frame buffer is a linear one, and we need only the base address.

#include "doomdef.h"
#include "doomstat.h"
#include "r_local.h"
#include "st_stuff.h" // need ST_HEIGHT
#include "i_video.h"
#include "v_video.h"
#include "m_misc.h"
#include "w_wad.h"
#include "z_zone.h"
#include "console.h" // Until buffering gets finished

#ifdef HWRENDER
#include "hardware/hw_main.h"
#endif

// ==========================================================================
//                     COMMON DATA FOR 8bpp AND 16bpp
// ==========================================================================

/**	\brief view info
*/
int viewwidth, scaledviewwidth, viewheight, viewwindowx, viewwindowy;

/**	\brief pointer to the start of each line of the screen,
*/
byte *ylookup[MAXVIDHEIGHT*4];

/**	\brief pointer to the start of each line of the screen, for view1 (splitscreen)
*/
byte *ylookup1[MAXVIDHEIGHT*4];

/**	\brief pointer to the start of each line of the screen, for view2 (splitscreen)
*/
byte *ylookup2[MAXVIDHEIGHT*4];

/**	\brief  x byte offset for columns inside the viewwindow,
	so the first column starts at (SCRWIDTH - VIEWWIDTH)/2
*/
int columnofs[MAXVIDWIDTH*4];

byte *topleft;

// =========================================================================
//                      COLUMN DRAWING CODE STUFF
// =========================================================================

lighttable_t *dc_colormap;
int dc_x = 0, dc_yl = 0, dc_yh = 0;

fixed_t dc_iscale, dc_texturemid;
byte dc_hires; // under MSVC boolean is a byte, while on other systems, it a bit,
               // soo lets make it a byte on all system for the ASM code
byte *dc_source;

// -----------------------
// translucency stuff here
// -----------------------
#define NUMTRANSTABLES 9 // how many translucency tables are used

byte *transtables; // translucency tables

/**	\brief R_DrawTransColumn uses this
*/
byte *dc_transmap; // one of the translucency tables

// ----------------------
// translation stuff here
// ----------------------

byte *translationtables[MAXSKINS];
byte *defaulttranslationtables;
byte *bosstranslationtables;

/**	\brief R_DrawTranslatedColumn uses this
*/
byte *dc_translation;

struct r_lightlist_s *dc_lightlist = NULL;
int dc_numlights = 0, dc_maxlights, dc_texheight;

// =========================================================================
//                      SPAN DRAWING CODE STUFF
// =========================================================================

int ds_y, ds_x1, ds_x2;
lighttable_t *ds_colormap;
fixed_t ds_xfrac, ds_yfrac, ds_xstep, ds_ystep;

byte *ds_source; // start of a 64*64 tile image
byte *ds_transmap; // one of the translucency tables

/**	\brief Variable flat sizes
*/

ULONG nflatxshift, nflatyshift, nflatshiftup, nflatmask;

// =========================================================================
//                   TRANSLATION COLORMAP CODE
// =========================================================================

CV_PossibleValue_t Color_cons_t[MAXSKINCOLORS+1];
const char *Color_Names[MAXSKINCOLORS] =
{
	"None",

	// Default color set
	"Cyan",
	"Peach",
	"Lavender",
	"Silver",
	"Orange",
	"Red",
	"Blue",
	"Steel_Blue",
	"Pink",
	"Beige",
	"Purple",
	"Green",
	"White", // White (also used for fireflower)
	"Gold",
	"Yellow",

	"Black",
	"Dark_Red",
	"Dark_Blue",
     // New colors to this engine
	"Neon_Green",
	"Hot_Pink",
	"Brown"
};

/**	\brief the R_LoadSkinTable

	Creates the translation tables to map the green color ramp to
	another ramp (gray, brown, red, ...)

	This is precalculated for drawing the player sprites in the player's
	chosen color
*/

void R_LoadSkinTable(void)
{
	int i;

	for (i = 0; i < MAXSKINS; i++)
		translationtables[i] = Z_MallocAlign (256*(MAXSKINCOLORS-1), PU_STATIC, NULL, 16);
}

/**	\brief The R_InitTranslationTables

  load in color translation tables
*/
void R_InitTranslationTables(void)
{
	int i, j;
	byte bi;

	// Load here the transparency lookup tables 'TINTTAB'
	// NOTE: the TINTTAB resource MUST BE aligned on 64k for the asm
	// optimised code (in other words, transtables pointer low word is 0)
	transtables = Z_MallocAlign(NUMTRANSTABLES*0x10000, PU_STATIC,
		NULL, 16);

	W_ReadLump(W_GetNumForName("TRANS10"), transtables);
	W_ReadLump(W_GetNumForName("TRANS20"), transtables+0x10000);
	W_ReadLump(W_GetNumForName("TRANS30"), transtables+0x20000);
	W_ReadLump(W_GetNumForName("TRANS40"), transtables+0x30000);
	W_ReadLump(W_GetNumForName("TRANS50"), transtables+0x40000);
	W_ReadLump(W_GetNumForName("TRANS60"), transtables+0x50000);
	W_ReadLump(W_GetNumForName("TRANS70"), transtables+0x60000);
	W_ReadLump(W_GetNumForName("TRANS80"), transtables+0x70000);
	W_ReadLump(W_GetNumForName("TRANS90"), transtables+0x80000);

	// The old "default" transtable for thok mobjs and such
	defaulttranslationtables =
		Z_MallocAlign(256*MAXSKINCOLORS, PU_STATIC, NULL, 16);

	// Translate the colors specified
	for (i = 0; i < 256; i++)
	{
		if (i >= 160 && i <= 175)
		{
			bi = (byte)(i & 0xf);

			defaulttranslationtables[i      ] = (byte)(0xd0 + bi); // Cyan
			defaulttranslationtables[i+  256] = (byte)(0x40 + bi); // Peach
			defaulttranslationtables[i+2*256] = (byte)(0xf8 + bi/2); // Lavender
			defaulttranslationtables[i+3*256] = (byte)(0 + bi/0.5); // Silver // SRB2CB: Better Silver
			defaulttranslationtables[i+4*256] = (byte)(0x50 + bi); // Orange
			defaulttranslationtables[i+5*256] = (byte)(0x80 + bi); // Red
			defaulttranslationtables[i+6*256] = (byte)(0xe0 + bi); // Blue

			defaulttranslationtables[i+7*256] = (byte)(0xc8 + bi/2); // Steel Blue

			defaulttranslationtables[i+8*256] = (byte)(0x90 + bi/2); // Pink
			defaulttranslationtables[i+9*256] = (byte)(0x20 + bi); // Beige

			// Purple
			defaulttranslationtables[i+10*256] = (byte)(0xc0 + bi/2);

			// Green
			defaulttranslationtables[i+11*256] = (byte)(0xa0 + bi);

			// White
			defaulttranslationtables[i+12*256] = (byte)(0x00 + bi/2);

			// Gold
			defaulttranslationtables[i+13*256] = (byte)(0x70 + bi/2);

			// Yellow
			if (bi < 8)
				defaulttranslationtables[i+14*256] = (byte)(97 + bi);
			else switch (bi)
			{
				case 8:
				case 9:
					defaulttranslationtables[i+14*256] = 113;
					break;
				case 10:
					defaulttranslationtables[i+14*256] = 114;
					break;
				case 11:
				case 12:
				case 13:
					defaulttranslationtables[i+14*256] = 115;
					break;
				default:
					defaulttranslationtables[i+14*256] = 116;
					break;
				case 15:
					defaulttranslationtables[i+14*256] = 117;
					break;
			}

			// New colors from previous SRB2 versions
			defaulttranslationtables[i+15*256] = (byte)(16 + bi); // Black
			defaulttranslationtables[i+16*256] = (byte)(152 + bi/2); // Dark Red
			defaulttranslationtables[i+17*256] = (byte)(231 + bi); // Dark Blue

			// New colors
			defaulttranslationtables[i+18*256] = (byte)(184 + bi/4); // Neon Green
			defaulttranslationtables[i+19*256] = (byte)(120 + bi); // Bright Pink
			defaulttranslationtables[i+20*256] = (byte)(48 + bi); // Brown
		}
		else // Keep other colors as is.
		{
			// JTE: Far more effecient and correct.
			for (j = 0; j < MAXSKINCOLORS; j++)
				defaulttranslationtables[i+j*256] = (byte)i;
		}
	}

	bosstranslationtables = Z_MallocAlign(256, PU_STATIC, NULL, 16);

	for (i = 0; i < 256; i++)
		bosstranslationtables[i] = (byte)i;
	bosstranslationtables[31] = 0; // White!
}

/**	\brief	The R_InitSkinTranslationTables function

	Allow skins to choose which color is translated!

	\param	starttranscolor	starting color
	\param	skinnum	number of skin

	\return	void


*/
void R_InitSkinTranslationTables(int starttranscolor, int skinnum)
{
	int i, j;
	byte bi;

	// Translate the colors specified by the skin information.
	for (i = 0; i < 256; i++)
	{
		if (i >= starttranscolor && i < starttranscolor+16)
		{
			bi = (byte)((i - starttranscolor) & 0xf);
			// Go in order of the color list, start at the color after "None"
			// "None" color doesn't have a translation
			translationtables[skinnum][i      ] = (byte)(208 + bi); // Cyan
			translationtables[skinnum][i+  256] = (byte)(64 + bi); // Peach
			translationtables[skinnum][i+2*256] = (byte)(248 + bi/2); // Lavender
			translationtables[skinnum][i+3*256] = (byte)(0 + bi/0.5); // Silver // SRB2CB: Better Silver
			translationtables[skinnum][i+4*256] = (byte)(80 + bi); // Orange
			translationtables[skinnum][i+5*256] = (byte)(125 + bi); // Red
			translationtables[skinnum][i+6*256] = (byte)(224 + bi); // Blue

			translationtables[skinnum][i+7*256] = (byte)(200 + bi/2); // Steel blue

			translationtables[skinnum][i+8*256] = (byte)(0x90 + bi/2); // Pink
			translationtables[skinnum][i+9*256] = (byte)(0x20 + bi); // Beige

			// Purple
			//translationtables[skinnum][i+10*256] = (byte)(248 + bi/2);
			translationtables[skinnum][i+10*256] = (byte)(192 + bi/2); // LXShadow: Revert this if necessary. I just see no good reason for this to be the same as Lavender. (Purple's my colour. =( )

			// Green
			translationtables[skinnum][i+11*256] = (byte)(160 + bi);

			// White
			translationtables[skinnum][i+12*256] = (byte)(0 + bi);

			// Gold
			translationtables[skinnum][i+13*256] = (byte)(112 + bi/2);

			// Yellow
			if (bi < 8)
				translationtables[skinnum][i+14*256] = (byte)(97 + bi);
			else switch (bi)
			{
				case 8:
				case 9:
					translationtables[skinnum][i+14*256] = 113;
					break;
				case 10:
					translationtables[skinnum][i+14*256] = 114;
					break;
				case 11:
				case 12:
				case 13:
					translationtables[skinnum][i+14*256] = 115;
					break;
				default:
					translationtables[skinnum][i+14*256] = 116;
					break;
				case 15:
					translationtables[skinnum][i+14*256] = 117;
					break;
			}

			// New colors from previous SRB2 versions
			translationtables[skinnum][i+15*256] = (byte)(16 + bi); // Black
			translationtables[skinnum][i+16*256] = (byte)(152 + bi/2); // Dark Red
			translationtables[skinnum][i+17*256] = (byte)(231 + bi); // Dark Blue

			// New colors
			translationtables[skinnum][i+18*256] = (byte)(184 + bi/4); // Neon Green
			translationtables[skinnum][i+19*256] = (byte)(120 + bi); // Bright Pink
			translationtables[skinnum][i+20*256] = (byte)(48 + bi); // Brown
		}
		else // Keep other colors as-is.
		{
			// JTE: Far more effecient and correct.
			for (j = 0; j < MAXSKINCOLORS; j++)
			{
				//if (j == colornumtoskip)
				//	continue;
				translationtables[skinnum][i+j*256] = (byte)i;
			}
		}
	}
}

// ==========================================================================
//               COMMON DRAWER FOR 8 AND 16 BIT COLOR MODES
// ==========================================================================

// in a perfect world, all routines would be compatible for either mode,
// and optimised enough
//
// in reality, the few routines that can work for either mode, are
// put here

/**	\brief	The R_InitViewBuffer function

	Creates lookup tables for getting the framebuffer address
	of a pixel to draw.

	\param	width	witdh of buffer
	\param	height	hieght of buffer

	\return	void


*/

void R_InitViewBuffer(int width, int height)
{
	int i, bytesperpixel = vid.bpp;

	if (width > MAXVIDWIDTH)
		width = MAXVIDWIDTH;
	if (height > MAXVIDHEIGHT)
		height = MAXVIDHEIGHT;
	if (bytesperpixel < 1 || bytesperpixel > 4)
		I_Error("R_InitViewBuffer: wrong bytesperpixel value %d\n", bytesperpixel);

	// Handle resize, e.g. smaller view windows with border and/or status bar.
	viewwindowx = (vid.width - width) >> 1;

	// Column offset for those columns of the view window, but relative to the entire screen
	for (i = 0; i < width; i++)
		columnofs[i] = (viewwindowx + i) * bytesperpixel;

	// Same with base row offset.
	if (width == vid.width)
		viewwindowy = 0;
	else
		viewwindowy = (vid.height - height) >> 1;

	// Precalculate all row offsets.
	for (i = 0; i < height; i++)
	{
		ylookup[i] = ylookup1[i] = screens[0] + (i+viewwindowy)*vid.width*bytesperpixel;
		ylookup2[i] = screens[0] + (i+(vid.height>>1))*vid.width*bytesperpixel; // for splitscreen
	}
}

/**	\brief viewborder patches lump numbers
*/
lumpnum_t viewborderlump[8];

/**	\brief Store the lumpnumber of the viewborder patches
*/

void R_InitViewBorder(void)
{
	viewborderlump[BRDR_T] = W_GetNumForName("brdr_t");
	viewborderlump[BRDR_B] = W_GetNumForName("brdr_b");
	viewborderlump[BRDR_L] = W_GetNumForName("brdr_l");
	viewborderlump[BRDR_R] = W_GetNumForName("brdr_r");
	viewborderlump[BRDR_TL] = W_GetNumForName("brdr_tl");
	viewborderlump[BRDR_BL] = W_GetNumForName("brdr_bl");
	viewborderlump[BRDR_TR] = W_GetNumForName("brdr_tr");
	viewborderlump[BRDR_BR] = W_GetNumForName("brdr_br");
}

/**	\brief	The R_VideoErase function

	Copy a screen buffer.

	\param	ofs	offest from buffer
	\param	count	bytes to erase

	\return	void


*/
void R_VideoErase(unsigned int ofs, int count)
{
	// LFB copy.
	// This might not be a good idea if memcpy
	//  is not optimal, e.g. byte by byte on
	//  a 32bit CPU, as GNU GCC/Linux libc did
	//  at one point.
	M_Memcpy(screens[0] + ofs, screens[1] + ofs, count);
}

#ifdef SESLOPE
void R_Throw(void);
#endif

// ==========================================================================
//                   INCLUDE 8bpp DRAWING CODE HERE
// ==========================================================================

#include "r_draw8.c"

// ==========================================================================
//                   INCLUDE 16bpp DRAWING CODE HERE
// ==========================================================================

#include "r_draw16.c"
