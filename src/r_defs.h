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
/// \brief Refresh/rendering module, shared data struct definitions

#ifndef __R_DEFS__
#define __R_DEFS__

// Some more or less basic data types we depend on.
#include "m_fixed.h"

// We rely on the thinker data struct to handle sound origins in sectors.
#include "d_think.h"
// SECTORS do store Mobjs anyway.
#include "p_mobj.h"

#include "screen.h" // MAXVIDWIDTH, MAXVIDHEIGHT

#define POLYOBJECTS
// SRB2CBTODO: Polyobjects need custom sector functions just for plane drawing
#define POLYOBJECTS_PLANES // Polyobject fake flat code for OpenGL and Software mode,
//#define POLYOBJECTS_PLANESS // Software polyobject planes, AKA never gonna happen

//
// ClipWallSegment
// Clips the given range of columns
// and includes it in the new clip list.
//
typedef struct
{
	int first;
	int last;
} cliprange_t;

// Silhouette, needed for clipping segs (mainly) and sprites representing things.
#define SIL_NONE   0
#define SIL_BOTTOM 1
#define SIL_TOP    2
#define SIL_BOTH   3

// This could be wider for >8 bit display.
// Indeed, true color support is possible precalculating 24bpp lightmap/colormap LUT
// from darkening PLAYPAL to all black.
// Could even use more than 32 levels.
typedef unsigned char lighttable_t;

// ExtraColormap type. Use for extra_colormaps from now on.
typedef struct
{
	USHORT maskcolor, fadecolor;
	double maskamt;
	USHORT fadestart, fadeend;
	int fog;

	// rgba is used in hw mode for colored sector lighting
	int rgba; // similar to maskcolor in sw mode

	lighttable_t *colormap;
} extracolormap_t;

//
// INTERNAL MAP TYPES used by play and refresh
//

// SRB2CBTODO: Vertexes for easy access in a sector
struct vertex_s;

/** Your plain vanilla vertex.
  */
typedef struct vertex_s
{
	fixed_t x; ///< X coordinate.
	fixed_t y; ///< Y coordinate.
	// New to the original engine, this maintains special coordinates for slopes only,
	// this is not representative of the vertex's actual z coordinate,
	// only slopped sectors use this, other sectors that aren't slopes
	// don't check for this and are uneffected in physics and rendering
	fixed_t z; ///< Z coordinate.
} vertex_t;

// Forward of linedefs, for sectors.
struct line_s;

/** Degenerate version of ::mobj_t, storing only a location.
  * Used for sound origins in sectors, hoop centers, and the like. Does not
  * handle sound from moving objects (doppler), because position is probably
  * just buffered, not updated.
  */
typedef struct
{
	thinker_t thinker; ///< Not used for anything.
	fixed_t x;         ///< X coordinate.
	fixed_t y;         ///< Y coordinate.
	fixed_t z;         ///< Z coordinate.
} degenmobj_t;

//
// Slope Structures
//

// ZDoom C++ to SRB2 C conversion (for slopes)
typedef struct secplane_t
{
	// the plane is defined as a*x + b*y + c*z + d = 0
	// ic is 1/c, for faster Z calculations

	fixed_t a, b, c, d, ic;
} secplane_t;

// SoM: Map-slope struct. Stores both floating point and fixed point versions
// of the vectors.
#ifdef ESLOPE

#include "m_vector.h"

typedef struct
{
	// --- Information used in clipping/projection ---
	// Origin vector for the plane
	// NOTE: All similarly named entries in this struct do the same thing,
	// differing with just 'f' in the name for float:
	// o = of, d = df, zdelta = zdeltaf; the only difference is that one's fixed,
	// and the one with the 'f' is floating point, for easier reference elsewhere in the code
	v3fixed_t o;
	v3float_t of;

	// The normal of the 3d plane the slope creates.
	v3float_t normalf;

	// 2-Dimentional vector (x, y) normalized. Used to determine distance from
	// the origin in 2d mapspace.
	v2fixed_t d;
	v2float_t df;

	// The rate at which z changes based on distance from the origin plane.
	fixed_t zdelta;
	float   zdeltaf;

	// For comparing when a slope should be rendered
	fixed_t lowz;
	fixed_t highz;

	// SRB2CBTODO: This could be used for something?
	// Determining the relative z values in a slope?
	struct line_s *sourceline;

	// This values only check and must be updated if the slope itself is modified
	USHORT zangle; // Angle of the plane going up from the ground (not mesured in degrees)
	angle_t xydirection; // The direction the slope is facing (north, west, south, etc.)
	secplane_t secplane; // Extra data for collision and stuff
} pslope_t;
#endif

#ifdef POLYOBJECTS
#include "p_polyobj.h"
#endif

// Store fake planes in a resizable array insted of just by
// heightsec. Allows for multiple fake planes.
/** Flags describing 3Dfloor behavior and appearance.
  */
typedef enum
{
	FF_EXISTS            = 0x1,        ///< Always set, to check for validity.
	FF_BLOCKPLAYER       = 0x2,        ///< Solid to player, but nothing else
	FF_BLOCKOTHERS       = 0x4,        ///< Solid to everything but player
	FF_SOLID             = 0x6,        ///< Clips things.
	FF_RENDERSIDES       = 0x8,        ///< Renders the sides.
	FF_RENDERPLANES      = 0x10,       ///< Renders the floor/ceiling.
	FF_RENDERALL         = 0x18,       ///< Renders everything.
	FF_SWIMMABLE         = 0x20,       ///< Is a water block.
	FF_NOSHADE           = 0x40,       ///< Messes with the lighting?
	FF_CUTSOLIDS         = 0x80,       ///< Cuts out hidden solid pixels.
	FF_CUTEXTRA          = 0x100,      ///< Cuts out hidden translucent pixels.
	FF_CUTLEVEL          = 0x180,      ///< Cuts out all hidden pixels.
	FF_CUTSPRITES        = 0x200,      ///< Final step in making 3D water.
	FF_BOTHPLANES        = 0x400,      ///< Renders both planes all the time.
	FF_EXTRA             = 0x800,      ///< Gets cut by ::FF_CUTEXTRA.
	FF_TRANSLUCENT       = 0x1000,     ///< See through! Not to be confused with FF_FOG blocks, which are just colormaped areas
	FF_FOG               = 0x2000,     ///< Fog "brush." Copy a colormap and fade the transparency of the block by the fog's lightlevel
	FF_INVERTPLANES      = 0x4000,     ///< Reverse the plane visibility rules.
	FF_ALLSIDES          = 0x8000,     ///< Render inside and outside sides.
	FF_INVERTSIDES       = 0x10000,    ///< Only render inside sides.
	FF_DOUBLESHADOW      = 0x20000,    ///< Make two lightlist entries to reset light?
	FF_FLOATBOB          = 0x40000,    ///< Floats on water and bobs if you step on it.
	FF_NORETURN          = 0x80000,    ///< Used with ::FF_CRUMBLE. Will not return to its original position after falling.
	FF_CRUMBLE           = 0x100000,   ///< Falls 2 seconds after being stepped on, and randomly brings all touching crumbling 3dfloors down with it, providing their master sectors share the same tag (allows crumble platforms above or below, to also exist).
	FF_SHATTERBOTTOM     = 0x200000,   ///< Used with ::FF_BUSTUP. Like FF_SHATTER, but only breaks from the bottom. Good for springing up through rubble.
	FF_MARIO             = 0x400000,   ///< Acts like a question block when hit from underneath. Goodie spawned at top is determined by master sector.
	FF_BUSTUP            = 0x800000,   ///< You can spin through/punch this block and it will crumble!
	FF_QUICKSAND         = 0x1000000,  ///< Quicksand!
	FF_PLATFORM          = 0x2000000,  ///< You can jump up through this to the top.
	FF_REVERSEPLATFORM   = 0x4000000,  ///< A fall-through floor in normal gravity, a platform in reverse gravity.
	FF_INTANGABLEFLATS   = 0x6000000,  ///< Both flats are intangable, but the sides are still solid.
	FF_SHATTER           = 0x8000000,  ///< Used with ::FF_BUSTUP. Thinks everyone's Knuckles.
	FF_SPINBUST          = 0x10000000, ///< Used with ::FF_BUSTUP. Jump or fall onto it while curled in a ball.
	FF_ONLYKNUX          = 0x20000000, ///< Used with ::FF_BUSTUP. Only Knuckles can break this rock.
	FF_RIPPLE            = 0x40000000, ///< Ripple the flats
	FF_COLORMAPONLY      = 0x80000000  ///< Only copy the colormap, not the lightlevel
} ffloortype_e;

typedef struct ffloor_s
{
	fixed_t *topheight;
	pslope_t *t_slope;
	long *toppic;
	short *toplightlevel;
	fixed_t *topxoffs;
	fixed_t *topyoffs;
	angle_t *topangle;
	long *topscale;

	fixed_t *bottomheight;
	pslope_t *b_slope;
	long *bottompic;
	fixed_t *bottomxoffs;
	fixed_t *bottomyoffs;
	angle_t *bottomangle;
	long *bottomscale;

	fixed_t delta;

	size_t secnum;
	ffloortype_e flags;
	struct line_s *master;

	struct sector_s *target;

	struct ffloor_s *next;
	struct ffloor_s *prev;

	int lastlight;
	int alpha;
	tic_t norender; // for culling

} ffloor_t;


// This struct holds information for shadows casted by 3D floors.
// This information is contained inside the sector_t and is used as the base
// information for casted shadows.
typedef struct lightlist_s
{
	fixed_t height;
	short *lightlevel;
	extracolormap_t *extra_colormap;
	int flags;
	ffloor_t *caster;
	pslope_t *heightslope; // Only 1 plane needs to be stored for the top of the shadow
} lightlist_t;


// This struct is used for rendering walls with shadows casted on them...
typedef struct r_lightlist_s
{
	fixed_t height;
	fixed_t heightstep;
	fixed_t botheight;
	fixed_t botheightstep;
	short lightlevel;
	extracolormap_t *extra_colormap;
	lighttable_t *rcolormap;
	ffloortype_e flags;
	int lightnum;
} r_lightlist_t;

// Kalaron: Made flag names eaiser to understand
// SF_TRIGGERSPECIAL_FLOOR - sector effect is triggered when an object touches the floor
// SF_TRIGGERSPECIALL_CEILING - sector effect is triggered when an object touches the ceiling
// SF_TRIGGERSPECIAL_BOTH - sector effect is triggered when an object touches the floor or the ceiling
// SF_TRIGGERSPECIAL_INSIDE - sector effect is triggered when an object is anywhere inside the sector
typedef enum
{
	SF_TRIGGERSPECIAL_FLOOR    =  1,
	SF_TRIGGERSPECIALL_CEILING  =  2,
	SF_TRIGGERSPECIAL_BOTH     =  3,
	SF_TRIGGERSPECIAL_INSIDE =  4
} sectorflags_t;

//
// The SECTORS record, at runtime.
// Stores things/mobjs.
//
typedef struct sector_s
{
	fixed_t floorheight;
	fixed_t ceilingheight;
	long floorpic;
	long ceilingpic;
	short lightlevel;
	short special;
	short tag;
	long nexttag, firsttag; // for fast tag searches

	// origin for any sounds played by the sector
	// also considered the center for e.g. Mario blocks
	degenmobj_t soundorg;

	// if == validcount, already checked
	size_t validcount;

	// list of mobjs in sector
	mobj_t *thinglist;

	// thinker_ts for reversable actions
	void *floordata; // floor move thinker
	void *ceilingdata; // ceiling move thinker
	void *lightingdata; // lighting change thinker

	// floor and ceiling texture offsets
	fixed_t floor_xoffs, floor_yoffs;
	fixed_t ceiling_xoffs, ceiling_yoffs;

	// flat angle
	angle_t floorpic_angle; // SRB2CBTODO: Polyobjects for OpenGL need this!
	angle_t ceilingpic_angle;

	// Set the plane to draw infinitely
	long floor_scale;
	long ceiling_scale;

	long heightsec; // other sector, or -1 if no other sector

	long floorlightsec, ceilinglightsec;
	int crumblestate; // used for crumbling and bobbing

	long bottommap, midmap, topmap; // dynamic colormaps

	// list of mobjs that are at least partially in the sector
	// thinglist is a subset of touching_thinglist
	struct msecnode_s *touching_thinglist;

	size_t linecount;
	struct line_s **lines; // [linecount] size

	// Improved fake floor hack
	ffloor_t *ffloors;
	size_t *attached;
	boolean *attachedsolid;
	size_t numattached;
	size_t maxattached;
	lightlist_t *lightlist;
	int numlights;
	boolean moved;

	// per-sector colormaps!
	extracolormap_t *extra_colormap;

	// This points to the master's floorheight, so it can be changed in realtime!
	fixed_t *gravity; // per-sector gravity
#ifdef VPHYSICS
	// VPHYSICS TODO: Direction of gravity
#endif
	boolean verticalflip; // If gravity < 0, then allow flipped physics
	sectorflags_t flags;

	// Sprite culling feature
	struct line_s *cullheight;

	// Current speed of ceiling/floor. For Knuckles to hold onto stuff.
	fixed_t floorspeed, ceilspeed;

	// list of precipitation mobjs in sector
	precipmobj_t *preciplist;
	struct mprecipsecnode_s *touching_preciplist;

#ifdef ESLOPE
	// Eternity engine slope
	pslope_t *f_slope; // floor slope
	pslope_t *c_slope; // ceiling slope
#endif
} sector_t;

//
// Move clipping aid for linedefs.
//
typedef enum
{
	ST_HORIZONTAL,
	ST_VERTICAL,
	ST_POSITIVE,
	ST_NEGATIVE
} slopetype_t;

typedef struct line_s
{
	// Vertices, from v1 to v2.
	vertex_t *v1;
	vertex_t *v2;

	fixed_t dx, dy; // Precalculated v2 - v1 for side checking.

	// Animation related.
	short flags;
	short special;
	short tag;

	// Visual appearance: sidedefs.
	USHORT sidenum[2]; // sidenum[1] will be 0xffff if one-sided

	fixed_t bbox[4]; // bounding box for the extent of the linedef

	// To aid move clipping.
	slopetype_t slopetype;

	// Front and back sector.
	// Note: redundant? Can be retrieved from SideDefs.
	sector_t *frontsector;
	sector_t *backsector;

	size_t validcount; // if == validcount, already checked
	void *splats; // wallsplat_t list
	long firsttag, nexttag; // improves searches for tags.
#ifdef POLYOBJECTS
	polyobj_t *polyobj; // Belongs to a polyobject?
#endif

#ifdef EPORTAL
	// SoM 12/10/03: wall portals
	portal_t *portal;
#endif

#ifdef ESLOPE
	// SoM 05/11/09: Pre-calculated 2D normal for the line
	float nx, ny;
	float len;
#endif
} line_t;


#ifdef FRADIO
typedef struct shadowcorner_s {
    float           corner;
    struct sector_s* proximity;
    float           pOffset;
    float           pHeight;
} shadowcorner_t;

typedef struct edgespan_s {
    float           length;
    float           shift;
} edgespan_t;
#endif




//
// The SideDef.
//

typedef struct
{
	// add this to the calculated texture column
	fixed_t textureoffset;

	// add this to the calculated texture top
	fixed_t rowoffset;

	// Texture indices.
	// We do not maintain names here.
	long toptexture, bottomtexture, midtexture;

	// Sector the SideDef is facing.
	sector_t *sector;

	short special; // the special of the linedef this side belongs to
	short repeatcnt; // # of times to repeat midtexture

#ifdef FRADIO
	int                 fakeRadioUpdateCount; // frame number of last update
    shadowcorner_t      topCorners[2];
    shadowcorner_t      bottomCorners[2];
    shadowcorner_t      sideCorners[2];
    edgespan_t          spans[2];      // [left, right]
#endif
} side_t;

//
// A subsector.
// References a sector.
// Basically, this is a list of linesegs, indicating the visible walls that define
//  (all or some) sides of a convex BSP leaf.
//
typedef struct subsector_s
{
	sector_t *sector;
	// haleyjd 06/19/06: converted from short to long for 65535 segs
	//long  numlines, firstline; // SRB2CBTODO: Full long conversion from short
	short numlines;
	USHORT firstline;
#ifdef POLYOBJECTS
	struct polyobj_s *polyList; // haleyjd 02/19/06: list of polyobjects
#endif
	void *splats; // floorsplat_t list
	size_t validcount;
} subsector_t;

// Sector list node showing all sectors an object appears in.
//
// There are two threads that flow through these nodes. The first thread
// starts at touching_thinglist in a sector_t and flows through the m_snext
// links to find all mobjs that are entirely or partially in the sector.
// The second thread starts at touching_sectorlist in an mobj_t and flows
// through the m_tnext links to find all sectors a thing touches. This is
// useful when applying friction or push effects to sectors. These effects
// can be done as thinkers that act upon all objects touching their sectors.
// As an mobj moves through the world, these nodes are created and
// destroyed, with the links changed appropriately.
//
// For the links, NULL means top or end of list.

typedef struct msecnode_s
{
	sector_t *m_sector; // a sector containing this object
	struct mobj_s *m_thing;  // this object
	struct msecnode_s *m_tprev;  // prev msecnode_t for this thing
	struct msecnode_s *m_tnext;  // next msecnode_t for this thing
	struct msecnode_s *m_sprev;  // prev msecnode_t for this sector
	struct msecnode_s *m_snext;  // next msecnode_t for this sector
	boolean visited; // used in search algorithms
} msecnode_t;

typedef struct mprecipsecnode_s
{
	sector_t *m_sector; // a sector containing this object
	struct precipmobj_s *m_thing;  // this object
	struct mprecipsecnode_s *m_tprev;  // prev msecnode_t for this thing
	struct mprecipsecnode_s *m_tnext;  // next msecnode_t for this thing
	struct mprecipsecnode_s *m_sprev;  // prev msecnode_t for this sector
	struct mprecipsecnode_s *m_snext;  // next msecnode_t for this sector
	boolean visited; // used in search algorithms
} mprecipsecnode_t;

//
// The lineseg.
//
typedef struct seg_s
{
	vertex_t *v1;
	vertex_t *v2;

	int side;

	fixed_t offset;

	angle_t angle;

	side_t *sidedef;
	line_t *linedef;

	// Sector references.
	// Could be retrieved from linedef, too. backsector is NULL for one sided lines
	sector_t *frontsector;
	sector_t *backsector;

	float len; // float length of the seg, for OpenGL conversion and slopes

	// Why slow things down by calculating lightlists for every thick side? // SRB2CBTODO: FIX
	size_t numlights;
	r_lightlist_t *rlights;
#ifdef POLYOBJECTS
	polyobj_t *polyseg;
	boolean dontrenderme;
#endif
} seg_t;


#ifdef FRADIO




#define RL_MAX_DIVS         64
typedef struct walldiv_s {
    unsigned int    num;
    float           pos[RL_MAX_DIVS];
} walldiv_t;

typedef struct rvertex_s {
    float           pos[3];
} rvertex_t;


typedef struct rcolor_s {
    float           rgba[4];
} rcolor_t;

typedef struct rtexcoord_s {
    float           st[2];
} rtexcoord_t;

typedef struct shadowlink_s {
    struct shadowlink_s* next;
    seg_t*      lineDef;
    byte            side;
} shadowlink_t;

#endif

//
// BSP node.
//
typedef struct
{
	// Partition line.
	fixed_t x, y;
	fixed_t dx, dy;

	// Bounding box for each child.
	fixed_t bbox[2][4];

	// If NF_SUBSECTOR its a subsector.
	USHORT children[2];
} node_t;

#if defined(_MSC_VER)
#pragma pack(1)
#endif

// posts are runs of non masked source pixels
typedef struct
{
	byte topdelta; // -1 is the last post in a column
	byte length;   // length data bytes follows
} ATTRPACK post_t;

#if defined(_MSC_VER)
#pragma pack()
#endif

// column_t is a list of 0 or more post_t, (byte)-1 terminated
typedef post_t column_t;

//
// OTHER TYPES
//

#ifndef MAXFFLOORS
#define MAXFFLOORS 40
#endif

//
// ?
//
typedef struct drawseg_s
{
	seg_t *curline;
	int x1;
	int x2;

	fixed_t scale1;
	fixed_t scale2;
	fixed_t scalestep;

	int silhouette; // 0 = none, 1 = bottom, 2 = top, 3 = both

	fixed_t bsilheight; // do not clip sprites above this
	fixed_t tsilheight; // do not clip sprites below this

	// Pointers to lists for sprite clipping, all three adjusted so [x1] is first value.
	short *sprtopclip;
	short *sprbottomclip;
	short *maskedtexturecol;

	struct visplane_s *ffloorplanes[MAXFFLOORS];
	int numffloorplanes;
	struct ffloor_s *thicksides[MAXFFLOORS];
	short *thicksidecol;
	int numthicksides;
	fixed_t frontscale[MAXVIDWIDTH];
} drawseg_t;

typedef enum
{
	PALETTE         = 0,  // 1 byte is the index in SRB2's palette (as usual)
	INTENSITY       = 1,  // 1 byte intensity
	INTENSITY_ALPHA = 2,  // 2 byte: alpha then intensity
	RGB24           = 3,  // 24 bit rgb
	RGBA32          = 4   // 32 bit rgba
} pic_mode_t;

#if defined(_MSC_VER)
#pragma pack(1)
#endif

// Patches.
// A patch holds one or more columns.
// Patches are used for sprites and all masked pictures, and we compose
// textures from the list of patches.
//
// WARNING: this structure is cloned in GLPatch_t
typedef struct
{
	short width;          // bounding box size
	short height;
	short leftoffset;     // pixels to the left of origin
	short topoffset;      // pixels below the origin
	int columnofs[8];     // only [width] used
	// the [0] is &columnofs[width]
} ATTRPACK patch_t;

#ifdef _MSC_VER
#pragma warning(disable :  4200)
#endif

// a pic is an unmasked block of pixels, stored in horizontal way
typedef struct
{
	short width;
	byte zero;       // set to 0 allow autodetection of pic_t
	                 // mode instead of patch or raw
	byte mode;       // see pic_mode_t above
	short height;
	short reserved1; // set to 0
	byte data[0]; // SRB2CBTODO: ** data?
} ATTRPACK pic_t;

#ifdef _MSC_VER
#pragma warning(default : 4200)
#endif

#if defined(_MSC_VER)
#pragma pack()
#endif

//
// Sprites are patches with a special naming convention so they can be
//  recognized by R_InitSprites.
// The base name is NNNNFx or NNNNFxFx, with x indicating the rotation,
//  x = 0, 1-7.
// The sprite and frame specified by a thing_t is range checked at run time.
// A sprite is a patch_t that is assumed to represent a three dimensional
//  object and may have multiple rotations predrawn.
// Horizontal flipping is used to save space, thus NNNNF2F5 defines a mirrored patch.
// Some sprites will only have one picture used for all views: NNNNF0
//
typedef struct
{
	// If false use 0 for any position.
	// Note: as eight entries are available, we might as well insert the same
	//  name eight times.
	byte rotate;

	// Lump to use for view angles 0-7.
	lumpnum_t lumppat[8]; // lump number 16 : 16 wad : lump
	size_t lumpid[8]; // id in the spriteoffset, spritewidth, etc. tables

	// Flip bit (1 = flip) to use for view angles 0-7.
	byte flip[8];
} spriteframe_t;

//
// A sprite definition:  a number of animation frames.
//
typedef struct
{
	size_t numframes;
	spriteframe_t *spriteframes;
} spritedef_t;

#endif
