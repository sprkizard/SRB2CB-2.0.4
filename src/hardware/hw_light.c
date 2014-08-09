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
/// \brief Corona/Dynamic/Static lighting

#include "../doomdef.h"

#ifdef HWRENDER
#include "hw_main.h"
#include "hw_light.h"
#include "hw_drv.h"
#include "../i_video.h"
#include "../z_zone.h"
#include "../m_random.h"
#include "../m_bbox.h"
#include "../w_wad.h"
#include "../r_state.h"
#include "../r_main.h"
#include "../p_local.h"


//=============================================================================
//                                                                      DEFINES
//=============================================================================
// Dynamic lights
static dynlights_t view_dynlights[2]; // 2 players in splitscreen mode // SRB2CBTODO: More players?
static dynlights_t *dynlights = &view_dynlights[0];

#define DL_SQRRADIUS(x) (dynlights->p_lspr[(x)]->dynamic_radius*dynlights->p_lspr[(x)]->dynamic_radius)
#define DL_RADIUS(x) dynlights->p_lspr[(x)]->dynamic_radius
#define LIGHT_POS(i) dynlights->position[(i)]

// Dynamic shadows
static dynshadows_t view_dynshadows[2]; // 2 players in splitscreen mode // SRB2CBTODO: More players?
static dynshadows_t *dynshadows = &view_dynshadows[0];

#define DS_SQRRADIUS(x) (dynshadows->p_sspr[(x)]->dynamic_radius*dynshadows->p_sspr[(x)]->dynamic_radius)
#define DS_RADIUS(x) dynshadows->p_sspr[(x)]->dynamic_radius
#define SHADOW_POS(i) dynshadows->position[(i)]

//=============================================================================
//                                                                       PROTOS
//=============================================================================

static void HWR_SetShadowTexture(gr_vissprite_t *spr);

// --------------------------------------------------------------------------
// Calculate the projection of a point on a line (two determinite points)
// and then calculate the distance (carry at this point to point this line projects
// --------------------------------------------------------------------------
static float HWR_DistP2D(FOutVector *p1, FOutVector *p2, FVector *p3, FVector *inter)
{
	if (p1->z == p2->z)
	{
		inter->x = p3->x;
		inter->z = p1->z;
	}
	else if (p1->x == p2->x)
	{
		inter->x = p1->x;
		inter->z = p3->z;
	}
	else
	{
		register float local, pente;
		pente = (p1->z-p2->z) / (p1->x-p2->x);
		local = p1->z - p1->x*pente;
		inter->x = (p3->z - local + p3->x/pente) * (pente/(pente*pente+1));
		inter->z = inter->x*pente + local;
	}
	
	return (p3->x-inter->x)*(p3->x-inter->x) + (p3->z-inter->z)*(p3->z-inter->z);
}

// Check if sphere (radius r) centred in p3 touch the bounding box defined by p1, p2
static boolean SphereTouchBBox3D(FOutVector *p1, FOutVector *p2, FVector *p3, float r)
{
	float minx = p1->x, maxx = p2->x, miny = p2->y, maxy = p1->y, minz = p2->z, maxz = p1->z;
	
	if (minx > maxx)
	{
		minx = maxx;
		maxx = p1->x;
	}
	if (miny > maxy)
	{
		miny = maxy;
		maxy = p2->y;
	}
	if (minz > maxz)
	{
		minz = maxz;
		maxz = p2->z;
	}
	
	if (minx-r > p3->x) return false;
	if (maxx+r < p3->x) return false;
	if (miny-r > p3->y) return false;
	if (maxy+r < p3->y) return false;
	if (minz-r > p3->z) return false;
	if (maxz+r < p3->z) return false;
	return true;
}

// Epic new light method!
// Get the position from the light's sprite's location,
// and draw the polygon in the sprite drawing function itself
void HWR_DrawCorona(double x, double y, double z, ULONG color, double size)
{	
	FOutVector      light[4];
	FSurfaceInfo    Surf;
	float           cx = x;
	// In OpenGL, these coords are flipped
	float           cz = y;
	float           cy = z;
	
	HWR_Transform(&cx,&cy,&cz, false);
	
	// Get the color from the light defines
	Surf.FlatColor.rgba = color;
	// Always draw it full brightness
	Surf.FlatColor.s.alpha = 0xff;
	
	light[0].x = cx-size;  light[0].z = cz;
	light[0].y = cy-size;
	light[0].sow = 0.0f;   light[0].tow = 0.0f;
	
	light[1].x = cx+size;  light[1].z = cz;
	light[1].y = cy-size;
	light[1].sow = 1.0f;   light[1].tow = 0.0f;
	
	light[2].x = cx+size;  light[2].z = cz;
	light[2].y = cy+size;
	light[2].sow = 1.0f;   light[2].tow = 1.0f;
	
	light[3].x = cx-size;  light[3].z = cz;
	light[3].y = cy+size;
	light[3].sow = 0.0f;   light[3].tow = 1.0f;
	
	HWR_GetPic(W_GetNumForName("corona")); // Get the corona image!
	
	GL_DrawPolygon(&Surf, light, 4, PF_Modulated|PF_Additive, 0);
}

#if 0
void HWR_DrawCorona(double x, double y, double z, ULONG color, double size)
{	
	wallVert3D      light[4];
	FSurfaceInfo    Surf;
	float           cx = x;
	// In OpenGL, these coords are flipped
	float           cz = y;
	float           cy = z;
	
	//HWR_Transform(&cx,&cy,&cz, false);
	
	// Get the color from the light defines
	Surf.FlatColor.rgba = color;
	// Always draw it full brightness
	Surf.FlatColor.s.alpha = 0xff;
	
	light[0].x = cx-size;  light[0].z = cz;
	light[0].y = cy-size;
	light[0].s = 0.0f;   light[0].t = 0.0f;
	
	light[1].x = cx+size;  light[1].z = cz;
	light[1].y = cy-size;
	light[1].s = 1.0f;   light[1].t = 0.0f;
	
	light[2].x = cx+size;  light[2].z = cz;
	light[2].y = cy+size;
	light[2].s = 1.0f;   light[2].t = 1.0f;
	
	light[3].x = cx-size;  light[3].z = cz;
	light[3].y = cy+size;
	light[3].s = 0.0f;   light[3].t = 1.0f;
	
	GL_SetTransform(&atransform);

	HWR_GetPic(W_GetNumForName("corona")); // Get the corona image!
	HWR_ProjectWall(light, &Surf, PF_Modulated|PF_Additive);
	
	GL_SetTransform(NULL);
}
#endif


// --------------------------------------------------------------------------
// Calculation of dynamic lighting on walls
// Coords lVerts contains the wall with mlook transformed
// --------------------------------------------------------------------------
void HWR_SpriteLighting(FOutVector *wlVerts) // SRB2CBTODO: Support sprites to be lit too
{
	if (!dynlights->nb)
		return;
	
	int             i, j;
	
	for (j = 0; j < dynlights->nb; j++)
	{
		FVector         inter;
		FSurfaceInfo    Surf;
		float           dist_p2d, d[4], s;
		
		// check bounding box first
		if (SphereTouchBBox3D(&wlVerts[2], &wlVerts[0], &LIGHT_POS(j), DL_RADIUS(j))==false)
			continue;
		
		CONS_Printf("SphereTouchBBox3D Success!\n");
		
		d[0] = wlVerts[2].x - wlVerts[0].x;
		d[1] = wlVerts[2].z - wlVerts[0].z;
		d[2] = LIGHT_POS(j).x - wlVerts[0].x;
		d[3] = LIGHT_POS(j).z - wlVerts[0].z;
		// backface cull
		//if (d[2]*d[1] - d[3]*d[0] < 0)
		//	continue;
		
		// check exact distance
		dist_p2d = HWR_DistP2D(&wlVerts[2], &wlVerts[0],  &LIGHT_POS(j), &inter);
		if (dist_p2d >= DL_SQRRADIUS(j))
			continue;
		
		CONS_Printf("Sprite close to light: Rendering\n");
		
		d[0] = (float)sqrt((wlVerts[0].x-inter.x)*(wlVerts[0].x-inter.x)
						   + (wlVerts[0].z-inter.z)*(wlVerts[0].z-inter.z));
		d[1] = (float)sqrt((wlVerts[2].x-inter.x)*(wlVerts[2].x-inter.x)
						   + (wlVerts[2].z-inter.z)*(wlVerts[2].z-inter.z));
		//dAB = (float)sqrt((wlVerts[0].x-wlVerts[2].x)*(wlVerts[0].x-wlVerts[2].x)+(wlVerts[0].z-wlVerts[2].z)*(wlVerts[0].z-wlVerts[2].z));
		//if ((d[0] < dAB) && (d[1] < dAB)) // test if the intersection is on the wall
		//{
		//    d[0] = -d[0]; // if yes, the left distcance must be negative for texcoord
		//}
		// test if the intersection is on the wall
		if ((wlVerts[0].x < inter.x && wlVerts[2].x > inter.x) ||
			(wlVerts[0].x > inter.x && wlVerts[2].x < inter.x) ||
			(wlVerts[0].z < inter.z && wlVerts[2].z > inter.z) ||
			(wlVerts[0].z > inter.z && wlVerts[2].z < inter.z))
		{
			d[0] = -d[0]; // if yes, the left distcance must be negative for texcoord
		}
		d[2] = d[1]; d[3] = d[0];
		
		s = 0.5f / DL_RADIUS(j);
		
		for (i = 0; i < 4; i++)
		{
			wlVerts[i].sow = (float)(0.5f + d[i]*s);
			wlVerts[i].tow = (float)(0.5f + (wlVerts[i].y-LIGHT_POS(j).y)*s*1.2f);
		}
		
		Surf.FlatColor.rgba = LONG(dynlights->p_lspr[j]->dynamic_color);
		
		Surf.FlatColor.s.alpha = (byte)((1-dist_p2d/DL_SQRRADIUS(j))*Surf.FlatColor.s.alpha);
		
		GL_DrawPolygon (&Surf, wlVerts, 4, PF_Modulated|PF_Additive, 0);
		
	} // end for (j = 0; j < dynlights->nb; j++)
}

// --------------------------------------------------------------------------
// Calculation of dynamic lighting on walls
// Coords lVerts contains the wall with mlook transformed
// --------------------------------------------------------------------------
void HWR_WallLighting(FOutVector *wlVerts)
{
	if (!dynlights->nb)
		return;

	int j;

	for (j = 0; j < dynlights->nb; j++)
	{
		FVector         inter;
		FSurfaceInfo    Surf;
		float           dist_p2d, d[4];
		
		// check bounding box first
		if (SphereTouchBBox3D(&wlVerts[2], &wlVerts[0], &LIGHT_POS(j), DL_RADIUS(j))==false)
			continue;
		d[0] = wlVerts[2].x - wlVerts[0].x;
		d[1] = wlVerts[2].z - wlVerts[0].z;
		d[2] = LIGHT_POS(j).x - wlVerts[0].x;
		d[3] = LIGHT_POS(j).z - wlVerts[0].z;
		// backface cull
		if (d[2]*d[1] - d[3]*d[0] < 0)
			continue;
		// check exact distance
		dist_p2d = HWR_DistP2D(&wlVerts[2], &wlVerts[0],  &LIGHT_POS(j), &inter);
		if (dist_p2d >= DL_SQRRADIUS(j))
			continue;
		
		d[0] = (float)sqrt((wlVerts[0].x-inter.x)*(wlVerts[0].x-inter.x)
						   + (wlVerts[0].z-inter.z)*(wlVerts[0].z-inter.z));
		d[1] = (float)sqrt((wlVerts[2].x-inter.x)*(wlVerts[2].x-inter.x)
						   + (wlVerts[2].z-inter.z)*(wlVerts[2].z-inter.z));
		//dAB = (float)sqrt((wlVerts[0].x-wlVerts[2].x)*(wlVerts[0].x-wlVerts[2].x)+(wlVerts[0].z-wlVerts[2].z)*(wlVerts[0].z-wlVerts[2].z));
		//if ((d[0] < dAB) && (d[1] < dAB)) // test if the intersection is on the wall
		//{
		//    d[0] = -d[0]; // if yes, the left distcance must be negative for texcoord
		//}
		// test if the intersection is on the wall
		if ((wlVerts[0].x < inter.x && wlVerts[2].x > inter.x) ||
			(wlVerts[0].x > inter.x && wlVerts[2].x < inter.x) ||
			(wlVerts[0].z < inter.z && wlVerts[2].z > inter.z) ||
			(wlVerts[0].z > inter.z && wlVerts[2].z < inter.z))
		{
			d[0] = -d[0]; // if yes, the left distcance must be negative for texcoord
		}
		d[2] = d[1]; d[3] = d[0];
		
		
		
		// SRB2CBTODO: Cut out a disk shape of texture to cast on the wall
#if 0
		int i;
		float s = 0.5f / DL_RADIUS(j);
		for (i = 0; i < 4; i++)
		{
			wlVerts[i].sow = (float)(0.5f + d[i]*s);
			wlVerts[i].tow = (float)(0.5f + (wlVerts[i].y-LIGHT_POS(j).y)*s*1.2f);
		}
#endif
		
		Surf.FlatColor.rgba = LONG(dynlights->p_lspr[j]->dynamic_color);
		
		Surf.FlatColor.s.alpha = (byte)((1-dist_p2d/DL_SQRRADIUS(j))*Surf.FlatColor.s.alpha);
		
		GL_DrawPolygon (&Surf, wlVerts, 4, PF_Modulated|PF_Additive, 0);
		
	} // end for (j = 0; j < dynlights->nb; j++)
}

// --------------------------------------------------------------------------
// Calculation od dynamic lighting on planes
// Coords clVerts contain positions without up/down mouselook transform
// --------------------------------------------------------------------------
// SRB2CBTODO: Use plane's HWR_Lighting to add modulation to an individual texture for true lighting
// SRB2CBTODO: The sow and tow of the texture should be the same as the plane/wall
void HWR_PlaneLighting(FOutVector *clVerts, int nrClipVerts)
{
	if (!dynlights->nb)
		return;

	int j = 0;
	FOutVector p1, p2;
	
	p1.z = FIXED_TO_FLOAT(hwlightbbox[BOXTOP   ]);
	p1.x = FIXED_TO_FLOAT(hwlightbbox[BOXLEFT  ]);
	p2.z = FIXED_TO_FLOAT(hwlightbbox[BOXBOTTOM]);
	p2.x = FIXED_TO_FLOAT(hwlightbbox[BOXRIGHT ]);
	p2.y = clVerts[0].y;
	p1.y = clVerts[0].y;
	
	for (j = 0; j < dynlights->nb; j++)
	{
		FSurfaceInfo    Surf;
		float           dist_p2d, s;
		
		// Check if the light touches a bounding box
		if (SphereTouchBBox3D(&p1, &p2, &dynlights->position[j], DL_RADIUS(j))==false)
			continue;
		// backface cull
		//Hurdler: doesn't work with new TANDL code
		if ((clVerts[0].y > atransform.z)       // true mean it is a ceiling false is a floor
			^ (LIGHT_POS(j).y < clVerts[0].y)) // true mean light is down plane false light is up plane
			continue;
		dist_p2d = (clVerts[0].y-LIGHT_POS(j).y);
		dist_p2d *= dist_p2d;
		// done in SphereTouchBBox3D
		//if (dist_p2d >= DL_SQRRADIUS(j))
		//    continue;
		
		s = 0.5f / DL_RADIUS(j);
	// SRB2CBTODO: Cut out a disk shape of texture to cast on the plane	
#if 1
		int i;
		for (i = 0; i < nrClipVerts; i++)
		{
			clVerts[i].sow = 0.5f + (clVerts[i].x-LIGHT_POS(j).x)*s;
			clVerts[i].tow = 0.5f + (clVerts[i].z-LIGHT_POS(j).z)*s*1.2f;
		}
#endif
		
		Surf.FlatColor.rgba = LONG(dynlights->p_lspr[j]->dynamic_color);
		
		Surf.FlatColor.s.alpha = (byte)((1 - dist_p2d/DL_SQRRADIUS(j))*Surf.FlatColor.s.alpha);
		
		GL_DrawPolygon(&Surf, clVerts, nrClipVerts, PF_Modulated|PF_Additive, 0);
	} // end for (j = 0; j < dynlights->nb; j++)
}


void HWR_RenderFloorSplat(FOutVector *clVerts, int nrClipVerts)
{
	if (!dynshadows->nb)
		return;

	int     i, j;
	FOutVector p1,p2;
	
	p1.z = FIXED_TO_FLOAT(hwlightbbox[BOXTOP   ]);
	p1.x = FIXED_TO_FLOAT(hwlightbbox[BOXLEFT  ]);
	p2.z = FIXED_TO_FLOAT(hwlightbbox[BOXBOTTOM]);
	p2.x = FIXED_TO_FLOAT(hwlightbbox[BOXRIGHT ]);
	p2.y = clVerts[0].y;
	p1.y = clVerts[0].y;
	
	for (j = 0; j < dynshadows->nb; j++)
	{
		FSurfaceInfo    Surf;
		float           dist_p2d, s; // s = size
		
		// Check if the light touches a bounding box
		if (SphereTouchBBox3D(&p1, &p2, &dynshadows->position[j], DS_RADIUS(j))==false)
			continue;
		// backface cull

		if ((clVerts[0].y > atransform.z)       // true means it is a ceiling false is a floor
			^ (SHADOW_POS(j).y < clVerts[0].y)) // true means light is down plane false light is up plane
			continue;
		dist_p2d = (clVerts[0].y-SHADOW_POS(j).y);
		dist_p2d *= dist_p2d;
		
		// 1.0f
		s = (0.5f)/100.f;
		// SRB2CBTODO: Cut out a disk shape of texture to cast on the plane	
		for (i = 0; i < nrClipVerts; i++)
		{
			clVerts[i].sow = (clVerts[i].x - SHADOW_POS(j).x)*s;
			clVerts[i].tow = (clVerts[i].z - SHADOW_POS(j).z)*s;
		}

		HWR_SetShadowTexture(dynshadows->p_sspr[j]->spr); // SRB2CBTODO: Have an individual "downloaded" thing like lightmap has so it STAYS
		
		//Surf.FlatColor.rgba = LONG(dynshadows->p_sspr[j]->dynamic_color);
		// The normal color of a shadow   
		Surf.FlatColor.s.red = 0x00;
		Surf.FlatColor.s.blue = 0x00;
		Surf.FlatColor.s.green = 0x00;
		
		Surf.FlatColor.s.alpha = (byte)((1 - dist_p2d/DS_SQRRADIUS(j))*128); // 128 = max darkness of shadow
		
		GL_DrawPolygon(&Surf, clVerts, nrClipVerts, PF_Modulated|PF_Decal|PF_Translucent, 0);
	} // end for (j = 0; j < dynshadows->nb; j++)
}


// --------------------------------------------------------------------------
// Calculation of dynamic lighting on walls
// Coords lVerts contains the wall with mlook transformed
// --------------------------------------------------------------------------
void HWR_WallShading(FOutVector *wlVerts)
{
	if (!dynshadows->nb)
		return;

	int             i, j;
	
	for (j = 0; j < dynshadows->nb; j++)
	{
		FVector         inter;
		FSurfaceInfo    Surf;
		float           dist_p2d, d[4], s;
		
		// check bounding box first
		if (SphereTouchBBox3D(&wlVerts[2], &wlVerts[0], &SHADOW_POS(j), DS_RADIUS(j))==false)
			continue;
		d[0] = wlVerts[2].x - wlVerts[0].x;
		d[1] = wlVerts[2].z - wlVerts[0].z;
		d[2] = SHADOW_POS(j).x - wlVerts[0].x;
		d[3] = SHADOW_POS(j).z - wlVerts[0].z;
		// backface cull
		if (d[2]*d[1] - d[3]*d[0] < 0)
			continue;
		// check exact distance
		dist_p2d = HWR_DistP2D(&wlVerts[2], &wlVerts[0],  &SHADOW_POS(j), &inter);
		if (dist_p2d >= DS_SQRRADIUS(j))
			continue;
		
		d[0] = (float)sqrt((wlVerts[0].x-inter.x)*(wlVerts[0].x-inter.x)
						   + (wlVerts[0].z-inter.z)*(wlVerts[0].z-inter.z));
		d[1] = (float)sqrt((wlVerts[2].x-inter.x)*(wlVerts[2].x-inter.x)
						   + (wlVerts[2].z-inter.z)*(wlVerts[2].z-inter.z));
		//dAB = (float)sqrt((wlVerts[0].x-wlVerts[2].x)*(wlVerts[0].x-wlVerts[2].x)+(wlVerts[0].z-wlVerts[2].z)*(wlVerts[0].z-wlVerts[2].z));
		//if ((d[0] < dAB) && (d[1] < dAB)) // test if the intersection is on the wall
		//{
		//    d[0] = -d[0]; // if yes, the left distcance must be negative for texcoord
		//}
		// test if the intersection is on the wall
		if ((wlVerts[0].x < inter.x && wlVerts[2].x > inter.x) ||
			(wlVerts[0].x > inter.x && wlVerts[2].x < inter.x) ||
			(wlVerts[0].z < inter.z && wlVerts[2].z > inter.z) ||
			(wlVerts[0].z > inter.z && wlVerts[2].z < inter.z))
		{
			d[0] = -d[0]; // if yes, the left distcance must be negative for texcoord
		}
		d[2] = d[1]; d[3] = d[0];
		
		s = 0.5f / DS_RADIUS(j);
		
		// SRB2CBTODO: Cut out a disk shape of texture to cast on the wall
		for (i = 0; i < 4; i++)
		{
			wlVerts[i].sow = (float)(0.5f + d[i]*s);
			wlVerts[i].tow = (float)(0.5f + (wlVerts[i].y-SHADOW_POS(j).y)*s*1.2f);
		}
		
		HWR_SetShadowTexture(dynshadows->p_sspr[j]->spr);
		
		//Surf.FlatColor.rgba = LONG(dynshadows->p_sspr[j]->dynamic_color);
		// The normal color of a shadow   
		Surf.FlatColor.s.red = 0x00;
		Surf.FlatColor.s.blue = 0x00;
		Surf.FlatColor.s.green = 0x00;
		
		Surf.FlatColor.s.alpha = (byte)((1-dist_p2d/DS_SQRRADIUS(j))*Surf.FlatColor.s.alpha);
		
		GL_DrawPolygon (&Surf, wlVerts, 4, PF_Modulated|PF_Decal|PF_Translucent, 0);
		
	} // end for (j = 0; j < dynshadows->nb; j++)
}


#if 0
// --------------------------------------------------------------------------
// Calculation of dynamic lighting on walls
// Coords lVerts contains the wall with mlook transformed
// --------------------------------------------------------------------------
void HWR_WallLighting(FOutVector *wlVerts)
{
	if (!dynlights->nb)
		return;

	int             i, j;
	
	for (j = 0; j < dynlights->nb; j++)
	{
		FVector         inter;
		FSurfaceInfo    Surf;
		float           dist_p2d, d[4], s;
		
		// check bounding box first
		if (SphereTouchBBox3D(&wlVerts[2], &wlVerts[0], &LIGHT_POS(j), DL_RADIUS(j))==false)
			continue;
		d[0] = wlVerts[2].x - wlVerts[0].x;
		d[1] = wlVerts[2].z - wlVerts[0].z;
		d[2] = LIGHT_POS(j).x - wlVerts[0].x;
		d[3] = LIGHT_POS(j).z - wlVerts[0].z;
		// backface cull
		if (d[2]*d[1] - d[3]*d[0] < 0)
			continue;
		// check exact distance
		dist_p2d = HWR_DistP2D(&wlVerts[2], &wlVerts[0],  &LIGHT_POS(j), &inter);
		if (dist_p2d >= DL_SQRRADIUS(j))
			continue;
		
		d[0] = (float)sqrt((wlVerts[0].x-inter.x)*(wlVerts[0].x-inter.x)
						   + (wlVerts[0].z-inter.z)*(wlVerts[0].z-inter.z));
		d[1] = (float)sqrt((wlVerts[2].x-inter.x)*(wlVerts[2].x-inter.x)
						   + (wlVerts[2].z-inter.z)*(wlVerts[2].z-inter.z));
		//dAB = (float)sqrt((wlVerts[0].x-wlVerts[2].x)*(wlVerts[0].x-wlVerts[2].x)+(wlVerts[0].z-wlVerts[2].z)*(wlVerts[0].z-wlVerts[2].z));
		//if ((d[0] < dAB) && (d[1] < dAB)) // test if the intersection is on the wall
		//{
		//    d[0] = -d[0]; // if yes, the left distcance must be negative for texcoord
		//}
		// test if the intersection is on the wall
		if ((wlVerts[0].x < inter.x && wlVerts[2].x > inter.x) ||
			(wlVerts[0].x > inter.x && wlVerts[2].x < inter.x) ||
			(wlVerts[0].z < inter.z && wlVerts[2].z > inter.z) ||
			(wlVerts[0].z > inter.z && wlVerts[2].z < inter.z))
		{
			d[0] = -d[0]; // if yes, the left distcance must be negative for texcoord
		}
		d[2] = d[1]; d[3] = d[0];
		
		s = 0.5f / DL_RADIUS(j);
		
		for (i = 0; i < 4; i++)
		{
			wlVerts[i].sow = (float)(0.5f + d[i]*s);
			wlVerts[i].tow = (float)(0.5f + (wlVerts[i].y-LIGHT_POS(j).y)*s*1.2f);
		}
		
		Surf.FlatColor.rgba = LONG(dynlights->p_lspr[j]->dynamic_color);
		
		Surf.FlatColor.s.alpha = (byte)((1-dist_p2d/DL_SQRRADIUS(j))*Surf.FlatColor.s.alpha);
		
		GL_DrawPolygon(&Surf, wlVerts, 4, PF_Modulated|PF_Additive);
		
	} // end for (j = 0; j < dynlights->nb; j++)
}

// --------------------------------------------------------------------------
// Calculation od dynamic lighting on planes
// Coords clVerts contain positions without up/down mouselook transform
// --------------------------------------------------------------------------
// SRB2CBTODO: Use plane's HWR_Lighting to add modulation to an individual texture for true lighting
// SRB2CBTODO: The sow and tow of the texture should be the same as the plane/wall
void HWR_PlaneLighting(FOutVector *clVerts, int nrClipVerts)
{
	if (!dynlights->nb)
		return;

	int     i, j;
	FOutVector p1,p2;
	
	p1.z = FIXED_TO_FLOAT(hwlightbbox[BOXTOP   ]);
	p1.x = FIXED_TO_FLOAT(hwlightbbox[BOXLEFT  ]);
	p2.z = FIXED_TO_FLOAT(hwlightbbox[BOXBOTTOM]);
	p2.x = FIXED_TO_FLOAT(hwlightbbox[BOXRIGHT ]);
	p2.y = clVerts[0].y;
	p1.y = clVerts[0].y;
	
	for (j = 0; j < dynlights->nb; j++)
	{
		FSurfaceInfo    Surf;
		float           dist_p2d, s;
		
		// Check if the light touches a bounding box
		if (SphereTouchBBox3D(&p1, &p2, &dynlights->position[j], DL_RADIUS(j))==false)
			continue;
		// backface cull

		if ((clVerts[0].y > atransform.z)       // true mean it is a ceiling false is a floor
			^ (LIGHT_POS(j).y < clVerts[0].y)) // true mean light is down plane false light is up plane
			continue;
		dist_p2d = (clVerts[0].y-LIGHT_POS(j).y);
		dist_p2d *= dist_p2d;
		// done in SphereTouchBBox3D
		//if (dist_p2d >= DL_SQRRADIUS(j))
		//    continue;
		
		s = 0.5f / DL_RADIUS(j);
		
		for (i = 0; i < nrClipVerts; i++)
		{
			clVerts[i].sow = 0.5f + (clVerts[i].x-LIGHT_POS(j).x)*s;
			clVerts[i].tow = 0.5f + (clVerts[i].z-LIGHT_POS(j).z)*s*1.2f;
			
			// SRB2CBTODO: It is possible to make a circle shape
			// and scale it or make an awesome texture like light texture
		}
		
		// SRB2CBTODO: It could be possible to use the plane/wall's texture for REAL sector like lighting!
		Surf.FlatColor.rgba = LONG(dynlights->p_lspr[j]->dynamic_color);
		
		Surf.FlatColor.s.alpha = (byte)((1 - dist_p2d/DL_SQRRADIUS(j))*Surf.FlatColor.s.alpha);
		
		GL_DrawPolygon(&Surf, clVerts, nrClipVerts, PF_Modulated|PF_Additive);
	} // end for (j = 0; j < dynlights->nb; j++)
}
#endif

// --------------------------------------------------------------------------
// Remove all the dynamic lights at eatch frame
// --------------------------------------------------------------------------
void HWR_ResetLights(void)
{
	dynlights->nb = 0;
}

// --------------------------------------------------------------------------
// Remove all the dynamic shadows at eatch frame
// --------------------------------------------------------------------------
void HWR_ResetShadows(void)
{
	dynshadows->nb = 0;
}

// --------------------------------------------------------------------------
// Make sure to render the correct lights for splitscreen
// --------------------------------------------------------------------------
void HWR_SetLights(int viewnumber)
{
	dynlights = &view_dynlights[viewnumber];
}

// --------------------------------------------------------------------------
// Make sure to render the correct shadows for splitscreen
// --------------------------------------------------------------------------
void HWR_SetShadows(int viewnumber)
{
	dynshadows = &view_dynshadows[viewnumber];
}

// --------------------------------------------------------------------------
// Add a light for dynamic lighting
// The light position is already transformed execpt for mlook
// --------------------------------------------------------------------------
void HWR_AddDynLight(double x, double y, double z, ULONG color, byte radius)
{	
	// Make sure that no more lights than max are used per level
	if (dynlights->nb < DL_MAX_LIGHT)
	{
		light_t   *newlight = Z_Malloc(sizeof(light_t), PU_LEVEL, NULL);
		
		// check if sprite contain dynamic light
		newlight->dynamic_color = color;
		newlight->dynamic_radius = radius;
		newlight->light_xoffset = newlight->light_yoffset = 0.0f; // SRB2CBTODO: Could be used?

		// NOTE: The coordinates are flipped here for OpenGL
		LIGHT_POS(dynlights->nb).x = x;
		LIGHT_POS(dynlights->nb).y = z; // SRB2CBTODO: + lightoffset?
		LIGHT_POS(dynlights->nb).z = y;
		
		dynlights->p_lspr[dynlights->nb] = newlight;
		
		dynlights->nb++;
	}
}

// --------------------------------------------------------------------------
// Add a light for dynamic lighting
// The light position is already transformed execpt for mlook
// --------------------------------------------------------------------------
void HWR_AddDynShadow(double x, double y, double z, ULONG color, byte radius, gr_vissprite_t *spr)
{
	// Make sure that no more lights than max are used per level
	if (dynshadows->nb < DL_MAX_SHADOW)
	{
		shadow_t   *newshadow = Z_Malloc(sizeof(shadow_t), PU_LEVEL, NULL);
		
		// check if sprite contain dynamic light
		newshadow->dynamic_color = color;
		newshadow->dynamic_radius = radius;
		newshadow->spr = spr;
		
		// NOTE: The coordinates are flipped here for OpenGL
		SHADOW_POS(dynshadows->nb).x = x;
		SHADOW_POS(dynshadows->nb).y = z; // SRB2CBTODO: + lightoffset?
		SHADOW_POS(dynshadows->nb).z = y;
		
		dynshadows->p_sspr[dynshadows->nb] = newshadow;
		
		dynshadows->nb++;
	}
}

static void HWR_SetShadowTexture(gr_vissprite_t *spr)
{
	GLPatch_t *gpatch; // sprite patch converted to hardware
	gpatch = W_CachePatchNum(spr->patchlumpnum, PU_CACHE);
	
	HWR_GetMappedPatch(gpatch, spr->colormap);
}


#endif // HWRENDER
