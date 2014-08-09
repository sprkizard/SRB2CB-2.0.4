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
/// \brief Title screen, intro, game evaluation, and credits.

#include "doomdef.h"
#include "doomstat.h"
#include "am_map.h"
#include "dstrings.h"
#include "d_main.h"
#include "f_finale.h"
#include "g_game.h"
#include "hu_stuff.h"
#include "r_local.h"
#include "s_sound.h"
#include "i_video.h"
#include "v_video.h"
#include "w_wad.h"
#include "z_zone.h"
#include "i_system.h"
#include "m_menu.h"
#include "dehacked.h"
#include "g_input.h"
#include "console.h"
#include "m_random.h"
#include "y_inter.h"

credit_t credits[19];

// Stage of animation:
// 0 = text, 1 = art screen
static int finalestage;
static int finalecount;
int titlescrollspeed = 80;

static int timetonext; // Delay between screen changes
static int finaletextcount;
static tic_t animtimer; // Used for some animation timings
static int roidtics; // Asteroid spinning

static int deplete;
static tic_t stoptimer;

// Intro
static int scene;

#define NUMINTROSCENES 16

static tic_t introscenetime[NUMINTROSCENES] =
{
	5*TICRATE,					// STJr Presents
	10*TICRATE + (TICRATE/2),	// Two months had passed since...
	13*TICRATE + (TICRATE/2),	// As it was about to drain the rings...
	15*TICRATE,					// What Sonic, Tails, and Knuckles...
	15*TICRATE + (TICRATE/2),	// About once every year, a strange...
	18*TICRATE + (TICRATE/2),	// Curses! Eggman yelled. That ridiculous...
	20*TICRATE + (TICRATE/4)*3,	// It was only later that he had an idea...
	9*TICRATE + (TICRATE/2),	// Before beginning his scheme, Eggman decided to give Sonic...
	15*TICRATE,					// We're ready to fire in 15 seconds, the robot said...
	16*TICRATE,					// Meanwhile, Sonic was tearing across the zones...
	14*TICRATE,					// Sonic knew he was getting closer to the city...
	13*TICRATE,					// Greenflower City was gone...
	19*TICRATE,					// You're not quite as dead as we thought, huh?...
	17*TICRATE + (TICRATE/2),	// Eggman took this as his cue and blasted off...
	15*TICRATE,					// Easy! We go find Eggman and stop his...
	24*TICRATE,					// I'm just finding what mission obje...
};

#define TEXTSPEED 3
#define TEXTWAIT 250

static const char *finaletext = NULL;
static boolean keypressed = false;

static patch_t *background;
static patch_t *currentanim;
static patch_t *nextanim;
static patch_t *first;
static patch_t *second;
static patch_t *third;

// De-Demo'd Title Screen
static patch_t *ttbanner; // white banner with "robo blast" and "2"
static patch_t *ttwing; // wing background
static patch_t *ttsonic; // "SONIC"
static patch_t *ttswave1; // Title Sonics
static patch_t *ttswave2;
static patch_t *ttswip1;
static patch_t *ttsprep1;
static patch_t *ttsprep2;
static patch_t *ttspop1;
static patch_t *ttspop2;
static patch_t *ttspop3;
static patch_t *ttspop4;
static patch_t *ttspop5;
static patch_t *ttspop6;
static patch_t *ttspop7;

static void F_SkyScroll(void);

static boolean drawemblem = false, drawchaosemblem = false, runningprecutscene = false, precutresetplayer = false;

// De-Demo'd Title Screen
void F_StartTitleScreen(void)
{
	G_SetGamestate(GS_TITLESCREEN);
	CON_ClearHUD();

	// IWAD dependent stuff.

	S_ChangeMusic(mus_titles, looptitle);

	finalecount = 0;
	finalestage = 0;
	animtimer = 0;

	ttbanner = W_CachePatchName("TTBANNER", PU_LEVEL);
	ttwing = W_CachePatchName("TTWING", PU_LEVEL);
	ttsonic = W_CachePatchName("TTSONIC", PU_LEVEL);
	ttswave1 = W_CachePatchName("TTSWAVE1", PU_LEVEL);
	ttswave2 = W_CachePatchName("TTSWAVE2", PU_LEVEL);
	ttswip1 = W_CachePatchName("TTSWIP1", PU_LEVEL);
	ttsprep1 = W_CachePatchName("TTSPREP1", PU_LEVEL);
	ttsprep2 = W_CachePatchName("TTSPREP2", PU_LEVEL);
	ttspop1 = W_CachePatchName("TTSPOP1", PU_LEVEL);
	ttspop2 = W_CachePatchName("TTSPOP2", PU_LEVEL);
	ttspop3 = W_CachePatchName("TTSPOP3", PU_LEVEL);
	ttspop4 = W_CachePatchName("TTSPOP4", PU_LEVEL);
	ttspop5 = W_CachePatchName("TTSPOP5", PU_LEVEL);
	ttspop6 = W_CachePatchName("TTSPOP6", PU_LEVEL);
	ttspop7 = W_CachePatchName("TTSPOP7", PU_LEVEL);
}

// Game end thingy
void F_StartGameEnd(void)
{
	G_SetGamestate(GS_GAMEEND);

	gameaction = ga_nothing;
	playerdeadview = false;
	paused = false;
	CON_ToggleOff();
	CON_ClearHUD();
	S_StopMusic();

	timetonext = TICRATE;
}

void F_StartGameEvaluation(void)
{
	G_SetGamestate(GS_EVALUATION);

	gameaction = ga_nothing;
	playerdeadview = false;
	paused = false;
	CON_ToggleOff();
	CON_ClearHUD();

	if (ALL7EMERALDS(emeralds))
		animtimer = 64;

	finalecount = 0;
}

void F_StartCredits(void)
{
	size_t i = 0;
	size_t j = 0;

	if (creditscutscene)
	{
		F_StartCustomCutscene(creditscutscene - 1, false, false);
		return;
	}

	G_SetGamestate(GS_CREDITS);

	gameaction = ga_nothing;
	playerdeadview = false;
	paused = false;
	CON_ToggleOff();
	CON_ClearHUD();
	S_StopMusic();

	if (!modifiedgame && (grade & 8) && ALL7EMERALDS(emeralds))
		S_ChangeMusic(mus_mapl4m, false);
	else
		S_ChangeMusic(mus_credit, false);

	finalecount = 0;
	animtimer = 0;

	timetonext = 5*TICRATE-1;

	memset(credits, 0, sizeof(credit_t)*19);

	// Initalize the credits table
	strcpy(credits[i].header, "Sonic Team Junior\n");
	strcpy(credits[i].fakenames[j++], "Staff\n");
	j = 0;
	strcpy(credits[i].realnames[j++], "Staff\n");
	credits[i].numnames = 1;
	i++;
	strcpy(credits[i].header, "Producer\n");
	j = 0;
	strcpy(credits[i].fakenames[j++], "SSNTails\n");
	strcpy(credits[i].fakenames[j++], "\n");
	strcpy(credits[i].fakenames[j++], "Director\n");
	strcpy(credits[i].fakenames[j++], "Sonikku\n");
	j = 0;
	strcpy(credits[i].realnames[j++], "Art Freda\n");
	strcpy(credits[i].realnames[j++], "\n");
	strcpy(credits[i].realnames[j++], "Director\n");
	strcpy(credits[i].realnames[j++], "Johnny Wallbank\n");
	credits[i].numnames = (byte)j;
	i++;
	strcpy(credits[i].header, "Game Designers\n");
	j = 0;
	strcpy(credits[i].fakenames[j++], "Sonikku\n");
	strcpy(credits[i].fakenames[j++], "SSNTails\n");
	strcpy(credits[i].fakenames[j++], "Mystic\n");
	j = 0;
	strcpy(credits[i].realnames[j++], "Johnny Wallbank\n");
	strcpy(credits[i].realnames[j++], "Art Freda\n");
	strcpy(credits[i].realnames[j++], "Ben Geyer\n");
	credits[i].numnames = (byte)j;
	i++;
	strcpy(credits[i].header, "Character Designers\n");
	j = 0;
	strcpy(credits[i].fakenames[j++], "Sonikku\n");
	strcpy(credits[i].fakenames[j++], "Instant Sonic\n");
	j = 0;
	strcpy(credits[i].realnames[j++], "Johnny Wallbank\n");
	strcpy(credits[i].realnames[j++], "David Spencer Jr\n");
	credits[i].numnames = (byte)j;
	i++;
	strcpy(credits[i].header, "Visual Design\n");
	j = 0;
	strcpy(credits[i].fakenames[j++], "SSNTails\n");
	j = 0;
	strcpy(credits[i].realnames[j++], "Art Freda\n");
	credits[i].numnames = (byte)j;
	i++;
	strcpy(credits[i].header, "Landscape Design\n");
	j = 0;
	strcpy(credits[i].fakenames[j++], "Sonikku\n");
	strcpy(credits[i].fakenames[j++], "JEV3\n");
	j = 0;
	strcpy(credits[i].realnames[j++], "Johnny Wallbank\n");
	strcpy(credits[i].realnames[j++], "Jarrett Voight\n");
	credits[i].numnames = (byte)j;
	i++;
	strcpy(credits[i].header, "Chief Programmer\n");
	j = 0;
	strcpy(credits[i].fakenames[j++], "SSNTails\n");
	j = 0;
	strcpy(credits[i].realnames[j++], "Art Freda\n");
	credits[i].numnames = (byte)j;
	i++;
	strcpy(credits[i].header, "Programmers\n");
	j = 0;
	strcpy(credits[i].fakenames[j++], "Alam_GBC\n");
	strcpy(credits[i].fakenames[j++], "Jazz\n");
	strcpy(credits[i].fakenames[j++], "Graue\n");
	strcpy(credits[i].fakenames[j++], "MattW CFI\n");
	strcpy(credits[i].fakenames[j++], "Orospakr\n");
	j = 0;
	strcpy(credits[i].realnames[j++], "Alam Arias\n");
	strcpy(credits[i].realnames[j++], "Nathan Giroux\n");
	strcpy(credits[i].realnames[j++], "Scott Feeney\n");
	strcpy(credits[i].realnames[j++], "Matthew Walsh\n");
	strcpy(credits[i].realnames[j++], "Andrew Clunis\n");
	credits[i].numnames = (byte)j;
	i++;
	strcpy(credits[i].header, "Coding Assistants\n");
	j = 0;
	strcpy(credits[i].fakenames[j++], "StroggOnMeth\n");
	strcpy(credits[i].fakenames[j++], "Cyan Helkaraxe\n");
	strcpy(credits[i].fakenames[j++], "Logan_GBA\n");
	strcpy(credits[i].fakenames[j++], "Shuffle\n");
	strcpy(credits[i].fakenames[j++], "Oogaland\n");
	strcpy(credits[i].fakenames[j++], "Jason the Echidna\n");
	j = 0;
	strcpy(credits[i].realnames[j++], "Steven McGranahan\n");
	strcpy(credits[i].realnames[j++], "Cyan Helkaraxe\n");
	strcpy(credits[i].realnames[j++], "Logan Arias\n");
	strcpy(credits[i].realnames[j++], "Matt Marsalko\n");
	strcpy(credits[i].realnames[j++], "Gregor Dick\n");
	strcpy(credits[i].realnames[j++], "John J. Muniz\n");
	credits[i].numnames = (byte)j;
	i++;
	strcpy(credits[i].header, "Level Designers\n");
	j = 0;
	strcpy(credits[i].fakenames[j++], "Sonikku\n");
	strcpy(credits[i].fakenames[j++], "Mystic\n");
	strcpy(credits[i].fakenames[j++], "SSNTails\n");
	strcpy(credits[i].fakenames[j++], "Digiku\n");
	strcpy(credits[i].fakenames[j++], "Torgo\n");
	strcpy(credits[i].fakenames[j++], "Nev3r\n");
	strcpy(credits[i].fakenames[j++], "JEV3\n");
	strcpy(credits[i].fakenames[j++], "Spazzo\n");
	strcpy(credits[i].fakenames[j++], "MattW CFI\n");
	strcpy(credits[i].fakenames[j++], "Jazz\n");
	strcpy(credits[i].fakenames[j++], "FoxBlitzz\n");
	j = 0;
	strcpy(credits[i].realnames[j++], "Johnny Wallbank\n");
	strcpy(credits[i].realnames[j++], "Ben Geyer\n");
	strcpy(credits[i].realnames[j++], "Art Freda\n");
	strcpy(credits[i].realnames[j++], "Marco Zafra\n");
	strcpy(credits[i].realnames[j++], "Erik Nielsen\n");
	strcpy(credits[i].realnames[j++], "Pedro Iceta\n");
	strcpy(credits[i].realnames[j++], "Jarrett Voight\n");
	strcpy(credits[i].realnames[j++], "Michael Antonakes\n");
	strcpy(credits[i].realnames[j++], "Matthew Walsh\n");
	strcpy(credits[i].realnames[j++], "Nathan Giroux\n");
	strcpy(credits[i].realnames[j++], "Dan Hagerstrand\n");
	credits[i].numnames = (byte)j;
	credits[i].smallnames = true;
	i++;
	strcpy(credits[i].header, "Texture Artists\n");
	j = 0;
	strcpy(credits[i].fakenames[j++], "KinkaJoy\n");
	strcpy(credits[i].fakenames[j++], "SSNTails\n");
	strcpy(credits[i].fakenames[j++], "Blaze Hedgehog\n");
	strcpy(credits[i].fakenames[j++], "JEV3\n");
	strcpy(credits[i].fakenames[j++], "Nev3r\n");
	j = 0;
	strcpy(credits[i].realnames[j++], "Buddy Fischer\n");
	strcpy(credits[i].realnames[j++], "Art Freda\n");
	strcpy(credits[i].realnames[j++], "Ryan Bloom\n");
	strcpy(credits[i].realnames[j++], "Jarrett Voight\n");
	strcpy(credits[i].realnames[j++], "Pedro Iceta\n");
	credits[i].numnames = (byte)j;
	i++;
	strcpy(credits[i].header, "Music Production\n");
	j = 0;
	strcpy(credits[i].fakenames[j++], "Bulmybag\n");
	strcpy(credits[i].fakenames[j++], "Arrow\n");
	strcpy(credits[i].fakenames[j++], "Stuf\n");
	strcpy(credits[i].fakenames[j++], "SSNTails\n");
	strcpy(credits[i].fakenames[j++], "Cyan Helkaraxe\n");
	strcpy(credits[i].fakenames[j++], "Red XVI\n");
	strcpy(credits[i].fakenames[j++], "Spazzo\n");
	strcpy(credits[i].fakenames[j++], "Nev3r\n");
	j = 0;
	strcpy(credits[i].realnames[j++], "David Bulmer\n");
	strcpy(credits[i].realnames[j++], "Jarel Jones\n");
	strcpy(credits[i].realnames[j++], "Stefan Rimalia\n");
	strcpy(credits[i].realnames[j++], "Art Freda\n");
	strcpy(credits[i].realnames[j++], "Cyan Helkaraxe\n");
	strcpy(credits[i].realnames[j++], "Malcolm Brown\n");
	strcpy(credits[i].realnames[j++], "Michael Antonakes\n");
	strcpy(credits[i].realnames[j++], "Pedro Iceta\n");
	credits[i].smallnames = true;
	credits[i].numnames = (byte)j;
	i++;
	strcpy(credits[i].header, "Lead Guitar\n");
	j = 0;
	strcpy(credits[i].fakenames[j++], "Big Wave Dave\n");
	strcpy(credits[i].fakenames[j++], "Shane Strife\n");
	j = 0;
	strcpy(credits[i].realnames[j++], "David Spencer Sr\n");
	strcpy(credits[i].realnames[j++], "Shane Sexton\n");
	credits[i].numnames = (byte)j;
	i++;
	strcpy(credits[i].header, "Sound Effects\n");
	j = 0;
	strcpy(credits[i].fakenames[j++], "Sega\n");
	strcpy(credits[i].fakenames[j++], "Instant Sonic\n");
	strcpy(credits[i].fakenames[j++], "Various Sources\n");
	j = 0;
	strcpy(credits[i].realnames[j++], "Sega\n");
	strcpy(credits[i].realnames[j++], "David Spencer Jr\n");
	strcpy(credits[i].realnames[j++], "Various Sources\n");
	credits[i].numnames = (byte)j;
	i++;
	strcpy(credits[i].header, "Official Mascot\n");
	j = 0;
	strcpy(credits[i].fakenames[j++], "Spazzo\n");
	j = 0;
	strcpy(credits[i].realnames[j++], "Michael Antonakes\n");
	credits[i].numnames = (byte)j;
	i++;
	strcpy(credits[i].header, "Sky Sanctuary Team\n");
	j = 0;
	strcpy(credits[i].fakenames[j++], "Boinciel\n");
	strcpy(credits[i].fakenames[j++], "Dark Warrior\n");
	strcpy(credits[i].fakenames[j++], "FoxBlitzz\n");
	strcpy(credits[i].fakenames[j++], "Jazz\n");
	strcpy(credits[i].fakenames[j++], "JEV3\n");
	strcpy(credits[i].fakenames[j++], "MattW CFI\n");
	strcpy(credits[i].fakenames[j++], "Nev3r\n");
	strcpy(credits[i].fakenames[j++], "Senku\n");
	strcpy(credits[i].fakenames[j++], "Shadow Hog\n");
	strcpy(credits[i].fakenames[j++], "Sonict\n");
	strcpy(credits[i].fakenames[j++], "Spazzo\n");
	strcpy(credits[i].fakenames[j++], "SRB2 Playah\n");
	strcpy(credits[i].fakenames[j++], "ST218\n");
	strcpy(credits[i].fakenames[j++], "Tets\n");
	strcpy(credits[i].fakenames[j++], "Torgo\n");
	strcpy(credits[i].fakenames[j++], "Chaos Zero 64\n");
	strcpy(credits[i].fakenames[j++], "Prime 2\n");
	j = 0;
	strcpy(credits[i].realnames[j++], "Paul Clempson\n");	// Boinciel
	strcpy(credits[i].realnames[j++], "Rob Tisdell\n");		// Dark Warrior
	strcpy(credits[i].realnames[j++], "Dan Hagerstrand\n");	// Fox Blitzz
	strcpy(credits[i].realnames[j++], "Nathan Giroux\n");	// Jazz
	strcpy(credits[i].realnames[j++], "Jarrett Voight\n");	// JEV3
	strcpy(credits[i].realnames[j++], "Matthew Walsh\n");	// MattW_CFI
	strcpy(credits[i].realnames[j++], "Pedro Iceta\n");		// Nev3r
	strcpy(credits[i].realnames[j++], "Andrew Moran\n");	// Senku
	strcpy(credits[i].realnames[j++], "Thomas Igoe\n");		// Shadow Hog
	strcpy(credits[i].realnames[j++], "Colin Pfaff\n");		// Sonict
	strcpy(credits[i].realnames[j++], "Michael Antonakes\n"); // Spazzo
	strcpy(credits[i].realnames[j++], "Cody Koester\n");	// SRB2-Playah
	strcpy(credits[i].realnames[j++], "Nick Molina\n");		// ST218
	strcpy(credits[i].realnames[j++], "Bill Reed\n");		// Tets
	strcpy(credits[i].realnames[j++], "Erik Nielsen\n");	// Torgo
	strcpy(credits[i].realnames[j++], "Julio Guir\n");      // Chaos Zero 64
	strcpy(credits[i].realnames[j++], "Samuel Peters\n");   // Prime 2
	credits[i].numnames = (byte)j;
	credits[i].smallnames = true;
	i++;
	strcpy(credits[i].header, "Special Thanks\n");
	j = 0;
	strcpy(credits[i].fakenames[j++], "Doom Legacy Project\n");
	strcpy(credits[i].fakenames[j++], "iD Software\n");
	strcpy(credits[i].fakenames[j++], "Dave Perry\n");
	strcpy(credits[i].fakenames[j++], "MistaED\n");
	strcpy(credits[i].fakenames[j++], "Chrispy\n");
	j = 0;
	strcpy(credits[i].realnames[j++], "Doom Legacy Project\n");
	strcpy(credits[i].realnames[j++], "iD Software\n");
	strcpy(credits[i].realnames[j++], "Dave Perry\n");
	strcpy(credits[i].realnames[j++], "Alex Fuller\n");
	strcpy(credits[i].realnames[j++], "Chris Pryor\n");
	credits[i].numnames = (byte)j;
	i++;
	strcpy(credits[i].header, "In Fond Memory Of\n");
	j = 0;
	strcpy(credits[i].fakenames[j++], "Naoto Oshima\n");
	strcpy(credits[i].fakenames[j++], "Howard Drossin\n");
	strcpy(credits[i].fakenames[j++], "\n");
	strcpy(credits[i].fakenames[j++], "\n");
	j = 0;
	strcpy(credits[i].realnames[j++], "Naoto Oshima\n");
	strcpy(credits[i].realnames[j++], "Howard Drossin\n");
	strcpy(credits[i].realnames[j++], "\n");
	strcpy(credits[i].realnames[j++], "\n");
	credits[i].numnames = (byte)j;
	i++;
}

static int scenenum, cutnum;
static int picxpos, picypos, picnum, pictime;
static int textxpos, textypos;

void F_StartCustomCutscene(int cutscenenum, boolean precutscene, boolean resetplayer)
{
	G_SetGamestate(GS_CUTSCENE);

	gameaction = ga_nothing;
	playerdeadview = false;
	paused = false;
	CON_ToggleOff();
	finaletext = cutscenes[cutscenenum].scene[0].text;

	CON_ClearHUD();

	runningprecutscene = precutscene;

	if (runningprecutscene)
		precutresetplayer = resetplayer;

	scenenum = picnum = 0;
	cutnum = cutscenenum;
	picxpos = cutscenes[cutnum].scene[0].xcoord[0];
	picypos = cutscenes[cutnum].scene[0].ycoord[0];
	textxpos = cutscenes[cutnum].scene[0].textxpos;
	textypos = cutscenes[cutnum].scene[0].textypos;

	pictime = cutscenes[cutnum].scene[0].picduration[0];

	keypressed = false;
	finalestage = 0;
	finalecount = 0;
	finaletextcount = 0;
	timetonext = 0;
	animtimer = cutscenes[cutnum].scene[0].picduration[0]; // Picture duration
	stoptimer = 0;

	if (cutscenes[cutnum].scene[scenenum].musicslot != 0)
		S_ChangeMusic(cutscenes[cutnum].scene[scenenum].musicslot, cutscenes[cutnum].scene[scenenum].musicloop);
}

static void F_IntroTextWrite(void);
// Introduction
void F_StartIntro(void)
{
	S_StopMusic(); // Stop all music playing

	if (introtoplay)
	{
		F_StartCustomCutscene(introtoplay - 1, false, false);
		return;
	}

	G_SetGamestate(GS_INTRO);
	gameaction = ga_nothing;
	playerdeadview = false;
	paused = false;
	CON_ToggleOff();
	CON_ClearHUD();
	finaletext = text[INTRO01TEXT];

	finalestage = finaletextcount = finalecount = animtimer = stoptimer = 0;
	roidtics = BASEVIDWIDTH - 64;
	scene = 0;
	timetonext = introscenetime[scene];
}

// Intro
boolean F_IntroResponder(event_t *event)
{
	int key = event->data1;

	// remap virtual keys (mouse & joystick buttons)
	switch (key)
	{
		case KEY_MOUSE1:
			key = KEY_ENTER;
			break;
		case KEY_MOUSE1 + 1:
			key = KEY_BACKSPACE;
			break;
		case KEY_JOY1:
		case KEY_JOY1 + 2:
			key = KEY_ENTER;
			break;
		case KEY_JOY1 + 3:
			key = 'n';
			break;
		case KEY_JOY1 + 1:
			key = KEY_BACKSPACE;
			break;
		case KEY_HAT1:
			key = KEY_UPARROW;
			break;
		case KEY_HAT1 + 1:
			key = KEY_DOWNARROW;
			break;
		case KEY_HAT1 + 2:
			key = KEY_LEFTARROW;
			break;
		case KEY_HAT1 + 3:
			key = KEY_RIGHTARROW;
			break;
	}

	if (event->type != ev_keydown && key != 301)
		return false;

	if (key != 27 && key != KEY_ENTER && key != KEY_SPACE && key != KEY_BACKSPACE)
		return false;

	if (keypressed)
		return false;

	keypressed = true;
	return true;
}

boolean F_CutsceneResponder(event_t *event)
{
	if (cutnum == introtoplay-1)
		return F_IntroResponder(event);

	return false;
}

boolean F_CreditResponder(event_t *event)
{
	int key = event->data1;

	// remap virtual keys (mouse & joystick buttons)
	switch (key)
	{
		case KEY_MOUSE1:
			key = KEY_ENTER;
			break;
		case KEY_MOUSE1 + 1:
			key = KEY_BACKSPACE;
			break;
		case KEY_JOY1:
		case KEY_JOY1 + 2:
			key = KEY_ENTER;
			break;
		case KEY_JOY1 + 3:
			key = 'n';
			break;
		case KEY_JOY1 + 1:
			key = KEY_BACKSPACE;
			break;
		case KEY_HAT1:
			key = KEY_UPARROW;
			break;
		case KEY_HAT1 + 1:
			key = KEY_DOWNARROW;
			break;
		case KEY_HAT1 + 2:
			key = KEY_LEFTARROW;
			break;
		case KEY_HAT1 + 3:
			key = KEY_RIGHTARROW;
			break;
	}

	if (!(grade & 1))
		return false;

	if (event->type != ev_keydown)
		return false;

	if (key != 27 && key != KEY_ENTER && key != KEY_SPACE && key != KEY_BACKSPACE)
		return false;

	if (keypressed)
		return true;

	keypressed = true;
	return true;
}

// De-Demo'd Title Screen
void F_TitleScreenTicker(void)
{
	finalecount++;
	finalestage += 8;
}

// Game end thingy
//
// F_GameEndTicker
//
void F_GameEndTicker(void)
{
	if (timetonext > 0)
		timetonext--;
	else
		D_StartTitle();
}

void F_GameEvaluationTicker(void)
{
	finalecount++;

	if (finalecount > 10*TICRATE)
		F_StartGameEnd();
}

void F_CreditTicker(void)
{
	finalecount++;

	if (finalecount > 90*TICRATE)
		F_StartGameEvaluation();
}

//
// F_IntroTicker
//
void F_IntroTicker(void)
{
	// advance animation
	finalecount++;
	finaletextcount++;

	if (finalecount % 3*NEWTICRATERATIO == 0)
		roidtics--;

	timetonext--;

	// check for skipping
	if (keypressed)
		keypressed = false;
}

void F_CutsceneTicker(void)
{
	int i;

	// advance animation
	finalecount++;
	finaletextcount++;

	for (i = 0; i < MAXPLAYERS; i++)
	{
		if (netgame && i != serverplayer && i != adminplayer)
			continue;

		if (players[i].cmd.buttons & BT_USE)
		{
			keypressed = false;
			finaletextcount += 64;
			if (timetonext)
				timetonext = 2;
		}
	}
}

//
// F_WriteText
//
static void F_WriteText(int cx, int cy)
{
	int count, c, w, originalx = cx;
	const char *ch = finaletext; // draw some of the text onto the screen

	count = (finaletextcount - 10)/2;

	if (count < 0)
		count = 0;

	if (timetonext == 1 || !ch)
	{
		finaletextcount = 0;
		timetonext = 0;
		roidtics = BASEVIDWIDTH - 64;
		return;
	}

	for (; count; count--)
	{
		c = *ch++;
		if (!c)
			break;

		if (c == '#')
		{
			if (!timetonext)
			{
				if (finaletext == text[INTRO16TEXT])
					timetonext = 12*TICRATE + 1;
				else
					timetonext = 5*TICRATE + 1;
			}
			break;
		}
		if (c == '\n')
		{
			cx = originalx;
			cy += 12;
			continue;
		}

		c = toupper(c) - HU_FONTSTART;
		if (c < 0 || (c >= HU_REALFONTSIZE && c != '~' - HU_FONTSTART && c != '`' - HU_FONTSTART)) /// \note font end hack
		{
			cx += 4;
			continue;
		}

		w = SHORT(hu_font[c]->width);
		if (cx + w > vid.width)
			break;
		V_DrawScaledPatch(cx, cy, 0, hu_font[c]);
		cx += w;
	}
}

static void F_WriteCutsceneText(void)
{
	int count, c, w, originalx = textxpos, cx = textxpos, cy = textypos;
	const char *ch = finaletext; // draw some of the text onto the screen

	count = (finaletextcount - 10)/2;

	if (count < 0)
		count = 0;

	if (timetonext == 1 || !ch)
	{
		finaletextcount = 0;
		timetonext = 0;
		roidtics = BASEVIDWIDTH - 64;
		return;
	}

	for (; count; count--)
	{
		c = *ch++;
		if (!c)
			break;

		if (c == '#')
		{
			if (!timetonext)
				timetonext = 5*TICRATE + 1;
			break;
		}
		if (c == '\n')
		{
			cx = originalx;
			cy += 12;
			continue;
		}

		c = toupper(c) - HU_FONTSTART;
		if (c < 0 || (c >= HU_REALFONTSIZE && c != '~' - HU_FONTSTART && c != '`' - HU_FONTSTART))
		{ /// \note font end hack
			cx += 4;
			continue;
		}

		w = SHORT(hu_font[c]->width);
		if (cx + w > vid.width)
			break;
		V_DrawScaledPatch(cx, cy, 0, hu_font[c]);
		cx += w;
	}
}

//
// F_IntroTextWrite
//
static void F_IntroTextWrite(void)
{
	boolean nobg = false, highres = false;
	int cx = 8, cy = 128;

	// DRAW A FULL PIC INSTEAD OF FLAT!
	if (finaletext == text[INTRO01TEXT])
		nobg = true;
	else if (finaletext == text[INTRO02TEXT])
		background = W_CachePatchName("INTRO1", PU_CACHE);
	else if (finaletext == text[INTRO03TEXT])
	{
		background = W_CachePatchName("INTRO2", PU_CACHE);
		highres = true;
	}
	else if (finaletext == text[INTRO04TEXT])
		background = W_CachePatchName("INTRO3", PU_CACHE);
	else if (finaletext == text[INTRO05TEXT])
		background = W_CachePatchName("INTRO4", PU_CACHE);
	else if (finaletext == text[INTRO06TEXT])
	{
		background = W_CachePatchName("DRAT", PU_CACHE);
		highres = true;
	}
	else if (finaletext == text[INTRO07TEXT])
	{
		background = W_CachePatchName("INTRO6", PU_CACHE);
		cx = 180;
		cy = 8;
	}
	else if (finaletext == text[INTRO08TEXT])
		background = W_CachePatchName("SGRASS1", PU_CACHE);
	else if (finaletext == text[INTRO09TEXT])
	{
		background = W_CachePatchName("WATCHING", PU_CACHE);
		highres = true;
	}
	else if (finaletext == text[INTRO10TEXT])
	{
		background = W_CachePatchName("ZOOMING", PU_CACHE);
		highres = true;
	}
	else if (finaletext == text[INTRO11TEXT])
		nobg = true;
	else if (finaletext == text[INTRO12TEXT])
		background = W_CachePatchName("INTRO5", PU_CACHE);
	else if (finaletext == text[INTRO13TEXT])
	{
		background = W_CachePatchName("REVENGE", PU_CACHE);
		highres = true;
	}
	else if (finaletext == text[INTRO14TEXT])
	{
		nobg = true;
		cx = 8;
		cy = 8;
	}
	else if (finaletext == text[INTRO15TEXT])
	{
		background = W_CachePatchName("SONICDO1", PU_CACHE);
		highres = true;
		cx = 224;
		cy = 8;
	}
	else if (finaletext == text[INTRO16TEXT])
	{
		background = W_CachePatchName("INTRO7", PU_CACHE);
		highres = true;
	}

	V_DrawFill(0, 0, vid.width, vid.height, 31);

	if (finaletext == text[INTRO01TEXT])
	{
		V_DrawCreditString(160 - (V_CreditStringWidth("SONIC TEAM JR")/2), 80, 0, "SONIC TEAM JR");
		V_DrawCreditString(160 - (V_CreditStringWidth("PRESENTS")/2), 96, 0, "PRESENTS");
	}
	else if (finaletext == text[INTRO11TEXT])
	{
		if (finaletextcount > 8*TICRATE && finaletextcount < 9*TICRATE)
		{
			if (!(finalecount & 3*NEWTICRATERATIO))
				background = W_CachePatchName("BRITEGG1", PU_CACHE);
			else
				background = W_CachePatchName("DARKEGG1", PU_CACHE);

			V_DrawScaledPatch(0, 0, 0, background);
		}
		else if (finaletextcount > 10*TICRATE && finaletextcount < 11*TICRATE)
		{
			if (!(finalecount & 3*NEWTICRATERATIO))
				background = W_CachePatchName("BRITEGG2", PU_CACHE);
			else
				background = W_CachePatchName("DARKEGG2", PU_CACHE);

			V_DrawScaledPatch(0, 0, 0, background);
		}
		else if (finaletextcount > 12*TICRATE && finaletextcount < 13*TICRATE)
		{
			if (!(finalecount & 3*NEWTICRATERATIO))
				background = W_CachePatchName("BRITEGG3", PU_CACHE);
			else
				background = W_CachePatchName("DARKEGG3", PU_CACHE);

			V_DrawScaledPatch(0, 0, 0, background);
		}
		else
		{
			F_SkyScroll();
			if (timetonext == 6)
			{
				stoptimer = finalecount;
				animtimer = finalecount % 16*NEWTICRATERATIO;
			}
			else if (timetonext >= 0 && timetonext < 6)
			{
				animtimer = stoptimer;
				deplete -= 32;
			}
			else
			{
				animtimer = finalecount % 16*NEWTICRATERATIO;
				deplete = 160;
			}

			if (finalecount & 1*NEWTICRATERATIO)
			{
				V_DrawScaledPatch(deplete, 8, 0, W_CachePatchName("RUN2", PU_CACHE));
				V_DrawScaledPatch(deplete, 72, 0, W_CachePatchName("PEELOUT2", PU_CACHE));
			}
			else
			{
				V_DrawScaledPatch(deplete, 8, 0, W_CachePatchName("RUN1", PU_CACHE));
				V_DrawScaledPatch(deplete, 72, 0, W_CachePatchName("PEELOUT1", PU_CACHE));
			}
			V_DrawFill(0, 112, vid.width, (int)(vid.height - 112*vid.fdupy), 31);
		}
	}
	else if (finaletext == text[INTRO08TEXT] && timetonext > 0 && finaletextcount >= 5*TICRATE
		&& finaletextcount > 0)
	{
		background = W_CachePatchName("SGRASS5", PU_CACHE);
	}

	if (!nobg)
	{
		if (highres)
			V_DrawSmallScaledPatch(0, 0, 0, background);
		else
			V_DrawScaledPatch(0, 0, 0, background);
	}

	if (finaletext == text[INTRO14TEXT])
	{
		V_DrawFill(0, 0, vid.width, vid.height, 31);
		V_DrawSmallScaledPatch(144, 0, 0, W_CachePatchName("TAILSSAD", PU_CACHE));
	}
	else if (finaletext == text[INTRO05TEXT]) // The asteroid SPINS!
	{
		if (roidtics >= 0)
			V_DrawScaledPatch(roidtics, 24, 0,
				W_CachePatchName(va("ROID00%.2d", finaletextcount%35), PU_CACHE));
	}
	else if (finaletext == text[INTRO06TEXT])
	{
		if (finaletextcount == 5*TICRATE + (TICRATE/5)*3)
		{
			if (rendermode != render_none)
			{
				F_WriteText(cx, cy);

				F_WipeStartScreen();

				V_DrawScaledPatch(0, 0, 0, W_CachePatchName("RADAR", PU_CACHE));
				// draw some of the text onto the screen
				F_WriteText(cx, cy);

				F_WipeEndScreen(0, 0, vid.width, vid.height);

				F_RunWipe(TICRATE);
			}
		}
		else if (finaletextcount > 5*TICRATE+(TICRATE/5)*3)
			V_DrawScaledPatch(0, 0, 0, W_CachePatchName("RADAR", PU_CACHE));
	}
	else if (finaletext == text[INTRO13TEXT])
	{
		if (finaletextcount == 9*TICRATE)
		{
			if (rendermode != render_none)
			{
				F_WriteText(cx, cy);

				F_WipeStartScreen();

				V_DrawSmallScaledPatch(0, 0, 0, W_CachePatchName("CONFRONT", PU_CACHE));
				// draw some of the text onto the screen
				F_WriteText(cx, cy);

				F_WipeEndScreen(0, 0, vid.width, vid.height);

				F_RunWipe(TICRATE);
			}
		}
		else if (finaletextcount > 9*TICRATE)
			V_DrawSmallScaledPatch(0, 0, 0, W_CachePatchName("CONFRONT", PU_CACHE));
	}
	else if (finaletext == text[INTRO15TEXT])
	{
		if (finaletextcount == 7*TICRATE)
		{
			if (rendermode != render_none)
			{
				F_WriteText(cx, cy);

				F_WipeStartScreen();

				V_DrawSmallScaledPatch(0, 0, 0, W_CachePatchName("SONICDO2", PU_CACHE));
				// draw some of the text onto the screen
				F_WriteText(cx, cy);

				F_WipeEndScreen(0, 0, vid.width, vid.height);

				F_RunWipe(TICRATE);
			}
		}
		else if (finaletextcount > 7*TICRATE)
			V_DrawSmallScaledPatch(0, 0, 0, W_CachePatchName("SONICDO2", PU_CACHE));
	}

	if (animtimer)
		animtimer--;

	if (finaletext == text[INTRO08TEXT] && finaletextcount > 5*TICRATE)
	{
		first = W_CachePatchName("SGRASS2", PU_CACHE);
		second = W_CachePatchName("SGRASS3", PU_CACHE);
		third = W_CachePatchName("SGRASS4", PU_CACHE);

		if (finaletextcount == 5*TICRATE + 1)
		{
			currentanim = first;
			nextanim = second;
			animtimer = TICRATE/7 + 1;
		}
		else if (animtimer == 1 && currentanim == first)
		{
			currentanim = second;
			nextanim = third;
			animtimer = TICRATE/7 + 1;
		}
		else if (animtimer == 1 && currentanim == second)
			currentanim = third;

		if (currentanim)
			V_DrawScaledPatch(123, 4, 0, currentanim);
	}

	F_WriteText(cx, cy);
}

//
// F_DrawPatchCol
//
static void F_DrawPatchCol(int x, patch_t *patch, int col, int yrepeat)
{
	const column_t *column;
	const byte *source;
	byte *desttop, *dest = NULL;
	const byte *deststop;
	size_t count;

	column = (column_t *)((byte *)patch + LONG(patch->columnofs[col]));
	desttop = screens[0] + x*vid.dupx;
	deststop = screens[0] + vid.width * vid.height * vid.bpp;

	// step through the posts in a column
	while (column->topdelta != 0xff)
	{
		source = (const byte *)column + 3;
		dest = desttop + column->topdelta*vid.width;
		count = column->length;

		while (count--)
		{
			int dupycount = vid.dupy;

			while (dupycount--)
			{
				int dupxcount = vid.dupx;
				while (dupxcount-- && dest <= deststop)
					*dest++ = *source;

				dest += (vid.width - vid.dupx);
			}
			source++;
		}
		column = (const column_t *)((const byte *)column + column->length + 4);
	}

	// repeat a second time, for yrepeat number of pixels
	if (yrepeat)
	{
		column = (column_t *)((byte *)patch + LONG(patch->columnofs[col]));
		while (column->topdelta != 0xff)
		{
			source = (const byte *)column + 3;
			count = column->length;

			while (count--)
			{
				int dupycount = vid.dupy;

				while (dupycount--)
				{
					int dupxcount = vid.dupx;
					while (dupxcount-- && dest <= deststop)
						*dest++ = *source;

					dest += (vid.width - vid.dupx);
				}
				source++;
			}
			if (!--yrepeat)
				break;
			column = (const column_t *)((const byte *)column + column->length + 4);
		}
	}
}

//
// F_SkyScroll
//
static void F_SkyScroll(void) // TODO: SRB2CBTODO: Do fancy visuals in the menu and stuff!
{
	int scrolled, x, mx, fakedwidth;
	patch_t *pat;

	pat = W_CachePatchName("TITLESKY", PU_CACHE);

	animtimer = ((finalecount*((gamestate == GS_INTRO || gamestate == GS_INTRO2) ? (titlescrollspeed/NEWTICRATERATIO)*4 : (titlescrollspeed/NEWTICRATERATIO)))/16) % SHORT(pat->width);
	
	fakedwidth = vid.width / vid.dupx;
	
	if (rendermode == render_soft)
	{
		int yr = 0;
		
		if (vid.fdupy > vid.dupy)
			yr = vid.height - vid.dupy*SHORT(pat->height);
		
		scrolled = BASEVIDWIDTH - animtimer;
		if (scrolled > BASEVIDWIDTH)
			scrolled = BASEVIDWIDTH;
		if (scrolled < 0)
			scrolled = 0;
		for (x = 0, mx = 0; x < fakedwidth; x++, mx++)
		{
			if (mx >= SHORT(pat->width))
				mx = 0;
			
			if (mx + scrolled < SHORT(pat->width))
				F_DrawPatchCol(x, pat, mx + scrolled, yr);
			else
				F_DrawPatchCol(x, pat, mx + scrolled - SHORT(pat->width), yr);
		}
	}
#ifdef HWRENDER
	else if (rendermode == render_opengl)
	{
		scrolled = animtimer;
		if (scrolled > 0)
			V_DrawScaledPatch(scrolled - SHORT(pat->width), 0, 0, pat);
		while (scrolled < BASEVIDWIDTH)
		{
			V_DrawScaledPatch(scrolled, 0, 0, pat);
			scrolled += SHORT(pat->width);
		}
	}
#endif
}

#ifdef AWESOME
// JTE's RANDOM turns num into a long between 0 and max, assuming num is a number from 0 to 255.
#define RANDOM(num,max) ((long)(max)*(long)(num)/256)
typedef struct menuparticles_s
	{
		USHORT x,y;
		char momx,momy;
		patch_t *patch;
		
		struct menuparticles_s *next;
	} menuparticles_t;
static menuparticles_t *menuparticles = NULL;

static void F_MenuParticles(void)
{
	//S_StartSound(NULL, sfx_litng1); <--should be some custom soc defined sound
	
	menuparticles_t *particles, *last;
	static int particletimer = 0; // how many particles a tic?
	
	// Add another one!
	{
		char momx = 0,momy = 0;
		patch_t *patch = NULL;
		
		particletimer++;
		if (cv_speed.value == 2) // snow for Christmas!
		{
			patch = W_CachePatchName(va("SNO1%c0", (int)(RANDOM(M_Random(),3)+'A')), PU_CACHE);
			momx = (char)(RANDOM(M_Random(),6)-3);
			momy = 1;
			particletimer = -1;
		}
		
		else
		{
			patch = W_CachePatchName("RAINA0", PU_CACHE); // Rain in the menu
			momx = (char)(RANDOM(M_Random(),0)+0);  // Just a little sideways momentum for the rain
			momy = (char)(RANDOM(M_Random(),10)+10); // Randomly have the rain drops fall at a speed around 20
			particletimer = -1;
		}
		
		while(particletimer < 0)
		{
			particles = ZZ_Alloc(sizeof(menuparticles_t));
			particles->next = menuparticles;
			menuparticles = particles;
			
			particles->patch = patch;
			particles->x = (USHORT)RANDOM(M_Random(),BASEVIDWIDTH);
			if (particles->momy >= 0)
				particles->y = (USHORT)(rendermode == render_soft ? particles->patch->topoffset : 0);
			else
				particles->y = (USHORT)(BASEVIDWIDTH + particles->patch->topoffset);
			particles->momx = momx;
			particles->momy = momy;
			particletimer++;
		}
	}
	
	// Draw and tick the precipitation
	if (menuparticles && particles)
	{
		for(particles = menuparticles, last = NULL; particles; particles = particles->next)
		{
			V_DrawScaledPatch(particles->x, particles->y, 0, particles->patch);
			
			// Move it
			particles->x = (USHORT)(particles->x + particles->momx);
			particles->y = (USHORT)(particles->y + particles->momy);
			
			// Wrap around the X axis
			if (particles->x > BASEVIDWIDTH)
			{
				if (particles->momx < 0)
					particles->x += BASEVIDWIDTH;
				else
					particles->x -= BASEVIDWIDTH;
			}
			
			// At the bottom? Free it.
			if (particles->y > BASEVIDHEIGHT + particles->patch->topoffset)
			{
				if (last)
					last->next = particles->next;
				else
					last = particles->next;
				if (menuparticles == particles)
					menuparticles = last;
				Z_Free(particles);
				particles = last;
				continue;
			}
			
			// Repeat
			last = particles;
		}
	}
	
}
#endif

// De-Demo'd Title Screen
void F_TitleScreenDrawer(void)
{
	// Draw that sky!
	F_SkyScroll();
	
#ifdef AWESOME
	// Add particle effects in the menu, because we can
	F_MenuParticles();
#endif

	V_DrawScaledPatch(30, 14, 0, ttwing);

	if (finalecount < 56*NEWTICRATERATIO)
	{
		if (finalecount >= 35*NEWTICRATERATIO && finalecount < 36*NEWTICRATERATIO)
			V_DrawScaledPatch(115, 15, 0, ttspop1);
		else if (finalecount >= 36*NEWTICRATERATIO && finalecount < 37*NEWTICRATERATIO)
			V_DrawScaledPatch(114, 15, 0,ttspop2);
		else if (finalecount >= 37*NEWTICRATERATIO && finalecount < 38*NEWTICRATERATIO)
			V_DrawScaledPatch(113, 15, 0,ttspop3);
		else if (finalecount >= 38*NEWTICRATERATIO && finalecount < 39*NEWTICRATERATIO)
			V_DrawScaledPatch(112, 15, 0,ttspop4);
		else if (finalecount >= 39*NEWTICRATERATIO && finalecount < 40*NEWTICRATERATIO)
			V_DrawScaledPatch(111, 15, 0,ttspop5);
		else if (finalecount >= 40*NEWTICRATERATIO && finalecount < 41*NEWTICRATERATIO)
			V_DrawScaledPatch(110, 15, 0, ttspop6);
		else if (finalecount >= 41*NEWTICRATERATIO && finalecount < 44*NEWTICRATERATIO)
			V_DrawScaledPatch(109, 15, 0, ttspop7);
		else if (finalecount >= 44*NEWTICRATERATIO && finalecount < 48*NEWTICRATERATIO)
			V_DrawScaledPatch(108, 12, 0, ttsprep1);
		else if (finalecount >= 48*NEWTICRATERATIO && finalecount < 53*NEWTICRATERATIO)
			V_DrawScaledPatch(107, 9, 0, ttsprep2);
		else if (finalecount >= 53*NEWTICRATERATIO && finalecount < 56*NEWTICRATERATIO)
			V_DrawScaledPatch(106, 6, 0, ttswip1);
		
		V_DrawScaledPatch(93, 106, 0, ttsonic);
	}
	else
	{
		V_DrawScaledPatch(93, 106, 0, ttsonic);
		if (finalecount/5 & 1*NEWTICRATERATIO)
			V_DrawScaledPatch(100, 3, 0, ttswave1);
		else
			V_DrawScaledPatch(100,3, 0, ttswave2);
	}

	V_DrawScaledPatch(48, 142, 0, ttbanner);
}

// Game End Sequence
//
// F_GameEndDrawer
//
void F_GameEndDrawer(void)
{
}

#define INTERVAL 50
#define TRANSLEVEL V_8020TRANS

void F_GameEvaluationDrawer(void)
{
	int x, y;
	const fixed_t radius = 48*FRACUNIT;
	angle_t fa;

	V_DrawFill(0, 0, vid.width, vid.height, 31);

	// Draw the text of the status
	if (animtimer == 64)
		V_DrawString(114, 16, 0, "GOT THEM ALL!");
	else
		V_DrawString(124, 16, 0, "TRY AGAIN!");

	finalestage++;
	timetonext = finalestage;

	fa = FINEANGLE_C(timetonext);
	x = 160 + FixedMul(FixedMul(FINECOSINE(fa),radius),1);
	y = 100 + FixedMul(FixedMul(FINESINE(fa),radius),1);

	if (emeralds & EMERALD1)
		V_DrawScaledPatch(x, y, 0, W_CachePatchName("CEMGA0", PU_CACHE));
	else
		V_DrawTranslucentPatch(x, y, TRANSLEVEL|V_TOPLEFT, W_CachePatchName("CEMGA0", PU_CACHE));

	timetonext += INTERVAL;

	fa = FINEANGLE_C(timetonext);
	x = 160 + FixedMul(FixedMul(FINECOSINE(fa),radius),1);
	y = 100 + FixedMul(FixedMul(FINESINE(fa),radius),1);

	if (emeralds & EMERALD2)
		V_DrawScaledPatch(x, y, 0, W_CachePatchName("CEMGB0", PU_CACHE));
	else
		V_DrawTranslucentPatch(x, y, TRANSLEVEL|V_TOPLEFT, W_CachePatchName("CEMGB0", PU_CACHE));

	timetonext += INTERVAL;

	fa = FINEANGLE_C(timetonext);
	x = 160 + FixedMul(FixedMul(FINECOSINE(fa),radius),1);
	y = 100 + FixedMul(FixedMul(FINESINE(fa),radius),1);

	if (emeralds & EMERALD3)
		V_DrawScaledPatch(x, y, 0, W_CachePatchName("CEMGC0", PU_CACHE));
	else
		V_DrawTranslucentPatch(x, y, TRANSLEVEL|V_TOPLEFT, W_CachePatchName("CEMGC0", PU_CACHE));

	timetonext += INTERVAL;

	fa = FINEANGLE_C(timetonext);
	x = 160 + FixedMul(FixedMul(FINECOSINE(fa),radius),1);
	y = 100 + FixedMul(FixedMul(FINESINE(fa),radius),1);

	if (emeralds & EMERALD4)
		V_DrawScaledPatch(x, y, 0, W_CachePatchName("CEMGD0", PU_CACHE));
	else
		V_DrawTranslucentPatch(x, y, TRANSLEVEL|V_TOPLEFT, W_CachePatchName("CEMGD0", PU_CACHE));

	timetonext += INTERVAL;

	fa = FINEANGLE_C(timetonext);
	x = 160 + FixedMul(FixedMul(FINECOSINE(fa),radius),1);
	y = 100 + FixedMul(FixedMul(FINESINE(fa),radius),1);

	if (emeralds & EMERALD5)
		V_DrawScaledPatch(x, y, 0, W_CachePatchName("CEMGE0", PU_CACHE));
	else
		V_DrawTranslucentPatch(x, y, TRANSLEVEL|V_TOPLEFT, W_CachePatchName("CEMGE0", PU_CACHE));

	timetonext += INTERVAL;

	fa = FINEANGLE_C(timetonext);
	x = 160 + FixedMul(FixedMul(FINECOSINE(fa),radius),1);
	y = 100 + FixedMul(FixedMul(FINESINE(fa),radius),1);

	if (emeralds & EMERALD6)
		V_DrawScaledPatch(x, y, 0, W_CachePatchName("CEMGF0", PU_CACHE));
	else
		V_DrawTranslucentPatch(x, y, TRANSLEVEL|V_TOPLEFT, W_CachePatchName("CEMGF0", PU_CACHE));

	timetonext += INTERVAL;

	fa = FINEANGLE_C(timetonext);
	x = 160 + FixedMul(FixedMul(FINECOSINE(fa),radius),1);
	y = 100 + FixedMul(FixedMul(FINESINE(fa),radius),1);

	if (emeralds & EMERALD7)
		V_DrawScaledPatch(x, y, 0, W_CachePatchName("CEMGG0", PU_CACHE));
	else
		V_DrawTranslucentPatch(x, y, TRANSLEVEL|V_TOPLEFT, W_CachePatchName("CEMGG0", PU_CACHE));

	if (finalestage >= 360)
		finalestage -= 360;

	if (finalecount == 5*TICRATE)
	{
		if ((!modifiedgame || savemoddata) && !(netgame || multiplayer) && mapheaderinfo[gamemap-1].nextlevel == 1102)
		{
			boolean alreadyplayed = false;
			int bitcount = 0;
			int i;

			if (!(emblemlocations[MAXEMBLEMS-2].collected))
			{
				emblemlocations[MAXEMBLEMS-2].collected = true; // For completing the game.
				S_StartSound(NULL, sfx_ncitem);
				alreadyplayed = true;
				drawemblem = true;
			}

			if (ALL7EMERALDS(emeralds))
			{
				if (!(emblemlocations[MAXEMBLEMS-1].collected))
				{
					emblemlocations[MAXEMBLEMS-1].collected = true;

					if (!alreadyplayed)
						S_StartSound(NULL, sfx_ncitem);

					drawchaosemblem = true;
				}
				grade |= 2; // Now you can access Mario!
			}

			for (i = 0; i < MAXEMBLEMS; i++)
			{
				if (emblemlocations[i].collected)
					bitcount++;
			}


			if (bitcount >= numemblems/2) // Got half of the emblems
				grade |= 4;

			if (bitcount >= numemblems/4) // NiGHTS
				grade |= 16;

			if (bitcount == numemblems) // Got ALL emblems!
				grade |= 8;

			grade |= 1; // Just for completing the game.

			timesbeaten++;

			// An award for beating ultimate mode, perhaps?
			if (ultimatemode)
				grade |= 256;

			// Other unlockables
			if (gamemap == 29) // Cleared NiGHTS
				grade |= 64;
			else if (gamemap == 32) // Cleared Mario
				grade |= 32;
			else if (gamemap == 132) // Cleared SRB1
				grade |= 1024;

			if (cursaveslot != -1)
				G_SaveGame((unsigned int)cursaveslot);
		}
	}

	G_SaveGameData();

	if (finalecount >= 5*TICRATE)
	{
		if (drawemblem)
			V_DrawScaledPatch(120, 192, 0, W_CachePatchName("NWNGA0", PU_CACHE));

		if (drawchaosemblem)
			V_DrawScaledPatch(200, 192, 0, W_CachePatchName("NWNGA0", PU_CACHE));

		V_DrawString(8, 16, V_YELLOWMAP, "Unlocked:");

		if (savemoddata)
		{
			int i;
			boolean unlocked;
			int startcoord = 32;

			for (i = 0; i < 15; i++)
			{
				unlocked = false;

				if (customsecretinfo[i].neededemblems)
				{
					unlocked = M_GotEnoughEmblems(customsecretinfo[i].neededemblems);

					if (unlocked && customsecretinfo[i].neededtime)
						unlocked = M_GotLowEnoughTime(customsecretinfo[i].neededtime);
				}
				else if (customsecretinfo[i].neededtime)
					unlocked = M_GotLowEnoughTime(customsecretinfo[i].neededtime);
				else
					unlocked = (grade & customsecretinfo[i].neededgrade);

				if (unlocked)
				{
					V_DrawString(8, startcoord, 0, customsecretinfo[i].name);

					startcoord += 8;
				}
			}
		}
		else
		{
			if (grade & 1)
				V_DrawString(8, 32, 0, "Level Select");

			if (grade & 2)
				V_DrawString(8, 40, 0, "SRB1 Remake");

			if (grade & 4)
				V_DrawString(8, 48, 0, "Mario");

			if (grade & 8)
				V_DrawString(8, 56, 0, "Pandora's Box");

			if (grade & 16)
				V_DrawString(8, 64, 0, "NiGHTS");

			if (netgame)
				V_DrawString(8, 96, V_YELLOWMAP, "Prizes only\nawarded in\nsingle player!");
			else if (modifiedgame)
				V_DrawString(8, 96, V_YELLOWMAP, "Prizes not\nawarded in\nmodified games!");
		}
	}
}

static void F_DrawCreditScreen(credit_t *creditpassed)
{
	int i, height, y;

	if (creditpassed->smallnames)
		height = BASEVIDHEIGHT/((creditpassed->numnames/2)+1);
	else
		height = BASEVIDHEIGHT/(creditpassed->numnames+1);

	switch (animtimer)
	{
		case 1:
		case 2:
			if (ultimatemode)
				V_DrawSmallScaledPatch(204, 118, 0, W_CachePatchName("ACRED02", PU_CACHE));
			else
				V_DrawSmallScaledPatch(8, 112, 0, W_CachePatchName("CREDIT01", PU_CACHE));
			break;
		case 3:
		case 4:
			if (ultimatemode)
			{
				V_DrawSmallScaledPatch(234, 118, 0, W_CachePatchName("ACRED01", PU_CACHE));
				V_DrawSmallScaledPatch(4, 4, 0, W_CachePatchName("ACRED03", PU_CACHE));
			}
			else
			{
				V_DrawSmallScaledPatch(4, 4, 0, W_CachePatchName("CREDIT13", PU_CACHE));
				V_DrawSmallScaledPatch(250, 100, 0, W_CachePatchName("CREDIT12", PU_CACHE));
			}
			break;
		case 5:
			if (ultimatemode)
				V_DrawSmallScaledPatch(4, 72, 0, W_CachePatchName("ACRED04", PU_CACHE));
			else
				V_DrawSmallScaledPatch(8, 0, 0, W_CachePatchName("CREDIT03", PU_CACHE));
			break;
		case 6:
			if (ultimatemode)
				V_DrawSmallScaledPatch(55, 0, 0, W_CachePatchName("ACRED05", PU_CACHE));
			else
				V_DrawSmallScaledPatch(248, 110, 0, W_CachePatchName("CREDIT11", PU_CACHE));
			break;
		case 7:
		case 8:
			if (ultimatemode)
				V_DrawSmallScaledPatch(8, 108, 0, W_CachePatchName("ACRED06", PU_CACHE));
			else
				V_DrawSmallScaledPatch(8, 112, 0, W_CachePatchName("CREDIT04", PU_CACHE));
			break;
		case 9:
			if (ultimatemode)
				V_DrawSmallScaledPatch((BASEVIDWIDTH/2) - 46, 102, 0, W_CachePatchName("ACRED07", PU_CACHE));
			else
				V_DrawSmallScaledPatch((BASEVIDWIDTH/2) - 48, 108, 0, W_CachePatchName("CREDIT10", PU_CACHE));
			break;
		case 10:
			if (ultimatemode)
				V_DrawSmallScaledPatch(202, 128, 0, W_CachePatchName("ACRED09", PU_CACHE));
			else
				V_DrawSmallScaledPatch(240, 8, 0, W_CachePatchName("CREDIT05", PU_CACHE));
			break;
		case 11:
		case 12:
			if (ultimatemode)
				V_DrawSmallScaledPatch((BASEVIDWIDTH/2) - 46, 102, 0, W_CachePatchName("ACRED11", PU_CACHE));
			else
				V_DrawSmallScaledPatch(120, 120, 0, W_CachePatchName("CREDIT06", PU_CACHE));
			break;
		case 13:
		case 14:
			if (ultimatemode)
			{
				V_DrawSmallScaledPatch(174, 84, 0, W_CachePatchName("ACRED08", PU_CACHE));
				V_DrawSmallScaledPatch(2, 64, 0, W_CachePatchName("ACRED10", PU_CACHE));
			}
			else
				V_DrawSmallScaledPatch(8, 100, 0, W_CachePatchName("CREDIT07", PU_CACHE));
			break;
		case 15:
		case 16:
			if (ultimatemode)
			{
				V_DrawSmallScaledPatch(174, 84, 0, W_CachePatchName("ACRED08", PU_CACHE));
				V_DrawSmallScaledPatch(2, 52, 0, W_CachePatchName("ACRED10", PU_CACHE));
			}
			else
				V_DrawSmallScaledPatch(8, 0, 0, W_CachePatchName("CREDIT08", PU_CACHE));
			break;
		case 17:
		case 18:
			V_DrawSmallScaledPatch(112, 104, 0, W_CachePatchName("CREDIT09", PU_CACHE));
			break;
	}

	if (creditpassed->numnames == 1) // Shift it up a bit
		height -= 16;

	V_DrawCreditString((BASEVIDWIDTH - V_CreditStringWidth(creditpassed->header))/2, height, 0,
		creditpassed->header);

	if (creditpassed->smallnames)
		height += 32;

	y = 0;

	for (i = 0; i < creditpassed->numnames; i++)
	{
		if (creditpassed->smallnames)
		{
			int x;

			if (cv_realnames.value)
			{
				if (i < creditpassed->numnames/2)
					x = (BASEVIDWIDTH/4) - (V_StringWidth(creditpassed->realnames[i])/2);
				else
					x = ((BASEVIDWIDTH/4)*3) - (V_StringWidth(creditpassed->realnames[i])/2);

				V_DrawString(x,
					height + y, 0, creditpassed->realnames[i]);
			}
			else
			{
				if (i < creditpassed->numnames/2)
					x = (BASEVIDWIDTH/4) - (V_StringWidth(creditpassed->fakenames[i])/2);
				else
					x = ((BASEVIDWIDTH/4)*3) - (V_StringWidth(creditpassed->fakenames[i])/2);

				V_DrawString(x,
					height + y, 0, creditpassed->fakenames[i]);
			}
			y += 16;

			if (i + 1 == creditpassed->numnames/2)
				y = 0;
		}
		else
		{
			if (cv_realnames.value)
				V_DrawCreditString((BASEVIDWIDTH - V_CreditStringWidth(creditpassed->realnames[i]))/2,
					height + (1+i)*24, 0, creditpassed->realnames[i]);
			else
				V_DrawCreditString((BASEVIDWIDTH - V_CreditStringWidth(creditpassed->fakenames[i]))/2,
					height + (1+i)*24, 0, creditpassed->fakenames[i]);
		}
	}
}

void F_CreditDrawer(void)
{
	V_DrawFill(0, 0, vid.width, vid.height, 31);

	if (timetonext-- <= 0) // Fade to the next!
	{
		if (modcredits)
			timetonext = 165*NEWTICRATERATIO;
		else
			timetonext = 5*TICRATE-1;

		F_DrawCreditScreen(&credits[animtimer]);

		animtimer++;

		if (rendermode != render_none)
		{
			F_WipeStartScreen();
			V_DrawFill(0, 0, vid.width, vid.height, 31);
			F_DrawCreditScreen(&credits[animtimer]);
			F_WipeEndScreen(0, 0, vid.width, vid.height);

			F_RunWipe(TICRATE);
		}
	}
	else
		F_DrawCreditScreen(&credits[animtimer]);
}

//
// F_IntroDrawer
//
void F_IntroDrawer(void)
{
	if (timetonext <= 0)
	{
		if (finaletext == text[INTRO01TEXT])
		{
			S_ChangeMusic(mus_read_m, false);
			finaletext = text[INTRO02TEXT];
		}
		else if (finaletext == text[INTRO02TEXT])
			finaletext = text[INTRO03TEXT];
		else if (finaletext == text[INTRO03TEXT])
			finaletext = text[INTRO04TEXT];
		else if (finaletext == text[INTRO04TEXT])
		{
			finaletext = text[INTRO05TEXT];
			roidtics = BASEVIDWIDTH - 64;
		}
		else if (finaletext == text[INTRO05TEXT])
			finaletext = text[INTRO06TEXT];
		else if (finaletext == text[INTRO06TEXT])
			finaletext = text[INTRO07TEXT];
		else if (finaletext == text[INTRO07TEXT])
			finaletext = text[INTRO08TEXT];
		else if (finaletext == text[INTRO08TEXT])
			finaletext = text[INTRO09TEXT];
		else if (finaletext == text[INTRO09TEXT])
			finaletext = text[INTRO10TEXT];
		else if (finaletext == text[INTRO10TEXT])
			finaletext = text[INTRO11TEXT];
		else if (finaletext == text[INTRO11TEXT])
		{
			if (rendermode != render_none)
			{
				F_WipeStartScreen();
				//V_DrawFill(0, 0, vid.width, vid.height, 0);
				F_WipeEndScreen(0, 0, vid.width, vid.height);

				F_RunWipe(TICRATE);
			}

			finaletext = text[INTRO12TEXT];
		}
		else if (finaletext == text[INTRO12TEXT])
			finaletext = text[INTRO13TEXT];
		else if (finaletext == text[INTRO13TEXT])
			finaletext = text[INTRO14TEXT];
		else if (finaletext == text[INTRO14TEXT])
			finaletext = text[INTRO15TEXT];
		else if (finaletext == text[INTRO15TEXT])
			finaletext = text[INTRO16TEXT];
		else if (finaletext == text[INTRO16TEXT])
		{
			if (rendermode != render_none)
			{
				F_WipeStartScreen();
				V_DrawFill(0, 0, vid.width, vid.height, 31);
				F_WipeEndScreen(0, 0, vid.width, vid.height);

				F_RunWipe(TICRATE);
			}

			// Stay on black for a bit. =)
			{
				tic_t quittime;
				quittime = I_GetTime() + TICRATE*2; // Shortened the quit time, used to be 2 seconds
				while (quittime > I_GetTime())
				{
					I_OsPolling();
					I_FinishUpdate();
				}
			}

			D_StartTitle();
			return;
		}

		if (gamestate == GS_INTRO)
			G_SetGamestate(GS_INTRO2);
		else
			G_SetGamestate(GS_INTRO);

		if (rendermode != render_none)
			F_WipeStartScreen();

		wipegamestate = -1;
		finaletextcount = 0;
		animtimer = 0;
		stoptimer = 0;
		scene++;
		timetonext = introscenetime[scene];
	}

	if (finaletext == text[INTRO08TEXT] && finaletextcount == 5*TICRATE) // Force a wipe here
	{
		if (rendermode != render_none)
		{
			F_WipeStartScreen();
			V_DrawFill(0, 0, vid.width, vid.height, 31);
			V_DrawScaledPatch(0, 0, 0, W_CachePatchName("SGRASS5", PU_CACHE));
			F_IntroTextWrite();
			F_WipeEndScreen(0, 0, vid.width, vid.height);

			F_RunWipe(TICRATE);
		}
	}

	F_IntroTextWrite();
}

static void F_AdvanceToNextScene(void)
{
	scenenum++;

	if (scenenum < cutscenes[cutnum].numscenes)
	{
		picnum = 0;
		picxpos = cutscenes[cutnum].scene[scenenum].xcoord[picnum];
		picypos = cutscenes[cutnum].scene[scenenum].ycoord[picnum];
	}

	if (cutscenes[cutnum].scene[scenenum].musicslot != 0)
		S_ChangeMusic(cutscenes[cutnum].scene[scenenum].musicslot, cutscenes[cutnum].scene[scenenum].musicloop);

	if (rendermode != render_none)
	{
		F_WipeStartScreen();
		V_DrawFill(0,0, vid.width, vid.height, 31);
		if (scenenum < cutscenes[cutnum].numscenes)
		{
			if (cutscenes[cutnum].scene[scenenum].pichires[picnum])
				V_DrawSmallScaledPatch(picxpos, picypos, 0, W_CachePatchName(cutscenes[cutnum].scene[scenenum].picname[picnum], PU_CACHE));
			else
				V_DrawScaledPatch(picxpos, picypos, 0, W_CachePatchName(cutscenes[cutnum].scene[scenenum].picname[picnum], PU_CACHE));
		}
		F_WipeEndScreen(0, 0, vid.width, vid.height);

		F_RunWipe(TICRATE);
	}

	finaletextcount = 0;
	timetonext = 0;
	stoptimer = 0;

	if (scenenum >= cutscenes[cutnum].numscenes)
	{
		if (cutnum == creditscutscene-1)
			F_StartGameEvaluation();
		else
			F_EndCutScene();
		return;
	}

	finaletext = cutscenes[cutnum].scene[scenenum].text;

	picnum = 0;
	picxpos = cutscenes[cutnum].scene[scenenum].xcoord[picnum];
	picypos = cutscenes[cutnum].scene[scenenum].ycoord[picnum];
	textxpos = cutscenes[cutnum].scene[scenenum].textxpos;
	textypos = cutscenes[cutnum].scene[scenenum].textypos;

	animtimer = pictime = cutscenes[cutnum].scene[scenenum].picduration[picnum];
}

static void F_CutsceneTextWrite(void)
{
	V_DrawFill(0, 0, vid.width, vid.height, 31);

	if (cutscenes[cutnum].scene[scenenum].picname[picnum][0] != '\0')
	{
		if (cutscenes[cutnum].scene[scenenum].pichires[picnum])
			V_DrawSmallScaledPatch(picxpos, picypos, 0,
				W_CachePatchName(cutscenes[cutnum].scene[scenenum].picname[picnum], PU_CACHE));
		else
			V_DrawScaledPatch(picxpos,picypos, 0,
				W_CachePatchName(cutscenes[cutnum].scene[scenenum].picname[picnum], PU_CACHE));
	}

	if (animtimer)
	{
		animtimer--;
		if (animtimer <= 0)
		{
			if (picnum < 7
				&& cutscenes[cutnum].scene[scenenum].picname[picnum+1][0] != '\0')
			{
				picnum++;
				picxpos = cutscenes[cutnum].scene[scenenum].xcoord[picnum];
				picypos = cutscenes[cutnum].scene[scenenum].ycoord[picnum];
				pictime = cutscenes[cutnum].scene[scenenum].picduration[picnum];
				animtimer = pictime;
			}
			else
				timetonext = 2;
		}
	}

	F_WriteCutsceneText();
}

void F_EndCutScene(void)
{
	if (runningprecutscene && server)
	{
		D_MapChange(gamemap, gametype, ultimatemode, precutresetplayer, 0, true, false);
	}
	else
	{
		if (cutnum == introtoplay-1)
		{
			D_StartTitle();
			return;
		}

		if (nextmap < 1100-1)
			G_NextLevel();
		else
			Y_EndGame();
	}
}

//
// F_CutsceneDrawer
//
void F_CutsceneDrawer(void)
{
	if (timetonext)
		timetonext--;

	stoptimer++;

	if (timetonext == 1 && stoptimer > 0)
		F_AdvanceToNextScene();

	F_CutsceneTextWrite();
}
