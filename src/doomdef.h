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
/// \brief  Internally used data structures for virtually everything,
///	key definitions, lots of other stuff.

#ifndef __DOOMDEF__
#define __DOOMDEF__

#ifdef _WINDOWS
#if !defined (HWRENDER) && !defined (NOHW)
#define HWRENDER // SRB2CBTODO: ALWAYS define HWRENDER, don't #ifdef
#endif

/*
 // SRB2CBTODO: Does this HW3SOUND need a framework? If its not amazing, remove
#if !defined(HW3SOUND) && !defined (NOHS)
#define HW3SOUND
#endif*/

#endif

#if defined (_WIN32) || defined (_WIN32_WCE)
#define ASMCALL __cdecl
#else
#define ASMCALL
#endif

#ifdef _MSC_VER
#pragma warning(disable : 4127 4152 4213 4514)
#endif
// warning level 4
// warning C4127: conditional expression is constant
// warning C4152: nonstandard extension, function/data pointer conversion in expression
// warning C4213: nonstandard extension used : cast on l-value

#if defined (_WIN32_WCE) && defined (DEBUG) && defined (ARM)
#if defined (ARMV4) || defined (ARMV4I)
//#pragma warning(disable : 1166)
// warning LNK1166: cannot adjust code at offset=
#endif
#endif


#include "doomtype.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <math.h>

#if !defined (_WIN32_WCE)
#include <sys/types.h>
#include <sys/stat.h>
#endif
#include <ctype.h>

#if defined (_WIN32)
#include <io.h>
#endif

//#define NOMD5
//#define NONET
// Uncheck this to compile debugging code
//#ifdef _DEBUG
//#ifndef RANGECHECK
//#define RANGECHECK
//#endif
//#ifndef PARANOIA
//#define PARANOIA // do some tests that never fail but maybe
//#endif
// turn this on by make etc.. DEBUGMODE = 1 or use the Debug profile in the VC++ projects
//#endif

#if defined (_WIN32) || defined (__unix__) || defined(__APPLE__) || defined (UNIXLIKE) || defined (macintosh)
#define LOGMESSAGES // Write messages in log.txt
#endif

#if defined (LOGMESSAGES) && !defined (_WINDOWS)
extern FILE *logstream;
#endif

// For future use, the codebase is the version of SRB2
// that the modification is based on, and should not be changed unless you have
// merged changes between versions of SRB2 (such as 2.0.4 to 2.0.5, etc) into your working copy.
// Will always resemble the versionstring, 205 = 2.0.5, 210 = 2.1, etc.
#define CODEBASE 206

#define VERSION 110 // Game version
#define VERSIONSTRING "v1.1"

// HIGHLY IMPORTANT Modification Options,
// MUST be changed when creating a modification (or updating SRB2),
// else a lot of errors will occur when trying to access the Master Server.
// If you are just making clientside fixes for private use,
// that will not break netplay, these options are not important,
// but any builds not compatible with the normal SRB2 release must change these options accordingly,
// if they are compatible with normal releases but you would still like to enable the updating feature,
// you must still change these values.

// The Modification ID (1 = Official Build),
// must be obtained directly from a web adminstrator if connecting to the Master Server,
// can also be set yourself if you know how to create your own dedicated server
// (Making your own server is NOT the same thing as hosting a regular netgame)
// BlastEngine/SRB2CB is MODID 10
#define MODID 10


// The Modification Version, starting from 1, do not follow your version string for this,
// it's only for detection of the version the player is using
// so the MS can alert them of an update accordingly. Only set it higher, not lower.
#define MODVERSION 110

// Comment out this line to completely disable update alerts
// (recommended for testing, but not for release)
#define UPDATE_ALERT

// The string used in the alert that pops up in the event of an update being available.
// Please change to apply to your modification (we don't want everyone asking where your mod is on SRB2.org!).
#define UPDATE_ALERT_STRING "New updates are available for SRB2CB\nPlease visit mb.srb2.org to download them.\n"\
"\nYou will not be able to connect to the\nMaster Server until you have updated to\n"\
"the latest version!\n\nCurrent Version: %s\nLatest Version: %s"






// =========================================================================

// The maximum number of players, multiplayer/networking.
// NOTE: You cannot just change this value if you want more players than this

#define MAXPLAYERS 32
#define MAXSKINS MAXPLAYERS
#define PLAYERSMASK (MAXPLAYERS-1)
// Maximum characters in a player's name
#define MAXPLAYERNAME 21

#define MAXSKINCOLORS 22 // JTE

#define MAXROOMS 32

// The TICRATE is the maximum amount of total game updates per seconds,
// To modify the FPS but keep the real game syncronised, NEWTICRATERATIO is used
// NOTE: This is used to set the time // SRB2CBTODO: Use a static way to keep real time
// SRB2CBTODO: Rename to NUMTICS and TICRATIO
#define OLDTICRATE 35
// NOTE: Time and network syncing error with a NEWTICRATERATIO that's not 1
// the REAL way to do 60FPS is to manually interpolate between frames
// and leave the game's system as-is
// This is left in here just for fun :P

// NOTE: - Real and stable 60FPS requires leaving the actual game code alone
#define NEWTICRATERATIO 1 // NOTE: This is unstable stuff just for testing
#define TICRATE (OLDTICRATE*NEWTICRATERATIO)

#define LOGNAME "SRB2CBLog"

// Name of local directory for config files and savegames
// NOTE: The directory here is relative to the user's local directory
#if !defined(_arch_dreamcast) && !defined(_WIN32_WCE) && !defined(GP2X)
	#if ((defined (__unix__) || defined (UNIXLIKE)) && !defined (__CYGWIN__)) && !defined (__APPLE__)
		#define DEFAULTDIR ".SRB2CB"
	#else
		#if defined (__APPLE__)
			#define DEFAULTDIR "Library/Application Support/SRB2CB"
		#else
			#define DEFAULTDIR "SRB2CB"
		#endif
	#endif
#endif

#include "g_state.h"

// Globally used functions such as printf and the game's error function

/**	\brief	The I_Error function

	\param	error	the error message

	\return	void


*/
void I_Error(const char *error, ...) FUNCIERROR;

// console.h
void CONS_Printf(const char *fmt, ...) FUNCPRINTF;

// Print a message only to the log file,
// different and more general use than I_OutputMsg
void CONS_LogPrintf(const char *lpFmt, ...) FUNCPRINTF;

#include "m_swap.h"

// m_misc.h
char *va(const char *format, ...) FUNCPRINTF;

// d_main.c
extern boolean devparm; // development mode (-debug), not to be confused with the in game "devmode"

// =======================
// Misc stuff for later...
// =======================

// if we ever make our alloc stuff...
#define ZZ_Alloc(x) Z_Malloc(x, PU_STATIC, NULL)

// i_system.c, replace getchar() once the keyboard has been appropriated
int I_GetKey(void);

#ifndef min // Double-Check with WATTCP-32's cdefs.h
#define min(x, y) (((x) < (y)) ? (x) : (y))
#endif
#ifndef max // Double-Check with WATTCP-32's cdefs.h
#define max(x, y) (((x) > (y)) ? (x) : (y))
#endif

// An assert-type mechanism.
#ifdef PARANOIA
#define I_Assert(e) ((e) ? (void)0 : I_Error("Assert failed: %s, file %s, line %d", #e, __FILE__, __LINE__))
#else
#define I_Assert(e) ((void)0)
#endif

// The character that separates pathnames. Forward slash on
// most systems, but reverse solidus (\) on Windows and DOS.
#if defined (_WIN32)
	#define PATHSEP "\\"
#else
	#define PATHSEP "/"
#endif

// Compile date and time.
extern const char *compdate, *comptime;

#define MAXBANS 255 // Maximum number of players not allowed to join a netgame

ULONG glpolycount;

// EDIT: Newmem was very unstable (for most things)
// Reference for Z_ to regular C conversion left here for reference
// Experimental better memory management! (But no, it was too crashy)
// The game's memory management can be much more optimized by passing memory management
// directly to C instead of wasting space in the ZONE system, anything that does not need
// Zone memory management(for things like Z_Changetag, etc.)
// is changed, so it's faster, simpler, etc
// You waste over like 2KB for every Zone call compared to a direct C call

// ---- Z_ to regular C conversion ------
// Z_Calloc(size, tag, user) turns into simply memset(malloc(size), 0, size)
// Z_Malloc(size, tag, user) turns into malloc(size)
// Z_Free(size) turns into free(size)
// No Z_ChangeTag, or anything relating to tags is used
// Z_MallocAlign(size, tag, user, alignbits) turns into malloc(size + ((size_t)(1<<alignbits) - 1));
// Z_CallocAlign(size, tag, user, alignbits) turns into memset(malloc(size + ((size_t)(1<<alignbits) - 1)), 0, size);

// maximum number of verts around a convex floor/ceiling polygon
#define MAXPLANEVERTICES 4096 // SRB2CBTODO: Make this bigger

#define DOF // SRB2CBTODO: Depth of Field for OpenGL!!!!
#define MOTIONBLUR // SRB2CBTODO: MOOOTION BLUUUURR
// Use OpenGL's accumulation buffer (only for misc. effects because not all computers handle accum well)
#define GLACCUML
#define GLSTENCIL // Use OpenGL's stencil for reflections and other cool stuff!
//#define HUDFADE // Fades the hud in and out, but comes back if player's score/ring count changes
//#define RINGSCALE // UNSTABLE! Causes crashes when used with crushing objects
// but it's left in here just for reference and stuff

//#define IFRAME // Interpoliated frames
#define SRB2K
#define GDF
#define EENGINE // SRB2CBTODO: AWESOME Misc imports from Eternity engine
#define ESLOPE // SRB2CBTODO: Slopes with OpenGL drawing and physics (new to this engine)

#define FREEFLY

#define FRADIO // Fake radiosity, makes sector lighting much cooler!

//#define WADTEX


#define NEWCLIP // FINALLY some real clipping that doesn't make walls dissappear AND speeds the game up
//#define PAPERMARIO
#ifdef ESLOPE
#define VPHYSICS // Full vector based sonic like physics!
#endif
//#define AWESOME // Some real game changing stuff like camera rolling & omnidirectional movement and stuff
//#define WALLRUN
#define PARTICLES
//#define ANGLE2D // A new kind of 2D mode that works like nights mode
#define TXEND
#ifdef ESLOPE
//#define SESLOPE // Gasp! Slopes in the software renderer! (Also never gonna happen)
#endif
#define THINGSCALING // XSRB2 scale stuff
//#define WALLSPLATS // Draw stuff on walls, now stable and works TODO: Make more versatile
#define CONSCALE // Backport from SVN rev 6815 from Oogaland
//#define EPORTAL // Extensive dynamic map geometry from the Eternity engine
#define SPRITEROLL // SRB2CBTODO:! Make sure this doesn't mess up any other data!
//#define HDRES // 2560 * 1440, Software mode would die, but OpenGL is fun with it
// Per-level handling of water - Optionally allows the character to swim instead of just fall to the bottom of water
// Not very Sonic like, but then again this whole game isn't really Sonic like to begin with :P
//#define CUSTOMWATER
//#define SRB2CBTODO // Chartrans, coronas
#define JTEBOTS // JTE's bots, artificially intellegent players!
//#define SEENAMES // When looking at a player, display thier name
//#define SEENAMES2 // 2.0.6 version SRB2CBTODO: !!

// Disabled code and code under testing

//#define WEAPON_SFX
//#define FISHCAKE /// \todo Remove this to disable cheating. Remove for release!
//#define JOHNNYFUNCODE
//#define CHAOSISNOTDEADYET // Pre-1.08 Chaos gametype code

//#define BLUE_SPHERES // Blue spheres for future use.

#endif // __DOOMDEF__
