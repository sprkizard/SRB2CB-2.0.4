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
/// \brief BSP traversal, handling of LineSegs for rendering

#include "doomdef.h"
#include "g_game.h"
#include "r_local.h"
#include "r_state.h"

#include "r_splats.h"
#include "p_local.h" // camera
#include "z_zone.h" // Check R_Prep3DFloors

seg_t *curline;
side_t *sidedef;
line_t *linedef;
sector_t *frontsector;
sector_t *backsector;

// very ugly realloc() of drawsegs at run-time, I upped it to 512
// instead of 256.. and someone managed to send me a level with
// 896 drawsegs! So too bad here's a limit removal a-la-Boom
drawseg_t *drawsegs = NULL;
drawseg_t *ds_p = NULL;
drawseg_t *firstnewseg = NULL;

// indicates doors closed wrt automap bugfix:
boolean doorclosed;

//
// R_ClearDrawSegs
//
void R_ClearDrawSegs(void)
{
	ds_p = drawsegs;
}

// newend is one past the last valid seg
static cliprange_t *newend;
static cliprange_t solidsegs[MAXSEGS];

//
// R_ClipSolidWallSegment
// Does handle solid walls,
//  e.g. single sided LineDefs (middle texture)
//  that entirely block the view.
//
static void R_ClipSolidWallSegment(int first, int last)
{
	cliprange_t *next;
	cliprange_t *start;

	// Find the first range that touches the range (adjacent pixels are touching).
	start = solidsegs;
	while (start->last < first - 1)
		start++;

	if (first < start->first)
	{
		if (last < start->first - 1)
		{
			// Post is entirely visible (above start), so insert a new clippost.
			R_StoreWallRange(first, last);
			next = newend;
			newend++;
			// NO MORE CRASHING!
			if (newend - solidsegs > MAXSEGS)
				I_Error("R_ClipSolidWallSegment: Solid Segs overflow!\n");

			while (next != start)
			{
				*next = *(next-1);
				next--;
			}
			next->first = first;
			next->last = last;
			return;
		}

		// There is a fragment above *start.
		R_StoreWallRange(first, start->first - 1);
		// Now adjust the clip size.
		start->first = first;
	}

	// Bottom contained in start?
	if (last <= start->last)
		return;

	next = start;
	while (last >= (next+1)->first - 1)
	{
		// There is a fragment between two posts.
		R_StoreWallRange(next->last + 1, (next+1)->first - 1);
		next++;

		if (last <= next->last)
		{
			// Bottom is contained in next.
			// Adjust the clip size.
			start->last = next->last;
			goto crunch;
		}
	}

	// There is a fragment after *next.
	R_StoreWallRange(next->last + 1, last);
	// Adjust the clip size.
	start->last = last;

	// Remove start+1 to next from the clip list, because start now covers their area.
crunch:
	if (next == start)
		return; // Post just extended past the bottom of one post.

	while (next++ != newend)
		*++start = *next; // Remove a post.

	newend = start + 1;

	// NO MORE CRASHING!
	if (newend - solidsegs > MAXSEGS)
		I_Error("R_ClipSolidWallSegment: Solid Segs overflow!\n");
}

//
// R_ClipPassWallSegment
// Clips the given range of columns, but does not include it in the clip list.
// Does handle windows, e.g. LineDefs with upper and lower texture.
//
static inline void R_ClipPassWallSegment(int first, int last)
{
	cliprange_t *start;

	// Find the first range that touches the range
	//  (adjacent pixels are touching).
	start = solidsegs;
	while (start->last < first - 1)
		start++;

	if (first < start->first)
	{
		if (last < start->first - 1)
		{
			// Post is entirely visible (above start).
			R_StoreWallRange(first, last);
			return;
		}

		// There is a fragment above *start.
		R_StoreWallRange(first, start->first - 1);
	}

	// Bottom contained in start?
	if (last <= start->last)
		return;

	while (last >= (start+1)->first - 1)
	{
		// There is a fragment between two posts.
		R_StoreWallRange(start->last + 1, (start+1)->first - 1);
		start++;

		if (last <= start->last)
			return;
	}

	// There is a fragment after *next.
	R_StoreWallRange(start->last + 1, last);
}

//
// R_ClearClipSegs
//
void R_ClearClipSegs(void)
{
	solidsegs[0].first = -0x7fffffff;
	solidsegs[0].last = -1;
	solidsegs[1].first = viewwidth;
	solidsegs[1].last = 0x7fffffff;
	newend = solidsegs + 2;
}


// R_DoorClosed
//
// This function is used to fix the automap bug which
// showed lines behind closed doors simply because the door had a dropoff.
//
// It assumes that the game has already ruled out a door being closed because
// of front-back closure (e.g. front floor is taller than back ceiling).
static boolean R_DoorClosed(void)
{
	return

	// if door is closed because back is shut:
	backsector->ceilingheight <= backsector->floorheight

	// preserve a kind of transparent door/lift special effect:
	&& (backsector->ceilingheight >= frontsector->ceilingheight || curline->sidedef->toptexture)

	&& (backsector->floorheight <= frontsector->floorheight || curline->sidedef->bottomtexture)

	// properly render skies (consider door "open" if both ceilings are sky):
	&& (backsector->ceilingpic != skyflatnum || frontsector->ceilingpic != skyflatnum);
}

//
// If player's view height is underneath fake floor, lower the
// drawn ceiling to be just under the floor height, and replace
// the drawn floor and ceiling textures, and light level, with
// the control sector's.
//
// Similar for ceiling, only reflected.
//
sector_t *R_FakeFlat(sector_t *sec, sector_t *tempsec, int *floorlightlevel,
	int *ceilinglightlevel, boolean back)
{
	int mapnum = -1;
	mobj_t *viewmobj = viewplayer->mo;

	if (!viewplayer)
		I_Error("R_FakeFlat: viewplayer == NULL");

	if (!viewmobj)
		return sec;

	if (floorlightlevel)
		*floorlightlevel = sec->floorlightsec == -1 ?
			sec->lightlevel : sectors[sec->floorlightsec].lightlevel;

	if (ceilinglightlevel)
		*ceilinglightlevel = sec->ceilinglightsec == -1 ?
			sec->lightlevel : sectors[sec->ceilinglightsec].lightlevel;

	// If the sector has a midmap, it's probably from 280 type
	if (sec->midmap != -1)
		mapnum = sec->midmap;
	else if (sec->heightsec != -1)
	{
		const sector_t *s = &sectors[sec->heightsec];
		long heightsec = R_PointInSubsector(viewmobj->x, viewmobj->y)->sector->heightsec;
		int underwater = heightsec != -1 && viewz <= sectors[heightsec].floorheight;

		if ((splitscreen && rendersplit) && viewplayer == &players[secondarydisplayplayer]
			&& camera2.chase)
		{
			heightsec = R_PointInSubsector(camera2.x, camera2.y)->sector->heightsec;
		}
		else if (camera.chase && viewplayer == &players[displayplayer])
			heightsec = R_PointInSubsector(camera.x, camera.y)->sector->heightsec;

		// Replace sector being drawn, with a copy to be hacked
		*tempsec = *sec;

		// Replace floor and ceiling height with other sector's heights.
		tempsec->floorheight = s->floorheight;
		tempsec->ceilingheight = s->ceilingheight;

		mapnum = s->midmap;

		if ((underwater && (tempsec->  floorheight = sec->floorheight,
			tempsec->ceilingheight = s->floorheight - 1, !back)) || viewz <= s->floorheight)
		{ // head-below-floor hack
			tempsec->floorpic = s->floorpic;
			tempsec->floor_xoffs = s->floor_xoffs;
			tempsec->floor_yoffs = s->floor_yoffs;
			tempsec->floorpic_angle = s->floorpic_angle;
			tempsec->floor_scale = s->floor_scale;

			if (underwater)
			{
				if (s->ceilingpic == skyflatnum)
				{
					tempsec->floorheight = tempsec->ceilingheight+1;
					tempsec->ceilingpic = tempsec->floorpic;
					tempsec->ceiling_xoffs = tempsec->floor_xoffs;
					tempsec->ceiling_yoffs = tempsec->floor_yoffs;
					tempsec->ceilingpic_angle = tempsec->floorpic_angle;
					tempsec->ceiling_scale = tempsec->floor_scale;
				}
				else
				{
					tempsec->ceilingpic = s->ceilingpic;
					tempsec->ceiling_xoffs = s->ceiling_xoffs;
					tempsec->ceiling_yoffs = s->ceiling_yoffs;
					tempsec->ceilingpic_angle = s->ceilingpic_angle;
					tempsec->ceiling_scale = s->ceiling_scale;
				}
				mapnum = s->bottommap;
			}

			tempsec->lightlevel = s->lightlevel;

			if (floorlightlevel)
				*floorlightlevel = s->floorlightsec == -1 ? s->lightlevel
					: sectors[s->floorlightsec].lightlevel;

			if (ceilinglightlevel)
				*ceilinglightlevel = s->ceilinglightsec == -1 ? s->lightlevel
					: sectors[s->ceilinglightsec].lightlevel;
		}
		else if (heightsec != -1 && viewz >= sectors[heightsec].ceilingheight
			&& sec->ceilingheight > s->ceilingheight)
		{ // Above-ceiling hack
			tempsec->ceilingheight = s->ceilingheight;
			tempsec->floorheight = s->ceilingheight + 1;

			tempsec->floorpic = tempsec->ceilingpic = s->ceilingpic;
			tempsec->floor_xoffs = tempsec->ceiling_xoffs = s->ceiling_xoffs;
			tempsec->floor_yoffs = tempsec->ceiling_yoffs = s->ceiling_yoffs;
			tempsec->floorpic_angle = tempsec->ceilingpic_angle = s->ceilingpic_angle;
			tempsec->floor_scale = tempsec->ceiling_scale = s->ceiling_scale;

			mapnum = s->topmap;

			if (s->floorpic == skyflatnum) // SKYFIX?
			{
				tempsec->ceilingheight = tempsec->floorheight-1;
				tempsec->floorpic = tempsec->ceilingpic;
				tempsec->floor_xoffs = tempsec->ceiling_xoffs;
				tempsec->floor_yoffs = tempsec->ceiling_yoffs;
				tempsec->floorpic_angle = tempsec->ceilingpic_angle;
				tempsec->floor_scale = tempsec->ceiling_scale;
			}
			else
			{
				tempsec->ceilingheight = sec->ceilingheight;
				tempsec->floorpic = s->floorpic;
				tempsec->floor_xoffs = s->floor_xoffs;
				tempsec->floor_yoffs = s->floor_yoffs;
				tempsec->floorpic_angle = s->floorpic_angle;
				tempsec->floor_scale = s->floor_scale;
			}

			tempsec->lightlevel = s->lightlevel;

			if (floorlightlevel)
				*floorlightlevel = s->floorlightsec == -1 ? s->lightlevel :
			sectors[s->floorlightsec].lightlevel;

			if (ceilinglightlevel)
				*ceilinglightlevel = s->ceilinglightsec == -1 ? s->lightlevel :
			sectors[s->ceilinglightsec].lightlevel;
		}
		sec = tempsec;
	}

	if (mapnum >= 0 && (size_t)mapnum < num_extra_colormaps)
		sec->extra_colormap = &extra_colormaps[mapnum];
	else
		sec->extra_colormap = NULL;

	return sec;
}

//
// R_AddLine
// Clips the given segment and adds any visible pieces to the line list.
//
static void R_AddLine(seg_t *line)
{
	int x1, x2;
	angle_t angle1, angle2;
	static sector_t tempsec; // ceiling/water hack

#ifdef POLYOBJECTS
	if (line->polyseg && !(line->polyseg->flags & POF_RENDERSIDES))
		return;
#endif

	curline = line;

	// OPTIMIZE: quickly reject orthogonal back sides.
	angle1 = R_PointToAngle(line->v1->x, line->v1->y);
	angle2 = R_PointToAngle(line->v2->x, line->v2->y);

	// Back side? i.e. backface culling?
	if (angle1 - angle2 >= ANG180)
		return;

	// Global angle needed by segcalc.
	rw_angle1 = angle1;
	angle1 -= viewangle;
	angle2 -= viewangle;

	// cph - replaced old code, which was unclear and badly commented
	// Much more efficient code now
	if ((signed)angle1 < (signed)angle2) { /* it's "behind" us */
		/* Either angle1 or angle2 is behind us, so it doesn't matter if we
		 * change it to the corect sign
		 */
		if ((angle1 >= ANG180) && (angle1 < ANG270))
			angle1 = INT_MAX; /* which is ANG180-1 */
		else
			angle2 = INT_MIN;
	}

	if ((signed)angle2 >= (signed)clipangle) return; // Both off left edge
	if ((signed)angle1 <= -(signed)clipangle) return; // Both off right edge
	if ((signed)angle1 >= (signed)clipangle) angle1 = clipangle; // Clip at left edge
	if ((signed)angle2 <= -(signed)clipangle) angle2 = 0-clipangle; // Clip at right edge

	// Find the first clippost
	//  that touches the source post
	//  (adjacent pixels are touching).
	angle1 = (angle1+ANG90)>>ANGLETOFINESHIFT;
	angle2 = (angle2+ANG90)>>ANGLETOFINESHIFT;
	x1 = viewangletox[angle1];
	x2 = viewangletox[angle2];

	// Does not cross a pixel?
	if (x1 >= x2)       // killough 1/31/98 -- change == to >= for robustness
		return;

	backsector = line->backsector;

	// Single sided line?
	if (!backsector)
		goto clipsolid;

	backsector = R_FakeFlat(backsector, &tempsec, NULL, NULL, true);

	doorclosed = false;

	// Closed door.
	if (backsector->ceilingheight <= frontsector->floorheight
		|| backsector->floorheight >= frontsector->ceilingheight)
	{
		goto clipsolid;
	}

	// Check for automap fix. Store in doorclosed for r_segs.c
	doorclosed = R_DoorClosed();
	if (doorclosed)
		goto clipsolid;

	// Window.
	if (backsector->ceilingheight != frontsector->ceilingheight
		|| backsector->floorheight != frontsector->floorheight)
	{
		goto clippass;
	}

	// Reject empty lines used for triggers and special events.
	// Identical floor and ceiling on both sides, identical light levels on both sides,
	// and no middle texture.
	if (line->linedef->flags & ML_EFFECT6) // Don't even draw these lines
		return;

	if (
#ifdef POLYOBJECTS
		!line->polyseg &&
#endif
		backsector->ceilingpic == frontsector->ceilingpic
		&& backsector->floorpic == frontsector->floorpic
		&& backsector->lightlevel == frontsector->lightlevel
		&& !curline->sidedef->midtexture
		// Check offsets too!
		&& backsector->floor_xoffs == frontsector->floor_xoffs
		&& backsector->floor_yoffs == frontsector->floor_yoffs
		&& backsector->floorpic_angle == frontsector->floorpic_angle
		&& backsector->floor_scale == frontsector->floor_scale
		&& backsector->ceiling_xoffs == frontsector->ceiling_xoffs
		&& backsector->ceiling_yoffs == frontsector->ceiling_yoffs
		&& backsector->ceilingpic_angle == frontsector->ceilingpic_angle
		&& backsector->ceiling_scale == frontsector->ceiling_scale
		// Consider altered lighting.
		&& backsector->floorlightsec == frontsector->floorlightsec
		&& backsector->ceilinglightsec == frontsector->ceilinglightsec
		// Consider colormaps
		&& backsector->extra_colormap == frontsector->extra_colormap
#ifdef ESLOPE
		// Consider slopes
		&& backsector->f_slope == frontsector->f_slope
		&& backsector->c_slope == frontsector->c_slope
#endif
		&& ((!frontsector->ffloors && !backsector->ffloors)
			|| frontsector->tag == backsector->tag))
	{
		return;
	}


clippass:
	R_ClipPassWallSegment(x1, x2 - 1);
	return;

clipsolid:
	R_ClipSolidWallSegment(x1, x2 - 1);
}

//
// R_CheckBBox
// Checks BSP node/subtree bounding box.
// Returns true if some part of the bbox might be visible.
//
//   | 0 | 1 | 2
// --+---+---+---
// 0 | 0 | 1 | 2
// 1 | 4 | 5 | 6
// 2 | 8 | 9 | A
int checkcoord[12][4] =
{
	{3, 0, 2, 1},
	{3, 0, 2, 0},
	{3, 1, 2, 0},
	{0}, // UNUSED
	{2, 0, 2, 1},
	{0}, // UNUSED
	{3, 1, 3, 0},
	{0}, // UNUSED
	{2, 0, 3, 1},
	{2, 1, 3, 1},
	{2, 1, 3, 0}
};

static boolean R_CheckBBox(fixed_t *bspcoord)
{
	int boxpos;
	angle_t angle1, angle2;
	cliprange_t *start;
	const int* check;

	// Find the corners of the box
	// that define the edges from current viewpoint.
	boxpos = (viewx <= bspcoord[BOXLEFT] ? 0 : viewx < bspcoord[BOXRIGHT ] ? 1 : 2) +
    (viewy >= bspcoord[BOXTOP ] ? 0 : viewy > bspcoord[BOXBOTTOM] ? 4 : 8);

	if (boxpos == 5)
		return true;

    check = checkcoord[boxpos];

	angle1 = R_PointToAngle(bspcoord[check[0]], bspcoord[check[1]]) - viewangle;
	angle2 = R_PointToAngle(bspcoord[check[2]], bspcoord[check[3]]) - viewangle;

	// cph - replaced old code, which was unclear and badly commented
	// Much more efficient code now
	if ((signed)angle1 < (signed)angle2) { /* it's "behind" us */
		/* Either angle1 or angle2 is behind us, so it doesn't matter if we
		 * change it to the corect sign
		 */
		if ((angle1 >= ANG180) && (angle1 < ANG270))
			angle1 = INT_MAX; /* which is ANG180-1 */
		else
			angle2 = INT_MIN;
	}

	if ((signed)angle2 >= (signed)clipangle) return false; // Both off left edge
	if ((signed)angle1 <= -(signed)clipangle) return false; // Both off right edge
	if ((signed)angle1 >= (signed)clipangle) angle1 = clipangle; // Clip at left edge
	if ((signed)angle2 <= -(signed)clipangle) angle2 = 0-clipangle; // Clip at right edge

	// Find the first clippost
	//  that touches the source post
	//  (adjacent pixels are touching).
	angle1 = (angle1+ANG90)>>ANGLETOFINESHIFT;
	angle2 = (angle2+ANG90)>>ANGLETOFINESHIFT;
	{
		int sx1 = viewangletox[angle1];
		int sx2 = viewangletox[angle2];
		//    const cliprange_t *start;

		// Does not cross a pixel.
		if (sx1 == sx2)
			return false;

		//if (!memchr(solidcol+sx1, 0, sx2-sx1)) return false; // SRB2CBTODO: Even more optimized
		sx2--;

		start = solidsegs;
		while (start->last < sx2)
			start++;

		if (sx1 >= start->first && sx2 <= start->last)
			return false; // The clippost contains the new span.
		// All columns it covers are already solidly covered
	}

	return true;
}

#ifdef POLYOBJECTS

size_t numpolys;        // number of polyobjects in current subsector
size_t num_po_ptrs;     // number of polyobject pointers allocated
polyobj_t **po_ptrs; // temp ptr array to sort polyobject pointers

//
// R_PolyobjCompare
//
// Callback for qsort that compares the z distance of two polyobjects.
// Returns the difference such that the closer polyobject will be
// sorted first.
//
static int R_PolyobjCompare(const void *p1, const void *p2)
{
	const polyobj_t *po1 = *(const polyobj_t * const *)p1;
	const polyobj_t *po2 = *(const polyobj_t * const *)p2;

	return po1->zdist - po2->zdist;
}

//
// R_SortPolyObjects
//
// haleyjd 03/03/06: Here's the REAL meat of Eternity's polyobject system.
// Hexen just figured this was impossible, but as mentioned in polyobj.c,
// it is perfectly doable within the confines of the BSP tree. Polyobjects
// must be sorted to draw in the game's front-to-back order within individual
// subsectors. This is a modified version of R_SortVisSprites.
//
void R_SortPolyObjects(subsector_t *sub)
{
	if (numpolys)
	{
		polyobj_t *po;
		int i = 0;

		// allocate twice the number needed to minimize allocations
		if (num_po_ptrs < numpolys*2)
		{
			// Use free instead realloc since faster (thanks Lee ^_^)
			free(po_ptrs);
			po_ptrs = malloc((num_po_ptrs = numpolys*2)
				* sizeof(*po_ptrs));
		}

		po = sub->polyList;

		while (po)
		{
			po->zdist = R_PointToDist2(viewx, viewy,
				po->centerPt.x, po->centerPt.y);
			po_ptrs[i++] = po;
			po = (polyobj_t *)(po->link.next);
		}

		// the polyobjects are NOT in any particular order, so use qsort
		// 03/10/06: only bother if there are actually polys to sort
		if (numpolys >= 2)
		{
			qsort(po_ptrs, numpolys, sizeof(polyobj_t *),
				R_PolyobjCompare);
		}
	}
}

//
// R_AddPolyObjects
//
// haleyjd 02/19/06
// Adds all segs in all polyobjects in the given subsector.
//
static void R_AddPolyObjects(subsector_t *sub)
{
	polyobj_t *po = sub->polyList;
	size_t i, j;

	numpolys = 0;

	// count polyobjects
	while (po)
	{
		++numpolys;
		po = (polyobj_t *)(po->link.next);
	}

	// sort polyobjects
	R_SortPolyObjects(sub);

	// render polyobjects
	for (i = 0; i < numpolys; ++i)
	{
		for (j = 0; j < po_ptrs[i]->segCount; ++j)
			R_AddLine(po_ptrs[i]->segs[j]);
	}
}
#endif

//
// R_Subsector
// Determine floor/ceiling planes.
// Add sprites of things in sector.
// Draw one or more line segments.
//

drawseg_t *firstseg;

static void R_Subsector(size_t num)
{
	int count, floorlightlevel, ceilinglightlevel, light;
	seg_t *line;
	subsector_t *sub;
	static sector_t tempsec; // Deep water hack
	extracolormap_t *floorcolormap;
	extracolormap_t *ceilingcolormap;
#ifdef ESLOPE
	v3float_t   cam;
#endif

#ifdef RANGECHECK
	if (num >= numsubsectors)
		I_Error("R_Subsector: ss %u with numss = %u", (int)num, (int)numsubsectors);
#endif

	// subsectors added at run-time
	if (num >= numsubsectors)
		return;

	sub = &subsectors[num];
	frontsector = sub->sector;
	count = sub->numlines;
	line = &segs[sub->firstline];

	// Deep water/fake ceiling effect.
	frontsector = R_FakeFlat(frontsector, &tempsec, &floorlightlevel, &ceilinglightlevel, false);

	floorcolormap = ceilingcolormap = frontsector->extra_colormap;

	// Check and prep all 3D floors. Set the sector floor/ceiling light levels and colormaps.
	if (frontsector->ffloors)
	{
		if (frontsector->moved)
		{
			frontsector->numlights = sub->sector->numlights = 0;
			R_Prep3DFloors(frontsector);
			sub->sector->lightlist = frontsector->lightlist;
			sub->sector->numlights = frontsector->numlights;
			sub->sector->moved = frontsector->moved = false;
		}

		light = R_GetPlaneLight(frontsector, frontsector->floorheight, false);
		if (frontsector->floorlightsec == -1)
			floorlightlevel = *frontsector->lightlist[light].lightlevel;
		floorcolormap = frontsector->lightlist[light].extra_colormap;
		light = R_GetPlaneLight(frontsector, frontsector->ceilingheight, false);
		if (frontsector->ceilinglightsec == -1)
			ceilinglightlevel = *frontsector->lightlist[light].lightlevel;
		ceilingcolormap = frontsector->lightlist[light].extra_colormap;
	}

	sub->sector->extra_colormap = frontsector->extra_colormap;

#ifdef ESLOPE
	// SoM: Slopes!
	cam.x = FIXED_TO_FLOAT(viewx);
	cam.y = FIXED_TO_FLOAT(viewy);
	cam.z = FIXED_TO_FLOAT(viewz);


	if ((frontsector->f_slope &&  P_DistFromPlanef(&cam, &frontsector->f_slope->of, &frontsector->f_slope->normalf) > 0.0f)
		|| (!frontsector->f_slope && (frontsector->floorheight < viewz || (frontsector->heightsec != -1 && sectors[frontsector->heightsec].ceilingpic == skyflatnum))))
#else
	if ((frontsector->floorheight < viewz || (frontsector->heightsec != -1
		&& sectors[frontsector->heightsec].ceilingpic == skyflatnum)))
#endif
	{
		floorplane = R_FindPlane(frontsector->floorheight, frontsector->floorpic, floorlightlevel,
			frontsector->floor_xoffs, frontsector->floor_yoffs, frontsector->floorpic_angle, floorcolormap, NULL
#ifdef SESLOPE
								 ,frontsector->f_slope
#endif
										);
	}
	else
		floorplane = NULL;

#ifdef ESLOPE
	if ((frontsector->c_slope &&  P_DistFromPlanef(&cam, &frontsector->c_slope->of,
												   &frontsector->c_slope->normalf) > 0.0f)
		|| (!frontsector->c_slope && (frontsector->ceilingheight > viewz || frontsector->ceilingpic == skyflatnum || (frontsector->heightsec != -1 && sectors[frontsector->heightsec].floorpic == skyflatnum))))
#else
	if ((frontsector->ceilingheight > viewz || frontsector->ceilingpic == skyflatnum
		|| (frontsector->heightsec != -1
		&& sectors[frontsector->heightsec].floorpic == skyflatnum)))
#endif
	{
		ceilingplane = R_FindPlane(frontsector->ceilingheight, frontsector->ceilingpic,
			ceilinglightlevel, frontsector->ceiling_xoffs, frontsector->ceiling_yoffs, frontsector->ceilingpic_angle,
			ceilingcolormap, NULL
#ifdef SESLOPE
								   ,frontsector->c_slope
#endif
		);
	}
	else
		ceilingplane = NULL;

	numffloors = 0;
	ffloor[numffloors].plane = NULL;
	if (frontsector->ffloors)
	{
		ffloor_t *rover;

		for (rover = frontsector->ffloors; rover && numffloors < MAXFFLOORS; rover = rover->next)
		{
			if (!(rover->flags & FF_EXISTS) || !(rover->flags & FF_RENDERPLANES))
				continue;

			if (frontsector->cullheight)
			{
				if (frontsector->cullheight->flags & ML_NOCLIMB) // Group culling
				{
					// Make sure this is part of the same group
					if (viewsector->cullheight && viewsector->cullheight->frontsector
						== frontsector->cullheight->frontsector)
					{
						// OK, we can cull
						if (viewz > frontsector->cullheight->frontsector->floorheight
							&& *rover->topheight < frontsector->cullheight->frontsector->floorheight) // Cull if below plane
						{
							rover->norender = leveltime;
							continue;
						}

						if (*rover->bottomheight > frontsector->cullheight->frontsector->floorheight
							&& viewz <= frontsector->cullheight->frontsector->floorheight) // Cull if above plane
						{
							rover->norender = leveltime;
							continue;
						}
					}
				}
				else // Quick culling
				{
					if (viewz > frontsector->cullheight->frontsector->floorheight
						&& *rover->topheight < frontsector->cullheight->frontsector->floorheight) // Cull if below plane
					{
						rover->norender = leveltime;
						continue;
					}

					if (*rover->bottomheight > frontsector->cullheight->frontsector->floorheight
						&& viewz <= frontsector->cullheight->frontsector->floorheight) // Cull if above plane
					{
						rover->norender = leveltime;
						continue;
					}
				}
			}

			ffloor[numffloors].plane = NULL;
			if (*rover->bottomheight <= frontsector->ceilingheight
				&& *rover->bottomheight >= frontsector->floorheight
				&& ((viewz < *rover->bottomheight && !(rover->flags & FF_INVERTPLANES))
				|| (viewz > *rover->bottomheight && (rover->flags & FF_BOTHPLANES))))
			{
				light = R_GetPlaneLight(frontsector, *rover->bottomheight,
					viewz < *rover->bottomheight);
				ffloor[numffloors].plane = R_FindPlane(*rover->bottomheight, *rover->bottompic,
					*frontsector->lightlist[light].lightlevel, *rover->bottomxoffs,
					*rover->bottomyoffs, *rover->bottomangle, frontsector->lightlist[light].extra_colormap, rover
#ifdef SESLOPE
													   ,rover->master->frontsector->f_slope
#endif
														); // SRB2CBTODO: ESLOPE FOFS!(But not for software)

				ffloor[numffloors].height = *rover->bottomheight;
				ffloor[numffloors].ffloor = rover;
				numffloors++;
			}
			if (numffloors >= MAXFFLOORS)
				break;
			ffloor[numffloors].plane = NULL;
			if (*rover->topheight >= frontsector->floorheight
				&& *rover->topheight <= frontsector->ceilingheight
				&& ((viewz > *rover->topheight && !(rover->flags & FF_INVERTPLANES))
				|| (viewz < *rover->topheight && (rover->flags & FF_BOTHPLANES))))
			{
				light = R_GetPlaneLight(frontsector, *rover->topheight, viewz < *rover->topheight);
				ffloor[numffloors].plane = R_FindPlane(*rover->topheight, *rover->toppic,
					*frontsector->lightlist[light].lightlevel, *rover->topxoffs, *rover->topyoffs, *rover->topangle,
					frontsector->lightlist[light].extra_colormap, rover
#ifdef SESLOPE
													   ,rover->master->frontsector->f_slope
#endif
				); // ESLOPE: Gotta make special cases for FOF slopes
				ffloor[numffloors].height = *rover->topheight;
				ffloor[numffloors].ffloor = rover;
				numffloors++;
			}
		}
	}

#ifdef POLYOBJECTS_PLANESS // Software
	// Polyobjects have planes, too!
	if (sub->polyList)
	{
		polyobj_t *po = sub->polyList;
		sector_t *polysec;

		while (po)
		{
			if (numffloors >= MAXFFLOORS)
				break;

			ffloor_t *ffloorx;

			// Add the floor
			ffloorx = Z_Calloc(sizeof (*ffloorx), PU_LEVEL, NULL);
			ffloorx->secnum = polysec - sectors;
			ffloorx->target = polysec;
			ffloorx->bottomheight = &polysec->floorheight;
			ffloorx->bottompic = &polysec->floorpic;
			ffloorx->bottomxoffs = &polysec->floor_xoffs;
			ffloorx->bottomyoffs = &polysec->floor_yoffs;
			ffloorx->bottomangle = &polysec->floorpic_angle;
			ffloorx->bottomscale = &polysec->floor_scale;

			// Add the ceiling
			ffloorx->topheight = &polysec->ceilingheight;
			ffloorx->toppic = &polysec->ceilingpic;
			ffloorx->toplightlevel = &polysec->lightlevel;
			ffloorx->topxoffs = &polysec->ceiling_xoffs;
			ffloorx->topyoffs = &polysec->ceiling_yoffs;
			ffloorx->topangle = &polysec->ceilingpic_angle;
			ffloorx->topscale = &polysec->ceiling_scale;

#if 1 // Polyobjects in game are all non renderplane, remove this #if when done coding
			if (!((po_ptrs[i]->flags & POF_RENDERTOP) || (po_ptrs[i]->flags & POF_RENDERBOTTOM)))
			{
				po = (polyobj_t *)(po->link.next);
				continue;
			}
#endif

			polysec = po->lines[0]->backsector;
			ffloor[numffloors].plane = NULL;

			if (polysec->floorheight <= frontsector->ceilingheight
				&& polysec->floorheight >= frontsector->floorheight
				&& (viewz < polysec->floorheight))
			{
				light = R_GetPlaneLight(frontsector, polysec->floorheight, viewz < polysec->floorheight);
				light = 0;
				ffloor[numffloors].plane = R_FindPlane(polysec->floorheight, polysec->floorpic,
						polysec->lightlevel, polysec->floor_xoffs,
						polysec->floor_yoffs,
						polysec->floorpic_angle,
						NULL,
						NULL, polysec->f_slope);
				ffloor[numffloors].plane->polyobj = true;

				ffloor[numffloors].height = polysec->floorheight;
				ffloor[numffloors].polyobj = po;
				ffloor[numffloors].ffloor = ffloorx;
				numffloors++;
			}

			if (numffloors >= MAXFFLOORS)
				break;

			ffloor[numffloors].plane = NULL;

			if (polysec->ceilingheight >= frontsector->floorheight
				&& polysec->ceilingheight <= frontsector->ceilingheight
				&& (viewz > polysec->ceilingheight))
			{
				light = R_GetPlaneLight(frontsector, polysec->ceilingheight, viewz < polysec->ceilingheight);
				light = 0;
				ffloor[numffloors].plane = R_FindPlane(polysec->ceilingheight, polysec->ceilingpic,
					polysec->lightlevel, polysec->ceiling_xoffs, polysec->ceiling_yoffs, polysec->ceilingpic_angle,
					NULL, NULL, polysec->f_slope);
				ffloor[numffloors].plane->polyobj = true;
				ffloor[numffloors].polyobj = po;
				ffloor[numffloors].height = polysec->ceilingheight;
				ffloor[numffloors].ffloor = ffloorx;
				numffloors++;
			}

			po = (polyobj_t *)(po->link.next);
		}
	}
#endif

#ifdef FLOORSPLATS
	if (sub->splats)
		R_AddVisibleFloorSplats(sub);
#endif

	R_AddSprites(sub->sector, (floorlightlevel+ceilinglightlevel)/2);

	firstseg = NULL;

#ifdef POLYOBJECTS
	// haleyjd 02/19/06: draw polyobjects before static lines
	if (sub->polyList)
		R_AddPolyObjects(sub);
#endif

	while (count--)
	{
//		CONS_Printf("Adding normal line %d...(%d)\n", line->linedef-lines, leveltime);
		R_AddLine(line);
		line++;
		curline = NULL; /* cph 2001/11/18 - must clear curline now we're done with it, so stuff doesn't try using it for other things */
	}
}

//
// R_Prep3DFloors
//
// This function creates the lightlists that the given sector uses to light
// floors/ceilings/walls according to the 3D floors.
void R_Prep3DFloors(sector_t *sector)
{
	ffloor_t *rover;
	ffloor_t *best;
	fixed_t bestheight, maxheight;
	int count, i, mapnum;
	pslope_t *bestslope = NULL;
	sector_t *sec;

	count = 1;
	for (rover = sector->ffloors; rover; rover = rover->next)
	{
		if ((rover->flags & FF_EXISTS) && (!(rover->flags & FF_NOSHADE)
			|| (rover->flags & FF_CUTLEVEL) || (rover->flags & FF_CUTSPRITES)))
		{
			count++;
			if (rover->flags & FF_DOUBLESHADOW)
				count++;
		}
	}

	if (count != sector->numlights)
	{
		Z_Free(sector->lightlist);
		sector->lightlist = Z_Calloc(sizeof (*sector->lightlist) * count, PU_LEVEL, NULL);
		sector->numlights = count;
	}
	else
		memset(sector->lightlist, 0, sizeof (lightlist_t) * count);

	sector->lightlist[0].height = sector->ceilingheight + 1;
	if (sector->f_slope)
		sector->lightlist[0].heightslope = sector->c_slope;
	else
		sector->lightlist[0].heightslope = NULL;
	sector->lightlist[0].lightlevel = &sector->lightlevel;
	sector->lightlist[0].caster = NULL;
	sector->lightlist[0].extra_colormap = sector->extra_colormap;
	sector->lightlist[0].flags = 0;

	maxheight = MAXINT;
	for (i = 1; i < count; i++)
	{
		bestheight = MAXINT * -1;
		best = NULL;
		for (rover = sector->ffloors; rover; rover = rover->next)
		{
			rover->lastlight = 0;
			if (!(rover->flags & FF_EXISTS) || (rover->flags & FF_NOSHADE
				&& !(rover->flags & FF_CUTLEVEL) && !(rover->flags & FF_CUTSPRITES)))
				continue;

			if (*rover->topheight > bestheight && *rover->topheight < maxheight)
			{
				best = rover;
				bestheight = *rover->topheight;
				bestslope = rover->master->frontsector->c_slope;
				continue;
			}
			if (rover->flags & FF_DOUBLESHADOW && *rover->bottomheight > bestheight
				&& *rover->bottomheight < maxheight)
			{
				best = rover;
				bestheight = *rover->bottomheight;
				bestslope = rover->master->frontsector->f_slope;
				continue;
			}
		}
		if (!best)
		{
			sector->numlights = i;
			return;
		}

		sector->lightlist[i].height = maxheight = bestheight;
		sector->lightlist[i].heightslope = bestslope;
		sector->lightlist[i].caster = best;
		sector->lightlist[i].flags = best->flags;

		sec = &sectors[best->secnum];
		mapnum = sec->midmap;
		if (mapnum >= 0 && (size_t)mapnum < num_extra_colormaps)
			sec->extra_colormap = &extra_colormaps[mapnum];
		else
			sec->extra_colormap = NULL;

		if (best->flags & FF_NOSHADE)
		{
			sector->lightlist[i].lightlevel = sector->lightlist[i-1].lightlevel;
			sector->lightlist[i].extra_colormap = sector->lightlist[i-1].extra_colormap;
		}
		else if (best->flags & FF_COLORMAPONLY)
		{
			sector->lightlist[i].lightlevel = sector->lightlist[i-1].lightlevel;
			sector->lightlist[i].extra_colormap = sec->extra_colormap;
		}
		else
		{
			sector->lightlist[i].lightlevel = best->toplightlevel;
			sector->lightlist[i].extra_colormap = sec->extra_colormap;
		}

		if (best->flags & FF_DOUBLESHADOW)
		{
			if (bestheight == *best->bottomheight)
			{
				sector->lightlist[i].lightlevel = sector->lightlist[best->lastlight].lightlevel;
				sector->lightlist[i].extra_colormap =
					sector->lightlist[best->lastlight].extra_colormap;
			}
			else
				best->lastlight = i - 1;
		}
	}
}

int R_GetPlaneLight(sector_t *sector, fixed_t planeheight, boolean underside)
{
	int i;


	// ESLOPETODO: can't use planeheight anymore, switch whole code to use secplanes instead of planeheight
	//fixed_t lightheight = P_GetzAt(sector->f_slope, sector->soundorg.x, sector->soundorg.y);

	if (!underside)
	{
		for (i = 1; i < sector->numlights; i++)
		{
			if (sector->lightlist[i].height <= planeheight)
				return i - 1;
		}

		return sector->numlights - 1;
	}

	for (i = 1; i < sector->numlights; i++)
		if (sector->lightlist[i].height < planeheight)
			return i - 1;

	return sector->numlights - 1;
}

//
// R_RenderBSPNode
// Renders all subsectors below a given node,
//  traversing subtree recursively.
// Just call with BSP root.
//
void R_RenderBSPNode(int bspnum)
{
	while (!(bspnum & NF_SUBSECTOR))  // Found a subsector?
	{
		node_t *bsp = &nodes[bspnum];

		// Decide which side the view point is on.
		int side = R_PointOnSide(viewx, viewy, bsp);

		// Recursively divide front space.
		R_RenderBSPNode(bsp->children[side]);

		// Possibly divide back space.
		if (!R_CheckBBox(bsp->bbox[side^1]))
			return;

		bspnum = bsp->children[side^1];
	}
	R_Subsector(bspnum == -1 ? 0 : bspnum & ~NF_SUBSECTOR);
}
