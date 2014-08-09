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
/// \brief Rendering of moving objects, sprites

#ifndef __R_THINGS__
#define __R_THINGS__

#include "sounds.h"
#include "r_plane.h"

// number of sprite lumps for spritewidth,offset,topoffset lookup tables
// Fab: this is a hack : should allocate the lookup tables per sprite
#define	MAXSPRITELUMPS 8192 // Increase maxspritelumps Graue 11-06-2003

#define MAXVISSPRITES 2048 // added 2-2-98 was 128

// Constant arrays used for psprite clipping
//  and initializing clipping.
extern short negonearray[MAXVIDWIDTH];
extern short screenheightarray[MAXVIDWIDTH];

// vars for R_DrawMaskedColumn
extern short *mfloorclip;
extern short *mceilingclip;
extern fixed_t spryscale;
extern fixed_t sprtopscreen;
extern fixed_t sprbotscreen;
extern fixed_t windowtop;
extern fixed_t windowbottom;

void R_DrawMaskedColumn(column_t *column);
void R_SortVisSprites(void);

// Find sprites in wadfile, replace existing, add new ones
// (only sprites from namelist are added or replaced)
void R_AddSpriteDefs(USHORT wadnum);
void R_DelSpriteDefs(USHORT wadnum);
void R_AddSprites(sector_t *sec, int lightlevel);
void R_InitSprites(void);
void P_SetTranslucencies(void);
void R_ClearSprites(void);
void R_DrawMasked(void);

// -----------
// SKINS STUFF
// -----------
#define SKINNAMESIZE 16
#define DEFAULTSKIN "sonic\0\0\0\0\0\0\0\0\0\0"

typedef struct
{
	char name[SKINNAMESIZE+1]; // short descriptive name of the skin
	spritedef_t spritedef;
	USHORT wadnum;
	char sprite[9];
	char faceprefix[9]; // 8 chars+'\0', default is "SBOSLIFE"
	char superprefix[9]; // 8 chars+'\0', default is "SUPERICO"
	char nameprefix[9]; // 8 chars+'\0', default is "STSONIC"
	char ability[2]; // ability definition
	char ability2[2]; // secondary ability definition
	char thokitem[8];
	char ghostthokitem[2];
	char spinitem[8];
	char ghostspinitem[2];
	char actionspd[4];
	char mindash[3];
	char maxdash[3];

	// Lots of super definitions...
	char super[2];
	char superanims[2];
	char superspin[2];

	char normalspeed[3]; // Normal ground

	char runspeed[3]; // Speed that you break into your run animation

	char accelstart[4]; // Acceleration if speed = 0
	char acceleration[3]; // Acceleration
	char thrustfactor[2]; // Thrust = thrustfactor * acceleration

	char jumpfactor[4]; // % of standard jump height

	// Definable color translation table
	char starttranscolor[4];

	char prefcolor[3];

	// Draw the sprite 2x as small?
	char highres[2];

	// specific sounds per skin
	sfxenum_t soundsid[NUMSKINSOUNDS]; // sound # in S_sfx table
} skin_t;

// -----------
// NOT SKINS STUFF !
// -----------
typedef enum
{
	SC_NONE = 0,
	SC_TOP = 1,
	SC_BOTTOM = 2
} spritecut_e;

// A vissprite_t is a thing that will be drawn during a refresh,
// i.e. a sprite object that is partly visible.
typedef struct vissprite_s
{
	// Doubly linked list.
	struct vissprite_s *prev;
	struct vissprite_s *next;

	mobj_t *mobj; // for easy access

	int x1, x2;

	fixed_t gx, gy; // for line side calculation
	fixed_t gz, gzt; // global bottom/top for silhouette clipping
	fixed_t pz, pzt; // physical bottom/top for sorting with 3D floors

	fixed_t startfrac; // horizontal position of x1
	fixed_t scale;
	fixed_t xiscale; // negative if flipped

	fixed_t texturemid;
	lumpnum_t patch;

	lighttable_t *colormap; // for color translation and shadow draw
	                        // maxbright frames as well

	byte *transmap; // for MF2_TRANSLUCENT sprites, which translucency table to use

	long mobjflags;

	long heightsec; // height sector for underwater/fake ceiling support

	extracolormap_t *extra_colormap; // global colormaps

	fixed_t xscale;

	// Precalculated top and bottom screen coords for the sprite.
	fixed_t thingheight; // The actual height of the thing (for 3D floors)
	sector_t *sector; // The sector containing the thing.
	short sz, szt;

	spritecut_e cut;

	boolean precip;
	boolean vflip; // Flip vertically
} vissprite_t;

// A drawnode is something that points to a 3D floor, 3D side, or masked
// middle texture. This is used for sorting with sprites.
typedef struct drawnode_s
{
	visplane_t *plane;
	drawseg_t *seg;
	drawseg_t *thickseg;
	ffloor_t *ffloor;
	vissprite_t *sprite;

	struct drawnode_s *next;
	struct drawnode_s *prev;
} drawnode_t;

extern int numskins;
extern skin_t skins[MAXSKINS + 1];

void SetPlayerSkin(int playernum, const char *skinname);
void SetPlayerSkinByNum(int playernum, int skinnum);
int R_SkinAvailable(const char *name);
void R_AddSkins(USHORT wadnum);
void R_DelSkins(USHORT wadnum);
void R_InitDrawNodes(void);
void SetSavedSkin(int playernum, int skinnum, int skincolor);

char *GetPlayerFacePic(int skinnum);

#endif //__R_THINGS__
