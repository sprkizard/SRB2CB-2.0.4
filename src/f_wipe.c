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
/// \brief Wipe screen special effect.

#include "i_video.h"
#include "v_video.h"
#include "r_draw.h" // transtable
#include "p_pspr.h" // tr_transxxx
#include "i_system.h"
#include "m_menu.h"
#include "f_finale.h"
#include "r_main.h"
#include "hardware/hw_main.h"

#if NUMSCREENS < 3
#define NOWIPE // Do not enable wipe image post processing for ARM, SH and MIPS CPUs
#endif

//--------------------------------------------------------------------------
//                        SCREEN WIPE PACKAGE
//--------------------------------------------------------------------------

boolean WipeInAction = false;
#ifndef NOWIPE

static byte *wipe_scr_start; //screens 2
static byte *wipe_scr_end; //screens 3
static byte *wipe_scr; //screens 0

/**	\brief	start the wipe

	\param	width	width of wipe
	\param	height	height of wipe
	\param	ticks	ticks for wipe

	\return	unknown


*/
static inline int F_InitWipe(int width, int height, tic_t ticks)
{
	if (rendermode != render_soft)
		return 0;
	(void)ticks;
	memcpy(wipe_scr, wipe_scr_start, width*height*scr_bpp);
	return 0;
}

/**	\brief	wipe ticker

	\param	width	width of wipe
	\param	height	height of wipe
	\param	ticks	ticks for wipe

	\return	the change in wipe


*/
static int F_DoWipe(int width, int height, tic_t ticks)
{
	boolean changed = false;
	byte *w;
	byte *e;
	byte newval;
	static int slowdown = 0; // Slow down the fade a bit, this is for slower fading

	while (ticks--)
	{
		// OpenGL has it's own render for screen wiping
		// This has to come before "slowdown" or else a small visual error occurs
		if (rendermode == render_opengl)
		{
#ifdef HWRENDER
			if (cv_fadestyle.value != 0)
				HWR_DoScreenWipe();
#endif
			changed = true;
			continue;
		}
		
		// slowdown
		if (slowdown++)
		{
			slowdown = 0;
			return false;
		}
		
		if (cv_fadestyle.value != 0)
		{

			w = wipe_scr;
			e = wipe_scr_end;

			while (w != wipe_scr + width*height)
			{
				if (*w != *e)
				{
					if (((newval = transtables[(*e<<8) + *w + ((tr_trans80-1)<<FF_TRANSSHIFT)]) == *w)
						&& ((newval = transtables[(*e<<8) + *w + ((tr_trans50-1)<<FF_TRANSSHIFT)]) == *w)
						&& ((newval = transtables[(*w<<8) + *e + ((tr_trans80-1)<<FF_TRANSSHIFT)]) == *w))
					{
						newval = *e;
					}
					*w = newval;
					changed = true;
				}
				w++;
				e++;
			}
		}
	}
	return !changed;
}
#endif

/** Save the "before" screen of a wipe.
  */
void F_WipeStartScreen(void)
{
	if (cv_fadestyle.value == 0)
		return;

#ifndef NOWIPE
	if (rendermode != render_soft)
	{
#ifdef HWRENDER
		// OpenGL uses it's own function for screen wipe capture
		if (rendermode == render_opengl)
			HWR_StartScreenWipe();
#endif

		return;
	}
	wipe_scr_start = screens[2];
	if (rendermode == render_soft)
		I_ReadScreen(wipe_scr_start);
#endif
}

/** Save the "after" screen of a wipe.
  *
  * \param x      Starting x coordinate of the starting screen to restore.
  * \param y      Starting y coordinate of the starting screen to restore.
  * \param width  Width of the starting screen to restore.
  * \param height Height of the starting screen to restore.
  */
void F_WipeEndScreen(int x, int y, int width, int height)
{
	if (cv_fadestyle.value == 0)
		return;

	if (rendermode != render_soft)
	{
#ifdef HWRENDER
		// OpenGL screen wipe
		if (rendermode == render_opengl)
			HWR_EndScreenWipe();
#endif

		return;
	}
	
#ifdef NOWIPE
	(void)x;
	(void)y;
	(void)width;
	(void)height;
#else
	wipe_scr_end = screens[3];
	I_ReadScreen(wipe_scr_end);
	V_DrawBlock(x, y, 0, width, height, wipe_scr_start);
#endif
}

/**	\brief	wipe screen

	\param	x	x starting point
	\param	y	y starting point
	\param	width	width of wipe
	\param	height	height of wipe
	\param	ticks	ticks for wipe

	\return	if true, the wipe is done


*/

int F_ScreenWipe(int x, int y, int width, int height, tic_t ticks)
{
	int rc = 1;
	// initial stuff
	(void)x;
	(void)y;
#ifdef NOWIPE
	width = height = ticks = 0;
#else
	if (!WipeInAction)
	{
		WipeInAction = true;
		if (cv_fadestyle.value != 0)
		{
			wipe_scr = screens[0];
			F_InitWipe(width, height, ticks);
		}
	}

	rc = F_DoWipe(width, height, ticks);

	if (rc)
		WipeInAction = false;
#endif
	return rc;
}

//
// F_RunWipe
//
//
// After setting up the screens you want to
// wipe, calling this will do a 'typical'
// wipe.
//
void F_RunWipe(tic_t duration)
{
	tic_t wipestart, tics, nowtime, y;
	boolean done;
	
	if (cv_fadestyle.value == 0)
		return;

	wipestart = I_GetTime() - 1;
	y = wipestart + duration; // init a timeout
	do
	{
		do
		{
			nowtime = I_GetTime();
			tics = nowtime - wipestart;
			if (!tics) I_Sleep();
		} while (!tics);
		wipestart = nowtime;

		done = F_ScreenWipe(0, 0, vid.width, vid.height, tics);
			
		I_OsPolling();
		I_FinishUpdate();

	} while (!done && I_GetTime() < y);
}
