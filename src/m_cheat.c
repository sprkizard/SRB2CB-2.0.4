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
/// \brief Cheat sequence checking

#include "doomdef.h"
#include "dstrings.h"

#include "g_game.h"
#include "s_sound.h"

#include "p_local.h"

#include "m_cheat.h"
#include "m_menu.h"
#include "m_random.h"

#include "hu_stuff.h"

//
// CHEAT SEQUENCE PACKAGE
//

#define SCRAMBLE(a) \
((((a)&1)<<7) + (((a)&2)<<5) + ((a)&4) + (((a)&8)<<1) \
 + (((a)&16)>>1) + ((a)&32) + (((a)&64)>>5) + (((a)&128)>>7))

typedef struct
{
	byte *sequence;
	byte *p;
} cheatseq_t;

// ==========================================================================
//                             CHEAT Structures
// ==========================================================================

// SRB2CBTODO: Make cool use of special cheats that are activated by sequence like this!
static byte cheat_bulmer_seq[] =
{
	SCRAMBLE('b'), SCRAMBLE('e'), SCRAMBLE('e'), SCRAMBLE('d'), SCRAMBLE('e'), SCRAMBLE('e'), 0xff
};

static byte cheat_poksoc_seq[] =
{
	SCRAMBLE('p'), SCRAMBLE('o'), SCRAMBLE('k'), SCRAMBLE('s'), SCRAMBLE('o'), SCRAMBLE('c'), 0xff
};

static byte cheat_apl_seq[] =
{
	SCRAMBLE('a'), SCRAMBLE('p'), SCRAMBLE('l'), 0xff
};

static byte cheat_ultimate_seq[] =
{
	SCRAMBLE('u'), SCRAMBLE('l'), SCRAMBLE('t'), SCRAMBLE('i'), SCRAMBLE('m'), SCRAMBLE('a'), SCRAMBLE('t'), SCRAMBLE('e'), 0xff
};

// Now what?
static cheatseq_t cheat_bulmer   = { cheat_bulmer_seq,   0 };
static cheatseq_t cheat_poksoc   = { cheat_poksoc_seq,   0 };
static cheatseq_t cheat_apl      = { cheat_apl_seq,      0 };
static cheatseq_t cheat_ultimate = { cheat_ultimate_seq, 0 };

// ==========================================================================
//                        CHEAT SEQUENCE PACKAGE
// ==========================================================================

static byte cheat_xlate_table[256];

void cht_Init(void)
{
	size_t i = 0;
	short pi = 0;
	for (; i < 256; i++, pi++)
	{
		const int cc = SCRAMBLE(pi);
		cheat_xlate_table[i] = (byte)cc;
	}
}

//
// Called in st_stuff module, which handles the input.
// Returns a 1 if the cheat was successful, 0 if failed.
//
static int cht_CheckCheat(cheatseq_t *cht, char key)
{
	int rc = 0;

	if (!cht->p)
		cht->p = cht->sequence; // initialize if first time

	if (*cht->p == 0)
		*(cht->p++) = key;
	else if (cheat_xlate_table[(byte)key] == *cht->p)
		cht->p++;
	else
		cht->p = cht->sequence;

	if (*cht->p == 1)
		cht->p++;
	else if (*cht->p == 0xff) // end of sequence character
	{
		cht->p = cht->sequence;
		rc = 1;
	}

	return rc;
}

static inline void cht_GetParam(cheatseq_t *cht, char *buffer)
{
	byte *p;
	byte c;

	p = cht->sequence;
	while (*(p++) != 1)
		;

	do
	{
		c = *p;
		*(buffer++) = c;
		*(p++) = 0;
	} while (c && *p != 0xff);

	if (*p == 0xff)
		*buffer = 0;
}

boolean cht_Responder(event_t *ev)
{
	static player_t *plyr;

	if (ev->type == ev_keydown && 0x00 <= ev->data1 && ev->data1 <= 0xFF) // SRB2CBTODO: && 0x00 <= ev->data1 && ev->data1 <= 0xFF?
	{
		plyr = &players[consoleplayer];

		// devmode cheat
		if (cht_CheckCheat(&cheat_bulmer, (char)ev->data1))
		{
			sfxenum_t sfxid;
			const char *emoticon;
			byte mrandom;

			/*
			Shows a picture of David Bulmer with one the following messages:
			"*B^C", "*B^D", "*B^I", "*B^J", "*B^L", "*B^O", "*B^P", "*B^S", "*B^X"
			Accompany each emoticon with sound clip.
			*/

			M_StartControlPanel();
			M_SetupNextMenu(&ReadDef2);

			mrandom = M_Random();

			if (mrandom < 64)
			{
				emoticon = "*B^O";
				sfxid = sfx_beeoh;
			}
			else if (mrandom < 128)
			{
				emoticon = "*B^L";
				sfxid = sfx_beeel;
			}
			else if (mrandom < 192)
			{
				emoticon = "*B^J";
				sfxid = sfx_beejay;
			}
			else
			{
				emoticon = "*B^D";
				sfxid = sfx_beedee;
			}

			HU_DoCEcho(emoticon);
			COM_BufExecute();
			S_StartSound(0, sfxid);
		}
		else if (cht_CheckCheat(&cheat_poksoc, (char)ev->data1))
		{
			sfxenum_t sfxid;
			byte mrandom = M_Random();

			/*
			Plays one of these sounds:
			"You cheating, lying GIT!"
			"Hey... are you my Grandma?"
			"PIIIKKAAA!"
			"You little bugger!"
			"Oxy-pad, Oxy-pad, Oxy-pad, Ox--eeaygggh!"
			"(Eggman's Japanese) That's not fair, now two of your players have to die!"
			*/

			if (mrandom < 48)
				sfxid = sfx_poksoc1;
			else if (mrandom < 96)
				sfxid = sfx_poksoc2;
			else if (mrandom < 144)
				sfxid = sfx_poksoc3;
			else if (mrandom < 192)
				sfxid = sfx_poksoc4;
			else if (mrandom < 240)
				sfxid = sfx_poksoc5;
			else
				sfxid = sfx_poksoc6;

			S_StartSound(0, sfxid);
		}
		else if (cht_CheckCheat(&cheat_apl, (char)ev->data1))
		{
			sfxenum_t sfxid;
			byte mrandom = M_Random();

			/*
			Plays one of these sounds:
			"You do realize those are prohibited on planes, right?"
			"IT'S A HUNKY DUNKY SUPER SIZE BIG FAT REALLY REALLY BIG BOMB!"
			"Let's order a pizza!"
			"Tails, you made the engines quit!
			"Buggery! What happened out here?!"
			"Oh no! A GigaDoomBot!"
			*/

			if (mrandom < 48)
				sfxid = sfx_apl1;
			else if (mrandom < 96)
				sfxid = sfx_apl2;
			else if (mrandom < 144)
				sfxid = sfx_apl3;
			else if (mrandom < 192)
				sfxid = sfx_apl4;
			else if (mrandom < 240)
				sfxid = sfx_apl5;
			else
				sfxid = sfx_apl6;

			S_StartSound(0, sfxid);
		}
		else if (cht_CheckCheat(&cheat_ultimate, (char)ev->data1)) // SRB2CBTODO: USE THIS FOR OTHER COOL STUFF!
		{
			// Plays the ring sound effect, then begins a single player game at ultimate difficulty.
			S_StartSound(0, sfx_itemup);
			ultmode = true;
			startmap = spstage_start;
			M_SetupNextMenu(&PlayerDef);
		}
	}
	return false;
}

// Commands that can be typed at the console

void Command_CheatNoClip_f(void)
{
	player_t *plyr;
	if (multiplayer)
		return;

	plyr = &players[consoleplayer];
	plyr->pflags ^= PF_NOCLIP;
	CONS_Printf("No Clipping %s\n", plyr->pflags & PF_NOCLIP ? "On" : "Off");

	G_ModifyGame();
}

void Command_CheatGhost_f(void)
{
	player_t *plyr;

	if (multiplayer)
		return;

	plyr = &players[consoleplayer];
	plyr->pflags ^= PF_GHOSTMODE;
	CONS_Printf("Weenie Mode %s\n", plyr->pflags & PF_GHOSTMODE ? "On" : "Off");

	G_ModifyGame();
}

void Command_Scale_f(void)
{
	int scale = atoi(COM_Argv(1));

	G_ModifyGame();

	if (netgame
#ifndef _DEBUG
		|| multiplayer
#endif
		)
	{
		CONS_Printf("%s", text[SINGLEPLAYERONLY]);
		return;
	}
	
	if (!scale)
	{
		CONS_Printf("Current scale: %d%%, Valid scale values are 25%% to 500%%\n", players[consoleplayer].mo->scale);
		return;
	}
	else if (!(scale >= 25 && scale <= 500))
	{
		CONS_Printf("Valid scale values are 25%% to 500%%\n");
		if (scale < 25)
			scale = 25;
		else if (scale > 500)
			scale = 500;
	}

	if (!players[consoleplayer].mo)
	{
		CONS_Printf("%s",text[MUSTBEINLEVEL]);
		return;
	}

	players[consoleplayer].mo->destscale = (USHORT)scale;

	CONS_Printf("Scale set to %d%%\n", players[consoleplayer].mo->destscale);
}

void Command_Sheild_f(void)
{
	int value = atoi(COM_Argv(1));
	
	G_ModifyGame();
	
	if (netgame
#ifndef _DEBUG
		|| multiplayer
#endif
		)
	{
		CONS_Printf("%s", text[SINGLEPLAYERONLY]);
		return;
	}
	
	if (!value)
	{
		CONS_Printf("Sheilds:\n 1 JumpShield,  2 Ringshield,  3 BombShield,\n 4 WaterShield, 5 ForceShield, 6 FlameShield,\n 7 LightningShield, 8 BubbleShield\n");
		return;
	}
	else
	{
		if (!players[consoleplayer].mo)
		{
			CONS_Printf("%s",text[MUSTBEINLEVEL]);
			return;
		}
		
		player_t *player = &players[consoleplayer];
		
		if (value == 1)
		{
			player->powers[pw_forceshield] = player->powers[pw_bombshield] = false;
			player->powers[pw_watershield] = player->powers[pw_ringshield] = false;
#ifdef SRB2K
			player->powers[pw_bubbleshield] = player->powers[pw_lightningshield] = player->powers[pw_flameshield] = false;
#endif
			
			players[consoleplayer].powers[pw_jumpshield] = true; 
			P_SpawnShieldOrb(&players[consoleplayer]);
			if (players[consoleplayer].mo->tracer)
				A_JumpShield(players[consoleplayer].mo->tracer);
			
			CONS_Printf("Jump shield activated\n");
		}
		else if (value == 2)
		{
			player->powers[pw_bombshield] = player->powers[pw_watershield] = false;
			player->powers[pw_forceshield] = player->powers[pw_jumpshield] = false;
#ifdef SRB2K
			player->powers[pw_bubbleshield] = player->powers[pw_lightningshield] = player->powers[pw_flameshield] = false;
#endif
			players[consoleplayer].powers[pw_ringshield] = true;
			P_SpawnShieldOrb(&players[consoleplayer]);
			if (players[consoleplayer].mo->tracer)
				A_RingShield(players[consoleplayer].mo->tracer);
			
			CONS_Printf("Ring shield activated\n");
		}
		else if (value == 3)
		{
			player->powers[pw_watershield] = player->powers[pw_forceshield] = false;
			player->powers[pw_ringshield] = player->powers[pw_jumpshield] = false;
#ifdef SRB2K
			player->powers[pw_bubbleshield] = player->powers[pw_lightningshield] = player->powers[pw_flameshield] = false;
#endif
			players[consoleplayer].powers[pw_bombshield] = true;
			P_SpawnShieldOrb(&players[consoleplayer]);
			if (players[consoleplayer].mo->tracer)
				A_BombShield(players[consoleplayer].mo->tracer);
			
			CONS_Printf("Bomb shield activated\n");
		}
		else if (value == 4)
		{
			player->powers[pw_bombshield] = player->powers[pw_forceshield] = false;
			player->powers[pw_ringshield] = player->powers[pw_jumpshield] = false;
#ifdef SRB2K
			player->powers[pw_bubbleshield] = player->powers[pw_lightningshield] = player->powers[pw_flameshield] = false;
#endif
			players[consoleplayer].powers[pw_watershield] = 1;
			P_SpawnShieldOrb(&players[consoleplayer]);
			if (players[consoleplayer].mo->tracer)
				A_WaterShield(players[consoleplayer].mo->tracer);
			
			CONS_Printf("Water shield activated\n");
		}
		else if (value == 5)
		{
			player->powers[pw_bombshield] = player->powers[pw_watershield] = false;
			player->powers[pw_ringshield] = player->powers[pw_jumpshield] = false;
#ifdef SRB2K
			player->powers[pw_bubbleshield] = player->powers[pw_lightningshield] = player->powers[pw_flameshield] = false;
#endif
			players[consoleplayer].powers[pw_forceshield] = 2;
			P_SpawnShieldOrb(&players[consoleplayer]);
			if (players[consoleplayer].mo->tracer)
				A_ForceShield(players[consoleplayer].mo->tracer);
			
			CONS_Printf("Force shield activated\n");
		}
		else if (value == 6)
		{
			player->powers[pw_bombshield] = player->powers[pw_watershield] = false;
			player->powers[pw_ringshield] = player->powers[pw_jumpshield] = false;
			player->powers[pw_bubbleshield] = player->powers[pw_lightningshield] = false;
			player->powers[pw_forceshield] = false;
			
			players[consoleplayer].powers[pw_flameshield] = true;
			P_SpawnShieldOrb(&players[consoleplayer]);
			if (players[consoleplayer].mo->tracer)
				A_FlameShield(players[consoleplayer].mo->tracer);
			
			CONS_Printf("Flame shield activated\n");
		}
		else if (value == 7)
		{
			player->powers[pw_bombshield] = player->powers[pw_watershield] = false;
			player->powers[pw_ringshield] = player->powers[pw_jumpshield] = false;
			player->powers[pw_bubbleshield] = player->powers[pw_flameshield] = false;
			player->powers[pw_forceshield] = false;
			players[consoleplayer].powers[pw_lightningshield] = true;
			P_SpawnShieldOrb(&players[consoleplayer]);
			if (players[consoleplayer].mo->tracer)
				A_LightningShield(players[consoleplayer].mo->tracer);
			
			CONS_Printf("Lightning shield activated\n");
		}
		else if (value == 8)
		{
			player->powers[pw_bombshield] = player->powers[pw_watershield] = false;
			player->powers[pw_ringshield] = player->powers[pw_jumpshield] = false;
			player->powers[pw_flameshield] = player->powers[pw_lightningshield] = false;
			player->powers[pw_forceshield] = false;
			players[consoleplayer].powers[pw_bubbleshield] = true;
			P_SpawnShieldOrb(&players[consoleplayer]);
			if (players[consoleplayer].mo->tracer)
				A_BubbleShield(players[consoleplayer].mo->tracer);
			
			CONS_Printf("Bubble shield activated\n");
		}
		else
			CONS_Printf("Not a valid shield, try 1 - 8\n");

	}
}

void Command_Gravflip_f(void)
{
	if (gamestate != GS_LEVEL || demoplayback)
	{
		CONS_Printf("%s", text[MUSTBEINLEVEL]);
		return;
	}
	
	G_ModifyGame();

	if ((netgame || multiplayer) && !splitscreen) // Let the splitscreen players have fun :P
	{
		CONS_Printf("%s", text[SINGLEPLAYERONLY]);
		return;
	}

	if (players[consoleplayer].powers[pw_gravityboots])
		players[consoleplayer].powers[pw_gravityboots] = 0;
	else
		players[consoleplayer].powers[pw_gravityboots] += 3600 * TICRATE;
}

void Command_Hurt_f(void)
{
	if (gamestate != GS_LEVEL || demoplayback)
	{
		CONS_Printf("%s", text[MUSTBEINLEVEL]);
		return;
	}

	G_ModifyGame();

	if (netgame || multiplayer)
	{
		CONS_Printf("%s", text[CANTUSEMULTIPLAYER]);
		return;
	}

	if (COM_Argc() < 2)
	{
		CONS_Printf("hurt <damage>\n");
		return;
	}

	P_DamageMobj(players[consoleplayer].mo, NULL, NULL, atoi(COM_Argv(1)));
}

void Command_Charability_f(void)
{
	if (gamestate != GS_LEVEL || demoplayback)
	{
		CONS_Printf("%s", text[MUSTBEINLEVEL]);
		return;
	}

	G_ModifyGame();

	if (COM_Argc() < 3)
	{
		CONS_Printf("charability <1/2> <value>\n");
		return;
	}

	if (netgame || multiplayer)
	{
		CONS_Printf("%s", text[CANTUSEMULTIPLAYER]);
		return;
	}

	if (atoi(COM_Argv(1)) == 1)
		players[consoleplayer].charability = atoi(COM_Argv(2));
	else if (atoi(COM_Argv(1)) == 2)
		players[consoleplayer].charability2 = atoi(COM_Argv(2));
	else
		CONS_Printf("charability <1/2> <value>\n");
}

void Command_Savecheckpoint_f(void)
{
	G_ModifyGame();

	if (netgame || multiplayer)
	{
		CONS_Printf("%s", text[SINGLEPLAYERONLY]);
		return;
	}

	players[consoleplayer].starposttime = players[consoleplayer].realtime;
	players[consoleplayer].starpostx = players[consoleplayer].mo->x>>FRACBITS;
	players[consoleplayer].starposty = players[consoleplayer].mo->y>>FRACBITS;
	players[consoleplayer].starpostz = players[consoleplayer].mo->floorz>>FRACBITS;
	players[consoleplayer].starpostangle = players[consoleplayer].mo->angle;

	CONS_Printf("Temporary checkpoint created at %ld, %ld, %ld\n", players[consoleplayer].starpostx, players[consoleplayer].starposty, players[consoleplayer].starpostz);
}

void Command_Resetemeralds_f(void)
{
	if (netgame || multiplayer)
	{
		CONS_Printf("%s", text[SINGLEPLAYERONLY]);
		return;
	}

	emeralds = 0;

	CONS_Printf("Emeralds reset to zero.\n");
}

void Command_Kill_f(void)
{
	if (gamestate != GS_LEVEL || demoplayback)
	{
		CONS_Printf("%s", text[MUSTBEINLEVEL]);
		return;
	}
	
	G_ModifyGame();
	
	if (netgame || multiplayer)
	{
		CONS_Printf("%s", text[CANTUSEMULTIPLAYER]);
		return;
	}
	
	P_KillMobj(players[consoleplayer].mo, NULL, NULL);
}

void Command_Devmode_f(void)
{
	if (netgame
#ifndef _DEBUG
		|| multiplayer
#endif
		)
		return;

	if (COM_Argc() > 1)
		cv_devmode = atoi(COM_Argv(1));
	else if (!cv_devmode)
		cv_devmode = 1;
	else
		cv_devmode = 0;

#ifndef _DEBUG // Only modify the game in release mode
	G_ModifyGame();
#endif
}
