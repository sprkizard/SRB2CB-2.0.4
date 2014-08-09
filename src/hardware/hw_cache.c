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
//
//-----------------------------------------------------------------------------
/// \file
/// \brief load and convert graphics to the hardware format

#include "../doomdef.h"

#ifdef HWRENDER
#include "hw_glob.h"
#include "hw_drv.h"

#include "../doomstat.h"    // gamemode
#include "../i_video.h"     // rendermode
#include "../r_data.h"
#include "../w_wad.h"
#include "../z_zone.h"
#include "../v_video.h"
#include "../r_draw.h"
#include "../m_misc.h"
#include "../p_setup.h" // For level flats
#include "../w_wad.h"

#ifdef HAVE_PNG
#include "png.h"
#endif

// Values set after a call to HWR_ResizeBlock()
static int blocksize, blockwidth, blockheight;

int patchformat = GR_TEXFMT_AP_88; // use alpha for holes
int textureformat = GR_TEXFMT_P_8; // use chromakey for hole

// sprite, use alpha and chroma key for hole
static void HWR_DrawPatchInCache(GLMipmap_t *mipmap,
	int pblockwidth, int pblockheight, int blockmodulo,
	int ptexturewidth, int ptextureheight,
	int originx, int originy, // where to draw patch in surface block
	const patch_t *realpatch, int bpp)
{
	int x, x1, x2;
	int col, ncols;
	fixed_t xfrac, xfracstep;
	fixed_t yfrac, yfracstep, position, count;
	fixed_t scale_y;
	RGBA_t colortemp;
	byte *dest;
	const byte *source;
	const column_t *patchcol;
	byte alpha;
	byte *block = mipmap->glInfo.data;

	x1 = originx;
	x2 = x1 + SHORT(realpatch->width);

	if (x1 < 0)
		x = 0;
	else
		x = x1;

	if (x2 > ptexturewidth)
		x2 = ptexturewidth;

	if (!ptexturewidth)
		return;

	col = x * pblockwidth / ptexturewidth;
	ncols = ((x2 - x) * pblockwidth) / ptexturewidth;

	// source advance
	xfrac = 0;
	if (x1 < 0)
		xfrac = -x1<<FRACBITS;

	xfracstep = (ptexturewidth << FRACBITS) / pblockwidth;
	yfracstep = (ptextureheight<< FRACBITS) / pblockheight;
	if (bpp < 1 || bpp > 4)
		I_Error("HWR_DrawPatchInCache: no drawer defined for this bpp (%d)\n",bpp);

	for (block += col*bpp; ncols--; block += bpp, xfrac += xfracstep)
	{
		patchcol = (const column_t *)((const byte *)realpatch
		 + LONG(realpatch->columnofs[xfrac>>FRACBITS]));

		scale_y = (pblockheight << FRACBITS) / ptextureheight;

		while (patchcol->topdelta != 0xff)
		{
			source = (const byte *)patchcol + 3;
			count  = ((patchcol->length * scale_y) + (FRACUNIT/2)) >> FRACBITS;
			position = originy + patchcol->topdelta;

			yfrac = 0;
			//yfracstep = (patchcol->length << FRACBITS) / count;
			if (position < 0)
			{
				yfrac = -position<<FRACBITS;
				count += (((position * scale_y) + (FRACUNIT/2)) >> FRACBITS);
				position = 0;
			}

			position = ((position * scale_y) + (FRACUNIT/2)) >> FRACBITS;

			if (position < 0)
				position = 0;

			if (position + count >= pblockheight)
				count = pblockheight - position;

			dest = block + (position*blockmodulo);
			while (count > 0)
			{
				byte texel;
				count--;

				texel = source[yfrac>>FRACBITS];

				alpha = 0xff;

				// Not perfect, but better than holes
				if (texel == HWR_PATCHES_CHROMAKEY_COLORINDEX && (mipmap->flags & TF_CHROMAKEYED))
					texel = HWR_CHROMAKEY_EQUIVALENTCOLORINDEX;
				// Colormap support
				else if (mipmap->colormap)
					texel = mipmap->colormap[texel];

				// Hope the compiler will get this switch out of the loops
				// GCC does it, VCC does not, this game should be compiled with MingW on Windows!
				switch (bpp)
				{
					case 2 : *((USHORT *)dest) = (USHORT)((alpha<<8) | texel);
					         break;
					case 3 : colortemp = V_GetColor(texel);
					         ((RGBA_t *)dest)->s.red   = colortemp.s.red;
					         ((RGBA_t *)dest)->s.green = colortemp.s.green;
					         ((RGBA_t *)dest)->s.blue  = colortemp.s.blue;
					         break;
					case 4 : *((RGBA_t *)dest) = V_GetColor(texel);
					         ((RGBA_t *)dest)->s.alpha = alpha;
					         break;
					// default is 1
					default: *dest = texel; // PNG support
					         break;
				}

				dest += blockmodulo;
				yfrac += yfracstep;
			}
			patchcol = (const column_t *)((const byte *)patchcol + patchcol->length + 4);
		}





	}


}


// Convert a texture to a power of 2 so it displays correctly in OpenGL
// set : blocksize = blockwidth * blockheight  (no bpp used)
//       blockwidth
//       blockheight
// NOTE :  8bit (1 byte per pixel) palettized format
// SRB2CBTODO: Can this function allow for a higher size limit?
static void HWR_ResizeBlock(int originalwidth, int originalheight)
{
	int     j,k;
	int     max,min;

	// Find a power of 2 width/height
	// Round to nearest power of 2
	blockwidth = 1;

	while (blockwidth < originalwidth)
		blockwidth <<= 1;

	// Scale down the graphics to fit in 256
	if (blockwidth > 2048)
		blockwidth = 2048;

	// Round to nearest power of 2
	blockheight = 1;
	while (blockheight < originalheight)
		blockheight <<= 1;
	// Scale down the graphics to fit in 256
	if (blockheight > 2048)
		blockheight = 2048;

	if (blockwidth >= blockheight)
	{
		max = blockwidth;
		min = blockheight;
	}
	else
	{
		max = blockheight;
		min = blockwidth;
	}

	for (k = 2048, j = 0; k > max; j++)
		k>>=1;

	for (k = max, j = 0; k > min && j < 4; j++)
		k>>=1;

	// Aspect ratio too small
	if (j == 4)
	{
		j = 3;

		if (blockwidth < blockheight)
			blockwidth = max>>3;
		else
			blockheight = max>>3;
	}

	blocksize = blockwidth * blockheight;
}


static const int format2bpp[16] =
{
	0, // 0
	0, // 1
	1, // 2  GR_TEXFMT_ALPHA_8
	1, // 3  GR_TEXFMT_INTENSITY_8
	1, // 4  GR_TEXFMT_ALPHA_INTENSITY_44
	1, // 5  GR_TEXFMT_P_8
	4, // 6  GR_RGBA
	0, // 7
	0, // 8
	0, // 9
	2, // 10 GR_TEXFMT_RGB_565
	2, // 11 GR_TEXFMT_ARGB_1555
	2, // 12 GR_TEXFMT_ARGB_4444
	2, // 13 GR_TEXFMT_ALPHA_INTENSITY_88
	2, // 14 GR_TEXFMT_AP_88
};

// Convert a mipmap into a special format Doom graphics format
static byte *HWR_MakeBlock(GLMipmap_t *grMipmap)
{
	int bpp;
	byte *block;
	int i;

	if (!grMipmap || !grMipmap->glInfo.format)
		I_Error("HWR_MakeBlock: A texture attempted to be used doesn't exist or is corrupt\n");

	bpp =  format2bpp[grMipmap->glInfo.format];
	block = Z_Malloc(blocksize*bpp, PU_STATIC, &(grMipmap->glInfo.data));

	switch (bpp)
	{
		case 1: memset(block, HWR_PATCHES_CHROMAKEY_COLORINDEX, blocksize); break;
		case 2:
			// fill background with chromakey, alpha = 0
			for (i = 0; i < blocksize; i++)
				*((USHORT *)block+i) = ((0x00 <<8) | HWR_CHROMAKEY_EQUIVALENTCOLORINDEX);
				break;
		case 4: memset(block,0,blocksize*4); break;
	}

	return block;
}

//
// Create a composite texture from patches, adapt the texture size to a power of 2
// height and width for the hardware texture cache.
//
#if 0
static void HWR_GenerateTexture(int texnum, GLTexture_t *grtex)
{
	byte *block;
	texture_t *texture;
	texpatch_t *patch;
	patch_t *realpatch;

	int i;
	// Special check for the sky texture
	boolean skyspecial = false;

	texture = textures[texnum];

	if (!texture)
		I_Error("HWR_GenerateTexture: Invalid texture num");

	if (!texture->name)
		I_Error("HWR_GenerateTexture: Invalid name");

	if (texture->name && !texture->name[0])
		I_Error("HWR_GenerateTexture: Invalid/Blank name: %s\n", texture->name);

	// Special "on the fly" loaded texture
	// Update the texture's height & width in the case that the user
	// added a bitmap texture that replaced a PNG texture in game
	if (texture->wadlocation != 0 && texture->tx == true)
	{
		// Update the height, width and patches,
		// because these can get out of sync and need rechecking and png recalculating
		patch = texture->patches;
		realpatch = W_CacheLumpNumPwad(texture->wadlocation, patch->patch, PU_CACHE);
		texture->height = realpatch->height;
		texture->width = realpatch->width;
		textureheight[texnum] = realpatch->height<<FRACBITS; // Update the height
	}

	// Sky textures are special, find them by name and convert them
	if (texture->name[0] == 'S' &&
	    texture->name[1] == 'K' &&
	    texture->name[2] == 'Y' &&
	    (texture->name[4] == 0 ||
	     texture->name[5] == 0)
	   )
	{
		skyspecial = true;
		grtex->mipmap.flags = TF_WRAPXY|TF_NOFILTER; // Don't filter the sky
	}
	else
		grtex->mipmap.flags = TF_WRAPXY|TF_CHROMAKEYED;

	HWR_ResizeBlock(texture->width, texture->height);
	grtex->mipmap.width = (USHORT)blockwidth;
	grtex->mipmap.height = (USHORT)blockheight;
	grtex->mipmap.glInfo.format = textureformat;

	block = HWR_MakeBlock(&grtex->mipmap); // SRB2CBTODO: HERE IT IS, rewrite this to allow for PNGs

	// Not efficient, but better than holes in the sky (and it's done only at level loading)
	if (skyspecial)
	{
		int j;
		RGBA_t col;

		col = V_GetColor(HWR_CHROMAKEY_EQUIVALENTCOLORINDEX);
		for (j = 0; j < blockheight; j++)
		{
			for (i = 0; i < blockwidth; i++)
			{
				block[4*(j*blockwidth+i)+0] = col.s.red;
				block[4*(j*blockwidth+i)+1] = col.s.green;
				block[4*(j*blockwidth+i)+2] = col.s.blue;
				block[4*(j*blockwidth+i)+3] = 0xff;
			}
		}
	}

	// Composite the columns together.
	for (i = 0, patch = texture->patches; i < texture->patchcount; i++, patch++)
	{
		realpatch = W_CacheLumpNum (patch->patch, PU_CACHE);


        // Special "on the fly" loaded texture
        if (texture->wadlocation != 0 && texture->tx == true)
        {
            // Update the height, width and patches, because these can get out of sync and need rechecking
            patch = texture->patches;
            realpatch = W_CacheLumpNumPwad(texture->wadlocation, patch->patch, PU_CACHE);
            texture->height = realpatch->height;
            texture->width = realpatch->width;
        }


        // Modifies the mipmap
		HWR_DrawPatchInCache(&grtex->mipmap,
		                     blockwidth, blockheight,
		                     blockwidth*format2bpp[grtex->mipmap.glInfo.format],
		                     texture->width, texture->height,
		                     patch->originx, patch->originy,
		                     realpatch,
		                     format2bpp[grtex->mipmap.glInfo.format]);
	}

	// NOTE: May be inefficeint
	if (format2bpp[grtex->mipmap.glInfo.format] == 4)
	{
		for (i = 3; i < blocksize; i += 4)
		{
			if (block[i] == 0)
			{
				grtex->mipmap.flags |= TF_TRANSPARENT|TF_NOFILTER;
				break;
			}
		}
	}

	// make it purgable from zone memory
	// use PU_PURGELEVEL so we can Z_FreeTags all at once
	Z_ChangeTag(block, PU_HWRCACHE);

	grtex->scaleX = 1.0f/(texture->width*FRACUNIT);
	grtex->scaleY = 1.0f/(texture->height*FRACUNIT);
}
#endif


GLTextureFormat_t HWR_LoadPNG(const char *filename, int *w, int *h, GLTexture_t *grMipmap)
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

	lumpnum_t lumpnum;
	ULONG lumppos;

	lumpnum = W_CheckNumForName(filename);
	if (lumpnum == LUMPERROR)
		return 0;

	// Is a PNG file?
	if (W_LumpIsPng(lumpnum) == false)
	{
		return 0;
	}

	lumppos = W_LumpPos(lumpnum);
	png_FILE = W_GetWadHandleForLumpNum(lumpnum);

	if (!png_FILE)
	{
		CONS_Printf("PNG file %s not found\n", filename);
		return 0;
	}


	// move the wad file pos to the start.
	fseek(png_FILE, lumppos, SEEK_SET);

	png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL,
									 PNG_error, NULL); // use null istead of PNG warn here, sometimes data can be shifted in wad files (TEMPORARY)
	if (!png_ptr)
	{
		CONS_Printf("HWR_LoadPNG: Error on initialize libpng\n");
		return 0;
	}

	png_info_ptr = png_create_info_struct(png_ptr);
	if (!png_info_ptr)
	{
		CONS_Printf("HWR_LoadPNG: Error on allocate for libpng\n");
		png_destroy_read_struct(&png_ptr, NULL, NULL);
		return 0;
	}

#ifdef USE_FAR_KEYWORD
	if (setjmp(jmpbuf))
#else
		if (setjmp(png_jmpbuf(png_ptr)))
#endif
		{
			png_destroy_read_struct(&png_ptr, &png_info_ptr, NULL);
#ifdef GLFUN
			Z_Free(grMipmap->glInfo.data);
#else
			Z_Free(grMipmap->mipmap.glInfo.data);
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
		png_bytep PNG_image = Z_Malloc(pitch*height, PU_HWRCACHE, &grMipmap->mipmap.glInfo.data);
#endif
		png_bytepp row_pointers = png_malloc(png_ptr, height * sizeof (png_bytep));
		for (i = 0; i < height; i++)
			row_pointers[i] = PNG_image + i*pitch;
		png_read_image(png_ptr, row_pointers);
		png_free(png_ptr, (png_voidp)row_pointers);
	}

	png_destroy_read_struct(&png_ptr, &png_info_ptr, NULL);

	*w = width;
	*h = height;
	return GR_RGBA;
}



static GLTextureFormat_t HWR_LoadFlatPNG(const char *filename, int *w, int *h, GLMipmap_t *mipmap)
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

	lumpnum_t lumpnum;
	ULONG lumppos;

	lumpnum = W_CheckNumForName(filename);
	if (lumpnum == LUMPERROR)
		return 0;

	// Is a PNG file?
	if (W_LumpIsPng(lumpnum) == false)
	{
		return 0;
	}

	lumppos = W_LumpPos(lumpnum);
	png_FILE = W_GetWadHandleForLumpNum(lumpnum);

	if (!png_FILE)
	{
		CONS_Printf("PNG file %s not found\n", filename);
		return 0;
	}


	// move the wad file pos to the start.
	fseek(png_FILE, lumppos, SEEK_SET);

	png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL,
									 PNG_error, NULL); // use null instead of PNG warn here, sometimes data can be shifted in wad files (TEMPORARY)
	if (!png_ptr)
	{
		CONS_Printf("HWR_LoadPNG: Error on initialize libpng\n");
		return 0;
	}

	png_info_ptr = png_create_info_struct(png_ptr);
	if (!png_info_ptr)
	{
		CONS_Printf("HWR_LoadPNG: Error on allocate for libpng\n");
		png_destroy_read_struct(&png_ptr, NULL, NULL);
		return 0;
	}

#ifdef USE_FAR_KEYWORD
	if (setjmp(jmpbuf))
#else
		if (setjmp(png_jmpbuf(png_ptr)))
#endif
		{
			png_destroy_read_struct(&png_ptr, &png_info_ptr, NULL);


			Z_Free(mipmap->glInfo.data);


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


		png_bytep PNG_image = Z_Malloc(pitch*height, PU_HWRCACHE, &mipmap->glInfo.data);


		png_bytepp row_pointers = png_malloc(png_ptr, height * sizeof (png_bytep));
		for (i = 0; i < height; i++)
			row_pointers[i] = PNG_image + i*pitch;
		png_read_image(png_ptr, row_pointers);
		png_free(png_ptr, (png_voidp)row_pointers);
	}

	png_destroy_read_struct(&png_ptr, &png_info_ptr, NULL);

	*w = width;
	*h = height;

	//CONS_Printf("PNG file %s read\n", filename);

	return GR_RGBA;
}



// grTex : Hardware texture cache info
//         .data : address of converted patch in heap memory
//                 user for Z_Malloc(), becomes NULL if it is purged from the cache
void HWR_MakePatch(const patch_t *patch, GLPatch_t *grPatch, GLMipmap_t *grMipmap)
{
	byte *block;
	int newwidth, newheight;











	// don't do it twice (like a cache)
	if (grMipmap->width == 0)
	{
		// save the original patch header so that the GLPatch can be casted
		// into a standard patch_t struct and the existing code can get the
		// orginal patch dimensions and offsets.
		grPatch->width = SHORT(patch->width);
		grPatch->height = SHORT(patch->height);
		grPatch->leftoffset = SHORT(patch->leftoffset);
		grPatch->topoffset = SHORT(patch->topoffset);

		// Convert the texture to a power of 2
		HWR_ResizeBlock (SHORT(patch->width), SHORT(patch->height));
		grMipmap->width = (USHORT)blockwidth;
		grMipmap->height = (USHORT)blockheight;

		// no wrap around, no chroma key
		grMipmap->flags = 0;
		// setup the texture info
		grMipmap->glInfo.format = patchformat;
	}
	else
	{
		blockwidth = grMipmap->width;
		blockheight = grMipmap->height;
		blocksize = blockwidth * blockheight;
	}

	Z_Free(grMipmap->glInfo.data);
	grMipmap->glInfo.data = NULL;

	block = HWR_MakeBlock(grMipmap);

	// no rounddown, do not size up patches, so they don't look 'scaled'
	newwidth  = min(SHORT(patch->width), blockwidth);
	newheight = min(SHORT(patch->height), blockheight);

	HWR_DrawPatchInCache(grMipmap,
	                     newwidth, newheight,
	                     blockwidth*format2bpp[grMipmap->glInfo.format],
	                     SHORT(patch->width), SHORT(patch->height),
	                     0, 0,
	                     patch,
	                     format2bpp[grMipmap->glInfo.format]);

	grPatch->max_s = (float)newwidth / (float)blockwidth;
	grPatch->max_t = (float)newheight / (float)blockheight;

	// Now that the texture has been built in cache, it is purgable from zone memory.
	Z_ChangeTag (block, PU_HWRCACHE);
}


// =================================================
//             CACHING HANDLING
// =================================================

static size_t gr_numtextures;
static GLTexture_t *gr_textures; // for ALL SRB2 textures

void HWR_InitTextureCache(void)
{
	gr_numtextures = 0;
	gr_textures = NULL;
}

void HWR_FreeTextureCache(void)
{
	int i, j;
	// free references to the textures
	GL_ClearMipMapCache();

	// free all hardware-converted graphics cached in the heap,
	// our gool is only the textures since user of the texture is the texture cache
	if (Z_TagUsage(PU_HWRCACHE))
		Z_FreeTags(PU_HWRCACHE, PU_HWRCACHE);

	// Free all textures after each level: must be done after GL_ClearMipMapCache!
	for (j = 0; j < numwadfiles; j++)
	{
		for (i = 0; i < wadfiles[j]->numlumps; i++)
		{
			GLPatch_t *grpatch = &(wadfiles[j]->hwrcache[i]);
			while (grpatch->mipmap.nextcolormap)
			{
				GLMipmap_t *grmip = grpatch->mipmap.nextcolormap;
				grpatch->mipmap.nextcolormap = grmip->nextcolormap;
				free(grmip);
			}
		}
	}

	// Now the heap doesn't have any 'user' pointing to our
	// texturecache info, we can free it
	if (gr_textures)
		free(gr_textures);
	gr_textures = NULL;
	gr_numtextures = 0;
}

//
// HWR_PrepLevelCache
//
// Prepare the game for new textures to be loaded every level
//
void HWR_PrepLevelCache(size_t pnumtextures)
{
	// SRB2CBTODO: HWR_PrepLevelCache
	// problem: the mipmap cache management holds a list of mipmaps.. but they are
	//           reallocated on each level..
	//sub-optimal, but 1) just need re-download stuff in hardware cache VERY fast
	//   2) sprite/menu stuff mixed with level textures so can't do anything else

	// we must free it since numtextures changed
	HWR_FreeTextureCache();

	gr_numtextures = pnumtextures;
	gr_textures = calloc(pnumtextures, sizeof (*gr_textures)); // TODO: ESLOPE: Flat->Walls and Wall->Flats & TX_END support! VPHYSICS :P
	if (gr_textures == NULL)
		I_Error("HWR_PrepLevelCache allocation gr_textures failed");
}

// SRB2CBTODO: Make palettes work correctly in OpenGL
void HWR_SetPalette(RGBA_t *palette)
{
	// Added for OpenGL gamma correction
	RGBA_t gamma_correction = {0x00000000};

	// Added for OpenGL gamma correction
	gamma_correction.s.red   = (byte)cv_grgammared.value;
	gamma_correction.s.green = (byte)cv_grgammagreen.value;
	gamma_correction.s.blue  = (byte)cv_grgammablue.value;
	GL_SetPalette(palette, &gamma_correction);

	// OpenGL will flush textures own it's own cache if the cache is non paletized,
	// now flush data texture cache so 32 bit textures are recomputed
	if (patchformat == GR_RGBA || textureformat == GR_RGBA)
		Z_FreeTags(PU_HWRCACHE, PU_HWRCACHE);
}





static void HWR_PatchPNG(int texnum, GLTexture_t *grtex, boolean anisotropic)
{
		int w = 0, h = 0, l = 0;
#ifdef HAVE_PNG
        const char *filename = textures[texnum]->name;
		grtex->mipmap.glInfo.format = HWR_LoadPNG(filename, &w, &h, grtex);
#endif

		grtex->mipmap.downloaded = 0;
	
	
	// Sky textures are special, find them by name and convert them
	if (textures[texnum]->name[0] == 'S' &&
	    textures[texnum]->name[1] == 'K' &&
	    textures[texnum]->name[2] == 'Y' &&
	    (textures[texnum]->name[4] == 0 ||
	     textures[texnum]->name[5] == 0)
		)
	{
		grtex->mipmap.flags = TF_WRAPXY|TF_NOFILTER; // Don't filter the sky
	}
	else
		grtex->mipmap.flags = TF_WRAPXY|TF_CHROMAKEYED;

		if (!anisotropic)
            grtex->mipmap.flags |= TF_NOFILTER;




		// PNG textures are a bit different because they're much closer to the OpenGL renderer,
		// a PNG file is read and its height, width and pixel data are passed back to OpenGL
		// a PNG texture in the game must be updated to be in sync with the height of the real PNG file
		grtex->mipmap.width = (USHORT)w;
		grtex->mipmap.height = (USHORT)h;


		//textures[texnum]->name = filename; // double check the name
		textures[texnum]->height = (USHORT)h;
		textures[texnum]->width = (USHORT)w;

		// Setup the widthmask too
		{
			l = 1;
			while (l*2 <= SHORT(w))
				l <<= 1;

			texturewidthmask[texnum] = l - 1;
		}

		textureheight[texnum] = h*FRACUNIT;


    grtex->scaleX = 1.0f/(float)((w*FRACUNIT));
	grtex->scaleY = 1.0f/(float)((h*FRACUNIT));
}


// --------------------------------------------------------------------------
// Make sure texture is downloaded and set it as the source
// --------------------------------------------------------------------------
GLTexture_t *HWR_GetTexture(int tex, boolean anisotropic)
{
	GLTexture_t *grtex;

	grtex = &gr_textures[tex];

	if (!grtex || !textures[tex])
		I_Error("HWR_GetTexture: Invalid texture number %i", tex);

	if (!grtex->mipmap.glInfo.data && !grtex->mipmap.downloaded)
	{
		lumpnum_t lumpnum = W_CheckNumForName(textures[tex]->name);

		if (lumpnum != LUMPERROR)
		{
			// Is the lump a PNG? If not, load the texture using the old bitmap format
			if (W_LumpIsPng(lumpnum) == true)
				HWR_PatchPNG(tex, grtex, anisotropic);
			//else
				//	HWR_GenerateTexture(tex, grtex); // ZTODO: Fix this!!
		}
	}

	GL_SetTexture(&grtex->mipmap, anisotropic);
	return grtex;
}

static void HWR_FlatPNG(GLMipmap_t *mipmap, lumpnum_t flatlumpnum, boolean anisotropic)
{
    int w = 0, h = 0;

	if (!mipmap->width) // Only load up stuff if needed!
	{

	// setup the texture info
	mipmap->glInfo.format = GR_TEXFMT_P_8;
	mipmap->flags = TF_WRAPXY|TF_CHROMAKEYED; // SRB2CBTODO: Transparent flat support!

	if (!anisotropic)
		mipmap->flags |= TF_NOFILTER;











    //CONS_Printf("Flat %.8s is PNG\n", W_CheckNameForNum(flatlumpnum));
    const char *filename = W_CheckNameForNum(flatlumpnum);
    mipmap->glInfo.format = HWR_LoadFlatPNG(filename, &w, &h, mipmap);

    mipmap->downloaded = 0;


    // PNG textures are a bit different because they're much closer to the OpenGL renderer,
    // a PNG file is read and its height, width and pixel data are passed back to OpenGL
    // a PNG texture in the game must be updated to be in sync with the height of the real PNG file
    mipmap->width = (USHORT)w;
    mipmap->height = (USHORT)h;

    // Is a power of 2 conversion necessary for OpenGL?


    //grtex->scaleX = 1.0f/(float)((w*FRACUNIT));
	//grtex->scaleY = 1.0f/(float)((h*FRACUNIT));
	}



}





static void HWR_MakePNGPatch(const patch_t *patch, GLPatch_t *grPatch, GLMipmap_t *mipmap, lumpnum_t lumpnum)
{
    int w = 0, h = 0;
	
	patch = NULL; // NOTE: Remove these NULLs if you're going to use this function
	grPatch = NULL; // There' just null here to prevent compile warnings

	// don't do it twice (like a cache)
	if (!mipmap->width)
	{
		// SRB2CBTODO: ZTODO: save the original patch header so that the GLPatch can be casted
		// into a standard patch_t struct and the existing code can get the
		// orginal patch dimensions and offsets.

		// setup the texture info
		mipmap->glInfo.format = GR_TEXFMT_P_8;
		mipmap->flags = TF_WRAPXY|TF_CHROMAKEYED; // SRB2CBTODO: Transparent flat support!



		//CONS_Printf("Flat %.8s is PNG\n", W_CheckNameForNum(flatlumpnum));
		const char *filename = W_CheckNameForNum(lumpnum);
		mipmap->glInfo.format = HWR_LoadFlatPNG(filename, &w, &h, mipmap);

		mipmap->downloaded = 0;


		// PNG textures are a bit different because they're much closer to the OpenGL renderer,
		// a PNG file is read and its height, width and pixel data are passed back to OpenGL
		// a PNG texture in the game must be updated to be in sync with the height of the real PNG file
		mipmap->width = (USHORT)w;
		mipmap->height = (USHORT)h;

		// Is a power of 2 conversion necessary for OpenGL?


		//grtex->scaleX = 1.0f/(float)((w*FRACUNIT));
		//grtex->scaleY = 1.0f/(float)((h*FRACUNIT));
	}



}








static void HWR_CacheFlat(GLMipmap_t *grMipmap, lumpnum_t flatlumpnum, boolean anisotropic)
{
	size_t size, pflatsize;

	// setup the texture info
	grMipmap->glInfo.format = GR_TEXFMT_P_8;
	grMipmap->flags = TF_WRAPXY|TF_CHROMAKEYED; // SRB2CBTODO: Transparent flat support!

	if (!anisotropic)
		grMipmap->flags |= TF_NOFILTER;

	size = W_LumpLength(flatlumpnum);

	switch (size)
	{
		case 4194304: // 2048x2048 lump
			pflatsize = 2048;
			break;
		case 1048576: // 1024x1024 lump
			pflatsize = 1024;
			break;
		case 262144:// 512x512 lump
			pflatsize = 512;
			break;
		case 65536: // 256x256 lump
			pflatsize = 256;
			break;
		case 16384: // 128x128 lump
			pflatsize = 128;
			break;
		case 1024: // 32x32 lump
			pflatsize = 32;
			break;
		default: // 64x64 lump
			pflatsize = 64;
			break;
	}
	// All flats are power of 2 already, no conversion needed
	grMipmap->width  = (USHORT)pflatsize;
	grMipmap->height = (USHORT)pflatsize;

    // Allocate memory, at the same time, copy the image data to glInfo.data
	Z_Malloc(W_LumpLength(flatlumpnum),
					 PU_HWRCACHE, &grMipmap->glInfo.data);

	W_ReadLump(flatlumpnum, grMipmap->glInfo.data);
}


// The old engine used a low quality data format that was read,
// now we've switched to PNG
void HWR_GetFlat(lumpnum_t flatlumpnum, boolean anisotropic)
{
	GLMipmap_t *grmip;

	grmip = &(wadfiles[WADFILENUM(flatlumpnum)]->hwrcache[LUMPNUM(flatlumpnum)].mipmap);

	if (!grmip->downloaded && !grmip->glInfo.data)
	{
		// ZTODO: NOte .name is VERY unstable here
	    if (levelflats[flatlumpnum].name)
	    {
		//	if (gl_devmode)
			//CONS_Printf("Getting flat %s..\n", W_CheckNameForNum(flatlumpnum));
            if (W_LumpIsPng(flatlumpnum))
                HWR_FlatPNG(grmip, flatlumpnum, anisotropic);
            else
                HWR_CacheFlat(grmip, flatlumpnum, anisotropic);
	    }
	}

	GL_SetTexture(grmip, anisotropic);
}

// Speed up the process of looking for the resolution of flats,
// VERY hacky, but the best posssible without a real rewrite of this code mess
int HWR_GetFlatRez(lumpnum_t flatlumpnum)
{
	GLMipmap_t *grmip;

	grmip = &(wadfiles[WADFILENUM(flatlumpnum)]->hwrcache[LUMPNUM(flatlumpnum)].mipmap);

	if (!grmip->downloaded && !grmip->glInfo.data)
	{
		return 0; // Should really happen
	}

	return grmip->height;
}

//
// HWR_LoadMappedPatch(): replace the skin color of the sprite in cache
//                          : load it first in SRB2's cache if not already
//
static void HWR_LoadMappedPatch(GLMipmap_t *grmip, GLPatch_t *gpatch)
{
	if (!grmip->downloaded && !grmip->glInfo.data)
	{
		patch_t *patch = W_CacheLumpNum(gpatch->patchlump, PU_STATIC);


		if (gpatch->patchlump != LUMPERROR)
		{
			// Is the lump a PNG? If not, load the texture using the old bitmap format
			if (W_LumpIsPng(gpatch->patchlump) == true)
				HWR_MakePNGPatch(patch, gpatch, grmip, gpatch->patchlump);
			else
		HWR_MakePatch(patch, gpatch, grmip);
		}

		Z_Free(patch);
	}

	GL_SetTexture(grmip, false);
}

// -----------------+
// HWR_GetPatch     : Download a patch to the hardware cache and make it ready for use
// -----------------+
void HWR_GetPatch(GLPatch_t *gpatch)
{
	// is it in hardware cache
	if (!gpatch->mipmap.downloaded && !gpatch->mipmap.glInfo.data)
	{
		// load the software patch, PU_STATIC or the Z_Malloc for hardware patch will
		// flush the software patch before the conversion! oh yeah I suffered
		patch_t *ptr = W_CacheLumpNum(gpatch->patchlump, PU_STATIC);

		if (gpatch->patchlump != LUMPERROR)
		{
			// Is the lump a PNG? If not, load the texture using the old bitmap format
			if (W_LumpIsPng(gpatch->patchlump) == true)
				HWR_MakePNGPatch(ptr, gpatch, &gpatch->mipmap, gpatch->patchlump);
			else
		HWR_MakePatch(ptr, gpatch, &gpatch->mipmap);
		}

		// this is inefficient.. but the hardware patch in heap is purgeable so it should
		// not fragment memory, and besides the REAL cache here is the hardware memory
		Z_Free(ptr);
	}

	GL_SetTexture(&gpatch->mipmap, false);
}


// -------------------+
// HWR_GetMappedPatch : Same as HWR_GetPatch for sprite color
// -------------------+
void HWR_GetMappedPatch(GLPatch_t *gpatch, const byte *colormap)
{
	GLMipmap_t *grmip, *newmip;

	if (colormap == colormaps || colormap == NULL)
	{
		// Load the default (green) color in SRB2's cache (temporary?) AND hardware cache
		HWR_GetPatch(gpatch);
		return;
	}

	// search for the mimmap
	// skip the first (no colormap translated)
	for (grmip = &gpatch->mipmap; grmip->nextcolormap; )
	{
		grmip = grmip->nextcolormap;
		if (grmip->colormap == colormap)
		{
			HWR_LoadMappedPatch(grmip, gpatch);
			return;
		}
	}
	// If the colormaped patch is not found, create it
	// this calloc is cleared in HWR_FreeTextureCache
	// It's always better to be able to free something manually than pass it to the Zone system
	newmip = calloc(1, sizeof(*newmip));
	if (newmip == NULL)
		I_Error("%s: Out of memory", __FUNCTION__);

	grmip->nextcolormap = newmip;

	newmip->colormap = colormap;
	HWR_LoadMappedPatch(newmip, gpatch);
}

static const int picmode2GR[] =
{
	GR_TEXFMT_P_8,                // PALETTE
	0,                            // INTENSITY          (unsupported yet)
	GR_TEXFMT_ALPHA_INTENSITY_88, // INTENSITY_ALPHA    (corona use this)
	0,                            // RGB24              (unsupported yet)
	GR_RGBA,                      // RGBA32             (opengl only)
};

static void HWR_DrawPicInCache(byte *block, int pblockwidth, int pblockheight,
	int blockmodulo, pic_t *pic, int bpp)
{
	int i,j;
	fixed_t posx, posy, stepx, stepy;
	byte *dest, *src, texel;
	int picbpp;
	RGBA_t col;

	if (!pic || !block || !bpp || !blockmodulo)
	{
		I_Error("HWR_DrawPicInCache: A pic's drawn is non-existent or corrupt\n");
		return;
	}

	if (!pblockwidth || !pblockheight)
	{
		I_Error("HWR_DrawPicInCache: A pic's height/width equals 0!\nThe picture is non-existent or corrupt\n");
		return;
	}

	stepy = ((int)SHORT(pic->height)<<FRACBITS)/pblockheight;
	stepx = ((int)SHORT(pic->width)<<FRACBITS)/pblockwidth;
	picbpp = format2bpp[picmode2GR[pic->mode]];
	posy = 0;
	for (j = 0; j < pblockheight; j++)
	{
		posx = 0;
		dest = &block[j*blockmodulo];
		src = &pic->data[(posy>>FRACBITS)*SHORT(pic->width)*picbpp];
		for (i = 0; i < pblockwidth;i++)
		{
			switch (pic->mode)
			{ // source bpp
				case PALETTE :
					texel = src[(posx+FRACUNIT/2)>>FRACBITS];
					switch (bpp)
					{ // destination bpp
						case 1 :
							*dest++ = texel; break;
						case 2 :
							*(USHORT *)dest = (USHORT)(texel | 0xff00);
							dest +=2;
							break;
						case 3 :
							col = V_GetColor(texel);
							((RGBA_t *)dest)->s.red   = col.s.red;
							((RGBA_t *)dest)->s.green = col.s.green;
							((RGBA_t *)dest)->s.blue  = col.s.blue;
							dest += 3;
							break;
						case 4 :
							*(RGBA_t *)dest = V_GetColor(texel);
							dest += 4;
							break;
					}
					break;
				case INTENSITY :
					*dest++ = src[(posx+FRACUNIT/2)>>FRACBITS];
					break;
				case INTENSITY_ALPHA : // assume dest bpp = 2
					*(USHORT*)dest = *((short *)src + ((posx+FRACUNIT/2)>>FRACBITS));
					dest += 2;
					break;
				case RGB24 :
					break;  // not supported yet
				case RGBA32 : // assume dest bpp = 4
					dest += 4;
					*(ULONG *)dest = *((ULONG *)src + ((posx+FRACUNIT/2)>>FRACBITS));
					break;
			}
			posx += stepx;
		}
		posy += stepy;
	}
}

// -----------------+
// HWR_GetPic       : Download an SRB2 pic (raw row encoded with no 'holes')
// Returns          :
// -----------------+
GLPatch_t *HWR_GetPic(lumpnum_t lumpnum)
{
	GLPatch_t *grpatch;

	grpatch = &(wadfiles[lumpnum>>16]->hwrcache[lumpnum & 0xffff]);

	if (!grpatch->mipmap.downloaded && !grpatch->mipmap.glInfo.data)
	{
		pic_t *pic;
		byte *block;
		size_t len;
		int newwidth, newheight;

		pic = W_CacheLumpNum(lumpnum, PU_STATIC);
		grpatch->width = SHORT(pic->width);
		grpatch->height = SHORT(pic->height);
		len = W_LumpLength(lumpnum) - sizeof (pic_t);

		grpatch->leftoffset = 0;
		grpatch->topoffset = 0;

		// Convert textures to a power of 2
		HWR_ResizeBlock(grpatch->width, grpatch->height);
		grpatch->mipmap.width = (USHORT)blockwidth;
		grpatch->mipmap.height = (USHORT)blockheight;

		if (pic->mode == PALETTE)
			grpatch->mipmap.glInfo.format = textureformat; // can be set by driver
		else
			grpatch->mipmap.glInfo.format = picmode2GR[pic->mode];

		Z_Free(grpatch->mipmap.glInfo.data);

		// allocate block
		block = HWR_MakeBlock(&grpatch->mipmap);

		// no rounddown, do not size up patches, so they don't look 'scaled'
		newwidth  = min(SHORT(pic->width),blockwidth);
		newheight = min(SHORT(pic->height),blockheight);

		if (grpatch->width  == blockwidth &&
			grpatch->height == blockheight &&
			format2bpp[grpatch->mipmap.glInfo.format] == format2bpp[picmode2GR[pic->mode]])
		{
			// no conversion needed
			memcpy(grpatch->mipmap.glInfo.data, pic->data,len);
		}
		else
			HWR_DrawPicInCache(block, newwidth, newheight,
			                   blockwidth*format2bpp[grpatch->mipmap.glInfo.format],
			                   pic,
			                   format2bpp[grpatch->mipmap.glInfo.format]);

		Z_ChangeTag(pic, PU_CACHE);
		Z_ChangeTag(block, PU_HWRCACHE);

		grpatch->mipmap.flags = 0;
		grpatch->max_s = (float)newwidth  / (float)blockwidth;
		grpatch->max_t = (float)newheight / (float)blockheight;
	}
	GL_SetTexture(&grpatch->mipmap, false);

	return grpatch;
}

#endif //HWRENDER
