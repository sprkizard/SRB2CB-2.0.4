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
/// \brief host/client network commands
///	commands are executed through the command buffer
///	like console commands

#ifndef __D_NETCMD__
#define __D_NETCMD__

#include "command.h"

// console vars
extern consvar_t cv_playername;
extern consvar_t cv_playercolor;
extern consvar_t cv_usemouse;
extern consvar_t cv_usejoystick;
extern consvar_t cv_usejoystick2;
#ifdef LJOYSTICK
extern consvar_t cv_joyport;
extern consvar_t cv_joyport2;
#endif
extern consvar_t cv_joyscale;
extern consvar_t cv_joyscale2;
extern consvar_t cv_controlperkey;

// splitscreen with second mouse
extern consvar_t cv_mouse2port;
extern consvar_t cv_usemouse2;
#if defined (__unix__) || defined (UNIXLIKE)
extern consvar_t cv_mouse2opt;
#endif
extern consvar_t cv_invertmouse2;
extern consvar_t cv_alwaysfreelook2;
extern consvar_t cv_mousemove2;
extern consvar_t cv_mousesens2;
extern consvar_t cv_mlooksens2;

// Some options for when the game is inactive
extern consvar_t cv_inactivemusic; // Play music when the game is inactive
extern consvar_t cv_inactivepause; // Pause the game when its window is inactive
extern consvar_t cv_pauseresume; // Auto-resume the game when it becomes active if it was paused


// normally in p_mobj but the .h is not read
extern consvar_t cv_itemrespawntime;
extern consvar_t cv_itemrespawn;

extern consvar_t cv_flagtime;
extern consvar_t cv_suddendeath;

extern consvar_t cv_skin;

// secondary splitscreen player
extern consvar_t cv_playername2;
extern consvar_t cv_playercolor2;
extern consvar_t cv_skin2;

extern consvar_t cv_tagtype;
extern consvar_t cv_hidetime;

extern consvar_t cv_matchtype;
extern consvar_t cv_friendlyfire;
extern consvar_t cv_pointlimit;
extern consvar_t cv_timelimit;
extern consvar_t cv_numlaps;
extern ULONG timelimitintics;
extern consvar_t cv_allowexitlevel;

extern consvar_t cv_cheats;

extern consvar_t cv_autobalance;
extern consvar_t cv_teamscramble;
extern consvar_t cv_scrambleonchange;

extern consvar_t cv_useranalog, cv_useranalog2;
extern consvar_t cv_analog, cv_analog2;

extern consvar_t cv_netstat;
extern consvar_t cv_splats;

extern consvar_t cv_countdowntime;
extern consvar_t cv_runscripts;
extern consvar_t cv_mute;
extern consvar_t cv_killingdead;
extern consvar_t cv_pause;

extern consvar_t cv_allowteamchange;

extern consvar_t cv_teleporters, cv_superring, cv_supersneakers, cv_invincibility;
extern consvar_t cv_jumpshield, cv_watershield, cv_ringshield, cv_forceshield, cv_bombshield;
extern consvar_t cv_1up, cv_eggmanbox, cv_questionbox;
extern consvar_t cv_recycler;

extern consvar_t cv_objectplace, cv_objflags, cv_mapthingnum, cv_speed, cv_snapto, cv_grid;

extern consvar_t cv_inttime, cv_advancemap, cv_playersforexit;
extern consvar_t cv_soniccd;
extern consvar_t cv_match_scoring;
extern consvar_t cv_overtime;

extern consvar_t cv_realnames;

extern consvar_t cv_resetmusic;

extern consvar_t cv_autoaim, cv_autoaim2;
extern consvar_t cv_ringslinger, cv_setrings, cv_setlives, cv_setcontinues, cv_soundtest, cv_ringcolor;

extern consvar_t cv_specialrings, cv_powerstones, cv_matchboxes, cv_racetype, cv_raceitemboxes;

#ifdef CHAOSISNOTDEADYET
extern consvar_t cv_chaos_spawnrate, cv_chaos_bluecrawla, cv_chaos_redcrawla;
extern consvar_t cv_chaos_crawlacommander, cv_chaos_jettysynbomber, cv_chaos_jettysyngunner;
extern consvar_t cv_chaos_eggmobile1, cv_chaos_eggmobile2, cv_chaos_skim;
#endif

#ifdef JTEBOTS
extern consvar_t cv_botai;
extern consvar_t cv_maxbots;
extern consvar_t cv_botcanflyme;
#endif

// XSrb2, color variable
extern consvar_t cv_allowcolors;

// hacks for menu system.
extern consvar_t cv_dummyteam, cv_dummyscramble;

extern consvar_t cv_skipmapcheck;

extern consvar_t cv_sleep, cv_screenshot_option, cv_screenshot_folder;

extern consvar_t cv_zlib_level, cv_zlib_memory, cv_zlib_strategy;

extern consvar_t cv_zlib_window_bits, cv_zlib_levela, cv_zlib_memorya;

extern consvar_t cv_zlib_strategya, cv_zlib_window_bitsa, cv_apng_disable;

typedef enum
{
	XD_NAMEANDCOLOR = 1,
	XD_WEAPONPREF,  // 2
	XD_KICK,        // 3
	XD_NETVAR,      // 4
	XD_SAY,         // 5
	XD_MAP,         // 6
	XD_EXITLEVEL,   // 7
	XD_ADDFILE,     // 8
	XD_PAUSE,       // 9
	XD_ADDPLAYER,   // 10
	XD_TEAMCHANGE,  // 11
	XD_CLEARSCORES, // 12
	XD_LOGIN,       // 13
	XD_VERIFIED,    // 14
	XD_RANDOMSEED,  // 15
	XD_ORDERPIZZA,  // 16
	XD_RUNSOC,      // 17
	XD_REQADDFILE,  // 18
	XD_DELFILE,     // 19
	XD_SETMOTD,     // 20
	XD_CONSISTENCY, // 21
#ifdef JTEBOTS
	XD_ADDBOT,     // 22
#endif
	MAXNETXCMD
} netxcmd_t;

// So yeah, my dad taught me about these nifty buggers called bitwise structures.
// They can hold and transmit our data without having to mess around with packing our bits ourselves.
// If we utilize these, this code should be 99.9% more dummy-proof, readable and flexible. -Jazz

#if defined(_MSC_VER)
#pragma pack(1)
#endif

// Packet composition for Command_TeamChange_f() ServerTeamChange, etc.
typedef  struct
{
	unsigned int playernum    : 5;  // value 0 to 31
	unsigned int newteam      : 5;  // value 0 to 31
	unsigned int verification : 1;  // value 0 to 1
	unsigned int autobalance  : 1;  // value 0 to 1
	unsigned int scrambled    : 1;  // value 0 to 1
} ATTRPACK changeteam_packet_t;

// Since we do not want other files/modules to know about this data buffer we union it here with a Short Int.
// Other files/modules will hand the short int back to us and we will decode it here.
// We don't have to use a union, but we would then send four bytes instead of two.
typedef union
{
	changeteam_packet_t packet;
	USHORT value;
} ATTRPACK changeteam_union;

// Reminder for myself if I ever need packets of odd composition. -Jazz
// NetPacket = *(( scrambleteams_packet_t *)*cp)++; //lol, pointers

#if defined(_MSC_VER)
#pragma pack()
#endif

// add game commands, needs cleanup
void D_RegisterServerCommands(void);
void D_RegisterClientCommands(void);
void D_SendPlayerConfig(void);
void Command_ExitGame_f(void);
void D_GameTypeChanged(int lastgametype); // not a real _OnChange function anymore
void D_MapChange(int pmapnum, int pgametype, boolean pultmode, int presetplayers, int pdelay, boolean pskipprecutscene, boolean pfromlevelselect);
void ObjectPlace_OnChange(void);

#endif


