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
/// \brief Dynamic lighting & coronas

#ifndef _HW_LIGHTS_
#define _HW_LIGHTS_

#include "hw_glob.h"
#include "hw_defs.h"

// Maximum number of lights per frame (extra lights are ignored)
#define DL_MAX_LIGHT 256
#define DL_MAX_SHADOW 256

void HWR_AddDynLight(double x, double y, double z, ULONG color, byte radius);
void HWR_AddDynShadow(double x, double y, double z, ULONG color, byte radius, gr_vissprite_t *spr);
void HWR_PlaneLighting(FOutVector *clVerts, int nrClipVerts);
void HWR_RenderFloorSplat(FOutVector *clVerts, int nrClipVerts);
void HWR_WallShading(FOutVector *wlVerts);
void HWR_WallLighting(FOutVector *wlVerts);
void HWR_SpriteLighting(FOutVector *wlVerts);
void HWR_ResetLights(void);
void HWR_ResetShadows(void);
void HWR_SetLights(int viewnumber);
void HWR_SetShadows(int viewnumber);
void HWR_DrawCorona(double x, double y, double z, ULONG color, double size);

// OpenGL dynamic light support
typedef struct light_s
{
	float light_xoffset;
	float light_yoffset;  // y offset to adjust corona's height

	ULONG dynamic_color;  // color of the light for dynamic lighting
	float dynamic_radius; // radius of the light ball
} light_t;

// OpenGL dynamic shadow support
typedef struct shadow_s
{
	ULONG dynamic_color;  // color of the light for dynamic lighting
	float dynamic_radius; // radius of the light ball
	gr_vissprite_t *spr; // Store data for a sprite
} shadow_t;

// SRB2CBTODO: What is with lightmaps? Use these for better lighted maps
typedef struct lightmap_s
{
	float s[2], t[2];
	light_t *light;
	struct lightmap_s *next;
} lightmap_t;

typedef struct
{
	int nb; // SRB2CBTODO: What is this?
	light_t *p_lspr[DL_MAX_LIGHT]; // SRB2CBTODO: Rename
	FVector position[DL_MAX_LIGHT]; // actually maximum DL_MAX_LIGHT lights
} dynlights_t;

typedef struct
{
	int nb;
	shadow_t *p_sspr[DL_MAX_LIGHT];
	FVector position[DL_MAX_LIGHT]; // actually maximum DL_MAX_LIGHT lights
} dynshadows_t;

#endif
