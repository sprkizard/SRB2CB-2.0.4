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
/// \brief convert SRB2's map format into a format that's compatible OpenGL rendering
// SRB2CBTODO: THIS is the source of all our OpenGL related BSP issues, FIX!!!!!!!!

#include "../doomdef.h"
#include "../doomstat.h"

#ifdef HWRENDER
#include "hw_glob.h"
#include "../r_local.h"
#include "../z_zone.h"
#include "../console.h"
#include "../v_video.h"
#include "../m_menu.h"
#include "../i_system.h"
#include "../m_argv.h"
#include "../i_video.h"
#include "../w_wad.h"

// --------------------------------------------------------------------------
// This is global data for needed for plane rendering
// --------------------------------------------------------------------------

extrasubsector_t *extrasubsectors = NULL;

typedef struct
{
	float x, y;
	float dx, dy;
} fdivline_t;

// ==========================================================================
//                                    FLOOR & CEILING CONVEX POLYS GENERATION
// ==========================================================================

// --------------------------------------------------------------------------
// Function for fast polygon alloc / free, polygons are freed manualy at the start/end of a level
// --------------------------------------------------------------------------
static poly_t *HWR_AllocPoly(int numpts)
{
	poly_t *p;
	size_t polysize = sizeof (poly_t) + sizeof (polyvertex_t) * numpts;
	p = malloc(polysize);
	p->numpts = numpts;
	return p;
}

// Return interception along bsp line,
// with the polygon segment
//
static float bspfrac;
static polyvertex_t *GLBSP_FracDivline(fdivline_t *bsp, polyvertex_t *v1,
	polyvertex_t *v2)
{
	static polyvertex_t pt;
	// Double precision is absoulutely REQUIRED here
	double frac;
	double num;
	double den;
	double v1x,v1y,v1dx,v1dy;
	double v2x,v2y,v2dx,v2dy;

	// a segment of a polygon
	v1x  = v1->x;
	v1y  = v1->y;
	v1dx = v2->x - v1->x;
	v1dy = v2->y - v1->y;

	// the bsp partition line
	v2x  = bsp->x;
	v2y  = bsp->y;
	v2dx = bsp->dx;
	v2dy = bsp->dy;

	den = v2dy*v1dx - v2dx*v1dy;
	if (den == 0)
		return NULL;       // parallel

	// first check the frac along the polygon segment,
	// (do not accept hit with the extensions)
	num = (v2x - v1x)*v2dy + (v1y - v2y)*v2dx;
	frac = num / den;
	if (frac < 0 || frac > 1)
		return NULL;

	// now get the frac along the BSP line
	// which is useful to determine what is left, what is right
	num = (v2x - v1x)*v1dy + (v1y - v2y)*v1dx;
	frac = num / den;
	bspfrac = (float)frac;


	// find the interception point along the partition line
	pt.x = (float)(v2x + v2dx*frac);
	pt.y = (float)(v2y + v2dy*frac);

	return &pt;
}

// If two vertice coords have a x and/or y difference
// less than or equal to 1 FRACUNIT, they are considered the same
// point. Note: hardcoded value, 1.0f could be anything else.
static boolean GLBSP_SameVertice(polyvertex_t *p1, polyvertex_t *p2)
{
	if (p1->x != p2->x)
		return false;
	if (p1->y != p2->y)
		return false;
	// p1 and p2 are considered the same vertex
	return true;
}


// Split a _CONVEX_ polygon in two convex polygons
// outputs:
//   frontpoly : polygon on right side of bsp line
//   backpoly  : polygon on left side
//
static void GLBSP_SplitPoly(fdivline_t *bsp,         // splitting parametric line
					  poly_t *poly,            // the convex poly we split
					  poly_t **frontpoly,      // return one poly here
					  poly_t **backpoly)       // return the other here
{
	size_t i,j;
	polyvertex_t *pv;

	int ps = -1, pe = -1;
	int nptfront, nptback;
	polyvertex_t vs = {0,0,0};
	polyvertex_t ve = {0,0,0};
	polyvertex_t lastpv = {0,0,0};
	// used to tell which poly is on the
	// front side of the bsp partition line
	float fracs = 0.0f, frace = 0.0f;
	byte psonline = 0, peonline = 0; // // SRB2CBTODO: Either 1 or 0 in the code, could be boolean but subtracted in code

	for (i = 0; i < poly->numpts; i++)
	{
		j = i + 1;
		if (j == poly->numpts) j = 0; // SRB2CBTODO: Yes, removing this causes issues, why?

		// start & end points
		pv = GLBSP_FracDivline(bsp, &poly->pts[i], &poly->pts[j]);

		if (pv)
		{
			if (ps < 0)
			{
				// first point
				ps = i;
				vs = *pv;
				fracs = bspfrac;
			}
			else
			{
				// the partition line traverse a junction between two segments
				// or the two points are so close, they can be considered as one
				// thus, don't accept, since split 2 must be another vertex
				if (GLBSP_SameVertice(pv, &lastpv))
				{
					if (pe < 0)
					{
						ps = i;
						psonline = 1;
					}
					else
					{
						pe = i;
						peonline = 1;
					}
				}
				else
				{
					if (pe < 0)
					{
						pe = i;
						ve = *pv;
						frace = bspfrac;
					}
					else
					{
					// a frac, not same vertice as last one
					// we already got pt2 so pt 2 is not on the line,
					// so we probably got back to the start point
					// which is on the line
						if (GLBSP_SameVertice(pv, &vs))
							psonline = 1;
						break;
					}
				}
			}

			// remember last point intercept to detect identical points
			lastpv = *pv;
		}
	}

	// no split: the partition line is either parallel and
	// aligned with one of the poly segments, or the line is totally
	// out of the polygon and doesn't traverse it (happens if the bsp
	// is fooled by some trick where the sidedefs don't point to
	// the right sectors)
	if (ps < 0)
	{
		// this eventually happens with 'broken' BSP's that accept
		// linedefs where each side point the same sector, that is:
		// the deep water effect with the original Doom // SRB2CBTODO: This is unsed now, remove this

		/// \todo make sure front poly is to front of partition line?

		*frontpoly = poly;
		*backpoly = NULL;
		return;
	}

	if (ps >= 0 && pe < 0)
	{
		*frontpoly = poly;
		*backpoly = NULL;
		return;
	}
	if (pe <= ps)
		I_Error("GLBSP_SplitPoly: invalid splitting line (%d %d)", ps, pe);

	// number of points on each side, _not_ counting those
	// that may lie just one the line
	nptback  = pe - ps - peonline;
	nptfront = poly->numpts - peonline - psonline - nptback;

	if (nptback > 0)
		*backpoly = HWR_AllocPoly(2 + nptback); // SRB2CBTODO: WHY +2?
	else
		*backpoly = NULL;
	if (nptfront)
		*frontpoly = HWR_AllocPoly(2 + nptfront);
	else
		*frontpoly = NULL;

	// generate FRONT poly
	if (*frontpoly)
	{
		pv = (*frontpoly)->pts;
		*pv++ = vs;
		*pv++ = ve;
		i = pe;
		do
		{
			if (++i == poly->numpts)
				i = 0;
			*pv++ = poly->pts[i];
		} while ((int)i != ps && --nptfront);
	}

	// generate BACK poly
	if (*backpoly)
	{
		pv = (*backpoly)->pts;
		*pv++ = ve;
		*pv++ = vs;
		i = ps;
		do
		{
			if (++i == poly->numpts)
				i = 0;
			*pv++ = poly->pts[i];
		} while ((int)i != pe && --nptback);
	}

	// make sure frontpoly is the one on the 'right' side
	// of the partition line
	if (fracs > frace)
	{
		poly_t *swappoly;
		swappoly = *backpoly;
		*backpoly = *frontpoly;
		*frontpoly = swappoly;
	}

	free(poly);
}


// use each seg of the poly as a partition line, keep only the
// part of the convex poly to the front of the seg (that is,
// the part inside the sector), the part behind the seg, is
// the void space and is cut out
//
static poly_t *GLBSP_CutOutSubsecPoly(seg_t *lseg, int count, poly_t *poly)
{
	size_t i, j;

	polyvertex_t *pv;

	int nump = 0, ps, pe;
	polyvertex_t vs = {0, 0, 0}, ve = {0, 0, 0},
		p1 = {0, 0, 0}, p2 = {0, 0, 0};
	float fracs = 0.0f;

	fdivline_t cutseg; // x, y, dx, dy as start of node_t struct

	poly_t *temppoly;
	
	// SRB2CBTODO: Polyobject support using same BSP method POLYOBJECTS_PLANES

	// for each seg of the subsector
	for (; count--; lseg++)
	{
		//x,y,dx,dy (like a divline)
		line_t *line = lseg->linedef;
		p1.x = FIXED_TO_FLOAT(lseg->side ? line->v2->x : line->v1->x);
		p1.y = FIXED_TO_FLOAT(lseg->side ? line->v2->y : line->v1->y);
		p2.x = FIXED_TO_FLOAT(lseg->side ? line->v1->x : line->v2->x);
		p2.y = FIXED_TO_FLOAT(lseg->side ? line->v1->y : line->v2->y);

		cutseg.x = p1.x;
		cutseg.y = p1.y;
		cutseg.dx = p2.x - p1.x;
		cutseg.dy = p2.y - p1.y;

		// see if it cuts the convex poly
		ps = -1;
		pe = -1;
		for (i = 0; i < poly->numpts; i++)
		{
			j = i + 1;
			if (j == poly->numpts)
				j = 0;

			pv = GLBSP_FracDivline(&cutseg, &poly->pts[i], &poly->pts[j]);

			if (pv)
			{
				if (ps < 0)
				{
					ps = i;
					vs = *pv;
					fracs = bspfrac;
				}
				else
				{
					// frac 1 on previous segment,
					//     0 on the next,
					// the split line goes through one of the convex poly
					// vertices, happens quite often since the convex
					// poly is already adjacent to the subsector segs
					// on most borders
					if (GLBSP_SameVertice(pv, &vs))
						continue;

					if (fracs <= bspfrac)
					{
						nump = 2 + poly->numpts - (i-ps); // SRB2CBTODO: WHY +2?!!!
						pe = ps;
						ps = i;
						ve = *pv;
					}
					else
					{
						nump = 2 + (i-ps);
						pe = i;
						ve = vs;
						vs = *pv;
					}
					// found 2nd point
					break;
				}
			}
		}

		// there was a split
		if (ps >= 0)
		{
			// need 2 points
			if (pe >= 0)
			{
				// generate FRONT poly
				temppoly = HWR_AllocPoly(nump);
				pv = temppoly->pts;
				*pv++ = vs;
				*pv++ = ve;
				do
				{
					if (++ps == (int)poly->numpts)
						ps = 0;
					*pv++ = poly->pts[ps];
				} while (ps != pe);
				free(poly);
				poly = temppoly;
			}
			// SRB2CBTODO: // hmmm... maybe we should NOT accept skipping a cut, but this happens
			// only when the cut is not needed it seems (when the cut
			// line is aligned to one of the borders of the poly, and
			// only some times..)
		}
	}
	return poly;
}

// At this point, the poly should be convex and the exact
// layout of the subsector, it is not always the case,
// so continue to cut off the poly into smaller parts with
// each seg of the subsector.
//
static inline void GLBSP_SubsecPoly(int num, poly_t *poly) // SRB2CBTODO: Is this right?
{
	short count;
	subsector_t *sub;
	seg_t *lseg;

	sub = &subsectors[num];
	count = sub->numlines;
	lseg = &segs[sub->firstline];

	if (poly)
	{
		poly = GLBSP_CutOutSubsecPoly(lseg, count, poly);
		// extra data for this subsector
		extrasubsectors[num].planepoly = poly;
	}
}

// The BSP divline does not enough precision
// search for the segs that are source of this divline
static inline void GLBSP_SearchDivline(node_t *bsp, fdivline_t *divline)
{
	divline->x = FIXED_TO_FLOAT(bsp->x);
	divline->y = FIXED_TO_FLOAT(bsp->y);
	divline->dx = FIXED_TO_FLOAT(bsp->dx);
	divline->dy = FIXED_TO_FLOAT(bsp->dy);
}

// The convex polygon that encloses all child subsectors
static void GLBSP_Traverse(int bspnum, poly_t *poly, fixed_t *bbox)
{	
	if (numnodes == 0)
		return;

	// Found a subsector?
	if (bspnum & NF_SUBSECTOR)
	{
		size_t i;
		GLBSP_SubsecPoly(bspnum & (~NF_SUBSECTOR), poly);

		M_ClearBox(bbox);
		poly = extrasubsectors[bspnum & ~NF_SUBSECTOR].planepoly;

		polyvertex_t *pt = poly->pts;
		for (i = 0; i < poly->numpts; i++)
		{
			M_AddToBox(bbox, (fixed_t)(pt->x * FRACUNIT), (fixed_t)(pt->y * FRACUNIT));
			++pt;
		}

	}
	else
	{
		node_t *bsp = &nodes[bspnum];
		fdivline_t fdivline;
		GLBSP_SearchDivline(bsp, &fdivline);
		poly_t *backpoly, *frontpoly;
		GLBSP_SplitPoly(&fdivline, poly, &frontpoly, &backpoly);
		poly = NULL; // SRB2CBTODO: Poly = NULL?!!! Why put it in the functions at all then?!
		
		// Recursively divide front space.
		if (frontpoly)
		{
			GLBSP_Traverse(bsp->children[0], frontpoly, bsp->bbox[0]);
			
			// copy child bbox
			memcpy(bbox, bsp->bbox[0], 4*sizeof (fixed_t));
			//bbox = bsp->bbox[0];
		}
		else
			I_Error("GLBSP_Traverse: No front polygon");
		
		// Recursively divide back space.
		if (backpoly)
		{
			// Correct back bbox to include floor/ceiling convex polygon
			GLBSP_Traverse(bsp->children[1], backpoly, bsp->bbox[1]);
			
			// enlarge bbox with seconde child
			M_AddToBox(bbox, bsp->bbox[1][BOXLEFT  ], bsp->bbox[1][BOXTOP   ]);
			M_AddToBox(bbox, bsp->bbox[1][BOXRIGHT ], bsp->bbox[1][BOXBOTTOM]);
		}
	}
}

#define MAXDIST 1.5f
// Make sure not to actually change any geometry, only render what is given
static boolean GLBSP_PointInSeg(polyvertex_t *a,polyvertex_t *v1,polyvertex_t *v2)
{
	register float ax, ay, bx, by, cx, cy, d, norm;
	register polyvertex_t *p;
	
	// check bbox of the seg first
	if (v1->x > v2->x)
	{
		p = v1;
		v1 = v2;
		v2 = p;
	}
	
	if (a->x < v1->x - MAXDIST || a->x > v2->x + MAXDIST)
		return false;
	
	if (v1->y > v2->y)
	{
		p = v1;
		v1 = v2;
		v2 = p;
	}
	if (a->y < v1->y - MAXDIST || a->y > v2->y + MAXDIST)
		return false;
	
	// v1 = original
	ax= v2->x-v1->x;
	ay= v2->y-v1->y;
	norm = (float)sqrt(ax*ax + ay*ay);
	ax /= norm;
	ay /= norm;
	bx = a->x-v1->x;
	by = a->y-v1->y;
	// d = a.b
	d =ax*bx+ay*by;
	// bound of the seg
	if (d < 0 || d > norm)
		return false;
	// c = d.1a-b
	cx = ax*d-bx;
	cy = ay*d-by;
	
	return cx*cx+cy*cy <= MAXDIST*MAXDIST;
}

ULONG bsprecursions = 0;
boolean done1, done2 = false;

static void GLBSP_ReSearchSegInBSP(int bspnum, polyvertex_t *p, poly_t *poly)
{
	poly_t  *q;
	size_t j, k;
	
	bsprecursions++;
	
	if ((bspnum & NF_SUBSECTOR) && (bspnum != -1))
	{
		bspnum &= ~NF_SUBSECTOR;
		q = extrasubsectors[bspnum].planepoly;
		if (poly == q || !q)
			return;
		for (j = 0; j < q->numpts; j++)
		{
			k = j+1;
			if (k == q->numpts) k = 0;
			if (!GLBSP_SameVertice(p, &q->pts[j])
				&& !GLBSP_SameVertice(p, &q->pts[k])
				&& GLBSP_PointInSeg(p, &q->pts[j],
									&q->pts[k]))
			{
				poly_t *newpoly = HWR_AllocPoly(q->numpts+1);
				size_t n;
				
				for (n = 0; n <= j; n++)
					newpoly->pts[n] = q->pts[n];
				newpoly->pts[k] = *p;
				for (n = k+1; n < newpoly->numpts; n++)
					newpoly->pts[n] = q->pts[n-1];
				extrasubsectors[bspnum].planepoly = newpoly;
				free(q);
				return;
			}
		}
		return;
	}
}
static void GLBSP_SearchSegInBSP(int bspnum, polyvertex_t *p, poly_t *poly)
{
	poly_t  *q;
	size_t j, k;
	
	if ((bspnum & NF_SUBSECTOR) && (bspnum != -1))
	{
		bspnum &= ~NF_SUBSECTOR;
		q = extrasubsectors[bspnum].planepoly;
		if (poly == q || !q)
			return;
		for (j = 0; j < q->numpts; j++)
		{
			k = j+1;
			if (k == q->numpts) k = 0;
			if (!GLBSP_SameVertice(p, &q->pts[j])
				&& !GLBSP_SameVertice(p, &q->pts[k])
				&& GLBSP_PointInSeg(p, &q->pts[j],
									&q->pts[k]))
			{
				poly_t *newpoly = HWR_AllocPoly(q->numpts+1);
				size_t n;
				
				for (n = 0; n <= j; n++)
					newpoly->pts[n] = q->pts[n];
				newpoly->pts[k] = *p;
				for (n = k+1; n < newpoly->numpts; n++)
					newpoly->pts[n] = q->pts[n-1];
				extrasubsectors[bspnum].planepoly = newpoly;
				free(q);
				return;
			}
		}
		return;
	}
	
	// SRB2CBTODO: This code right here causes infinite loops,
	// set it so that values can be modified in a loop until proper,
	// and only for a set amount of loops to prevent infinite recursion
	if ((bsprecursions < 10000) && ((FIXED_TO_FLOAT(nodes[bspnum].bbox[0][BOXBOTTOM])-MAXDIST <= p->y) &&
		 (FIXED_TO_FLOAT(nodes[bspnum].bbox[0][BOXTOP   ])+MAXDIST >= p->y) &&
		 (FIXED_TO_FLOAT(nodes[bspnum].bbox[0][BOXLEFT  ])-MAXDIST <= p->x) &&
		 (FIXED_TO_FLOAT(nodes[bspnum].bbox[0][BOXRIGHT ])+MAXDIST >= p->x)))
	{
		GLBSP_ReSearchSegInBSP(nodes[bspnum].children[0], p, poly);
	}
	else
	{
		done1 = true;
	}
	
	
	if ((bsprecursions < 10000) && ((FIXED_TO_FLOAT(nodes[bspnum].bbox[1][BOXBOTTOM])-MAXDIST <= p->y) &&
		 (FIXED_TO_FLOAT(nodes[bspnum].bbox[1][BOXTOP   ])+MAXDIST >= p->y) &&
		 (FIXED_TO_FLOAT(nodes[bspnum].bbox[1][BOXLEFT  ])-MAXDIST <= p->x) &&
		 (FIXED_TO_FLOAT(nodes[bspnum].bbox[1][BOXRIGHT ])+MAXDIST >= p->x)))
	{
		GLBSP_ReSearchSegInBSP(nodes[bspnum].children[1], p, poly);
	}
	else
	{
		done2 = true;
	}
	
	if (done1 && done2)
	{
		bsprecursions = 0;
		done1 = done2 = false;
	}

}

// Search for the T-intersection problem for plane polygons
// It can be much more faster doing this at the same time of the splitpoly
// but we must use a different structure : polygon pointing on segs
// segs pointing on polygon and on vertex
// the method discibed is also better for segs precision
static void GLBSP_SolvePlaneTJoins(void) // SRB2CBTODO: Infinite loop can happen
{
	poly_t *p;
	size_t i;
	size_t l;
	
	for (l = 0; l < numsubsectors; l++)
	{
		if (!(extrasubsectors[l].planepoly))
			continue;
		
		p = extrasubsectors[l].planepoly;
		
		for (i = 0; i < p->numpts; i++)
			GLBSP_SearchSegInBSP((int)numnodes-1, &p->pts[i], p);
	}
}

/* Adjust the true segs (from the segs lump) to be exactly the same as
 * the plane polygon segs
 * This also converts segs from fixed_t to floating point for OpenGL
 * (in most cases it shares the same vertices)
 */
static void GLBSP_AdjustSegs(void)
{
	size_t i, count;
	seg_t *lseg;

	for (i = 0; i < numsubsectors; i++)
	{
		count = subsectors[i].numlines;
		lseg = &segs[subsectors[i].firstline];

		if (!extrasubsectors[i].planepoly)
			continue;

		for (; count--; lseg++)
		{
#ifdef POLYOBJECTS // Skip polyobject segs for BSP
			// Polyobject segments are special, their line segments must be converted elsewhere
			if (lseg->polyseg)
				continue;
#endif

			// SRB2CBTODO: BP: Possible to do better, using PointInSeg and compute
			// the right point position also split a polygon side to
			// solve a T-intersection
			
			// Convert the linedef from fixed_t to floating point
			polyvertex_t *pv1;
			pv1 = Z_Malloc(sizeof(polyvertex_t), PU_LEVEL, NULL);
			
			pv1->x = FIXED_TO_FLOAT(lseg->v1->x);
			pv1->y = FIXED_TO_FLOAT(lseg->v1->y);
			lseg->v1 = (vertex_t *)pv1;
			
			polyvertex_t *pv2;
			pv2 = Z_Malloc(sizeof(polyvertex_t), PU_LEVEL, NULL);
			
			pv2->x = FIXED_TO_FLOAT(lseg->v2->x);
			pv2->y = FIXED_TO_FLOAT(lseg->v2->y);
			lseg->v2 = (vertex_t *)pv2;
			
			
			// Recompute the length of the linedef
			float x;
			float y;
			x = ((polyvertex_t *)lseg->v2)->x - ((polyvertex_t *)lseg->v1)->x;
			y = ((polyvertex_t *)lseg->v2)->y - ((polyvertex_t *)lseg->v1)->y;
			// Seglength(len) is calculated already at setup,
			// recomputing it here would actually,
			// make minor unwanted changes in texture alignment
			lseg->len = (float)sqrt(x*x+y*y)*FRACUNIT;
		}
	}
}

void HWR_FreeExtraSubsectors(void)
{
	if (extrasubsectors)
		free(extrasubsectors);
	extrasubsectors = NULL;
}

// Call this routine after the BSP of a wad file is loaded,
// and it will generate all the convex polys for the hardware renderer
void HWR_CreateGLBSP(int bspnum)
{
	poly_t *rootp;
	polyvertex_t *rootpv;
	size_t i;
	fixed_t rootbbox[4];

	// Find min/max boundaries of map
	M_ClearBox(rootbbox);
	for (i = 0; i < numvertexes; i++)
		M_AddToBox(rootbbox, vertexes[i].x, vertexes[i].y);

	HWR_FreeExtraSubsectors();
	// Allocate extra data for each subsector present in map
	extrasubsectors = calloc(numsubsectors, sizeof (*extrasubsectors));

	// SRB2CBTODO: WOOOOWWWW, ALWAYS USE THIS __FUNCTION__ INFORMATION
	if (extrasubsectors == NULL)
		I_Error("%s: Couldn't malloc extrasubsectors totalsubsectors %d\n", __FUNCTION__, (int)numsubsectors);

	// construct the initial convex poly that encloses the full map // SRB2CBTODO: Does this always work?
	rootp = HWR_AllocPoly(4);
	
	// NOTE: This allocates stuff for the BSP
	// also, the poly has 4 vertexes and encompases the whole map and then gets divided up
	rootpv = rootp->pts;
	
	rootpv->x = FIXED_TO_FLOAT(rootbbox[BOXLEFT  ]);
	rootpv->y = FIXED_TO_FLOAT(rootbbox[BOXBOTTOM]);  //lr
	rootpv++;
	rootpv->x = FIXED_TO_FLOAT(rootbbox[BOXLEFT  ]);
	rootpv->y = FIXED_TO_FLOAT(rootbbox[BOXTOP   ]);  //ur
	rootpv++;
	rootpv->x = FIXED_TO_FLOAT(rootbbox[BOXRIGHT ]);
	rootpv->y = FIXED_TO_FLOAT(rootbbox[BOXTOP   ]);  //ul
	rootpv++;
	rootpv->x = FIXED_TO_FLOAT(rootbbox[BOXRIGHT ]);
	rootpv->y = FIXED_TO_FLOAT(rootbbox[BOXBOTTOM]);  //ll
	rootpv++;
	
	if (!rootpv)
		I_Error("Could not allocate BSP!\n");

	GLBSP_Traverse(bspnum, rootp, rootbbox); // Create sub sectors
	GLBSP_SolvePlaneTJoins(); // Kalaron: Fixed to prevent infinite recursions
	GLBSP_AdjustSegs();
}

#endif //HWRENDER
