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

/// \file
/// \brief Tool for dynamic referencing of hardware rendering functions
///
///	Declaration and definition of the HW rendering
///	functions do have the same name. Originally, the
///	implementation was stored in a separate library.
///	For SDL, we need some function to return the addresses,
///	otherwise we have a conflict with the compiler.

#include "hwsym_sdl.h"
#include "../doomdef.h"

#ifdef _MSC_VER
#pragma warning(disable : 4214 4244)
#endif

#ifdef _WIN32
#include <SDL/SDL.h>
#else
#include "SDL.h"
#endif

#ifdef _MSC_VER
#pragma warning(default : 4214 4244)
#endif

#if defined (_XBOX) || defined (_arch_dreamcast) || defined(GP2X) || defined(_WIN32)
#define NOLOADSO
#endif

#if SDL_VERSION_ATLEAST(1,2,6) && !defined (NOLOADSO)
#include "SDL_loadso.h" // 1.2.6+
#elif !defined (NOLOADSO)
#define NOLOADSO
#endif

#ifdef HWRENDER
#include "../hardware/hw_drv.h"
#include "ogl_sdl.h"
#endif

#ifdef HW3SOUND
#include "../hardware/hw3dsdrv.h"
#endif

//
//
/**	\brief	The *hwSym function

	Stupid function to return function addresses

	\param	funcName	the name of the function
	\param	handle	an object to look in(NULL for self)

	\return	void
*/
//
// SRB2CBTODO: What is this pointer thing?
// This function is needed, but are the startup/addsfx things needed?
void *hwSym(const char *funcName,void *handle)
{
	void *funcPointer = NULL;
	if (0 == strcmp("FinishUpdate", funcName))
		return funcPointer; //&FinishUpdate;
#ifdef STATIC3DS
	else if (0 == strcmp("Startup", funcName))
		funcPointer = &Startup;
	else if (0 == strcmp("AddSfx", funcName))
		funcPointer = &AddSfx;
	else if (0 == strcmp("AddSource", funcName))
		funcPointer = &AddSource;
	else if (0 == strcmp("StartSource", funcName))
		funcPointer = &StartSource;
	else if (0 == strcmp("StopSource", funcName))
		funcPointer = &StopSource;
	else if (0 == strcmp("GetHW3DSVersion", funcName))
		funcPointer = &GetHW3DSVersion;
	else if (0 == strcmp("BeginFrameUpdate", funcName))
		funcPointer = &BeginFrameUpdate;
	else if (0 == strcmp("EndFrameUpdate", funcName))
		funcPointer = &EndFrameUpdate;
	else if (0 == strcmp("IsPlaying", funcName))
		funcPointer = &IsPlaying;
	else if (0 == strcmp("UpdateListener", funcName))
		funcPointer = &UpdateListener;
	else if (0 == strcmp("UpdateSourceParms", funcName))
		funcPointer = &UpdateSourceParms;
	else if (0 == strcmp("SetGlobalSfxVolume", funcName))
		funcPointer = &SetGlobalSfxVolume;
	else if (0 == strcmp("SetCone", funcName))
		funcPointer = &SetCone;
	else if (0 == strcmp("Update3DSource", funcName))
		funcPointer = &Update3DSource;
	else if (0 == strcmp("ReloadSource", funcName))
		funcPointer = &ReloadSource;
	else if (0 == strcmp("KillSource", funcName))
		funcPointer = &KillSource;
	else if (0 == strcmp("GetHW3DSTitle", funcName))
		funcPointer = &GetHW3DSTitle;
#endif
#ifdef NOLOADSO
	else
		funcPointer = handle;
#else
	else if (handle)
		funcPointer = SDL_LoadFunction(handle,funcName);
#endif
	return funcPointer;
}

/**	\brief	The *hwOpen function

	\param	hwfile	Open a handle to the SO

	\return	Handle to SO


*/

void *hwOpen(const char *hwfile)
{
#ifdef NOLOADSO
	(void)hwfile;
	return NULL;
#else
	void *tempso = NULL;
	tempso = SDL_LoadObject(hwfile);
	if (!tempso) CONS_Printf("hwOpen: %s\n",SDL_GetError());
	return tempso;
#endif
}

/**	\brief	The hwClose function

	\param	handle	Close the handle of the SO

	\return	void


*/

void hwClose(void *handle)
{
#ifdef NOLOADSO
	(void)handle;
#else
	SDL_UnloadObject(handle);
#endif
}
