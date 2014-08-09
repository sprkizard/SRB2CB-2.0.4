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
/// \brief MD2 Handling
///	Inspired from md2.c by Mete Ciragan (mete@swissquake.ch)


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifdef HAVE_PNG
#include "png.h"
#endif

#include "../doomdef.h"

#ifdef HWRENDER
#include "hw_drv.h"
#include "hw_light.h"
#include "hw_md2.h"

#include "../r_main.h"
#include "../r_things.h"
#include "../r_bsp.h"
#include "../m_misc.h"
#include "../w_wad.h"
#include "../z_zone.h"
#include "hw_main.h"
#include "../d_main.h" // For proper file loading
#include "../doomstat.h" // For devmode

md2_t md2_models[NUMSPRITES];
md2_t md2_playermodels[MAXSKINS];
const char *homedir = NULL;

/*
 * free model
 */
void MD2_FreeModel(md2_model_t *model)
{
	if (model)
	{
		if (model->skins)
			free(model->skins);
		
		if (model->texCoords)
			free(model->texCoords);
		
		if (model->triangles)
			free(model->triangles);
		
		if (model->frames)
		{
			size_t i;
			
			for (i = 0; i < model->header.numFrames; i++)
			{
				if (model->frames[i].vertices)
					free(model->frames[i].vertices);
			}
			free(model->frames);
		}
		
		if (model->glCommandBuffer)
			free(model->glCommandBuffer);
		
		free(model);
	}
}


#define NUMVERTEXNORMALS 162 // For interpoliation and lighting on models
float avertexnormals[NUMVERTEXNORMALS][3] = {
	{-0.525731f, 0.000000f, 0.850651f},
	{-0.442863f, 0.238856f, 0.864188f},
	{-0.295242f, 0.000000f, 0.955423f},
	{-0.309017f, 0.500000f, 0.809017f},
	{-0.162460f, 0.262866f, 0.951056f},
	{0.000000f, 0.000000f, 1.000000f},
	{0.000000f, 0.850651f, 0.525731f},
	{-0.147621f, 0.716567f, 0.681718f},
	{0.147621f, 0.716567f, 0.681718f},
	{0.000000f, 0.525731f, 0.850651f},
	{0.309017f, 0.500000f, 0.809017f},
	{0.525731f, 0.000000f, 0.850651f},
	{0.295242f, 0.000000f, 0.955423f},
	{0.442863f, 0.238856f, 0.864188f},
	{0.162460f, 0.262866f, 0.951056f},
	{-0.681718f, 0.147621f, 0.716567f},
	{-0.809017f, 0.309017f, 0.500000f},
	{-0.587785f, 0.425325f, 0.688191f},
	{-0.850651f, 0.525731f, 0.000000f},
	{-0.864188f, 0.442863f, 0.238856f},
	{-0.716567f, 0.681718f, 0.147621f},
	{-0.688191f, 0.587785f, 0.425325f},
	{-0.500000f, 0.809017f, 0.309017f},
	{-0.238856f, 0.864188f, 0.442863f},
	{-0.425325f, 0.688191f, 0.587785f},
	{-0.716567f, 0.681718f, -0.147621f},
	{-0.500000f, 0.809017f, -0.309017f},
	{-0.525731f, 0.850651f, 0.000000f},
	{0.000000f, 0.850651f, -0.525731f},
	{-0.238856f, 0.864188f, -0.442863f},
	{0.000000f, 0.955423f, -0.295242f},
	{-0.262866f, 0.951056f, -0.162460f},
	{0.000000f, 1.000000f, 0.000000f},
	{0.000000f, 0.955423f, 0.295242f},
	{-0.262866f, 0.951056f, 0.162460f},
	{0.238856f, 0.864188f, 0.442863f},
	{0.262866f, 0.951056f, 0.162460f},
	{0.500000f, 0.809017f, 0.309017f},
	{0.238856f, 0.864188f, -0.442863f},
	{0.262866f, 0.951056f, -0.162460f},
	{0.500000f, 0.809017f, -0.309017f},
	{0.850651f, 0.525731f, 0.000000f},
	{0.716567f, 0.681718f, 0.147621f},
	{0.716567f, 0.681718f, -0.147621f},
	{0.525731f, 0.850651f, 0.000000f},
	{0.425325f, 0.688191f, 0.587785f},
	{0.864188f, 0.442863f, 0.238856f},
	{0.688191f, 0.587785f, 0.425325f},
	{0.809017f, 0.309017f, 0.500000f},
	{0.681718f, 0.147621f, 0.716567f},
	{0.587785f, 0.425325f, 0.688191f},
	{0.955423f, 0.295242f, 0.000000f},
	{1.000000f, 0.000000f, 0.000000f},
	{0.951056f, 0.162460f, 0.262866f},
	{0.850651f, -0.525731f, 0.000000f},
	{0.955423f, -0.295242f, 0.000000f},
	{0.864188f, -0.442863f, 0.238856f},
	{0.951056f, -0.162460f, 0.262866f},
	{0.809017f, -0.309017f, 0.500000f},
	{0.681718f, -0.147621f, 0.716567f},
	{0.850651f, 0.000000f, 0.525731f},
	{0.864188f, 0.442863f, -0.238856f},
	{0.809017f, 0.309017f, -0.500000f},
	{0.951056f, 0.162460f, -0.262866f},
	{0.525731f, 0.000000f, -0.850651f},
	{0.681718f, 0.147621f, -0.716567f},
	{0.681718f, -0.147621f, -0.716567f},
	{0.850651f, 0.000000f, -0.525731f},
	{0.809017f, -0.309017f, -0.500000f},
	{0.864188f, -0.442863f, -0.238856f},
	{0.951056f, -0.162460f, -0.262866f},
	{0.147621f, 0.716567f, -0.681718f},
	{0.309017f, 0.500000f, -0.809017f},
	{0.425325f, 0.688191f, -0.587785f},
	{0.442863f, 0.238856f, -0.864188f},
	{0.587785f, 0.425325f, -0.688191f},
	{0.688191f, 0.587785f, -0.425325f},
	{-0.147621f, 0.716567f, -0.681718f},
	{-0.309017f, 0.500000f, -0.809017f},
	{0.000000f, 0.525731f, -0.850651f},
	{-0.525731f, 0.000000f, -0.850651f},
	{-0.442863f, 0.238856f, -0.864188f},
	{-0.295242f, 0.000000f, -0.955423f},
	{-0.162460f, 0.262866f, -0.951056f},
	{0.000000f, 0.000000f, -1.000000f},
	{0.295242f, 0.000000f, -0.955423f},
	{0.162460f, 0.262866f, -0.951056f},
	{-0.442863f, -0.238856f, -0.864188f},
	{-0.309017f, -0.500000f, -0.809017f},
	{-0.162460f, -0.262866f, -0.951056f},
	{0.000000f, -0.850651f, -0.525731f},
	{-0.147621f, -0.716567f, -0.681718f},
	{0.147621f, -0.716567f, -0.681718f},
	{0.000000f, -0.525731f, -0.850651f},
	{0.309017f, -0.500000f, -0.809017f},
	{0.442863f, -0.238856f, -0.864188f},
	{0.162460f, -0.262866f, -0.951056f},
	{0.238856f, -0.864188f, -0.442863f},
	{0.500000f, -0.809017f, -0.309017f},
	{0.425325f, -0.688191f, -0.587785f},
	{0.716567f, -0.681718f, -0.147621f},
	{0.688191f, -0.587785f, -0.425325f},
	{0.587785f, -0.425325f, -0.688191f},
	{0.000000f, -0.955423f, -0.295242f},
	{0.000000f, -1.000000f, 0.000000f},
	{0.262866f, -0.951056f, -0.162460f},
	{0.000000f, -0.850651f, 0.525731f},
	{0.000000f, -0.955423f, 0.295242f},
	{0.238856f, -0.864188f, 0.442863f},
	{0.262866f, -0.951056f, 0.162460f},
	{0.500000f, -0.809017f, 0.309017f},
	{0.716567f, -0.681718f, 0.147621f},
	{0.525731f, -0.850651f, 0.000000f},
	{-0.238856f, -0.864188f, -0.442863f},
	{-0.500000f, -0.809017f, -0.309017f},
	{-0.262866f, -0.951056f, -0.162460f},
	{-0.850651f, -0.525731f, 0.000000f},
	{-0.716567f, -0.681718f, -0.147621f},
	{-0.716567f, -0.681718f, 0.147621f},
	{-0.525731f, -0.850651f, 0.000000f},
	{-0.500000f, -0.809017f, 0.309017f},
	{-0.238856f, -0.864188f, 0.442863f},
	{-0.262866f, -0.951056f, 0.162460f},
	{-0.864188f, -0.442863f, 0.238856f},
	{-0.809017f, -0.309017f, 0.500000f},
	{-0.688191f, -0.587785f, 0.425325f},
	{-0.681718f, -0.147621f, 0.716567f},
	{-0.442863f, -0.238856f, 0.864188f},
	{-0.587785f, -0.425325f, 0.688191f},
	{-0.309017f, -0.500000f, 0.809017f},
	{-0.147621f, -0.716567f, 0.681718f},
	{-0.425325f, -0.688191f, 0.587785f},
	{-0.162460f, -0.262866f, 0.951056f},
	{0.442863f, -0.238856f, 0.864188f},
	{0.162460f, -0.262866f, 0.951056f},
	{0.309017f, -0.500000f, 0.809017f},
	{0.147621f, -0.716567f, 0.681718f},
	{0.000000f, -0.525731f, 0.850651f},
	{0.425325f, -0.688191f, 0.587785f},
	{0.587785f, -0.425325f, 0.688191f},
	{0.688191f, -0.587785f, 0.425325f},
	{-0.955423f, 0.295242f, 0.000000f},
	{-0.951056f, 0.162460f, 0.262866f},
	{-1.000000f, 0.000000f, 0.000000f},
	{-0.850651f, 0.000000f, 0.525731f},
	{-0.955423f, -0.295242f, 0.000000f},
	{-0.951056f, -0.162460f, 0.262866f},
	{-0.864188f, 0.442863f, -0.238856f},
	{-0.951056f, 0.162460f, -0.262866f},
	{-0.809017f, 0.309017f, -0.500000f},
	{-0.864188f, -0.442863f, -0.238856f},
	{-0.951056f, -0.162460f, -0.262866f},
	{-0.809017f, -0.309017f, -0.500000f},
	{-0.681718f, 0.147621f, -0.716567f},
	{-0.681718f, -0.147621f, -0.716567f},
	{-0.850651f, 0.000000f, -0.525731f},
	{-0.688191f, 0.587785f, -0.425325f},
	{-0.587785f, 0.425325f, -0.688191f},
	{-0.425325f, 0.688191f, -0.587785f},
	{-0.425325f, -0.688191f, -0.587785f},
	{-0.587785f, -0.425325f, -0.688191f},
	{-0.688191f, -0.587785f, -0.425325f},
};


//
// load model
//
// The current directory is the game's program path
md2_model_t *MD2_ReadModel(const char *filename)
{
	FILE *file;
	
#ifdef WADMOD
	lumpnum_t lumpnum;
	ULONG lumppos;
#endif
	
	md2_model_t *model;
	byte buffer[MD2_MAX_FRAMESIZE];
	size_t i;
	
	model = calloc(1, sizeof(*model));
	
	if (model == NULL)
		return 0;
	
#ifdef WADMOD // Should be able to be its own option
	lumpnum = W_CheckNumForName(filename);
	if (lumpnum == LUMPERROR)
	{
		free(model);
		return 0;
	}
	lumppos = W_LumpPos(lumpnum);
	file = W_GetWadHandleForLumpNum(lumpnum);
#endif
	
	homedir = D_Home();
	if (homedir)
		file = fopen(va("%s/"DEFAULTDIR"/%s", homedir, filename), "rt");
	else
		file = fopen(filename, "rb");
	
	if (!file)
	{
		CONS_Printf("MD2 file: %s not found!\n", filename);
		
		free(model);
		return 0;
	}
	
#ifdef WADMOD
	// move the wad file pos to the start of the lump.
	fseek(file, lumppos, SEEK_SET);
#endif
	
	// initialize model and read header
	
	if (fread(&model->header, sizeof (model->header), 1, file) != 1
		|| model->header.magic !=
		(int)(('2' << 24) + ('P' << 16) + ('D' << 8) + 'I'))
	{
#ifndef WADMOD // Don't close the wadfile.
		fclose(file);
#endif
		free(model);
		return 0;
	}
	
	model->header.numSkins = 1;
	
	// read skins
#ifdef WADMOD
	fseek(file, lumppos+model->header.offsetSkins, SEEK_SET);
#else
	fseek(file, model->header.offsetSkins, SEEK_SET);
#endif
	if (model->header.numSkins > 0)
	{
		model->skins = calloc(sizeof (md2_skin_t), model->header.numSkins);
		if (!model->skins || model->header.numSkins !=
			fread(model->skins, sizeof (md2_skin_t), model->header.numSkins, file))
		{
			MD2_FreeModel(model);
			return 0;
		}
		
		;
	}
	
	// read texture coordinates
#ifdef WADMOD
	fseek(file, lumppos+model->header.offsetTexCoords, SEEK_SET);
#else
	fseek(file, model->header.offsetTexCoords, SEEK_SET);
#endif
	if (model->header.numTexCoords > 0)
	{
		model->texCoords = calloc(sizeof (md2_textureCoordinate_t), model->header.numTexCoords);
		if (!model->texCoords || model->header.numTexCoords !=
			fread(model->texCoords, sizeof (md2_textureCoordinate_t), model->header.numTexCoords, file))
		{
			MD2_FreeModel(model);
			return 0;
		}
		
		
	}
	
	// read triangles
#ifdef WADMOD
	fseek(file, lumppos+model->header.offsetTriangles, SEEK_SET);
#else
	fseek(file, model->header.offsetTriangles, SEEK_SET);
#endif
	if (model->header.numTriangles > 0)
	{
		model->triangles = calloc(sizeof (md2_triangle_t), model->header.numTriangles);
		if (!model->triangles || model->header.numTriangles !=
			fread(model->triangles, sizeof (md2_triangle_t), model->header.numTriangles, file))
		{
			MD2_FreeModel(model);
			return 0;
		}
	}
	
	// read alias frames
#ifdef WADMOD
	fseek(file, lumppos+model->header.offsetFrames, SEEK_SET);
#else
	fseek(file, model->header.offsetFrames, SEEK_SET);
#endif
	if (model->header.numFrames > 0)
	{
		model->frames = calloc(sizeof (md2_frame_t), model->header.numFrames);
		if (!model->frames)
		{
			MD2_FreeModel(model);
			return 0;
		}
		
		for (i = 0; i < model->header.numFrames; i++)
		{
			md2_alias_frame_t *frame = (md2_alias_frame_t *)(void *)buffer;
			size_t j;
			
			model->frames[i].vertices = calloc(sizeof (md2_triangleVertex_t), model->header.numVertices);
			if (!model->frames[i].vertices || model->header.frameSize !=
				fread(frame, 1, model->header.frameSize, file))
			{
				MD2_FreeModel(model);
				return 0;
			}
			
			strcpy(model->frames[i].name, frame->name);
			for (j = 0; j < model->header.numVertices; j++)
			{
				model->frames[i].vertices[j].vertex[0] = (float)((int) frame->alias_vertices[j].vertex[0]) * frame->scale[0] + frame->translate[0];
				model->frames[i].vertices[j].vertex[2] = -1* ((float)((int) frame->alias_vertices[j].vertex[1]) * frame->scale[1] + frame->translate[1]);
				model->frames[i].vertices[j].vertex[1] = (float)((int) frame->alias_vertices[j].vertex[2]) * frame->scale[2] + frame->translate[2];
				model->frames[i].vertices[j].normal[0] = avertexnormals[frame->alias_vertices[j].lightNormalIndex][0];
				model->frames[i].vertices[j].normal[1] = avertexnormals[frame->alias_vertices[j].lightNormalIndex][1];
				model->frames[i].vertices[j].normal[2] = avertexnormals[frame->alias_vertices[j].lightNormalIndex][2];
			}
		}
	}
	
	// Read the OpenGL commands
#ifdef WADMOD
	fseek(file, lumppos+model->header.offsetGlCommands, SEEK_SET);
#else
	fseek(file, model->header.offsetGlCommands, SEEK_SET);
#endif
	if (model->header.numGlCommands)
	{
		model->glCommandBuffer = calloc(sizeof (int), model->header.numGlCommands);
		if (!model->glCommandBuffer || model->header.numGlCommands !=
			fread(model->glCommandBuffer, sizeof (int), model->header.numGlCommands, file))
		{
			MD2_FreeModel(model);
			return 0;
		}
	}
	
#ifndef WADMOD // Don't close the wadfile
	fclose(file);
#endif
	
	return model;
}

/*
 * center model
 */
void MD2_GetBoundingBox(md2_model_t *model, float *minmax)
{
	size_t i;
	float minx, maxx;
	float miny, maxy;
	float minz, maxz;
	
	minx = miny = minz = 999999.0f;
	maxx = maxy = maxz = -999999.0f;
	
	/* get bounding box */
	for (i = 0; i < model->header.numVertices; i++)
	{
		md2_triangleVertex_t *v = &model->frames[0].vertices[i];
		
		if (v->vertex[0] < minx)
			minx = v->vertex[0];
		else if (v->vertex[0] > maxx)
			maxx = v->vertex[0];
		
		if (v->vertex[1] < miny)
			miny = v->vertex[1];
		else if (v->vertex[1] > maxy)
			maxy = v->vertex[1];
		
		if (v->vertex[2] < minz)
			minz = v->vertex[2];
		else if (v->vertex[2] > maxz)
			maxz = v->vertex[2];
	}
	
	minmax[0] = minx;
	minmax[1] = maxx;
	minmax[2] = miny;
	minmax[3] = maxy;
	minmax[4] = minz;
	minmax[5] = maxz;
}

int MD2_GetAnimationCount(md2_model_t *model)
{
	size_t i, pos;
	int j = 0, count;
	char name[16], last[16];
	
	strcpy(last, model->frames[0].name);
	pos = strlen(last) - 1;
	while (last[pos] >= '0' && last[pos] <= '9' && j < 2)
	{
		pos--;
		j++;
	}
	last[pos + 1] = '\0';
	
	count = 0;
	
	for (i = 0; i <= model->header.numFrames; i++)
	{
		if (i == model->header.numFrames)
			strcpy(name, ""); // some kind of a sentinel
		else
			strcpy(name, model->frames[i].name);
		pos = strlen(name) - 1;
		j = 0;
		while (name[pos] >= '0' && name[pos] <= '9' && j < 2)
		{
			pos--;
			j++;
		}
		name[pos + 1] = '\0';
		
		if (strcmp(last, name))
		{
			strcpy(last, name);
			count++;
		}
	}
	
	return count;
}

const char *MD2_GetAnimationName(md2_model_t *model, int animation)
{
	size_t i, pos;
	int j = 0, count;
	static char last[32];
	char name[32];
	
	strcpy(last, model->frames[0].name);
	pos = strlen(last) - 1;
	while (last[pos] >= '0' && last[pos] <= '9' && j < 2)
	{
		pos--;
		j++;
	}
	last[pos + 1] = '\0';
	
	count = 0;
	
	for (i = 0; i <= model->header.numFrames; i++)
	{
		if (i == model->header.numFrames)
			strcpy(name, ""); // some kind of a sentinel
		else
			strcpy(name, model->frames[i].name);
		pos = strlen(name) - 1;
		j = 0;
		while (name[pos] >= '0' && name[pos] <= '9' && j < 2)
		{
			pos--;
			j++;
		}
		name[pos + 1] = '\0';
		
		if (strcmp(last, name))
		{
			if (count == animation)
				return last;
			
			strcpy(last, name);
			count++;
		}
	}
	
	return 0;
}

void MD2_GetAnimationFrames(md2_model_t *model,
							int animation, int *startFrame, int *endFrame)
{
	size_t i, pos;
	int j = 0, count, numFrames, frameCount;
	char name[16], last[16];
	
	strcpy(last, model->frames[0].name);
	pos = strlen(last) - 1;
	while (last[pos] >= '0' && last[pos] <= '9' && j < 2)
	{
		pos--;
		j++;
	}
	last[pos + 1] = '\0';
	
	count = 0;
	numFrames = 0;
	frameCount = 0;
	
	for (i = 0; i <= model->header.numFrames; i++)
	{
		if (i == model->header.numFrames)
			strcpy(name, ""); // some kind of a sentinel
		else
			strcpy(name, model->frames[i].name);
		pos = strlen(name) - 1;
		j = 0;
		while (name[pos] >= '0' && name[pos] <= '9' && j < 2)
		{
			pos--;
			j++;
		}
		name[pos + 1] = '\0';
		
		if (strcmp(last, name))
		{
			strcpy(last, name);
			
			if (count == animation)
			{
				*startFrame = frameCount - numFrames;
				*endFrame = frameCount - 1;
				return;
			}
			
			count++;
			numFrames = 0;
		}
		frameCount++;
		numFrames++;
	}
	*startFrame = *endFrame = 0;
}

#ifdef HAVE_PNG

// Loads a PNG file then converts it into hardware's format
// SRB2CBTODO: Make a version for general use throughout the code
GLTextureFormat_t HWR_LoadMD2PNG(const char *filename, int *w, int *h, GLPatch_t *grpatch)
{
	png_structp png_ptr;
	png_infop png_info_ptr;
	png_uint_32 width, height;
	int bit_depth, color_type;
#ifdef PNG_SETJMP_SUPPORTED
#ifdef USE_FAR_KEYWORD
	jmp_buf jmpbuf;
#endif
#endif
	png_FILE_p png_FILE;
	homedir = D_Home();
	char *pngfilename;
	
	if (homedir)
		pngfilename = va("%s/"DEFAULTDIR"/md2/%s", homedir, filename);
	else
		pngfilename = va("md2/%s", filename);
	
#ifdef WADTEX // This can be the main method alternative
	lumpnum_t lumpnum;
	ULONG lumppos;
	
	lumpnum = W_CheckNumForName(filename);
	if (lumpnum == LUMPERROR)
		return 0;
	
	// Check its a PNG image.
	if (W_LumpIsPng(lumpnum) == false)
	{
		return 0;
	}
	
	lumppos = W_LumpPos(lumpnum);
	png_FILE = W_GetWadHandleForLumpNum(lumpnum);
#endif
	
	FIL_ForceExtension(pngfilename, ".png");
	png_FILE = fopen(pngfilename, "rb");
	if (!png_FILE)
	{
		CONS_Printf("Error: MD2 %s does not have a texture\n", filename);
		return 0;
	}
	
#ifdef WADTEX
	// move the wad file pos to the start.
	fseek(png_FILE, lumppos, SEEK_SET);
#endif
	
	png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL,
									 PNG_error, PNG_warn);
	if (!png_ptr)
	{
		CONS_Printf("HWR_LoadMD2PNG: Error on initialize libpng\n");
#ifndef WADTEX
		fclose(png_FILE);
#endif
		return 0;
	}
	
	png_info_ptr = png_create_info_struct(png_ptr);
	if (!png_info_ptr)
	{
		CONS_Printf("HWR_LoadMD2PNG: Error on allocate for libpng\n");
		png_destroy_read_struct(&png_ptr, NULL, NULL);
#ifndef WADTEX
		fclose(png_FILE);
#endif
		return 0;
	}
	
#ifdef USE_FAR_KEYWORD
	if (setjmp(jmpbuf))
#else
		if (setjmp(png_jmpbuf(png_ptr)))
#endif
		{
			//CONS_Printf("libpng load error on %s\n", filename);
			png_destroy_read_struct(&png_ptr, &png_info_ptr, NULL);
#ifndef WADTEX
			fclose(png_FILE);
#endif
#ifdef GLFUN
			Z_Free(grMipmap->glInfo.data);
#else
			Z_Free(grpatch->mipmap.glInfo.data);
#endif
			return 0;
		}
#ifdef USE_FAR_KEYWORD
	png_memcpy(png_jmpbuf(png_ptr), jmpbuf, sizeof jmp_buf);
#endif
	
	png_init_io(png_ptr, png_FILE);
	
#ifdef PNG_SET_USER_LIMITS_SUPPORTED
	png_set_user_limits(png_ptr, 2048, 2048);
#endif
	
	png_read_info(png_ptr, png_info_ptr);
	
	png_get_IHDR(png_ptr, png_info_ptr, &width, &height, &bit_depth, &color_type,
				 NULL, NULL, NULL);
	
	if (bit_depth == 16)
		png_set_strip_16(png_ptr);
	
	if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
		png_set_gray_to_rgb(png_ptr);
	else if (color_type == PNG_COLOR_TYPE_PALETTE)
		png_set_palette_to_rgb(png_ptr);
	
	if (png_get_valid(png_ptr, png_info_ptr, PNG_INFO_tRNS))
		png_set_tRNS_to_alpha(png_ptr);
	else if (color_type != PNG_COLOR_TYPE_RGB_ALPHA && color_type != PNG_COLOR_TYPE_GRAY_ALPHA)
	{
#if PNG_LIBPNG_VER < 10207
		png_set_filler(png_ptr, 0xFF, PNG_FILLER_AFTER);
#else
		png_set_add_alpha(png_ptr, 0xFF, PNG_FILLER_AFTER);
#endif
	}
	
	png_read_update_info(png_ptr, png_info_ptr);
	
	{
		png_uint_32 i, pitch = png_get_rowbytes(png_ptr, png_info_ptr);
#ifdef GLFUN
		png_bytep PNG_image = Z_Malloc(pitch*height, PU_HWRCACHE, &grMipmap->glInfo.data);
#else
		png_bytep PNG_image = Z_Malloc(pitch*height, PU_HWRCACHE, &grpatch->mipmap.glInfo.data);
#endif
		png_bytepp row_pointers = png_malloc(png_ptr, height * sizeof (png_bytep));
		for (i = 0; i < height; i++)
			row_pointers[i] = PNG_image + i*pitch;
		png_read_image(png_ptr, row_pointers);
		png_free(png_ptr, (png_voidp)row_pointers);
	}
	
	png_destroy_read_struct(&png_ptr, &png_info_ptr, NULL);
	
#ifndef WADTEX
	fclose(png_FILE);
#endif
	*w = width;
	*h = height;
	return GR_RGBA;
}





#endif  // HAVE_PNG







// SRB2CB: No one cares about PCX :P
typedef struct
{
	byte manufacturer;
	byte version;
	byte encoding;
	byte bitsPerPixel;
	short xmin;
	short ymin;
	short xmax;
	short ymax;
	short hDpi;
	short vDpi;
	byte colorMap[48];
	byte reserved;
	byte numPlanes;
	short bytesPerLine;
	short paletteInfo;
	short hScreenSize;
	short vScreenSize;
	byte filler[54];
} PcxHeader;

static GLTextureFormat_t PCX_Load(const char *filename, int *w, int *h, // SRB2CBTODO: PCX support? lol I don't think so :P
								  GLPatch_t *grpatch)
{
	PcxHeader header;
#define PALSIZE 768
	byte palette[PALSIZE];
	const byte *pal;
	RGBA_t *image;
	size_t pw, ph, size, ptr = 0;
	int ch, rep;
	FILE *file;
	
	homedir = D_Home();
	char *pcxfilename;
	
	if (homedir)
		pcxfilename = va("%s/"DEFAULTDIR"/md2/%s", homedir, filename);
	else
		pcxfilename = va("md2/%s", filename);
	
	FIL_ForceExtension(pcxfilename, ".pcx");
	file = fopen(pcxfilename, "rb");
	if (!file)
		return 0;
	
	if (fread(&header, sizeof (PcxHeader), 1, file) != 1)
	{
		fclose(file);
		return 0;
	}
	
	if (header.bitsPerPixel != 8)
	{
		fclose(file);
		return 0;
	}
	
	fseek(file, -PALSIZE, SEEK_END);
	
	pw = *w = header.xmax - header.xmin + 1;
	ph = *h = header.ymax - header.ymin + 1;
	image = Z_Malloc(pw*ph*4, PU_HWRCACHE, &grpatch->mipmap.glInfo.data);
	
	if (fread(palette, sizeof (byte), PALSIZE, file) != PALSIZE)
	{
		Z_Free(image);
		fclose(file);
		return 0;
	}
	fseek(file, sizeof (PcxHeader), SEEK_SET);
	
	size = pw * ph;
	while (ptr < size)
	{
		ch = fgetc(file);
		if (ch >= 192)
		{
			rep = ch - 192;
			ch = fgetc(file);
		}
		else
		{
			rep = 1;
		}
		while (rep--)
		{
			pal = palette + ch*3;
			image[ptr].s.red   = *pal++;
			image[ptr].s.green = *pal++;
			image[ptr].s.blue  = *pal++;
			image[ptr].s.alpha = 0xFF;
			ptr++;
		}
	}
	fclose(file);
	return GR_RGBA;
}

// -----------------+
// MD2_LoadTexture  : Download a png texture for MD2 models
// -----------------+
void MD2_LoadTexture(md2_t *model)
{
	GLPatch_t *grpatch;
	const char *filename = model->filename;
	
	if (model->grpatch)
	{
		grpatch = model->grpatch;
		Z_Free(grpatch->mipmap.glInfo.data);
	}
	else
		grpatch = Z_Calloc(sizeof *grpatch, PU_HWRPATCHINFO,
		                   &(model->grpatch));
	
	if (!grpatch->mipmap.downloaded && !grpatch->mipmap.glInfo.data)
	{
		int w = 0, h = 0;
#ifdef HAVE_PNG
		grpatch->mipmap.glInfo.format = HWR_LoadMD2PNG(filename, &w, &h, grpatch);
		
		if (grpatch->mipmap.glInfo.format == 0)
#endif
			grpatch->mipmap.glInfo.format = PCX_Load(filename, &w, &h, grpatch);
		
		if (grpatch->mipmap.glInfo.format == 0)
		{
			model->notfound = true;
			return;
		}
		
		grpatch->mipmap.downloaded = 0;
		grpatch->mipmap.flags = 0;
		
		grpatch->width = (short)w;
		grpatch->height = (short)h;
		grpatch->mipmap.width = (USHORT)w;
		grpatch->mipmap.height = (USHORT)h;
		
		// SRB2CBTODO: We DONT CARE about this part at all, 
		// this is only for glide renderer support, which is not happening
		//grpatch->mipmap.glInfo.smallLodLog2 = GR_LOD_LOG2_256;
		//grpatch->mipmap.glInfo.largeLodLog2 = GR_LOD_LOG2_256;
		//grpatch->mipmap.glInfo.aspectRatioLog2 = GR_ASPECT_LOG2_1x1;
	}
	
	GL_SetTexture(&grpatch->mipmap, true);
	
	// Set the md2's texutre value so we can set up other things!
	FTextureInfo *pTexInfo = &grpatch->mipmap;
	if (!pTexInfo)
	{
		return;
	}
	else if (pTexInfo->downloaded)
		model->texture = pTexInfo->downloaded;
}

void HWR_InitMD2(void)
{
	size_t i;
	int s;
	FILE *f;
	// name[18] is used to check for names in the md2.dat file that match with sprites or player skins
	// sprite names are always 4 characters long, and names is for player skins can be up to 19 characters long
	char name[18], filename[32];
	float scale, offset;
	
	CONS_Printf("InitMD2()...\n");
	// Check for any MD2s that match the names of player skins! 
	for (s = 0; s < MAXSKINS; s++)
	{
		md2_playermodels[s].scale = -1.0f;
		md2_playermodels[s].model = NULL;
		md2_playermodels[s].grpatch = NULL;
		md2_playermodels[s].skin = -1; // Yup, get the skin number to look up for skins[i]
		md2_playermodels[s].notfound = true;
	}
	for (i = 0; i < NUMSPRITES; i++)
	{
		md2_models[i].scale = -1.0f;
		md2_models[i].model = NULL;
		md2_models[i].grpatch = NULL;
		md2_models[i].skin = -1; // Yup, get the skin number to look up for skins[i]
		md2_models[i].notfound = true;
	}
	
	// Read the md2.dat file
	
	homedir = D_Home();
	if (homedir)
		f = fopen(va("%s/"DEFAULTDIR"/md2.dat", homedir), "rt");
	else
		f = fopen("md2.dat", "rt");
	
	if (!f)
	{
		CONS_Printf("Error while loading md2.dat\n");
		return;
	}
	
	md2_t *md2;
	int *buff = 0; // SRB2CBTODO: Check buff
	
	
	// Even though MD2's can be dynamically added, we still need to check at startup
	// for MD2s associated with any default sprites and skins 
	while (fscanf(f, "%19s %31s %f %f", name, filename, &scale, &offset) == 4)
	{
		for (i = 0; i < NUMSPRITES; i++)
		{
			if (stricmp(name, sprnames[i]) == 0)
			{
				if (stricmp(name, "PLAY") == 0) // Handled already NEWMD2: Per sprite, per-skin check
					continue;
				
				md2_models[i].scale = scale;
				md2_models[i].offset = offset;
				md2_models[i].notfound = false;
				strcpy(md2_models[i].filename, filename);
				md2 = &md2_models[i];
				if (!md2->model)
				{
					char filenamet[64];
					sprintf(filenamet, "md2/%s", md2->filename);
					md2->model = MD2_ReadModel(filenamet);
					buff = md2->model->glCommandBuffer;
				}
				///GL_BuildMD2Lists(buff, md2); // Build shadows for the MD2 a la SSNTail's Wizard engine
				break;
			}
			
			if (i == NUMSPRITES)
			{
				CONS_Printf("MD2 for sprite %s not found\n", name);
				md2_models[i].notfound = true;
			}
		}
		
		for (s = 0; s < MAXSKINS; s++)
		{
			if (stricmp(name, skins[s].name) == 0)
			{
				md2_playermodels[s].skin = s;
				md2_playermodels[s].scale = scale;
				md2_playermodels[s].offset = offset;
				md2_playermodels[s].notfound = false;
				strcpy(md2_playermodels[s].filename, filename);
				md2 = &md2_playermodels[s];
				// the skin will be -1 for now, it's handled in HWR_DrawMD2 // md2->skin = s;
				if (!md2->model)
				{
					char filenamet[64];
					sprintf(filenamet, "md2/%s", md2->filename);
					md2->model = MD2_ReadModel(filenamet);
					buff = md2->model->glCommandBuffer;
				}
				//GL_BuildMD2Lists(buff, md2);
				break;
			}
			
			if (s == MAXSKINS-1)
			{
				CONS_Printf("MD2 for player skin %s not found\n", name);
				md2_playermodels[i].notfound = true;
			}
			
		}
	}
	
	fclose(f);
}


void HWR_AddPlayerMD2(int skin) // For MD2s that were added after startup
{
	FILE *f;
	// name[18] is used to check for names in the md2.dat file that match with sprites or player skins
	// sprite names are always 4 characters long, and names is for player skins can be up to 19 characters long
	char name[18], filename[32];
	float scale, offset;
	
	CONS_Printf("AddPlayerMD2()...\n");
	
	// Read the md2.dat file
	
	homedir = D_Home();
	if (homedir)
		f = fopen(va("%s/"DEFAULTDIR"/md2.dat", homedir), "rt");
	else
		f = fopen("md2.dat", "rt");
	
	if (!f)
	{
		CONS_Printf("Error while loading md2.dat\n");
		return;
	}
	
	// Check for any MD2s that match the names of player skins!
	while (fscanf(f, "%19s %31s %f %f", name, filename, &scale, &offset) == 4)
	{
		if (stricmp(name, skins[skin].name) == 0)
		{
			md2_playermodels[skin].skin = skin;
			md2_playermodels[skin].scale = scale;
			md2_playermodels[skin].offset = offset;
			md2_playermodels[skin].notfound = false;
			strcpy(md2_playermodels[skin].filename, filename);
			break;
		}
		
		if (skin == MAXSKINS-1)
		{
			CONS_Printf("MD2 for player skin %s not found\n", name);
			md2_playermodels[skin].notfound = true;
		}
		
	}
	
	fclose(f);
}



void HWR_AddSpriteMD2(int spritenum) // For MD2s that were added after startup
{
	FILE *f;
	// name[18] is used to check for names in the md2.dat file that match with sprites or player skins
	// sprite names are always 4 characters long, and names is for player skins can be up to 19 characters long
	char name[18], filename[32];
	float scale, offset;
	
	// Read the md2.dat file
	
	homedir = D_Home();
	if (homedir)
		f = fopen(va("%s/"DEFAULTDIR"/md2.dat", homedir), "rt");
	else
		f = fopen("md2.dat", "rt");
	
	if (!f)
	{
		CONS_Printf("Error while loading md2.dat\n");
		return;
	}
	
	// Check for any MD2s that match the names of player skins!
	while (fscanf(f, "%19s %31s %f %f", name, filename, &scale, &offset) == 4)
	{
		{
			if (stricmp(name, sprnames[spritenum]) == 0)
			{
				if (stricmp(name, "PLAY") == 0) // Handled already NEWMD2: Per sprite, per-skin check
					continue;
				
				md2_models[spritenum].scale = scale;
				md2_models[spritenum].offset = offset;
				md2_models[spritenum].notfound = false;
				strcpy(md2_models[spritenum].filename, filename);
				break;
			}
			
			if (spritenum == NUMSPRITES-1)
			{
				CONS_Printf("MD2 for sprite %s not found\n", name);
				md2_models[spritenum].notfound = true;
			}
		}
	}
	
	fclose(f);
}





#endif //HWRENDER
