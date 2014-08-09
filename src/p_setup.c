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
/// \brief Do all the WAD I/O, get map description, set up initial state and misc. LUTs

#include "doomdef.h"
#include "d_main.h"

#if defined (_WIN32) || defined (_WIN32_WCE)
#include <malloc.h>
#include <math.h>
#endif

#include "byteptr.h"
#include "g_game.h"

#include "p_local.h"
#include "p_setup.h"
#include "p_spec.h"
#include "p_saveg.h"

#include "i_sound.h" // for I_PlayCD()..
#include "i_video.h" // for I_FinishUpdate()..
#include "r_sky.h"
#include "i_system.h"

#include "r_data.h"
#include "r_things.h"
#include "r_sky.h"

#include "s_sound.h"
#include "st_stuff.h"
#include "w_wad.h"
#include "z_zone.h"
#include "r_splats.h"

#include "hu_stuff.h"
#include "console.h"

#include "m_misc.h"
#include "m_fixed.h"
#include "m_random.h"
#include "m_menu.h"

#include "f_finale.h"
#include "st_stuff.h"

#include "dehacked.h" // for map headers
#include "r_main.h"

#include "md5.h"

#include "m_argv.h"

#include "dstrings.h"
#include "v_video.h"

#include "p_polyobj.h"

#ifdef HWRENDER
#include "hardware/hw_main.h"
#include "hardware/hw_light.h"
#endif

#ifdef ESLOPE
#include "p_slopes.h"
#endif

#ifdef JTEBOTS
#include "p_bots.h"
#endif

//
// Map MD5, calculated on level load.
// Sent to clients in PT_SERVERINFO.
//
unsigned char mapmd5[16];


//
// MAP related Lookup tables.
// Store VERTEXES, LINEDEFS, SIDEDEFS, etc.
//

size_t numvertexes, numsegs, numsectors, numsubsectors, numnodes, numlines, numsides, nummapthings;
vertex_t *vertexes;
seg_t *segs;
sector_t *sectors;
subsector_t *subsectors;
node_t *nodes;
line_t *lines;
side_t *sides;
mapthing_t *mapthings;
int numstarposts;
boolean LoadingCachedActions;

boolean noautosave = false;

// BLOCKMAP
// Created from axis aligned bounding box
// of the map, a rectangular array of
// blocks of size ...
// Used to speed up collision detection
// by spatial subdivision in 2D.
//
// Blockmap size.
int bmapwidth, bmapheight; // size in mapblocks

long *blockmap; // int for large maps
// offsets in blockmap are from here
long *blockmaplump; // Big blockmap

// origin of block map
fixed_t bmaporgx, bmaporgy;
// for thing chains
mobj_t **blocklinks;

// REJECT
// For fast sight rejection.
// Speeds up enemy AI by skipping detailed LineOf Sight calculation.
// Without special effect, this could be used as a PVS lookup as well.
//
byte *rejectmatrix;

// Maintain single and multi player starting spots.
int numdmstarts, numcoopstarts, numredctfstarts, numbluectfstarts, numtagstarts;

mapthing_t *deathmatchstarts[MAX_DM_STARTS];
mapthing_t *playerstarts[MAXPLAYERS];
mapthing_t *bluectfstarts[MAXPLAYERS];
mapthing_t *redctfstarts[MAXPLAYERS];
mapthing_t *tagstarts[MAXPLAYERS];

/** Logs an error about a map being corrupt, then terminates.
  * This allows reporting highly technical errors for usefulness, without
  * confusing a novice map designer who simply needs to run ZenNode.
  *
  * If logging is disabled in this compile, or the log file is not opened, the
  * full technical details are printed in the I_Error() message.
  *
  * \param msg The message to log. This message can safely result from a call
  *            to va(), since that function is not used here.
  * \todo Fix the I_Error() message. On some implementations the logfile may
  *       not be called log.txt.
  * \sa CONS_LogPrintf, I_Error
  */
FUNCNORETURN static ATTRNORETURN void CorruptMapError(const char *msg)
{
	// Don't use va() because the calling function probably uses it
	char mapnum[10];

	const char *stringy = NULL;

	CONS_LogPrintf("\n--------Error--------\n");
	sprintf(mapnum, "%hd", gamemap);
	CONS_LogPrintf("Map ");
	CONS_LogPrintf(mapnum, stringy);
	CONS_LogPrintf(" is corrupt: ");
	CONS_LogPrintf(msg, stringy);
	CONS_LogPrintf("\n");
	CONS_LogPrintf("---------------------\n\n");
	I_Error("Map %s is invalid or corrupt.\n\n"
			"Reason: %s\n\n"
			"This error has been copied to the log file and/or text console.", mapnum, msg);
}

/** Loading screen for various functions from XSRB2
 *
 */
void P_LoadingScreen(const char *msg, short percent) // SRB2CBTODO: LOAD
{
	if (rendermode == render_none)
		return;

	char s[16];

	// SRB2CBTODO!:! This allows the OS to know when the program is just
	// loading and not stalling, do this for everything
	I_OsPolling();

	//V_DrawPatchFill(W_CachePatchName("SKY1", PU_CACHE));
	if (rendermode == render_soft)
		V_DrawFlatFill(0, 0, BASEVIDWIDTH, BASEVIDHEIGHT,W_GetNumForName("PIT"), 0);
	else
		HWR_ScreenPolygon(0x11111111, 0, 100);

	M_DrawTextBox(BASEVIDWIDTH/2-104, BASEVIDHEIGHT-24-8, 24, 1);

	if (percent >= 0)
	{
		sprintf(s, "%d%%", percent);
		if (percent > 4)
			V_DrawTransFill(BASEVIDWIDTH/2-96, BASEVIDHEIGHT-24, (percent-4)*2, 8, 162, 150);
		V_DrawString(BASEVIDWIDTH/2+96-V_StringWidth(s), BASEVIDHEIGHT-24, V_TRANSLUCENT, s);
	}
	else
		V_DrawTransFill(BASEVIDWIDTH/2-96, BASEVIDHEIGHT-24, 192, 8, 162, 150);
	V_DrawString(BASEVIDWIDTH/2-96, BASEVIDHEIGHT-24, V_TRANSLUCENT, msg);
	I_UpdateNoVsync();
}

/** Clears the data from a single map header.
  *
  * \param i Map number to clear header for.
  * \sa P_ClearMapHeaderInfo, P_LoadMapHeader
  */
static void P_ClearSingleMapHeaderInfo(short i)
{
	const short num = (short)(i-1);
	DEH_WriteUndoline("LEVELNAME", mapheaderinfo[num].lvlttl, UNDO_NONE);
	mapheaderinfo[num].lvlttl[0] = '\0';
	DEH_WriteUndoline("SUBTITLE", mapheaderinfo[num].subttl, UNDO_NONE);
	mapheaderinfo[num].subttl[0] = '\0';
	DEH_WriteUndoline("ACT", va("%d", mapheaderinfo[num].actnum), UNDO_NONE);
	mapheaderinfo[num].actnum = 0;
	DEH_WriteUndoline("TYPEOFLEVEL", va("%d", mapheaderinfo[num].typeoflevel), UNDO_NONE);
	mapheaderinfo[num].typeoflevel = 0;
	DEH_WriteUndoline("NEXTLEVEL", va("%d", mapheaderinfo[num].nextlevel), UNDO_NONE);
	mapheaderinfo[num].nextlevel = (short)(i + 1);
	DEH_WriteUndoline("MUSICSLOT", va("%d", mapheaderinfo[num].musicslot), UNDO_NONE);
	mapheaderinfo[num].musicslot = mus_map01m + num;
	DEH_WriteUndoline("FORCECHARACTER", va("%d", mapheaderinfo[num].forcecharacter), UNDO_NONE);
	mapheaderinfo[num].forcecharacter = 255;
	DEH_WriteUndoline("WEATHER", va("%d", mapheaderinfo[num].weather), UNDO_NONE);
	mapheaderinfo[num].weather = 0;
	DEH_WriteUndoline("SKYNUM", va("%d", mapheaderinfo[num].skynum), UNDO_NONE);
	mapheaderinfo[num].skynum = i;
	DEH_WriteUndoline("INTERSCREEN", mapheaderinfo[num].interscreen, UNDO_NONE);
	mapheaderinfo[num].interscreen[0] = '#';
	DEH_WriteUndoline("SCRIPTNAME", mapheaderinfo[num].scriptname, UNDO_NONE);
	mapheaderinfo[num].scriptname[0] = '#';
	DEH_WriteUndoline("SCRIPTISLUMP", va("%d", mapheaderinfo[num].scriptislump), UNDO_NONE);
	mapheaderinfo[num].scriptislump = false;
	DEH_WriteUndoline("PRECUTSCENENUM", va("%d", mapheaderinfo[num].precutscenenum), UNDO_NONE);
	mapheaderinfo[num].precutscenenum = 0;
	DEH_WriteUndoline("CUTSCENENUM", va("%d", mapheaderinfo[num].cutscenenum), UNDO_NONE);
	mapheaderinfo[num].cutscenenum = 0;
	DEH_WriteUndoline("COUNTDOWN", va("%d", mapheaderinfo[num].countdown), UNDO_NONE);
	mapheaderinfo[num].countdown = 0;
	DEH_WriteUndoline("NOZONE", va("%d", mapheaderinfo[num].nozone), UNDO_NONE);
	mapheaderinfo[num].nozone = false;
	DEH_WriteUndoline("HIDDEN", va("%d", mapheaderinfo[num].hideinmenu), UNDO_NONE);
	mapheaderinfo[num].hideinmenu = false;
	DEH_WriteUndoline("NOSSMUSIC", va("%d", mapheaderinfo[num].nossmusic), UNDO_NONE);
	mapheaderinfo[num].nossmusic = false;
	DEH_WriteUndoline("SPEEDMUSIC", va("%d", mapheaderinfo[num].speedmusic), UNDO_NONE);
	mapheaderinfo[num].speedmusic = false;
	DEH_WriteUndoline("NORELOAD", va("%d", mapheaderinfo[num].noreload), UNDO_NONE);
	mapheaderinfo[num].noreload = false;
	DEH_WriteUndoline("TIMEATTACK", va("%d", mapheaderinfo[num].timeattack), UNDO_NONE);
	mapheaderinfo[num].timeattack = false;
	DEH_WriteUndoline("LEVELSELECT", va("%d", mapheaderinfo[num].levelselect), UNDO_NONE);
	mapheaderinfo[num].levelselect = false;
#ifdef FREEFLY
	DEH_WriteUndoline("FREEFLY", va("%d", mapheaderinfo[num].freefly), UNDO_NONE);
	mapheaderinfo[i-1].freefly = false;
#endif
	DEH_WriteUndoline("RUNSOC", mapheaderinfo[num].runsoc, UNDO_NONE);
	mapheaderinfo[num].runsoc[0] = '#';
	DEH_WriteUndoline(va("# uload for map %d", i), NULL, UNDO_DONE);
	DEH_WriteUndoline("MAPCREDITS", mapheaderinfo[num].mapcredits, UNDO_NONE);
	mapheaderinfo[num].mapcredits[0] = '\0';
	DEH_WriteUndoline("LEVELFOGCOLOR", mapheaderinfo[num].levelfogcolor, UNDO_NONE);
	mapheaderinfo[num].mapcredits[0] = '\0';
	DEH_WriteUndoline("LEVELFOG", va("%d", mapheaderinfo[num].levelfog), UNDO_NONE);
	mapheaderinfo[num].levelselect = false;
	DEH_WriteUndoline("LEVELFOGDENSITY", va("%d", mapheaderinfo[num].levelfogdensity), UNDO_NONE);
	mapheaderinfo[num].levelfogdensity = 0;
}

/** Clears the data from the map headers for all levels.
  *
  * \sa P_ClearSingleMapHeaderInfo, P_InitMapHeaders
  */
void P_ClearMapHeaderInfo(void)
{
	short i;

	for (i = 1; i <= NUMMAPS; i++)
		P_ClearSingleMapHeaderInfo(i);
}

/** Initializes the map headers.
  * Only new, Dehacked format map headers (MAPxxD) are loaded here. Old map
  * headers (MAPxxN) are no longer supported.
  *
  * \sa P_ClearMapHeaderInfo, P_LoadMapHeader
  */
void P_InitMapHeaders(void)
{
	char mapheader[7];
	lumpnum_t lumpnum;
	int moremapnumbers, mapnum;

	for (mapnum = 1; mapnum <= NUMMAPS; mapnum++)
	{
		moremapnumbers = mapnum - 1;

		strncpy(mapheader, G_BuildMapName(mapnum), 5);

		mapheader[5] = 'D'; // New header
		mapheader[6] = '\0';

		lumpnum = W_CheckNumForName(mapheader);

		if (!(lumpnum == LUMPERROR || W_LumpLength(lumpnum) == 0))
			DEH_LoadDehackedLump(lumpnum);
	}
}

/** Sets up the data in a single map header.
  *
  * \param mapnum Map number to load header for.
  * \sa P_ClearSingleMapHeaderInfo, P_InitMapHeaders
  */
static inline void P_LoadMapHeader(short mapnum)
{
	char mapheader[7];
	lumpnum_t lumpnum;

	strncpy(mapheader, G_BuildMapName(mapnum), 5);

	mapheader[5] = 'D'; // New header
	mapheader[6] = '\0';

	lumpnum = W_CheckNumForName(mapheader);

	if (!(lumpnum == LUMPERROR || W_LumpLength(lumpnum) == 0))
	{
		P_ClearSingleMapHeaderInfo(mapnum);
		DEH_LoadDehackedLump(lumpnum);
		return;
	}
}

//
// Computes the line length in fracunits, the OpenGL render needs this
//

/** Computes the length of a seg in fracunits.
  * This is needed for splats and polyobjects
  *
  * \param seg Seg to compute length for.
  * \return Length in fracunits.
  */
float P_CalcSegLength(seg_t *seg)
{
	float dx, dy;

	// Make a vector (start at origin)
	dx = FIXED_TO_FLOAT(seg->v2->x - seg->v1->x);
	dy = FIXED_TO_FLOAT(seg->v2->y - seg->v1->y);

	return (float)sqrt(dx*dx + dy*dy)*FRACUNIT; // NOTE: NOT hypt
}

//
// ShortToLong
//
// haleyjd 06/19/06: Happy birthday to me ^_^
// Inline routine to convert a short value to a long, preserving the value
// -1 but treating any other negative value as unsigned.
//
static inline long ShortToLong(short value)
{
	return (value == -1) ? -1l : (long)value & 0xffff;
}

//
// SafeUintIndex
//
// haleyjd 12/04/08: Inline routine to convert an effectively unsigned short
// index into a long index, safely checking against the provided upper bound
// and substituting the value of 0 in the event of an overflow.
//
static inline long SafeUintIndex(short input, int limit, const char *func,
                                   const char *item)
{
	long ret = (long)(SHORT(input)) & 0xffff;

	if (ret >= limit)
	{
		CONS_Printf("%s error: %s #%li out of range\a\n", func, item, ret);
		ret = 0;
	}

	return ret;
}


/** Loads the vertexes for a level.
 *
 * \param lump VERTEXES lump number.
 * \sa ML_VERTEXES
 */
static inline void P_LoadVertexes(lumpnum_t lumpnum) // SRB2CBTODO: Use Eternity's superior and simplier code!(for other things too)
{
	byte *data;
	size_t i;
	mapvertex_t *ml;
	vertex_t *li;

	// Determine number of lumps:
	//  total lump length / vertex record length.
	numvertexes = W_LumpLength(lumpnum) / sizeof (mapvertex_t);

	if (numvertexes <= 0)
		I_Error("Level has no vertices"); // Instead of crashing

	// Allocate zone memory for buffer.
	vertexes = Z_Calloc(numvertexes * sizeof (*vertexes), PU_LEVEL, NULL);

	// Load data into cache.
	data = W_CacheLumpNum(lumpnum, PU_STATIC);

	ml = (mapvertex_t *)data;
	li = vertexes;

	// Copy and convert vertex coordinates, internal representation as fixed.
	for (i = 0; i < numvertexes; i++, li++, ml++)
	{
		li->x = SHORT(ml->x)<<FRACBITS;
		li->y = SHORT(ml->y)<<FRACBITS;
	}

	// Free buffer memory.
	Z_Free(data);
}


/** Loads the SEGS resource from a level.
  *
  * \param lump Lump number of the SEGS resource.
  * \sa ::ML_SEGS
  */
static void P_LoadSegs(lumpnum_t lumpnum)
{
	size_t i;
	byte *data;

	numsegs = W_LumpLength(lumpnum) / sizeof (mapseg_t);
	if (numsegs <= 0)
		I_Error("Level has no segs!"); // instead of crashing
	segs = Z_Calloc(numsegs * sizeof (*segs), PU_LEVEL, NULL);
	data = W_CacheLumpNum(lumpnum, PU_STATIC);

	seg_t *li = segs;
	mapseg_t *ml = (mapseg_t *)data;

	for (i = 0; i < numsegs; i++, li++, ml++)
	{
		int side, linedef;
		line_t *ldef;

		li->v1 = &vertexes[SHORT(ml->v1)];
		li->v2 = &vertexes[SHORT(ml->v2)];

		li->angle = (SHORT(ml->angle))<<FRACBITS;
		li->offset = (SHORT(ml->offset))<<FRACBITS;
		linedef = SHORT(ml->linedef);
		ldef = &lines[linedef];
		li->linedef = ldef;
		li->side = side = SHORT(ml->side);
		li->sidedef = &sides[ldef->sidenum[side]];
		li->frontsector = sides[ldef->sidenum[side]].sector;

#ifdef EENGINEO // SRB2CBTODO: Make this standard
		// haleyjd 05/24/08: link segs into parent linedef;
		// allows fast reverse searches
		li->linenext = ldef->segs;
		ldef->segs   = li;
#endif

		if ((ldef-> flags & ML_TWOSIDED) && (ldef->sidenum[side^1]!= 0xffff))
			li->backsector = sides[ldef->sidenum[side^1]].sector;
		else
			li->backsector = NULL;

		li->numlights = 0;
		li->rlights = NULL;

		li->len = P_CalcSegLength(li);
	}

	Z_Free(data);
}

/** Loads the SSECTORS resource from a level.
  *
  * \param lump Lump number of the SSECTORS resource.
  * \sa ::ML_SSECTORS
  */
static inline void P_LoadSubsectors(lumpnum_t lumpnum)
{
	void *data;
	size_t i;
	mapsubsector_t *ms;
	subsector_t *ss;

	numsubsectors = W_LumpLength(lumpnum) / sizeof (mapsubsector_t);
	if (numsubsectors <= 0)
		I_Error("Level has no subsectors: It may not have been run through a nodebuilder");
	ss = subsectors = Z_Calloc(numsubsectors * sizeof (*subsectors), PU_LEVEL, NULL);
	data = W_CacheLumpNum(lumpnum, PU_STATIC);

	ms = (mapsubsector_t *)data;

	for (i = 0; i < numsubsectors; i++, ss++, ms++)
	{
		ss->sector = NULL;
		ss->numlines = SHORT(ms->numsegs);
		ss->firstline = SHORT(ms->firstseg);
#ifdef FLOORSPLATS
		ss->splats = NULL;
#endif
		ss->validcount = 0;
	}

	Z_Free(data);
}

//
// P_LoadSectors
//
// levelflats
//

#define MAXLEVELFLATS 512 // SRB2CBTODO: Needs more?

size_t numlevelflats;
levelflat_t *levelflats;

// Other files need this info
size_t P_PrecacheLevelFlats(void)
{
	lumpnum_t lump;
	size_t i, flatmemory = 0;

	// New flat code to make use of levelflats.
	for (i = 0; i < numlevelflats; i++)
	{
		lump = levelflats[i].lumpnum;
		if (devparm)
			flatmemory += W_LumpLength(lump);
		R_GetFlat(lump);
	}
	return flatmemory;
}

// Helper function for P_LoadSectors, find a flat in the active wad files,
// SRB2CBTODO: TODO: Do the same flat cache stuff for TX_START+END support!
// allocate an id for it, and set the levelflat (to speedup search)
//
long P_AddLevelFlat(const char *flatname, levelflat_t *levelflat)
{
	size_t i;

	// first scan through the already found flats
	for (i = 0; i < numlevelflats; i++, levelflat++)
		if (strnicmp(levelflat->name,flatname,8)==0)
			break;

	// That flat was already found in the level, return the id
	if (i == numlevelflats)
	{
		// store the name
		strlcpy(levelflat->name, flatname, sizeof (levelflat->name));
		strupr(levelflat->name);

		// store the flat lump number
		// Kalaron: now check to see if this lump is a PNG file,
		// then flag this flat to use PNG textures!!!

		levelflat->lumpnum = R_GetFlatNumForName(flatname); // Remember, the game isn't picky - it just searches for any lump with the wanted name


		if (devparm)
			I_OutputMsg("flat #%03u: %s\n", (int)numlevelflats, levelflat->name);

		numlevelflats++;

		if (numlevelflats >= MAXLEVELFLATS)
			I_Error("Too many flats in level\n");
	}

	// level flat id
	return (long)i;
}

static void P_LoadSectors(lumpnum_t lumpnum)
{
	byte *data;
	size_t i;
	mapsector_t *ms;
	sector_t *ss;
	levelflat_t *foundflats;

	numsectors = W_LumpLength(lumpnum) / sizeof (mapsector_t);
	if (numsectors <= 0)
		I_Error("Map Loading Error: Level has no sectors\n It may not have been run through a nodebuilder.");
	sectors = Z_Calloc(numsectors*sizeof (*sectors), PU_LEVEL, NULL);
	data = W_CacheLumpNum(lumpnum, PU_STATIC);

	foundflats = calloc(MAXLEVELFLATS, sizeof (*foundflats));
	if (!foundflats)
		I_Error("Map Loading Error: The game ran out of memory while loading sectors of this level\n");

	numlevelflats = 0;

	ms = (mapsector_t *)data;
	ss = sectors;
	for (i = 0; i < numsectors; i++, ss++, ms++)
	{
		ss->floorheight = SHORT(ms->floorheight)<<FRACBITS;
		ss->ceilingheight = SHORT(ms->ceilingheight)<<FRACBITS;

		//
		//  flats
		//
		ss->floorpic = P_AddLevelFlat(ms->floorpic, foundflats);
		ss->ceilingpic = P_AddLevelFlat(ms->ceilingpic, foundflats);

		ss->lightlevel = SHORT(ms->lightlevel);
		ss->special = SHORT(ms->special);
		ss->tag = SHORT(ms->tag);
		ss->nexttag = ss->firsttag = -1;

		memset(&ss->soundorg, 0, sizeof(ss->soundorg));
		ss->validcount = 0;

		ss->thinglist = NULL;
		ss->touching_thinglist = NULL;
		ss->preciplist = NULL;
		ss->touching_preciplist = NULL;

		ss->floordata = NULL;
		ss->ceilingdata = NULL;
		ss->lightingdata = NULL;

		ss->linecount = 0;
		ss->lines = NULL;

		ss->heightsec = -1;
		ss->floorlightsec = -1;
		ss->ceilinglightsec = -1;
		ss->crumblestate = 0;
		ss->ffloors = NULL;
		ss->lightlist = NULL;
		ss->numlights = 0;
		ss->attached = NULL;
		ss->attachedsolid = NULL;
		ss->numattached = 0;
		ss->maxattached = 1;
		ss->moved = true;

		ss->extra_colormap = NULL;

		ss->floor_xoffs = ss->ceiling_xoffs = ss->floor_yoffs = ss->ceiling_yoffs = 0;
		ss->floorpic_angle = ss->ceilingpic_angle = 0;
		ss->floor_scale = ss->ceiling_scale = false;
		ss->bottommap = ss->midmap = ss->topmap = -1;
		ss->gravity = NULL;
		ss->cullheight = NULL;
		ss->verticalflip = false;
		ss->flags = 0;
		ss->flags |= SF_TRIGGERSPECIAL_FLOOR;

		ss->floorspeed = 0;
		ss->ceilspeed = 0;

		// SoM: Slopes!
#ifdef ESLOPE
		ss->c_slope = ss->f_slope = NULL;
#endif
	}

	Z_Free(data);

	// set the sky flat num
	skyflatnum = P_AddLevelFlat("F_SKY1", foundflats);

	// copy table for global usage
	levelflats = M_Memcpy(Z_Calloc(numlevelflats * sizeof (*levelflats), PU_LEVEL, NULL), foundflats, numlevelflats * sizeof (levelflat_t));
	free(foundflats);

	// search for animated flats and set up
	P_SetupLevelFlatAnims();
}

//
// P_LoadNodes
//
// SRB2CBTODO: Nodes....this is a severe limit on the engine,
// ignore this lump and completely reimplement!
static void P_LoadNodes(lumpnum_t lumpnum)
{
	byte *data;
	size_t i;

	numnodes = W_LumpLength(lumpnum) / sizeof (mapnode_t);
	if (numnodes <= 0)
		I_Error("Map Loading Error: Level has no nodes, this level was built incorrectly");

	byte j, k;
	mapnode_t *mn;
	node_t *no;

	nodes = Z_Calloc(numnodes * sizeof (*nodes), PU_LEVEL, NULL);
	data = W_CacheLumpNum(lumpnum, PU_STATIC);

	mn = (mapnode_t *)data;
	no = nodes;

	for (i = 0; i < numnodes; i++, no++, mn++)
	{
		no->x = SHORT(mn->x)<<FRACBITS;
		no->y = SHORT(mn->y)<<FRACBITS;
		no->dx = SHORT(mn->dx)<<FRACBITS;
		no->dy = SHORT(mn->dy)<<FRACBITS;
		for (j = 0; j < 2; j++)
		{
			no->children[j] = SHORT(mn->children[j]);
			for (k = 0; k < 4; k++)
				no->bbox[j][k] = SHORT(mn->bbox[j][k])<<FRACBITS;
		}
	}

	Z_Free(data);
}

//
// P_LoadThings
//
static void P_LoadThings(lumpnum_t lumpnum)
{
	size_t i;
	mapthing_t *mt;
	char *data;
	char *datastart;

	// SRB2CBTODO: WHAT IS (5 * sizeof (short))?! It = 10
	// anything else seems to make a map not load properly,
	// but this hard-coded value MUST have some reason for being what it is
	nummapthings = W_LumpLength(lumpnum) / (5 * sizeof (short));
	mapthings = Z_Calloc(nummapthings * sizeof (*mapthings), PU_LEVEL, NULL);

	tokenbits = 0;
	runemeraldmanager = false;
	nummaprings = 0;

	// Spawn axis points first so they are
	// at the front of the list for fast searching.
	data = datastart = W_CacheLumpNum(lumpnum, PU_LEVEL);
	mt = mapthings;
	for (i = 0; i < nummapthings; i++, mt++)
	{
		mt->x = READSHORT(data);
		mt->y = READSHORT(data);
		mt->angle = READSHORT(data);
		mt->type = READSHORT(data);
		mt->options = READSHORT(data);
		mt->extrainfo = (byte)(mt->type >> 12);

		mt->type &= 4095; // SRB2CBTODO: WHAT IS THIS???? Mobj type limits?!!!!

		switch (mt->type)
		{
			case 1700: // MT_AXIS
			case 1701: // MT_AXISTRANSFER
			case 1702: // MT_AXISTRANSFERLINE
				mt->mobj = NULL;
				P_SpawnMapThing(mt);
				break;
			default:
				break;
		}
	}
	Z_Free(datastart);

	mt = mapthings;
	numhuntemeralds = 0; // SRB2CBTODO: Don't make the player redo emeralds on map reload

	sector_t *sector;
	fixed_t x, y;

	for (i = 0; i < nummapthings; i++, mt++)
	{
		x = mt->x << FRACBITS;
		y = mt->y << FRACBITS;
		sector = R_PointInSubsector(x, y)->sector;
		// Z for objects
#ifdef ESLOPE
		if (sector->f_slope)
			mt->z = (short)(P_GetZAt(sector->f_slope, x, y)>>FRACBITS);
		else
#endif
			mt->z = (short)(sector->floorheight>>FRACBITS);

		if (mt->type == 1700 // MT_AXIS
			|| mt->type == 1701 // MT_AXISTRANSFER
			|| mt->type == 1702) // MT_AXISTRANSFERLINE
			continue; // These were already spawned
#ifdef ESLOPE
		if (mt->type == THING_SlopeFloorPointLine
			|| mt->type == THING_SlopeCeilingPointLine
			|| mt->type == THING_SetFloorSlope
			|| mt->type == THING_SetCeilingSlope
			|| mt->type == THING_CopyFloorPlane
			|| mt->type == THING_CopyCeilingPlane
			//|| mt->type == THING_VavoomFloor
			//|| mt->type == THING_VavoomCeiling
			|| mt->type == THING_VertexFloorZ
			|| mt->type == THING_VertexCeilingZ
			)
			continue; // These were also spawned already
#endif

		mt->mobj = NULL;
		P_SpawnMapThing(mt);
	}

	// Got through a list of random emeralds for hunt,
	// map things are already spawned here
	if (numhuntemeralds)
	{
		byte emer1, emer2, emer3;
		emer1 = emer2 = emer3 = 0;

		byte emerald1num = P_Random() % numhuntemeralds;
		emer1 = emerald1num;

		byte k;

		for (k = 0; k < numhuntemeralds; k++)
		{
			byte emerald2num = P_Random() % numhuntemeralds;

			if (emerald2num != 0 && emerald2num != emer1)
			{
				emer2 = emerald2num;
				break;
			}
			else
				continue;
		}

		for (k = 0; k < numhuntemeralds; k++)
		{
			byte emerald3num = P_Random() % numhuntemeralds;

			if (emerald3num != 0 && emerald3num != emer2 && emerald3num != emer1)
			{
				emer3 = emerald3num;
				break;
			}
			else
				continue;
		}

		// Increment all values by one after they are checked to not match with each other
		// a value can equal 0, which is null, so add 1 to all the list numbers
		emer1 += 1;
		emer2 += 1;
		emer3 += 1;

		if (emer1)
			P_SpawnMobj(huntemeralds[emer1]->x<<FRACBITS,
				huntemeralds[emer1]->y<<FRACBITS,
				huntemeralds[emer1]->z<<FRACBITS, MT_EMERHUNT);

		if (emer2)
			P_SpawnMobj(huntemeralds[emer2]->x<<FRACBITS,
				huntemeralds[emer2]->y<<FRACBITS,
				huntemeralds[emer2]->z<<FRACBITS, MT_EMERHUNT);

		if (emer3)
			P_SpawnMobj(huntemeralds[emer3]->x<<FRACBITS,
				huntemeralds[emer3]->y<<FRACBITS,
				huntemeralds[emer3]->z<<FRACBITS, MT_EMERHUNT);
	}

	// Run through the list of mapthings again to spawn hoops and rings
	mt = mapthings;
	for (i = 0; i < nummapthings; i++, mt++)
	{
		switch (mt->type) // SRB2CBTODO: change all of these to the mobj[type].doomednum so that this area is more flexible.
		{
			case 300:
			case 308:
			case 309:
			case 600:
			case 601:
			case 602:
			case 603:
			case 604:
			case 605:
			case 606:
			case 607:
			case 608:
			case 609:
			case 1705:
			case 1706:
			case 1800:
				mt->mobj = NULL;

			{
				x = mt->x << FRACBITS;
				y = mt->y << FRACBITS;
				sector = R_PointInSubsector(x, y)->sector;
#ifdef ESLOPE
				// Z for objects
				if (sector->f_slope) // SRB2CBTODO: Get ceiling slopes too (someday maybe...)
					mt->z = (short)(P_GetZAt(sector->f_slope, x, y)>>FRACBITS);
				else
#endif
					mt->z = (short)(sector->floorheight>>FRACBITS);

				P_SpawnHoopsAndRings (mt);
			}
				break;
			default:
				break;
		}
	}
}

static void P_SpawnEmblems(void)
{
	int i;
	mobj_t *emblemmobj;

	for (i = 0; i < numemblems - 2; i++)
	{
		if (emblemlocations[i].level != gamemap)
			continue;

		emblemmobj = P_SpawnMobj(emblemlocations[i].x<<FRACBITS, emblemlocations[i].y<<FRACBITS,
			emblemlocations[i].z<<FRACBITS, MT_EMBLEM);

		P_SetMobjStateNF(emblemmobj, emblemmobj->info->spawnstate);

		emblemmobj->health = i+1;

		// Absorb the color of the player you belong to.
		// Note: "Everyone" emblems use Sonic's color.
		emblemmobj->flags |= MF_TRANSLATION;
		if (emblemlocations[i].player < numskins)
			emblemmobj->color = atoi(skins[emblemlocations[i].player].prefcolor);
		else
			emblemmobj->color = atoi(skins[0].prefcolor);

		if (emblemlocations[i].collected
			|| (emblemlocations[i].player != players[0].skin && emblemlocations[i].player != 255))
		{
			P_UnsetThingPosition(emblemmobj);
			emblemmobj->flags |= MF_NOCLIP;
			emblemmobj->flags &= ~MF_SPECIAL;
			emblemmobj->flags |= MF_NOBLOCKMAP;
			emblemmobj->frame |= (tr_trans50<<FF_TRANSSHIFT);
			P_SetThingPosition(emblemmobj);
		}
		else
			emblemmobj->frame &= ~FF_TRANSMASK;
	}
}

void P_SpawnSecretItems(boolean loademblems)
{
	// Spawn secret things // SRB2CBTODO:!!! REMOVE THIS HACKY P_SPAWNMOBJ!!!!!
	if (netgame || multiplayer || (modifiedgame && !savemoddata) || timeattacking) // No cheating!!
		return;

	if (loademblems)
		P_SpawnEmblems();

	if (gamemap == 11)
		P_SpawnMobj(04220000000*-1, 0554000000*-1, ONFLOORZ, MT_PXVI)->angle = ANG270;
}

// Experimental groovy write function! // SRB2CBTODO: EXTEND ON THIS!!!!!!
void P_WriteThings(lumpnum_t lumpnum)
{
	size_t i, length;
	mapthing_t *mt;
	char *data, *datastart;
	byte *savebuffer, *savebuf_p;
	short temp;

	data = datastart = W_CacheLumpNum(lumpnum, PU_LEVEL);

	savebuf_p = savebuffer = (byte *)malloc(nummapthings * sizeof (mapthing_t));

	if (!savebuf_p)
	{
		CONS_Printf("No more free memory for thingwriting!\n");
		return;
	}

	mt = mapthings;
	for (i = 0; i < nummapthings; i++, mt++)
	{
		WRITESHORT(savebuf_p, mt->x);
		WRITESHORT(savebuf_p, mt->y);

		WRITESHORT(savebuf_p, mt->angle);

		temp = (short)(mt->type + ((short)mt->extrainfo << 12));
		WRITESHORT(savebuf_p, temp);
		WRITESHORT(savebuf_p, mt->options);
	}

	Z_Free(datastart);

	length = savebuf_p - savebuffer;

	FIL_WriteFile(va("Newthings%d.lmp", gamemap), savebuffer, length);
	free(savebuffer);
	savebuf_p = NULL;

	CONS_Printf("Newthings%d.lmp saved.\n", gamemap);
}

//
// P_LoadLineDefs
//
static void P_LoadLineDefs(lumpnum_t lumpnum)
{
	byte *data;
	size_t i;
	maplinedef_t *mld;
	line_t *ld;
	vertex_t *v1, *v2;

	numlines = W_LumpLength(lumpnum) / sizeof (maplinedef_t);
	if (numlines <= 0)
		I_Error("Level has no linedefs");
	lines = Z_Calloc(numlines * sizeof (*lines), PU_LEVEL, NULL);
	data = W_CacheLumpNum(lumpnum, PU_STATIC);

	mld = (maplinedef_t *)data;
	ld = lines;
	for (i = 0; i < numlines; i++, mld++, ld++)
	{
		ld->flags = SHORT(mld->flags);
		ld->special = SHORT(mld->special);
		ld->tag = SHORT(mld->tag);
		v1 = ld->v1 = &vertexes[SHORT(mld->v1)];
		v2 = ld->v2 = &vertexes[SHORT(mld->v2)];
		ld->dx = v2->x - v1->x;
		ld->dy = v2->y - v1->y;

#ifdef WALLSPLATS
		ld->splats = NULL;
#endif

		if (!ld->dx)
			ld->slopetype = ST_VERTICAL;
		else if (!ld->dy)
			ld->slopetype = ST_HORIZONTAL;
		else if (FixedDiv(ld->dy, ld->dx) > 0)
			ld->slopetype = ST_POSITIVE;
		else
			ld->slopetype = ST_NEGATIVE;

		if (v1->x < v2->x)
		{
			ld->bbox[BOXLEFT] = v1->x;
			ld->bbox[BOXRIGHT] = v2->x;
		}
		else
		{
			ld->bbox[BOXLEFT] = v2->x;
			ld->bbox[BOXRIGHT] = v1->x;
		}

		if (v1->y < v2->y)
		{
			ld->bbox[BOXBOTTOM] = v1->y;
			ld->bbox[BOXTOP] = v2->y;
		}
		else
		{
			ld->bbox[BOXBOTTOM] = v2->y;
			ld->bbox[BOXTOP] = v1->y;
		}

		ld->sidenum[0] = SHORT(mld->sidenum[0]);
		ld->sidenum[1] = SHORT(mld->sidenum[1]);

		{
			// cph 2006/09/30 - fix sidedef errors right away.
			// cph 2002/07/20 - these errors are fatal if not fixed, so apply them
			byte j;

			for (j=0; j < 2; j++)
			{
				if (ld->sidenum[j] != 0xffff && ld->sidenum[j] >= (USHORT)numsides)
				{
					ld->sidenum[j] = 0xffff;
					CONS_Printf("P_LoadLineDefs: linedef %u has out-of-range sidedef number\n",(unsigned int)(numlines-i-1));
				}
			}
		}

		ld->frontsector = ld->backsector = NULL;
		ld->validcount = 0;
		ld->firsttag = ld->nexttag = -1;
		// killough 11/98: fix common wad errors (missing sidedefs):

		if (ld->sidenum[0] == 0xffff)
		{
			ld->sidenum[0] = 0;  // Substitute dummy sidedef for missing right side
			// Print a warning about the texture when in devmode
			if (cv_devmode)
				CONS_Printf("P_LoadLineDefs: linedef %u missing first sidedef\n",(int)(numlines-i-1));
		}

		if ((ld->sidenum[1] == 0xffff) && (ld->flags & ML_TWOSIDED))
		{
			ld->flags &= ~ML_TWOSIDED;  // Clear 2s flag for missing left side
			// Print a warning about the texture
			if (cv_devmode)
				CONS_Printf("P_LoadLineDefs: linedef %u has two-sided flag set, but no second sidedef\n",(int)(numlines-i-1));
		}

		if (ld->sidenum[0] != 0xffff && ld->special)
			sides[ld->sidenum[0]].special = ld->special;

#ifdef POLYOBJECTS
		ld->polyobj = NULL;
#endif

		// SoM: Calculate line normal (for every line)
#ifdef ESLOPE
		P_MakeLineNormal(ld);
#endif
	}

	Z_Free(data);
}

static void P_LoadLineDefs2(void)
{
	size_t i = numlines;
	register line_t *ld = lines;
	for (;i--;ld++)
	{
		ld->frontsector = sides[ld->sidenum[0]].sector; //e6y: Can't be -1 here
		ld->backsector  = ld->sidenum[1] != 0xffff ? sides[ld->sidenum[1]].sector : 0;

		// Repeat count for midtexture
		if ((ld->flags & ML_EFFECT5) && (ld->sidenum[1] != 0xffff))
		{
			sides[ld->sidenum[0]].repeatcnt = (short)(((unsigned int)sides[ld->sidenum[0]].textureoffset >> FRACBITS) >> 12);
			sides[ld->sidenum[0]].textureoffset = (((unsigned int)sides[ld->sidenum[0]].textureoffset >> FRACBITS) & 2047) << FRACBITS;
			sides[ld->sidenum[1]].repeatcnt = (short)(((unsigned int)sides[ld->sidenum[1]].textureoffset >> FRACBITS) >> 12);
			sides[ld->sidenum[1]].textureoffset = (((unsigned int)sides[ld->sidenum[1]].textureoffset >> FRACBITS) & 2047) << FRACBITS;
		}
	}

	// Optimize sidedefs
	if (M_CheckParm("-compress"))
	{
		side_t *newsides;
		size_t numnewsides = 0;
		size_t z;

		for (i = 0; i < numsides; i++)
		{
			size_t j, k;
			if (sides[i].sector == NULL)
				continue;

			for (k = numlines, ld = lines; k--; ld++)
			{
				if (ld->sidenum[0] == i)
					ld->sidenum[0] = (USHORT)numnewsides;

				if (ld->sidenum[1] == i)
					ld->sidenum[1] = (USHORT)numnewsides;
			}

			for (j = i+1; j < numsides; j++)
			{
				if (sides[j].sector == NULL)
					continue;

				if (!memcmp(&sides[i], &sides[j], sizeof(side_t)))
				{
					// Find the linedefs that belong to this one
					for (k = numlines, ld = lines; k--; ld++)
					{
						if (ld->sidenum[0] == j)
							ld->sidenum[0] = (USHORT)numnewsides;

						if (ld->sidenum[1] == j)
							ld->sidenum[1] = (USHORT)numnewsides;
					}
					sides[j].sector = NULL; // Flag for deletion
				}
			}
			numnewsides++;
		}

		// We're loading things into this block anyhow, so no point in zeroing it out.
		newsides = Z_Malloc(numnewsides * sizeof(*newsides), PU_LEVEL, NULL);

		// Copy the sides to their new block of memory.
		for (i = 0, z = 0; i < numsides; i++)
		{
			if (sides[i].sector != NULL)
				memcpy(&newsides[z++], &sides[i], sizeof(side_t));
		}

		CONS_Printf("Old sides is %d, new sides is %d\n", (int)numsides, (int)numnewsides);

		Z_Free(sides);
		sides = newsides;
		numsides = numnewsides;
	}
}

//
// P_LoadSideDefs
//
static inline void P_LoadSideDefs(lumpnum_t lumpnum)
{
	numsides = W_LumpLength(lumpnum) / sizeof (mapsidedef_t);
	if (numsides <= 0)
		I_Error("Level has no sidedefs");
	sides = Z_Calloc(numsides * sizeof (*sides), PU_LEVEL, NULL);
}

// Delay loading texture names until after loaded linedefs.

static void P_LoadSideDefs2(lumpnum_t lumpnum)
{
	byte *data = W_CacheLumpNum(lumpnum, PU_STATIC);
	USHORT i;
	long num;

	for (i = 0; i < numsides; i++)
	{
		register mapsidedef_t *msd = (mapsidedef_t *)data + i;
		register side_t *sd = sides + i;
		register sector_t *sec;

		sd->textureoffset = SHORT(msd->textureoffset)<<FRACBITS;
		sd->rowoffset = SHORT(msd->rowoffset)<<FRACBITS;

		{ /* cph 2006/09/30 - catch out-of-range sector numbers; use sector 0 instead */
			USHORT sector_num = SHORT(msd->sector);

			if (sector_num >= numsectors)
			{
				CONS_Printf("P_LoadSideDefs2: sidedef %u has out-of-range sector num %u\n", i, sector_num);
				sector_num = 0;
			}
			sd->sector = sec = &sectors[sector_num];
		}

		// refined to allow colormaps to work as wall textures if invalid as colormaps
		// but valid as textures.

		sd->sector = sec = &sectors[SHORT(msd->sector)];

		// Colormaps!
		switch (sd->special)
		{
			case 63: // variable colormap via 242 linedef
			case 606: // Just colormap transfer
				// R_CreateColormap will only create a colormap in software mode...
				// Perhaps we should just call it instead of doing the calculations here.
				if (rendermode != render_opengl)
				{
					if (msd->toptexture[0] == '#' || msd->bottomtexture[0] == '#')
					{
						sec->midmap = R_CreateColormap(msd->toptexture, msd->midtexture,
							msd->bottomtexture);
						sd->toptexture = sd->bottomtexture = 0;
					}
					else
					{
						if ((num = R_CheckTextureNumForName(msd->toptexture, i)) == -1)
							sd->toptexture = 0;
						else
							sd->toptexture = num;
						if ((num = R_CheckTextureNumForName(msd->midtexture, i)) == -1)
							sd->midtexture = 0;
						else
							sd->midtexture = num;
						if ((num = R_CheckTextureNumForName(msd->bottomtexture, i)) == -1)
							sd->bottomtexture = 0;
						else
							sd->bottomtexture = num;
					}
					break;
				}
#ifdef HWRENDER
				else
				{
					// for now, full support of toptexture only
					if (msd->toptexture[0] == '#')
					{
						char *col = msd->toptexture;

						sec->midmap = R_CreateColormap(msd->toptexture, msd->midtexture,
							msd->bottomtexture);
						sd->toptexture = sd->bottomtexture = 0;
						// SRB2CBTODO: This HEX colormap translation may be wrong
#define HEX2INT(x) (x >= '0' && x <= '9' ? x - '0' : x >= 'a' && x <= 'f' ? x - 'a' + 10 : x >= 'A' && x <= 'F' ? x - 'A' + 10 : 0)
#define ALPHA2INT(x) (x >= 'a' && x <= 'z' ? x - 'a' : x >= 'A' && x <= 'Z' ? x - 'A' : 0)
						sec->extra_colormap = &extra_colormaps[sec->midmap];
						sec->extra_colormap->rgba =
							(HEX2INT(col[1]) << 4) + (HEX2INT(col[2]) << 0) +
							(HEX2INT(col[3]) << 12) + (HEX2INT(col[4]) << 8) +
							(HEX2INT(col[5]) << 20) + (HEX2INT(col[6]) << 16) +
							(ALPHA2INT(col[7]) << 24);
#undef ALPHA2INT
#undef HEX2INT
					}
					else
					{
						if ((num = R_CheckTextureNumForName(msd->toptexture, i)) == -1)
							sd->toptexture = 0;
						else
							sd->toptexture = num;

						if ((num = R_CheckTextureNumForName(msd->midtexture, i)) == -1)
							sd->midtexture = 0;
						else
							sd->midtexture = num;

						if ((num = R_CheckTextureNumForName(msd->bottomtexture, i)) == -1)
							sd->bottomtexture = 0;
						else
							sd->bottomtexture = num;
					}
					break;
				}
#endif

			default: // normal cases
				if (msd->toptexture[0] == '#')
				{
					char *col = msd->toptexture;
					sd->toptexture = sd->bottomtexture =
						((col[1]-'0')*100 + (col[2]-'0')*10 + col[3]-'0') + 1;
					sd->midtexture = R_TextureNumForName(msd->midtexture, i);
				}
				else
				{
					sd->midtexture = R_TextureNumForName(msd->midtexture, i);
					sd->toptexture = R_TextureNumForName(msd->toptexture, i);
					sd->bottomtexture = R_TextureNumForName(msd->bottomtexture, i);
				}
				break;
		}
	}

	Z_Free(data);
}

//#define UNSTABLEBLOCKMAP // The new unstable way SRB2 v2.0 handled blockmap

#ifdef UNSTABLEBLOCKMAP
static boolean LineInBlock(fixed_t cx1, fixed_t cy1, fixed_t cx2, fixed_t cy2, fixed_t bx1, fixed_t by1)
{
	fixed_t bx2 = bx1 + MAPBLOCKUNITS;
	fixed_t by2 = by1 + MAPBLOCKUNITS;
	fixed_t bbox[4];
	line_t boxline, testline;
	vertex_t vbox, vtest;

	// Trivial rejection
	if (cx1 < bx1 && cx2 < bx1)
		return false;

	if (cx1 > bx2 && cx2 > bx2)
		return false;

	if (cy1 < by1 && cy2 < by1)
		return false;

	if (cy1 > by2 && cy2 > by2)
		return false;

	// Rats, guess we gotta check
	// if the line intersects
	// any sides of the block.
	cx1 <<= FRACBITS;
	cy1 <<= FRACBITS;
	cx2 <<= FRACBITS;
	cy2 <<= FRACBITS;
	bx1 <<= FRACBITS;
	by1 <<= FRACBITS;
	bx2 <<= FRACBITS;
	by2 <<= FRACBITS;

	bbox[BOXTOP] = by2;
	bbox[BOXBOTTOM] = by1;
	bbox[BOXRIGHT] = bx2;
	bbox[BOXLEFT] = bx1;
	boxline.v1 = &vbox;
	testline.v1 = &vtest;

	testline.v1->x = cx1;
	testline.v1->y = cy1;
	testline.dx = cx2 - cx1;
	testline.dy = cy2 - cy1;

	// Test line against bottom edge of box
	boxline.v1->x = bx1;
	boxline.v1->y = by1;
	boxline.dx = bx2 - bx1;
	boxline.dy = 0;

	if (P_PointOnLineSide(cx1, cy1, &boxline) != P_PointOnLineSide(cx2, cy2, &boxline)
		&& P_PointOnLineSide(boxline.v1->x, boxline.v1->y, &testline) != P_PointOnLineSide(boxline.v1->x+boxline.dx, boxline.v1->y+boxline.dy, &testline))
		return true;

	// Right edge of box
	boxline.v1->x = bx2;
	boxline.v1->y = by1;
	boxline.dx = 0;
	boxline.dy = by2-by1;

	if (P_PointOnLineSide(cx1, cy1, &boxline) != P_PointOnLineSide(cx2, cy2, &boxline)
		&& P_PointOnLineSide(boxline.v1->x, boxline.v1->y, &testline) != P_PointOnLineSide(boxline.v1->x+boxline.dx, boxline.v1->y+boxline.dy, &testline))
		return true;

	// Top edge of box
	boxline.v1->x = bx1;
	boxline.v1->y = by2;
	boxline.dx = bx2 - bx1;
	boxline.dy = 0;

	if (P_PointOnLineSide(cx1, cy1, &boxline) != P_PointOnLineSide(cx2, cy2, &boxline)
		&& P_PointOnLineSide(boxline.v1->x, boxline.v1->y, &testline) != P_PointOnLineSide(boxline.v1->x+boxline.dx, boxline.v1->y+boxline.dy, &testline))
		return true;

	// Left edge of box
	boxline.v1->x = bx1;
	boxline.v1->y = by1;
	boxline.dx = 0;
	boxline.dy = by2-by1;

	if (P_PointOnLineSide(cx1, cy1, &boxline) != P_PointOnLineSide(cx2, cy2, &boxline)
		&& P_PointOnLineSide(boxline.v1->x, boxline.v1->y, &testline) != P_PointOnLineSide(boxline.v1->x+boxline.dx, boxline.v1->y+boxline.dy, &testline))
		return true;

	return false;
}

//
// killough 10/98:
//
// Rewritten to use faster algorithm.
//
// SSN Edit: Killough's wasn't accurate enough, sometimes excluding
// blocks that the line did in fact exist in, so now we use
// a fail-safe approach that puts a 'box' around each line.
//
// Please note: This section of code is not interchangable with TeamTNT's
// code which attempts to fix the same problem.
static void P_CreateBlockMapo(void) // SRB2CBTODO: Make this actualy effecient
{
	register size_t i;
	fixed_t minx = MAXINT, miny = MAXINT, maxx = MININT, maxy = MININT;

	// First find the limits of the current map
	for (i = 0; i < numvertexes; i++)
	{
		if (vertexes[i].x>>FRACBITS < minx)
			minx = vertexes[i].x>>FRACBITS;
		else if (vertexes[i].x>>FRACBITS > maxx)
			maxx = vertexes[i].x>>FRACBITS;
		if (vertexes[i].y>>FRACBITS < miny)
			miny = vertexes[i].y>>FRACBITS;
		else if (vertexes[i].y>>FRACBITS > maxy)
			maxy = vertexes[i].y>>FRACBITS;
	}

	// Save blockmap parameters
	bmaporgx = minx << FRACBITS;
	bmaporgy = miny << FRACBITS;
	bmapwidth = ((maxx-minx) >> MAPBTOFRAC) + 1;
	bmapheight = ((maxy-miny) >> MAPBTOFRAC)+ 1;

	// Compute blockmap, which is stored as a 2d array of variable-sized lists.
	//
	// Pseudocode:
	//
	// For each linedef:
	//
	//   Map the starting and ending vertices to blocks.
	//
	//   Starting in the starting vertex's block, do:
	//
	//     Add linedef to current block's list, dynamically resizing it.
	//
	//     If current block is the same as the ending vertex's block, exit loop.
	//
	//     Move to an adjacent block by moving towards the ending block in
	//     either the x or y direction, to the block which contains the linedef.

	{
		typedef struct
		{
			int n, nalloc;
			int *list;
		} bmap_t; // blocklist structure

		size_t tot = bmapwidth * bmapheight; // size of blockmap
		bmap_t *bmap = calloc(tot, sizeof (*bmap)); // array of blocklists
		boolean straight;

		for (i = 0; i < numlines; i++)
		{
			// starting coordinates
			int x = (lines[i].v1->x>>FRACBITS) - minx;
			int y = (lines[i].v1->y>>FRACBITS) - miny;
			int bxstart, bxend, bystart, byend, v2x, v2y, curblockx, curblocky;

			v2x = lines[i].v2->x>>FRACBITS;
			v2y = lines[i].v2->y>>FRACBITS;

			// Draw a "box" around the line.
			bxstart = (x >> MAPBTOFRAC);
			bystart = (y >> MAPBTOFRAC);

			v2x -= minx;
			v2y -= miny;

			bxend = ((v2x) >> MAPBTOFRAC);
			byend = ((v2y) >> MAPBTOFRAC);

			if (bxend < bxstart)
			{
				int temp = bxstart;
				bxstart = bxend;
				bxend = temp;
			}

			if (byend < bystart)
			{
				int temp = bystart;
				bystart = byend;
				byend = temp;
			}

			// Catch straight lines
			// This fixes the error where straight lines
			// directly on a blockmap boundary would not
			// be included in the proper blocks.
			if (lines[i].v1->y == lines[i].v2->y)
			{
				straight = true;
				bystart--;
				byend++;
			}
			else if (lines[i].v1->x == lines[i].v2->x)
			{
				straight = true;
				bxstart--;
				bxend++;
			}
			else
				straight = false;

			// Now we simply iterate block-by-block until we reach the end block.
			for (curblockx = bxstart; curblockx <= bxend; curblockx++)
			for (curblocky = bystart; curblocky <= byend; curblocky++)
			{
				size_t b = curblocky * bmapwidth + curblockx;

				if (b >= tot)
					continue;

				if (!straight && !(LineInBlock((fixed_t)x, (fixed_t)y, (fixed_t)v2x, (fixed_t)v2y, (fixed_t)(curblockx << MAPBTOFRAC), (fixed_t)(curblocky << MAPBTOFRAC))))
					continue;

				// Increase size of allocated list if necessary
				if (bmap[b].n >= bmap[b].nalloc)
				{
					// Make the code more readable, don't realloc a null pointer
					// (because it crashes for me, and because the comp.lang.c FAQ says so)
					if (bmap[b].nalloc == 0)
					{
						bmap[b].nalloc = 8;
						bmap[b].list = malloc(bmap[b].nalloc * sizeof (*bmap->list));
					}
					else
					{
						bmap[b].nalloc = bmap[b].nalloc * 2;
						bmap[b].list = realloc(bmap[b].list, bmap[b].nalloc * sizeof (*bmap->list));
					}
					if (!bmap[b].list)
						I_Error("Out of Memory in P_CreateBlockMap");
				}

				// Add linedef to end of list
				bmap[b].list[bmap[b].n++] = (int)i;
			}
		}

		// Compute the total size of the blockmap.
		//
		// Compression of empty blocks is performed by reserving two offset words
		// at tot and tot+1.
		//
		// 4 words, unused if this routine is called, are reserved at the start.
		{
			size_t count = tot + 6; // we need at least 1 word per block, plus reserved's

			for (i = 0; i < tot; i++)
				if (bmap[i].n)
					count += bmap[i].n + 2; // 1 header word + 1 trailer word + blocklist

			// Allocate blockmap lump with computed count
			blockmaplump = Z_Calloc(sizeof (*blockmaplump) * count, PU_LEVEL, NULL);
		}

		// Now compress the blockmap.
		{
			size_t ndx = tot += 4; // Advance index to start of linedef lists
			bmap_t *bp = bmap; // Start of uncompressed blockmap

			blockmaplump[ndx++] = 0; // Store an empty blockmap list at start
			blockmaplump[ndx++] = -1; // (Used for compression)

			for (i = 4; i < tot; i++, bp++)
			{
				if (bp->n) // Non-empty blocklist
				{
					blockmaplump[blockmaplump[i] = (long)(ndx++)] = 0; // Store index & header
					do
						blockmaplump[ndx++] = bp->list[--bp->n]; // Copy linedef list
					while (bp->n);
					blockmaplump[ndx++] = -1; // Store trailer
					free(bp->list); // Free linedef list
				}
				else // Empty blocklist: point to reserved empty blocklist
					blockmaplump[i] = (long)tot;
			}

			free(bmap); // Free uncompressed blockmap
		}
	}
	{
		size_t count = sizeof (*blocklinks) * bmapwidth * bmapheight;
		// clear out mobj chains (copied from from P_LoadBlockMap)
		blocklinks = Z_Calloc(count, PU_LEVEL, NULL);
		blockmap = blockmaplump + 4;

#ifdef POLYOBJECTS
		// haleyjd 2/22/06: setup polyobject blockmap
		count = sizeof(*polyblocklinks) * bmapwidth * bmapheight;
		polyblocklinks = Z_Calloc(count, PU_LEVEL, NULL);
#endif
	}
}
#else
static void P_CreateBlockMap(void) // SRB2CBTODO: CLEANUP!! and optimize
{
	register size_t i;
	fixed_t minx, miny, maxx, maxy;

	if (numvertexes <= 0)
		CorruptMapError(va("P_CreateBlockMap:\n "
						   "Map %d has no vertexes!\n"
						   "This map may have been incorrectly saved!\n",
						   gamemap));

	// Find map extents for the blockmap
	minx = maxx = vertexes[0].x;
	miny = maxy = vertexes[0].y;

	// First find limits of map
	for (i = 1; i < numvertexes; i++)
	{
		if (vertexes[i].x < minx)
			minx = vertexes[i].x;
		else if (vertexes[i].x > maxx)
			maxx = vertexes[i].x;

		if (vertexes[i].y < miny)
			miny = vertexes[i].y;
		else if (vertexes[i].y > maxy)
			maxy = vertexes[i].y;
	}

	maxx >>= FRACBITS;
	minx >>= FRACBITS;
	maxy >>= FRACBITS;
	miny >>= FRACBITS;

	// Save blockmap parameters
	bmapwidth =  ((maxx - minx) >> MAPBTOFRAC) + 1;
	bmapheight = ((maxy - miny) >> MAPBTOFRAC) + 1;
	bmaporgx = minx << FRACBITS;
	bmaporgy = miny << FRACBITS;

	// Compute blockmap, which is stored as a 2d array of variable-sized lists.
	//
	// Pseudocode:
	//
	// For each linedef:
	//
	//   Map the starting and ending vertices to blocks.
	//
	//   Starting in the starting vertex's block, do:
	//
	//     Add linedef to current block's list, dynamically resizing it.
	//
	//     If current block is the same as the ending vertex's block, exit loop.
	//
	//     Move to an adjacent block by moving towards the ending block in
	//     either the x or y direction, to the block which contains the linedef.

	{
		typedef struct
		{
			int n, nalloc;
			int *list;
		} bmap_t; // blocklist structure

		size_t tot = bmapwidth * bmapheight; // size of blockmap
		bmap_t *bmap = calloc(tot, sizeof *bmap); // array of blocklists

		if (gamestate != wipegamestate)
		{
			F_WipeStartScreen();

			if (!(mapheaderinfo[gamemap-1].interscreen[0] == '#'
				  && gamestate == GS_INTERMISSION))
			{
				V_DrawFill(0, 0, vid.width, vid.height, 31);
#ifdef HWRENDER
				if (rendermode == render_opengl)
					HWR_PrepFadeToBlack(false);
#endif
			}

			F_WipeEndScreen(0, 0, vid.width, vid.height);

			F_RunWipe(TICRATE);

			F_WipeStartScreen();

			WipeInAction = false;
		}

		for (i = 0; i < numlines; i++)
		{
			// starting coordinates
			int x = (lines[i].v1->x >> FRACBITS) - minx;
			int y = (lines[i].v1->y >> FRACBITS) - miny;

			// x-y deltas
			int adx = lines[i].dx >> FRACBITS, dx = adx < 0 ? -1 : 1;
			int ady = lines[i].dy >> FRACBITS, dy = ady < 0 ? -1 : 1;

			// difference in preferring to move across y (> 0) instead of x (< 0)
			int diff = !adx ? 1 : !ady ? -1 : (((x >> MAPBTOFRAC) << MAPBTOFRAC) +
											   (dx > 0 ? MAPBLOCKUNITS-1 : 0) - x) * (ady = abs(ady)) * dx -
			(((y >> MAPBTOFRAC) << MAPBTOFRAC) +
			 (dy > 0 ? MAPBLOCKUNITS-1 : 0) - y) * (adx = abs(adx)) * dy;

			// starting block, and pointer to its blocklist structure
			size_t b = (y >> MAPBTOFRAC) * bmapwidth + (x >> MAPBTOFRAC);

			// ending block
			size_t bend = (((lines[i].v2->y >> FRACBITS) - miny) >> MAPBTOFRAC) *
			bmapwidth + (((lines[i].v2->x >> FRACBITS) - minx) >> MAPBTOFRAC);

			// delta for pointer when moving across y
			dy *= bmapwidth;

			// deltas for diff inside the loop
			adx <<= MAPBTOFRAC;
			ady <<= MAPBTOFRAC;

			// Now we simply iterate block-by-block until we reach the end block.
			while (b < tot) // failsafe -- should ALWAYS be true
			{
				// Increase size of allocated list if necessary
				if (bmap[b].n >= bmap[b].nalloc)
				{
					// Make the code more readable, don't realloc a null pointer
					// (because it crashes for me, and because the comp.lang.c FAQ says so)
					if (bmap[b].nalloc == 0)
					{
						bmap[b].nalloc = 8;
						bmap[b].list = malloc(bmap[b].nalloc * sizeof (*bmap->list));
					}
					else
					{
						bmap[b].nalloc = bmap[b].nalloc * 2;
						bmap[b].list = realloc(bmap[b].list, bmap[b].nalloc * sizeof (*bmap->list));
					}
					if (!bmap[b].list)
						I_Error("Out of Memory in P_CreateBlockMap");
				}

				// Add linedef to end of list
				bmap[b].list[bmap[b].n++] = (unsigned int)i;

				// If we have reached the last block, exit
				if (b == bend)
					break;

				// XSRB2
				// Displaying is a slow process, updating the loading screen
				// too much will actually make the loading take longer!
				// Note: This function's i value is not always ++ the last one!
				if (!(i % 500) || !i)
					P_LoadingScreen("Building Blockmap", ((i*100)/numlines));

				// Move in either the x or y direction to the next block
				if (diff < 0)
					diff += ady, b += dx;
				else
					diff -= adx, b += dy;
			}
		}

		// Compute the total size of the blockmap.
		//
		// Compression of empty blocks is performed by reserving two offset words
		// at tot and tot+1.
		//
		// 4 words, unused if this routine is called, are reserved at the start.
		{
			size_t count = tot + 6; // we need at least 1 word per block, plus reserved's

			for (i = 0; i < tot; i++)
				if (bmap[i].n)
					count += bmap[i].n + 2; // 1 header word + 1 trailer word + blocklist

			// Allocate blockmap lump with computed count
			blockmaplump = Z_Calloc(sizeof (*blockmaplump) * count, PU_LEVEL, NULL);
		}

		// Now compress the blockmap.
		{
			int ndx = tot += 4; // Advance index to start of linedef lists
			bmap_t *bp = bmap; // Start of uncompressed blockmap

			blockmaplump[ndx++] = 0; // Store an empty blockmap list at start
			blockmaplump[ndx++] = -1; // (Used for compression)

			for (i = 4; i < tot; i++, bp++)
			{
				if (bp->n) // Non-empty blocklist
				{
					blockmaplump[blockmaplump[i] = ndx++] = 0; // Store index & header
					do
						blockmaplump[ndx++] = bp->list[--bp->n]; // Copy linedef list
					while (bp->n);
					blockmaplump[ndx++] = -1; // Store trailer
					free(bp->list); // Free linedef list
				}
				else // Empty blocklist: point to reserved empty blocklist
					blockmaplump[i] = tot;
			}

			free(bmap); // Free uncompressed blockmap
		}
	}
	{
		size_t count;
		count = sizeof (*blocklinks) * bmapwidth * bmapheight;
		// clear out mobj chains (copied from from P_LoadBlockMap)
		blocklinks = Z_Calloc(count, PU_LEVEL, NULL);
		blockmap = blockmaplump + 4;
#ifdef POLYOBJECTS
		// haleyjd 2/22/06: setup polyobject blockmap
		count = sizeof(*polyblocklinks) * bmapwidth * bmapheight;
		polyblocklinks = Z_Calloc(count, PU_LEVEL, NULL);
#endif
	}
}
#endif

//
// P_LoadBlockMap
//
// Levels might not have a blockmap, so if one does not exist
// this should return false.
static boolean P_LoadBlockMap(lumpnum_t lumpnum)
{
	size_t count;
	const char *lumpname = W_CheckNameForNum(lumpnum);

	// Check if the lump exists, and if it's named "BLOCKMAP"
	if (!lumpname || memcmp(lumpname, "BLOCKMAP", 8) != 0)
	{
		if (cv_devmode)
			CONS_Printf("P_LoadBlockMap: Generating Blockmap for level %d\n", gamemap);
		return false;
	}

	count = W_LumpLength(lumpnum);

	if (!count || count >= 0x20000)
		return false;

	{
		size_t i;
		short *wadblockmaplump = malloc(count); //short *wadblockmaplump = W_CacheLumpNum (lump, PU_LEVEL);

		if (wadblockmaplump)
			W_ReadLump(lumpnum, wadblockmaplump);
		else return false;

		count /= 2;
		blockmaplump = Z_Calloc(sizeof (*blockmaplump) * count, PU_LEVEL, 0);

		// killough 3/1/98: Expand wad blockmap into larger internal one,
		// by treating all offsets except -1 as unsigned and zero-extending
		// them. This potentially doubles the size of blockmaps allowed,
		// because SRB2's engine originally considered the offsets as always signed.

		blockmaplump[0] = SHORT(wadblockmaplump[0]);
		blockmaplump[1] = SHORT(wadblockmaplump[1]);
		blockmaplump[2] = (long)(SHORT(wadblockmaplump[2])) & 0xffff;
		blockmaplump[3] = (long)(SHORT(wadblockmaplump[3])) & 0xffff;

		for (i = 4; i < count; i++)
		{
			short t = SHORT(wadblockmaplump[i]);          // killough 3/1/98
			blockmaplump[i] = t == -1 ? -1l : (long) t & 0xffff;
		}

		free(wadblockmaplump);

		bmaporgx = blockmaplump[0]<<FRACBITS;
		bmaporgy = blockmaplump[1]<<FRACBITS;
		bmapwidth = blockmaplump[2];
		bmapheight = blockmaplump[3];
	}

	// clear out mobj chains
	count = sizeof (*blocklinks)* bmapwidth*bmapheight;
	blocklinks = Z_Calloc(count, PU_LEVEL, NULL);
	blockmap = blockmaplump+4;

#ifdef POLYOBJECTS
	// haleyjd 2/22/06: setup polyobject blockmap
	count = sizeof(*polyblocklinks) * bmapwidth * bmapheight;
	polyblocklinks = Z_Calloc(count, PU_LEVEL, NULL);
#endif
	return true;
/* Original
		blockmaplump = W_CacheLumpNum(lump, PU_LEVEL);
		blockmap = blockmaplump+4;
		count = W_LumpLength (lump)/2;

		for (i = 0; i < count; i++)
			blockmaplump[i] = SHORT(blockmaplump[i]);

		bmaporgx = blockmaplump[0]<<FRACBITS;
		bmaporgy = blockmaplump[1]<<FRACBITS;
		bmapwidth = blockmaplump[2];
		bmapheight = blockmaplump[3];
	}

	// clear out mobj chains
	count = sizeof (*blocklinks)*bmapwidth*bmapheight;
	blocklinks = Z_Calloc(count, PU_LEVEL, NULL);
	return true;
	*/
}

//
// P_GroupLines
// Builds sector line lists and subsector sector numbers.
// Finds block bounding boxes for sectors.
//
static void P_GroupLines(void)
{
	line_t **linebuffer;

	size_t i, j;
	size_t totallines = 0;

	line_t *li = lines;
	sector_t *sector = sectors;
	subsector_t *ss = subsectors;
	seg_t *seg;
	fixed_t bbox[4];

	// Look up the sector number for each subsector
	for (i = 0; i < numsubsectors; i++, ss++)
	{
		if (ss->firstline >= numsegs)
			CorruptMapError(va("P_GroupLines: ss->firstline invalid "
				"(subsector %d, firstline refers to %d of %u)", ss - subsectors, ss->firstline,
				(int)numsegs));
		seg = &segs[ss->firstline];
		if (!seg->sidedef)
			CorruptMapError(va("P_GroupLines: seg->sidedef is NULL "
				"(subsector %d, firstline is %d)", ss - subsectors, ss->firstline));
		if (seg->sidedef - sides < 0 || seg->sidedef - sides > (USHORT)numsides)
			CorruptMapError(va("P_GroupLines: seg->sidedef refers to sidedef %d of %u "
				"(subsector %d, firstline is %d)", seg->sidedef - sides, (int)numsides,
				ss - subsectors, ss->firstline));
		if (!seg->sidedef->sector)
			CorruptMapError(va("P_GroupLines: seg->sidedef->sector is NULL "
				"(subsector %d, firstline is %d, sidedef is %d)", ss - subsectors, ss->firstline,
				seg->sidedef - sides));
		ss->sector = seg->sidedef->sector;
	}

	////////// Group and count the lines  ////////////

	// Count number of lines in each sector and add it to the sector's linecount
	for (i = 0; i < numlines; i++, li++)
	{
		totallines++;
		li->frontsector->linecount++;

		if (li->backsector && li->backsector != li->frontsector)
		{
			li->backsector->linecount++;
			totallines++;
		}
	}

	// Build the line tables and set the approxiamte vertex coordinates for each sector
	// These buffers store the data just for one sector at a time
	linebuffer = Z_Calloc(totallines * sizeof (*linebuffer), PU_LEVEL, NULL);

	for (i = 0; i < numsectors; i++, sector++)
	{
		M_ClearBox(bbox);
		sector->lines = linebuffer;

		li = lines;
		for (j = 0; j < numlines; j++, li++)
		{
			if (li->frontsector == sector || li->backsector == sector)
			{
				*linebuffer = li;
				linebuffer++;

				M_AddToBox(bbox, li->v1->x, li->v1->y);
				M_AddToBox(bbox, li->v2->x, li->v2->y);
			}
		}

		// Check to make sure that the data counted is correctly,
		// if it's wrong, end the game now so the game doesn't completely crash later
		if ((size_t)(linebuffer - sector->lines) != sector->linecount)
		{
			CorruptMapError(va("P_GroupLines:\n "
					"Sector %lu has miscounted linedefs\n"
					" linecount = %lu, linecount should be %lu.\n"
					"This map may have been incorrectly saved!",
					(ULONG)i, (ULONG)((size_t)(linebuffer - sector->lines)), (ULONG)sector->linecount));
		}

		// Set the degenmobj_t to the middle of the bounding box
		// SRB2CBTODO: soundorg should be called a more general "sectorcenter"
		sector->soundorg.x = (((bbox[BOXRIGHT]>>FRACBITS) + (bbox[BOXLEFT]>>FRACBITS))/2)<<FRACBITS;
		sector->soundorg.y = (((bbox[BOXTOP]>>FRACBITS) + (bbox[BOXBOTTOM]>>FRACBITS))/2)<<FRACBITS;
	}

#if 0

	////////// Group and count the vertexes ////////////

	// The exact vertexes in one map can be determined
	// by the total of all the linecount's of every sector
	// And the vertexes in one sector is equal to it's line count

	// A sector's vertex data can always be determined by a lines v1;
	// since 1 line will always have 2 vertexes (v1 and v2), using only
	// 1 vertex (v1) in a line will always get the proper data of a sector without
	// overlapping, simple geometry but it got totally overlooked before!

	size_t k;

	// The linecount has already been established, sort through them and add as a vertex!
	for (i = 0; i < numsectors; i++) // SRB2CBTODO: Do this only for a sector as its needed instead of in the precache like this
	{
		// Get the number of vertexes for this sector
		size_t totalvertexes = 0;
		totalvertexes = sectors[i].linecount;

		// Number of the vertex in the list array we're adding to
		size_t vlistnum = 0;

		// Make a list of vertexes to be stored
		vertex_t **vertexbuffer;

		// The vertexbuffer can't be individually alloc'd and freed here, because like linebuffer,
		// this data is copied to a sector's data list and the game must reserve space for it
		vertexbuffer = Z_Calloc(totalvertexes * sizeof (*vertexbuffer), PU_LEVEL, NULL);

		for (k = 0; k < sectors[i].linecount; k++)
		{
			// Add only one vertex (v1) of a line,
			// doing this logically means you cannot get any duplicates from a full polygon
			vertexbuffer[vlistnum] = sectors[i].lines[k]->v1;
			vlistnum++;
			sectors[i].vertexcount++;
		}

		sectors[i].vertexes = vertexbuffer;

		// Check if the count is correct
		if (sectors[i].vertexcount != sectors[i].linecount)
		{
			CorruptMapError(va("P_GroupLines:\n "
					"Miscounted vertexes: Sector %lu's vertexcount = %lu,\n"
					" the vertexcount should be %lu.\nThis map may be incorrectly saved!",
					(ULONG)i, (ULONG)sectors[i].vertexcount, (ULONG)sectors[i].linecount));

		}
	}
#endif
}


#ifdef EENGINEO // SRB2CBTODO:
//
// killough 10/98
//
// Remove slime trails.
//
// Slime trails are inherent to Doom's coordinate system -- i.e. there is
// nothing that a node builder can do to prevent slime trails ALL of the time,
// because it's a product of the integer coordinate system, and just because
// two lines pass through exact integer coordinates, doesn't necessarily mean
// that they will intersect at integer coordinates. Thus we must allow for
// fractional coordinates if we are to be able to split segs with node lines,
// as a node builder must do when creating a BSP tree.
//
// A wad file does not allow fractional coordinates, so node builders are out
// of luck except that they can try to limit the number of splits (they might
// also be able to detect the degree of roundoff error and try to avoid splits
// with a high degree of roundoff error). But we can use fractional coordinates
// here, inside the engine. It's like the difference between square inches and
// square miles, in terms of granularity.
//
// For each vertex of every seg, check to see whether it's also a vertex of
// the linedef associated with the seg (i.e, it's an endpoint). If it's not
// an endpoint, and it wasn't already moved, move the vertex towards the
// linedef by projecting it using the law of cosines. Formula:
//
//      2        2                         2        2
//    dx  x0 + dy  x1 + dx dy (y0 - y1)  dy  y0 + dx  y1 + dx dy (x0 - x1)
//   {---------------------------------, ---------------------------------}
//                  2     2                            2     2
//                dx  + dy                           dx  + dy
//
// (x0,y0) is the vertex being moved, and (x1,y1)-(x1+dx,y1+dy) is the
// reference linedef.
//
// Segs corresponding to orthogonal linedefs (exactly vertical or horizontal
// linedefs), which comprise at least half of all linedefs in most wads, don't
// need to be considered, because they almost never contribute to slime trails
// (because then any roundoff error is parallel to the linedef, which doesn't
// cause slime). Skipping simple orthogonal lines lets the code finish quicker.
//
// Please note: This section of code is not interchangable with TeamTNT's
// code which attempts to fix the same problem.
//
// Firelines (TM) is a Rezistered Trademark of MBF Productions
//

static void P_RemoveSlimeTrails(void) // SRB2CBTODO: AWESOME!!!!! But does it work?
{
	byte *hit = calloc(1, numvertexes);     // Hitlist for vertices
	size_t i;

	CONS_Printf("P_RemoveSlimeTrails\n");

	for(i = 0; i < numsegs; i++)                // Go through each seg
	{
		const line_t *l = segs[i].linedef;   // The parent linedef
		if (l->dx && l->dy)                  // We can ignore orthogonal lines
		{
			vertex_t *v = segs[i].v1;
			do
			{
				if(!hit[v - vertexes])         // If we haven't processed vertex
				{
					hit[v - vertexes] = 1;        // Mark this vertex as processed
					if(v != l->v1 && v != l->v2)  // Exclude endpoints of linedefs
					{ // Project the vertex back onto the parent linedef
						INT64 dx2 = (l->dx >> FRACBITS) * (l->dx >> FRACBITS);
						INT64 dy2 = (l->dy >> FRACBITS) * (l->dy >> FRACBITS);
						INT64 dxy = (l->dx >> FRACBITS) * (l->dy >> FRACBITS);
						INT64 s = dx2 + dy2;
						int x0 = v->x, y0 = v->y, x1 = l->v1->x, y1 = l->v1->y;
						v->x = (int)((dx2 * x0 + dy2 * x1 + dxy * (y0 - y1)) / s);
						v->y = (int)((dy2 * y0 + dx2 * y1 + dxy * (x0 - x1)) / s);
						// Cardboard store float versions of vertices.
						//v->fx = M_FixedToFloat(v->x);
						//v->fy = M_FixedToFloat(v->y);
					}
				}  // Obfuscated C contest entry:   :)
			}
			while((v != segs[i].v2) && (v = segs[i].v2));
		}
	}
	free(hit);
}
#endif



#if 0 // P_CheckLevel
static char *levellumps[] =
{
"label",        // ML_LABEL,    A separator, name, MAPxx
	"THINGS",       // ML_THINGS,   Enemies, items..
	"LINEDEFS",     // ML_LINEDEFS, Linedefs, from editing
	"SIDEDEFS",     // ML_SIDEDEFS, Sidedefs, from editing
	"VERTEXES",     // ML_VERTEXES, Vertices, edited and BSP splits generated
	"SEGS",         // ML_SEGS,     Linesegs, from linedefs split by BSP
	"SSECTORS",     // ML_SSECTORS, Subsectors, list of linesegs
	"NODES",        // ML_NODES,    BSP nodes
	"SECTORS",      // ML_SECTORS,  Sectors, from editing
	"REJECT",       // ML_REJECT,   LUT, sector-sector visibility
};

/** Checks a lump and returns whether it is a valid start-of-level marker.
  *
  * \param lumpnum Lump number to check.
  * \return True if the lump is a valid level marker, false if not.
  */
static inline boolean P_CheckLevel(lumpnum_t lumpnum)
{
	USHORT file, lump;
	size_t i;

	for (i = ML_THINGS; i <= ML_REJECT; i++)
	{
		file = WADFILENUM(lumpnum);
		lump = LUMPNUM(lumpnum+1);
		if (file > numwadfiles || lump < LUMPNUM(lumpnum) || lump > wadfiles[file]->numlumps ||
			memcmp(wadfiles[file]->lumpinfo[lump].name, levellumps[i], 8) != 0)
		return false;
	}
	return true; // all right
}
#endif

/** Sets up a sky texture to use for the level.
  * The sky texture is used instead of F_SKY1.
  */
void P_SetupLevelSky(int skynum)
{
	char skytexname[12];

	sprintf(skytexname, "SKY%d", skynum);
	skytexture = R_TextureNumForName(skytexname, 0xffff);
	levelskynum = skynum;

	// scale up the old skies, if needed
	R_SetupSkyDraw();
}

static const char *maplumpname;
lumpnum_t lastloadedmaplumpnum; // for comparative savegame

//
// P_LevelInitStuff
//
// Some player initialization for map start.
//
static void P_LevelInitStuff(void)
{
	int i, j;

	circuitmap = false;
	numstarposts = 0;
	totalrings = timeinmap = 0;

	for (i = 0; i < MAXPLAYERS; i++)
	{
		// Initialize the player state duration table.
		for (j = 0; j < S_PLAY_SUPERTRANS9+1; j++)
			playerstatetics[i][j] = states[j].tics;

		if (netgame || multiplayer)
		{
			// In Co-Op, replenish a user's lives if they are depleted.
			if (ultimatemode)
				players[i].lives = 1;
			else
				players[i].lives = 3;
		}

		players[i].realtime = countdown = countdown2 = 0;

		players[i].xtralife = players[i].deadtimer = players[i].numboxes = players[i].totalring = players[i].laps = 0;
		players[i].health = 1;
		players[i].nightstime = players[i].mare = 0;
		P_SetTarget(&players[i].capsule, NULL);
		players[i].aiming = 0;
		players[i].dbginfo = false;
		players[i].pflags &= ~PF_TIMEOVER;

		if (gametype == GT_RACE && players[i].lives < 3)
			players[i].lives = 3;

		players[i].exiting = 0;

#ifdef JTEBOTS
		if (players[i].bot && !players[i].mo)
#endif
		{
			P_ResetPlayer(&players[i]);
			players[i].mo = NULL;
		}
	}

	hunt1 = hunt2 = hunt3 = NULL; // SRB2CBTODO: AHA! This is stupid, don't clear hunt stuff if just reloading level

	leveltime = 0;

	localaiming = 0;
	localaiming2 = 0;

	if (mapheaderinfo[gamemap-1].countdown)
		countdowntimer = mapheaderinfo[gamemap-1].countdown * TICRATE;
	else
		countdowntimer = 0;

	countdowntimeup = false; // DuuuuuuuuuhhhH!H!H!!
#ifdef JTEBOTS
	JB_LevelInit();
#endif
}

//
// P_RehitStarposts
//
// Rehits all the starposts. Called after P_LoadThingsOnly.
//
void P_RehitStarposts(void)
{
	// Search through all the thinkers.
	mobj_t *mo;
	thinker_t *think;
	int i;

	for (think = thinkercap.next; think != &thinkercap; think = think->next)
	{
		if (think->function.acp1 != (actionf_p1)P_MobjThinker)
			continue; // not a mobj thinker

		mo = (mobj_t *)think;

		if (mo->type != MT_STARPOST)
			continue;

		for (i = 0; i < MAXPLAYERS; i++)
		{
			if (!playeringame[i])
				continue;

			if (players[i].starpostbit & (1<<(mo->health-1)))
				P_SetMobjState(mo, mo->info->seestate);
		}
	}
}

//
// P_LoadThingsOnly
//
// "Reloads" a level, but only reloads all of the mobjs.
//
void P_LoadThingsOnly(void)
{
	// Search through all the thinkers.
	mobj_t *mo;
	thinker_t *think;

	for (think = thinkercap.next; think != &thinkercap; think = think->next)
	{
		if (think->function.acp1 != (actionf_p1)P_MobjThinker)
			continue; // not a mobj thinker

		mo = (mobj_t *)think;

		// Make sure not to touch any objects that don't need to be touched
		if ((!(mo->type = MT_PLAYER) && !(mo->info->doomednum == -1)) || !mo->health || (mo->type == MT_PLAYER && !mo->player)) // was if (mo)
			P_RemoveMobj(mo);
	}

	P_LevelInitStuff();

	P_LoadThings(lastloadedmaplumpnum + ML_THINGS);

	P_SpawnSecretItems(true);

	// Mobj can't be removed here TODO: Huge bug where the game stops working with noreload levels and bots?
}

#if 0
static void R_MissingTextures(void)
{
	size_t i;
	line_t *ld;
	side_t *sdl = NULL, *sdr;
	sector_t *secl, *secr;

	// now for the missing textures
	for (i = 0; i < numlines; i++)
	{
		ld = &lines[i];

		// Skip polyobject lines
		if (ld->polyobj)
			continue;

		sdr = &sides[ld->sidenum[0]];

		if (ld->sidenum[1] != 0xffff)
			sdl = &sides[ld->sidenum[1]];

		secr = ld->frontsector;
		secl = ld->backsector;

		if (secr == secl)
			continue;

		if (secl) // only if there is a backsector
		{
			if (secl->floorheight > secr->floorheight)
			{
				// now check if r-sidedef is correct
				if (sdr->bottomtexture == 0)
				{
					if (sdr->midtexture == 0)
						sdr->bottomtexture = R_TextureNumForName("GFZROCK", (USHORT)(sdr-sides));
					else
						sdr->bottomtexture = sdr->midtexture;
				}
			}
			else if (secl->floorheight < secr->floorheight)
			{
				// now check if l-sidedef is correct
				if (sdl->bottomtexture == 0)
				{
					if (sdl->midtexture == 0)
						sdl->bottomtexture = R_TextureNumForName("GFZROCK", (USHORT)(sdl-sides));
					else
						sdl->bottomtexture = sdl->midtexture;
				}
			}


			if (secl->ceilingheight < secr->ceilingheight)
			{
				// now check if r-sidedef is correct
				if (sdr->toptexture == 0)
				{
					if (sdr->midtexture == 0)
						sdr->toptexture = R_TextureNumForName("GFZROCK", (USHORT)(sdr-sides));
					else
						sdr->toptexture = sdr->midtexture;
				}
			}
			else if (secl->ceilingheight > secr->ceilingheight)
			{
				// now check if l-sidedef is correct
				if (sdl->toptexture == 0)
				{
					if (sdl->midtexture == 0)
						sdl->toptexture = R_TextureNumForName("GFZROCK", (USHORT)(sdl-sides));
					else
						sdl->toptexture = sdl->midtexture;
				}
			}

		} // if (NULL != secl)
	} // for (i = 0; i < numlines; i++)
}
#endif

/** Compute MD5 message digest for bytes read from memory source
 *
 * The resulting message digest number will be written into the 16 bytes
 * beginning at RESBLOCK.
 *
 * \param filename path of file
 * \param resblock resulting MD5 checksum
 * \return 0 if MD5 checksum was made, and is at resblock, 1 if error was found
 */
static INT32 P_MakeBufferMD5(const char *buffer, size_t len, void *resblock)
{
#ifdef NOMD5
	(void)buffer;
	(void)len;
	memset(resblock, 0x00, 16);
	return 1;
#else
	tic_t t = I_GetTime();
#ifndef _arch_dreamcast
	if (devparm)
#endif
		CONS_Printf("Making MD5\n");
	if (md5_buffer(buffer, len, resblock) == NULL)
		return 1;
#ifndef _arch_dreamcast
	if (devparm)
#endif
		CONS_Printf("MD5 calc took %f seconds\n",
					(float)(I_GetTime() - t)/TICRATE);
	return 0;
#endif
}

static void P_MakeMapMD5(lumpnum_t maplumpnum, void *dest)
{
	unsigned char linemd5[16];
	unsigned char sectormd5[16];
	unsigned char thingmd5[16];
	unsigned char sidedefmd5[16];
	unsigned char resmd5[16];
	UINT8 i;

	// Create a hash for the current map
	// get the actual lumps!
	UINT8 *datalines   = W_CacheLumpNum(maplumpnum + ML_LINEDEFS, PU_CACHE);
	UINT8 *datasectors = W_CacheLumpNum(maplumpnum + ML_SECTORS, PU_CACHE);
	UINT8 *datathings  = W_CacheLumpNum(maplumpnum + ML_THINGS, PU_CACHE);
	UINT8 *datasides   = W_CacheLumpNum(maplumpnum + ML_SIDEDEFS, PU_CACHE);

	P_MakeBufferMD5((char*)datalines,   W_LumpLength(maplumpnum + ML_LINEDEFS), linemd5);
	P_MakeBufferMD5((char*)datasectors, W_LumpLength(maplumpnum + ML_SECTORS),  sectormd5);
	P_MakeBufferMD5((char*)datathings,  W_LumpLength(maplumpnum + ML_THINGS),   thingmd5);
	P_MakeBufferMD5((char*)datasides,   W_LumpLength(maplumpnum + ML_SIDEDEFS), sidedefmd5);

	Z_Free(datalines);
	Z_Free(datasectors);
	Z_Free(datathings);
	Z_Free(datasides);

	for (i = 0; i < 16; i++)
		resmd5[i] = (linemd5[i] + sectormd5[i] + thingmd5[i] + sidedefmd5[i]) & 0xFF;

	M_Memcpy(dest, &resmd5, 16);
}


/** Loads a level from a lump or external wad.
  *
  * \param map     Map number.
  * \param skipprecip If true, don't spawn precipitation.
  * \todo Clean up, refactor, split up; get rid of the bloat.
  */
boolean P_SetupLevel(int map, boolean skipprecip)
{
	int i, loadprecip = 1;
	int loademblems = 1;
	boolean loadedbm = false;
	sector_t *ss;

	LoadingCachedActions = true;

	// This is needed. Don't touch.
	maptol = mapheaderinfo[gamemap-1].typeoflevel;

	// SRB2CB: Do not clear chat when people are in the middle of talking!

	// Reset the palette
#if 0 // I don't think OpenGL needs this
	if (rendermode == render_opengl)
		HWR_SetPaletteColor(0);
	else
#endif
		if (rendermode != render_none)
			V_SetPaletteLump("PLAYPAL");

	// Initialize sector node list.
	// SRB2CBTODO: don't clear!
	P_Initsecnode();

	if (netgame || multiplayer)
		cv_devmode = 0;

	// Clear CECHO messages
	HU_ClearCEcho();

	if (mapheaderinfo[gamemap-1].runsoc[0] != '#')
		P_RunSOC(mapheaderinfo[gamemap-1].runsoc);

	if (cv_runscripts.value && mapheaderinfo[gamemap-1].scriptname[0] != '#')
	{
		if (mapheaderinfo[gamemap-1].scriptislump)
		{
			lumpnum_t lumpnum;
			char newname[9];

			strncpy(newname, mapheaderinfo[gamemap-1].scriptname, 8);

			newname[8] = '\0';

			lumpnum = W_CheckNumForName(newname);

			if (lumpnum == LUMPERROR || W_LumpLength(lumpnum) == 0)
			{
				CONS_Printf("SOC Error: script lump %s not found/not valid.\n", newname);
				goto noscript;
			}

			COM_BufInsertText(W_CacheLumpNum(lumpnum, PU_CACHE));
		}
		else
		{
			COM_BufAddText(va("exec %s\n", mapheaderinfo[gamemap-1].scriptname));
		}
		COM_BufExecute(); // Run it!
	}
noscript:

	P_LevelInitStuff(); // SRB2CBTODO: Should be named P_PlayerLevelInit

	postimgtype = postimg_none;

	if (mapheaderinfo[gamemap-1].forcecharacter != 255)
	{
		char skincmd[33];
		if (splitscreen)
		{
			sprintf(skincmd, "skin2 %s\n", skins[mapheaderinfo[gamemap-1].forcecharacter].name);
			CV_Set(&cv_skin2, skins[mapheaderinfo[gamemap-1].forcecharacter].name);
		}

		sprintf(skincmd, "skin %s\n", skins[mapheaderinfo[gamemap-1].forcecharacter].name);

		COM_BufAddText(skincmd);

		if (!netgame)
		{
			if (splitscreen)
			{
				SetPlayerSkinByNum(secondarydisplayplayer, mapheaderinfo[gamemap-1].forcecharacter);
				if (cv_playercolor2.value != players[secondarydisplayplayer].prefcolor)
				{
					CV_StealthSetValue(&cv_playercolor2, players[secondarydisplayplayer].prefcolor);
					players[secondarydisplayplayer].skincolor = players[secondarydisplayplayer].prefcolor;

					// a copy of color
					if (players[secondarydisplayplayer].mo)
					{
						players[secondarydisplayplayer].mo->flags |= MF_TRANSLATION;
						players[secondarydisplayplayer].mo->color = players[secondarydisplayplayer].skincolor;
					}
				}
			}

			SetPlayerSkinByNum(consoleplayer, mapheaderinfo[gamemap-1].forcecharacter);
			// Normal player colors in single player
			if (cv_playercolor.value != players[consoleplayer].prefcolor)
			{
				CV_StealthSetValue(&cv_playercolor, players[consoleplayer].prefcolor);
				players[consoleplayer].skincolor = players[consoleplayer].prefcolor;

				// a copy of color
				if (players[consoleplayer].mo)
				{
					players[consoleplayer].mo->flags |= MF_TRANSLATION;
					players[consoleplayer].mo->color = players[consoleplayer].skincolor;
				}
			}
		}
	}

	if (!dedicated)
	{
		// SRB2CBTODO: With 2D mode, use a cleaner method than changing console values
		if (maptol & TOL_2D)
			CV_SetValue(&cv_cam_speed, 0);
		else if (!cv_cam_speed.changed && !(maptol & TOL_2D))
			CV_Set(&cv_cam_speed, cv_cam_speed.defaultvalue);

		// chasecam on in chaos, race, coop
		// chasecam off in match, tag, capture the flag
		if (!cv_chasecam.changed)
			CV_SetValue(&cv_chasecam,
				(gametype == GT_RACE || gametype == GT_COOP
#ifdef CHAOSISNOTDEADYET
				|| gametype == GT_CHAOS
#endif
				) || (maptol & TOL_2D)
						|| cv_autocam.value // autocam overrides everything
			);

		// same for second player
		if (!cv_chasecam2.changed)
			CV_SetValue(&cv_chasecam2,
				(gametype == GT_RACE || gametype == GT_COOP
#ifdef CHAOSISNOTDEADYET
				|| gametype == GT_CHAOS
#endif
				) || (maptol & TOL_2D)
				|| cv_autocam2.value // autocam overrides everything
			);
	}

	// Initial height of PointOfView
	// will be set by player think.
	players[consoleplayer].viewz = 1;

	// Make sure all sounds are stopped before Z_FreeTags.
	S_StopSounds();
	S_ClearSfx();

	// Free attached sectors seperately from Z_FreeTags
	for (ss = sectors; (sectors + numsectors) != ss; ss++)
	{
		if (ss->attached)
		{
			free(ss->attached);
			ss->attached = NULL;
		}
		if (ss->attachedsolid)
		{
			free(ss->attachedsolid);
			ss->attachedsolid = NULL;
		}
	}

	// Free any data that's not needed anymore
	// Memory with the tag numbers to be freed start at number 50 (PU_LEVEL),
	// and end at PU_PURGELEVEL - 1 (100 - 1), so any memory from 50 - 99 gets freed
	// This includes PU_LEVEL(50) and PU_LEVELSPEC(51)
	// See z_zone.h for more details on the defined memory tags
	Z_FreeTags(PU_LEVEL, PU_PURGELEVEL - 1);

	// Preload graphics and textures for OpenGL
#ifdef HWRENDER
	if (rendermode == render_opengl)
		HWR_PrepLevelCache(numtextures);
#endif

#if defined (WALLSPLATS) || defined (FLOORSPLATS)
	// clear the splats from previous level
	R_ClearLevelSplats();
#endif

	P_InitThinkers();
	P_InitCachedActions();

	/// \note For not spawning precipitation, etc. when loading netgame snapshots
	if (skipprecip)
	{
		loadprecip = 0;
		loademblems = 0;
	}

	// internal game map
	lastloadedmaplumpnum = W_GetNumForName(maplumpname = G_BuildMapName(map));

	R_ClearColormaps();

	// Start the music!
	S_Start();

	// now part of level loading since in future each level may have
	// its own anim texture sequences, switches etc.
	P_InitPicAnims();

	P_MakeMapMD5(lastloadedmaplumpnum, &mapmd5);



	// Note: The ordering of loading this level data from a file is important
	loadedbm = P_LoadBlockMap(lastloadedmaplumpnum + ML_BLOCKMAP);

	P_LoadVertexes(lastloadedmaplumpnum + ML_VERTEXES);
	P_LoadSectors(lastloadedmaplumpnum + ML_SECTORS);

	P_LoadSideDefs(lastloadedmaplumpnum + ML_SIDEDEFS);

	P_LoadLineDefs(lastloadedmaplumpnum + ML_LINEDEFS);

	// If this level doesn't have a blockmap included, build a custom one
	if (!loadedbm)
		P_CreateBlockMap();

	P_LoadSideDefs2(lastloadedmaplumpnum + ML_SIDEDEFS);
	R_MakeColormaps();
	P_LoadLineDefs2();
	P_LoadSubsectors(lastloadedmaplumpnum + ML_SSECTORS);
	P_LoadNodes(lastloadedmaplumpnum + ML_NODES);
	P_LoadSegs(lastloadedmaplumpnum + ML_SEGS);
	rejectmatrix = W_CacheLumpNum(lastloadedmaplumpnum + ML_REJECT, PU_LEVEL);
#ifdef EENGINEO
	P_RemoveSlimeTrails(); // killough 10/98: remove slime trails from wad
#endif
	// Group geometry and create other data for sectors,
	// such as data for each linedef and vertex
	P_GroupLines();
	// SoM: Deferred specials that need to be spawned after P_SpawnSpecials,
	// NOTE: These specials MUST be loaded BEFORE other things and objects,
	// especially for slopes because they are needed for floorz coords of spawned objects!
	// all specials cannot be be spawned before 'things' are, because some specials need objects!
#ifdef ESLOPE
	P_SpawnDeferredSpecials(); // ESLOPE
	//P_SpawnSlopeMakers(, );
#endif

	numdmstarts = numredctfstarts = numbluectfstarts = numtagstarts = 0;


	// reset the player starts
	for (i = 0; i < MAXPLAYERS; i++)
		playerstarts[i] = NULL;

	P_MapStart();
#ifdef ESLOPE
	// All objects that create/effect slopes have to be spawened before other objects
	P_SetSlopesFromVertexHeights(lastloadedmaplumpnum + ML_THINGS);
#endif

	P_LoadThings(lastloadedmaplumpnum + ML_THINGS);

	P_SpawnSecretItems(loademblems);

	for (numcoopstarts = 0; numcoopstarts < MAXPLAYERS; numcoopstarts++)
		if (!playerstarts[numcoopstarts])
			break;

	// set up world state
	P_SpawnSpecials();

	if (loadprecip) //  ugly hack for P_NetUnArchiveMisc (and P_LoadNetGame) // SRB2CBTODO: DON'T reload an entire level by default!
		P_SpawnPrecipitation();

	globalweather = mapheaderinfo[gamemap-1].weather;

#ifdef HWRENDER
	if (rendermode == render_opengl)
	{
		HWR_ResetLights(); // SRB2CBTODO: Light reset effecient like this?
		HWR_ResetShadows();
		HWR_CreateGLBSP((int)numnodes - 1); // SRB2CBTODO: OpenGL could be worked to not use this?
	}
#endif

	//R_MissingTextures(); // SRB2CBTODO: Correct missing textures that cause HOM

	for (i = 0; i < MAXPLAYERS; i++)
	{
		if (playeringame[i])
		{
			players[i].pflags &= ~PF_NIGHTSMODE;

			if (gametype == GT_MATCH
#ifdef CHAOSISNOTDEADYET
				|| gametype == GT_CHAOS
#endif
				|| gametype == GT_TAG)
			{
				players[i].mo = NULL;
				G_DoReborn(i);
			}
			else // gametype is GT_COOP or GT_RACE
			{
				players[i].mo = NULL;

				if (players[i].starposttime)
				{
					G_CoopSpawnPlayer(i, true);
					P_ClearStarPost(&players[i], players[i].starpostnum);
				}
				else
					G_CoopSpawnPlayer(i, false);
			}
		}
	}

	if (gametype == GT_TAG)
	{
		int realnumplayers = 0;
		int playersactive[MAXPLAYERS];

		// I just realized how problematic this code can be.
		// D_NumPlayers() will not always cover the scope of the netgame.
		// What if one player is node 0 and the other node 31?
		// The solution? Make a temp array of all players that are currently playing and pick from them.
		// Future todo? When a player leaves, shift all nodes down so D_NumPlayers() can be used as intended?
		// Also, you'd never have to loop through all 32 players slots to find anything ever again. =P -Jazz
		for (i = 0; i < MAXPLAYERS; i++)
		{
			if (playeringame[i] && !players[i].spectator)
			{
				playersactive[realnumplayers] = i; // stores the player's node in the array.
				realnumplayers++;
			}
		}

		if (realnumplayers) // This should also fix the dedicated server crash bug. You only pick a player if one exists to be picked.
		{
			i = P_Random() % realnumplayers;
			players[playersactive[i]].pflags |= PF_TAGIT; // Choose the initial tagger before map starts.

			// Taken and modified from G_DoReborn()
			// Remove the player so he can respawn elsewhere.
			// First, nullify the player
			if (players[playersactive[i]].mo)
			{
				players[playersactive[i]].mo->player = NULL;
				players[playersactive[i]].mo->flags2 &= ~MF2_DONTDRAW;
				// Remove the player, NOTE: If the player has no ->player in its mo,
				// P_SetMobjState can be used // SRB2CBTODO: OR should P_SetPlayermobjState be used to NULL it?!
				P_SetMobjState(players[playersactive[i]].mo, S_DISS);
			}

			G_DeathMatchSpawnPlayer(playersactive[i]); // Respawn the tagger in a spawn location
		}
		else
			CONS_Printf("No player currently available to become IT. Awaiting available players.\n");

	}

	if (!dedicated)
	{
		if (players[displayplayer].mo)
		{
			camera.x = players[displayplayer].mo->x;
			camera.y = players[displayplayer].mo->y;
			camera.z = players[displayplayer].mo->z;
			camera.angle = players[displayplayer].mo->angle;
		}

		if (!cv_cam_height.changed)
			CV_Set(&cv_cam_height, cv_cam_height.defaultvalue);

		if (!cv_cam_dist.changed)
			CV_Set(&cv_cam_dist, cv_cam_dist.defaultvalue);

		if (!cv_cam_rotate.changed)
			CV_Set(&cv_cam_rotate, cv_cam_rotate.defaultvalue);

		if (!cv_cam2_height.changed)
			CV_Set(&cv_cam2_height, cv_cam2_height.defaultvalue);

		if (!cv_cam2_dist.changed)
			CV_Set(&cv_cam2_dist, cv_cam2_dist.defaultvalue);

		if (!cv_cam2_rotate.changed)
			CV_Set(&cv_cam2_rotate, cv_cam2_rotate.defaultvalue);

		if (!cv_useranalog.value)
		{
			if (!cv_analog.changed)
				CV_SetValue(&cv_analog, 0);
			if (!cv_analog2.changed)
				CV_SetValue(&cv_analog2, 0);
		}

#ifdef HWRENDER
		if (rendermode == render_opengl)
			CV_Set(&cv_grfov, cv_grfov.defaultvalue);
#endif

		displayplayer = consoleplayer; // Start with your OWN view, please!
	}

	if (cv_useranalog.value)
		CV_SetValue(&cv_analog, true);

	if (splitscreen && cv_useranalog2.value)
		CV_SetValue(&cv_analog2, true);

	if (twodlevel)
	{
		// SRB2CBTODO: Actually handle twodlevel's setup in the code, not by console values!
		CV_SetValue(&cv_cam_dist, 320); // SRB2CBTODO: NO! Set analog and cam dist in the 2d code itself!
		CV_SetValue(&cv_analog2, false);
		CV_SetValue(&cv_analog, false);
	}

	// Clear special respawning que
	iquehead = iquetail = 0;

	// Start CD music for this level (Note: can be remapped)
	I_PlayCD(map + 1, false);

	P_MapEnd();


	if (precache || dedicated)
		R_PrecacheLevel();

	nextmapoverride = 0;
	nextmapgametype = -1;
	skipstats = false;

	if (!(netgame || multiplayer) && (!modifiedgame || savemoddata))
		mapvisited[gamemap-1] = true;

	LoadingCachedActions = false;
	useskybox = false;
	moveskybox = false;
	skyboxmobj = NULL;
	skyboxcentermobj = NULL;

	P_RunCachedActions();

	// SRB2 determines the sky texture to be used depending on the map header,
	// The sky is setup after skyboxes are determined
	P_SetupLevelSky(mapheaderinfo[gamemap-1].skynum);
	globallevelskynum = levelskynum;

	// Kalaron: Auto-save every act :P
	if (!(netgame || multiplayer || demoplayback || demorecording || timeattacking || players[consoleplayer].lives <= 0)
		&& (!modifiedgame || savemoddata)
		&& cursaveslot != -1 && !ultimatemode
		&& ((!mapheaderinfo[gamemap-1].hideinmenu && (!G_IsSpecialStage(gamemap)) && gamemap != lastmapsaved)
			|| gamecomplete))
		G_SaveGame((unsigned int)cursaveslot);

	if (savedata.lives > 0)
	{
		players[consoleplayer].continues = savedata.continues;
		players[consoleplayer].lives = savedata.lives;
		players[consoleplayer].score = savedata.score;
		emeralds = savedata.emeralds;
		savedata.lives = 0;
	}

#ifdef JTEBOTS
	for (i = 0; i < MAXPLAYERS; i++)
	{
		if (players[i].bot)
			P_ResetPlayer(&players[i]);
	}
#endif

	return true;
}

//
// P_RunSOC
//
// Runs a SOC file or a lump, depending on if ".SOC" exists in the filename
//
boolean P_RunSOC(const char *socfilename)
{
	lumpnum_t lump;

	if (strstr(socfilename, ".soc") != NULL)
		return P_AddWadFile(socfilename, NULL);

	lump = W_CheckNumForName(socfilename);
	if (lump == LUMPERROR)
		return false;

	CONS_Printf("Loading SOC lump: %s\n", socfilename);
	DEH_LoadDehackedLump(lump);

	return true;
}

//
// Add a wadfile to the active wad files,
// replace sounds, musics, patches, textures, sprites and maps
//
boolean P_AddWadFile(const char *wadfilename, char **firstmapname)
{
	size_t i, j, sreplaces = 0, mreplaces = 0;
	USHORT numlumps, wadnum;
	short firstmapreplaced = 0, num;
	char *name;
	lumpinfo_t *lumpinfo;
	boolean texturechange = false;
	boolean replacedcurrentmap = false;

	if ((numlumps = W_LoadWadFile(wadfilename)) == MAXSHORT)
	{
		CONS_Printf("couldn't load wad file %s\n", wadfilename);
		return false;
	}
	else wadnum = (USHORT)(numwadfiles-1);

	//
	// search for sound replacements
	//
	lumpinfo = wadfiles[wadnum]->lumpinfo;
	for (i = 0; i < numlumps; i++, lumpinfo++)
	{
		name = lumpinfo->name;
		if (name[0] == 'D')
		{
			if (name[1] == 'S') for (j = 1; j < NUMSFX; j++)
			{
				if (S_sfx[j].name && !strnicmp(S_sfx[j].name, name + 2, 6))
				{
					// the sound will be reloaded when needed,
					// since sfx->data will be NULL
					if (devparm)
						I_OutputMsg("Sound %.8s replaced\n", name);

					I_FreeSfx(&S_sfx[j]);

					sreplaces++;
				}
			}
			else if (name[1] == '_')
			{
				if (devparm)
					I_OutputMsg("Music %.8s replaced\n", name);
				mreplaces++;
			}
		}
		
		else if (!memcmp(name, "TX_START", 8) || !memcmp(name, "TX_END", 8))
			texturechange = true;
	}
	if (!devparm && sreplaces)
		CONS_Printf("%u sounds replaced\n", (int)sreplaces);
	if (!devparm && mreplaces)
		CONS_Printf("%u musics replaced\n", (int)mreplaces);

	//
	// search for sprite replacements
	//
	R_AddSpriteDefs(wadnum);

	// Reload it all anyway, just in case they
	// added some textures but didn't insert a
	// TEXTURE1/PNAMES/etc. list.
	if (texturechange) // initialized in the sound check
		R_LoadTextures(); // numtexture changes
	else
		R_FlushTextureCache(); // just reload it from file

	// Flush and reload HUD graphics
	ST_UnloadGraphics();
	HU_LoadGraphics();
	ST_LoadGraphics();
	ST_ReloadSkinFaceGraphics();

	//
	// look for skins
	//
	R_AddSkins(wadnum); // faB: wadfile index in wadfiles[]

	//
	// search for maps
	//
	lumpinfo = wadfiles[wadnum]->lumpinfo;
	for (i = 0; i < numlumps; i++, lumpinfo++)
	{
		name = lumpinfo->name;
		num = firstmapreplaced;

		if (name[0] == 'M' && name[1] == 'A' && name[2] == 'P') // Ignore the headers
		{
			num = (short)M_MapNumber(name[3], name[4]);

			//If you replaced the map you're on, end the level when done.
			if (num == gamemap)
				replacedcurrentmap = true;

			if (name[5] == 'D')
				P_LoadMapHeader(num);
			else if (name[5]!='\0')
				continue;

			CONS_Printf("%s\n", name);
		}

		if (num && (num < firstmapreplaced || !firstmapreplaced))
		{
			firstmapreplaced = num;
			if (firstmapname)
				*firstmapname = name;
		}
	}
	if (!firstmapreplaced)
		CONS_Printf("no maps added\n");

	// reload status bar (warning should have valid player!)
	if (gamestate == GS_LEVEL)
		ST_Start();

	if (replacedcurrentmap && gamestate == GS_LEVEL && (netgame || multiplayer))
	{
		CONS_Printf("Current map %d replaced by added file, ending the level to ensure consistiency.\n", gamemap);
		if (server)
			SendNetXCmd(XD_EXITLEVEL, NULL, 0);
	}

	return true;
}

