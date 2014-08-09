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
/// \brief New stuff?
///
///	Player related stuff.
///	Bobbing POV/weapon, movement.
///	Pending weapon.

#include "doomdef.h"
#include "i_system.h"
#include "d_event.h"
#include "d_net.h"
#include "g_game.h"
#include "p_local.h"
#include "r_main.h"
#include "s_sound.h"
#include "r_things.h"
#include "d_think.h"
#include "r_sky.h"
#include "p_setup.h"
#include "m_random.h"
#include "m_misc.h"
#include "i_video.h"
#include "p_spec.h"
#include "r_splats.h"
#include "z_zone.h"
#include "w_wad.h"
#include "dstrings.h"
#include "hu_stuff.h"
#include "v_video.h"
#include "p_slopes.h"

#ifdef HW3SOUND
#include "hardware/hw3sound.h"
#endif

#ifdef HWRENDER
#include "hardware/hw_light.h"
#include "hardware/hw_main.h"
#include "hardware/hw_md2.h"
#endif

// JTEBOTS
#ifdef JTEBOTS
#include "p_bots.h"
#include "i_net.h"
#include "doomstat.h"
#endif

//
// Movement.
//

static boolean onground;

// SRB2CBTODO: Make this more general use for stopping movement
boolean P_FreezeObjectplace(void)
{
	if (!cv_objectplace.value)
		return false;

	if ((maptol & TOL_NIGHTS) && (players[consoleplayer].pflags & PF_NIGHTSMODE))
		return false;

	return true;
}

//
// P_CalcHeight
// Calculate the walking / running height adjustment
//
static void P_CalcHeight(player_t *player)
{
	int angle;
	fixed_t bob;
	fixed_t pviewheight;
	mobj_t *mo = player->mo;
	static fixed_t MAXBOB = (0x10 << FRACBITS); // 16 pixels of bob

	// Regular movement bobbing.
	// Should not be calculated when not on ground (FIXTHIS?)
	// OPTIMIZE: tablify angle
	// Note: a LUT allows for effects
	//  like a ramp with low health.

	player->bob = ((FixedMul(player->rmomx,player->rmomx)
		+ FixedMul(player->rmomy,player->rmomy))*NEWTICRATERATIO)>>2;

	if (player->bob > MAXBOB)
		player->bob = MAXBOB;

	if (!P_IsObjectOnGround(mo))
	{
		if (mo->eflags & MFE_VERTICALFLIP)
			player->viewz = mo->z + mo->height - player->viewheight;
		else
			player->viewz = mo->z + player->viewheight;

		if (player->viewz > mo->ceilingz - FRACUNIT)
			player->viewz = mo->ceilingz - FRACUNIT;
		return;
	}

	angle = (FINEANGLES/20*localgametic/NEWTICRATERATIO) & FINEMASK;
	bob = FixedMul(player->bob/2, FINESINE(angle));

	// move viewheight
	pviewheight = FIXEDSCALE(cv_viewheight.value << FRACBITS, mo->scale); // default eye view height

	if (player->playerstate == PST_LIVE)
	{
		player->viewheight += player->deltaviewheight;

		if (player->viewheight > pviewheight)
		{
			player->viewheight = pviewheight;
			player->deltaviewheight = 0;
		}

		if (player->viewheight < pviewheight/2)
		{
			player->viewheight = pviewheight/2;
			if (player->deltaviewheight <= 0)
				player->deltaviewheight = 1;
		}

		if (player->deltaviewheight)
		{
			player->deltaviewheight += FRACUNIT/4;
			if (!player->deltaviewheight)
				player->deltaviewheight = 1;
		}
	}

	if (player->mo->eflags & MFE_VERTICALFLIP)
		player->viewz = mo->z + mo->height - player->viewheight - bob;
	else
		player->viewz = mo->z + player->viewheight + bob;

	if (player->viewz > mo->ceilingz-4*FRACUNIT)
		player->viewz = mo->ceilingz-4*FRACUNIT;
	if (player->viewz < mo->floorz+4*FRACUNIT)
		player->viewz = mo->floorz+4*FRACUNIT;
}

static fixed_t P_GridSnap(fixed_t value) // For ObjectPlace
{
	fixed_t pos = value/cv_grid.value;
	const fixed_t poss = (pos/FRACBITS)<<FRACBITS;
	pos = (pos & FRACMASK) < FRACUNIT/2 ? poss : poss+FRACUNIT;
	return pos * cv_grid.value;
}

/** Decides if a player is moving.
  * \param pnum The player number to test.
  * \return True if the player is considered to be moving.
  * \author Graue <graue@oceanbase.org>
  */
boolean P_PlayerMoving(int pnum)
{
	player_t *p = &players[pnum];

	if (p->jointime < 5*TICRATE)
		return false;

	return gamestate == GS_LEVEL && p->mo && p->mo->health > 0
		&& (
			p->rmomx >= FRACUNIT/2 ||
			p->rmomx <= -FRACUNIT/2 ||
			p->rmomy >= FRACUNIT/2 ||
			p->rmomy <= -FRACUNIT/2 ||
			p->mo->momz >= FRACUNIT/2 ||
			p->mo->momz <= -FRACUNIT/2 ||
			p->climbing ||
			p->powers[pw_tailsfly] ||
			(p->pflags & PF_JUMPED) ||
			(p->pflags & PF_SPINNING));
}

//
// P_GiveEmerald
//
// Award an emerald upon completion
// of a special stage.
//
void P_GiveEmerald(void)
{
	int i;

	S_StartSound(NULL, sfx_cgot); // Got the emerald!

	// Check what emeralds the player has so you know which one to award next.
	if (!(emeralds & EMERALD1))
	{
		emeralds |= EMERALD1;
		for (i = 0; i < MAXPLAYERS; i++)
			if (playeringame[i])
				P_SetMobjState(P_SpawnMobj(players[i].mo->x, players[i].mo->y, players[i].mo->z + players[i].mo->info->height, MT_GOTEMERALD), mobjinfo[MT_GOTEMERALD].spawnstate);
	}
	else if ((emeralds & EMERALD1) && !(emeralds & EMERALD2))
	{
		emeralds |= EMERALD2;
		for (i = 0; i < MAXPLAYERS; i++)
			if (playeringame[i])
				P_SetMobjState(P_SpawnMobj(players[i].mo->x, players[i].mo->y, players[i].mo->z + players[i].mo->info->height, MT_GOTEMERALD), mobjinfo[MT_GOTEMERALD].spawnstate+1);
	}
	else if ((emeralds & EMERALD2) && !(emeralds & EMERALD3))
	{
		emeralds |= EMERALD3;
		for (i = 0; i < MAXPLAYERS; i++)
			if (playeringame[i])
				P_SetMobjState(P_SpawnMobj(players[i].mo->x, players[i].mo->y, players[i].mo->z + players[i].mo->info->height, MT_GOTEMERALD), mobjinfo[MT_GOTEMERALD].spawnstate+2);
	}
	else if ((emeralds & EMERALD3) && !(emeralds & EMERALD4))
	{
		emeralds |= EMERALD4;
		for (i = 0; i < MAXPLAYERS; i++)
			if (playeringame[i])
				P_SetMobjState(P_SpawnMobj(players[i].mo->x, players[i].mo->y, players[i].mo->z + players[i].mo->info->height, MT_GOTEMERALD), mobjinfo[MT_GOTEMERALD].spawnstate+3);
	}
	else if ((emeralds & EMERALD4) && !(emeralds & EMERALD5))
	{
		emeralds |= EMERALD5;
		for (i = 0; i < MAXPLAYERS; i++)
			if (playeringame[i])
				P_SetMobjState(P_SpawnMobj(players[i].mo->x, players[i].mo->y, players[i].mo->z + players[i].mo->info->height, MT_GOTEMERALD), mobjinfo[MT_GOTEMERALD].spawnstate+4);
	}
	else if ((emeralds & EMERALD5) && !(emeralds & EMERALD6))
	{
		emeralds |= EMERALD6;
		for (i = 0; i < MAXPLAYERS; i++)
			if (playeringame[i])
				P_SetMobjState(P_SpawnMobj(players[i].mo->x, players[i].mo->y, players[i].mo->z + players[i].mo->info->height, MT_GOTEMERALD), mobjinfo[MT_GOTEMERALD].spawnstate+5);
	}
	else if ((emeralds & EMERALD6) && !(emeralds & EMERALD7))
	{
		emeralds |= EMERALD7;
		for (i = 0; i < MAXPLAYERS; i++)
			if (playeringame[i])
				P_SetMobjState(P_SpawnMobj(players[i].mo->x, players[i].mo->y, players[i].mo->z + players[i].mo->info->height, MT_GOTEMERALD), mobjinfo[MT_GOTEMERALD].spawnstate+6);
	}
}

//
// P_ResetScore
//
// This is called when your chain is reset. If in
// Chaos mode, it displays what chain you got.
void P_ResetScore(player_t *player)
{
#ifdef CHAOSISNOTDEADYET
	if (gametype == GT_CHAOS && player->scoreadd >= 5)
		CONS_Printf("%s got a chain of %lu!\n", player_names[player-players], player->scoreadd);
#endif

	player->scoreadd = 0;
	player->shielddelay = 0;
}

//
// P_FindLowestMare
//
// Returns the lowest open mare available
//
byte P_FindLowestMare(void)
{
	thinker_t *th;
	mobj_t *mo2;
	byte mare = 255;

#ifdef FREEFLY
	if (gametype == GT_RACE || (mapheaderinfo[gamemap-1].freefly))
#else
		if (gametype == GT_RACE)
#endif
			return 0;

	// scan the thinkers
	// to find the egg capsule with the lowest mare
	for (th = thinkercap.next; th != &thinkercap; th = th->next)
	{
		if (th->function.acp1 != (actionf_p1)P_MobjThinker)
			continue;

		mo2 = (mobj_t *)th;

		if (mo2->type == MT_EGGCAPSULE && mo2->health > 0)
		{
			const byte threshold = (byte)mo2->threshold;
			if (mare == 255)
				mare = threshold;
			else if (threshold < mare)
				mare = threshold;
		}
	}

	if (cv_devmode)
		CONS_Printf("Lowest mare found: %d\n", mare);

	return mare;
}

//
// P_TransferToNextMare
//
// Transfers the player to the next Mare.
// (Finds the lowest mare # for capsules that have not been destroyed).
// Returns true if successful, false if there is no other mare.
//
boolean P_TransferToNextMare(player_t *player)
{
	thinker_t *th;
	mobj_t *mo2;
	mobj_t *closestaxis = NULL;
	int lowestaxisnum = -1;
	byte mare = P_FindLowestMare();
	fixed_t dist1, dist2 = 0;

	if (mare == 255)
		return false;

	if (cv_devmode)
		CONS_Printf("Mare is %d\n", mare);

	player->mare = mare;

	// scan the thinkers
	// to find the closest axis point
	for (th = thinkercap.next; th != &thinkercap; th = th->next)
	{
		if (th->function.acp1 != (actionf_p1)P_MobjThinker)
			continue;

		mo2 = (mobj_t *)th;

		if (mo2->type == MT_AXIS)
		{
			if (mo2->threshold == mare)
			{
				if (closestaxis == NULL)
				{
					closestaxis = mo2;
					lowestaxisnum = mo2->health;
					dist2 = R_PointToDist2(player->mo->x, player->mo->y, mo2->x, mo2->y)-mo2->radius;
				}
				else if (mo2->health < lowestaxisnum)
				{
					dist1 = R_PointToDist2(player->mo->x, player->mo->y, mo2->x, mo2->y)-mo2->radius;

					if (dist1 < dist2)
					{
						closestaxis = mo2;
						lowestaxisnum = mo2->health;
						dist2 = dist1;
					}
				}
			}
		}
	}

	if (closestaxis == NULL)
		return false;

	P_SetTarget(&player->mo->target, closestaxis);
	return true;
}

//
// P_FindAxis
//
// Given a mare and axis number, returns
// the mobj for that axis point.
static mobj_t *P_FindAxis(int mare, int axisnum)
{
	thinker_t *th;
	mobj_t *mo2;

	// scan the thinkers
	// to find the closest axis point
	for (th = thinkercap.next; th != &thinkercap; th = th->next)
	{
		if (th->function.acp1 != (actionf_p1)P_MobjThinker)
			continue;

		mo2 = (mobj_t *)th;

		// Axis things are only at beginning of list.
		if (!(mo2->flags2 & MF2_AXIS))
			return NULL;

		if (mo2->type == MT_AXIS)
		{
			if (mo2->health == axisnum && mo2->threshold == mare)
				return mo2;
		}
	}

	return NULL;
}

//
// P_FindAxisTransfer
//
// Given a mare and axis number, returns
// the mobj for that axis transfer point.
static mobj_t *P_FindAxisTransfer(int mare, int axisnum, mobjtype_t type)
{
	thinker_t *th;
	mobj_t *mo2;

	// scan the thinkers
	// to find the closest axis point
	for (th = thinkercap.next; th != &thinkercap; th = th->next)
	{
		if (th->function.acp1 != (actionf_p1)P_MobjThinker)
			continue;

		mo2 = (mobj_t *)th;

		// Axis things are only at beginning of list.
		if (!(mo2->flags2 & MF2_AXIS))
			return NULL;

		if (mo2->type == type)
		{
			if (mo2->health == axisnum && mo2->threshold == mare)
				return mo2;
		}
	}

	return NULL;
}

//
// P_TransferToAxis
//
// Finds the CLOSEST axis with the number specified.
void P_TransferToAxis(player_t *player, int axisnum)
{
	thinker_t *th;
	mobj_t *mo2;
	mobj_t *closestaxis;
	int mare = player->mare;
	fixed_t dist1, dist2 = 0;

	if (cv_devmode)
		CONS_Printf("Transferring to axis %d\nLeveltime: %u...\n", axisnum,leveltime);

	closestaxis = NULL;

	// scan the thinkers
	// to find the closest axis point
	for (th = thinkercap.next; th != &thinkercap; th = th->next)
	{
		if (th->function.acp1 != (actionf_p1)P_MobjThinker)
			continue;

		mo2 = (mobj_t *)th;

		if (mo2->type == MT_AXIS)
		{
			if (mo2->health == axisnum && mo2->threshold == mare)
			{
				if (closestaxis == NULL)
				{
					closestaxis = mo2;
					dist2 = R_PointToDist2(player->mo->x, player->mo->y, mo2->x, mo2->y)-mo2->radius;
				}
				else
				{
					dist1 = R_PointToDist2(player->mo->x, player->mo->y, mo2->x, mo2->y)-mo2->radius;

					if (dist1 < dist2)
					{
						closestaxis = mo2;
						dist2 = dist1;
					}
				}
			}
		}
	}

	if (!closestaxis)
		CONS_Printf("ERROR: Specified axis point to transfer to not found!\n%d\n", axisnum);
	else if (cv_devmode)
		CONS_Printf("Transferred to axis %ld, mare %ld\n", closestaxis->health, closestaxis->threshold);

	P_SetTarget(&player->mo->target, closestaxis);
}

//
// P_DeNightserizePlayer
//
// Whoops! Ran out of NiGHTS time!
//
static void P_DeNightserizePlayer(player_t *player)
{
	thinker_t *th;
	mobj_t *mo2;

	player->pflags &= ~PF_NIGHTSMODE;

	//if (player->mo->tracer)
		//P_RemoveMobj(player->mo->tracer);

	player->powers[pw_underwater] = 0;
	player->pflags &= ~PF_USEDOWN;
	player->pflags &= ~PF_JUMPDOWN;
	player->pflags &= ~PF_ATTACKDOWN;
	player->pflags &= ~PF_WALKINGANIM;
	player->pflags &= ~PF_RUNNINGANIM;
	player->pflags &= ~PF_SPINNINGANIM;
	player->pflags &= ~PF_STARTDASH;
	player->pflags &= ~PF_GLIDING;
	player->pflags &= ~PF_JUMPED;
	player->pflags &= ~PF_THOKKED;
	player->pflags &= ~PF_SPINNING;
	player->pflags &= ~PF_DRILLING;
	player->pflags &= ~PF_TRANSFERTOCLOSEST;
	player->secondjump = false;
	player->dbginfo = false;
	player->jumping = false;
	player->homing = 0;
	player->climbing = 0;
	player->mo->fuse = 0;
	player->speed = 0;
	P_SetTarget(&player->mo->target, NULL);
	P_SetTarget(&player->axis1, P_SetTarget(&player->axis2, NULL));

	player->mo->flags &= ~MF_NOGRAVITY;

	player->mo->flags2 &= ~MF2_DONTDRAW;

	// SRB2CBTODO: NO! Do not use the cam console to set this!

	if (splitscreen && player == &players[secondarydisplayplayer])
	{
		if (cv_analog2.value)
			CV_SetValue(&cv_cam2_dist, 192);
		else
			CV_SetValue(&cv_cam2_dist, atoi(cv_cam2_dist.defaultvalue));
	}
	else if (player == &players[displayplayer])
	{
		if (cv_analog.value)
			CV_SetValue(&cv_cam_dist, 192); // SRB2CBTODO: This is stupid
		else
			CV_SetValue(&cv_cam_dist, atoi(cv_cam_dist.defaultvalue));
	}

	// Restore aiming angle
	if (player == &players[consoleplayer])
		localaiming = 0;
	else if (splitscreen && player == &players[secondarydisplayplayer])
		localaiming2 = 0;

	if (player->mo->tracer)
		P_SetMobjState(player->mo->tracer, S_DISS);
	P_SetPlayerMobjState(player->mo, S_PLAY_FALL1);
	player->pflags |= PF_NIGHTSFALL;

	// If in a special stage, add some preliminary exit time.
	if (G_IsSpecialStage(gamemap))
		player->exiting = TICRATE * 3;

	// Check to see if the player should be killed.
	for (th = thinkercap.next; th != &thinkercap; th = th->next)
	{
		if (th->function.acp1 != (actionf_p1)P_MobjThinker)
			continue;

		mo2 = (mobj_t *)th;

		if (!(mo2->type == MT_NIGHTSDRONE))
			continue;

		if (mo2->flags & MF_AMBUSH)
		{
			P_DamageMobj(player->mo, NULL, NULL, 10000);
			break;
		}
	}
}
//
// P_NightserizePlayer
//
// NiGHTS Time!
void P_NightserizePlayer(player_t *player, int nighttime)
{
	int oldmare;

	player->pflags &= ~PF_USEDOWN;
	player->pflags &= ~PF_JUMPDOWN;
	player->pflags &= ~PF_ATTACKDOWN;
#ifndef ANGLE2D
	player->pflags &= ~PF_WALKINGANIM;
	player->pflags &= ~PF_RUNNINGANIM;
	player->pflags &= ~PF_SPINNINGANIM;
#endif
	player->pflags &= ~PF_STARTDASH;
	player->pflags &= ~PF_GLIDING;
	player->pflags &= ~PF_JUMPED;
	player->pflags &= ~PF_THOKKED;
	player->pflags &= ~PF_SPINNING;
	player->pflags &= ~PF_DRILLING;
	player->homing = 0;
	player->mo->fuse = 0;
#ifndef ANGLE2D
	player->speed = 0;
#endif
	player->climbing = 0;
	player->secondjump = false;
	player->dbginfo = false;

#ifndef ANGLE2D
	player->powers[pw_jumpshield] = 0;
	player->powers[pw_forceshield] = 0;
	player->powers[pw_watershield] = 0;
	player->powers[pw_bombshield] = 0;
	player->powers[pw_ringshield] = 0;

#ifdef SRB2K
	player->powers[pw_bubbleshield] = 0;
	player->powers[pw_lightningshield] = 0;
	player->powers[pw_flameshield] = 0;
#endif
#endif

#ifndef ANGLE2D
	player->mo->flags |= MF_NOGRAVITY;
	player->mo->flags2 |= MF2_DONTDRAW;
#endif

#if 1 // SRB2CBTODO
	if (splitscreen && player == &players[secondarydisplayplayer])
		CV_SetValue(&cv_cam2_dist, 320);
	else if (player == &players[displayplayer])
		CV_SetValue(&cv_cam_dist, 320); // SRB2CBTODO: NO!
#endif

	player->nightstime = nighttime;
	player->bonustime = false;

#ifndef ANGLE2D
	P_SetMobjState(player->mo->tracer, S_SUPERTRANS1);
#endif

	if (gametype == GT_RACE)
	{
		if (player->drillmeter < 48*20)
			player->drillmeter = 48*20;
	}
	else
	{
		if (player->drillmeter < 40*20)
			player->drillmeter = 40*20;
	}

	oldmare = player->mare;

	if (P_TransferToNextMare(player) == false)
	{
		int i;

		P_SetTarget(&player->mo->target, NULL);

		for (i = 0; i < MAXPLAYERS; i++)
			P_DoPlayerExit(&players[i]);
	}

	if (oldmare != player->mare)
		player->mo->health = player->health = 1;

	player->pflags |= PF_NIGHTSMODE;
}

//
// P_DoPlayerPain
//
// Player was hit,
// put them in pain.
//
void P_DoPlayerPain(player_t *player, mobj_t *source, mobj_t *inflictor)
{
	angle_t ang;
	fixed_t fallbackspeed;

	player->mo->z++;

	if (player->mo->eflags & MFE_UNDERWATER)
		P_SetObjectMomZ(player->mo, FixedDiv(10511*FRACUNIT,2600*FRACUNIT), false, false);
	else
		P_SetObjectMomZ(player->mo, FixedDiv(69*FRACUNIT,10*FRACUNIT), false, false);

	if (inflictor)
	{
		ang = R_PointToAngle2(inflictor->x-inflictor->momx,	inflictor->y - inflictor->momy, player->mo->x - player->mo->momx, player->mo->y - player->mo->momy);

		// explosion and rail rings send you farther back, making it more difficult
		// to recover
		if ((inflictor->flags2 & MF2_SCATTER) && source)
		{
			fixed_t dist = P_AproxDistance(P_AproxDistance(source->x-player->mo->x, source->y-player->mo->y), source->z-player->mo->z);

			dist = 128*FRACUNIT - dist/4;

			if (dist < 4*FRACUNIT)
				dist = 4*FRACUNIT;

			fallbackspeed = dist;
		}
		else if (inflictor->flags2 & MF2_EXPLOSION)
		{
			if (inflictor->flags2 & MF2_RAILRING)
				fallbackspeed = 38*FRACUNIT; // 7x
			else
				fallbackspeed = 30*FRACUNIT; // 5x
		}
		else if (inflictor->flags2 & MF2_RAILRING)
			fallbackspeed = 45*FRACUNIT; // 4x
		else
			fallbackspeed = 4*FRACUNIT; // the usual amount of force
	}
	else
	{
		ang = R_PointToAngle2(player->mo->x + player->mo->momx, player->mo->y + player->mo->momy, player->mo->x, player->mo->y);
		fallbackspeed = 4*FRACUNIT;
	}

	if (maptol & TOL_ERZ3)
	{
		fallbackspeed >>= 2;
		player->mo->momz >>= 2;
	}

	if (twodlevel || (player->mo->flags2 & MF2_TWOD))
	{
		if (ang != 0 && ang != ANG180) // You're not facing left or right?
		{
			if (ang < ANG270 && ang > ANG90) // Find the closest angle for 180 or 0 degrees
				ang = ANG180; // Round to 180
			else
				ang = 0; // Round to 0
		}
	}

	P_InstaThrust(player->mo, ang, fallbackspeed);

	if ((player->pflags & PF_ROPEHANG) || (player->pflags & PF_MINECART))
		P_SetTarget(&player->mo->tracer, NULL);

	P_ResetPlayer(player);
	P_SetPlayerMobjState(player->mo, player->mo->info->painstate);
	player->powers[pw_flashing] = flashingtics;
}

//
// P_ResetPlayer
//
// Useful when you want to kill everything the player is doing.
void P_ResetPlayer(player_t *player)
{
	player->pflags &= ~PF_MINECART;
	player->pflags &= ~PF_ROPEHANG;
	player->pflags &= ~PF_MACESPIN;
	player->pflags &= ~PF_ITEMHANG;
	player->pflags &= ~PF_SPINNING;
	player->pflags &= ~PF_JUMPED;
	player->pflags &= ~PF_GLIDING;
	player->pflags &= ~PF_THOKKED;
	player->pflags &= ~PF_CARRIED;
	player->secondjump = false;
	player->glidetime = 0;
	player->homing = 0;
	player->climbing = 0;
	player->powers[pw_tailsfly] = 0;
	player->onconveyor = 0;
	player->itemspeed = 0;
}

//
// P_GivePlayerRings
//
// Gives rings to the player, and does any special things required.
// Call this function when you want to increment the player's health.
//
void P_GivePlayerRings(player_t *player, int num_rings, boolean flingring)
{
#ifdef PARANOIA
	if (!player->mo)
		return;
#endif

	player->mo->health += num_rings;
	player->health += num_rings;

	if (!flingring)
	{
		player->losscount = 0;
		player->totalring += num_rings;
	}
	else
	{
		if (player->mo->health > 2)
			player->losscount = 0;
	}

	// Can only get up to 9999 rings, sorry!
	if (player->mo->health > 10000)
	{
		player->mo->health = 10000;
		player->health = 10000;
	}
	else if (player->mo->health < 1)
	{
		player->mo->health = 1;
		player->health = 1;
	}
#ifdef JTEBOTS
	// Ring sharing stuff.
	if (gametype == GT_COOP && player->bot)
	{
		// Since you're a bot, lets just give these rings to your owner...
		if (players[player->bot->ownernum].playerstate == PST_LIVE)
		{
			P_GivePlayerRings(&players[player->bot->ownernum], num_rings, flingring);
			if (!(gamemap >= sstage_start && gamemap <= sstage_end))
			{
				player->health -= num_rings;
				player->mo->health -= num_rings; // Your player will give these back, dun worry.
			}
		}
	}
	else if (gametype == GT_COOP)
	{
		// Since you're a player, lets also give these rings to all your bots. :)
		int i, playernum = (int)(player-players);

		// What? You're not playing? This cannot be!
		if (playernum < 0 || playernum >= MAXPLAYERS
		   || !playeringame[playernum])
			return;

		// Don't give rings to bots in special stages!
		if (gamemap >= sstage_start && gamemap <= sstage_end)
			return;

		// Search the player list for bots and give them rings.
		for (i = 0; i < MAXPLAYERS; i++)
		{
			if (playeringame[i] && players[i].bot
			   && players[i].mo && players[i].mo->health
			   && players[i].bot->ownernum == playernum)
			{
				player = &players[i];
				player->health = player->mo->health += num_rings;
				if (player->mo->health > 1000)
					player->mo->health = player->health = 1000;
			}
		}
	}
#endif
}

//
// P_GivePlayerLives
//
// Gives the player an extra life.
// Call this function when you want to add lives to the player.
//
void P_GivePlayerLives(player_t *player, int numlives)
{
	player->lives += numlives;

	if (player->lives > 99)
		player->lives = 99;
	else if (player->lives < 1)
		player->lives = 1;
}

//
// P_DoSuperTransformation
//
// Transform into Super Sonic!
void P_DoSuperTransformation(player_t *player, boolean giverings)
{
	player->powers[pw_super] = 1;
	if (!mapheaderinfo[gamemap-1].nossmusic && P_IsLocalPlayer(player))
	{
		S_StopMusic();
		S_ChangeMusic(mus_supers, true);
	}

	S_StartSound(NULL, sfx_supert); //let all players hear it -mattw_cfi

	// Transformation animation
	if (player->charflags & SF_SUPERANIMS)
		P_SetPlayerMobjState(player->mo, S_PLAY_SUPERTRANS1);

	player->mo->momx >>= 1;
	player->mo->momy >>= 1;
	player->mo->momz >>= 1;

	if (giverings)
	{
		player->mo->health = 51;
		player->health = player->mo->health;
	}

	// Just in case.
	if (!mapheaderinfo[gamemap-1].nossmusic)
	{
		player->powers[pw_extralife] = 0;
		player->powers[pw_invulnerability] = 0;
	}
	player->powers[pw_sneakers] = 0;

	if (gametype != GT_COOP)
	{
		HU_SetCEchoFlags(0);
		HU_SetCEchoDuration(5);
		HU_DoCEcho(va("%s\\is now super.\\\\\\\\", player_names[player-players]));
		I_OutputMsg("%s is now super.\n", player_names[player-players]);
	}

	P_PlayerFlagBurst(player, false);
}
// Adds to the player's score
void P_AddPlayerScore(player_t *player, ULONG amount)
{
	ULONG oldscore = player->score;

	if (player->score + amount < MAXLONG)
		player->score += amount;
	else
		player->score = 0;

#ifdef JTEBOTS
	// Score sharing stuff
	if (gametype == GT_COOP && player->bot)
	{
		if (playeringame[player->bot->ownernum])
			P_AddPlayerScore(&players[player->bot->ownernum], amount);
		return;
	}
#endif

	// check for extra lives every 50000 pts
	if (player->score % 50000 < amount && (gametype == GT_RACE || gametype == GT_COOP)
		&& !(mapheaderinfo[gamemap-1].typeoflevel & TOL_NIGHTS))
	{
		P_GivePlayerLives(player, (player->score/50000) - (oldscore/50000));

		if (mariomode)
			S_StartSound(player->mo, sfx_marioa);
		else
		{
			if (P_IsLocalPlayer(player))
			{
				S_StopMusic();
				S_ChangeMusic(mus_xtlife, false);
			}
			player->powers[pw_extralife] = extralifetics + 1;
		}
	}

	// In team match, all awarded points are incremented to the team's running score.
	if (gametype == GT_MATCH && cv_matchtype.value)
	{
		if (player->ctfteam == 1)
			redscore += amount;
		else if (player->ctfteam == 2)
			bluescore += amount;
	}
}

//
// P_RestoreMusic
//
// Restores music after some special music change
//
void P_RestoreMusic(player_t *player)
{
	if (!P_IsLocalPlayer(player)) // Only applies to a local player
		return;

	if ((mus_playing == &S_music[mapmusic & 2047]) //the music is correct! don't come in and wreck our speed changes!
		&& !(player->powers[pw_super] && !mapheaderinfo[gamemap-1].nossmusic)
		&& !(player->powers[pw_invulnerability] > 1)
		&& !(player->powers[pw_sneakers] > 1))
		return;

	if (player->powers[pw_super] && !mapheaderinfo[gamemap-1].nossmusic)
	{
		S_SpeedMusic(1.0f);
		S_ChangeMusic(mus_supers, true);
	}
	else if (player->powers[pw_invulnerability] > 1 && player->powers[pw_extralife] <= 1)
	{
		S_SpeedMusic(1.0f);
		if (mariomode)
			S_ChangeMusic(mus_minvnc, false);
		else
			S_ChangeMusic(mus_invinc, false);
	}
	else if (player->powers[pw_sneakers] > 1)
	{
		if (S_SpeedMusic(0.0f) && mapheaderinfo[gamemap-1].speedmusic)
			S_SpeedMusic(1.4f);
		else
		{
			S_SpeedMusic(1.0f);
			S_ChangeMusic(mapmusic & 2047, true);
		}
	}
	else if (!(player->powers[pw_extralife] > 1))
	{
		S_SpeedMusic(1.0f);
		S_ChangeMusic(mapmusic & 2047, true);
	}
}

//
// P_GetPlayerHeight
//
// Returns the height
// of the player.
//
fixed_t P_GetPlayerHeight(player_t *player)
{
	return FIXEDSCALE(player->mo->info->height, player->mo->scale);
}

//
// P_GetPlayerSpinHeight
//
// Returns the 'spin height'
// of the player.
//
fixed_t P_GetPlayerSpinHeight(player_t *player)
{
	return FixedDiv(FIXEDSCALE(player->mo->info->height, player->mo->scale), 7*(FRACUNIT/4));
}

//
// P_IsLocalPlayer
//
// Returns true if player is
// on the local machine.
//
boolean P_IsLocalPlayer(player_t *player)
{
	return ((splitscreen && player == &players[secondarydisplayplayer]) || player == &players[consoleplayer]);
}

//
// P_SpawnShieldOrb
//
// Spawns the shield orb on the player
// depending on which shield they are
// supposed to have.
//
void P_SpawnShieldOrb(player_t *player)
{
	mobj_t *shieldobj = NULL;

#ifdef PARANOIA
	if (!player->mo)
		I_Error("P_SpawnShieldOrb: player->mo is NULL!\n");
#endif

	if (player->powers[pw_jumpshield])
		shieldobj = P_SpawnMobj(player->mo->x, player->mo->y, player->mo->z, MT_WHITEORB);
	else if (player->powers[pw_ringshield])
		shieldobj = P_SpawnMobj(player->mo->x, player->mo->y, player->mo->z, MT_YELLOWORB);
	else if (player->powers[pw_watershield])
		shieldobj = P_SpawnMobj(player->mo->x, player->mo->y, player->mo->z, MT_GREENORB);
	else if (player->powers[pw_bombshield])
		shieldobj = P_SpawnMobj(player->mo->x, player->mo->y, player->mo->z, MT_BLACKORB);
	else if (player->powers[pw_forceshield])
		shieldobj = P_SpawnMobj(player->mo->x, player->mo->y, player->mo->z, MT_BLUEORB);
#ifdef SRB2K
	else if (player->powers[pw_bubbleshield])
		shieldobj = P_SpawnMobj(player->mo->x, player->mo->y, player->mo->z, MT_BOUNCEORB);
	else if (player->powers[pw_lightningshield])
		shieldobj = P_SpawnMobj(player->mo->x, player->mo->y, player->mo->z, MT_ELECTRICORB);
	else if (player->powers[pw_flameshield])
		shieldobj = P_SpawnMobj(player->mo->x, player->mo->y, player->mo->z, MT_REDFIREORB);
#endif

	if (shieldobj)
	{
		P_SetTarget(&shieldobj->target, player->mo);
		shieldobj->flags |= MF_TRANSLATION;
		shieldobj->color = shieldobj->info->painchance;
	}
}

//
// P_SpawnThokMobj
//
// Spawns the appropriate thok object on the player
//
static mobj_t *P_SpawnThokMobj(player_t *player)
{
	mobj_t *mobj;
	mobjtype_t type;

	// Now check the player's color so the right THOK object is displayed.
	if (player->skincolor == 0)
		return NULL;

	if (player->spectator)
		return NULL;

	if (player->thokitem > 0)
		type = player->thokitem;
	else
		type = player->mo->info->painchance;

	mobj = P_SpawnMobj(player->mo->x, player->mo->y, player->mo->z, type);

	P_SetTarget(&mobj->target, player->mo);

	P_SetScale(mobj, mobj->target->player->mo->scale);
	mobj->destscale = mobj->target->player->mo->scale;

	// Sync proper colors with the thok image
	if (mobj->target->player)
	{
		mobj->flags |= MF_TRANSLATION;

		if (mobj->target->player->powers[pw_fireflower])
			mobj->color = 13;
		else if (mariomode && mobj->target->player->powers[pw_invulnerability] && !mobj->target->player->powers[pw_super])
			mobj->color = (leveltime % MAXSKINCOLORS);
		else
			mobj->color = ((player->powers[pw_super]) ? 15 : player->skincolor);
	}

	P_UnsetThingPosition(mobj);
	mobj->x = player->mo->x;
	mobj->y = player->mo->y;
	mobj->z = player->mo->z - (FIXEDSCALE(player->mo->info->height, player->mo->scale)
								- player->mo->height) / 3 + FIXEDSCALE(2*FRACUNIT, player->mo->scale);
	mobj->floorz = mobj->target->floorz;
	mobj->ceilingz = mobj->target->ceilingz;

	P_SetThingPosition(mobj); // Finaly, set the position

	return mobj;
}

//
// P_SpawnSpinMobj
//
// Spawns the appropriate spin object on the player
//
static mobj_t *P_SpawnSpinMobj(player_t *player, mobjtype_t type)
{
	mobj_t *mobj;

	if (player->skincolor == 0)
		return NULL;

	if (player->spectator)
		return NULL;

	mobj = P_SpawnMobj(player->mo->x, player->mo->y, player->mo->z, type);

	P_SetTarget(&mobj->target, player->mo);

	mobj->destscale = mobj->target->player->mo->scale;
	P_SetScale(mobj, mobj->target->player->mo->scale);

	// Sync proper colors with the thok image
	if (mobj->target->player)
	{
		mobj->flags |= MF_TRANSLATION;

		if (mobj->target->player->powers[pw_fireflower])
			mobj->color = 13;
		else if (mariomode && mobj->target->player->powers[pw_invulnerability] && !mobj->target->player->powers[pw_super])
			mobj->color = (leveltime % MAXSKINCOLORS);
		else
			mobj->color = ((player->powers[pw_super]) ? 15 : player->skincolor);
	}

	P_UnsetThingPosition(mobj);
	mobj->x = player->mo->x;
	mobj->y = player->mo->y;
	if (player->mo->eflags & MFE_VERTICALFLIP)
		mobj->z = player->mo->z - ((FIXEDSCALE(player->mo->info->height, player->mo->scale)
								   - player->mo->height) / 3 + FIXEDSCALE(2*FRACUNIT, player->mo->scale))*2;
	else
		mobj->z = player->mo->z - (FIXEDSCALE(player->mo->info->height, player->mo->scale)
								   - player->mo->height) / 3 + FIXEDSCALE(2*FRACUNIT, player->mo->scale);

	mobj->floorz = mobj->target->floorz;
	mobj->ceilingz = mobj->target->ceilingz;

	if (mobj->z < mobj->target->floorz)
		mobj->z = mobj->target->floorz;
	if (mobj->z > mobj->target->ceilingz - mobj->height)
		mobj->z = mobj->target->ceilingz - mobj->height;

	P_SetThingPosition(mobj); // Finaly, set the position

	// check again

	if (mobj->z < player->mo->floorz)
		mobj->z = player->mo->floorz;
	if (mobj->z > player->mo->ceilingz - mobj->height)
		mobj->z = player->mo->ceilingz - mobj->height;

	return mobj;
}

//
// P_DoPlayerExit
//
// Player exits the map via sector trigger
void P_DoPlayerExit(player_t *player)
{
	if (player->exiting)
		return;

#ifdef JTEBOTS // allow bots to finish levels?
	if (gametype == GT_COOP && player->bot)
	{
		P_ResetPlayer(player);
		return;
	}
#endif

	if (cv_allowexitlevel.value == 0 && (gametype == GT_MATCH || gametype == GT_TAG
										 || gametype == GT_CTF
#ifdef CHAOSISNOTDEADYET
										 || gametype == GT_CHAOS
#endif
										 ))
	{
		return;
	}
	else if (gametype == GT_RACE) // If in Race Mode, allow
	{

		if (!countdown) // a 60-second wait ala Sonic 2.
			countdown = cv_countdowntime.value*TICRATE + 1; // Use cv_countdowntime

		player->exiting = 3*TICRATE;

		if (!countdown2)
			countdown2 = (11 + cv_countdowntime.value)*TICRATE + 1; // 11sec more than countdowntime

		if (P_CheckRacers())
			player->exiting = (14*TICRATE)/5 + 1;
	}
	else
		player->exiting = (14*TICRATE)/5 + 2; // Accidental death safeguard???

	player->pflags &= ~PF_GLIDING;
	player->climbing = 0;
	player->powers[pw_underwater] = 1; // So music resets

	if (playeringame[player-players] && netgame && (gametype == GT_COOP || gametype == GT_RACE) && !circuitmap)
		CONS_Printf(text[FINISHEDLEVEL], player_names[player-players]);
}


static boolean P_InSpaceSector(mobj_t *mo) // Returns true if you are in space
{
	// Space's specialnum is 12
	sector_t *sector;

	sector = mo->subsector->sector;

	if (GETSECSPECIAL(sector->special, 1) == 12) // SRB2CBTODO: Don't use #, use #defines!
		return true;

	if (sector->ffloors)
	{
		ffloor_t *rover;

		for (rover = sector->ffloors; rover; rover = rover->next) // SRB2CBTODO: REALLY Need to function split this up
		{
			fixed_t topheight = *rover->topheight;
			fixed_t bottomheight = *rover->bottomheight;

#ifdef ESLOPE
			if (rover->t_slope)
				topheight = P_GetZAt(rover->t_slope, mo->x, mo->y);
			if (rover->b_slope)
				bottomheight = P_GetZAt(rover->b_slope, mo->x, mo->y);
#endif

			if (GETSECSPECIAL(rover->master->frontsector->special, 1) != 12)
				continue;

			if (mo->z > topheight)
				continue;

			if (mo->z + (mo->height/2) < bottomheight)
				continue;

			return true;
		}
	}

	return false; // No vacuum here, Captain!
}

static boolean P_InQuicksand(mobj_t *mo) // Returns true if you are in quicksand
{
	sector_t *sector;

	sector = mo->subsector->sector;

	if (sector->ffloors)
	{
		ffloor_t *rover;

		for (rover = sector->ffloors; rover; rover = rover->next)
		{
			fixed_t topheight = *rover->topheight;
			fixed_t bottomheight = *rover->bottomheight;

#ifdef ESLOPE
			if (rover->t_slope)
				topheight = P_GetZAt(rover->t_slope, mo->x, mo->y);
			if (rover->b_slope)
				bottomheight = P_GetZAt(rover->b_slope, mo->x, mo->y);
#endif

			if (!(rover->flags & FF_EXISTS))
				continue;

			if (!(rover->flags & FF_QUICKSAND))
				continue;

			if (mo->z > topheight)
				continue;

			if (mo->z + (mo->height/2) < bottomheight)
				continue;

			return true;
		}
	}

	return false; // No sand here, Captain!
}

//
// P_CheckSneakerAndLivesTimer
//
// Restores music from sneaker and life fanfares
//
static void P_CheckSneakerAndLivesTimer(player_t *player)
{
	if (player->powers[pw_extralife] == 1) // Extra Life!
		P_RestoreMusic(player);

	if (player->powers[pw_sneakers] == 1)
		P_RestoreMusic(player);
}

//
// P_CheckUnderwaterAndSpaceTimer
//
// Restores music from underwater and space warnings, and handles number generation
//
static void P_CheckUnderwaterAndSpaceTimer(player_t *player)
{
	fixed_t height;
	mobj_t *numbermobj = NULL;

	// Create air bubbles from the player's mouth
	if (player->mo->eflags & MFE_UNDERWATER && !(player->powers[pw_watershield]) && !player->spectator)
	{
		fixed_t zh;

		if (player->mo->eflags & MFE_VERTICALFLIP)
			zh = player->mo->z + player->mo->height - FixedDiv(player->mo->height,5*(FRACUNIT/4));
		else
			zh = player->mo->z + FixedDiv(player->mo->height,5*(FRACUNIT/4));

		if (!(P_Random() % 16))
			P_SpawnMobj(player->mo->x, player->mo->y, zh, MT_SMALLBUBBLE)->threshold = 42;
		else if (!(P_Random() % 96))
			P_SpawnMobj(player->mo->x, player->mo->y, zh, MT_MEDIUMBUBBLE)->threshold = 42;

		// Stir up the water while flying in it
		if (player->powers[pw_tailsfly] && (leveltime & 1*NEWTICRATERATIO) && player->charability != CA_SWIM)
		{
			fixed_t radius = (3*player->mo->radius)>>1;
			angle_t fa = ((leveltime % 45)*FINEANGLES/8) & FINEMASK;
			fixed_t stirwaterx = FixedMul(FINECOSINE(fa),radius);
			fixed_t stirwatery = FixedMul(FINESINE(fa),radius);
			fixed_t stirwaterz;

			if (player->mo->eflags & MFE_VERTICALFLIP)
				stirwaterz = player->mo->z + player->mo->height - FixedDiv(player->mo->height, 3*FRACUNIT/2);
			else
				stirwaterz = player->mo->z + FixedDiv(player->mo->height, 3*FRACUNIT/2);

			mobj_t *bubble;

			bubble = P_SpawnMobj(player->mo->x + stirwaterx,
								 player->mo->y + stirwatery,
								 stirwaterz, MT_SMALLBUBBLE);

			bubble->destscale = player->mo->scale;
			P_SetScale(bubble, player->mo->scale);

			bubble = P_SpawnMobj(player->mo->x - stirwaterx,
								 player->mo->y - stirwatery,
								 stirwaterz, MT_SMALLBUBBLE);

			bubble->destscale = player->mo->scale;
			P_SetScale(bubble, player->mo->scale);
		}

	}

	if (player->mo->eflags & MFE_VERTICALFLIP)
		height = player->mo->z - FIXEDSCALE(8*FRACUNIT - mobjinfo[MT_DROWNNUMBERS].height, player->mo->scale);
	else
		height = player->mo->z + player->mo->height + FIXEDSCALE(8*FRACUNIT, player->mo->scale);

	if (player->powers[pw_underwater] == 11*TICRATE + 1 || player->powers[pw_spacetime] == 11*TICRATE + 1)
	{
		numbermobj = P_SpawnMobj(player->mo->x, player->mo->y, height, MT_DROWNNUMBERS);
		P_SetMobjState(numbermobj, numbermobj->info->spawnstate+5);
	}
	else if (player->powers[pw_underwater] == 9*TICRATE + 1 || player->powers[pw_spacetime] == 9*TICRATE + 1)
	{
		numbermobj = P_SpawnMobj(player->mo->x, player->mo->y, height, MT_DROWNNUMBERS);
		P_SetMobjState(numbermobj, numbermobj->info->spawnstate+4);
	}
	else if (player->powers[pw_underwater] == 7*TICRATE + 1 || player->powers[pw_spacetime] == 7*TICRATE + 1)
	{
		numbermobj = P_SpawnMobj(player->mo->x, player->mo->y, height, MT_DROWNNUMBERS);
		P_SetMobjState(numbermobj, numbermobj->info->spawnstate+3);
	}
	else if (player->powers[pw_underwater] == 5*TICRATE + 1 || player->powers[pw_spacetime] == 5*TICRATE + 1)
	{
		numbermobj = P_SpawnMobj(player->mo->x, player->mo->y, height, MT_DROWNNUMBERS);
		P_SetMobjState(numbermobj, numbermobj->info->spawnstate+2);
	}
	else if (player->powers[pw_underwater] == 3*TICRATE + 1 || player->powers[pw_spacetime] == 3*TICRATE + 1)
	{
		numbermobj = P_SpawnMobj(player->mo->x, player->mo->y, height, MT_DROWNNUMBERS);
		P_SetMobjState(numbermobj, numbermobj->info->spawnstate+1);
	}
	else if (player->powers[pw_underwater] == 1*TICRATE + 1 || player->powers[pw_spacetime] == 1*TICRATE + 1)
	{
		numbermobj = P_SpawnMobj(player->mo->x, player->mo->y, height, MT_DROWNNUMBERS);
		//P_SetMobjState(numbermobj, numbermobj->info->spawnstate+0);
	}
	// Underwater timer runs out
	else if (player->powers[pw_underwater] == 1)
	{
		mobj_t *killer;

		if ((netgame || multiplayer) && P_IsLocalPlayer(player))
			S_ChangeMusic(mapmusic & 2047, true);

		killer = P_SpawnMobj(player->mo->x, player->mo->y, player->mo->z, MT_DISS);
		killer->threshold = 42; // Special flag that it was drowning which killed you.

		P_DamageMobj(player->mo, killer, killer, 10000);
	}
	else if (player->powers[pw_spacetime] == 1)
	{
		if ((netgame || multiplayer) && P_IsLocalPlayer(player))
			S_ChangeMusic(mapmusic & 2047, true);

		P_DamageMobj(player->mo, NULL, NULL, 10000);
	}

	if (numbermobj)
	{
		P_SetTarget(&numbermobj->target, player->mo);
		numbermobj->threshold = 40;
		S_StartSound(player->mo, sfx_dwnind);
		numbermobj->destscale = player->mo->scale;
		P_SetScale(numbermobj, player->mo->scale);
	}

	if (!(player->mo->eflags & MFE_UNDERWATER) && player->powers[pw_underwater])
	{
		if (player->powers[pw_underwater] <= 12*TICRATE + 1)
			P_RestoreMusic(player);

		player->powers[pw_underwater] = 0;
	}

	if (player->powers[pw_spacetime] > 1 && !P_InSpaceSector(player->mo))
	{
		P_RestoreMusic(player);
		player->powers[pw_spacetime] = 0;
	}

	// Underwater audio cues
	if (P_IsLocalPlayer(player))
	{
		if (player->powers[pw_underwater] == 11*TICRATE + 1)
		{
			S_StopMusic();
			S_ChangeMusic(mus_drown, false);
		}

		if (player->powers[pw_underwater] == 25*TICRATE + 1)
			S_StartSound(NULL, sfx_wtrdng);
		else if (player->powers[pw_underwater] == 20*TICRATE + 1)
			S_StartSound(NULL, sfx_wtrdng);
		else if (player->powers[pw_underwater] == 15*TICRATE + 1)
			S_StartSound(NULL, sfx_wtrdng);
	}

	if (player->exiting)
	{
		if (player->powers[pw_underwater] > 1)
			player->powers[pw_underwater] = 0;

		player->powers[pw_spacetime] = 0;
	}
}

//
// P_DoSuperStuff()
//
// Handle related superform functionality.
//
static void P_DoSuperStuff(player_t *player)
{
	// Does player have all emeralds? If so, flag the "Ready For Super!"
	if ((ALL7EMERALDS(emeralds) || ALL7EMERALDS(player->powers[pw_emeralds])) && player->health > 50)
		player->pflags |= PF_SUPERREADY;
	else
		player->pflags &= ~PF_SUPERREADY;

	player->mo->flags |= MF_TRANSLATION;
	if (player->powers[pw_fireflower])
		player->mo->color = 13;
	else
		player->mo->color = player->skincolor;

	if (player->powers[pw_super])
	{
		// If you're super and not Sonic, de-superize!
		if (!((ALL7EMERALDS(emeralds)) && (player->skin == 0)) && !(ALL7EMERALDS(player->powers[pw_emeralds])))
		{
			player->powers[pw_super] = 0;
			P_SetPlayerMobjState(player->mo, S_PLAY_STND);
			P_RestoreMusic(player);
			P_SpawnShieldOrb(player);
			if (gametype != GT_COOP)
			{
				HU_SetCEchoFlags(0);
				HU_SetCEchoDuration(5);
				HU_DoCEcho(va("%s\\is no longer super.\\\\\\\\", player_names[player-players]));
				I_OutputMsg("%s is no longer super.\n", player_names[player-players]);
			}
		}

		// Deplete one ring every second while super
		if ((leveltime % TICRATE == 0) && !(player->exiting))
		{
			player->health--;
			player->mo->health--;
		}

		// You're yellow now!
		player->mo->flags |= MF_TRANSLATION;
		player->mo->color = 15;

		// Ran out of rings while super!
		if ((player->powers[pw_super]) && (player->health <= 1 || player->exiting))
		{
			player->powers[pw_emeralds] = 0; // lost the power stones
			P_SpawnGhostMobj(player->mo);

			player->powers[pw_super] = 0;

			if (gametype != GT_COOP)
				player->powers[pw_flashing] = flashingtics-1;

			if (player->mo->health > 0)
			{
				if ((player->pflags & PF_JUMPED) || (player->pflags & PF_SPINNING))
					P_SetPlayerMobjState(player->mo, S_PLAY_ATK1);
				else if (player->pflags & PF_RUNNINGANIM)
					P_SetPlayerMobjState(player->mo, S_PLAY_SPD1);
				else if (player->pflags & PF_WALKINGANIM)
					P_SetPlayerMobjState(player->mo, S_PLAY_RUN1);
				else
					P_SetPlayerMobjState(player->mo, S_PLAY_STND);

				if (!player->exiting)
				{
					player->health = 1;
					player->mo->health = 1;
				}
			}

			// Inform the netgame that the champion has fallen in the heat of battle.
			if (gametype != GT_COOP)
			{
				S_StartSound(NULL, sfx_s3k_52); //let all players hear it.
				HU_SetCEchoFlags(0);
				HU_SetCEchoDuration(5);
				HU_DoCEcho(va("%s\\is no longer super.\\\\\\\\", player_names[player-players]));
				I_OutputMsg("%s is no longer super.\n", player_names[player-players]);
			}

			// Resume normal music if you're the console player
			P_RestoreMusic(player);

			// If you had a shield, restore its visual significance.
			P_SpawnShieldOrb(player);
		}
	}
}

//
// P_DoJump
//
// Jump routine for the player
//
void P_DoJump(player_t *player, boolean soundandstate)
{
	fixed_t factor;

	if (player->pflags & PF_STASIS || (player->powers[pw_nocontrol] && !(player->powers[pw_nocontrol] & FRACUNIT)))
		return;

	if (!player->jumpfactor)
		return;

	if (player->powers[pw_ingoop])
		return;

	if ((player->pflags & PF_MINECART) && !(player->mo->tracer && player->mo->tracer->type == MT_SKATEBOARD))
	{
		player->itemspeed = 0;
		player->pflags &= ~PF_MINECART;
		P_SetTarget(&player->mo->tracer, NULL);
	}

	if (player->climbing)
	{
		// Jump this high.
		if (player->powers[pw_super])
			player->mo->momz = 5*FRACUNIT;
		else if (player->mo->eflags & MFE_UNDERWATER)
			player->mo->momz = 2*FRACUNIT;
		else
			player->mo->momz = 15*(FRACUNIT/4);

		player->mo->angle = player->mo->angle - ANG180; // Turn around from the wall you were climbing.

		if (player == &players[consoleplayer])
			localangle = player->mo->angle; // Adjust the local control angle.
		else if (splitscreen && player == &players[secondarydisplayplayer])
			localangle2 = player->mo->angle;

		player->climbing = 0; // Stop climbing, duh!
		P_InstaThrust(player->mo, player->mo->angle, 6*FRACUNIT); // Jump off the wall.
	}
	else if (!(player->pflags & PF_JUMPED)) // Spin Attack
	{
		if (player->mo->ceilingz-player->mo->floorz <= player->mo->height-1)
			return;

		// Jump this high.
		if (player->pflags & PF_CARRIED)
		{
			player->mo->momz = 9*FRACUNIT;
			player->pflags &= ~PF_CARRIED;
		}
		else if (player->pflags & PF_ITEMHANG)
		{
			player->mo->momz = 9*FRACUNIT;
			player->pflags &= ~PF_ITEMHANG;
			// If there is no glider controler,
			// Slowly lower the glider to the floor
			if (player->mo->tracer && player->mo->tracer->type == MT_HANGGLIDER)
			{
				// The player should be able to use the character's ability after jumping off
				player->pflags &= ~PF_THOKKED;
				P_SetObjectMomZ(player->mo->tracer, -FRACUNIT*3, false, false);
				P_SetTarget(&player->mo->tracer, NULL);
			}
		}
		else if (player->pflags & PF_ROPEHANG)
		{
			player->mo->momz = 12*FRACUNIT;
			player->pflags &= ~PF_ROPEHANG;
			P_SetTarget(&player->mo->tracer, NULL);
		}
		else if (player->pflags & PF_MINECART)
		{
			if (player->mo->tracer && player->mo->tracer->type == MT_SKATEBOARD)
					player->mo->tracer->momz = 34*(FRACUNIT/4);
		}
		else if (player->powers[pw_super])
		{
			if (player->charability == CA_FLOAT)
				player->mo->momz = 28*FRACUNIT; // Obscene jump height anyone?
			else if (player->charability == CA_SLOWFALL)
				player->mo->momz = 37*(FRACUNIT/2); // Less obscene because during super, floating propells oneself upward.
			else // Default super jump momentum.
				player->mo->momz = 13*FRACUNIT;

			// Add a boost for super characters with float/slowfall and multiability.
			if (player->charability2 == CA2_MULTIABILITY &&
				(player->charability == CA_FLOAT || player->charability == CA_SLOWFALL))
					P_SetObjectAbsMomZ(player->mo, 2*FRACUNIT, true);
		}
		else if (player->charability2 == CA2_MULTIABILITY &&
			(player->charability == CA_DOUBLEJUMP || player->charability == CA_FLOAT || player->charability == CA_SLOWFALL))
		{
			// Multiability exceptions, since some abilities cannot effectively use it and need a boost.
			if (player->charability == CA_DOUBLEJUMP)
				player->mo->momz = 23*(FRACUNIT/2); // Increased jump height instead of infinite jumps.
			else if (player->charability == CA_FLOAT || player->charability == CA_SLOWFALL)
				player->mo->momz = 12*FRACUNIT; // Increased jump height due to ineffective repeat.
		}
		else
			player->mo->momz = 39*(FRACUNIT/4); // Default jump momentum.

		// Reduce player momz by 58.5% when underwater.
		if (player->mo->eflags & MFE_UNDERWATER)
		{
			if (player->pflags & PF_MINECART)
			{
				if (player->mo->tracer && player->mo->tracer->type == MT_SKATEBOARD)
					player->mo->tracer->momz = FixedMul(player->mo->tracer->momz, 117*(FRACUNIT/200));
			}
			else
				player->mo->momz = FixedMul(player->mo->momz, 117*(FRACUNIT/200));
		}

		// Quicksand bitshift reduction.
		if (P_InQuicksand(player->mo))
		{
			if (player->pflags & PF_MINECART)
			{
				if (player->mo->tracer && player->mo->tracer->type == MT_SKATEBOARD)
				{
					player->mo->tracer->momz = player->mo->tracer->momz>>1;
				}
			}
			else
				player->mo->momz >>= 1;
		}

		player->jumping = true;
	}

	factor = player->jumpfactor;

	if (twodlevel || (player->mo->flags2 & MF2_TWOD))
		factor *= 1.1f;

#ifdef VPHYSICS // Jump higher when running up a slope!
	mobj_t *thing = player->mo;
	if (P_IsObjectOnSlope(thing, false))
	{
		fixed_t thingspeed = P_AproxDistance(thing->momx, thing->momy);
		sector_t *nextsector;
		fixed_t predictmomx = thing->x+(thing->momx/2);
		fixed_t predictmomy = thing->y+(thing->momy/2);
		nextsector = R_PointInSubsector(predictmomx, predictmomy)->sector;
		sector_t *currentsector = R_PointInSubsector(thing->x, thing->y)->sector;
		fixed_t zthrust = 0;

		fixed_t currentz = P_GetZAt(currentsector->f_slope, thing->x, thing->y);
		fixed_t predictz = P_GetZAt(currentsector->f_slope, thing->x+thing->momx, thing->y+thing->momy);

		predictz += (((thing->pitchangle/(ANG45/45))+90)/70.0f)+thingspeed/((31 - currentsector->f_slope->zangle/3));

		// Make sure that the z doesn't go too high for steep slopes
		if (currentsector->f_slope->zangle > 50)
			predictz -= (currentsector->f_slope->zangle/2.7f)*FRACUNIT;

		zthrust = (predictz - currentz)/2;
		thing->momz += zthrust/1.9f; // VPHYSICS TODO: Make a real formula for z trajectory going off a slope
		/*CONS_Printf("CurZ %i,  PredictZ %i\n", currentz/FRACUNIT, predictz/FRACUNIT);
		 CONS_Printf("Pitch: %i\n", thing->pitchangle/(ANG45/45)+90);
		 CONS_Printf("ZThrust: %i\n", zthrust/FRACUNIT);*/
	}
#endif

	if (player->pflags & PF_MINECART)
	{
		if (player->mo->tracer && player->mo->tracer->type == MT_SKATEBOARD)
		{
#ifdef VPHYSICS
			// VPHYSICS!
			if (player->mo->subsector->sector && player->mo->subsector->sector->f_slope)
			{
				v3float_t vector = player->mo->subsector->sector->f_slope->normalf;

				// Set the tracer's momz just for good measure
				player->mo->tracer->momx += FLOAT_TO_FIXED(vector.x)*6;
				player->mo->tracer->momy += FLOAT_TO_FIXED(vector.y)*6;
				player->mo->tracer->momz += FLOAT_TO_FIXED(vector.z);

				player->mo->momx += FLOAT_TO_FIXED(vector.x)*6;
				player->mo->momy += FLOAT_TO_FIXED(vector.y)*6;
				player->mo->momz += FLOAT_TO_FIXED(vector.z);
			}
#endif
			P_SetObjectMomZ(player->mo->tracer, FixedDiv(factor*player->mo->tracer->momz, 100*FRACUNIT), false, false); // Custom height
		}
	}
	else
	{
#ifdef VPHYSICS
		if (player->mo->subsector->sector && player->mo->subsector->sector->f_slope)
		{
			// VPHYSICS!
			v3float_t vector = player->mo->subsector->sector->f_slope->normalf;

			player->mo->momx += FLOAT_TO_FIXED(vector.x)*6;
			player->mo->momy += FLOAT_TO_FIXED(vector.y)*6;
			player->mo->momz += FLOAT_TO_FIXED(vector.z); // VPHYSICS TODO: Rewrite everything for vectors!
		}
#endif

		P_SetObjectMomZ(player->mo, FixedDiv(factor*player->mo->momz, 100*FRACUNIT), false, false); // Custom height
	}

	// set just an eensy above the ground
	if (player->pflags & PF_MINECART)
	{
		if (player->mo->tracer && player->mo->tracer->type == MT_SKATEBOARD)
		{
			if (player->mo->eflags & MFE_VERTICALFLIP)
			{
				player->mo->tracer->z = player->mo->tracer->z;
				player->mo->tracer->z--;
			}
			else
				player->mo->tracer->z++;
		}
	}
	else
	{
		if (player->mo->eflags & MFE_VERTICALFLIP)
		{
			player->mo->z = player->mo->z + (P_GetPlayerHeight(player) - P_GetPlayerSpinHeight(player));
			player->mo->z--;
		}
		else
			player->mo->z++;
	}

	// SRB2CBTODO: pmomz MFE_VERTICALFLIP?
	if ((player->pflags & PF_MINECART) && player->mo->tracer && player->mo->tracer->type == MT_SKATEBOARD)
		player->mo->tracer->z += player->mo->tracer->pmomz;
	else
		player->mo->z += player->mo->pmomz; // Solves problem of 'hitting around again after jumping on a moving platform'.

	if (!(player->pflags & PF_SPINNING))
		P_ResetScore(player);

	player->pflags |= PF_JUMPED;

	if ((player->pflags & PF_MINECART) && !(player->mo->tracer && player->mo->tracer->type == MT_SKATEBOARD))
	{
		if (!player->spectator)
			S_StartSound(player->mo, sfx_jump); // Play jump sound!
	}
	else
	{
		if (soundandstate)
		{
			if (!player->spectator)
				S_StartSound(player->mo, sfx_jump); // Play jump sound!

			if (!(player->charability2 == CA2_SPINDASH))
				P_SetPlayerMobjState(player->mo, S_PLAY_PLG1);
			else
				P_SetPlayerMobjState(player->mo, S_PLAY_ATK1);
		}
	}
}

//
// P_DoSpinDash
//
// Player spindash handling
//
#ifdef ESLOPE
static void P_DoSpinDash(player_t *player, ticcmd_t *cmd)
{
	if (player->pflags & PF_STASIS || player->powers[pw_nocontrol])
		return;

	// Spinning and Spindashing
	if ((player->charability2 == CA2_SPINDASH) && !(player->pflags & PF_SLIDING) && !player->exiting && !(!(player->pflags & PF_SLIDING)
		&& player->mo->state == &states[player->mo->info->painstate] && player->powers[pw_flashing])) // subsequent revs
	{
		if (P_SlopeLessThan(player->mo, false, LEVELSLOPE) || P_SlopeLessThan(player->mo, true, LEVELSLOPE))
		{
			if ((cmd->buttons & BT_USE || ((twodlevel || (player->mo->flags2 & MF2_TWOD)) && cmd->forwardmove < -20))
				&& !player->climbing && onground && !(player->pflags & PF_USEDOWN)
				&& !(player->pflags & PF_SPINNING) && !player->usedspin)
			{
				P_ResetScore(player);
				player->pflags |= PF_SPINNING;
				P_SetPlayerMobjState(player->mo, S_PLAY_ATK1);
				if (!player->spectator)
					S_StartSound(player->mo, sfx_spin);
				player->pflags |= PF_USEDOWN;
				if (player->mo->eflags & MFE_VERTICALFLIP)
					player->mo->z = player->mo->ceilingz - P_GetPlayerSpinHeight(player);
			}

		}
		else if (!(P_SlopeLessThan(player->mo, false, LEVELSLOPE) || P_SlopeLessThan(player->mo, true, LEVELSLOPE))
				 && (cmd->buttons & BT_USE) && player->speed < 5 && !player->mo->momz
			&& onground && !(player->pflags & PF_USEDOWN) && !(player->pflags & PF_SPINNING))
		{
			P_ResetScore(player);
			player->mo->momx = player->cmomx;
			player->mo->momy = player->cmomy;
			player->pflags |= PF_STARTDASH;
			player->pflags |= PF_SPINNING;
			player->dashspeed = FIXEDSCALE(FRACUNIT, player->mo->scale); // SRB2CBTODO: Correct to /by ticratio?
			P_SetPlayerMobjState(player->mo, S_PLAY_ATK1);
			player->pflags |= PF_USEDOWN;
			if (player->mo->eflags & MFE_VERTICALFLIP)
				player->mo->z = player->mo->ceilingz - P_GetPlayerSpinHeight(player);
		}
		else if (!(P_SlopeLessThan(player->mo, false, LEVELSLOPE) || P_SlopeLessThan(player->mo, true, LEVELSLOPE)) && (cmd->buttons & BT_USE) && (player->pflags & PF_STARTDASH))
		{
			player->dashspeed += FIXEDSCALE(FRACUNIT, player->mo->scale)/(float)NEWTICRATERATIO;

			if ((leveltime % (TICRATE/10)) == 0)
			{
				mobj_t *item;

				if (!player->spectator)
					S_StartSound(player->mo, sfx_spndsh); // Make the rev sound!

				// Now spawn the color thok circle.
				if (player->spinitem > 0)
					item = P_SpawnSpinMobj(player, player->spinitem);
				else
					item = P_SpawnSpinMobj(player, player->mo->info->raisestate);

				if (item && (player->charflags & SF_GHOSTSPINITEM))
				{
					P_SpawnGhostMobj(item);
					P_SetMobjState(item, S_DISS);
				}
			}
		}
		// If not moving up or down, and travelling faster than a speed of four while not holding
		// down the spin button and not spinning.
		// AKA Just go into a spin on the ground ;)
		else
		{
			if ((cmd->buttons & BT_USE || ((twodlevel || (player->mo->flags2 & MF2_TWOD)) && cmd->forwardmove < -20))
				&& !player->climbing && (!player->mo->momz) && onground && (player->speed > 5*(player->mo->scale/100) || (P_SlopeLessThan(player->mo, false, LEVELSLOPE) || P_SlopeLessThan(player->mo, true, LEVELSLOPE))) && !(player->pflags & PF_USEDOWN)
				&& !(player->pflags & PF_SPINNING) && !player->usedspin)
			{
				P_ResetScore(player);
				player->pflags |= PF_SPINNING;
				P_SetPlayerMobjState(player->mo, S_PLAY_ATK1);
				if (!player->spectator)
					S_StartSound(player->mo, sfx_spin);
				player->pflags |= PF_USEDOWN;
				if (player->mo->eflags & MFE_VERTICALFLIP)
					player->mo->z = player->mo->ceilingz - P_GetPlayerSpinHeight(player);

				player->usedspin = true;
				player->unspinready = false;
			}

			if (!onground || (P_SlopeLessThan(player->mo, false, LEVELSLOPE) || P_SlopeLessThan(player->mo, true, LEVELSLOPE)) || !(player->pflags & PF_SPINNING))
				player->usedspin = false;


			if (player->usedspin && !(cmd->buttons & BT_USE) && (player->pflags & PF_SPINNING))
				player->unspinready = true;

			if (!onground || !(player->pflags & PF_SPINNING) || (P_SlopeLessThan(player->mo, false, LEVELSLOPE) || P_SlopeLessThan(player->mo, true, LEVELSLOPE)))
				player->unspinready = false;


			// If spin button is not down from the first spin
			if ((cmd->buttons & BT_USE) && player->unspinready && player->usedspin && (player->pflags & PF_SPINNING))
			{
				player->pflags &= ~PF_SPINNING;
				P_SetPlayerMobjState(player->mo, S_PLAY_STND);
				player->unspinready = false;
				player->usedspin = false;
			}

		}

	}


	if (!(P_SlopeLessThan(player->mo, false, LEVELSLOPE) || P_SlopeLessThan(player->mo, true, LEVELSLOPE)))
	{
		if (onground && (player->pflags & PF_SPINNING) && !(player->pflags & PF_STARTDASH)
			&& (player->rmomx < FIXEDSCALE(5*FRACUNIT/(float)NEWTICRATERATIO, player->mo->scale)
				&& player->rmomx > -FIXEDSCALE(5*FRACUNIT/(float)NEWTICRATERATIO, player->mo->scale))
			&& (player->rmomy < FIXEDSCALE(5*FRACUNIT/(float)NEWTICRATERATIO, player->mo->scale)
				&& player->rmomy > -FIXEDSCALE(5*FRACUNIT/(float)NEWTICRATERATIO, player->mo->scale)))
		{
				if (GETSECSPECIAL(player->mo->subsector->sector->special, 4) == 7 || (player->mo->ceilingz - player->mo->floorz < P_GetPlayerHeight(player)))
					P_InstaThrust(player->mo, player->mo->angle, 10*FRACUNIT); // SRB2CBCHECK:
				else
				{
					player->pflags &= ~PF_SPINNING;
					P_SetPlayerMobjState(player->mo, S_PLAY_STND);
					player->mo->momx = player->cmomx;
					player->mo->momy = player->cmomy;
					P_ResetScore(player);
					if (player->mo->eflags & MFE_VERTICALFLIP)
						player->mo->z = player->mo->ceilingz - P_GetPlayerHeight(player);
				}
		}
	}

	// Catapult the player from a spindash rev!
	if (!(P_SlopeLessThan(player->mo, false, LEVELSLOPE) || P_SlopeLessThan(player->mo, true, LEVELSLOPE))
		&& onground && !(player->pflags & PF_USEDOWN) && player->dashspeed && (player->pflags & PF_STARTDASH) && (player->pflags & PF_SPINNING))
	{
		if (player->powers[pw_ingoop])
			player->dashspeed = 0;

		player->pflags &= ~PF_STARTDASH;
		if (!(gametype == GT_RACE && leveltime < 4*TICRATE) && !player->usedspin)
		{
			P_InstaThrust(player->mo, player->mo->angle, player->dashspeed); // catapult forward ho!!
			if (!player->spectator)
				S_StartSound(player->mo, sfx_zoom);

			player->usedspin = true;
			player->unspinready = false;
		}

		if (player->usedspin && !(cmd->buttons & BT_USE) && (player->pflags & PF_SPINNING) && onground && (player->pflags & PF_STARTDASH))
			player->unspinready = true;

		if (!onground || !(player->pflags & PF_SPINNING))
			player->unspinready = false;

		player->dashspeed = 0;
	}

	if (onground && (player->pflags & PF_SPINNING)
		&& !(player->mo->state >= &states[S_PLAY_ATK1]
		&& player->mo->state <= &states[S_PLAY_ATK4]))
		P_SetPlayerMobjState(player->mo, S_PLAY_ATK1);
}
#else


static void P_DoSpinDash(player_t *player, ticcmd_t *cmd)
{
	if (player->pflags & PF_STASIS || player->powers[pw_nocontrol])
		return;

	// Spinning and Spindashing
	if ((player->charability2 == CA2_SPINDASH) && !(player->pflags & PF_SLIDING) && !player->exiting && !(!(player->pflags & PF_SLIDING)
																										  && player->mo->state == &states[player->mo->info->painstate] && player->powers[pw_flashing])) // subsequent revs
	{
		if ((cmd->buttons & BT_USE) && player->speed < 5 && !player->mo->momz
			&& onground && !(player->pflags & PF_USEDOWN) && !(player->pflags & PF_SPINNING))
		{
			P_ResetScore(player);
			player->mo->momx = player->cmomx;
			player->mo->momy = player->cmomy;
			player->pflags |= PF_STARTDASH;
			player->pflags |= PF_SPINNING;
			player->dashspeed = FIXEDSCALE(FRACUNIT, player->mo->scale); // SRB2CBTODO: Correct to /by ticratio?
			P_SetPlayerMobjState(player->mo, S_PLAY_ATK1);
			player->pflags |= PF_USEDOWN;
			if (player->mo->eflags & MFE_VERTICALFLIP)
				player->mo->z = player->mo->ceilingz - P_GetPlayerSpinHeight(player);
		}
		else if ((cmd->buttons & BT_USE) && (player->pflags & PF_STARTDASH))
		{
			player->dashspeed += FIXEDSCALE(FRACUNIT, player->mo->scale)/(float)NEWTICRATERATIO;

			if ((leveltime % (TICRATE/10)) == 0)
			{
				mobj_t *item;

				if (!player->spectator)
					S_StartSound(player->mo, sfx_spndsh); // Make the rev sound!

				// Now spawn the color thok circle.
				if (player->spinitem > 0)
					item = P_SpawnSpinMobj(player, player->spinitem);
				else
					item = P_SpawnSpinMobj(player, player->mo->info->raisestate);

				if (item && (player->charflags & SF_GHOSTSPINITEM))
				{
					P_SpawnGhostMobj(item);
					P_SetMobjState(item, S_DISS);
				}
			}
		}
		// If not moving up or down, and travelling faster than a speed of four while not holding
		// down the spin button and not spinning.
		// AKA Just go into a spin on the ground ;)
		else
		{
			if ((cmd->buttons & BT_USE || ((twodlevel || (player->mo->flags2 & MF2_TWOD)) && cmd->forwardmove < -20))
				&& !player->climbing && !player->mo->momz && onground && player->speed > 5 && !(player->pflags & PF_USEDOWN)
				&& !(player->pflags & PF_SPINNING) && !player->usedspin)
			{
				P_ResetScore(player);
				player->pflags |= PF_SPINNING;
				P_SetPlayerMobjState(player->mo, S_PLAY_ATK1);
				if (!player->spectator)
					S_StartSound(player->mo, sfx_spin);
				player->pflags |= PF_USEDOWN;
				if (player->mo->eflags & MFE_VERTICALFLIP)
					player->mo->z = player->mo->ceilingz - P_GetPlayerSpinHeight(player);

				player->usedspin = true;
				player->unspinready = false;
			}

			if (!onground || !(player->pflags & PF_SPINNING))
				player->usedspin = false;


			if (player->usedspin && !(cmd->buttons & BT_USE) && (player->pflags & PF_SPINNING))
				player->unspinready = true;

			if (!onground || !(player->pflags & PF_SPINNING))
				player->unspinready = false;


			// If spin button is not down from the first spin
			if ((cmd->buttons & BT_USE) && player->unspinready && player->usedspin && (player->pflags & PF_SPINNING))
			{
				player->pflags &= ~PF_SPINNING;
				P_SetPlayerMobjState(player->mo, S_PLAY_STND);
				player->unspinready = false;
				player->usedspin = false;
			}

		}
	}

	if (onground && (player->pflags & PF_SPINNING) && !(player->pflags & PF_STARTDASH)
		&& (player->rmomx < FIXEDSCALE(5*FRACUNIT/(float)NEWTICRATERATIO, player->mo->scale)
			&& player->rmomx > -FIXEDSCALE(5*FRACUNIT/(float)NEWTICRATERATIO, player->mo->scale))
		&& (player->rmomy < FIXEDSCALE(5*FRACUNIT/(float)NEWTICRATERATIO, player->mo->scale)
			&& player->rmomy > -FIXEDSCALE(5*FRACUNIT/(float)NEWTICRATERATIO, player->mo->scale)))
	{
		if (GETSECSPECIAL(player->mo->subsector->sector->special, 4) == 7 || (player->mo->ceilingz - player->mo->floorz < P_GetPlayerHeight(player)))
			P_InstaThrust(player->mo, player->mo->angle, 10*FRACUNIT); // SRB2CBCHECK:
		else
		{
			player->pflags &= ~PF_SPINNING;
			P_SetPlayerMobjState(player->mo, S_PLAY_STND);
			player->mo->momx = player->cmomx;
			player->mo->momy = player->cmomy;
			P_ResetScore(player);
			if (player->mo->eflags & MFE_VERTICALFLIP)
				player->mo->z = player->mo->ceilingz - P_GetPlayerHeight(player);
		}
	}

	// Catapult the player from a spindash rev!
	if (onground && !(player->pflags & PF_USEDOWN) && player->dashspeed && (player->pflags & PF_STARTDASH) && (player->pflags & PF_SPINNING))
	{
		if (player->powers[pw_ingoop])
			player->dashspeed = 0;

		player->pflags &= ~PF_STARTDASH;
		if (!(gametype == GT_RACE && leveltime < 4*TICRATE) && !player->usedspin)
		{
			P_InstaThrust(player->mo, player->mo->angle, player->dashspeed); // catapult forward ho!!
			if (!player->spectator)
				S_StartSound(player->mo, sfx_zoom);

			player->usedspin = true;
			player->unspinready = false;
		}

		if (player->usedspin && !(cmd->buttons & BT_USE) && (player->pflags & PF_SPINNING) && onground && (player->pflags & PF_STARTDASH))
			player->unspinready = true;

		if (!onground || !(player->pflags & PF_SPINNING))
			player->unspinready = false;

#if 0
		// If spin button is not down from the first spin
		if ((cmd->buttons & BT_USE) && player->unspinready && player->usedspin && onground)
		{
			player->pflags &= ~PF_SPINNING;
			P_SetPlayerMobjState(player->mo, S_PLAY_STND);
			player->unspinready = false;
			player->usedspin = false;
		}
#endif

		player->dashspeed = 0;
	}

	if (onground && (player->pflags & PF_SPINNING)
		&& !(player->mo->state >= &states[S_PLAY_ATK1]
			 && player->mo->state <= &states[S_PLAY_ATK4]))
		P_SetPlayerMobjState(player->mo, S_PLAY_ATK1);
}


#endif

//
// P_DoJumpShield
//
// Jump Shield Activation
//
static void P_DoJumpShield(player_t *player)
{
	// If the player is falling, don't reactivate the shield
	if (player->mo->state >= &states[S_PLAY_FALL1]
		&& player->mo->state <= &states[S_PLAY_FALL2])
		return;

	// When tails is tierd from flying, don't allow jumping again
	if (player->charability == CA_FLY
		&& player->mo->state >= &states[S_PLAY_SPC1]
		&& player->mo->state <= &states[S_PLAY_SPC4])
		return;

	// If the player is hanging on to something, don't let him jump
	if ((player->pflags & PF_CARRIED) || (player->pflags & PF_ITEMHANG) || (player->pflags & PF_MACESPIN)
		|| (player->pflags & PF_ROPEHANG) || (player->pflags & PF_MINECART))
		return;

	player->pflags &= ~PF_JUMPED;
#ifdef SRB2K
	if (player->powers[pw_lightningshield])
	{
		player->pflags |= PF_JUMPED;
		if (player->mo->momz < 0)
			P_SetObjectMomZ(player->mo, 10*FRACUNIT, false, false);
		else
			P_SetObjectMomZ(player->mo, 10*FRACUNIT, true, false);
	}
	else
#endif
		P_DoJump(player, false);

#ifdef SRB2K
	// Allow attacking enemies on jumping with a lighting shield
	if (!player->powers[pw_lightningshield])
#endif
		player->pflags &= ~PF_JUMPED;
	player->secondjump = false;
	player->jumping = false;
	player->pflags |= PF_THOKKED;
	player->pflags &= ~PF_SPINNING;
	if (!player->powers[pw_lightningshield])
		P_SetPlayerMobjState(player->mo, S_PLAY_FALL1);
#ifdef SRB2K
	if (player->powers[pw_lightningshield])
	{
		S_StartSound(player->mo, sfx_shield);
		// Pretty particles just like Sonic 3 &K!
		P_Particles(player->mo, MT_LIGHTPARTICLE, 255, 8*FRACUNIT, 15*FRACUNIT, TICRATE+1, true, false, 13, 1, 0);
		P_Particles(player->mo, MT_LIGHTPARTICLE, 255, 15*FRACUNIT, 13*FRACUNIT, TICRATE+1, true, false, 13, 1, 0);
		P_Particles(player->mo, MT_LIGHTPARTICLE, 255, 14*FRACUNIT, 19*FRACUNIT, TICRATE+1, true, false, 13, 1, 0);
		P_Particles(player->mo, MT_LIGHTPARTICLE, 255, 13*FRACUNIT, 1*FRACUNIT, TICRATE+1, true, false, 13, 1, 0);
		P_Particles(player->mo, MT_LIGHTPARTICLE, 255, 18*FRACUNIT, 10*FRACUNIT, TICRATE+1, true, false, 13, 1, 0);
		P_Particles(player->mo, MT_LIGHTPARTICLE, 255, 15*FRACUNIT, 7*FRACUNIT, TICRATE+1, true, false, 13, 1, 0);
	}
	else
#endif
		S_StartSound(player->mo, sfx_wdjump);
}

//
// P_DoJumpStuff
//
// Handles player jumping
//
static void P_DoJumpStuff(player_t *player, ticcmd_t *cmd)
{
	if (player->pflags & PF_STASIS || (player->powers[pw_nocontrol] && !(player->powers[pw_nocontrol] & FRACUNIT)))
		return;

	if (cmd->buttons & BT_JUMP && !(player->pflags & PF_JUMPDOWN) && !player->exiting
		&& !(!(player->pflags & PF_SLIDING) && player->mo->state == &states[player->mo->info->painstate] && player->powers[pw_flashing]))
	{
		// When you're on a vehicle that you can jump with like a skateboard
		// Your "onground" check must be checked by the vehicle being onground, if
		// you are standing on the object
		if ((player->pflags & PF_MINECART) && (player->mo->tracer && player->mo->tracer->type == MT_SKATEBOARD))
		{
			if (P_IsObjectOnGround(player->mo->tracer))
			{
				P_DoJump(player, true);
				player->secondjump = false;
			}
		}
		// Normally, you can jump when on the ground, hanging on something
		else if (onground || player->climbing
				 || (player->pflags & PF_CARRIED)
				 || (player->pflags & PF_ITEMHANG) || (player->pflags & PF_ROPEHANG) || (player->pflags & PF_MINECART))
		{
			P_DoJump(player, true);
			player->secondjump = false;
		}
		else if ((player->pflags & PF_MACESPIN) && player->mo->tracer)
		{
			player->pflags &= ~PF_MACESPIN;
			player->powers[pw_flashing] = TICRATE/2;
		}
		else if (!(player->pflags & PF_SLIDING) && ((gametype != GT_CTF) || (!player->gotflag)))
		{
			switch (player->charability)
			{
				case CA_THOK:
				case CA_HOMINGTHOK:
					// Now it's Sonic's abilities turn!
					if (player->pflags & PF_JUMPED)
					{
						// If you can turn super and aren't already,
						// and you don't have a shield, do it!
						if ((player->pflags & PF_SUPERREADY) && !player->powers[pw_super]
							&& !player->powers[pw_jumpshield] && !player->powers[pw_forceshield]
							&& !player->powers[pw_watershield] && !player->powers[pw_ringshield]
							&& !player->powers[pw_bombshield] && !player->powers[pw_invulnerability]
#ifdef SRB2K
							&& !player->powers[pw_bubbleshield] && !player->powers[pw_lightningshield] && !player->powers[pw_flameshield]
#endif
							&& !(maptol & TOL_NIGHTS) // don't turn 'regular super' in nights levels
							&& ((player->skin == 0) || ALL7EMERALDS(player->powers[pw_emeralds])))
						{
							P_DoSuperTransformation(player, false);
						}
						else // Otherwise, THOK!
						{
							if (!(player->pflags & PF_THOKKED) || (player->charability2 == CA2_MULTIABILITY))
							{
								mobj_t *item;
								// Catapult the player
								if ((player->mo->eflags & MFE_UNDERWATER))
									P_InstaThrust(player->mo, player->mo->angle, (player->actionspd<<FRACBITS)/2);
								else
									P_InstaThrust(player->mo, player->mo->angle, player->actionspd<<FRACBITS);

								if (player->mo->flags2 & MF2_TWOD || twodlevel)
								{
									player->mo->momx /= 1.5;
									player->mo->momy /= 1.5;
								}
								else if (player->charability == CA_HOMINGTHOK)
								{
									player->mo->momx /= 3;
									player->mo->momy /= 3;
								}

								if (player->mo->info->attacksound && !player->spectator)
									S_StartSound(player->mo, player->mo->info->attacksound); // Play the THOK sound

								item = P_SpawnThokMobj(player);

								if (item && (player->charflags & SF_GHOSTTHOKITEM))
								{
									P_SpawnGhostMobj(item);
									P_SetMobjState(item, S_DISS);
								}

								if ((player->charability == CA_HOMINGTHOK) && !player->homing && (player->pflags & PF_JUMPED))
								{
									if (P_LookForEnemies(player))
									{
										if (player->mo->tracer)
											player->homing = 3*TICRATE;
									}
								}

								player->pflags &= ~PF_SPINNING;
								player->pflags &= ~PF_STARTDASH;
								player->pflags |= PF_THOKKED;
							}
						}
					}
					else if (player->powers[pw_jumpshield] && !player->powers[pw_super])
						P_DoJumpShield(player);
					break;

				case CA_FLY:
				case CA_SWIM: // Swim
					// If you can turn super and aren't already,
					// and you don't have a shield, do it!
					if ((player->pflags & PF_SUPERREADY) && !player->powers[pw_super] && !player->powers[pw_tailsfly]
						&& !player->powers[pw_jumpshield] && !player->powers[pw_forceshield]
						&& !player->powers[pw_watershield] && !player->powers[pw_ringshield]
						&& !player->powers[pw_bombshield] && !player->powers[pw_invulnerability]
#ifdef SRB2K
						&& !player->powers[pw_bubbleshield] && !player->powers[pw_lightningshield] && !player->powers[pw_flameshield]
#endif
						&& !(maptol & TOL_NIGHTS) // don't turn 'regular super' in nights levels
						&& player->pflags & PF_JUMPED
						&& ((player->skin == 0) || ALL7EMERALDS(player->powers[pw_emeralds])))
					{
						P_DoSuperTransformation(player, false);
					}
					// If currently in the air from a jump, and you pressed the
					// button again and have the ability to fly, fly!
					else if (!(player->pflags & PF_THOKKED) && !(player->powers[pw_tailsfly])
							 && (player->pflags & PF_JUMPED) && !(player->charability == CA_SWIM && !(player->mo->eflags & MFE_UNDERWATER)))
					{
						P_SetPlayerMobjState(player->mo, S_PLAY_ABL1); // Change to the flying animation

						player->powers[pw_tailsfly] = tailsflytics + 1; // Set the fly timer

						player->pflags &= ~PF_JUMPED;
						player->pflags &= ~PF_SPINNING;
						player->pflags &= ~PF_STARTDASH;
						player->pflags |= PF_THOKKED;
					}
#ifdef UNSONIC // For abilities/features that are general game stuff, not really sonic stuff
					// Jump when close to the top of the water while swimming or flying
					else if (player->powers[pw_tailsfly] && (player->mo->watertop != player->mo->waterbottom
															 && player->mo->z >= player->mo->watertop - player->mo->height - player->mo->height))
					{
						P_DoJump(player, true);
						player->powers[pw_tailsfly] = 0;
						player->pflags &= ~PF_THOKKED;
					}
#endif
					// If currently flying, give an ascend boost.
					else if (player->powers[pw_tailsfly] && !(player->charability == CA_SWIM && !(player->mo->eflags & MFE_UNDERWATER)))
					{
						if (!player->fly1)
							player->fly1 = 20;
						else
							player->fly1 = 2;

						if (player->charability == CA_SWIM)
							player->fly1 /= 2;
					}
					// If currently flying, give an ascend boost.
					else if (player->powers[pw_jumpshield] && !player->powers[pw_super])
						P_DoJumpShield(player);

					break;

				case CA_GLIDEANDCLIMB:
					// Now Knuckles-type abilities are checked.
					// If you can turn super and aren't already,
					// and you don't have a shield, do it!
					if ((player->pflags & PF_SUPERREADY) && !player->powers[pw_super]
						&& !player->powers[pw_jumpshield] && !player->powers[pw_forceshield]
						&& !player->powers[pw_watershield] && !player->powers[pw_ringshield]
						&& !player->powers[pw_bombshield] && !player->powers[pw_invulnerability]
#ifdef SRB2K
						&& !player->powers[pw_bubbleshield] && !player->powers[pw_lightningshield] && !player->powers[pw_flameshield]
#endif
						&& !(maptol & TOL_NIGHTS) // don't turn 'regular super' in nights levels
						&& player->pflags & PF_JUMPED
						&& ((player->skin == 0) || ALL7EMERALDS(player->powers[pw_emeralds])))
					{
						P_DoSuperTransformation(player, false);
					}
					else if ((player->pflags & PF_JUMPED) && (!(player->pflags & PF_THOKKED) || player->charability2 == CA2_MULTIABILITY))
					{
						player->pflags |= PF_GLIDING;
						player->pflags |= PF_THOKKED;
						player->glidetime = 0;

						// Gain the ability to glide multiple times when super
						if (player->powers[pw_super] && ALL7EMERALDS(player->powers[pw_emeralds]))
							player->pflags &= ~PF_THOKKED;

						P_SetPlayerMobjState(player->mo, S_PLAY_ABL1);
						P_InstaThrust(player->mo, player->mo->angle, (player->actionspd<<FRACBITS));
						player->pflags &= ~PF_SPINNING;
						player->pflags &= ~PF_STARTDASH;
					}
					else if ((player->powers[pw_jumpshield]
#ifdef SRB2K
							  || player->powers[pw_lightningshield]
#endif
							 ) && !player->powers[pw_super])
						P_DoJumpShield(player);
					break;
				case CA_DOUBLEJUMP: // Double-Jump
					if ((player->pflags & PF_SUPERREADY) && !player->powers[pw_super]
						&& !player->powers[pw_jumpshield] && !player->powers[pw_forceshield]
						&& !player->powers[pw_watershield] && !player->powers[pw_ringshield]
						&& !player->powers[pw_bombshield] && !player->powers[pw_invulnerability]
#ifdef SRB2K
						&& !player->powers[pw_bubbleshield] && !player->powers[pw_lightningshield] && !player->powers[pw_flameshield]
#endif
						&& !(maptol & TOL_NIGHTS) // don't turn 'regular super' in nights levels
						&& player->pflags & PF_JUMPED
						&& ((player->skin == 0) || ALL7EMERALDS(player->powers[pw_emeralds])))
					{
						P_DoSuperTransformation(player, false);
					}
					else if ((player->pflags & PF_JUMPED) && !player->secondjump)
					{
						player->pflags &= ~PF_JUMPED;
						P_DoJump(player, true);

						// Allow infinite double jumping if super.
						if (!(player->powers[pw_super]/* && ALL7EMERALDS(player->powers[pw_emeralds])*/))
							player->secondjump = true;
					}
					else if (player->powers[pw_jumpshield] && !player->powers[pw_super])
						P_DoJumpShield(player);
					break;
				case CA_FLOAT: // Float
				case CA_SLOWFALL: // Slow descent hover
					if ((player->pflags & PF_SUPERREADY) && !player->powers[pw_super]
						&& !player->powers[pw_jumpshield] && !player->powers[pw_forceshield]
						&& !player->powers[pw_watershield] && !player->powers[pw_ringshield]
						&& !player->powers[pw_bombshield] && !player->powers[pw_invulnerability]
#ifdef SRB2K
						&& !player->powers[pw_bubbleshield] && !player->powers[pw_lightningshield] && !player->powers[pw_flameshield]
#endif
						&& !(maptol & TOL_NIGHTS) // don't turn 'regular super' in nights levels
						&& player->pflags & PF_JUMPED
						&& ((player->skin == 0) || ALL7EMERALDS(player->powers[pw_emeralds])))
					{
						P_DoSuperTransformation(player, false);
					}
					else if ((player->pflags & PF_JUMPED) && !player->secondjump)
					{
						player->secondjump = true;
					}
					else if (player->powers[pw_jumpshield] && !player->powers[pw_super])
						P_DoJumpShield(player);
					break;
				default:
					break;
			}
		}
	}
	player->pflags |= PF_JUMPDOWN;

	if (!(cmd->buttons & BT_JUMP)) // If not pressing the jump button
	{
		player->pflags &= ~PF_JUMPDOWN;

		// Repeat abilities, but not double jump!
		if ((player->charability2 == CA2_MULTIABILITY && player->charability != CA_DOUBLEJUMP)
			|| (player->powers[pw_super] && ALL7EMERALDS(player->powers[pw_emeralds])))
			player->secondjump = false;
		else if ((player->charability == CA_FLOAT) && player->secondjump == 1)
			player->secondjump = 2;
	}

	if ((gametype != GT_CTF) || (!player->gotflag))
	{
		if (player->secondjump == 1 && (cmd->buttons & BT_JUMP))
		{
			if (player->charability == CA_FLOAT)
				player->mo->momz = 0;
			else if (player->charability == CA_SLOWFALL)
			{
				if (!(player->powers[pw_super] /*&& ALL7EMERALDS(player->powers[pw_emeralds])*/) && player->mo->momz < -gravity*4)
					player->mo->momz = -gravity*4;
				else if ((player->powers[pw_super] /*&& ALL7EMERALDS(player->powers[pw_emeralds])*/) && player->mo->momz < gravity*16)
					player->mo->momz = gravity*16; //Float upward 4x as fast while super.
			}

			player->pflags &= ~PF_SPINNING;
		}
	}

	// If letting go of the jump button while still on ascent, cut the jump height. // SRB2CBTODO: Revamp for uncapped FPS?
	if (!(player->pflags & PF_JUMPDOWN) && (player->pflags & PF_JUMPED)
		&& ((player->mo->eflags & MFE_VERTICALFLIP && player->mo->momz < 0)
			|| (!(player->mo->eflags & MFE_VERTICALFLIP) && player->mo->momz > 0)) && player->jumping)
	{
		player->mo->momz >>= 1;
		player->jumping = false;
	}
}


//
// P_GetPlayerControlDirection
//
// Determines if the player is pressing in the direction they are moving
//
// 0 = no controls pressed
// 1 = pressing in the direction of movement
// 2 = pressing in the opposite direction of movement
//
int P_GetPlayerControlDirection(player_t *player)
{
	ticcmd_t *cmd = &player->cmd;
	angle_t controldirection, controllerdirection, controlplayerdirection;
	camera_t *thiscam;

	if (splitscreen && player == &players[secondarydisplayplayer])
		thiscam = &camera2;
	else
		thiscam = &camera;

	if (!netgame && ((player == &players[consoleplayer] && cv_analog.value)
		|| (splitscreen && player == &players[secondarydisplayplayer]
		&& cv_analog2.value)) && thiscam->chase)
	{
		fixed_t tempx, tempy;
		angle_t tempangle;

		tempx = tempy = 0;

		// Calculate the angle at which the controls are pointing
		// to figure out the proper mforward and mbackward.
		tempangle = thiscam->angle;
		tempangle >>= ANGLETOFINESHIFT;
		tempx += FixedMul(cmd->forwardmove,FINECOSINE(tempangle));
		tempy += FixedMul(cmd->forwardmove,FINESINE(tempangle));

		tempangle = thiscam->angle-ANG90;
		tempangle >>= ANGLETOFINESHIFT;
		tempx += FixedMul(cmd->sidemove,FINECOSINE(tempangle));
		tempy += FixedMul(cmd->sidemove,FINESINE(tempangle));

		tempx = tempx*FRACUNIT;
		tempy = tempy*FRACUNIT;

		controldirection = controllerdirection =
			R_PointToAngle2(player->mo->x, player->mo->y, player->mo->x + tempx,
				player->mo->y + tempy);

		controlplayerdirection = player->mo->angle;

		if (controlplayerdirection < ANG90)
		{
			controlplayerdirection += ANG90;
			controllerdirection += ANG90;
		}
		else if (controlplayerdirection >= ANG270) // SRB2CBTODO: controlplayerdirection >= ?
		{
			controlplayerdirection -= ANG90;
			controllerdirection -= ANG90;
		}

		// Controls pointing backwards from player
		if (controllerdirection > controlplayerdirection + ANG90
			&& controllerdirection < controlplayerdirection - ANG90)
		{
			return 2;
		}
		else // Controls pointing in player's general direction
			return 1;
	}
	else
	{
		if (!cmd->forwardmove)
			return 0;
	}

	controldirection = controllerdirection =
		R_PointToAngle2(player->mo->x, player->mo->y, P_ReturnThrustX(player->mo, player->mo->angle, cmd->forwardmove),
			P_ReturnThrustY(player->mo, player->mo->angle, cmd->forwardmove));

	controlplayerdirection = R_PointToAngle2(0, 0, player->mo->momx,
			player->mo->momy);

	if (controlplayerdirection < ANG90)
	{
		controlplayerdirection += ANG90;
		controllerdirection += ANG90;
	}
	else if (controlplayerdirection >= ANG270)
	{
		controlplayerdirection -= ANG90;
		controllerdirection -= ANG90;
	}

	// Controls pointing backwards from player
	if (controllerdirection > controlplayerdirection + ANG90
		&& controllerdirection < controlplayerdirection - ANG90)
	{
		return 2;
	}
	else // Controls pointing in player's general direction
		return 1;
}

// Control scheme for 2d levels.
static void P_2dMovement(player_t *player)
{
	ticcmd_t *cmd;
	int topspeed, acceleration, thrustfactor;
	fixed_t movepushforward = 0;
	angle_t movepushangle = 0;
	fixed_t normalspd = player->normalspeed;

	cmd = &player->cmd;

	if (player->exiting
		|| (player->pflags & PF_STASIS)
		|| (player->powers[pw_nocontrol]) || (player->powers[pw_ingoop]))
	{
		cmd->forwardmove = cmd->sidemove = 0;
		if (player->pflags & PF_GLIDING)
			player->pflags &= ~PF_GLIDING;
	}

	if (player->pflags & PF_SLIDING)
		cmd->forwardmove = 0;

	// cmomx/cmomy stands for the conveyor belt speed.
	if (player->onconveyor == 2) // Wind/Current
	{
		//if (player->mo->z > player->mo->watertop || player->mo->z + player->mo->height < player->mo->waterbottom)
		if (!(player->mo->eflags & MFE_UNDERWATER) && !(player->mo->eflags & MFE_TOUCHWATER))
			player->cmomx = player->cmomy = 0;
	}
	else if (player->onconveyor == 4 && !P_IsObjectOnGround(player->mo)) // Actual conveyor belt
		player->cmomx = player->cmomy = 0;
	else if (player->onconveyor != 2 && player->onconveyor != 4)
		player->cmomx = player->cmomy = 0;

	player->rmomx = player->mo->momx - player->cmomx;
	player->rmomy = player->mo->momy - player->cmomy;

	// Calculates player's speed based on distance-of-a-line formula
	player->speed = abs(player->rmomx)>>FRACBITS;

	if (player->pflags & PF_GLIDING)
	{
		// Angle fix.
		if (player->mo->angle < ANG180 && player->mo->angle > ANG90)
			player->mo->angle = ANG180;
		else if (player->mo->angle < ANG90 && player->mo->angle > 0)
			player->mo->angle = 0;

		if (cmd->sidemove > 0 && player->mo->angle != 0 && player->mo->angle >= ANG180)
			player->mo->angle += (640/NEWTICRATERATIO)<<FRACBITS;
		else if (cmd->sidemove < 0 && player->mo->angle != ANG180 && (player->mo->angle > ANG180 || player->mo->angle == 0))
			player->mo->angle -= (640/NEWTICRATERATIO)<<FRACBITS;
		else if (cmd->sidemove == 0)
		{
			if (player->mo->angle >= ANG270)
				player->mo->angle += (640/NEWTICRATERATIO)<<FRACBITS;
			else if (player->mo->angle < ANG270 && player->mo->angle > ANG180)
				player->mo->angle -= (640/NEWTICRATERATIO)<<FRACBITS;
		}
	}
	else if (cmd->sidemove && !(player->climbing) && !(!(player->pflags & PF_SLIDING)
			&& player->mo->state == &states[player->mo->info->painstate] && player->powers[pw_flashing]))
	{
		if (cmd->sidemove > 0)
			player->mo->angle = 0;
		else if (cmd->sidemove < 0)
			player->mo->angle = ANG180;
	}

	if (player == &players[consoleplayer])
		localangle = player->mo->angle;
	else if (splitscreen && player == &players[secondarydisplayplayer])
		localangle2 = player->mo->angle;

	if (player->pflags & PF_GLIDING)
		movepushangle = player->mo->angle;
	else
	{
		if (cmd->sidemove > 0)
			movepushangle = 0;
		else if (cmd->sidemove < 0)
			movepushangle = ANG180;
		else
			movepushangle = player->mo->angle;
	}

	// Do not let the player control movement if not onground.
	onground = P_IsObjectOnGround(player->mo);

	player->aiming = cmd->aiming<<FRACBITS;

	// Set the player speeds.
	//normalspd = (normalspd * 0.73f); // (normalspd * 0.73f);

	if (player->powers[pw_super] || player->powers[pw_sneakers])
	{
		thrustfactor = player->thrustfactor*2;
		acceleration = player->accelstart/4 + player->speed*(player->acceleration/4);

		if (player->powers[pw_tailsfly])
			topspeed = normalspd;
		else if (player->mo->eflags & MFE_UNDERWATER && !(player->pflags & PF_SLIDING))
		{
			topspeed = normalspd;
			acceleration = (acceleration * 2) / 3;
		}
		else
			topspeed = normalspd * 2 > 50 ? 50 : normalspd * 2;
	}
	else
	{
		thrustfactor = player->thrustfactor;
		acceleration = player->accelstart + player->speed*player->acceleration;

		if (player->powers[pw_tailsfly])
		{
			topspeed = normalspd/2;
		}
		else if (player->mo->eflags & MFE_UNDERWATER && !(player->pflags & PF_SLIDING))
		{
			topspeed = normalspd/2;
			acceleration = (acceleration * 2) / 3;
		}
		else
		{
			topspeed = normalspd;
		}
	}

//////////////////////////////////////
	if (player->climbing == 1)
	{
		P_SetObjectMomZ(player->mo, FixedDiv(cmd->forwardmove*FRACUNIT,10*FRACUNIT), false, false);

		if (player->powers[pw_super] && ALL7EMERALDS(player->powers[pw_emeralds]))
			player->mo->momz *= 2;

		player->mo->momx = 0;
	}

	if (cmd->sidemove != 0 && !(player->climbing || (player->pflags & PF_GLIDING) || player->exiting
		|| (!(player->pflags & PF_SLIDING) && player->mo->state == &states[player->mo->info->painstate] && player->powers[pw_flashing]
		&& !onground)))
	{
		if (player->powers[pw_sneakers] || player->powers[pw_super]) // do you have super sneakers?
			movepushforward = abs(cmd->sidemove) * ((thrustfactor*2)*acceleration);
		else // if not, then run normally
			movepushforward = abs(cmd->sidemove) * (thrustfactor*acceleration);

		// allow very small movement while in air for gameplay
		if (!onground)
		{
			movepushforward /= 1.1;
			movepushforward >>= 1; // Proper air movement
			if (movepushforward > 40000)
				movepushforward = 40000;
		}

		// Allow a bit of movement while spinning
		if (player->pflags & PF_SPINNING)
		{
			if (!(player->pflags & PF_STARTDASH))
				movepushforward = movepushforward/48;
			else
				movepushforward = 0;
		}

        if (player->powers[pw_nocontrol]) // Make sure there is no movement at all
			movepushforward = 0; // SRB2CBTODO: Correct?

		if (((player->rmomx>>FRACBITS) < topspeed) && (cmd->sidemove > 0)) // Sonic's Speed
			P_Thrust(player->mo, movepushangle, movepushforward);
		else if (((player->rmomx>>FRACBITS) > -topspeed) && (cmd->sidemove < 0))
			P_Thrust(player->mo, movepushangle, movepushforward);
	}
}

// SRB2CBTODO: P_3dMovement: Seriously clean up the if statements, and make it switch off of analog temp when needed
static void P_3dMovement(player_t *player)
{
	ticcmd_t *cmd;
	angle_t	movepushangle, movepushsideangle; // Analog
	int topspeed, acceleration, thrustfactor;
	fixed_t movepushforward = 0, movepushside = 0;
	int mforward = 0, mbackward = 0;
	camera_t *thiscam;
	fixed_t normalspd = player->normalspeed;

	if (splitscreen && player == &players[secondarydisplayplayer])
		thiscam = &camera2;
	else
		thiscam = &camera;

	cmd = &player->cmd;

	if (player->exiting
		|| (player->pflags & PF_STASIS)
		|| (player->powers[pw_nocontrol]) || (player->powers[pw_ingoop]))
	{
		cmd->forwardmove = cmd->sidemove = 0;
		if (player->pflags & PF_GLIDING)
			player->pflags &= ~PF_GLIDING;
	}

	if (!netgame && ((player == &players[consoleplayer] && cv_analog.value) || (splitscreen && player == &players[secondarydisplayplayer] && cv_analog2.value)))
	{
		movepushangle = thiscam->angle;
		movepushsideangle = thiscam->angle-ANG90;
	}
	else
	{
		movepushangle = player->mo->angle;
		movepushsideangle = player->mo->angle-ANG90;
	}

	// cmomx/cmomy stands for the conveyor belt speed.
	if (player->onconveyor == 2) // Wind/Current
	{
		//if (player->mo->z > player->mo->watertop || player->mo->z + player->mo->height < player->mo->waterbottom)
		if (!(player->mo->eflags & MFE_UNDERWATER) && !(player->mo->eflags & MFE_TOUCHWATER))
			player->cmomx = player->cmomy = 0;
	}
	else if (player->onconveyor == 4 && !P_IsObjectOnGround(player->mo)) // Actual conveyor belt
		player->cmomx = player->cmomy = 0;
	else if (player->onconveyor != 2 && player->onconveyor != 4)
		player->cmomx = player->cmomy = 0;

	player->rmomx = player->mo->momx - player->cmomx;
	player->rmomy = player->mo->momy - player->cmomy;

	// Calculates player's speed based on distance-of-a-line formula
	player->speed = P_AproxDistance(player->rmomx, player->rmomy)>>FRACBITS;

	// This determines if the player is facing the direction they are travelling or not.
	// Didn't your teacher say to pay attention in Geometry/Trigonometry class? ;)
	// forward
	if ((player->rmomx > 0 && player->rmomy > 0) && (/*player->mo->angle >= 0 &&*/ player->mo->angle < ANG90)) // Quadrant 1
		mforward = 1;
	else if ((player->rmomx < 0 && player->rmomy > 0) && (player->mo->angle >= ANG90 && player->mo->angle < ANG180)) // Quadrant 2
		mforward = 1;
	else if ((player->rmomx < 0 && player->rmomy < 0) && (player->mo->angle >= ANG180 && player->mo->angle < ANG270)) // Quadrant 3
		mforward = 1;
	else if ((player->rmomx > 0 && player->rmomy < 0) && ((player->mo->angle >= ANG270 /*&& (player->mo->angle <= ANGLE_MAX)*/)
														  || (/*player->mo->angle >= 0 &&*/ player->mo->angle <= ANG45))) // Quadrant 4
		mforward = 1;
	else if (player->rmomx > 0 && ((player->mo->angle >= ANG270+ANG45 /*&& player->mo->angle <= ANGLE_MAX*/)))
		mforward = 1;
	else if (player->rmomx < 0 && (player->mo->angle >= ANG90+ANG45 && player->mo->angle <= ANG180+ANG45))
		mforward = 1;
	else if (player->rmomy > 0 && (player->mo->angle >= ANG45 && player->mo->angle <= ANG90+ANG45))
		mforward = 1;
	else if (player->rmomy < 0 && (player->mo->angle >= ANG180+ANG45 && player->mo->angle <= ANG270+ANG45))
		mforward = 1;
	else
		mforward = 0;
	// backward
	if ((player->rmomx > 0 && player->rmomy > 0) && (player->mo->angle >= ANG180 && player->mo->angle < ANG270)) // Quadrant 3
		mbackward = 1;
	else if ((player->rmomx < 0 && player->rmomy > 0) && (player->mo->angle >= ANG270 /*&& (player->mo->angle <= ANGLE_MAX)*/)) // Quadrant 4
		mbackward = 1;
	else if ((player->rmomx < 0 && player->rmomy < 0) && (/*player->mo->angle >= 0 &&*/ player->mo->angle < ANG90)) // Quadrant 1
		mbackward = 1;
	else if ((player->rmomx > 0 && player->rmomy < 0) && (player->mo->angle >= ANG90 && player->mo->angle < ANG180)) // Quadrant 2
		mbackward = 1;
	else if (player->rmomx < 0 && ((player->mo->angle >= ANG270+ANG45 /*&& player->mo->angle <= ANGLE_MAX*/) || (/*player->mo->angle >= 0 &&*/ player->mo->angle <= ANG45)))
		mbackward = 1;
	else if (player->rmomx > 0 && (player->mo->angle >= ANG90+ANG45 && player->mo->angle <= ANG180+ANG45))
		mbackward = 1;
	else if (player->rmomy < 0 && (player->mo->angle >= ANG45 && player->mo->angle <= ANG90+ANG45))
		mbackward = 1;
	else if (player->rmomy > 0 && (player->mo->angle >= ANG180+ANG45 && player->mo->angle <= ANG270+ANG45))
		mbackward = 1;
	else // Put in 'or' checks here!
		mbackward = 0;

	// When sliding, don't allow little forward/back
	if (player->pflags & PF_SLIDING)
		cmd->forwardmove = 0;

	// Do not let the player control movement if not onground.
	onground = P_IsObjectOnGround(player->mo);

	player->aiming = cmd->aiming<<FRACBITS;

	if (player->powers[pw_super] || player->powers[pw_sneakers])
	{
		thrustfactor = player->thrustfactor*2;
		acceleration = player->accelstart/4 + player->speed*(player->acceleration/4);

		if (player->powers[pw_tailsfly])
			topspeed = normalspd;
		else if (player->mo->eflags & MFE_UNDERWATER && !(player->pflags & PF_SLIDING))
		{
			topspeed = normalspd;
			acceleration = (acceleration * 2) / 3;
		}
		else
			topspeed = normalspd * 2 > 50 ? 50 : normalspd * 2;
	}
	else
	{
		thrustfactor = player->thrustfactor;
		acceleration = player->accelstart + player->speed*player->acceleration;

		if (player->powers[pw_tailsfly])
			topspeed = normalspd/2;
		else if (player->mo->eflags & MFE_UNDERWATER && !(player->pflags & PF_SLIDING))
		{
			topspeed = normalspd/2;
			acceleration = (acceleration * 2) / 3;
		}
		else
			topspeed = normalspd;
	}

	// Better maneuverability while flying
	if (player->powers[pw_tailsfly])
	{
		thrustfactor = player->thrustfactor*2;
		acceleration = player->accelstart + player->speed*player->acceleration;
	}

//	thrustfactor /= (1.0f + NEWTICRATERATIO/10.0f); // SRB2CBTODO: Do a real way to do this

	if ((netgame
#ifdef JTEBOTS
		 || player->bot
#endif
		 || (player == &players[consoleplayer] && !cv_analog.value)
		|| (splitscreen && player == &players[secondarydisplayplayer] && !cv_analog2.value))
		&& cmd->forwardmove != 0 && !((player->pflags & PF_GLIDING) || player->exiting
		|| (!(player->pflags & PF_SLIDING) && player->mo->state == &states[player->mo->info->painstate] && player->powers[pw_flashing]
		&& !onground)))
	{
		if (player->climbing)
		{
			P_SetObjectMomZ(player->mo, FixedDiv(cmd->forwardmove*FRACUNIT, 10*FRACUNIT), false, false);

			if (player->powers[pw_super] && ALL7EMERALDS(player->powers[pw_emeralds]))
				player->mo->momz *= 2;
		}
		else if (player->powers[pw_sneakers] || player->powers[pw_super]) // super sneakers?
			movepushforward = cmd->forwardmove * ((thrustfactor*2)*acceleration);
		else // if not, then run normally
			movepushforward = cmd->forwardmove * (thrustfactor*acceleration);

		// allow very small movement while in air for gameplay
		if (!onground)
		{
		    if (player->mo->flags2 & MF2_FRET)
		    {
                movepushforward /= 1.5;
		    }
		    else
			movepushforward >>= 2; // proper air movement
		}

		// Allow a bit of movement while spinning
		if (player->pflags & PF_SPINNING)
		{
			if ((mforward && cmd->forwardmove > 0) || (mbackward && cmd->forwardmove < 0))
				movepushforward = 0;
			else if (!(player->pflags & PF_STARTDASH))
				movepushforward = FixedDiv(movepushforward, 16*FRACUNIT);
			else
				movepushforward = 0;
		}

		if ((player->speed < topspeed) && (mforward) && (cmd->forwardmove > 0)) // Sonic's Speed
			P_Thrust(player->mo, movepushangle, movepushforward);
		else if ((mforward) && (cmd->forwardmove < 0))
			P_Thrust(player->mo, movepushangle, movepushforward);
		else if ((player->speed < topspeed) && (mbackward) && (cmd->forwardmove < 0))
			P_Thrust(player->mo, movepushangle, movepushforward);
		else if ((mbackward) && (cmd->forwardmove > 0))
			P_Thrust(player->mo, movepushangle, movepushforward);
		else if (!mforward && !mbackward)
			P_Thrust(player->mo, movepushangle, movepushforward);





	}
	// Analog movement control // SRB2CBTODO: Netgame support for analog! Why is it unsupported anyway?
	if (!netgame
#ifdef JTEBOTS
		&& !player->bot // Bots cannot analog. :(
#endif
		&& ((player == &players[consoleplayer] && cv_analog.value)
		|| (splitscreen && player == &players[secondarydisplayplayer]
		&& cv_analog2.value)) && thiscam->chase)
	{
		if (!(player->exiting || (!(player->pflags & PF_SLIDING) && player->mo->state == &states[player->mo->info->painstate]
			&& player->powers[pw_flashing])))
		{
			angle_t controldirection, controllerdirection, controlplayerdirection;
			fixed_t tempx, tempy;
			angle_t tempangle;
			boolean cforward; // controls pointing forward from the player
			boolean cbackward; // controls pointing backward from the player

			tempx = tempy = 0;
			cforward = cbackward = false;

			// Calculate the angle at which the controls are pointing
			// to figure out the proper mforward and mbackward.
			tempangle = thiscam->angle;
			tempangle >>= ANGLETOFINESHIFT;
			tempx += FixedMul(cmd->forwardmove,FINECOSINE(tempangle));
			tempy += FixedMul(cmd->forwardmove,FINESINE(tempangle));

			tempangle = thiscam->angle-ANG90;
			tempangle >>= ANGLETOFINESHIFT;
			tempx += FixedMul(cmd->sidemove,FINECOSINE(tempangle));
			tempy += FixedMul(cmd->sidemove,FINESINE(tempangle));

			tempx = tempx*FRACUNIT;
			tempy = tempy*FRACUNIT;

			controldirection = controllerdirection =
				R_PointToAngle2(player->mo->x, player->mo->y, player->mo->x + tempx,
					player->mo->y + tempy);

			controlplayerdirection = player->mo->angle;

			if (controlplayerdirection < ANG90)
			{
				controlplayerdirection += ANG90;
				controllerdirection += ANG90;
			}
			else if (controlplayerdirection >= ANG270)
			{
				controlplayerdirection -= ANG90;
				controllerdirection -= ANG90;
			}

			// Controls pointing backwards from player
			if (controllerdirection > controlplayerdirection + ANG90
				&& controllerdirection < controlplayerdirection - ANG90)
			{
				cbackward = true;
			}
			else // Controls pointing in player's general direction
				cforward = true;

			if (player->climbing)
			{
				fixed_t value = 10*FRACUNIT;

				// Thrust in the direction of the controls
				P_SetObjectMomZ(player->mo, FixedDiv(cmd->forwardmove*FRACUNIT,10*FRACUNIT), false, false);

				if (player->powers[pw_super] && ALL7EMERALDS(player->powers[pw_emeralds]))
				{
					player->mo->momz *= 2;
					value /= 2;
				}

				P_InstaThrust(player->mo, player->mo->angle-ANG90, FixedDiv(cmd->sidemove*FRACUNIT,value));
			}

			else if (player->powers[pw_sneakers] || player->powers[pw_super]) // super sneakers?
				movepushforward = (fixed_t)((float)sqrt((float)(cmd->sidemove*cmd->sidemove + cmd->forwardmove*cmd->forwardmove)) * ((thrustfactor*2)*acceleration));
			else // if not, then run normally
				movepushforward = (fixed_t)((float)sqrt((float)(cmd->sidemove*cmd->sidemove + cmd->forwardmove*cmd->forwardmove)) * (thrustfactor*acceleration));

			// allow very small movement while in air for gameplay
			if (!onground)
				movepushforward >>= 2; // proper air movement

			// Allow a bit of movement while spinning
			if (player->pflags & PF_SPINNING)
			{
				// Stupid little movement prohibitor hack
				// that REALLY shouldn't belong in analog code.
				if ((mforward && cmd->forwardmove > 0) || (mbackward && cmd->forwardmove < 0))
					movepushforward = 0;
				else if (!(player->pflags & PF_STARTDASH))
					movepushforward = FixedDiv(movepushforward, 16*FRACUNIT);
				else
					movepushforward = 0;
			}

			movepushsideangle = controldirection;

			if (player->speed < topspeed)
				P_Thrust(player->mo, controldirection, movepushforward);
			else if ((mforward) && (cbackward))
				P_Thrust(player->mo, controldirection, movepushforward);
			else if ((mbackward) && (cforward))
				P_Thrust(player->mo, controldirection, movepushforward);
		}
	}
	else if (netgame
#ifdef JTEBOTS
			 || player->bot
#endif
			 || (player == &players[consoleplayer] && !cv_analog.value)
		|| (splitscreen && player == &players[secondarydisplayplayer]
		&& !cv_analog2.value))
	{
		if (player->climbing)
		{
			if (player->powers[pw_super] && ALL7EMERALDS(player->powers[pw_emeralds]))
				P_InstaThrust(player->mo, player->mo->angle - ANG90, FIXEDSCALE((cmd->sidemove/5)*FRACUNIT, player->mo->scale));
			else
				P_InstaThrust(player->mo, player->mo->angle - ANG90, FIXEDSCALE((cmd->sidemove/10)*FRACUNIT, player->mo->scale));
		}

		else if (cmd->sidemove && !player->exiting && !player->climbing
				 && !(!(player->pflags & PF_SLIDING) && player->mo->state == &states[player->mo->info->painstate] && player->powers[pw_flashing]))
		{
			boolean mright;
			boolean mleft;
			angle_t sideangle;

			sideangle = player->mo->angle - ANG90;

			// forward
			if ((player->rmomx > 0 && player->rmomy > 0) && (/*sideangle >= 0 &&*/ sideangle < ANG90)) // Quadrant 1
				mright = 1;
			else if ((player->rmomx < 0 && player->rmomy > 0) && (sideangle >= ANG90 && sideangle < ANG180)) // Quadrant 2
				mright = 1;
			else if ((player->rmomx < 0 && player->rmomy < 0) && (sideangle >= ANG180 && sideangle < ANG270)) // Quadrant 3
				mright = 1;
			else if ((player->rmomx > 0 && player->rmomy < 0) && ((sideangle >= ANG270 /*&& (sideangle <= ANGLE_MAX)*/) || (/*sideangle >= 0 &&*/ sideangle <= ANG45))) // Quadrant 4
				mright = 1;
			else if (player->rmomx > 0 && ((sideangle >= ANG270+ANG45 /*&& sideangle <= ANGLE_MAX*/)))
				mright = 1;
			else if (player->rmomx < 0 && (sideangle >= ANG90+ANG45 && sideangle <= ANG180+ANG45))
				mright = 1;
			else if (player->rmomy > 0 && (sideangle >= ANG45 && sideangle <= ANG90+ANG45))
				mright = 1;
			else if (player->rmomy < 0 && (sideangle >= ANG180+ANG45 && sideangle <= ANG270+ANG45))
				mright = 1;
			else
				mright = 0;
			// backward
			if ((player->rmomx > 0 && player->rmomy > 0) && (sideangle >= ANG180 && sideangle < ANG270)) // Quadrant 3
				mleft = 1;
			else if ((player->rmomx < 0 && player->rmomy > 0) && (sideangle >= ANG270 /*&& (sideangle <= ANGLE_MAX)*/)) // Quadrant 4
				mleft = 1;
			else if ((player->rmomx < 0 && player->rmomy < 0) && (/*sideangle >= 0 &&*/ sideangle < ANG90)) // Quadrant 1
				mleft = 1;
			else if ((player->rmomx > 0 && player->rmomy < 0) && (sideangle >= ANG90 && sideangle < ANG180)) // Quadrant 2
				mleft = 1;
			else if (player->rmomx < 0 && ((sideangle >= ANG270+ANG45 /*&& sideangle <= ANGLE_MAX*/) || (/*sideangle >= 0 &&*/ sideangle <= ANG45)))
				mleft = 1;
			else if (player->rmomx > 0 && (sideangle >= ANG90+ANG45 && sideangle <= ANG180+ANG45))
				mleft = 1;
			else if (player->rmomy < 0 && (sideangle >= ANG45 && sideangle <= ANG90+ANG45))
				mleft = 1;
			else if (player->rmomy > 0 && (sideangle >= ANG180+ANG45 && sideangle <= ANG270+ANG45))
				mleft = 1;
			else // Put in 'or' checks here!
				mleft = 0;

			movepushside = cmd->sidemove * (thrustfactor*acceleration);

			if (player->powers[pw_sneakers] || player->powers[pw_super])
				movepushside *= 2;

			if (!onground)
			{
				movepushside >>= 2;

				// Lower speed if over "max" flight speed and greatly reduce movepushside.
				if (player->powers[pw_tailsfly] && player->speed > topspeed)
				{
					player->speed = topspeed - 1;
					movepushside /= 8;
				}
			}

			// Allow a bit of movement while spinning
			if (player->pflags & PF_SPINNING)
			{
				if (!(player->pflags & PF_STARTDASH))
					movepushside = FixedDiv(movepushside, 16*FRACUNIT);
				else
					movepushside = 0;
			}

			// Finally move the player now that his speed/direction has been decided.
			if (player->speed < topspeed)
				P_Thrust(player->mo, movepushsideangle, movepushside);
			else if ((mright) && (cmd->sidemove < 0))
				P_Thrust(player->mo, movepushsideangle, movepushside);
			else if ((mleft) && (cmd->sidemove > 0))
				P_Thrust(player->mo, movepushsideangle, movepushside);



		}
	}
}

//
// P_ShootLine
//
// Fun and fancy
// graphical indicator
// for building/debugging
// NiGHTS levels!
static void P_ShootLine(mobj_t *source, mobj_t *dest, fixed_t height)
{
	mobj_t *mo;
	int i;
	fixed_t temp;
	int speed, seesound;

	temp = dest->z;
	dest->z = height;

	seesound = mobjinfo[MT_REDRING].seesound;
	speed = mobjinfo[MT_REDRING].speed;
	mobjinfo[MT_REDRING].seesound = sfx_None;
	mobjinfo[MT_REDRING].speed = 20*FRACUNIT;
	mobjinfo[MT_REDRING].deathsound = sfx_None;
	mobjinfo[MT_REDRING].flags = MF_NOGRAVITY;

	mo = P_SpawnXYZMissile(source, dest, MT_REDRING, source->x, source->y, height);

	dest->z = temp;
	if (mo)
	{
		mo->flags2 |= MF2_RAILRING;
		mo->flags2 |= MF2_DONTDRAW;
		mo->flags |= MF_NOCLIPHEIGHT;
		mo->flags |= MF_NOCLIP;
		mo->flags &= ~MF_MISSILE;
		mo->fuse = 3;
	}

	for (i = 0; i < 32; i++)
	{
		if (mo)
		{
			if (i & 1)
				P_SpawnMobj(mo->x, mo->y, mo->z, MT_PARTICLE);

			P_UnsetThingPosition(mo);
			mo->x += mo->momx;
			mo->y += mo->momy;
			mo->z += mo->momz;
			mo->floorz = mo->subsector->sector->floorheight;
			mo->ceilingz = mo->subsector->sector->ceilingheight;
			P_SetThingPosition(mo);
		}
		else
		{
			mobjinfo[MT_REDRING].seesound = seesound;
			mobjinfo[MT_REDRING].speed = speed;
			mobjinfo[MT_REDRING].deathsound = sfx_None;
			mobjinfo[MT_REDRING].flags = MF_NOGRAVITY;
			return;
		}
	}
	mobjinfo[MT_REDRING].seesound = seesound;
	mobjinfo[MT_REDRING].speed = speed;
	mobjinfo[MT_REDRING].deathsound = sfx_None;
	mobjinfo[MT_REDRING].flags = MF_NOGRAVITY;
}

static void P_NightsTransferPoints(player_t *player, fixed_t xspeed, fixed_t radius)
{
	if (player->pflags & PF_TRANSFERTOCLOSEST)
	{
		const angle_t fa = R_PointToAngle2(player->axis1->x, player->axis1->y, player->axis2->x, player->axis2->y);
		P_InstaThrust(player->mo, fa, xspeed/10);
	}
	else
	{
		const angle_t fa = player->angle_pos>>ANGLETOFINESHIFT;

		player->mo->momx = player->mo->target->x + FixedMul(FINECOSINE(fa),radius) - player->mo->x;

		player->mo->momy = player->mo->target->y + FixedMul(FINESINE(fa),radius) - player->mo->y;
	}

	{
		const int sequence = player->mo->target->threshold;
		mobj_t *transfer1 = NULL;
		mobj_t *transfer2 = NULL;
		mobj_t *axis;
		mobj_t *mo2;
		thinker_t *th;
		line_t transfer1line;
		line_t transfer2line;
		boolean transfer1last = false;
		boolean transfer2last = false;
		vertex_t vertices[4];

		// Find next waypoint
		for (th = thinkercap.next; th != &thinkercap; th = th->next)
		{
			if (th->function.acp1 != (actionf_p1)P_MobjThinker) // Not a mobj thinker
				continue;

			mo2 = (mobj_t *)th;

			// Axis things are only at beginning of list.
			if (!(mo2->flags2 & MF2_AXIS))
				break;

			if ((mo2->type == MT_AXISTRANSFER || mo2->type == MT_AXISTRANSFERLINE)
				&& mo2->threshold == sequence)
			{
				if (player->pflags & PF_TRANSFERTOCLOSEST)
				{
					if (mo2->health == player->axis1->health)
						transfer1 = mo2;
					else if (mo2->health == player->axis2->health)
						transfer2 = mo2;
				}
				else
				{
					if (mo2->health == player->mo->target->health)
						transfer1 = mo2;
					else if (mo2->health == player->mo->target->health + 1)
						transfer2 = mo2;
				}
			}
		}

		// It might be possible that one wasn't found.
		// Is it because we're at the end of the track?
		// Look for a wrapper point.
		if (!transfer1)
		{
			for (th = thinkercap.next; th != &thinkercap; th = th->next)
			{
				if (th->function.acp1 != (actionf_p1)P_MobjThinker) // Not a mobj thinker
					continue;

				mo2 = (mobj_t *)th;

				// Axis things are only at beginning of list.
				if (!(mo2->flags2 & MF2_AXIS))
					break;

				if (mo2->threshold == sequence && (mo2->type == MT_AXISTRANSFER || mo2->type == MT_AXISTRANSFERLINE))
				{
					if (!transfer1)
					{
						transfer1 = mo2;
						transfer1last = true;
					}
					else if (mo2->health > transfer1->health)
					{
						transfer1 = mo2;
						transfer1last = true;
					}
				}
			}
		}
		if (!transfer2)
		{
			for (th = thinkercap.next; th != &thinkercap; th = th->next)
			{
				if (th->function.acp1 != (actionf_p1)P_MobjThinker) // Not a mobj thinker
					continue;

				mo2 = (mobj_t *)th;

				// Axis things are only at beginning of list.
				if (!(mo2->flags2 & MF2_AXIS))
					break;

				if (mo2->threshold == sequence && (mo2->type == MT_AXISTRANSFER || mo2->type == MT_AXISTRANSFERLINE))
				{
					if (!transfer2)
					{
						transfer2 = mo2;
						transfer2last = true;
					}
					else if (mo2->health > transfer2->health)
					{
						transfer2 = mo2;
						transfer2last = true;
					}
				}
			}
		}

		if (!(transfer1 && transfer2)) // We can't continue...
			I_Error("Mare does not form a complete circuit!\n");

		transfer1line.v1 = &vertices[0];
		transfer1line.v2 = &vertices[1];
		transfer2line.v1 = &vertices[2];
		transfer2line.v2 = &vertices[3];

		if (cv_devmode && (leveltime % TICRATE == 0))
		{
			CONS_Printf("Transfer1 : %ld\n", transfer1->health);
			CONS_Printf("Transfer2 : %ld\n", transfer2->health);
		}

		//CONS_Printf("T1 is at %d, %d\n", transfer1->x>>FRACBITS, transfer1->y>>FRACBITS);
		//CONS_Printf("T2 is at %d, %d\n", transfer2->x>>FRACBITS, transfer2->y>>FRACBITS);
		//CONS_Printf("Distance from T1: %d\n", P_AproxDistance(transfer1->x - player->mo->x, transfer1->y - player->mo->y)>>FRACBITS);
		//CONS_Printf("Distance from T2: %d\n", P_AproxDistance(transfer2->x - player->mo->x, transfer2->y - player->mo->y)>>FRACBITS);

		// Transfer1 is closer to the player than transfer2
		if (P_AproxDistance(transfer1->x - player->mo->x, transfer1->y - player->mo->y)>>FRACBITS
			< P_AproxDistance(transfer2->x - player->mo->x, transfer2->y - player->mo->y)>>FRACBITS)
		{
			if (transfer1->type == MT_AXISTRANSFERLINE)
			{
				if (transfer1last)
					axis = P_FindAxis(transfer1->threshold, transfer1->health-2);
				else if (player->pflags & PF_TRANSFERTOCLOSEST)
					axis = P_FindAxis(transfer1->threshold, transfer1->health-1);
				else
					axis = P_FindAxis(transfer1->threshold, transfer1->health);

				if (!axis)
				{
					CONS_Printf("Unable to find an axis - error code #1\n");
					return;
				}

				//CONS_Printf("Drawing a line from %d to ", axis->health);

				transfer1line.v1->x = axis->x;
				transfer1line.v1->y = axis->y;

				transfer1line.v2->x = transfer1->x;
				transfer1line.v2->y = transfer1->y;

				if (cv_devmode)
					P_ShootLine(axis, transfer1, player->mo->z);

				//CONS_Printf("closest %d\n", transfer1->health);

				transfer1line.dx = transfer1line.v2->x - transfer1line.v1->x;
				transfer1line.dy = transfer1line.v2->y - transfer1line.v1->y;

				if (P_PointOnLineSide(player->mo->x, player->mo->y, &transfer1line)
						!= P_PointOnLineSide(player->mo->x+player->mo->momx, player->mo->y+player->mo->momy, &transfer1line))
				{
					if (cv_devmode)
					{
						HU_SetCEchoDuration(1);
						HU_DoCEcho("transfer!");
						HU_SetCEchoDuration(5);
						S_StartSound(NULL, sfx_strpst);
					}
					if (player->pflags & PF_TRANSFERTOCLOSEST)
					{
						player->pflags &= ~PF_TRANSFERTOCLOSEST;
						P_TransferToAxis(player, transfer1->health - 1);
					}
					else
					{
						player->pflags |= PF_TRANSFERTOCLOSEST;
						P_SetTarget(&player->axis2, transfer1);
						P_SetTarget(&player->axis1, P_FindAxisTransfer(transfer1->threshold, transfer1->health-1, MT_AXISTRANSFERLINE));//P_FindAxis(transfer1->threshold, axis->health-2);
					}
				}
			}
			else
			{
				// Transfer1
				if (transfer1last)
					axis = P_FindAxis(transfer1->threshold, 1);
				else
					axis = P_FindAxis(transfer1->threshold, transfer1->health);

				if (!axis)
				{
					CONS_Printf("Unable to find an axis - error code #2\n");
					return;
				}

				//CONS_Printf("Drawing a line from %d to ", axis->health);

				transfer1line.v1->x = axis->x;
				transfer1line.v1->y = axis->y;

				if (cv_devmode)
					P_ShootLine(transfer1, P_FindAxis(transfer1->threshold, transfer1->health-1), player->mo->z);

				//axis = P_FindAxis(transfer1->threshold, transfer1->health-1);

				//CONS_Printf("%d\n", axis->health);

				transfer1line.v2->x = transfer1->x;
				transfer1line.v2->y = transfer1->y;

				transfer1line.dx = transfer1line.v2->x - transfer1line.v1->x;
				transfer1line.dy = transfer1line.v2->y - transfer1line.v1->y;

				if (P_PointOnLineSide(player->mo->x, player->mo->y, &transfer1line)
					!= P_PointOnLineSide(player->mo->x+player->mo->momx, player->mo->y+player->mo->momy, &transfer1line))
				{
					if (cv_devmode)
					{
						HU_SetCEchoDuration(1);
						HU_DoCEcho("transfer!");
						HU_SetCEchoDuration(5);
						S_StartSound(NULL, sfx_strpst);
					}
					if (player->mo->target->health < transfer1->health)
					{
						// Find the next axis with a ->health
						// +1 from the current axis.
						if (transfer1last)
							P_TransferToAxis(player, transfer1->health - 1);
						else
							P_TransferToAxis(player, transfer1->health);
					}
					else if (player->mo->target->health >= transfer1->health)
					{
						// Find the next axis with a ->health
						// -1 from the current axis.
						P_TransferToAxis(player, transfer1->health - 1);
					}
				}
			}
		}
		else
		{
			if (transfer2->type == MT_AXISTRANSFERLINE)
			{
				if (transfer2last)
					axis = P_FindAxis(transfer2->threshold, 1);
				else if (player->pflags & PF_TRANSFERTOCLOSEST)
					axis = P_FindAxis(transfer2->threshold, transfer2->health);
				else
					axis = P_FindAxis(transfer2->threshold, transfer2->health - 1);

				if (!axis)
					axis = P_FindAxis(transfer2->threshold, 1);

				if (!axis)
				{
					CONS_Printf("Unable to find an axis - error code #3\n");
					return;
				}

				//CONS_Printf("Drawing a line from %d to ", axis->health);

				transfer2line.v1->x = axis->x;
				transfer2line.v1->y = axis->y;

				transfer2line.v2->x = transfer2->x;
				transfer2line.v2->y = transfer2->y;

				//CONS_Printf("closest %d\n", transfer2->health);

				if (cv_devmode)
					P_ShootLine(axis, transfer2, player->mo->z);

				transfer2line.dx = transfer2line.v2->x - transfer2line.v1->x;
				transfer2line.dy = transfer2line.v2->y - transfer2line.v1->y;

				if (P_PointOnLineSide(player->mo->x, player->mo->y, &transfer2line)
						!= P_PointOnLineSide(player->mo->x+player->mo->momx, player->mo->y+player->mo->momy, &transfer2line))
				{
					if (cv_devmode)
					{
						HU_SetCEchoDuration(1);
						HU_DoCEcho("transfer!");
						HU_SetCEchoDuration(5);
						S_StartSound(NULL, sfx_strpst);
					}
					if (player->pflags & PF_TRANSFERTOCLOSEST)
					{
						player->pflags &= ~PF_TRANSFERTOCLOSEST;

						if (!P_FindAxis(transfer2->threshold, transfer2->health))
							transfer2last = true;

						if (transfer2last)
							P_TransferToAxis(player, 1);
						else
							P_TransferToAxis(player, transfer2->health);
					}
					else
					{
						player->pflags |= PF_TRANSFERTOCLOSEST;
						P_SetTarget(&player->axis1, transfer2);
						P_SetTarget(&player->axis2, P_FindAxisTransfer(transfer2->threshold, transfer2->health+1, MT_AXISTRANSFERLINE));//P_FindAxis(transfer2->threshold, axis->health + 2);
					}
				}
			}
			else
			{
				// Transfer2
				if (transfer2last)
					axis = P_FindAxis(transfer2->threshold, 1);
				else
					axis = P_FindAxis(transfer2->threshold, transfer2->health);

				if (!axis)
					axis = P_FindAxis(transfer2->threshold, 1);

				if (!axis)
				{
					CONS_Printf("Unable to find an axis - error code #4\n");
					return;
				}

				//CONS_Printf("Drawing a line from %d to ", axis->health);

				transfer2line.v1->x = axis->x;
				transfer2line.v1->y = axis->y;

				if (cv_devmode)
					P_ShootLine(transfer2, P_FindAxis(transfer2->threshold, transfer2->health-1), player->mo->z);

				//axis = P_FindAxis(transfer2->threshold, transfer2->health-1);

				//CONS_Printf("%d\n", axis->health);

				transfer2line.v2->x = transfer2->x;
				transfer2line.v2->y = transfer2->y;

				transfer2line.dx = transfer2line.v2->x - transfer2line.v1->x;
				transfer2line.dy = transfer2line.v2->y - transfer2line.v1->y;

				if (P_PointOnLineSide(player->mo->x, player->mo->y, &transfer2line)
					!= P_PointOnLineSide(player->mo->x+player->mo->momx, player->mo->y+player->mo->momy, &transfer2line))
				{
					if (cv_devmode)
					{
						HU_SetCEchoDuration(1);
						HU_DoCEcho("transfer!");
						HU_SetCEchoDuration(5);
						S_StartSound(NULL, sfx_strpst);
					}
					if (player->mo->target->health < transfer2->health)
					{
						if (!P_FindAxis(transfer2->threshold, transfer2->health))
							transfer2last = true;

						if (transfer2last)
							P_TransferToAxis(player, 1);
						else
							P_TransferToAxis(player, transfer2->health);
					}
					else if (player->mo->target->health >= transfer2->health)
						P_TransferToAxis(player, transfer2->health - 1);
				}
			}
		}
	}
}

#define MAXDRILLSPEED 14000
#define MAXNORMALSPEED 6000

// P_Angle2DMovement
//
// Movement code special 2D mode!
//
#ifdef ANGLE2D
static void P_NiGHTSMovement(player_t *player) // SRB2CBTODO: P_Angle2DMovement
{
	int drillamt = 0;
	boolean still = false, moved = false, backwardaxis = false, firstdrill;
	signed short newangle = 0;
	fixed_t xspeed, yspeed;
	thinker_t *th;
	mobj_t *mo2;
	mobj_t *closestaxis = NULL;
	fixed_t newx, newy, radius;
	angle_t movingangle;
	ticcmd_t *cmd = &player->cmd;
	int thrustfactor;
	int i;
	int pspeed; // this is so the player's speed does not need to be modified

	pspeed = player->speed;

	firstdrill = false;

	if (!player->mo->tracer)
	{
		P_DeNightserizePlayer(player);
		return;
	}

	//if (leveltime % TICRATE == 0 && gametype != GT_RACE)
	//	player->nightstime--;
#ifdef FREEFLY
	if (mapheaderinfo[gamemap-1].freefly)
	{
		if (cmd->buttons & BT_ACTION)
		{
			P_DeNightserizePlayer(player);
			return;
		}
	}
	else
	{
		if (!player->nightstime)
		{
			P_DeNightserizePlayer(player);
			return;
		}
	}
#else
	if (!player->nightstime)
	{
		P_DeNightserizePlayer(player);
		return;
	}
#endif

	if (player->mo->z < player->mo->floorz)
		player->mo->z = player->mo->floorz;

	if (player->mo->z+player->mo->height > player->mo->ceilingz)
		player->mo->z = player->mo->ceilingz - player->mo->height;

	newx = P_ReturnThrustX(player->mo, player->mo->angle, 3*FRACUNIT)+player->mo->x;
	newy = P_ReturnThrustY(player->mo, player->mo->angle, 3*FRACUNIT)+player->mo->y;

	if (!player->mo->target)
	{
		fixed_t dist1, dist2 = 0;

		// scan the thinkers
		// to find the closest axis point
		for (th = thinkercap.next; th != &thinkercap; th = th->next)
		{
			if (th->function.acp1 != (actionf_p1)P_MobjThinker)
				continue;

			mo2 = (mobj_t *)th;

			if (mo2->type == MT_AXIS)
			{
				if (mo2->threshold == player->mare)
				{
					if (closestaxis == NULL)
					{
						closestaxis = mo2;
						dist2 = R_PointToDist2(newx, newy, mo2->x, mo2->y)-mo2->radius;
					}
					else
					{
						dist1 = R_PointToDist2(newx, newy, mo2->x, mo2->y)-mo2->radius;

						if (dist1 < dist2)
						{
							closestaxis = mo2;
							dist2 = dist1;
						}
					}
				}
			}
		}

		player->mo->target = closestaxis;
	}

	if (!player->mo->target) // Uh-oh!
	{
		CONS_Printf("No axis points found!\n");
		return;
	}

	// The 'ambush' flag says you should rotate
	// the other way around the axis.
	if (player->mo->target->flags & MF_AMBUSH)
		backwardaxis = true;

	player->angle_pos = R_PointToAngle2(player->mo->target->x, player->mo->target->y, player->mo->x, player->mo->y);

	player->old_angle_pos = player->angle_pos;

	radius = player->mo->target->radius;

	// Currently reeling from being hit.
	if (player->powers[pw_flashing] > (2*flashingtics)/3)
	{
		{
			const angle_t fa = FINEANGLE_C(player->flyangle);
			const fixed_t speed = (pspeed*FRACUNIT)/50;

			xspeed = FixedMul(finecosine[fa],speed);
			yspeed = FixedMul(finesine[fa],speed);
		}

		if (!(player->pflags & PF_TRANSFERTOCLOSEST))
		{
			xspeed = FixedMul(xspeed, FixedDiv(10240*FRACUNIT, player->mo->target->radius)/10);

			if (backwardaxis)
				xspeed *= -1;

			player->angle_pos += FixedAngleC(FixedDiv(xspeed,5*FRACUNIT),40*FRACUNIT);
		}

		if (player->pflags & PF_TRANSFERTOCLOSEST)
		{
			const angle_t fa = R_PointToAngle2(player->axis1->x, player->axis1->y, player->axis2->x, player->axis2->y);
			// You'll go off the track if this isn't here
			P_InstaThrust(player->mo, fa, xspeed/10);
		}
		else
		{
			const angle_t fa = player->angle_pos>>ANGLETOFINESHIFT;

			player->mo->momx = player->mo->target->x + FixedMul(finecosine[fa],radius) - player->mo->x;
			player->mo->momy = player->mo->target->y + FixedMul(finesine[fa],radius) - player->mo->y;
		}

		player->mo->momz = 0;

		return;
	}

	if (player->bumpertime)
	{

	}
	else if (cmd->buttons & BT_JUMP)
	{
		if (!player->jumping)
			firstdrill = true;

		player->jumping = 1;
	}
	else
	{
		if (cmd->sidemove != 0)
			moved = true;
	}

	if (cmd->forwardmove != 0)
		moved = true;

	if (player->bumpertime)
		drillamt = 0;
	else if (moved)
	{
		{
			const int distabs = abs(cmd->forwardmove)*abs(cmd->forwardmove) + abs(cmd->sidemove)*abs(cmd->sidemove);
			const float distsqrt = (float)(sqrt(distabs));
			const int distance = (int)distsqrt;

			drillamt += distance > 50 ? 50 : distance;

			drillamt = (5*drillamt)/4;
		}
	}

	pspeed += drillamt;
	player->speed = pspeed;

	//P_InstaThrust(player->mo, player->mo->angle, pspeed*FRACUNIT);

	if (player->speed > MAXDRILLSPEED)
		player->speed -= 100+drillamt;

	if (!player->bumpertime)
	{
		{
			if (pspeed > MAXDRILLSPEED)
				pspeed -= 100+drillamt;
			else if (pspeed > MAXNORMALSPEED)
				pspeed -= (drillamt*19)/16;
		}
	}

	if (!player->bumpertime)
	{
		if (drillamt == 0 && pspeed > 0)
			pspeed -= 25;

		if (pspeed < 0)
			pspeed = 0;

		if (cmd->sidemove != 0)
		{
			newangle = (signed short)(AngleFixed(R_PointToAngle2(0,0, cmd->sidemove*FRACUNIT, cmd->forwardmove*FRACUNIT))/FRACUNIT);
		}
		else if (cmd->forwardmove > 0)
			newangle = 90;
		else if (cmd->forwardmove < 0)
			newangle = 269;

		if (newangle < 0 && moved)
			newangle = (signed short)(360+newangle);
	}

	thrustfactor = 6;

	for (i = 0; i < thrustfactor; i++)
	{
		if (moved && player->flyangle != newangle)
		{
			// player->flyangle is the one to move
			// newangle is the "move to"
			if ((((newangle-player->flyangle)+360)%360)>(((player->flyangle-newangle)+360)%360))
			{
				player->flyangle--;
				if (player->flyangle < 0)
					player->flyangle = 360 + player->flyangle;
			}
			else
				player->flyangle++;
		}

		player->flyangle %= 360;
	}

	if (!(pspeed)
		&& cmd->forwardmove == 0)
		still = true;

	if ((cmd->buttons & BT_CAMLEFT) && (cmd->buttons & BT_CAMRIGHT))
	{
		if (!(player->pflags & PF_SKIDDOWN) && pspeed > 2000)
		{
			pspeed /= 10;
			S_StartSound(player->mo, sfx_ngskid);
		}
		player->pflags |= PF_SKIDDOWN;
	}
	else
		player->pflags &= ~PF_SKIDDOWN;

	{
		const angle_t fa = FINEANGLE_C(player->flyangle);
		const fixed_t speed = (pspeed*FRACUNIT)/50;
		xspeed = FixedMul(finecosine[fa],speed);
		yspeed = FixedMul(finesine[fa],speed);
	}

	if (!(player->pflags & PF_TRANSFERTOCLOSEST))
	{
		xspeed = FixedMul(xspeed, FixedDiv(10240*FRACUNIT, player->mo->target->radius)/10);

		if (backwardaxis)
			xspeed *= -1;

		player->angle_pos += FixedAngleC(FixedDiv(xspeed,5*FRACUNIT),40*FRACUNIT);
	}

	{
		if (player->pflags & PF_TRANSFERTOCLOSEST)
		{
			const angle_t fa = R_PointToAngle2(player->axis1->x, player->axis1->y, player->axis2->x, player->axis2->y);
			// You'll go off the track if this isn't here
			P_InstaThrust(player->mo, fa, xspeed/10);
		}
		else
		{
			const angle_t fa = player->angle_pos>>ANGLETOFINESHIFT;

			player->mo->momx = player->mo->target->x + FixedMul(finecosine[fa],radius) - player->mo->x;
			player->mo->momy = player->mo->target->y + FixedMul(finesine[fa],radius) - player->mo->y;
		}

		{
			const int sequence = player->mo->target->threshold;
			mobj_t *transfer1 = NULL;
			mobj_t *transfer2 = NULL;
			mobj_t *axis;
			line_t transfer1line;
			line_t transfer2line;
			boolean transfer1last = false;
			boolean transfer2last = false;
			vertex_t vertices[4];

			// Find next waypoint
			for (th = thinkercap.next; th != &thinkercap; th = th->next)
			{
				if (th->function.acp1 != (actionf_p1)P_MobjThinker) // Not a mobj thinker
					continue;

				mo2 = (mobj_t *)th;

				// Axis things are only at beginning of list.
				if (!(mo2->flags2 & MF2_AXIS))
					break;

				if ((mo2->type == MT_AXISTRANSFER || mo2->type == MT_AXISTRANSFERLINE)
					&& mo2->threshold == sequence)
				{
					if (player->pflags & PF_TRANSFERTOCLOSEST)
					{
						if (mo2->health == player->axis1->health)
							transfer1 = mo2;
						else if (mo2->health == player->axis2->health)
							transfer2 = mo2;
					}
					else
					{
						if (mo2->health == player->mo->target->health)
							transfer1 = mo2;
						else if (mo2->health == player->mo->target->health + 1)
							transfer2 = mo2;
					}
				}
			}

			// It might be possible that one wasn't found.
			// Is it because we're at the end of the track?
			// Look for a wrapper point.
			if (!transfer1)
			{
				for (th = thinkercap.next; th != &thinkercap; th = th->next)
				{
					if (th->function.acp1 != (actionf_p1)P_MobjThinker) // Not a mobj thinker
						continue;

					mo2 = (mobj_t *)th;

					// Axis things are only at beginning of list.
					if (!(mo2->flags2 & MF2_AXIS))
						break;

					if (mo2->threshold == sequence && (mo2->type == MT_AXISTRANSFER || mo2->type == MT_AXISTRANSFERLINE))
					{
						if (!transfer1)
						{
							transfer1 = mo2;
							transfer1last = true;
						}
						else if (mo2->health > transfer1->health)
						{
							transfer1 = mo2;
							transfer1last = true;
						}
					}
				}
			}
			if (!transfer2)
			{
				for (th = thinkercap.next; th != &thinkercap; th = th->next)
				{
					if (th->function.acp1 != (actionf_p1)P_MobjThinker) // Not a mobj thinker
						continue;

					mo2 = (mobj_t *)th;

					// Axis things are only at beginning of list.
					if (!(mo2->flags2 & MF2_AXIS))
						break;

					if (mo2->threshold == sequence && (mo2->type == MT_AXISTRANSFER || mo2->type == MT_AXISTRANSFERLINE))
					{
						if (!transfer2)
						{
							transfer2 = mo2;
							transfer2last = true;
						}
						else if (mo2->health > transfer2->health)
						{
							transfer2 = mo2;
							transfer2last = true;
						}
					}
				}
			}

			if (!(transfer1 && transfer2)) // We can't continue...
				I_Error("Mare does not form a complete circuit!\n");

			transfer1line.v1 = &vertices[0];
			transfer1line.v2 = &vertices[1];
			transfer2line.v1 = &vertices[2];
			transfer2line.v2 = &vertices[3];

			if (cv_devmode && (leveltime % TICRATE == 0))
			{
				CONS_Printf("Transfer1 : %li\n", transfer1->health);
				CONS_Printf("Transfer2 : %li\n", transfer2->health);
			}

			// Transfer1 is closer to the player than transfer2
			if (P_AproxDistance(transfer1->x - player->mo->x, transfer1->y - player->mo->y)>>FRACBITS
				< P_AproxDistance(transfer2->x - player->mo->x, transfer2->y - player->mo->y)>>FRACBITS)
			{
				if (transfer1->type == MT_AXISTRANSFERLINE)
				{
					if (transfer1last)
						axis = P_FindAxis(transfer1->threshold, transfer1->health-2);
					else if (player->pflags & PF_TRANSFERTOCLOSEST)
						axis = P_FindAxis(transfer1->threshold, transfer1->health-1);
					else
						axis = P_FindAxis(transfer1->threshold, transfer1->health);

					if (!axis)
					{
						CONS_Printf("Unable to find an axis - error code #1\n");
						goto nightsbombout;
					}

					transfer1line.v1->x = axis->x;
					transfer1line.v1->y = axis->y;

					transfer1line.v2->x = transfer1->x;
					transfer1line.v2->y = transfer1->y;

					if (cv_devmode)
						P_ShootLine(axis, transfer1, player->mo->z);

					transfer1line.dx = transfer1line.v2->x - transfer1line.v1->x;
					transfer1line.dy = transfer1line.v2->y - transfer1line.v1->y;

					if (P_PointOnLineSide(player->mo->x, player->mo->y, &transfer1line)
						!= P_PointOnLineSide(player->mo->x+player->mo->momx, player->mo->y+player->mo->momy, &transfer1line))
					{
						if (cv_devmode)
						{
							COM_BufAddText("cechoduration 1\n");
							COM_BufAddText("cecho transfer!\n");
							COM_BufAddText("cechoduration 5\n");
							S_StartSound(0, sfx_strpst);
						}
						if (player->pflags & PF_TRANSFERTOCLOSEST)
						{
							player->pflags &= ~PF_TRANSFERTOCLOSEST;
							P_TransferToAxis(player, transfer1->health - 1);
						}
						else
						{
							player->pflags |= PF_TRANSFERTOCLOSEST;
							player->axis2 = transfer1;
							player->axis1 = P_FindAxisTransfer(transfer1->threshold, transfer1->health-1, MT_AXISTRANSFERLINE);//P_FindAxis(transfer1->threshold, axis->health-2);
						}
					}
				}
				else
				{
					// Transfer1
					if (transfer1last)
						axis = P_FindAxis(transfer1->threshold, 1);
					else
						axis = P_FindAxis(transfer1->threshold, transfer1->health);

					if (!axis)
					{
						CONS_Printf("Unable to find an axis - error code #2\n");
						goto nightsbombout;
					}

					transfer1line.v1->x = axis->x;
					transfer1line.v1->y = axis->y;

					if (cv_devmode)
						P_ShootLine(transfer1, P_FindAxis(transfer1->threshold, transfer1->health-1), player->mo->z);

					transfer1line.v2->x = transfer1->x;
					transfer1line.v2->y = transfer1->y;

					transfer1line.dx = transfer1line.v2->x - transfer1line.v1->x;
					transfer1line.dy = transfer1line.v2->y - transfer1line.v1->y;

					if (P_PointOnLineSide(player->mo->x, player->mo->y, &transfer1line)
						!= P_PointOnLineSide(player->mo->x+player->mo->momx, player->mo->y+player->mo->momy, &transfer1line))
					{
						if (cv_devmode)
						{
							COM_BufAddText("cechoduration 1\n");
							COM_BufAddText("cecho transfer!\n");
							COM_BufAddText("cechoduration 5\n");
							S_StartSound(0, sfx_strpst);
						}
						if (player->mo->target->health < transfer1->health)
						{
							// Find the next axis with a ->health
							// +1 from the current axis.
							if (transfer1last)
								P_TransferToAxis(player, transfer1->health - 1);
							else
								P_TransferToAxis(player, transfer1->health);
						}
						else if (player->mo->target->health >= transfer1->health)
						{
							// Find the next axis with a ->health
							// -1 from the current axis.
							P_TransferToAxis(player, transfer1->health - 1);
						}
					}
				}
			}
			else
			{
				if (transfer2->type == MT_AXISTRANSFERLINE)
				{
					if (transfer2last)
						axis = P_FindAxis(transfer2->threshold, 1);
					else if (player->pflags & PF_TRANSFERTOCLOSEST)
						axis = P_FindAxis(transfer2->threshold, transfer2->health);
					else
						axis = P_FindAxis(transfer2->threshold, transfer2->health - 1);

					if (!axis)
						axis = P_FindAxis(transfer2->threshold, 1);

					if (!axis)
					{
						CONS_Printf("Unable to find an axis - error code #3\n");
						goto nightsbombout;
					}

					transfer2line.v1->x = axis->x;
					transfer2line.v1->y = axis->y;

					transfer2line.v2->x = transfer2->x;
					transfer2line.v2->y = transfer2->y;

					if (cv_devmode)
						P_ShootLine(axis, transfer2, player->mo->z);

					transfer2line.dx = transfer2line.v2->x - transfer2line.v1->x;
					transfer2line.dy = transfer2line.v2->y - transfer2line.v1->y;

					if (P_PointOnLineSide(player->mo->x, player->mo->y, &transfer2line)
						!= P_PointOnLineSide(player->mo->x+player->mo->momx, player->mo->y+player->mo->momy, &transfer2line))
					{
						if (cv_devmode)
						{
							COM_BufAddText("cechoduration 1\n");
							COM_BufAddText("cecho transfer!\n");
							COM_BufAddText("cechoduration 5\n");
							S_StartSound(0, sfx_strpst);
						}
						if (player->pflags & PF_TRANSFERTOCLOSEST)
						{
							player->pflags &= ~PF_TRANSFERTOCLOSEST;

							if (!P_FindAxis(transfer2->threshold, transfer2->health))
								transfer2last = true;

							if (transfer2last)
								P_TransferToAxis(player, 1);
							else
								P_TransferToAxis(player, transfer2->health);
						}
						else
						{
							player->pflags |= PF_TRANSFERTOCLOSEST;
							player->axis1 = transfer2;
							player->axis2 = P_FindAxisTransfer(transfer2->threshold, transfer2->health+1, MT_AXISTRANSFERLINE);//P_FindAxis(transfer2->threshold, axis->health + 2);
						}
					}
				}
				else
				{
					// Transfer2
					if (transfer2last)
						axis = P_FindAxis(transfer2->threshold, 1);
					else
						axis = P_FindAxis(transfer2->threshold, transfer2->health);

					if (!axis)
						axis = P_FindAxis(transfer2->threshold, 1);

					if (!axis)
					{
						CONS_Printf("Unable to find an axis - error code #4\n");
						goto nightsbombout;
					}

					transfer2line.v1->x = axis->x;
					transfer2line.v1->y = axis->y;

					if (cv_devmode)
						P_ShootLine(transfer2, P_FindAxis(transfer2->threshold, transfer2->health-1), player->mo->z);

					transfer2line.v2->x = transfer2->x;
					transfer2line.v2->y = transfer2->y;

					transfer2line.dx = transfer2line.v2->x - transfer2line.v1->x;
					transfer2line.dy = transfer2line.v2->y - transfer2line.v1->y;

					if (P_PointOnLineSide(player->mo->x, player->mo->y, &transfer2line)
						!= P_PointOnLineSide(player->mo->x+player->mo->momx, player->mo->y+player->mo->momy, &transfer2line))
					{
						if (cv_devmode)
						{
							COM_BufAddText("cechoduration 1\n");
							COM_BufAddText("cecho transfer!\n");
							COM_BufAddText("cechoduration 5\n");
							S_StartSound(0, sfx_strpst);
						}
						if (player->mo->target->health < transfer2->health)
						{
							if (!P_FindAxis(transfer2->threshold, transfer2->health))
								transfer2last = true;

							if (transfer2last)
								P_TransferToAxis(player, 1);
							else
								P_TransferToAxis(player, transfer2->health);
						}
						else if (player->mo->target->health >= transfer2->health)
							P_TransferToAxis(player, transfer2->health - 1);
					}
				}
			}
		}
	}

nightsbombout:

	// You can create splashes as you fly across water.
	if (player->mo->z + P_GetPlayerHeight(player) >= player->mo->watertop
		&& player->mo->z <= player->mo->watertop && player->speed > 9000 && leveltime % (TICRATE/7) == 0 && !player->spectator)
	{
		P_Particles(player->mo, MT_PARTICLE, 255, FRACUNIT/2, 6*FRACUNIT, TICRATE, 1, false, 13, 6, 0);
		P_Particles(player->mo, MT_PARTICLE, 255, FRACUNIT  , 5*FRACUNIT , TICRATE, 1, false, 13, 6, 0);
		mobj_t *water = P_SpawnMobj(player->mo->x, player->mo->y, player->mo->watertop, MT_SPLISH);
		S_StartSound(water, sfx_wslap);
		water->destscale = player->mo->scale;
		P_SetScale(water, player->mo->scale);
	}

	// Spawn Sonic's bubbles
	if (player->mo->eflags & MFE_UNDERWATER)
	{
		const fixed_t zh = player->mo->z + FixedDiv(player->mo->height,5*(FRACUNIT/4));
		if (!(P_Random() % 16))
			P_SpawnMobj(player->mo->x, player->mo->y, zh, MT_SMALLBUBBLE)->threshold = 42;
		else if (!(P_Random() % 96))
			P_SpawnMobj(player->mo->x, player->mo->y, zh, MT_MEDIUMBUBBLE)->threshold = 42;
	}

	// This allows you to turn in this wired circle system :P

	if (player->mo->momx || player->mo->momy)
		player->mo->angle = R_PointToAngle2(0, 0, player->mo->momx, player->mo->momy);

	if (still)
	{
		player->anotherflyangle = 0;
		movingangle = 0;
	}
	else if (backwardaxis)
	{
		// Special cases to prevent the angle from being
		// calculated incorrectly when wrapped.
		if (player->old_angle_pos > ANGLE_350 && player->angle_pos < ANGLE_10)
		{
			movingangle = R_PointToAngle2(0, player->mo->z, -R_PointToDist2(player->mo->momx, player->mo->momy, 0, 0), player->mo->z + player->mo->momz);
		}
		else if (player->old_angle_pos < ANGLE_10 && player->angle_pos > ANGLE_350)
		{
			movingangle = R_PointToAngle2(0, player->mo->z, R_PointToDist2(player->mo->momx, player->mo->momy, 0, 0), player->mo->z + player->mo->momz);
		}
		else if (player->angle_pos > player->old_angle_pos)
		{
			movingangle = R_PointToAngle2(0, player->mo->z, -R_PointToDist2(player->mo->momx, player->mo->momy, 0, 0), player->mo->z + player->mo->momz);
		}
		else
		{
			movingangle = R_PointToAngle2(0, player->mo->z, R_PointToDist2(player->mo->momx, player->mo->momy, 0, 0), player->mo->z + player->mo->momz);
		}
	}
	else
	{
		// Special cases to prevent the angle from being
		// calculated incorrectly when wrapped.
		if (player->old_angle_pos > ANGLE_350 && player->angle_pos < ANGLE_10)
		{
			movingangle = R_PointToAngle2(0, player->mo->z, R_PointToDist2(player->mo->momx, player->mo->momy, 0, 0), player->mo->z + player->mo->momz);
		}
		else if (player->old_angle_pos < ANGLE_10 && player->angle_pos > ANGLE_350)
		{
			movingangle = R_PointToAngle2(0, player->mo->z, -R_PointToDist2(player->mo->momx, player->mo->momy, 0, 0), player->mo->z + player->mo->momz);
		}
		else if (player->angle_pos < player->old_angle_pos)
		{
			movingangle = R_PointToAngle2(0, player->mo->z, -R_PointToDist2(player->mo->momx, player->mo->momy, 0, 0), player->mo->z + player->mo->momz);
		}
		else
		{
			movingangle = R_PointToAngle2(0, player->mo->z, R_PointToDist2(player->mo->momx, player->mo->momy, 0, 0), player->mo->z + player->mo->momz);
		}
	}


	if (player == &players[consoleplayer])
		localangle = player->mo->angle;
	else if (splitscreen && player == &players[secondarydisplayplayer])
		localangle2 = player->mo->angle;


	// Synchronizes the "real" amount of time spent in the level.
	if (!player->exiting)
	{
		if (gametype == GT_RACE)
		{
			if (leveltime >= 4*TICRATE)
				player->realtime = leveltime - 4*TICRATE;
			else
				player->realtime = 0;
		}
		else
			player->realtime = leveltime;
	}


	if (movingangle >= ANG90 && movingangle <= ANG180)
		movingangle = movingangle - ANG180;
	else if (movingangle >= ANG180 && movingangle <= ANG270)
		movingangle = movingangle - ANG180;
	else if (movingangle >= ANG270)
		movingangle = (movingangle - ANGLE_MAX);


	if (player->powers[pw_extralife] == 1 && P_IsLocalPlayer(player)) // Extra Life!
		P_RestoreMusic(player);

}

#else

//
// P_NiGHTSMovement
//
// Movement code for NiGHTS!
//
static void P_NiGHTSMovement(player_t *player) // Normal nights code
{
	int drillamt = 0;
	boolean still = false, moved = false, backwardaxis = false, firstdrill;
	signed short newangle = 0;
	fixed_t xspeed, yspeed;
	thinker_t *th;
	mobj_t *mo2;
	mobj_t *closestaxis = NULL;
	fixed_t newx, newy, radius;
	angle_t movingangle;
	ticcmd_t *cmd = &player->cmd;
	int thrustfactor;
	int i;

	player->pflags &= ~PF_DRILLING;

	firstdrill = false;

	if (player->drillmeter > 96*20)
		player->drillmeter = 96*20;

	if (player->drilldelay)
		player->drilldelay--;

	if (!(cmd->buttons & BT_JUMP))
	{
		// Always have just a TINY bit of drill power.
		if (player->drillmeter <= 0)
			player->drillmeter = (TICRATE/10)/NEWTICRATERATIO;
	}

	if (!player->mo->tracer)
	{
		P_DeNightserizePlayer(player);
		return;
	}

	if (leveltime % TICRATE == 0 && gametype != GT_RACE)
		player->nightstime--;

#ifdef FREEFLY
	if (mapheaderinfo[gamemap-1].freefly)
	{
		if (cmd->buttons & BT_USE)
		{
			P_DeNightserizePlayer(player);
			return;
		}
	}
	else
	{
		if (!player->nightstime)
		{
			P_DeNightserizePlayer(player);
			return;
		}
	}
#else
	if (!player->nightstime)
	{
		P_DeNightserizePlayer(player);
		return;
	}
#endif

	if (player->mo->z < player->mo->floorz)
		player->mo->z = player->mo->floorz;

	if (player->mo->z+player->mo->height > player->mo->ceilingz)
		player->mo->z = player->mo->ceilingz - player->mo->height;

	newx = P_ReturnThrustX(player->mo, player->mo->angle, 3*FRACUNIT)+player->mo->x;
	newy = P_ReturnThrustY(player->mo, player->mo->angle, 3*FRACUNIT)+player->mo->y;

	if (!player->mo->target)
	{
		fixed_t dist1, dist2 = 0;

		// scan the thinkers
		// to find the closest axis point
		for (th = thinkercap.next; th != &thinkercap; th = th->next)
		{
			if (th->function.acp1 != (actionf_p1)P_MobjThinker)
				continue;

			mo2 = (mobj_t *)th;

			if (mo2->type == MT_AXIS)
			{
				if (mo2->threshold == player->mare)
				{
					if (closestaxis == NULL)
					{
						closestaxis = mo2;
						dist2 = R_PointToDist2(newx, newy, mo2->x, mo2->y)-mo2->radius;
					}
					else
					{
						dist1 = R_PointToDist2(newx, newy, mo2->x, mo2->y)-mo2->radius;

						if (dist1 < dist2)
						{
							closestaxis = mo2;
							dist2 = dist1;
						}
					}
				}
			}
		}

		P_SetTarget(&player->mo->target, closestaxis);
	}

	if (!player->mo->target) // Uh-oh!
	{
		CONS_Printf("No axis points found!\n");
		return;
	}

	// The 'ambush' flag says you should rotate
	// the other way around the axis.
	if (player->mo->target->flags & MF_AMBUSH)
		backwardaxis = true;

	player->angle_pos = R_PointToAngle2(player->mo->target->x, player->mo->target->y, player->mo->x, player->mo->y);

	player->old_angle_pos = player->angle_pos;

	radius = player->mo->target->radius;

	player->mo->flags |= MF_NOGRAVITY;
	player->mo->flags2 |= MF2_DONTDRAW;
	P_SetScale(player->mo->tracer, player->mo->scale);

	// Check for flipped 'gravity'
	{
		boolean no3dfloorgrav = true; // Custom gravity

		if (player->playerstate != PST_DEAD)
			player->mo->eflags &= ~MFE_VERTICALFLIP;

		if (player->mo->subsector->sector->ffloors) // Check for 3D floor gravity too.
		{
			ffloor_t *rover;

			for (rover = player->mo->subsector->sector->ffloors; rover; rover = rover->next)
			{
				if (!(rover->flags & FF_EXISTS))
					continue;

				if (P_InsideANonSolidFFloor(player->mo, rover)) // SRB2CBTODO: Make all FOF stuff have functions like this!!!
				{
					if (rover->master->frontsector->gravity)
					{
						if (rover->master->frontsector->verticalflip)
						{
							if (player->playerstate != PST_DEAD)
								player->mo->eflags |= MFE_VERTICALFLIP;
						}

						no3dfloorgrav = false;
						break;
					}
				}
			}
		}

		if (no3dfloorgrav)
		{
			if (player->mo->subsector->sector->verticalflip)
			{
				if (player->playerstate != PST_DEAD)
					player->mo->eflags |= MFE_VERTICALFLIP;
			}
		}
	}

	if (player->mo->eflags & MFE_VERTICALFLIP)
		player->mo->tracer->eflags |= MFE_VERTICALFLIP;
	else
		player->mo->tracer->eflags &= ~MFE_VERTICALFLIP;

	if (player->mo->eflags & MFE_VERTICALFLIP)
		cmd->forwardmove = (char)(-cmd->forwardmove);

	// Currently reeling from being hit.
	if (player->powers[pw_flashing] > (2*flashingtics)/3)
	{
		{
			const angle_t fa = FINEANGLE_C(player->flyangle);
			const fixed_t speed = FixedDiv(player->speed*FRACUNIT,50*FRACUNIT);

			xspeed = FixedMul(FINECOSINE(fa),speed);
			yspeed = FixedMul(FINESINE(fa),speed);
		}

		if (!(player->pflags & PF_TRANSFERTOCLOSEST))
		{
			xspeed = FixedMul(xspeed, FixedDiv(1024*FRACUNIT, player->mo->target->radius));

			if (backwardaxis)
				xspeed *= -1;

			player->angle_pos += FixedAngleC(FixedDiv(xspeed,5*FRACUNIT),40*FRACUNIT)*NEWTICRATERATIO;
		}

		if (player->pflags & PF_TRANSFERTOCLOSEST)
		{
			const angle_t fa = R_PointToAngle2(player->axis1->x, player->axis1->y, player->axis2->x, player->axis2->y);
			P_InstaThrust(player->mo, fa, xspeed/10);
		}
		else
		{
			const angle_t fa = player->angle_pos>>ANGLETOFINESHIFT;

			player->mo->momx = player->mo->target->x + FixedMul(FINECOSINE(fa),radius) - player->mo->x;
			player->mo->momy = player->mo->target->y + FixedMul(FINESINE(fa),radius) - player->mo->y;
		}

		player->mo->momz = 0;

		P_NightsTransferPoints(player, xspeed, radius);

		P_UnsetThingPosition(player->mo->tracer);
		player->mo->tracer->x = player->mo->x;
		player->mo->tracer->y = player->mo->y;
		player->mo->tracer->z = player->mo->z;
		player->mo->tracer->floorz = player->mo->floorz;
		player->mo->tracer->ceilingz = player->mo->ceilingz;
		P_SetThingPosition(player->mo->tracer);
		return;
	}

	if (player->mo->tracer->state >= &states[S_SUPERTRANS1]
		&& player->mo->tracer->state <= &states[S_SUPERTRANS9])
	{
		player->mo->momx = player->mo->momy = player->mo->momz = 0;

		P_UnsetThingPosition(player->mo->tracer);
		player->mo->tracer->x = player->mo->x;
		player->mo->tracer->y = player->mo->y;
		player->mo->tracer->z = player->mo->z;
		player->mo->tracer->floorz = player->mo->floorz;
		player->mo->tracer->ceilingz = player->mo->ceilingz;
		P_SetThingPosition(player->mo->tracer);
		return;
	}

	if (player->exiting > 0 && player->exiting < 2*TICRATE)
	{
		player->mo->momx = player->mo->momy = 0;

		if (gametype != GT_RACE)
			player->mo->momz = 30*FRACUNIT;

		player->mo->tracer->angle += ANG45/4/NEWTICRATERATIO;

		if (!(player->mo->tracer->state  >= &states[S_NIGHTSDRONE1]
			&& player->mo->tracer->state <= &states[S_NIGHTSDRONE2]))
			P_SetMobjState(player->mo->tracer, S_NIGHTSDRONE1);

		player->mo->tracer->flags |= MF_NOCLIPHEIGHT;
		player->mo->flags |= MF_NOCLIPHEIGHT;

		P_UnsetThingPosition(player->mo->tracer);
		player->mo->tracer->x = player->mo->x;
		player->mo->tracer->y = player->mo->y;
		player->mo->tracer->z = player->mo->z;
		player->mo->tracer->floorz = player->mo->floorz;
		player->mo->tracer->ceilingz = player->mo->ceilingz;
		P_SetThingPosition(player->mo->tracer);
		return;
	}

	// Spawn the little sparkles on each side of the player.
	if (leveltime & 1*NEWTICRATERATIO)
	{
		mobj_t *firstmobj;
		mobj_t *secondmobj;
		fixed_t spawndist = FIXEDSCALE(16*FRACUNIT, player->mo->scale);

		firstmobj = P_SpawnMobj(player->mo->x + P_ReturnThrustX(player->mo, player->mo->angle+ANG90, spawndist), player->mo->y + P_ReturnThrustY(player->mo, player->mo->angle+ANG90, spawndist), player->mo->z + player->mo->height/2, MT_NIGHTSPARKLE);
		secondmobj = P_SpawnMobj(player->mo->x + P_ReturnThrustX(player->mo, player->mo->angle-ANG90, spawndist), player->mo->y + P_ReturnThrustY(player->mo, player->mo->angle-ANG90, spawndist), player->mo->z + player->mo->height/2, MT_NIGHTSPARKLE);

		firstmobj->fuse = leveltime;
		P_SetTarget(&firstmobj->target, player->mo);
		P_SetScale(firstmobj, player->mo->scale);

		secondmobj->fuse = leveltime;
		P_SetTarget(&secondmobj->target, player->mo);
		P_SetScale(secondmobj, player->mo->scale);

		player->mo->fuse = leveltime;
	}

	if (player->bumpertime)
	{
		player->jumping = true;
		player->pflags |= PF_DRILLING;
	}
	else if (cmd->buttons & BT_JUMP && player->drillmeter && player->drilldelay == 0)
	{
		if (!player->jumping)
			firstdrill = true;

		player->jumping = true;
		player->pflags |= PF_DRILLING;
	}
	else
	{
		player->jumping = false;

		if (cmd->sidemove != 0)
			moved = true;

		if (player->drillmeter & 1)
			player->drillmeter++; // I'll be nice and give them one.
	}

	if (cmd->forwardmove != 0)
		moved = true;

	if (player->bumpertime)
		drillamt = 0;
	else if (moved)
	{
		if (player->pflags & PF_DRILLING)
		{
			drillamt += 50;
		}
		else
		{
			const int distabs = abs(cmd->forwardmove)*abs(cmd->forwardmove) + abs(cmd->sidemove)*abs(cmd->sidemove);
			const float distsqrt = (float)(sqrt(distabs));
			const int distance = (int)distsqrt;

			drillamt += distance > 50 ? 50 : distance;

			drillamt = (5*drillamt)/4;
		}
	}

	drillamt /=NEWTICRATERATIO;

	player->speed += drillamt;

	if (!player->bumpertime)
	{
		if (!(player->pflags & PF_DRILLING))
		{
			if (player->speed > MAXDRILLSPEED)
				player->speed -= 100+drillamt;
			else if (player->speed > MAXNORMALSPEED)
				player->speed -= (drillamt*19)/16;
		}
		else
		{
			player->speed += 75;
			if (player->speed > MAXDRILLSPEED)
				player->speed -= 100+drillamt;

			if (--player->drillmeter == 0)
				player->drilldelay = TICRATE*2;
		}
	}

	if (!player->bumpertime)
	{
		if (drillamt == 0 && player->speed > 0)
			player->speed -= 25;

		if (player->speed < 0)
			player->speed = 0;

		if (cmd->sidemove != 0)
		{
			// old, uglier way: newangle = (signed short)FixedMul(AngleFixed(R_PointToAngle2(0,0, cmd->sidemove*FRACUNIT, cmd->forwardmove*FRACUNIT)), 1);
			newangle = (signed short)(AngleFixed(R_PointToAngle2(0,0, cmd->sidemove*FRACUNIT, cmd->forwardmove*FRACUNIT))/FRACUNIT);
		}
		else if (cmd->forwardmove > 0)
			newangle = 90;
		else if (cmd->forwardmove < 0)
			newangle = 269;

		if (newangle < 0 && moved)
			newangle = (signed short)(360+newangle);
	}

	if (player->pflags & PF_DRILLING)
		thrustfactor = 1;
	else
		thrustfactor = 6;

	for (i = 0; i < thrustfactor; i++)
	{
		if (moved && player->flyangle != newangle)
		{
			// player->flyangle is the one to move
			// newangle is the "move to"
			if ((((newangle-player->flyangle)+360)%360)>(((player->flyangle-newangle)+360)%360))
			{
				player->flyangle--;
				if (player->flyangle < 0)
					player->flyangle = 360 + player->flyangle;
			}
			else
				player->flyangle++;
		}

		player->flyangle %= 360;
	}

	if (!(player->speed)
		&& cmd->forwardmove == 0)
		still = true;

	if ((cmd->buttons & BT_CAMLEFT) && (cmd->buttons & BT_CAMRIGHT))
	{
		if (!(player->pflags & PF_SKIDDOWN) && player->speed > 2000)
		{
			player->speed /= 10;
			S_StartSound(player->mo, sfx_ngskid);
		}
		player->pflags |= PF_SKIDDOWN;
	}
	else
		player->pflags &= ~PF_SKIDDOWN;

	{
		const angle_t fa = FINEANGLE_C(player->flyangle);
		const fixed_t speed = (player->speed*FRACUNIT)/50;
		xspeed = FixedMul(FINECOSINE(fa),speed);
		yspeed = FixedMul(FINESINE(fa),speed);
	}

	if (!(player->pflags & PF_TRANSFERTOCLOSEST))
	{
		xspeed = FixedMul(xspeed, FixedDiv(1024*FRACUNIT, player->mo->target->radius));

		if (backwardaxis)
			xspeed *= -1;

		player->angle_pos += FixedAngleC(FixedDiv(xspeed, 5*FRACUNIT), 40*FRACUNIT);
	}

	P_NightsTransferPoints(player, xspeed, radius);

	if (still)
		player->mo->momz = -FRACUNIT;
	else
		player->mo->momz = yspeed/11;

	if (player->mo->momz > 20*FRACUNIT)
		player->mo->momz = 20*FRACUNIT;
	else if (player->mo->momz < -20*FRACUNIT)
		player->mo->momz = -20*FRACUNIT;

	// You can create splashes as you fly across water.
	if (player->mo->z + P_GetPlayerHeight(player) >= player->mo->watertop
		&& player->mo->z <= player->mo->watertop && player->speed > 9000 && leveltime % (TICRATE/7) == 0 && !player->spectator)
	{
		P_Particles(player->mo, MT_PARTICLE, 255, FRACUNIT/2, 6*FRACUNIT, TICRATE, 1, false, 13, 6, 0);
		P_Particles(player->mo, MT_PARTICLE, 255, FRACUNIT  , 5*FRACUNIT , TICRATE, 1, false, 13, 6, 0);
		mobj_t *water = P_SpawnMobj(player->mo->x, player->mo->y, player->mo->watertop, MT_SPLISH);
		S_StartSound(water, sfx_wslap);
		water->destscale = player->mo->scale;
		P_SetScale(water, player->mo->scale);
	}



	// Spawn Sonic's bubbles
	if (player->mo->eflags & MFE_UNDERWATER && !player->spectator)
	{
		const fixed_t zh = player->mo->z + FixedDiv(player->mo->height,5*(FRACUNIT/4));
		mobj_t *bubble = NULL;
		if (!(P_Random() % 16))
			bubble = P_SpawnMobj(player->mo->x, player->mo->y, zh, MT_SMALLBUBBLE);
		else if (!(P_Random() % 96))
			bubble = P_SpawnMobj(player->mo->x, player->mo->y, zh, MT_MEDIUMBUBBLE);

		if (bubble)
		{
			bubble->threshold = 42;
			bubble->destscale = player->mo->scale;
			P_SetScale(bubble,player->mo->scale);
		}
	}

	if (player->mo->momx || player->mo->momy)
		player->mo->angle = R_PointToAngle2(0, 0, player->mo->momx, player->mo->momy);

	if (still)
	{
		player->anotherflyangle = 0;
		movingangle = 0;
	}
	else if (backwardaxis)
	{
		// Special cases to prevent the angle from being
		// calculated incorrectly when wrapped.
		if (player->old_angle_pos > ANGLE_350 && player->angle_pos < ANGLE_10)
		{
			movingangle = R_PointToAngle2(0, player->mo->z, -R_PointToDist2(player->mo->momx, player->mo->momy, 0, 0), player->mo->z + player->mo->momz);
			player->anotherflyangle = (movingangle >> ANGLETOFINESHIFT) * 360/FINEANGLES;
		}
		else if (player->old_angle_pos < ANGLE_10 && player->angle_pos > ANGLE_350)
		{
			movingangle = R_PointToAngle2(0, player->mo->z, R_PointToDist2(player->mo->momx, player->mo->momy, 0, 0), player->mo->z + player->mo->momz);
			player->anotherflyangle = (movingangle >> ANGLETOFINESHIFT) * 360/FINEANGLES;
		}
		else if (player->angle_pos > player->old_angle_pos)
		{
			movingangle = R_PointToAngle2(0, player->mo->z, -R_PointToDist2(player->mo->momx, player->mo->momy, 0, 0), player->mo->z + player->mo->momz);
			player->anotherflyangle = (movingangle >> ANGLETOFINESHIFT) * 360/FINEANGLES;
		}
		else
		{
			movingangle = R_PointToAngle2(0, player->mo->z, R_PointToDist2(player->mo->momx, player->mo->momy, 0, 0), player->mo->z + player->mo->momz);
			player->anotherflyangle = (movingangle >> ANGLETOFINESHIFT) * 360/FINEANGLES;
		}
	}
	else
	{
		// Special cases to prevent the angle from being
		// calculated incorrectly when wrapped.
		if (player->old_angle_pos > ANGLE_350 && player->angle_pos < ANGLE_10)
		{
			movingangle = R_PointToAngle2(0, player->mo->z, R_PointToDist2(player->mo->momx, player->mo->momy, 0, 0), player->mo->z + player->mo->momz);
			player->anotherflyangle = (movingangle >> ANGLETOFINESHIFT) * 360/FINEANGLES;
		}
		else if (player->old_angle_pos < ANGLE_10 && player->angle_pos > ANGLE_350)
		{
			movingangle = R_PointToAngle2(0, player->mo->z, -R_PointToDist2(player->mo->momx, player->mo->momy, 0, 0), player->mo->z + player->mo->momz);
			player->anotherflyangle = (movingangle >> ANGLETOFINESHIFT) * 360/FINEANGLES;
		}
		else if (player->angle_pos < player->old_angle_pos)
		{
			movingangle = R_PointToAngle2(0, player->mo->z, -R_PointToDist2(player->mo->momx, player->mo->momy, 0, 0), player->mo->z + player->mo->momz);
			player->anotherflyangle = (movingangle >> ANGLETOFINESHIFT) * 360/FINEANGLES;
		}
		else
		{
			movingangle = R_PointToAngle2(0, player->mo->z, R_PointToDist2(player->mo->momx, player->mo->momy, 0, 0), player->mo->z + player->mo->momz);
			player->anotherflyangle = (movingangle >> ANGLETOFINESHIFT) * 360/FINEANGLES;
		}
	}

	if (player->mo->eflags & MFE_VERTICALFLIP)
	{
		if (player->anotherflyangle >= 349 || player->anotherflyangle <= 11)
		{
			if (player->pflags & PF_DRILLING)
			{
				if (!(player->mo->tracer->state >= &states[S_NIGHTSDRILL1A]
					&& player->mo->tracer->state <= &states[S_NIGHTSDRILL1D]))
				{
					if (!(player->mo->tracer->state >= &states[S_NIGHTSFLY1A]
						&& player->mo->tracer->state <= &states[S_NIGHTSFLY9B]))
					{
						int framenum;

						framenum = player->mo->tracer->state->frame & 3;

						if (framenum == 3) // Drilld special case
							P_SetMobjStateNF(player->mo->tracer, S_NIGHTSDRILL1A);
						else
							P_SetMobjStateNF(player->mo->tracer, S_NIGHTSDRILL1B+framenum);
					}
					else
						P_SetMobjStateNF(player->mo->tracer, S_NIGHTSDRILL1A);
				}
			}
			else
				P_SetMobjStateNF(player->mo->tracer, leveltime & 1*NEWTICRATERATIO? S_NIGHTSFLY1A : S_NIGHTSFLY1B);
		}
		else if (player->anotherflyangle >= 12 && player->anotherflyangle <= 33)
		{
			if (player->pflags & PF_DRILLING)
			{
				if (!(player->mo->tracer->state >= &states[S_NIGHTSDRILL6A]
					&& player->mo->tracer->state <= &states[S_NIGHTSDRILL6D]))
				{
					if (!(player->mo->tracer->state >= &states[S_NIGHTSFLY1A]
						&& player->mo->tracer->state <= &states[S_NIGHTSFLY9B]))
					{
						int framenum;

						framenum = player->mo->tracer->state->frame & 3;

						if (framenum == 3) // Drilld special case
							P_SetMobjStateNF(player->mo->tracer, S_NIGHTSDRILL6A);
						else
							P_SetMobjStateNF(player->mo->tracer, S_NIGHTSDRILL6B+framenum);
					}
					else
						P_SetMobjStateNF(player->mo->tracer, S_NIGHTSDRILL6A);
				}
			}
			else
				P_SetMobjStateNF(player->mo->tracer, leveltime & 1*NEWTICRATERATIO? S_NIGHTSFLY2A : S_NIGHTSFLY2B);
		}
		else if (player->anotherflyangle >= 34 && player->anotherflyangle <= 56)
		{
			if (player->pflags & PF_DRILLING)
			{
				if (!(player->mo->tracer->state >= &states[S_NIGHTSDRILL7A]
					&& player->mo->tracer->state <= &states[S_NIGHTSDRILL7D]))
				{
					if (!(player->mo->tracer->state >= &states[S_NIGHTSFLY1A]
						&& player->mo->tracer->state <= &states[S_NIGHTSFLY9B]))
					{
						int framenum;

						framenum = player->mo->tracer->state->frame & 3;

						if (framenum == 3) // Drilld special case
							P_SetMobjStateNF(player->mo->tracer, S_NIGHTSDRILL7A);
						else
							P_SetMobjStateNF(player->mo->tracer, S_NIGHTSDRILL7B+framenum);
					}
					else
						P_SetMobjStateNF(player->mo->tracer, S_NIGHTSDRILL7A);
				}
			}
			else
				P_SetMobjStateNF(player->mo->tracer, leveltime & 1*NEWTICRATERATIO? S_NIGHTSFLY7A : S_NIGHTSFLY7B);
		}
		else if (player->anotherflyangle >= 57 && player->anotherflyangle <= 79)
		{
			if (player->pflags & PF_DRILLING)
			{
				if (!(player->mo->tracer->state >= &states[S_NIGHTSDRILL8A]
					&& player->mo->tracer->state <= &states[S_NIGHTSDRILL8D]))
				{
					if (!(player->mo->tracer->state >= &states[S_NIGHTSFLY1A]
						&& player->mo->tracer->state <= &states[S_NIGHTSFLY9B]))
					{
						int framenum;

						framenum = player->mo->tracer->state->frame & 3;

						if (framenum == 3) // Drilld special case
							P_SetMobjStateNF(player->mo->tracer, S_NIGHTSDRILL8A);
						else
							P_SetMobjStateNF(player->mo->tracer, S_NIGHTSDRILL8B+framenum);
					}
					else
						P_SetMobjStateNF(player->mo->tracer, S_NIGHTSDRILL8A);
				}
			}
			else
				P_SetMobjStateNF(player->mo->tracer, leveltime & 1*NEWTICRATERATIO? S_NIGHTSFLY8A : S_NIGHTSFLY8B);
		}
		else if (player->anotherflyangle >= 80 && player->anotherflyangle <= 101)
		{
			if (player->pflags & PF_DRILLING)
			{
				if (!(player->mo->tracer->state >= &states[S_NIGHTSDRILL9A]
					&& player->mo->tracer->state <= &states[S_NIGHTSDRILL9D]))
				{
					if (!(player->mo->tracer->state >= &states[S_NIGHTSFLY1A]
						&& player->mo->tracer->state <= &states[S_NIGHTSFLY9B]))
					{
						int framenum;

						framenum = player->mo->tracer->state->frame & 3;

						if (framenum == 3) // Drilld special case
							P_SetMobjStateNF(player->mo->tracer, S_NIGHTSDRILL9A);
						else
							P_SetMobjStateNF(player->mo->tracer, S_NIGHTSDRILL9B+framenum);
					}
					else
						P_SetMobjStateNF(player->mo->tracer, S_NIGHTSDRILL9A);
				}
			}
			else
				P_SetMobjStateNF(player->mo->tracer, leveltime & 1*NEWTICRATERATIO? S_NIGHTSFLY9A : S_NIGHTSFLY9B);
		}
		else if (player->anotherflyangle >= 102 && player->anotherflyangle <= 123)
		{
			if (player->pflags & PF_DRILLING)
			{
				if (!(player->mo->tracer->state >= &states[S_NIGHTSDRILL8A]
					&& player->mo->tracer->state <= &states[S_NIGHTSDRILL8D]))
				{
					if (!(player->mo->tracer->state >= &states[S_NIGHTSFLY1A]
						&& player->mo->tracer->state <= &states[S_NIGHTSFLY9B]))
					{
						int framenum;

						framenum = player->mo->tracer->state->frame & 3;

						if (framenum == 3) // Drilld special case
							P_SetMobjStateNF(player->mo->tracer, S_NIGHTSDRILL8A);
						else
							P_SetMobjStateNF(player->mo->tracer, S_NIGHTSDRILL8B+framenum);
					}
					else
						P_SetMobjStateNF(player->mo->tracer, S_NIGHTSDRILL8A);
				}
			}
			else
				P_SetMobjStateNF(player->mo->tracer, leveltime & 1*NEWTICRATERATIO? S_NIGHTSFLY8A : S_NIGHTSFLY8B);
		}
		else if (player->anotherflyangle >= 124 && player->anotherflyangle <= 146)
		{
			if (player->pflags & PF_DRILLING)
			{
				if (!(player->mo->tracer->state >= &states[S_NIGHTSDRILL7A]
					&& player->mo->tracer->state <= &states[S_NIGHTSDRILL7D]))
				{
					if (!(player->mo->tracer->state >= &states[S_NIGHTSFLY1A]
						&& player->mo->tracer->state <= &states[S_NIGHTSFLY9B]))
					{
						int framenum;

						framenum = player->mo->tracer->state->frame & 3;

						if (framenum == 3) // Drilld special case
							P_SetMobjStateNF(player->mo->tracer, S_NIGHTSDRILL7A);
						else
							P_SetMobjStateNF(player->mo->tracer, S_NIGHTSDRILL7B+framenum);
					}
					else
						P_SetMobjStateNF(player->mo->tracer, S_NIGHTSDRILL7A);
				}
			}
			else
				P_SetMobjStateNF(player->mo->tracer, leveltime & 1*NEWTICRATERATIO? S_NIGHTSFLY7A : S_NIGHTSFLY7B);
		}
		else if (player->anotherflyangle >= 147 && player->anotherflyangle <= 168)
		{
			if (player->pflags & PF_DRILLING)
			{
				if (!(player->mo->tracer->state >= &states[S_NIGHTSDRILL6A]
					&& player->mo->tracer->state <= &states[S_NIGHTSDRILL6D]))
				{
					if (!(player->mo->tracer->state >= &states[S_NIGHTSFLY1A]
						&& player->mo->tracer->state <= &states[S_NIGHTSFLY9B]))
					{
						int framenum;

						framenum = player->mo->tracer->state->frame & 3;

						if (framenum == 3) // Drilld special case
							P_SetMobjStateNF(player->mo->tracer, S_NIGHTSDRILL6A);
						else
							P_SetMobjStateNF(player->mo->tracer, S_NIGHTSDRILL6B+framenum);
					}
					else
						P_SetMobjStateNF(player->mo->tracer, S_NIGHTSDRILL6A);
				}
			}
			else
				P_SetMobjStateNF(player->mo->tracer, leveltime & 1*NEWTICRATERATIO? S_NIGHTSFLY6A : S_NIGHTSFLY6B);
		}
		else if (player->anotherflyangle >= 169 && player->anotherflyangle <= 191)
		{
			if (player->pflags & PF_DRILLING)
			{
				if (!(player->mo->tracer->state >= &states[S_NIGHTSDRILL1A]
					&& player->mo->tracer->state <= &states[S_NIGHTSDRILL1D]))
				{
					if (!(player->mo->tracer->state >= &states[S_NIGHTSFLY1A]
						&& player->mo->tracer->state <= &states[S_NIGHTSFLY9B]))
					{
						int framenum;

						framenum = player->mo->tracer->state->frame & 3;

						if (framenum == 3) // Drilld special case
							P_SetMobjStateNF(player->mo->tracer, S_NIGHTSDRILL1A);
						else
							P_SetMobjStateNF(player->mo->tracer, S_NIGHTSDRILL1B+framenum);
					}
					else
						P_SetMobjStateNF(player->mo->tracer, S_NIGHTSDRILL1A);
				}
			}
			else
				P_SetMobjStateNF(player->mo->tracer, leveltime & 1*NEWTICRATERATIO? S_NIGHTSFLY1A : S_NIGHTSFLY1B);
		}
		else if (player->anotherflyangle >= 192 && player->anotherflyangle <= 213)
		{
			if (player->pflags & PF_DRILLING)
			{
				if (!(player->mo->tracer->state >= &states[S_NIGHTSDRILL2A]
					&& player->mo->tracer->state <= &states[S_NIGHTSDRILL2D]))
				{
					if (!(player->mo->tracer->state >= &states[S_NIGHTSFLY1A]
						&& player->mo->tracer->state <= &states[S_NIGHTSFLY9B]))
					{
						int framenum;

						framenum = player->mo->tracer->state->frame & 3;

						if (framenum == 3) // Drilld special case
							P_SetMobjStateNF(player->mo->tracer, S_NIGHTSDRILL2A);
						else
							P_SetMobjStateNF(player->mo->tracer, S_NIGHTSDRILL2B+framenum);
					}
					else
						P_SetMobjStateNF(player->mo->tracer, S_NIGHTSDRILL2A);
				}
			}
			else
				P_SetMobjStateNF(player->mo->tracer, leveltime & 1*NEWTICRATERATIO? S_NIGHTSFLY2A : S_NIGHTSFLY2B);
		}
		else if (player->anotherflyangle >= 214 && player->anotherflyangle <= 236)
		{
			if (player->pflags & PF_DRILLING)
			{
				if (!(player->mo->tracer->state >= &states[S_NIGHTSDRILL3A]
					&& player->mo->tracer->state <= &states[S_NIGHTSDRILL3D]))
				{
					if (!(player->mo->tracer->state >= &states[S_NIGHTSFLY1A]
						&& player->mo->tracer->state <= &states[S_NIGHTSFLY9B]))
					{
						int framenum;

						framenum = player->mo->tracer->state->frame & 3;

						if (framenum == 3) // Drilld special case
							P_SetMobjStateNF(player->mo->tracer, S_NIGHTSDRILL3A);
						else
							P_SetMobjStateNF(player->mo->tracer, S_NIGHTSDRILL3B+framenum);
					}
					else
						P_SetMobjStateNF(player->mo->tracer, S_NIGHTSDRILL3A);
				}
			}
			else
				P_SetMobjStateNF(player->mo->tracer, leveltime & 1*NEWTICRATERATIO? S_NIGHTSFLY3A : S_NIGHTSFLY3B);
		}
		else if (player->anotherflyangle >= 237 && player->anotherflyangle <= 258)
		{
			if (player->pflags & PF_DRILLING)
			{
				if (!(player->mo->tracer->state >= &states[S_NIGHTSDRILL4A]
					&& player->mo->tracer->state <= &states[S_NIGHTSDRILL4D]))
				{
					if (!(player->mo->tracer->state >= &states[S_NIGHTSFLY1A]
						&& player->mo->tracer->state <= &states[S_NIGHTSFLY9B]))
					{
						int framenum;

						framenum = player->mo->tracer->state->frame & 3;

						if (framenum == 3) // Drilld special case
							P_SetMobjStateNF(player->mo->tracer, S_NIGHTSDRILL4A);
						else
							P_SetMobjStateNF(player->mo->tracer, S_NIGHTSDRILL4B+framenum);
					}
					else
						P_SetMobjStateNF(player->mo->tracer, S_NIGHTSDRILL4A);
				}
			}
			else
				P_SetMobjStateNF(player->mo->tracer, leveltime & 1*NEWTICRATERATIO? S_NIGHTSFLY4A : S_NIGHTSFLY4B);
		}
		else if (player->anotherflyangle >= 259 && player->anotherflyangle <= 281)
		{
			if (player->pflags & PF_DRILLING)
			{
				if (!(player->mo->tracer->state >= &states[S_NIGHTSDRILL5A]
					&& player->mo->tracer->state <= &states[S_NIGHTSDRILL5D]))
				{
					if (!(player->mo->tracer->state >= &states[S_NIGHTSFLY1A]
						&& player->mo->tracer->state <= &states[S_NIGHTSFLY9B]))
					{
						int framenum;

						framenum = player->mo->tracer->state->frame & 3;

						if (framenum == 3) // Drilld special case
							P_SetMobjStateNF(player->mo->tracer, S_NIGHTSDRILL5A);
						else
							P_SetMobjStateNF(player->mo->tracer, S_NIGHTSDRILL5B+framenum);
					}
					else
						P_SetMobjStateNF(player->mo->tracer, S_NIGHTSDRILL5A);
				}
			}
			else
				P_SetMobjStateNF(player->mo->tracer, leveltime & 1*NEWTICRATERATIO? S_NIGHTSFLY5A : S_NIGHTSFLY5B);
		}
		else if (player->anotherflyangle >= 282 && player->anotherflyangle <= 304)
		{
			if (player->pflags & PF_DRILLING)
			{
				if (!(player->mo->tracer->state >= &states[S_NIGHTSDRILL4A]
					&& player->mo->tracer->state <= &states[S_NIGHTSDRILL4D]))
				{
					if (!(player->mo->tracer->state >= &states[S_NIGHTSFLY1A]
						&& player->mo->tracer->state <= &states[S_NIGHTSFLY9B]))
					{
						int framenum;

						framenum = player->mo->tracer->state->frame & 3;

						if (framenum == 3) // Drilld special case
							P_SetMobjStateNF(player->mo->tracer, S_NIGHTSDRILL4A);
						else
							P_SetMobjStateNF(player->mo->tracer, S_NIGHTSDRILL4B+framenum);
					}
					else
						P_SetMobjStateNF(player->mo->tracer, S_NIGHTSDRILL4A);
				}
			}
			else
				P_SetMobjStateNF(player->mo->tracer, leveltime & 1*NEWTICRATERATIO? S_NIGHTSFLY4A : S_NIGHTSFLY4B);
		}
		else if (player->anotherflyangle >= 305 && player->anotherflyangle <= 326)
		{
			if (player->pflags & PF_DRILLING)
			{
				if (!(player->mo->tracer->state >= &states[S_NIGHTSDRILL3A]
					&& player->mo->tracer->state <= &states[S_NIGHTSDRILL3D]))
				{
					if (!(player->mo->tracer->state >= &states[S_NIGHTSFLY1A]
						&& player->mo->tracer->state <= &states[S_NIGHTSFLY9B]))
					{
						int framenum;

						framenum = player->mo->tracer->state->frame & 3;

						if (framenum == 3) // Drilld special case
							P_SetMobjStateNF(player->mo->tracer, S_NIGHTSDRILL3A);
						else
							P_SetMobjStateNF(player->mo->tracer, S_NIGHTSDRILL3B+framenum);
					}
					else
						P_SetMobjStateNF(player->mo->tracer, S_NIGHTSDRILL3A);
				}
			}
			else
				P_SetMobjStateNF(player->mo->tracer, leveltime & 1*NEWTICRATERATIO? S_NIGHTSFLY3A : S_NIGHTSFLY3B);
		}
		else if (player->anotherflyangle >= 327 && player->anotherflyangle <= 348)
		{
			if (player->pflags & PF_DRILLING)
			{
				if (!(player->mo->tracer->state >= &states[S_NIGHTSDRILL2A]
					&& player->mo->tracer->state <= &states[S_NIGHTSDRILL2D]))
				{
					if (!(player->mo->tracer->state >= &states[S_NIGHTSFLY1A]
						&& player->mo->tracer->state <= &states[S_NIGHTSFLY9B]))
					{
						int framenum;

						framenum = player->mo->tracer->state->frame & 3;

						if (framenum == 3) // Drilld special case
							P_SetMobjStateNF(player->mo->tracer, S_NIGHTSDRILL2A);
						else
							P_SetMobjStateNF(player->mo->tracer, S_NIGHTSDRILL2B+framenum);
					}
					else
						P_SetMobjStateNF(player->mo->tracer, S_NIGHTSDRILL2A);
				}
			}
			else
				P_SetMobjStateNF(player->mo->tracer, leveltime & 1*NEWTICRATERATIO ? S_NIGHTSFLY2A : S_NIGHTSFLY2B);
		}
	}
	/////////////////////////////////////////////////////
	////////////////////////////////////////////////////////
	////////////////////////////////////////////////////////
	//////////////////////////////////////////////////////
	else
	{
		if (player->anotherflyangle >= 349 || player->anotherflyangle <= 11)
		{
			if (player->pflags & PF_DRILLING)
			{
				if (!(player->mo->tracer->state >= &states[S_NIGHTSDRILL1A]
					&& player->mo->tracer->state <= &states[S_NIGHTSDRILL1D]))
				{
					if (!(player->mo->tracer->state >= &states[S_NIGHTSFLY1A]
						&& player->mo->tracer->state <= &states[S_NIGHTSFLY9B]))
					{
						int framenum;

						framenum = player->mo->tracer->state->frame & 3;

						if (framenum == 3) // Drilld special case
							P_SetMobjStateNF(player->mo->tracer, S_NIGHTSDRILL1A);
						else
							P_SetMobjStateNF(player->mo->tracer, S_NIGHTSDRILL1B+framenum);
					}
					else
						P_SetMobjStateNF(player->mo->tracer, S_NIGHTSDRILL1A);
				}
			}
			else
				P_SetMobjStateNF(player->mo->tracer, leveltime & 1*NEWTICRATERATIO ? S_NIGHTSFLY1A : S_NIGHTSFLY1B);
		}
		else if (player->anotherflyangle >= 12 && player->anotherflyangle <= 33)
		{
			if (player->pflags & PF_DRILLING)
			{
				if (!(player->mo->tracer->state >= &states[S_NIGHTSDRILL2A]
					&& player->mo->tracer->state <= &states[S_NIGHTSDRILL2D]))
				{
					if (!(player->mo->tracer->state >= &states[S_NIGHTSFLY1A]
						&& player->mo->tracer->state <= &states[S_NIGHTSFLY9B]))
					{
						int framenum;

						framenum = player->mo->tracer->state->frame & 3;

						if (framenum == 3) // Drilld special case
							P_SetMobjStateNF(player->mo->tracer, S_NIGHTSDRILL2A);
						else
							P_SetMobjStateNF(player->mo->tracer, S_NIGHTSDRILL2B+framenum);
					}
					else
						P_SetMobjStateNF(player->mo->tracer, S_NIGHTSDRILL2A);
				}
			}
			else
				P_SetMobjStateNF(player->mo->tracer, leveltime & 1*NEWTICRATERATIO ? S_NIGHTSFLY2A : S_NIGHTSFLY2B);
		}
		else if (player->anotherflyangle >= 34 && player->anotherflyangle <= 56)
		{
			if (player->pflags & PF_DRILLING)
			{
				if (!(player->mo->tracer->state >= &states[S_NIGHTSDRILL3A]
					&& player->mo->tracer->state <= &states[S_NIGHTSDRILL3D]))
				{
					if (!(player->mo->tracer->state >= &states[S_NIGHTSFLY1A]
						&& player->mo->tracer->state <= &states[S_NIGHTSFLY9B]))
					{
						int framenum;

						framenum = player->mo->tracer->state->frame & 3;

						if (framenum == 3) // Drilld special case
							P_SetMobjStateNF(player->mo->tracer, S_NIGHTSDRILL3A);
						else
							P_SetMobjStateNF(player->mo->tracer, S_NIGHTSDRILL3B+framenum);
					}
					else
						P_SetMobjStateNF(player->mo->tracer, S_NIGHTSDRILL3A);
				}
			}
			else
				P_SetMobjStateNF(player->mo->tracer, leveltime & 1*NEWTICRATERATIO ? S_NIGHTSFLY3A : S_NIGHTSFLY3B);
		}
		else if (player->anotherflyangle >= 57 && player->anotherflyangle <= 79)
		{
			if (player->pflags & PF_DRILLING)
			{
				if (!(player->mo->tracer->state >= &states[S_NIGHTSDRILL4A]
					&& player->mo->tracer->state <= &states[S_NIGHTSDRILL4D]))
				{
					if (!(player->mo->tracer->state >= &states[S_NIGHTSFLY1A]
						&& player->mo->tracer->state <= &states[S_NIGHTSFLY9B]))
					{
						int framenum;

						framenum = player->mo->tracer->state->frame & 3;

						if (framenum == 3) // Drilld special case
							P_SetMobjStateNF(player->mo->tracer, S_NIGHTSDRILL4A);
						else
							P_SetMobjStateNF(player->mo->tracer, S_NIGHTSDRILL4B+framenum);
					}
					else
						P_SetMobjStateNF(player->mo->tracer, S_NIGHTSDRILL4A);
				}
			}
			else
				P_SetMobjStateNF(player->mo->tracer, leveltime & 1*NEWTICRATERATIO ? S_NIGHTSFLY4A : S_NIGHTSFLY4B);
		}
		else if (player->anotherflyangle >= 80 && player->anotherflyangle <= 101)
		{
			if (player->pflags & PF_DRILLING)
			{
				if (!(player->mo->tracer->state >= &states[S_NIGHTSDRILL5A]
					&& player->mo->tracer->state <= &states[S_NIGHTSDRILL5D]))
				{
					if (!(player->mo->tracer->state >= &states[S_NIGHTSFLY1A]
						&& player->mo->tracer->state <= &states[S_NIGHTSFLY9B]))
					{
						int framenum;

						framenum = player->mo->tracer->state->frame & 3;

						if (framenum == 3) // Drilld special case
							P_SetMobjStateNF(player->mo->tracer, S_NIGHTSDRILL5A);
						else
							P_SetMobjStateNF(player->mo->tracer, S_NIGHTSDRILL5B+framenum);
					}
					else
						P_SetMobjStateNF(player->mo->tracer, S_NIGHTSDRILL5A);
				}
			}
			else
				P_SetMobjStateNF(player->mo->tracer, leveltime & 1*NEWTICRATERATIO ? S_NIGHTSFLY5A : S_NIGHTSFLY5B);
		}
		else if (player->anotherflyangle >= 102 && player->anotherflyangle <= 123)
		{
			if (player->pflags & PF_DRILLING)
			{
				if (!(player->mo->tracer->state >= &states[S_NIGHTSDRILL4A]
					&& player->mo->tracer->state <= &states[S_NIGHTSDRILL4D]))
				{
					if (!(player->mo->tracer->state >= &states[S_NIGHTSFLY1A]
						&& player->mo->tracer->state <= &states[S_NIGHTSFLY9B]))
					{
						int framenum;

						framenum = player->mo->tracer->state->frame & 3;

						if (framenum == 3) // Drilld special case
							P_SetMobjStateNF(player->mo->tracer, S_NIGHTSDRILL4A);
						else
							P_SetMobjStateNF(player->mo->tracer, S_NIGHTSDRILL4B+framenum);
					}
					else
						P_SetMobjStateNF(player->mo->tracer, S_NIGHTSDRILL4A);
				}
			}
			else
				P_SetMobjStateNF(player->mo->tracer, leveltime & 1*NEWTICRATERATIO ? S_NIGHTSFLY4A : S_NIGHTSFLY4B);
		}
		else if (player->anotherflyangle >= 124 && player->anotherflyangle <= 146)
		{
			if (player->pflags & PF_DRILLING)
			{
				if (!(player->mo->tracer->state >= &states[S_NIGHTSDRILL3A]
					&& player->mo->tracer->state <= &states[S_NIGHTSDRILL3D]))
				{
					if (!(player->mo->tracer->state >= &states[S_NIGHTSFLY1A]
						&& player->mo->tracer->state <= &states[S_NIGHTSFLY9B]))
					{
						int framenum;

						framenum = player->mo->tracer->state->frame & 3;

						if (framenum == 3) // Drilld special case
							P_SetMobjStateNF(player->mo->tracer, S_NIGHTSDRILL3A);
						else
							P_SetMobjStateNF(player->mo->tracer, S_NIGHTSDRILL3B+framenum);
					}
					else
						P_SetMobjStateNF(player->mo->tracer, S_NIGHTSDRILL3A);
				}
			}
			else
				P_SetMobjStateNF(player->mo->tracer, leveltime & 1*NEWTICRATERATIO ? S_NIGHTSFLY3A : S_NIGHTSFLY3B);
		}
		else if (player->anotherflyangle >= 147 && player->anotherflyangle <= 168)
		{
			if (player->pflags & PF_DRILLING)
			{
				if (!(player->mo->tracer->state >= &states[S_NIGHTSDRILL2A]
					&& player->mo->tracer->state <= &states[S_NIGHTSDRILL2D]))
				{
					if (!(player->mo->tracer->state >= &states[S_NIGHTSFLY1A]
						&& player->mo->tracer->state <= &states[S_NIGHTSFLY9B]))
					{
						int framenum;

						framenum = player->mo->tracer->state->frame & 3;

						if (framenum == 3) // Drilld special case
							P_SetMobjStateNF(player->mo->tracer, S_NIGHTSDRILL2A);
						else
							P_SetMobjStateNF(player->mo->tracer, S_NIGHTSDRILL2B+framenum);
					}
					else
						P_SetMobjStateNF(player->mo->tracer, S_NIGHTSDRILL2A);
				}
			}
			else
				P_SetMobjStateNF(player->mo->tracer, leveltime & 1*NEWTICRATERATIO ? S_NIGHTSFLY2A : S_NIGHTSFLY2B);
		}
		else if (player->anotherflyangle >= 169 && player->anotherflyangle <= 191)
		{
			if (player->pflags & PF_DRILLING)
			{
				if (!(player->mo->tracer->state >= &states[S_NIGHTSDRILL1A]
					&& player->mo->tracer->state <= &states[S_NIGHTSDRILL1D]))
				{
					if (!(player->mo->tracer->state >= &states[S_NIGHTSFLY1A]
						&& player->mo->tracer->state <= &states[S_NIGHTSFLY9B]))
					{
						int framenum;

						framenum = player->mo->tracer->state->frame & 3;

						if (framenum == 3) // Drilld special case
							P_SetMobjStateNF(player->mo->tracer, S_NIGHTSDRILL1A);
						else
							P_SetMobjStateNF(player->mo->tracer, S_NIGHTSDRILL1B+framenum);
					}
					else
						P_SetMobjStateNF(player->mo->tracer, S_NIGHTSDRILL1A);
				}
			}
			else
				P_SetMobjStateNF(player->mo->tracer, leveltime & 1*NEWTICRATERATIO ? S_NIGHTSFLY1A : S_NIGHTSFLY1B);
		}
		else if (player->anotherflyangle >= 192 && player->anotherflyangle <= 213)
		{
			if (player->pflags & PF_DRILLING)
			{
				if (!(player->mo->tracer->state >= &states[S_NIGHTSDRILL6A]
					&& player->mo->tracer->state <= &states[S_NIGHTSDRILL6D]))
				{
					if (!(player->mo->tracer->state >= &states[S_NIGHTSFLY1A]
						&& player->mo->tracer->state <= &states[S_NIGHTSFLY9B]))
					{
						int framenum;

						framenum = player->mo->tracer->state->frame & 3;

						if (framenum == 3) // Drilld special case
							P_SetMobjStateNF(player->mo->tracer, S_NIGHTSDRILL6A);
						else
							P_SetMobjStateNF(player->mo->tracer, S_NIGHTSDRILL6B+framenum);
					}
					else
						P_SetMobjStateNF(player->mo->tracer, S_NIGHTSDRILL6A);
				}
			}
			else
				P_SetMobjStateNF(player->mo->tracer, leveltime & 1*NEWTICRATERATIO ? S_NIGHTSFLY6A : S_NIGHTSFLY6B);
		}
		else if (player->anotherflyangle >= 214 && player->anotherflyangle <= 236)
		{
			if (player->pflags & PF_DRILLING)
			{
				if (!(player->mo->tracer->state >= &states[S_NIGHTSDRILL7A]
					&& player->mo->tracer->state <= &states[S_NIGHTSDRILL7D]))
				{
					if (!(player->mo->tracer->state >= &states[S_NIGHTSFLY1A]
						&& player->mo->tracer->state <= &states[S_NIGHTSFLY9B]))
					{
						int framenum;

						framenum = player->mo->tracer->state->frame & 3;

						if (framenum == 3) // Drilld special case
							P_SetMobjStateNF(player->mo->tracer, S_NIGHTSDRILL7A);
						else
							P_SetMobjStateNF(player->mo->tracer, S_NIGHTSDRILL7B+framenum);
					}
					else
						P_SetMobjStateNF(player->mo->tracer, S_NIGHTSDRILL7A);
				}
			}
			else
				P_SetMobjStateNF(player->mo->tracer, leveltime & 1*NEWTICRATERATIO ? S_NIGHTSFLY7A : S_NIGHTSFLY7B);
		}
		else if (player->anotherflyangle >= 237 && player->anotherflyangle <= 258)
		{
			if (player->pflags & PF_DRILLING)
			{
				if (!(player->mo->tracer->state >= &states[S_NIGHTSDRILL8A]
					&& player->mo->tracer->state <= &states[S_NIGHTSDRILL8D]))
				{
					if (!(player->mo->tracer->state >= &states[S_NIGHTSFLY1A]
						&& player->mo->tracer->state <= &states[S_NIGHTSFLY9B]))
					{
						int framenum;

						framenum = player->mo->tracer->state->frame & 3;

						if (framenum == 3) // Drilld special case
							P_SetMobjStateNF(player->mo->tracer, S_NIGHTSDRILL8A);
						else
							P_SetMobjStateNF(player->mo->tracer, S_NIGHTSDRILL8B+framenum);
					}
					else
						P_SetMobjStateNF(player->mo->tracer, S_NIGHTSDRILL8A);
				}
			}
			else
				P_SetMobjStateNF(player->mo->tracer, leveltime & 1*NEWTICRATERATIO ? S_NIGHTSFLY8A : S_NIGHTSFLY8B);
		}
		else if (player->anotherflyangle >= 259 && player->anotherflyangle <= 281)
		{
			if (player->pflags & PF_DRILLING)
			{
				if (!(player->mo->tracer->state >= &states[S_NIGHTSDRILL9A]
					&& player->mo->tracer->state <= &states[S_NIGHTSDRILL9D]))
				{
					if (!(player->mo->tracer->state >= &states[S_NIGHTSFLY1A]
						&& player->mo->tracer->state <= &states[S_NIGHTSFLY9B]))
					{
						int framenum;

						framenum = player->mo->tracer->state->frame & 3;

						if (framenum == 3) // Drilld special case
							P_SetMobjStateNF(player->mo->tracer, S_NIGHTSDRILL9A);
						else
							P_SetMobjStateNF(player->mo->tracer, S_NIGHTSDRILL9B+framenum);
					}
					else
						P_SetMobjStateNF(player->mo->tracer, S_NIGHTSDRILL9A);
				}
			}
			else
				P_SetMobjStateNF(player->mo->tracer, leveltime & 1*NEWTICRATERATIO ? S_NIGHTSFLY9A : S_NIGHTSFLY9B);
		}
		else if (player->anotherflyangle >= 282 && player->anotherflyangle <= 304)
		{
			if (player->pflags & PF_DRILLING)
			{
				if (!(player->mo->tracer->state >= &states[S_NIGHTSDRILL8A]
					&& player->mo->tracer->state <= &states[S_NIGHTSDRILL8D]))
				{
					if (!(player->mo->tracer->state >= &states[S_NIGHTSFLY1A]
						&& player->mo->tracer->state <= &states[S_NIGHTSFLY9B]))
					{
						int framenum;

						framenum = player->mo->tracer->state->frame & 3;

						if (framenum == 3) // Drilld special case
							P_SetMobjStateNF(player->mo->tracer, S_NIGHTSDRILL8A);
						else
							P_SetMobjStateNF(player->mo->tracer, S_NIGHTSDRILL8B+framenum);
					}
					else
						P_SetMobjStateNF(player->mo->tracer, S_NIGHTSDRILL8A);
				}
			}
			else
				P_SetMobjStateNF(player->mo->tracer, leveltime & 1*NEWTICRATERATIO ? S_NIGHTSFLY8A : S_NIGHTSFLY8B);
		}
		else if (player->anotherflyangle >= 305 && player->anotherflyangle <= 326)
		{
			if (player->pflags & PF_DRILLING)
			{
				if (!(player->mo->tracer->state >= &states[S_NIGHTSDRILL7A]
					&& player->mo->tracer->state <= &states[S_NIGHTSDRILL7D]))
				{
					if (!(player->mo->tracer->state >= &states[S_NIGHTSFLY1A]
						&& player->mo->tracer->state <= &states[S_NIGHTSFLY9B]))
					{
						int framenum;

						framenum = player->mo->tracer->state->frame & 3;

						if (framenum == 3) // Drilld special case
							P_SetMobjStateNF(player->mo->tracer, S_NIGHTSDRILL7A);
						else
							P_SetMobjStateNF(player->mo->tracer, S_NIGHTSDRILL7B+framenum);
					}
					else
						P_SetMobjStateNF(player->mo->tracer, S_NIGHTSDRILL7A);
				}
			}
			else
				P_SetMobjStateNF(player->mo->tracer, leveltime & 1*NEWTICRATERATIO ? S_NIGHTSFLY7A : S_NIGHTSFLY7B);
		}
		else if (player->anotherflyangle >= 327 && player->anotherflyangle <= 348)
		{
			if (player->pflags & PF_DRILLING)
			{
				if (!(player->mo->tracer->state >= &states[S_NIGHTSDRILL6A]
					&& player->mo->tracer->state <= &states[S_NIGHTSDRILL6D]))
				{
					if (!(player->mo->tracer->state >= &states[S_NIGHTSFLY1A]
						&& player->mo->tracer->state <= &states[S_NIGHTSFLY9B]))
					{
						int framenum;

						framenum = player->mo->tracer->state->frame & 3;

						if (framenum == 3) // Drilld special case
							P_SetMobjStateNF(player->mo->tracer, S_NIGHTSDRILL6A);
						else
							P_SetMobjStateNF(player->mo->tracer, S_NIGHTSDRILL6B+framenum);
					}
					else
						P_SetMobjStateNF(player->mo->tracer, S_NIGHTSDRILL6A);
				}
			}
			else
				P_SetMobjStateNF(player->mo->tracer, leveltime & 1*NEWTICRATERATIO ? S_NIGHTSFLY6A : S_NIGHTSFLY6B);
		}
	}

	if (player == &players[consoleplayer])
		localangle = player->mo->angle;
	else if (splitscreen && player == &players[secondarydisplayplayer])
		localangle2 = player->mo->angle;

	if (still)
	{
		P_SetMobjStateNF(player->mo->tracer, S_NIGHTSDRONE1);
		player->mo->tracer->angle = player->mo->angle;
	}

	// Synchronizes the "real" amount of time spent in the level.
	if (!player->exiting)
	{
		if (gametype == GT_RACE)
		{
			if (leveltime >= 4*TICRATE)
				player->realtime = leveltime - 4*TICRATE;
			else
				player->realtime = 0;
		}
		else
			player->realtime = leveltime;
	}

	P_UnsetThingPosition(player->mo->tracer);
	player->mo->tracer->x = player->mo->x;
	player->mo->tracer->y = player->mo->y;
	player->mo->tracer->z = player->mo->z;
	player->mo->tracer->floorz = player->mo->floorz;
	player->mo->tracer->ceilingz = player->mo->ceilingz;
	P_SetThingPosition(player->mo->tracer);

	if (movingangle >= ANG90 && movingangle <= ANG180)
		movingangle = movingangle - ANG180;
	else if (movingangle >= ANG180 && movingangle <= ANG270)
		movingangle = movingangle - ANG180;
	else if (movingangle >= ANG270)
		movingangle = (movingangle - ANGLE_MAX);

	if (player == &players[consoleplayer])
		localaiming = movingangle;
	else if (splitscreen && player == &players[secondarydisplayplayer])
		localaiming2 = movingangle;

	player->mo->tracer->angle = player->mo->angle;

	if ((player->pflags & PF_DRILLING) && !player->bumpertime)
	{
		if (firstdrill)
		{
			S_StartSound(player->mo, sfx_drill1);
			player->drilltimer = 32 * NEWTICRATERATIO;
		}
		else if (--player->drilltimer <= 0)
		{
			player->drilltimer = 10 * NEWTICRATERATIO;
			S_StartSound(player->mo, sfx_drill2);
		}
	}

	if (player->powers[pw_extralife] == 1) // Extra Life!
		P_RestoreMusic(player);

	if (cv_objectplace.value)
	{
		player->nightstime = 3;
		player->drillmeter = TICRATE;

		// This places a hoop!
		if (cmd->buttons & BT_ATTACK && !(player->pflags & PF_ATTACKDOWN))
		{
			mapthing_t *mt;
			mapthing_t *oldmapthings;
			USHORT angle;
			short temp;

			angle = (USHORT)(player->anotherflyangle % 360);

			oldmapthings = mapthings;
			nummapthings++;

			mapthings = Z_Realloc(oldmapthings, nummapthings * sizeof (*mapthings), PU_LEVEL, NULL);

			mt = mapthings+nummapthings-1;

			mt->x = (short)(player->mo->x>>FRACBITS);
			mt->y = (short)(player->mo->y>>FRACBITS);

			// Tilt
			mt->angle = (short)FixedMul(FixedDiv(angle*FRACUNIT,360*(FRACUNIT/256)), 1); // new

			// Traditional 2D Angle
			temp = (short)FixedMul(AngleFixed(player->mo->angle), 1); // new

			if (player->anotherflyangle < 90 || player->anotherflyangle > 270)
				temp -= 90;
			else
				temp += 90;

			temp %= 360;

			mt->type = 1705;

			mt->options = (USHORT)((player->mo->z -
				player->mo->subsector->sector->floorheight)>>FRACBITS);

			/*'Fixed' version that doesn't work
			mt->angle = (short)(mt->angle+(short)(FixedDiv(FixedDiv(temp*FRACUNIT, 360*(FRACUNIT/256)),1)<<8));
			*/

			mt->angle = (short)(mt->angle+(short)((FixedDiv(temp*FRACUNIT, 360*(FRACUNIT/256))/FRACUNIT)<<8));

			P_SpawnHoopsAndRings(mt);

			player->pflags |= PF_ATTACKDOWN;
		}
		else if (!(cmd->buttons & BT_ATTACK))
			player->pflags &= ~PF_ATTACKDOWN;

		// This places a bumper!
		if (cmd->buttons & BT_TOSSFLAG && !player->weapondelay)
		{
			mapthing_t *mt;
			mapthing_t *oldmapthings;

			if (((player->mo->z - player->mo->subsector->sector->floorheight)>>FRACBITS) >= (1 << (16-ZSHIFT)))
			{
				CONS_Printf("%s",text[TOOHIGH_4095]);
				return;
			}

			oldmapthings = mapthings;
			nummapthings++;

			mapthings = Z_Realloc(oldmapthings, nummapthings * sizeof (*mapthings), PU_LEVEL, NULL);

			mt = mapthings+nummapthings-1;

			mt->x = (short)(player->mo->x>>FRACBITS);
			mt->y = (short)(player->mo->y>>FRACBITS);
			mt->angle = (short)(FixedDiv(AngleFixed(player->mo->angle),1)%360);

			mt->type = (USHORT)mobjinfo[MT_NIGHTSBUMPER].doomednum;

			mt->options = (USHORT)((player->mo->z - player->mo->subsector->sector->floorheight)>>FRACBITS);

			mt->options <<= ZSHIFT;

			P_SpawnMapThing(mt);

			player->weapondelay = TICRATE*TICRATE;
		}
		else if (!(cmd->buttons & BT_TOSSFLAG))
			player->weapondelay = false;

		// This places a ring!
		if (cmd->buttons & BT_CAMRIGHT && !player->dbginfo)
		{
			mapthing_t *mt;
			mapthing_t *oldmapthings;

			oldmapthings = mapthings;
			nummapthings++;

			mapthings = Z_Realloc(oldmapthings, nummapthings * sizeof (*mapthings), PU_LEVEL, NULL);

			mt = mapthings + nummapthings-1;

			mt->x = (short)(player->mo->x>>FRACBITS);
			mt->y = (short)(player->mo->y>>FRACBITS);
			mt->angle = 0;
			mt->type = (USHORT)mobjinfo[MT_RING].doomednum;

			mt->options = (USHORT)((player->mo->z - player->mo->subsector->sector->floorheight)>>FRACBITS);
			mt->options <<= ZSHIFT;

			mt->options = (USHORT)(mt->options + (USHORT)cv_objflags.value);
			P_SpawnHoopsAndRings(mt);

			player->dbginfo = true;
		}
		else if (!(cmd->buttons & BT_CAMRIGHT))
			player->dbginfo = false;

		// This places a wing item!
		if (cmd->buttons & BT_CAMLEFT && !(player->pflags & PF_JUMPED))
		{
			mapthing_t *mt;
			mapthing_t *oldmapthings;

			oldmapthings = mapthings;
			nummapthings++;

			mapthings = Z_Realloc(oldmapthings, nummapthings * sizeof (*mapthings), PU_LEVEL, NULL);

			mt = mapthings + nummapthings-1;

			mt->x = (short)(player->mo->x>>FRACBITS);
			mt->y = (short)(player->mo->y>>FRACBITS);
			mt->angle = 0;
			mt->type = (USHORT)mobjinfo[MT_NIGHTSWING].doomednum;

			mt->options = (USHORT)((player->mo->z - player->mo->subsector->sector->floorheight)>>FRACBITS);

			CONS_Printf("Z is %d\n", mt->options);

			mt->options <<= ZSHIFT;

			mt->options = (USHORT)(mt->options + (USHORT)cv_objflags.value);

			P_SpawnHoopsAndRings(mt);

			player->pflags |= PF_JUMPED;
		}
		else if (!(cmd->buttons & BT_CAMLEFT))
			player->pflags &= ~PF_JUMPED;

		// This places a custom object as defined in the console cv_mapthingnum.
		if (cmd->buttons & BT_USE && !(player->pflags & PF_USEDOWN) && cv_mapthingnum.value)
		{
			mapthing_t *mt;
			mapthing_t *oldmapthings;
			int shift;
			USHORT angle;

			angle = (USHORT)((360-player->anotherflyangle) % 360);
			if (angle > 90 && angle < 270)
			{
				angle += 180;
				angle %= 360;
			}

			if (player->mo->target->flags & MF_AMBUSH)
				angle = (USHORT)player->anotherflyangle;
			else
			{
				angle = (USHORT)((360-player->anotherflyangle) % 360);
				if (angle > 90 && angle < 270)
				{
					angle += 180;
					angle %= 360;
				}
			}

			if ((cv_mapthingnum.value == 16 || cv_mapthingnum.value == 2008) && ((player->mo->z - player->mo->subsector->sector->floorheight)>>FRACBITS) >= (1 << (16-(ZSHIFT+1))))
			{
				CONS_Printf("%s", text[TOOHIGH_2047]);
				return;
			}
			else if (((player->mo->z - player->mo->subsector->sector->floorheight)>>FRACBITS) >= (1 << (16-ZSHIFT)))
			{
				CONS_Printf("%s", text[TOOHIGH_4095]);
				return;
			}

			oldmapthings = mapthings;
			nummapthings++;

			mapthings = Z_Realloc(oldmapthings, nummapthings * sizeof (*mapthings), PU_LEVEL, NULL);

			mt = mapthings+nummapthings-1;

			mt->x = (short)(player->mo->x>>FRACBITS);
			mt->y = (short)(player->mo->y>>FRACBITS);
			mt->angle = angle;
			mt->type = (short)cv_mapthingnum.value;

			mt->options = (USHORT)((player->mo->z - player->mo->subsector->sector->floorheight)>>FRACBITS);

			if (mt->type == 200 || mt->type == 201) // Eggmobile 1 & 2
				shift = ZSHIFT+1; // Why you would want to place these in a NiGHTS map, I have NO idea!
			else if (mt->type == 502) // Stupid starpost...
				shift = 0;
			else
				shift = ZSHIFT;

			if (shift)
				mt->options <<= shift;
			else
				mt->options = 0;

			mt->options = (USHORT)(mt->options + (USHORT)cv_objflags.value);

			if (mt->type == 1705 || mt->type == 600 || mt->type == 601 || mt->type == 602 // SRB2CBTODO: 1705 = MT_HOOP?
				|| mt->type == 603 || mt->type == 604 || mt->type == 300 || mt->type == 605
				|| mt->type == 606 || mt->type == 607 || mt->type == 608
				|| mt->type == 609 || mt->type == 1706) // SRB2CBTODO: 1706 = MT_HOOPCOLLIDE?
			{
				P_SpawnHoopsAndRings(mt);
			}
			else
				P_SpawnMapThing(mt);

			CONS_Printf("Spawned at %d\n", mt->options >> shift);

			player->pflags |= PF_USEDOWN;
		}
		else if (!(cmd->buttons & BT_USE))
			player->pflags &= ~PF_USEDOWN;
	}
}











#endif // ANGLE2D

//
// P_ObjectplaceMovement
//
// Control code for Objectplace mode
//
static void P_ObjectplaceMovement(player_t *player)
{
	ticcmd_t *cmd = &player->cmd;
	mobj_t *currentitem;

	// Allow direct angle changing in Objectplace
	player->mo->angle = (cmd->angleturn<<16 /* not FRACBITS */);

	ticruned++;
	if (!(cmd->angleturn & TICCMD_RECEIVED))
		ticmiss++;

	if (cmd->buttons & BT_JUMP)
		player->mo->z += FRACUNIT*cv_speed.value;
	else if (cmd->buttons & BT_USE)
		player->mo->z -= FRACUNIT*cv_speed.value;

	if (player->mo->target && player->mo->z > player->mo->ceilingz - player->mo->target->height)
		player->mo->z = player->mo->ceilingz - player->mo->target->height;
	else if (!player->mo->target && player->mo->z > player->mo->ceilingz - player->mo->height)
		player->mo->z = player->mo->ceilingz - player->mo->height;
	if (player->mo->z < player->mo->floorz)
		player->mo->z = player->mo->floorz;

	if (cmd->buttons & BT_CAMLEFT && !(player->pflags & PF_SKIDDOWN))
	{
		do
		{
			player->currentthing--;
			if (player->currentthing <= 0)
				player->currentthing = NUMMOBJTYPES-1;
		}while (mobjinfo[player->currentthing].doomednum == -1
			|| player->currentthing == MT_NIGHTSDRONE
			|| mobjinfo[player->currentthing].flags & MF_AMBIENT
			|| mobjinfo[player->currentthing].flags & MF_NOSECTOR
			|| mobjinfo[player->currentthing].flags & MF_BOSS
			|| (states[mobjinfo[player->currentthing].spawnstate].sprite == SPR_DISS
				&& player->currentthing != MT_MINUS
				&& (mobjinfo[player->currentthing].spawnstate != S_TELEPORTER)
				&& (mobjinfo[player->currentthing].spawnstate != S_PARTICLEFOUNTAIN)
				));

        // SRB2CB: No, we don't need to flood the console lines, this data is
        // already in the objectplace HUD
		//CONS_Printf("Current mapthing is %d\n", mobjinfo[player->currentthing].doomednum);
		player->pflags |= PF_SKIDDOWN;
	}
	else if (cmd->buttons & BT_CAMRIGHT && !(player->pflags & PF_JUMPDOWN))
	{
		do
		{
			player->currentthing++;
			if (player->currentthing >= NUMMOBJTYPES)
				player->currentthing = 0;
		}while (mobjinfo[player->currentthing].doomednum == -1
			|| player->currentthing == MT_NIGHTSDRONE
			|| mobjinfo[player->currentthing].flags & MF_AMBIENT
			|| mobjinfo[player->currentthing].flags & MF_NOSECTOR
			|| mobjinfo[player->currentthing].flags & MF_BOSS
			|| (states[mobjinfo[player->currentthing].spawnstate].sprite == SPR_DISS
				&& player->currentthing != MT_MINUS
				&& (mobjinfo[player->currentthing].spawnstate != S_TELEPORTER)
				&& (mobjinfo[player->currentthing].spawnstate != S_PARTICLEFOUNTAIN)
				));

        // SRB2CB: No, we don't need to flood the console lines, this data is
        // already in the objectplace HUD
		//CONS_Printf("Current mapthing is %d\n", mobjinfo[player->currentthing].doomednum);
		player->pflags |= PF_JUMPDOWN;
	}

	// Place an object and add it to the maplist
	if (player->mo->target)
		if (cmd->buttons & BT_ATTACK && !(player->pflags & PF_ATTACKDOWN))
		{
			mapthing_t *mt;
			mapthing_t *oldmapthings;
			mobj_t *newthing;
			short x,y,z = 0;
			byte zshift;

			if (player->mo->target->flags & MF_SPAWNCEILING)
			{
				// Move down from the ceiling

				if (cv_snapto.value)
				{
					if (cv_snapto.value == 1) // Snap to floor
						z = (short)((player->mo->subsector->sector->ceilingheight - player->mo->floorz)>>FRACBITS);
					else if (cv_snapto.value == 2) // Snap to ceiling
						z = (short)((player->mo->subsector->sector->ceilingheight - player->mo->ceilingz - player->mo->target->height)>>FRACBITS);
					else if (cv_snapto.value == 3) // Snap to middle
						z = (short)((player->mo->subsector->sector->ceilingheight - (player->mo->ceilingz - player->mo->floorz)/2 - player->mo->target->height/2)>>FRACBITS);
				}
				else
				{
					if (cv_grid.value)
					{
						int adjust;

						adjust = cv_grid.value - (((player->mo->subsector->sector->ceilingheight -
							player->mo->subsector->sector->floorheight)>>FRACBITS) % cv_grid.value);

						z = (short)(((player->mo->subsector->sector->ceilingheight - player->mo->z))>>FRACBITS);
						z = (short)(z + (short)adjust);

						// round to the nearest cv_grid.value
						z = (short)((z + cv_grid.value/2) % cv_grid.value);
						z = (short)(z - (short)adjust);
					}
					else
						z = (short)((player->mo->subsector->sector->ceilingheight - player->mo->z)>>FRACBITS);
				}
			}
			else
			{
				if (cv_snapto.value)
				{
					if (cv_snapto.value == 1) // Snap to floor
						z = (short)((player->mo->floorz - player->mo->subsector->sector->floorheight)>>FRACBITS);
					else if (cv_snapto.value == 2) // Snap to ceiling
						z = (short)((player->mo->ceilingz - player->mo->target->height - player->mo->subsector->sector->floorheight)>>FRACBITS);
					else if (cv_snapto.value == 3) // Snap to middle
						z = (short)((((player->mo->ceilingz - player->mo->floorz)/2)-(player->mo->target->height/2)-player->mo->subsector->sector->floorheight)>>FRACBITS);
				}
				else
				{
					if (cv_grid.value)
					{
						z = (short)(((player->mo->subsector->sector->ceilingheight - player->mo->z))>>FRACBITS);

						// round to the nearest cv_grid.value
						z = (short)((z + cv_grid.value/2) % cv_grid.value);
					}
					else
						z = (short)((player->mo->z - player->mo->subsector->sector->floorheight)>>FRACBITS);
				}
			}

			// Starts have height limitations for some reason.
			if (cv_mapthingnum.value >= 1 && cv_mapthingnum.value <= 99)
			{
				if (z >= (1 << (16-(ZSHIFT+1))))
				{
					CONS_Printf("Sorry, you're too %s to place this object (max: %d %s).\n",
						player->mo->target->flags & MF_SPAWNCEILING ? "low" : "high",
						(1 << (16-(ZSHIFT+1))),
						player->mo->target->flags & MF_SPAWNCEILING ? "below top ceiling" : "above bottom floor");
					return;
				}
				zshift = ZSHIFT+1; // Shift it over 5 bits to make room for the flag info.
			}
			else
			{
				if (z >= (1 << (16-ZSHIFT)))
				{
					CONS_Printf("Sorry, you're too %s to place this object (max: %d %s).\n",
						player->mo->target->flags & MF_SPAWNCEILING ? "low" : "high",
						(1 << (16-ZSHIFT)),
						player->mo->target->flags & MF_SPAWNCEILING ? "below top ceiling" : "above bottom floor");
					return;
				}
				zshift = ZSHIFT;
			}

			z <<= zshift;

			// Currently only the Starpost uses this
			if (player->mo->target->flags & MF_SPECIALFLAGS)
			{
				if (player->mo->target->type == MT_STARPOST)
					z = (short)z;
			}
			else
				z = (short)(z + (short)cv_objflags.value); // Easy/med/hard/ambush/etc.

			oldmapthings = mapthings;
			nummapthings++;

			mapthings = Z_Realloc(oldmapthings, nummapthings * sizeof (*mapthings), PU_LEVEL, NULL);

			mt = mapthings + nummapthings-1;

			if (cv_grid.value)
			{
				x = (short)(P_GridSnap(player->mo->x)>>FRACBITS);
				y = (short)(P_GridSnap(player->mo->y)>>FRACBITS);
			}
			else
			{
				x = (short)(player->mo->x>>FRACBITS);
				y = (short)(player->mo->y>>FRACBITS);
			}

			mt->x = x;
			mt->y = y;
			mt->angle = (short)FixedMul(AngleFixed(player->mo->angle),1);

			if (cv_mapthingnum.value != 0)
			{
				mt->type = (short)cv_mapthingnum.value;
				CONS_Printf("Placed object mapthingum %d, not the one below.\n", mt->type);
			}
			else
				mt->type = (short)mobjinfo[player->currentthing].doomednum;

			mt->options = z;

			newthing = P_SpawnMobj(x << FRACBITS, y << FRACBITS, player->mo->target->flags & MF_SPAWNCEILING ? player->mo->subsector->sector->ceilingheight - ((z>>zshift)<<FRACBITS)
								   : player->mo->subsector->sector->floorheight + ((z>>zshift)<<FRACBITS), player->currentthing);
			newthing->angle = player->mo->angle;
			newthing->spawnpoint = mt;
			CONS_Printf("Placed object type %d at %d, %d, %d, %d\n", newthing->info->doomednum, mt->x, mt->y, newthing->z>>FRACBITS, mt->angle);

			player->pflags |= PF_ATTACKDOWN;
		}

	if (cmd->buttons & BT_TAUNT) // Remove any objects near you
	{
		thinker_t *th;
		mobj_t *mo2;
		boolean done = false;

		// scan the thinkers
		for (th = thinkercap.next; th != &thinkercap; th = th->next)
		{
			if (th->function.acp1 != (actionf_p1)P_MobjThinker)
				continue;

			mo2 = (mobj_t *)th;

			if (mo2 == player->mo->target)
				continue;

			if (mo2 == player->mo)
				continue;

			if (P_AproxDistance(P_AproxDistance(mo2->x - player->mo->x, mo2->y - player->mo->y), mo2->z - player->mo->z) < player->mo->radius)
			{
				if (mo2->spawnpoint)
				{
					mapthing_t *mt;
					size_t i;

					P_SetMobjState(mo2, S_DISS);
					mt = mapthings;
					for (i = 0; i < nummapthings; i++, mt++)
					{
						if (done)
							continue;

						if (mt->mobj == mo2) // Found it! Now to delete...
						{
							mapthing_t *oldmapthings;
							mapthing_t *oldmt;
							mapthing_t *newmt;
							size_t z;

							CONS_Printf("Deleting...\n");

							oldmapthings = mapthings;
							nummapthings--;
							mapthings = Z_Calloc(nummapthings * sizeof (*mapthings), PU_LEVEL, NULL);

							// Gotta rebuild the WHOLE MAPTHING LIST,
							// otherwise it doesn't work!
							oldmt = oldmapthings;
							newmt = mapthings;
							for (z = 0; z < nummapthings+1; z++, oldmt++, newmt++)
							{
								if (oldmt->mobj == mo2)
								{
									CONS_Printf("Deleted.\n");
									newmt--;
									continue;
								}

								newmt->x = oldmt->x;
								newmt->y = oldmt->y;
								newmt->angle = oldmt->angle;
								newmt->type = oldmt->type;
								newmt->options = oldmt->options;

								newmt->z = oldmt->z;
								newmt->mobj = oldmt->mobj;
							}

							Z_Free(oldmapthings);
							done = true;
						}
					}
				}
				else
					CONS_Printf("You cannot delete this item because it doesn't have a mapthing!\n");
			}
			done = false;
		}
	}

	if (!(cmd->buttons & BT_ATTACK))
		player->pflags &= ~PF_ATTACKDOWN;

	if (!(cmd->buttons & BT_CAMLEFT))
		player->pflags &= ~PF_SKIDDOWN;

	if (!(cmd->buttons & BT_CAMRIGHT))
		player->pflags &= ~PF_JUMPDOWN;

	if (cmd->forwardmove != 0)
	{
		P_Thrust(player->mo, player->mo->angle, cmd->forwardmove*(FRACUNIT/4));
		P_TeleportMove(player->mo, player->mo->x+player->mo->momx, player->mo->y+player->mo->momy, player->mo->z);
		player->mo->momx = player->mo->momy = 0;
	}
	if (cmd->sidemove != 0)
	{
		P_Thrust(player->mo, player->mo->angle-ANG90, cmd->sidemove*(FRACUNIT/4));
		P_TeleportMove(player->mo, player->mo->x+player->mo->momx, player->mo->y+player->mo->momy, player->mo->z);
		player->mo->momx = player->mo->momy = 0;
	}

	if (!player->mo->target || player->currentthing != player->mo->target->type)
	{
		if (player->mo->target)
			P_RemoveMobj(player->mo->target); // The object has MF_NOTHINK, so S_DISS would never pass? // SRB2CBTODO: Needed?

		currentitem = P_SpawnMobj(player->mo->x, player->mo->y, player->mo->z, player->currentthing);
		currentitem->flags |= MF_NOTHINK;
		currentitem->angle = player->mo->angle;
		currentitem->tics = -1;

		P_SetTarget(&player->mo->target, currentitem);
		P_UnsetThingPosition(currentitem);
		currentitem->flags |= MF_NOBLOCKMAP;
		currentitem->flags |= MF_NOCLIP;
		P_SetThingPosition(currentitem);
		currentitem->floorz = player->mo->floorz;
		currentitem->ceilingz = player->mo->ceilingz;
	}
	else if (player->mo->target)
	{
		P_UnsetThingPosition(player->mo->target);
		player->mo->target->x = player->mo->x;
		player->mo->target->y = player->mo->y;
		player->mo->target->z = player->mo->z;
		P_SetThingPosition(player->mo->target);
		player->mo->target->angle = player->mo->angle;
		player->mo->target->floorz = player->mo->floorz;
		player->mo->target->ceilingz = player->mo->ceilingz;
	}
}

#if 0 // May be used in future for CTF
static void P_PlayerDropWeapon(player_t *player)
{
	mobj_t *mo = NULL;

	if (player->powers[pw_homingring])
	{
		mo = P_SpawnMobj(player->mo->x, player->mo->y, player->mo->z+(60*FRACUNIT), MT_BOUNCERING);
		player->powers[pw_homingring] = 0;
	}
	else if (player->powers[pw_railring])
	{
		mo = P_SpawnMobj(player->mo->x, player->mo->y, player->mo->z+(60*FRACUNIT), MT_RAILRING);
		player->powers[pw_railring] = 0;
	}
	else if (player->powers[pw_automaticring])
	{
		mo = P_SpawnMobj(player->mo->x, player->mo->y, player->mo->z+(60*FRACUNIT), MT_AUTOMATICRING);
		player->powers[pw_automaticring] = 0;
	}
	else if (player->powers[pw_explosionring])
	{
		mo = P_SpawnMobj(player->mo->x, player->mo->y, player->mo->z+(60*FRACUNIT), MT_EXPLOSIONRING);
		player->powers[pw_explosionring] = 0;
	}
	else if (player->powers[pw_scatterring])
	{
		mo = P_SpawnMobj(player->mo->x, player->mo->y, player->mo->z+(60*FRACUNIT), MT_SCATTERRING);
		player->powers[pw_scatterring] = 0;
	}
	else if (player->powers[pw_grenadering])
	{
		mo = P_SpawnMobj(player->mo->x, player->mo->y, player->mo->z+(60*FRACUNIT), MT_GRENADERING);
		player->powers[pw_grenadering] = 0;
	}

	if (mo)
	{
		player->mo->health--;
		P_InstaThrust(mo, player->mo->angle-ANG180, 8*FRACUNIT);
		P_SetObjectMomZ(mo, 4*FRACUNIT, false, false);
		mo->flags2 |= MF2_DONTRESPAWN;
		mo->flags &= ~MF_NOGRAVITY;
		mo->flags &= ~MF_NOCLIPHEIGHT;
		mo->fuse = 12*TICRATE;
	}
}
#endif

//#define LIGHTDASH // SRB2CBTODO!

#ifdef LIGHTDASH
static boolean P_RingNearby(player_t *player) // Is a ring in range?
{
	mobj_t *mo;
	thinker_t *think;
	mobj_t *closest = NULL;

	for (think = thinkercap.next; think != &thinkercap; think = think->next)
	{
		if (think->function.acp1 != (actionf_p1)P_MobjThinker) // Not a mobj thinker
			continue;

		mo = (mobj_t *)think;
		if ((mo->health <= 0) || (mo->state == &states[S_DISS])) // Not a valid ring
			continue;

		if (!(mo->type == MT_RING))
		{
			continue;
		}

		if (P_AproxDistance(P_AproxDistance(player->mo->x-mo->x, player->mo->y-mo->y),
							player->mo->z-mo->z) > 192*FRACUNIT) // Out of range
			continue;

		if (!P_CheckSight(player->mo, mo)) // Out of sight
			continue;

		if (closest && P_AproxDistance(P_AproxDistance(player->mo->x-mo->x, player->mo->y-mo->y),
									   player->mo->z-mo->z) > P_AproxDistance(P_AproxDistance(player->mo->x-closest->x,
																							  player->mo->y-closest->y), player->mo->z-closest->z))
			continue;

		// We don't wanna be dashing into hurt bosses repeatedly.
		if (mo->flags2 & MF2_FRET)
			continue;

		// Found a target
		closest = mo;
	}

	if (closest)
		return true;

	return false;
}


static void P_LightDash(mobj_t *source, mobj_t *enemy) // Home in on your target
{
	fixed_t dist;
	mobj_t *dest;

	if (!source->tracer)
		return; // Nothing to home in on!

	// adjust direction
	dest = source->tracer;

	if (!dest)
		return;

	// change angle
	source->angle = R_PointToAngle2(source->x, source->y, enemy->x, enemy->y);

	if (source->player)
	{
		if (source->player == &players[consoleplayer])
			localangle = source->angle;
		else if (splitscreen && source->player == &players[secondarydisplayplayer])
			localangle2 = source->angle;
	}

	// change slope
	dist = P_AproxDistance(P_AproxDistance(dest->x - source->x, dest->y - source->y), dest->z - source->z);

	if (dist < 1)
		dist = 1;

	if (source->player->speed < source->player->normalspeed)
	{
		source->momx = FixedMul(FixedDiv(dest->x - source->x, dist), (2+source->player->normalspeed*FRACUNIT));

		if (source->player && (twodlevel || (source->flags2 & MF2_TWOD)))
			source->momy = 0;
		else
			source->momy = FixedMul(FixedDiv(dest->y - source->y, dist), (2+source->player->normalspeed*FRACUNIT));

		source->momz = FixedMul(FixedDiv(dest->z - source->z, dist), (40*FRACUNIT));
	}

	else
	{
		source->momx = FixedMul(FixedDiv(dest->x - source->x, dist), (MAXMOVE));

		if (source->player && (twodlevel || (source->flags2 & MF2_TWOD)))
			source->momy = 0;
		else
			source->momy = FixedMul(FixedDiv(dest->y - source->y, dist), (MAXMOVE));

		source->momz = FixedMul(FixedDiv(dest->z - source->z, dist), (40*FRACUNIT));
	}

}



static void P_LookForRings(player_t *player)
{
	mobj_t *mo;
	thinker_t *think;
	boolean found = false;

	player->mo->target = player->mo->tracer = NULL;

	for (think = thinkercap.next; think != &thinkercap; think = think->next)
	{
		if (think->function.acp1 != (actionf_p1)P_MobjThinker) // Not a mobj thinker
			continue;

		mo = (mobj_t *)think;
		if ((mo->health <= 0) || (mo->state == &states[S_DISS])) // Not a valid ring
			continue;

		if (!(mo->type == MT_RING))
			continue;

		if (player->powers[pw_flashing])
		{
			//player->lightdash = 0;
			continue;
		}

		if (((player->mo->flags2 & MF2_TWOD) || twodlevel)
			&& ((mo->y > player->mo->y+16*FRACUNIT) || (mo->y < player->mo->y-16*FRACUNIT)))
			continue;

		if (P_AproxDistance(P_AproxDistance(player->mo->x-mo->x, player->mo->y-mo->y),
							player->mo->z-mo->z) > 256*FRACUNIT) // The higher this number is, the farther the game looks for the next ring
			continue;

		if (!P_CheckSight(player->mo, mo)) // Out of sight
			continue;

		if (player->mo->target && P_AproxDistance(P_AproxDistance(player->mo->x-mo->x,
																  player->mo->y-mo->y), player->mo->z-mo->z) >
			P_AproxDistance(P_AproxDistance(player->mo->x-player->mo->target->x,
											player->mo->y-player->mo->target->y), player->mo->z-player->mo->target->z))
			continue;

		// We don't wanna be dashing into hurt bosses repeatedly.
		if (mo->flags2 & MF2_FRET)
			continue;

		// Found a target
		found = true;
		player->mo->target = mo;
		player->mo->tracer = mo;
	}

	if (found)
	{
		P_ResetPlayer(player);
		P_SetPlayerMobjState(player->mo, S_PLAY_FALL1);
		P_ResetScore(player);
		P_LightDash(player->mo, player->mo->target);
		return;
	}

	//player->lightdash = false;
}
#endif

static fixed_t P_PlayerZAtSecF(player_t *player, sector_t *sector) // SRB2CBTODO: This needs to be over all the code
{
	if(!player)
		I_Error("P_PlayerZAtSecF: No player!");
#ifdef ESLOPE
	if (sector->f_slope)
		return P_GetZAt(sector->f_slope, player->mo->x, player->mo->y);
	else
#endif
		return sector->floorheight;
}

static fixed_t P_PlayerZAtSecC(player_t *player, sector_t *sector) // SRB2CBTODO: This needs to be over all the code
{
	if(!player)
		I_Error("P_PlayerZAtSecF: No player!");
#ifdef ESLOPE
	if (sector->c_slope)
		return P_GetZAt(sector->c_slope, player->mo->x, player->mo->y);
	else
#endif
		return sector->ceilingheight;
}

static fixed_t P_PlayerZAtF(player_t *player) // SRB2CBTODO: This needs to be over all the code
{
	sector_t *sector;
	sector = R_PointInSubsector(player->mo->x, player->mo->y)->sector;

#ifdef ESLOPE
	if (sector->f_slope)
		return P_GetZAt(sector->f_slope, player->mo->x, player->mo->y);
	else
#endif
		return sector->floorheight;
}

#if 0
static fixed_t P_PlayerZAtC(player_t *player) // SRB2CBTODO: This needs to be over all the code
{
	sector_t *sector;
	sector = R_PointInSubsector(player->mo->x, player->mo->y)->sector;

#ifdef ESLOPE
	if (sector->c_slope)
		return P_GetZAt(sector->c_slope, player->mo->x, player->mo->y);
	else
#endif
		return sector->ceilingheight;
}
#endif

static void P_DrawRings(player_t *player) // SRB2CBTODO: Needed to be global?
{
	thinker_t *th;
	mobj_t *mo2;
	fixed_t x = player->mo->x;
	fixed_t y = player->mo->y;
	fixed_t z = player->mo->z;

	for (th = thinkercap.next; th != &thinkercap; th = th->next)
	{
		if (th->function.acp1 != (actionf_p1)P_MobjThinker)
			continue;

		mo2 = (mobj_t *)th;

		if (!(mo2->type == MT_NIGHTSWING || mo2->type == MT_RING || mo2->type == MT_COIN
#ifdef BLUE_SPHERES
			  || mo2->type == MT_BLUEBALL
#endif
			  ))
			continue;

		if (P_AproxDistance(P_AproxDistance(mo2->x - x, mo2->y - y), mo2->z - z) > 128*FRACUNIT)
			continue;

		// Yay! The thing's in reach! Pull it in!
		mo2->flags2 |= MF2_NIGHTSPULL;
		P_SetTarget(&mo2->tracer, player->mo);
	}
}

static void P_CheckChangeTagTeam(player_t *player)
{
	if (cv_allowteamchange.value)
	{
		// Exception for hide and seek. Don't join a game when you simply
		// respawn in place and sit there for the rest of the round.
		if (!(gametype == GT_TAG && cv_tagtype.value && leveltime > (hidetime * TICRATE)))
		{
			player->lives++;
			P_DamageMobj(player->mo, NULL, NULL, 42000);
			player->spectator = false;

			if (gametype == GT_TAG)
				P_CheckSurvivors();

			CONS_Printf(text[INGAME_SWITCH], player_names[player-players]);
		}
		else
		{
			if (P_IsLocalPlayer(player))
				CONS_Printf("You must wait until next round to enter the game.\n");

			player->powers[pw_flashing] += 2*TICRATE; //to prevent message spam.
		}
	}
	else
	{
		CONS_Printf("Server does not allow team change.\n");
		player->powers[pw_flashing] += 2*TICRATE; //to prevent message spam.
	}
}

static void P_CheckChangeCTFTeam(player_t *player)
{
	int i;

	if (cv_allowteamchange.value)
	{
		int changeto;
		int red, blue;
		int redarray[MAXPLAYERS], bluearray[MAXPLAYERS];

		red = blue = changeto = 0;

		// We have to store the players in an array with the rest of their team.
		// We can then pick which team the player will be assigned to.
		for (i = 0; i < MAXPLAYERS; i++)
		{
			redarray[i] = 0;
			bluearray[i] = 0;
		}

		for (i = 0; i < MAXPLAYERS; i++)
		{
			if (playeringame[i])
			{
				switch (players[i].ctfteam)
				{
					case 0:
						break;
					case 1:
						redarray[red] = i; // Store the red team player's node.
						red++;
						break;
					case 2:
						bluearray[blue] = i; // Store the blue team player's node.
						blue++;
						break;
				}
			}
		}

		// Find a team by players, then by score, or random if all else fails.
		if (blue > red)
			changeto = 1;
		else if (red > blue)
			changeto = 2;
		else if (bluescore > redscore)
			changeto = 1;
		else if (redscore > bluescore)
			changeto = 2;
		else
			changeto = (P_Random() % 2) + 1;

		// assign the player to a team
		player->lives++;
		P_DamageMobj(player->mo, NULL, NULL, 42000);
		player->spectator = false;

		// SRB2CBTODO: For all menu related console options, use COM_ImmedExecute, not BufAddText and Execute
		if (changeto == 1)
		{
			if (player == &players[consoleplayer])
				COM_ImmedExecute("changeteam red");
			else if (splitscreen && player == &players[secondarydisplayplayer])
				COM_ImmedExecute("changeteam2 red");
		}
		else if (changeto == 2)
		{
			if (player == &players[consoleplayer])
				COM_ImmedExecute("changeteam blue");
			else if (splitscreen && player == &players[secondarydisplayplayer])
				COM_ImmedExecute("changeteam2 blue");
		}
	}
	else
	{
		CONS_Printf("Server does not allow team changes.\n");
		player->powers[pw_flashing] += 2*TICRATE; // To prevent message spam.
	}
}

static void P_CheckNiGHTsCapsule(player_t *player)
{
	int i;

	// Locate the capsule for this mare.
	if (!player->capsule && !player->bonustime)
	{
		thinker_t *th;
		mobj_t *mo2;

		for (th = thinkercap.next; th != &thinkercap; th = th->next)
		{
			if (th->function.acp1 != (actionf_p1)P_MobjThinker)
				continue;

			mo2 = (mobj_t *)th;

			if (mo2->type == MT_EGGCAPSULE
				&& mo2->threshold == player->mare)
				player->capsule = mo2;
		}
	}
	else if (player->capsule && player->capsule->reactiontime > 0 && player == &players[player->capsule->reactiontime-1])
	{
		if ((player->pflags & PF_NIGHTSMODE) && (player->mo->tracer->state < &states[S_NIGHTSHURT1]
								   || player->mo->tracer->state > &states[S_NIGHTSHURT32]))
			P_SetMobjState(player->mo->tracer, S_NIGHTSHURT1);

		if (player->mo->x <= player->capsule->x + 2*FRACUNIT
			&& player->mo->x >= player->capsule->x - 2*FRACUNIT)
		{
			P_UnsetThingPosition(player->mo);
			player->mo->x = player->capsule->x;
			P_SetThingPosition(player->mo);
			player->mo->momx = 0;
		}

		if (player->mo->y <= player->capsule->y + 2*FRACUNIT
			&& player->mo->y >= player->capsule->y - 2*FRACUNIT)
		{
			P_UnsetThingPosition(player->mo);
			player->mo->y = player->capsule->y;
			P_SetThingPosition(player->mo);
			player->mo->momy = 0;
		}

		if (player->mo->z <= player->capsule->z+(player->capsule->height/3) + 2*FRACUNIT
			&& player->mo->z >= player->capsule->z+(player->capsule->height/3) - 2*FRACUNIT)
		{
			player->mo->z = player->capsule->z+(player->capsule->height/3);
			player->mo->momz = 0;
		}

		if (player->mo->x > player->capsule->x)
			player->mo->momx = -2*FRACUNIT;
		else if (player->mo->x < player->capsule->x)
			player->mo->momx = 2*FRACUNIT;

		if (player->mo->y > player->capsule->y)
			player->mo->momy = -2*FRACUNIT;
		else if (player->mo->y < player->capsule->y)
			player->mo->momy = 2*FRACUNIT;

		if (player->mo->z > player->capsule->z+(player->capsule->height/3))
			player->mo->momz = -2*FRACUNIT;
		else if (player->mo->z < player->capsule->z+(player->capsule->height/3))
			player->mo->momz = 2*FRACUNIT;

		// Time to blow it up!
		if (player->mo->x == player->capsule->x
			&& player->mo->y == player->capsule->y
			&& player->mo->z == player->capsule->z+(player->capsule->height/3))
		{
			if (player->mo->health > 1)
			{
				player->mo->health--;
				player->health--;
				player->capsule->health--;

				// Spawn a 'pop' for each ring you deposit
				S_StartSound(P_SpawnMobj(player->capsule->x + ((P_SignedRandom()/3)<<FRACBITS), player->capsule->y + ((P_SignedRandom()/3)<<FRACBITS), player->capsule->z + (player->capsule->height/2) + ((P_SignedRandom()/3)<<FRACBITS), MT_EXPLODE), sfx_pop);

				if (player->capsule->health <= 0)
				{
					player->capsule->flags &= ~MF_NOGRAVITY;
					player->capsule->momz = 5*FRACUNIT;

					for (i = 0; i < MAXPLAYERS; i++)
					{
						if (players[i].mare == player->mare)
						{
							players[i].bonustime = 3*TICRATE;
							player->bonuscount = 10;
						}
					}

					{
						fixed_t z;

						z = player->capsule->z + player->capsule->height/2;
						for (i = 0; i < 16; i++)
							P_SpawnMobj(player->capsule->x, player->capsule->y, z, MT_BIRD);
					}
					player->capsule->reactiontime = 0;
					player->capsule = NULL;
					S_StartScreamSound(player->mo, sfx_ngdone);
				}
			}
			else
			{
				if (player->capsule->health <= 0)
				{
					player->capsule->flags &= ~MF_NOGRAVITY;
					player->capsule->momz = 5*FRACUNIT;

					for (i = 0; i < MAXPLAYERS; i++)
					{
						if (players[i].mare == player->mare)
						{
							players[i].bonustime = 3*TICRATE;
							player->bonuscount = 10;
						}
					}

					{
						fixed_t z;

						z = player->capsule->z + player->capsule->height/2;
						for (i = 0; i < 16; i++)
							P_SpawnMobj(player->capsule->x, player->capsule->y, z, MT_BIRD);
						S_StartScreamSound(player->mo, sfx_ngdone);
					}
				}
				player->capsule->reactiontime = 0;
				player->capsule = NULL;
			}
		}

#ifndef ANGLE2D
		if (player->pflags & PF_NIGHTSMODE)
		{
			P_UnsetThingPosition(player->mo->tracer);
			player->mo->tracer->x = player->mo->x;
			player->mo->tracer->y = player->mo->y;
			player->mo->tracer->z = player->mo->z;
			player->mo->tracer->floorz = player->mo->floorz;
			player->mo->tracer->ceilingz = player->mo->ceilingz;
			P_SetThingPosition(player->mo->tracer);
		}
#endif
		return;
	}

#ifdef FREEFLY
	if (!mapheaderinfo[gamemap-1].freefly)
#endif
	{
		if ((player->pflags & PF_NIGHTSFALL) && P_IsObjectOnGround(player->mo))
		{
			if (player->health > 1)
				P_DamageMobj(player->mo, NULL, NULL, 1);

			player->pflags &= ~PF_NIGHTSFALL;

			if (G_IsSpecialStage(gamemap))
			{
				for (i = 0; i < MAXPLAYERS; i++)
				{
					if (playeringame[i])
						players[i].exiting = (14*TICRATE)/5 + 1;
				}

				S_StartSound(NULL, sfx_lose);
			}
		}
	}

}

static void P_PlayerAnim(player_t *player, fixed_t runspd)
{
	ticcmd_t *cmd;
	cmd = &player->cmd;
	runspd = player->runspeed;

	// Flag variables so it's easy to check
	// what state the player is in.
	if (player->mo->state == &states[S_PLAY_RUN1] || player->mo->state == &states[S_PLAY_RUN2]
		|| player->mo->state == &states[S_PLAY_RUN3] || player->mo->state == &states[S_PLAY_RUN4]
		|| player->mo->state == &states[S_PLAY_RUN5] || player->mo->state == &states[S_PLAY_RUN6]
		|| player->mo->state == &states[S_PLAY_RUN7] || player->mo->state == &states[S_PLAY_RUN8]
		|| player->mo->state == &states[S_PLAY_SUPERWALK1] || player->mo->state == &states[S_PLAY_SUPERWALK2])
	{
		player->pflags |= PF_WALKINGANIM;
		player->pflags &= ~PF_RUNNINGANIM;
		player->pflags &= ~PF_SPINNINGANIM;
	}
	else if (player->mo->state == &states[S_PLAY_SPD1] || player->mo->state == &states[S_PLAY_SPD2]
			 || player->mo->state == &states[S_PLAY_SPD3] || player->mo->state == &states[S_PLAY_SPD4]
			 || player->mo->state == &states[S_PLAY_SUPERFLY1] || player->mo->state == &states[S_PLAY_SUPERFLY2])
	{
		player->pflags |= PF_RUNNINGANIM;
		player->pflags &= ~PF_WALKINGANIM;
		player->pflags &= ~PF_SPINNINGANIM;
	}
	else if (player->mo->state == &states[S_PLAY_ATK1] || player->mo->state == &states[S_PLAY_ATK2]
			 || player->mo->state == &states[S_PLAY_ATK3] || player->mo->state == &states[S_PLAY_ATK4])
	{
		player->pflags |= PF_SPINNINGANIM;
		player->pflags &= ~PF_RUNNINGANIM;
		player->pflags &= ~PF_WALKINGANIM;
	}
	else
	{
		player->pflags &= ~PF_WALKINGANIM;
		player->pflags &= ~PF_RUNNINGANIM;
		player->pflags &= ~PF_SPINNINGANIM;
	}

	if ((cmd->forwardmove != 0 || cmd->sidemove != 0) || (player->powers[pw_super] && player->mo->z > player->mo->floorz))
	{
		// If the player is moving fast enough,
		// break into a run!
		if ((player->speed > runspd) && (player->pflags & PF_WALKINGANIM) && (onground || player->powers[pw_super]))
			P_SetPlayerMobjState (player->mo, S_PLAY_SPD1);

		// Otherwise, just walk.
		else if ((player->rmomx || player->rmomy) && (player->mo->state == &states[S_PLAY_STND]
													  || player->mo->state == &states[S_PLAY_CARRY] || player->mo->state == &states[S_PLAY_TAP1]
													  || player->mo->state == &states[S_PLAY_TAP2] || player->mo->state == &states[S_PLAY_TEETER1]
													  || player->mo->state == &states[S_PLAY_TEETER2] || player->mo->state == &states[S_PLAY_SUPERSTAND]
													  || player->mo->state == &states[S_PLAY_SUPERTEETER]))
			P_SetPlayerMobjState (player->mo, S_PLAY_RUN1);
	}

	// Adjust the player's animation speed to match their velocity.
	if (P_IsLocalPlayer(player) && !disableSpeedAdjust)
	{
		if (onground || (player->powers[pw_super] && player->mo->z > player->mo->floorz)) // Only if on the ground.
		{
			if (player->pflags & PF_WALKINGANIM)
			{
				if (player->speed > FIXEDSCALE(12, player->mo->scale))
					playerstatetics[player-players][player->mo->state->nextstate] = 2;
				else if (player->speed > FIXEDSCALE(6, player->mo->scale))
					playerstatetics[player-players][player->mo->state->nextstate] = 3;
				else
					playerstatetics[player-players][player->mo->state->nextstate] = 4;
			}
			else if (player->pflags & PF_RUNNINGANIM)
			{
				if (player->speed > FIXEDSCALE(52, player->mo->scale))
					playerstatetics[player-players][player->mo->state->nextstate] = 1;
				else
					playerstatetics[player-players][player->mo->state->nextstate] = 2;
			}
		}
		else if (player->mo->state == &states[S_PLAY_FALL1] || player->mo->state == &states[S_PLAY_FALL2])
		{
			fixed_t speed;
			speed = abs(player->mo->momz);
			if (speed < FIXEDSCALE(5*FRACUNIT, player->mo->scale))
				playerstatetics[player-players][player->mo->state->nextstate] = 4;
			else if (speed < FIXEDSCALE(10*FRACUNIT, player->mo->scale))
				playerstatetics[player-players][player->mo->state->nextstate] = 3;
			else if (speed < FIXEDSCALE(15*FRACUNIT, player->mo->scale))
				playerstatetics[player-players][player->mo->state->nextstate] = 2;
			else
				playerstatetics[player-players][player->mo->state->nextstate] = 1;
		}

		if (player->pflags & PF_SPINNINGANIM)
		{
			if (player->speed > FIXEDSCALE(16, player->mo->scale))
				playerstatetics[player-players][player->mo->state->nextstate] = 1;
			else
				playerstatetics[player-players][player->mo->state->nextstate] = 2;
		}
	}

	// SRB2CB: New bug fix
	// If the player is say, in a place where they can only roll under
	// and they get hit, don't stay in your pain animation, change back
	// This also absoulutely fixes the zombie bug that occurs in netgames
	if ((!(player->pflags & PF_SLIDING)) &&
		(onground && (player->mo->state == &states[player->mo->info->painstate])))
		P_SetPlayerMobjState(player->mo, S_PLAY_STND);

	// If your running animation is playing, and you're
	// going too slow, switch back to the walking frames.
	if ((player->pflags & PF_RUNNINGANIM) && !(player->speed > runspd))
		P_SetPlayerMobjState(player->mo, S_PLAY_RUN1);

	// If Springing, but travelling DOWNWARD, change back!
	if (player->mo->state == &states[S_PLAY_PLG1] && player->mo->momz < 0)
		P_SetPlayerMobjState(player->mo, S_PLAY_FALL1);
	// If Springing but on the ground, change back!
	else if (onground && (player->mo->state == &states[S_PLAY_PLG1] || player->mo->state == &states[S_PLAY_FALL1]
						  || player->mo->state == &states[S_PLAY_FALL2] || player->mo->state == &states[S_PLAY_CARRY]) && !player->mo->momz)
		P_SetPlayerMobjState(player->mo, S_PLAY_STND);


	// SRB2CB: New fix
	// sometimes, you can be on a ledge of a sector and get stuck in a state
	if (player->mo->z == player->mo->floorz
		&& (((player->mo->eflags & MFE_VERTICALFLIP) && player->mo->momz > 0) || (!(player->mo->eflags & MFE_VERTICALFLIP) && player->mo->momz < 0))
		&& (player->mo->state == &states[S_PLAY_PLG1]
			|| player->mo->state == &states[S_PLAY_ATK1] || player->mo->state == &states[S_PLAY_FALL1]
			|| player->mo->state == &states[S_PLAY_FALL2] || player->mo->state == &states[S_PLAY_CARRY]))
	{
		player->mo->momz = 0;
		P_SetPlayerMobjState(player->mo, S_PLAY_STND);
	}

	// SRB2CBTODO: Weird error of stuckness?
	if (player->mo->eflags & MFE_VERTICALFLIP)
		if (P_IsObjectOnCeiling(player->mo))
			if (player->mo->z < player->mo->floorz)
				player->mo->z = player->mo->floorz+player->mo->height;

	// If you stopped moving, but you're still walking, stand still!
	if (!player->mo->momx && !player->mo->momy && !player->mo->momz && (player->pflags & PF_WALKINGANIM))
		P_SetPlayerMobjState(player->mo, S_PLAY_STND);
}

static void P_CheckPlayerClimb(player_t *player)
{
	ticcmd_t *cmd;
	cmd = &player->cmd;

	if (player->climbing == 1)
	{
		fixed_t platx;
		fixed_t platy;
		sector_t *glidesector;
		boolean climb = true;

		platx = P_ReturnThrustX(player->mo, player->mo->angle, player->mo->radius + 8*FRACUNIT);
		platy = P_ReturnThrustY(player->mo, player->mo->angle, player->mo->radius + 8*FRACUNIT);

		glidesector = R_PointInSubsector(player->mo->x + platx, player->mo->y + platy)->sector;

		if (glidesector != player->mo->subsector->sector)
		{
			boolean floorclimb;
			boolean thrust;
			boolean boostup;
			boolean skyclimber;
			thrust = false;
			floorclimb = false;
			boostup = false;
			skyclimber = false;

			if (glidesector->ffloors)
			{
				ffloor_t *rover;
				for (rover = glidesector->ffloors; rover; rover = rover->next)
				{
					if (!(rover->flags & FF_EXISTS) || !(rover->flags & FF_BLOCKPLAYER) || (rover->flags & FF_BUSTUP))
						continue;

					fixed_t topheight = *rover->topheight;
					fixed_t bottomheight = *rover->bottomheight;

#ifdef ESLOPE
					if (rover->t_slope)
						topheight = P_GetZAt(rover->t_slope, player->mo->x, player->mo->y);
					if (rover->b_slope)
						bottomheight = P_GetZAt(rover->b_slope, player->mo->x, player->mo->y);
#endif

					floorclimb = true;

					// Only supports rovers that are moving like an 'elevator', not just the top or bottom.
					if (rover->master->frontsector->floorspeed && rover->master->frontsector->ceilspeed == 42)
					{
						if ((!(player->mo->eflags & MFE_VERTICALFLIP) && (bottomheight < player->mo->z+player->mo->height)
							 && (topheight >= player->mo->z + 16*FRACUNIT))
							|| ((player->mo->eflags & MFE_VERTICALFLIP) && (topheight > player->mo->z+player->mo->height)
								&& (bottomheight <= player->mo->z + player->mo->height - 16*FRACUNIT)))
						{
							if (cmd->forwardmove != 0)
								player->mo->momz += rover->master->frontsector->floorspeed;
							else
							{
								player->mo->momz = rover->master->frontsector->floorspeed;
								climb = false;
							}
						}
					}

					// Gravity is flipped, so the comments are, too.
					if (player->mo->eflags & MFE_VERTICALFLIP)
					{
						// Trying to climb down past the bottom of the FOF
						if ((topheight >= player->mo->z + player->mo->height)
							&& ((player->mo->z + player->mo->height + player->mo->momz) >= topheight))
						{
							ffloor_t *roverbelow;
							boolean foundfof = false;
							floorclimb = true;
							boostup = false;

							// Is there a FOF directly below this one that we can move onto?
							for (roverbelow = glidesector->ffloors; roverbelow; roverbelow = roverbelow->next)
							{
								if (!(roverbelow->flags & FF_EXISTS) || !(roverbelow->flags & FF_BLOCKPLAYER) || (roverbelow->flags & FF_BUSTUP))
									continue;

								if (roverbelow == rover)
									continue;

								if (*roverbelow->bottomheight < topheight + 16*FRACUNIT)
									foundfof = true;
							}

							if (!foundfof)
								player->mo->momz = 0;
						}

						// Below the FOF
						if (topheight <= player->mo->z)
						{
							floorclimb = false;
							boostup = false;
							thrust = false;
						}

						// Above the FOF
						if (bottomheight > player->mo->z + player->mo->height - 16*FRACUNIT)
						{
							floorclimb = false;
							thrust = true;
							boostup = true;
						}
					}
					else
					{
						// Trying to climb down past the bottom of a FOF
						if ((bottomheight <= player->mo->z) && ((player->mo->z + player->mo->momz) <= bottomheight))
						{
							ffloor_t *roverbelow;
							boolean foundfof = false;
							floorclimb = true;
							boostup = false;

							// Is there a FOF directly below this one that we can move onto?
							for (roverbelow = glidesector->ffloors; roverbelow; roverbelow = roverbelow->next)
							{
								if (!(roverbelow->flags & FF_EXISTS) || !(roverbelow->flags & FF_BLOCKPLAYER) || (roverbelow->flags & FF_BUSTUP))
									continue;

								if (roverbelow == rover)
									continue;

								if (*roverbelow->topheight > bottomheight - 16*FRACUNIT)
									foundfof = true;
							}

							if (!foundfof)
								player->mo->momz = 0;
						}

						// Below the FOF
						if (bottomheight >= player->mo->z + player->mo->height)
						{
							floorclimb = false;
							boostup = false;
							thrust = false;
						}

						// Above the FOF
						if (topheight < player->mo->z + 16*FRACUNIT)
						{
							floorclimb = false;
							thrust = true;
							boostup = true;
						}
					}

					if (rover->flags & FF_CRUMBLE && !(netgame && player->spectator))
						EV_StartCrumble(rover->master->frontsector, rover, (rover->flags & FF_FLOATBOB), player, rover->alpha, !(rover->flags & FF_NORETURN));

					if (floorclimb)
						break;
				}
			}

			// Gravity is flipped, so are comments.
			if (player->mo->eflags & MFE_VERTICALFLIP)
			{
				// Trying to climb down past the upper texture area
				if ((P_PlayerZAtSecF(player, glidesector) >= player->mo->z + player->mo->height) && ((player->mo->z + player->mo->height + player->mo->momz) >= P_PlayerZAtSecF(player, glidesector)))
				{
					boolean foundfof = false;
					floorclimb = true;

					// Is there a FOF directly below that we can move onto?
					if (glidesector->ffloors)
					{
						ffloor_t *rover;
						for (rover = glidesector->ffloors; rover; rover = rover->next)
						{
							if (!(rover->flags & FF_EXISTS) || !(rover->flags & FF_BLOCKPLAYER) || (rover->flags & FF_BUSTUP))
								continue;

							fixed_t topheight = *rover->topheight;
							fixed_t bottomheight = *rover->bottomheight;

#ifdef ESLOPE
							if (rover->t_slope)
								topheight = P_GetZAt(rover->t_slope, player->mo->x, player->mo->y);

							if (rover->b_slope)
								bottomheight = P_GetZAt(rover->b_slope, player->mo->x, player->mo->y);
#endif

							if (bottomheight < P_PlayerZAtSecF(player, glidesector) + 16*FRACUNIT)
							{
								foundfof = true;
								break;
							}
						}
					}

					if (!foundfof)
						player->mo->momz = 0;
				}

				// Reached the top of the lower texture area
				if (!floorclimb && P_PlayerZAtSecC(player, glidesector) > player->mo->z + player->mo->height - 16*FRACUNIT
					&& (glidesector->ceilingpic == skyflatnum || P_PlayerZAtSecF(player, glidesector) < (player->mo->z - 8*FRACUNIT)))
				{
					thrust = true;
					boostup = true;
					// Play climb-up animation here
				}
			}
			else
			{
				// Trying to climb down past the upper texture area
				if ((P_PlayerZAtSecC(player, glidesector) <= player->mo->z) && ((player->mo->z + player->mo->momz) <= P_PlayerZAtSecC(player, glidesector)))
				{
					boolean foundfof = false;
					floorclimb = true;

					// Is there a FOF directly below that we can move onto?
					if (glidesector->ffloors)
					{
						ffloor_t *rover;
						for (rover = glidesector->ffloors; rover; rover = rover->next)
						{
							if (!(rover->flags & FF_EXISTS) || !(rover->flags & FF_BLOCKPLAYER) || (rover->flags & FF_BUSTUP))
								continue;

							fixed_t topheight = *rover->topheight;
							fixed_t bottomheight = *rover->bottomheight;

#ifdef ESLOPE
							if (rover->t_slope)
								topheight = P_GetZAt(rover->t_slope, player->mo->x, player->mo->y);
							if (rover->b_slope)
								bottomheight = P_GetZAt(rover->b_slope, player->mo->x, player->mo->y);
#endif

							if (topheight > P_PlayerZAtSecC(player, glidesector) - 16*FRACUNIT)
							{
								foundfof = true;
								break;
							}
						}
					}

					if (!foundfof)
						player->mo->momz = 0;
				}

				// Allow climbing from a FOF or lower texture onto the upper texture and vice versa.
				if (player->mo->z > P_PlayerZAtSecC(player, glidesector) - 16*FRACUNIT)
				{
					floorclimb = true;
					thrust = false;
					boostup = false;
				}

				// Reached the top of the lower texture area
				if (!floorclimb && P_PlayerZAtSecF(player, glidesector) < player->mo->z + 16*FRACUNIT
					&& (glidesector->ceilingpic == skyflatnum || P_PlayerZAtSecC(player, glidesector) > (player->mo->z + player->mo->height + 8*FRACUNIT)))
				{
					thrust = true;
					boostup = true;
					// Play climb-up animation here
				}
			}

			// Trying to climb on the sky
			if ((P_PlayerZAtSecC(player, glidesector) < player->mo->z) && glidesector->ceilingpic == skyflatnum)
			{
				skyclimber = true;
			}

			// Climbing on the lower texture area? // SRB2CBTODO: ESLOPE climb
			if ((!(player->mo->eflags & MFE_VERTICALFLIP) && player->mo->z + 16*FRACUNIT < P_PlayerZAtSecF(player, glidesector))
				|| ((player->mo->eflags & MFE_VERTICALFLIP) && player->mo->z + player->mo->height <= P_PlayerZAtSecF(player, glidesector)))
			{
				floorclimb = true;

				if (glidesector->floorspeed)
				{
					if (cmd->forwardmove != 0)
						player->mo->momz += glidesector->floorspeed;
					else
					{
						player->mo->momz = glidesector->floorspeed;
						climb = false;
					}
				}
			}
			// Climbing on the upper texture area?
			else if ((!(player->mo->eflags & MFE_VERTICALFLIP) && player->mo->z >= P_PlayerZAtSecC(player, glidesector))
					 || ((player->mo->eflags & MFE_VERTICALFLIP) && player->mo->z + player->mo->height - 16*FRACUNIT > P_PlayerZAtSecC(player, glidesector)))
			{
				floorclimb = true;

				if (glidesector->ceilspeed)
				{
					if (cmd->forwardmove != 0)
						player->mo->momz += glidesector->ceilspeed;
					else
					{
						player->mo->momz = glidesector->ceilspeed;
						climb = false;
					}
				}
			}

			if (player->lastsidehit != -1 && player->lastlinehit != -1)
			{
				thinker_t *think;
				scroll_t *scroller;
				angle_t sideangle;

				for (think = thinkercap.next; think != &thinkercap; think = think->next)
				{
					if (think->function.acp1 != (actionf_p1)T_Scroll)
						continue;

					scroller = (scroll_t *)think;

					if (scroller->type != sc_side)
						continue;

					if (scroller->affectee != player->lastsidehit)
						continue;

					if (cmd->forwardmove != 0)
					{
						player->mo->momz += scroller->dy;
						climb = true;
					}
					else
					{
						player->mo->momz = scroller->dy;
						climb = false;
					}

					sideangle = R_PointToAngle2(lines[player->lastlinehit].v2->x,lines[player->lastlinehit].v2->y,lines[player->lastlinehit].v1->x,lines[player->lastlinehit].v1->y);

					if (cmd->sidemove != 0)
					{
						P_Thrust(player->mo, sideangle, scroller->dx);
						climb = true;
					}
					else
					{
						P_InstaThrust(player->mo, sideangle, scroller->dx);
						climb = false;
					}
				}
			}

			if (cmd->sidemove != 0 || cmd->forwardmove != 0)
				climb = true;
			else
				climb = false;

			if (player->climbing && climb && (player->mo->momx || player->mo->momy || player->mo->momz)
				&& !(player->mo->state == &states[S_PLAY_CLIMB2]
					 || player->mo->state == &states[S_PLAY_CLIMB3]
					 || player->mo->state == &states[S_PLAY_CLIMB4]
					 || player->mo->state == &states[S_PLAY_CLIMB5]))
				P_SetPlayerMobjState(player->mo, S_PLAY_CLIMB2);
			else if ((!(player->mo->momx || player->mo->momy || player->mo->momz) || !climb) && player->mo->state != &states[S_PLAY_CLIMB1])
				P_SetPlayerMobjState(player->mo, S_PLAY_CLIMB1);

			if (!floorclimb)
			{
				if (boostup)
					P_SetObjectMomZ(player->mo, 2*FRACUNIT, true, true); // Don't scale this
				if (thrust)
					P_InstaThrust(player->mo, player->mo->angle, FIXEDSCALE(4*FRACUNIT, player->mo->scale)); // Lil' boost up.

				player->climbing = 0;
				player->pflags |= PF_JUMPED;
				P_SetPlayerMobjState(player->mo, S_PLAY_ATK1);
			}

			if (skyclimber)
			{
				player->climbing = 0;
				player->pflags |= PF_JUMPED;
				P_SetPlayerMobjState(player->mo, S_PLAY_ATK1);
			}
		}
		else
		{
			player->climbing = 0;
			player->pflags |= PF_JUMPED;
			P_SetPlayerMobjState(player->mo, S_PLAY_ATK1);
		}

		if (cmd->sidemove != 0 || cmd->forwardmove != 0)
			climb = true;
		else
			climb = false;

		if (player->climbing && climb && (player->mo->momx || player->mo->momy || player->mo->momz)
			&& !(player->mo->state == &states[S_PLAY_CLIMB2]
				 || player->mo->state == &states[S_PLAY_CLIMB3]
				 || player->mo->state == &states[S_PLAY_CLIMB4]
				 || player->mo->state == &states[S_PLAY_CLIMB5]))
			P_SetPlayerMobjState(player->mo, S_PLAY_CLIMB2);
		else if ((!(player->mo->momx || player->mo->momy || player->mo->momz) || !climb)
				 && player->mo->state != &states[S_PLAY_CLIMB1])
			P_SetPlayerMobjState(player->mo, S_PLAY_CLIMB1);

		if (cmd->buttons & BT_USE && !(player->pflags & PF_STASIS || player->powers[pw_nocontrol]))
		{
			player->climbing = 0;
			player->pflags |= PF_JUMPED;
			P_SetPlayerMobjState(player->mo, S_PLAY_ATK1);
			P_SetObjectMomZ(player->mo, 4*FRACUNIT, false, false);
			P_InstaThrust(player->mo, player->mo->angle, -4*FRACUNIT);
		}

		if (player == &players[consoleplayer])
			localangle = player->mo->angle;
		else if (splitscreen && player == &players[secondarydisplayplayer])
			localangle2 = player->mo->angle;

		if (player->climbing == 0)
			P_SetPlayerMobjState(player->mo, S_PLAY_ATK1);

		if (player->climbing && P_IsObjectOnGround(player->mo))
		{
			P_ResetPlayer(player);
			P_SetPlayerMobjState(player->mo, S_PLAY_STND);
		}
	}

	if (player->climbing > 1)
	{
		P_InstaThrust(player->mo, player->mo->angle, 4*FRACUNIT); // Shove up against the wall
		player->climbing--;
	}

	if (!player->climbing)
	{
		player->lastsidehit = -1;
		player->lastlinehit = -1;
	}
}

static void P_CheckTeeter(player_t *player)
{
	msecnode_t *node;

	// Make sure you're not teetering when you shouldn't be.
	if ((player->mo->state == &states[S_PLAY_TEETER1]
		 || player->mo->state == &states[S_PLAY_TEETER2] || player->mo->state == &states[S_PLAY_SUPERTEETER])
		&& (player->mo->momx || player->mo->momy || player->mo->momz))
		P_SetPlayerMobjState(player->mo, S_PLAY_STND);

	// Check for teeter!
	if (!player->mo->momz &&
		((!(player->mo->momx || player->mo->momy) && (player->mo->state == &states[S_PLAY_STND]
													  || player->mo->state == &states[S_PLAY_TAP1] || player->mo->state == &states[S_PLAY_TAP2]
													  || player->mo->state == &states[S_PLAY_TEETER1] || player->mo->state == &states[S_PLAY_TEETER2]
													  || player->mo->state == &states[S_PLAY_SUPERSTAND] || player->mo->state == &states[S_PLAY_SUPERTEETER]))))
	{
		boolean teeter = false;
		boolean roverfloor; // solid 3d floors?
		boolean checkedforteeter = false;
		const fixed_t tiptop = 12*FRACUNIT; // Distance you have to be above the ground in order to teeter.

		for (node = player->mo->touching_sectorlist; node; node = node->m_snext)
		{
			// Ledge teetering. Check if any nearby sectors are low enough from your current one.
			checkedforteeter = true;
			roverfloor = false;
			if (node->m_sector->ffloors)
			{
				ffloor_t *rover;
				for (rover = node->m_sector->ffloors; rover; rover = rover->next)
				{
					if (!(rover->flags & FF_EXISTS)) continue;

					fixed_t topheight = *rover->topheight;
					fixed_t bottomheight = *rover->bottomheight;

#ifdef ESLOPE
					if (rover->t_slope)
						topheight = P_GetZAt(rover->t_slope, player->mo->x, player->mo->y);

					if (rover->b_slope)
						bottomheight = P_GetZAt(rover->b_slope, player->mo->x, player->mo->y);
#endif

					if ((rover->flags & FF_SWIMMABLE) && GETSECSPECIAL(rover->master->frontsector->special, 1) == 3
						&& !(rover->master->flags & ML_BLOCKMONSTERS) && ((rover->master->flags & ML_EFFECT3)
																		  || player->mo->z - player->mo->momz > topheight - 16*FRACUNIT))
						;
					else if (!(rover->flags & FF_BLOCKPLAYER || rover->flags & FF_QUICKSAND))
						continue; // intangible 3d floor

					if (player->mo->eflags & MFE_VERTICALFLIP)
					{
						if (bottomheight > node->m_sector->ceilingheight) // Above the ceiling
							continue;

						if (bottomheight > player->mo->z + player->mo->height + tiptop
							|| (topheight < player->mo->z
								&& player->mo->z + player->mo->height < node->m_sector->ceilingheight - tiptop))
						{
							teeter = true;
							roverfloor = true;
						}
						else
						{
							teeter = false;
							roverfloor = true;
							break;
						}
					}
					else
					{
						if (topheight < node->m_sector->floorheight) // Below the floor
							continue;

						if (topheight < player->mo->z - tiptop
							|| (bottomheight > player->mo->z + player->mo->height
								&& player->mo->z > P_PlayerZAtSecF(player, node->m_sector) + tiptop))
						{
							teeter = true;
							roverfloor = true;
						}
						else
						{
							teeter = false;
							roverfloor = true;
							break;
						}
					}
				}
			}

			if (!teeter && !roverfloor)
			{
				if (player->mo->eflags & MFE_VERTICALFLIP)
				{
					if (node->m_sector->ceilingheight > player->mo->z + player->mo->height + tiptop)
						teeter = true;
				}
				else
				{
					if (P_PlayerZAtSecF(player, node->m_sector) < player->mo->z - tiptop)
						teeter = true;
				}
			}
		}

		if (checkedforteeter && !teeter) // Backup code
		{
			subsector_t *a = R_PointInSubsector(player->mo->x + 5*FRACUNIT, player->mo->y + 5*FRACUNIT);
			subsector_t *b = R_PointInSubsector(player->mo->x - 5*FRACUNIT, player->mo->y + 5*FRACUNIT);
			subsector_t *c = R_PointInSubsector(player->mo->x + 5*FRACUNIT, player->mo->y - 5*FRACUNIT);
			subsector_t *d = R_PointInSubsector(player->mo->x - 5*FRACUNIT, player->mo->y - 5*FRACUNIT);
			teeter = false;
			roverfloor = false;
			if (a->sector->ffloors)
			{
				ffloor_t *rover;
				for (rover = a->sector->ffloors; rover; rover = rover->next)
				{
					if (!(rover->flags & FF_EXISTS)) continue;

					if ((rover->flags & FF_SWIMMABLE) && GETSECSPECIAL(rover->master->frontsector->special, 1) == 3
						&& !(rover->master->flags & ML_BLOCKMONSTERS) && ((rover->master->flags & ML_EFFECT3)
																		  || player->mo->z - player->mo->momz > *rover->topheight - 16*FRACUNIT))
						;
					else if (!(rover->flags & FF_BLOCKPLAYER || rover->flags & FF_QUICKSAND))
						continue; // intangible 3d floor

					fixed_t topheight = *rover->topheight;
					fixed_t bottomheight = *rover->bottomheight;

#ifdef ESLOPE
					if (rover->t_slope)
						topheight = P_GetZAt(rover->t_slope, player->mo->x, player->mo->y);

					if (rover->b_slope)
						bottomheight = P_GetZAt(rover->b_slope, player->mo->x, player->mo->y);
#endif

					if (player->mo->eflags & MFE_VERTICALFLIP)
					{
						if (bottomheight > a->sector->ceilingheight) // Above the ceiling
							continue;

						if (bottomheight > player->mo->z + player->mo->height + tiptop
							|| (topheight < player->mo->z
								&& player->mo->z + player->mo->height < a->sector->ceilingheight - tiptop))
						{
							teeter = true;
							roverfloor = true;
						}
						else
						{
							teeter = false;
							roverfloor = true;
							break;
						}
					}
					else
					{
						if (topheight < a->sector->floorheight) // Below the floor
							continue;

						if (topheight < player->mo->z - tiptop
							|| (bottomheight > player->mo->z + player->mo->height
								&& player->mo->z > P_PlayerZAtSecF(player, a->sector) + tiptop))
						{
							teeter = true;
							roverfloor = true;
						}
						else
						{
							teeter = false;
							roverfloor = true;
							break;
						}
					}
				}
			}
			else if (b->sector->ffloors)
			{
				ffloor_t *rover;
				for (rover = b->sector->ffloors; rover; rover = rover->next)
				{
					if (!(rover->flags & FF_EXISTS)) continue;

					fixed_t topheight = *rover->topheight;
					fixed_t bottomheight = *rover->bottomheight;

#ifdef ESLOPE
					if (rover->t_slope)
						topheight = P_GetZAt(rover->t_slope, player->mo->x, player->mo->y);

					if (rover->b_slope)
						bottomheight = P_GetZAt(rover->b_slope, player->mo->x, player->mo->y);
#endif

					if ((rover->flags & FF_SWIMMABLE) && GETSECSPECIAL(rover->master->frontsector->special, 1) == 3
						&& !(rover->master->flags & ML_BLOCKMONSTERS) && ((rover->master->flags & ML_EFFECT3)
																		  || player->mo->z - player->mo->momz > topheight - 16*FRACUNIT))
						;
					else if (!(rover->flags & FF_BLOCKPLAYER || rover->flags & FF_QUICKSAND))
						continue; // intangible 3d floor

					if (player->mo->eflags & MFE_VERTICALFLIP)
					{
						if (bottomheight > b->sector->ceilingheight) // Above the ceiling
							continue;

						if (bottomheight > player->mo->z + player->mo->height + tiptop
							|| (topheight < player->mo->z
								&& player->mo->z + player->mo->height < b->sector->ceilingheight - tiptop))
						{
							teeter = true;
							roverfloor = true;
						}
						else
						{
							teeter = false;
							roverfloor = true;
							break;
						}
					}
					else
					{
						if (topheight < b->sector->floorheight) // Below the floor
							continue;

						if (topheight < player->mo->z - tiptop
							|| (bottomheight > player->mo->z + player->mo->height
								&& player->mo->z > P_PlayerZAtSecF(player, b->sector) + tiptop))
						{
							teeter = true;
							roverfloor = true;
						}
						else
						{
							teeter = false;
							roverfloor = true;
							break;
						}
					}
				}
			}
			else if (c->sector->ffloors)
			{
				ffloor_t *rover;
				for (rover = c->sector->ffloors; rover; rover = rover->next)
				{
					if (!(rover->flags & FF_EXISTS)) continue;

					fixed_t topheight = *rover->topheight;
					fixed_t bottomheight = *rover->bottomheight;

#ifdef ESLOPE
					if (rover->t_slope)
						topheight = P_GetZAt(rover->t_slope, player->mo->x, player->mo->y);

					if (rover->b_slope)
						bottomheight = P_GetZAt(rover->b_slope, player->mo->x, player->mo->y);
#endif

					if ((rover->flags & FF_SWIMMABLE) && GETSECSPECIAL(rover->master->frontsector->special, 1) == 3
						&& !(rover->master->flags & ML_BLOCKMONSTERS) && ((rover->master->flags & ML_EFFECT3)
																		  || player->mo->z - player->mo->momz > topheight - 16*FRACUNIT))
						;
					else if (!(rover->flags & FF_BLOCKPLAYER || rover->flags & FF_QUICKSAND))
						continue; // intangible 3d floor

					if (player->mo->eflags & MFE_VERTICALFLIP)
					{
						if (bottomheight > c->sector->ceilingheight) // Above the ceiling
							continue;

						if (bottomheight > player->mo->z + player->mo->height + tiptop
							|| (topheight < player->mo->z
								&& player->mo->z + player->mo->height < c->sector->ceilingheight - tiptop))
						{
							teeter = true;
							roverfloor = true;
						}
						else
						{
							teeter = false;
							roverfloor = true;
							break;
						}
					}
					else
					{
						if (topheight < c->sector->floorheight) // Below the floor
							continue;

						if (topheight < player->mo->z - tiptop
							|| (bottomheight > player->mo->z + player->mo->height
								&& player->mo->z > P_PlayerZAtSecF(player, c->sector) + tiptop))
						{
							teeter = true;
							roverfloor = true;
						}
						else
						{
							teeter = false;
							roverfloor = true;
							break;
						}
					}
				}
			}
			else if (d->sector->ffloors)
			{
				ffloor_t *rover;
				for (rover = d->sector->ffloors; rover; rover = rover->next)
				{
					if (!(rover->flags & FF_EXISTS)) continue;

					fixed_t topheight = *rover->topheight;
					fixed_t bottomheight = *rover->bottomheight;

#ifdef ESLOPE
					if (rover->t_slope)
						topheight = P_GetZAt(rover->t_slope, player->mo->x, player->mo->y);

					if (rover->b_slope)
						bottomheight = P_GetZAt(rover->b_slope, player->mo->x, player->mo->y);
#endif

					if ((rover->flags & FF_SWIMMABLE) && GETSECSPECIAL(rover->master->frontsector->special, 1) == 3
						&& !(rover->master->flags & ML_BLOCKMONSTERS) && ((rover->master->flags & ML_EFFECT3)
																		  || player->mo->z - player->mo->momz > topheight - 16*FRACUNIT))
						;
					else if (!(rover->flags & FF_BLOCKPLAYER || rover->flags & FF_QUICKSAND))
						continue; // intangible 3d floor

					if (player->mo->eflags & MFE_VERTICALFLIP)
					{
						if (bottomheight > d->sector->ceilingheight) // Above the ceiling
							continue;

						if (bottomheight > player->mo->z + player->mo->height + tiptop
							|| (topheight < player->mo->z
								&& player->mo->z + player->mo->height < d->sector->ceilingheight - tiptop))
						{
							teeter = true;
							roverfloor = true;
						}
						else
						{
							teeter = false;
							roverfloor = true;
							break;
						}
					}
					else
					{
						if (topheight < d->sector->floorheight) // Below the floor
							continue;

						if (topheight < player->mo->z - tiptop
							|| (bottomheight > player->mo->z + player->mo->height
								&& player->mo->z > P_PlayerZAtSecF(player, d->sector) + tiptop))
						{
							teeter = true;
							roverfloor = true;
						}
						else
						{
							teeter = false;
							roverfloor = true;
							break;
						}
					}
				}
			}

			if (player->mo->eflags & MFE_VERTICALFLIP)
			{
				if (!teeter && !roverfloor && (a->sector->ceilingheight > player->mo->ceilingz + tiptop
											   || b->sector->ceilingheight > player->mo->ceilingz + tiptop
											   || c->sector->ceilingheight > player->mo->ceilingz + tiptop
											   || d->sector->ceilingheight > player->mo->ceilingz + tiptop))
					teeter = true;
			}
			else
			{
				if (!teeter && !roverfloor && (P_PlayerZAtSecF(player, a->sector) < player->mo->floorz - tiptop
											   || P_PlayerZAtSecF(player, b->sector) < player->mo->floorz - tiptop
											   || P_PlayerZAtSecF(player, c->sector) < player->mo->floorz - tiptop
											   || P_PlayerZAtSecF(player, d->sector) < player->mo->floorz - tiptop))
					teeter = true;
			}
		}

#ifdef POLYOBJECTS // Collision (see if standing on edge for teetering)
		// Polyobjects
		{
			long bx, by, xl, xh, yl, yh;

			validcount++;

			yh = (ULONG)(player->mo->y + player->mo->radius - bmaporgy)>>MAPBLOCKSHIFT;
			yl = (ULONG)(player->mo->y - player->mo->radius - bmaporgy)>>MAPBLOCKSHIFT;
			xh = (ULONG)(player->mo->x + player->mo->radius - bmaporgx)>>MAPBLOCKSHIFT;
			xl = (ULONG)(player->mo->x - player->mo->radius - bmaporgx)>>MAPBLOCKSHIFT;

			for (by = yl; by <= yh; by++)
				for (bx = xl; bx <= xh; bx++)
				{
					long offset;
					polymaplink_t *plink; // haleyjd 02/22/06

					if (bx < 0 || by < 0 || bx >= bmapwidth || by >= bmapheight)
						continue;

					offset = by*bmapwidth + bx;

					// haleyjd 02/22/06: consider polyobject lines
					plink = polyblocklinks[offset];

					while (plink)
					{
						polyobj_t *po = plink->po;

						if (po->validcount != validcount) // if polyobj hasn't been checked
						{
							sector_t *polysec;
							fixed_t polytop, polybottom;

							po->validcount = validcount;

							if (!(po->flags & POF_SOLID))
							{
								plink = (polymaplink_t *)(plink->link.next);
								continue;
							}

							if (!P_MobjInsidePolyobj(po, player->mo))
							{
								plink = (polymaplink_t *)(plink->link.next);
								continue;
							}

							// We're inside it! Yess...
							polysec = po->lines[0]->backsector;

							// Make the polyobject like an FOF!
							polytop = polysec->ceilingheight;
							polybottom = polysec->floorheight;


							if (player->mo->eflags & MFE_VERTICALFLIP)
							{
								if (polybottom > player->mo->ceilingz) // Above the ceiling
								{
									plink = (polymaplink_t *)(plink->link.next);
									continue;
								}

								if (polybottom > player->mo->z + player->mo->height + tiptop
									|| (polybottom < player->mo->z
										&& player->mo->z + player->mo->height < player->mo->ceilingz - tiptop))
								{
									teeter = true;
									roverfloor = true;
								}
								else
								{
									teeter = false;
									roverfloor = true;
									break;
								}
							}
							else
							{
								if (polytop < player->mo->floorz) // Below the floor
								{
									plink = (polymaplink_t *)(plink->link.next);
									continue;
								}

								if (polytop < player->mo->z - tiptop
									|| (polytop > player->mo->z + player->mo->height
										&& player->mo->z > player->mo->floorz + tiptop))
								{
									teeter = true;
									roverfloor = true;
								}
								else
								{
									teeter = false;
									roverfloor = true;
									break;
								}
							}
						}
						plink = (polymaplink_t *)(plink->link.next);
					}
				}
		}
#endif
		if (teeter)
		{
			if ((player->mo->state == &states[S_PLAY_STND] || player->mo->state == &states[S_PLAY_TAP1]
				 || player->mo->state == &states[S_PLAY_TAP2] || player->mo->state == &states[S_PLAY_SUPERSTAND]))
				P_SetPlayerMobjState(player->mo, S_PLAY_TEETER1);
		}
		else if (checkedforteeter && (player->mo->state == &states[S_PLAY_TEETER1]
									  || player->mo->state == &states[S_PLAY_TEETER2] || player->mo->state == &states[S_PLAY_SUPERTEETER]))
			P_SetPlayerMobjState(player->mo, S_PLAY_STND);
	}
}

//
// P_MovePlayer
//
// SRB2CBTODO: This seriously needs to be split up.
// 2939 lines of code in this function.
static void P_MovePlayer(player_t *player)
{
	ticcmd_t *cmd;
	int i;

	fixed_t tempx, tempy;
	angle_t tempangle;
	msecnode_t *node;
	camera_t *thiscam;
	fixed_t runspd;

	if (countdowntimeup)
		return;

	if (splitscreen && player == &players[secondarydisplayplayer])
		thiscam = &camera2;
	else
		thiscam = &camera;

	if (player->mo->state >= &states[S_PLAY_SUPERTRANS1] && player->mo->state <= &states[S_PLAY_SUPERTRANS9])
	{
		P_CheckSneakerAndLivesTimer(player);
		P_CheckUnderwaterAndSpaceTimer(player);
		player->mo->momx = player->mo->momy = player->mo->momz = 0;
		return;
	}

	cmd = &player->cmd;
	runspd = player->runspeed;

	// Only allow this style of joining the game in normal match and tag.
	// CTF and team match spectators have to join the game in another method.
	if ((netgame || splitscreen) && player->spectator && (cmd->buttons & BT_ATTACK) && !player->powers[pw_flashing] &&
		((gametype == GT_MATCH && !cv_matchtype.value) || gametype == GT_TAG))
	{
		P_CheckChangeTagTeam(player);
	}

	// Team changing in Team Match and CTF
	// Pressing fire assigns you to a team that needs players if allowed.
	// Partial code reproduction from p_tick.c autobalance code.
	if ((netgame || splitscreen) && player->spectator && (cmd->buttons & BT_ATTACK) && !player->powers[pw_flashing] &&
		((gametype == GT_MATCH && cv_matchtype.value) || gametype == GT_CTF))
	{
		P_CheckChangeCTFTeam(player);
	}

	// Even if not NiGHTS, pull in nearby objects when walking around as John Q. Elliot.
	if (!cv_objectplace.value && !(netgame && player->spectator) && ((maptol & TOL_NIGHTS))
		&& (!(player->pflags & PF_NIGHTSMODE) || player->powers[pw_nightshelper]))
	{
		P_DrawRings(player);
	}

	if (player->bonustime > 1)
	{
		player->bonustime--;
		if (player->bonustime <= 1)
			player->bonustime = 1;
	}

	if (player->linktimer)
	{
		if (--player->linktimer <= 0) // Link timer
			player->linkcount = 0;
	}

	if (P_FreezeObjectplace())
	{
		P_ObjectplaceMovement(player);
		return;
	}

	//////////////////////
	// MOVEMENT CODE	//
	//////////////////////

	// Use a different control scheme in 2D mode
	if (twodlevel || (player->mo->flags2 & MF2_TWOD))
	{
		P_2dMovement(player);
	}
	// Test revamped NiGHTs movement
	else if (player->pflags & PF_NIGHTSMODE) // Kalaron: Moved here so nightsmodes works on other compilers
	{
		P_NiGHTSMovement(player);
	}
	else
	{

		if (players[consoleplayer].mo->flags2 & MF2_TWOSEVEN)
		{
			if (!cv_useranalog.value)
				cv_analog.value = 1;
		}
		else
		{
			if (!cv_useranalog.value)
				cv_analog.value = 0;
		}

		if (players[secondarydisplayplayer].mo->flags2 & MF2_TWOSEVEN)
		{
			if (!cv_useranalog2.value)
				cv_analog2.value = 1;
		}
		else
		{
			if (!cv_useranalog2.value)
				cv_analog2.value = 0;
		}


		if (!player->climbing && (netgame || (player == &players[consoleplayer]
			&& !cv_analog.value) || (splitscreen
			&& player == &players[secondarydisplayplayer] && !cv_analog2.value)
			|| (player->pflags & PF_SPINNING)))
		{
			player->mo->angle = (cmd->angleturn<<16 /* not FRACBITS */);
		}

		ticruned++; // SRB2CBTODO: WAHT IS THIS TICRUINED/MISSED?!
		if ((cmd->angleturn & TICCMD_RECEIVED) == 0)
			ticmiss++;

		P_3dMovement(player);
	}

	// Locate the capsule for this mare.
	if (maptol & TOL_NIGHTS)
		P_CheckNiGHTsCapsule(player);

	if (player->mo->flags2 & MF2_TWOD || twodlevel)
		runspd = (runspd * 0.55f);

	/////////////////////////
	// MOVEMENT ANIMATIONS //
	/////////////////////////

	P_PlayerAnim(player, runspd);

	//////////////////
	//GAMEPLAY STUFF//
	//////////////////

	// Make sure you're not "jumping" on the ground
	if ((player->pflags & PF_MINECART) && (player->mo->tracer && player->mo->tracer->type == MT_SKATEBOARD))
	{
		if (P_IsObjectOnGround(player->mo->tracer) && (player->pflags & PF_JUMPED) && !player->mo->tracer->momz && !player->homing)
		{
			player->pflags &= ~PF_JUMPED;
			player->secondjump = false;
			player->pflags &= ~PF_THOKKED;
			player->jumping = false;
			//P_SetPlayerMobjState(player->mo, S_PLAY_STND); // SRB2CBTODO: Special state for grind and skating
		}
	}

	else if (onground && (player->pflags & PF_JUMPED) && !player->mo->momz && !player->homing)
	{
		player->pflags &= ~PF_JUMPED;
		player->secondjump = false;
		player->pflags &= ~PF_THOKKED;
		player->jumping = false;
		P_SetPlayerMobjState(player->mo, S_PLAY_STND);
	}

	// Clear bounce back flags if we're descending
	if (onground || (((player->mo->eflags & MFE_VERTICALFLIP) && player->mo->momz > 0)
		|| (!(player->mo->eflags & MFE_VERTICALFLIP) && player->mo->momz < 0)))
	{
		player->mo->eflags &= ~MFE_JUSTBOUNCEDBACK;
	}

	// Cap the speed limit on a spindash
	// Note: MAXMOVE variable must be change in p_local.h to see any effect over 60*FRACUNIT
	if (player->dashspeed > FIXEDSCALE(player->maxdash<<FRACBITS, player->mo->scale))
		player->dashspeed = FIXEDSCALE(player->maxdash<<FRACBITS, player->mo->scale);
	else if (player->dashspeed > 0 && player->dashspeed < FIXEDSCALE(player->mindash*FRACUNIT, player->mo->scale))
		player->dashspeed = FIXEDSCALE(player->mindash*FRACUNIT, player->mo->scale);

	// Glide MOMZ
	// AKA my own gravity. =)
	if (player->pflags & PF_GLIDING)
	{
		fixed_t leeway;
		int glidespeed;

		glidespeed = player->actionspd;

		if (player->powers[pw_super] && ALL7EMERALDS(player->powers[pw_emeralds]))
			glidespeed = player->actionspd*2;

		if (player->mo->momz == (-2*FRACUNIT)/NEWTICRATERATIO)
			P_SetObjectAbsMomZ(player->mo, -2*FRACUNIT/NEWTICRATERATIO, true);
		else if (player->mo->momz < (-2*FRACUNIT)/NEWTICRATERATIO)
			P_SetObjectAbsMomZ(player->mo, 3*(FRACUNIT/4)/NEWTICRATERATIO, true);

		// Strafing while gliding.
		leeway = FixedAngle(cmd->sidemove*(FRACUNIT/2));

		if ((player->mo->eflags & MFE_UNDERWATER))
			P_InstaThrust(player->mo, player->mo->angle-leeway, (((glidespeed<<FRACBITS)/2) + player->glidetime*750));
		else
			P_InstaThrust(player->mo, player->mo->angle-leeway, ((glidespeed<<FRACBITS) + player->glidetime*1500));

		player->glidetime++;

		if (!(player->pflags & PF_JUMPDOWN)) // If not holding the jump button
		{
			P_ResetPlayer(player); // down, stop gliding.
			if ((player->charability2 == CA2_MULTIABILITY)
				|| (player->powers[pw_super] && ALL7EMERALDS(player->powers[pw_emeralds]) && player->charability == CA_GLIDEANDCLIMB))
			{
				player->pflags |= PF_JUMPED;
				P_SetPlayerMobjState(player->mo, S_PLAY_ATK1);
			}
			else
			{
				player->mo->momx >>= 1;
				player->mo->momy >>= 1;
				P_SetPlayerMobjState(player->mo, S_PLAY_FALL1);
			}
		}
	}
	else if (player->climbing) // 'Deceleration' for climbing on walls.
	{
		if (player->mo->momz > 0)
			P_SetObjectAbsMomZ(player->mo, -FRACUNIT/2, true);
		else if (player->mo->momz < 0)
			P_SetObjectAbsMomZ(player->mo, FRACUNIT/2, true);
	}

	if (!(player->charability == CA_GLIDEANDCLIMB)) // If you can't glide, then why would you be gliding?
	{
		player->pflags &= ~PF_GLIDING;
		player->glidetime = 0;
		player->climbing = 0;
	}

	// If you're running fast enough, you can create splashes as you run in shallow water.
	if (!player->climbing && player->mo->z + player->mo->height >= player->mo->watertop
		&& player->mo->z <= player->mo->watertop && (player->speed > runspd || (player->pflags & PF_STARTDASH))
		&& leveltime % (TICRATE/7) == 0 && player->mo->momz == 0 && !player->spectator
		&& !(player->pflags & PF_SLIDING))
	{
		P_Particles(player->mo, MT_PARTICLE, 255, FRACUNIT/2, 4*FRACUNIT, TICRATE, 1, false, 13, 6, 0);
		P_Particles(player->mo, MT_PARTICLE, 255, FRACUNIT  , 5*FRACUNIT , TICRATE, 1, false, 13, 6, 0);
		mobj_t *water = P_SpawnMobj(player->mo->x, player->mo->y, player->mo->watertop, MT_SPLISH);
		S_StartSound(water, sfx_wslap);
		water->destscale = player->mo->scale;
		P_SetScale(water, player->mo->scale);
	}
	// You can create splashes as you walk in shallow water.
	else if (!player->climbing && player->mo->z + player->mo->height >= player->mo->watertop
		&& player->mo->z <= player->mo->watertop && (player->speed > 1 || (player->pflags & PF_STARTDASH))
		&& leveltime % (TICRATE/3) == 0 && player->mo->momz == 0 && !player->spectator
		&& !(player->pflags & PF_SLIDING))
	{
		P_Particles(player->mo, MT_PARTICLE, 255, FRACUNIT/2, 3*FRACUNIT, TICRATE, 1, false, 13, 6, 0);
		P_Particles(player->mo, MT_PARTICLE, 255, FRACUNIT  , 5*FRACUNIT , TICRATE, 1, false, 13, 6, 0);
		mobj_t *water = P_SpawnMobj(player->mo->x, player->mo->y, player->mo->watertop, MT_SPLISH);
		S_StartSound(water, sfx_wslap);
		water->destscale = player->mo->scale;
		P_SetScale(water, player->mo->scale);
	}

	// Little water sound while touching water - just a nicety.
	if ((player->mo->eflags & MFE_TOUCHWATER) && !(player->mo->eflags & MFE_UNDERWATER) && !player->spectator)
	{
		if (P_Random() & 1 && leveltime % TICRATE == 0)
			S_StartSound(player->mo, sfx_floush);
	}

	//////////////////////////
	// RING & SCORE			//
	// EXTRA LIFE BONUSES	//
	//////////////////////////

	// Ahh ahh! No ring shields in special stages!
	if ((player->powers[pw_ringshield]
#ifdef SRB2K
		|| player->powers[pw_lightningshield]
#endif
		 )
		&& G_IsSpecialStage(gamemap))
		P_DamageMobj(player->mo, NULL, NULL, 1);

	if (!G_IsSpecialStage(gamemap)
		&& (gametype == GT_COOP || gametype == GT_RACE)
		&& !(mapheaderinfo[gamemap-1].typeoflevel & TOL_NIGHTS)) // Don't do it in special stages.
	{
		if ((player->health > 100) && (!player->xtralife))
		{
			P_GivePlayerLives(player, 1);

			if (mariomode)
				S_StartSound(player->mo, sfx_marioa);
			else
			{
				if (P_IsLocalPlayer(player))
				{
					S_StopMusic();
					S_ChangeMusic(mus_xtlife, false);
				}
				player->powers[pw_extralife] = extralifetics + 1;
			}
			player->xtralife = 1;
		}

		if ((player->health > 200) && (player->xtralife > 0 && player->xtralife < 2))
		{
			P_GivePlayerLives(player, 1);

			if (mariomode)
				S_StartSound(player->mo, sfx_marioa);
			else
			{
				if (P_IsLocalPlayer(player))
				{
					S_StopMusic();
					S_ChangeMusic(mus_xtlife, false);
				}
				player->powers[pw_extralife] = extralifetics + 1;
			}
			player->xtralife = 2;
		}
	}

	//////////////////////////
	// SUPER SONIC STUFF	//
	//////////////////////////

	P_DoSuperStuff(player);

	/////////////////////////
	//Special Music Changes//
	/////////////////////////

	P_CheckSneakerAndLivesTimer(player);

	///////////////////////////
	//LOTS OF UNDERWATER CODE//
	///////////////////////////



	// Display the countdown drown numbers!
	P_CheckUnderwaterAndSpaceTimer(player);

	////////////////
	//TAILS FLYING// // SRB2CBTODO: General swimming
	////////////////

	// If not in a fly position, don't think you're flying!
	if (!(player->mo->state == &states[S_PLAY_ABL1] || player->mo->state == &states[S_PLAY_ABL2]))
		player->powers[pw_tailsfly] = 0;


	if ((player->charability == CA_FLY || player->charability == CA_SWIM) && !(player->pflags & PF_STASIS || player->powers[pw_nocontrol]))
	{
		// Fly counter for Tails.
		if (player->powers[pw_tailsfly])
		{
			const fixed_t actionspd = (atoi(skins[player->skin].actionspd)<<FRACBITS)/100; //(player->actionspd<<FRACBITS)/100; Temp hack until P_ScaleMomentum() use is more widespread.

			if (player->charability2 == CA2_MULTIABILITY)
			{
				// Adventure-style flying by just holding the button down
				if (cmd->buttons & BT_JUMP)
					P_SetObjectMomZ(player->mo, (actionspd/4)/NEWTICRATERATIO, true, false);
			}
			else
			{
				// Classic flying
				if (player->fly1)
				{
					if ((!(player->mo->eflags & MFE_VERTICALFLIP) && player->mo->momz < (5*actionspd)/NEWTICRATERATIO)
						|| ((player->mo->eflags & MFE_VERTICALFLIP) && player->mo->momz > (-5*actionspd)/NEWTICRATERATIO))
						P_SetObjectMomZ(player->mo, (actionspd/2)/NEWTICRATERATIO, true, false);

/*
 #if 0
					if (player->mo->momz < - 3*FRACUNIT)
					{
						if (leveltime % 3 == 0)
							P_SetObjectMomZ(player->mo, (3*FRACUNIT), true, false);
					}
					else if ((leveltime % 4 == 0) && player->mo->momz < 5*FRACUNIT)
						P_SetObjectMomZ(player->mo, (1*FRACUNIT), true, false);
#endif
 */

					player->fly1--;
				}
			}

			// Tails Put-Put noise
			if (player->charability == CA_FLY && leveltime % 10 == 0 && !player->spectator)
				S_StartSound(player->mo, sfx_putput);

			// Descend
			if (cmd->buttons & BT_USE)
			{
				if (player->mo->eflags & MFE_VERTICALFLIP)
				{
					if (player->mo->momz < (5*actionspd)/NEWTICRATERATIO)
						player->mo->momz += (actionspd)/NEWTICRATERATIO;
				}
				else if (player->mo->momz > (-5*actionspd)/NEWTICRATERATIO)
					player->mo->momz -= (actionspd)/NEWTICRATERATIO;
			}

		}
		else
		{
			// Tails-gets-tired Stuff
			if (player->mo->state == &states[S_PLAY_ABL1]
				|| player->mo->state == &states[S_PLAY_ABL2])
				P_SetPlayerMobjState(player->mo, S_PLAY_SPC4);

			if (player->charability == CA_FLY && (leveltime % 10 == 0)
				&& player->mo->state >= &states[S_PLAY_SPC1]
				&& player->mo->state <= &states[S_PLAY_SPC4]
				&& !player->spectator)
				S_StartSound(player->mo, sfx_pudpud);
		}
	}


	// Uncomment this to invoke a 10-minute time limit on levels.
	/*if (leveltime > 20999) // one tic off so the time doesn't display 10 : 00
		P_DamageMobj(player->mo, NULL, NULL, 10000);*/

	// Spawn Invincibility Sparkles
	if (mariomode && player->powers[pw_invulnerability] && !player->powers[pw_super])
	{
		player->mo->flags |= MF_TRANSLATION;
		player->mo->color = (leveltime % MAXSKINCOLORS);
	}
	else
	{
		if ((player->powers[pw_invulnerability] || (player->powers[pw_super] && ALL7EMERALDS(player->powers[pw_emeralds])
			&& !(player->skin == 0))) && leveltime % (TICRATE/7) == 0
			&& (!player->powers[pw_super] || (player->powers[pw_super] && ALL7EMERALDS(player->powers[pw_emeralds]) && !(player->skin == 0))))
		{
			fixed_t destx, desty;

			if (!splitscreen && rendermode != render_soft)
			{
				angle_t viewingangle;

				if (!cv_chasecam.value && players[displayplayer].mo)
					viewingangle = R_PointToAngle2(player->mo->x, player->mo->y, players[displayplayer].mo->x, players[displayplayer].mo->y);
				else
					viewingangle = R_PointToAngle2(player->mo->x, player->mo->y, camera.x, camera.y);

				destx = player->mo->x + P_ReturnThrustX(player->mo, viewingangle, FRACUNIT);
				desty = player->mo->y + P_ReturnThrustY(player->mo, viewingangle, FRACUNIT);
			}
			else
			{
				destx = player->mo->x;
				desty = player->mo->y;
			}

			P_SpawnMobj(destx, desty, player->mo->z, MT_IVSP);
		}

		if ((player->powers[pw_super]) && (cmd->forwardmove != 0 || cmd->sidemove != 0)
			&& !(leveltime % TICRATE) && (player->mo->momx || player->mo->momy))
		{
			P_SpawnMobj(player->mo->x, player->mo->y, player->mo->z, MT_SUPERSPARK);
		}
	}

	// Resume normal music stuff.
	if (player->powers[pw_invulnerability] == 1 && (!player->powers[pw_super] ||  mapheaderinfo[gamemap-1].nossmusic))
	{
		if (mariomode)
		{
			player->mo->flags |= MF_TRANSLATION;
			if (player->powers[pw_fireflower])
				player->mo->color = 13;
			else
				player->mo->color = player->skincolor;
		}

		P_RestoreMusic(player);

		// If you had a shield, restore its visual significance
		P_SpawnShieldOrb(player);
	}


	// Show the "THOK!" graphic when spinning quickly across the ground.
	if (!(player->pflags & PF_JUMPED) && (player->pflags & PF_SPINNING) && player->speed > FIXEDSCALE(15, player->mo->scale))
	{
		mobj_t *item;
		if (player->spinitem > 0)
			item = P_SpawnSpinMobj(player, player->spinitem);
		else
			item = P_SpawnSpinMobj(player, player->mo->info->damage);

		if (item && (player->charflags & SF_GHOSTSPINITEM))
		{
			P_SpawnGhostMobj(item);
			P_SetMobjState(item, S_DISS);
		}
	}


	////////////////////////////
	//SPINNING AND SPINDASHING//
	////////////////////////////

	// If the player isn't on the ground(or on a slope), make sure they aren't in a "starting dash" position.
	if (!onground
#ifdef ESLOPE
		|| (P_SlopeLessThan(player->mo, false, LEVELSLOPE) || P_SlopeLessThan(player->mo, true, LEVELSLOPE)) // VPHYSICS: only for steeper slopes
#endif
		)
	{
		player->pflags &= ~PF_STARTDASH;
		player->dashspeed = 0;
	}

	if (player->powers[pw_watershield] && (player->pflags & PF_SPINNING) && (player->speed > 4) && onground && (leveltime & 1*NEWTICRATERATIO)
		&& !(player->mo->eflags & MFE_UNDERWATER) && !(player->mo->eflags & MFE_TOUCHWATER))
	{
		fixed_t newx;
		fixed_t newy;
		fixed_t ground;
		mobj_t *flame;
		angle_t travelangle;

		if (player->mo->eflags & MFE_VERTICALFLIP)
			ground = player->mo->ceilingz - mobjinfo[MT_SPINFIRE].height - 1;
		else
			ground = player->mo->floorz + 1;

		travelangle = R_PointToAngle2(player->mo->x, player->mo->y, player->rmomx + player->mo->x, player->rmomy + player->mo->y);

		newx = player->mo->x + P_ReturnThrustX(player->mo, travelangle + ANG45 + ANG90, 24*FRACUNIT);
		newy = player->mo->y + P_ReturnThrustY(player->mo, travelangle + ANG45 + ANG90, 24*FRACUNIT);
		flame = P_SpawnMobj(newx, newy, ground, MT_SPINFIRE);
		P_SetTarget(&flame->target, player->mo);
		flame->angle = travelangle;
		flame->fuse = TICRATE*6;
		if (player->mo->eflags & MFE_VERTICALFLIP)
			flame->eflags |= MFE_VERTICALFLIP;

		flame->momx = 8;
		P_XYMovement(flame);

		if (player->mo->eflags & MFE_VERTICALFLIP)
		{
			if (flame->z + flame->height < flame->ceilingz-1)
				P_SetMobjState(flame, S_DISS);
		}
		else if (flame->z > flame->floorz+1)
			P_SetMobjState(flame, S_DISS);

		newx = player->mo->x + P_ReturnThrustX(player->mo, travelangle - ANG45 - ANG90, 24*FRACUNIT);
		newy = player->mo->y + P_ReturnThrustY(player->mo, travelangle - ANG45 - ANG90, 24*FRACUNIT);
		flame = P_SpawnMobj(newx, newy, ground, MT_SPINFIRE);
		P_SetTarget(&flame->target, player->mo);
		flame->angle = travelangle;
		flame->fuse = TICRATE*6;
		if (player->mo->eflags & MFE_VERTICALFLIP)
			flame->eflags |= MFE_VERTICALFLIP;

		flame->momx = 8;
		P_XYMovement(flame);

		if (player->mo->eflags & MFE_VERTICALFLIP)
		{
			if (flame->z + flame->height < flame->ceilingz-1)
				P_SetMobjState(flame, S_DISS);
		}
		else if (flame->z > flame->floorz+1)
			P_SetMobjState(flame, S_DISS);
	}

	P_DoSpinDash(player, cmd);

	// jumping
	P_DoJumpStuff(player, cmd);

	// If you're not spinning, you'd better not be spindashing!
	if (!(player->pflags & PF_SPINNING))
		player->pflags &= ~PF_STARTDASH;

	// Synchronizes the "real" amount of time spent in the level.
	if (!player->exiting)
	{
		if (gametype == GT_RACE)
		{
			if (leveltime >= 4*TICRATE)
				player->realtime = leveltime - 4*TICRATE;
			else
				player->realtime = 0;
		}
		else
			player->realtime = leveltime;
	}

	//////////////////
	//TAG MODE STUFF//
	//////////////////
	if (gametype == GT_TAG)
	{
		if (cv_tagtype.value == 1) // Hide and seek!
		{
			// Already tagged players are invincible and cannot drown.
			for (i = 0; i < MAXPLAYERS; i++)
			{
				if (players[i].pflags & PF_TAGGED)
				{
					players[i].powers[pw_flashing] = 5;
					players[i].powers[pw_underwater] = players[i].powers[pw_spacetime] = 0;
				}
			}
		}

		// During hide time, taggers cannot move.
		if (leveltime < hidetime * TICRATE)
		{
			for (i = 0; i < MAXPLAYERS; i++)
			{
				if (players[i].pflags & PF_TAGIT)
					players[i].pflags |= PF_STASIS;
				// Don't let stationary taggers drown before they have a chance to move!
				players[i].powers[pw_underwater] = players[i].powers[pw_spacetime] = 0;
			}
		}
		else // Taggers can now move, but if in hide and seek, hiding players cannot, spectators can always move.
		{
			for (i = 0; i < MAXPLAYERS; i++)
			{
				if (players[i].pflags & PF_TAGIT)
					players[i].pflags &= ~PF_STASIS;
				else
				{
					if (cv_tagtype.value == 1 && !players[i].spectator) //hide and seek.
					{
						players[i].pflags |= PF_STASIS;
						players[i].powers[pw_underwater] = players[i].powers[pw_spacetime] = 0;//Don't let stationary hiding players drown!
					}
				}
			}
		}

		// If you're "IT", show a big "IT" over your head for others to see.
		if (player->pflags & PF_TAGIT)
		{
			if (!(player == &players[consoleplayer] || player == &players[secondarydisplayplayer]
				  || player == &players[displayplayer])) // Don't display it on your own view.
			{
				if (!(player->mo->eflags & MFE_VERTICALFLIP))
					P_SpawnMobj(player->mo->x, player->mo->y, player->mo->z + player->mo->height, MT_TAG);
				else
					P_SpawnMobj(player->mo->x, player->mo->y, player->mo->z - (player->mo->height/2), MT_TAG)->eflags |= MFE_VERTICALFLIP;
			}
			// Note: time dictated by leveltime.
		}

		// "No-Tag-Zone" Stuff
		// If in the No-Tag sector and don't have any "tagzone lag",
		// protect the player for 10 seconds.
		if (GETSECSPECIAL(player->mo->subsector->sector->special, 4) == 2 && !player->tagzone && !player->taglag
			&& !(player->pflags & PF_TAGIT))
			player->tagzone = 10*TICRATE;

		// If your time is up, set a certain time that you aren't
		// allowed back in, known as "tagzone lag".
		if (player->tagzone == 1)
			player->taglag = 60*TICRATE;

		// Or if you left the no-tag sector, do the same.
		if (GETSECSPECIAL(player->mo->subsector->sector->special, 4) != 2 && player->tagzone)
			player->taglag = 60*TICRATE;

		// If you have "tagzone lag", you shouldn't be protected.
		if (player->taglag)
			player->tagzone = 0;
	}
	//////////////////////////
	//CAPTURE THE FLAG STUFF//
	//////////////////////////

	else if (gametype == GT_CTF)
	{
		if (player->gotflag & MF_REDFLAG || player->gotflag & MF_BLUEFLAG) // If you have the flag (duh).
		{
			// Spawn a got-flag message over the head of the player that
			// has it (but not on your own screen if you have the flag).
			if (splitscreen)
			{
				if (player->gotflag & MF_REDFLAG)
				{
					if (!(player->mo->eflags & MFE_VERTICALFLIP))
						P_SpawnMobj(player->mo->x+player->mo->momx, player->mo->y+player->mo->momy,
									player->mo->z + P_GetPlayerHeight(player)+16*FRACUNIT+ player->mo->momz, MT_GOTFLAG);
					else
						P_SpawnMobj(player->mo->x+player->mo->momx, player->mo->y+player->mo->momy,
									player->mo->z - P_GetPlayerHeight(player)+24*FRACUNIT+ player->mo->momz, MT_GOTFLAG)->eflags |= MFE_VERTICALFLIP;
				}
				if (player->gotflag & MF_BLUEFLAG)
				{
					if (!(player->mo->eflags & MFE_VERTICALFLIP))
						P_SpawnMobj(player->mo->x+player->mo->momx, player->mo->y+player->mo->momy,
									player->mo->z + P_GetPlayerHeight(player)+16*FRACUNIT+ player->mo->momz, MT_GOTFLAG2);
					else
						P_SpawnMobj(player->mo->x+player->mo->momx, player->mo->y+player->mo->momy,
									player->mo->z - P_GetPlayerHeight(player)+24*FRACUNIT+ player->mo->momz, MT_GOTFLAG2)->eflags |= MFE_VERTICALFLIP;
				}
			}
			else if ((player != &players[consoleplayer]))
			{
				if (player->gotflag & MF_REDFLAG)
				{
					if (!(player->mo->eflags & MFE_VERTICALFLIP))
						P_SpawnMobj(player->mo->x+player->mo->momx, player->mo->y+player->mo->momy,
									player->mo->z + P_GetPlayerHeight(player)+16*FRACUNIT+ player->mo->momz, MT_GOTFLAG);
					else
						P_SpawnMobj(player->mo->x+player->mo->momx, player->mo->y+player->mo->momy,
									player->mo->z - P_GetPlayerHeight(player)+24*FRACUNIT+ player->mo->momz, MT_GOTFLAG)->eflags |= MFE_VERTICALFLIP;
				}
				if (player->gotflag & MF_BLUEFLAG)
				{
					if (!(player->mo->eflags & MFE_VERTICALFLIP))
						P_SpawnMobj(player->mo->x+player->mo->momx, player->mo->y+player->mo->momy,
									player->mo->z + P_GetPlayerHeight(player)+16*FRACUNIT+ player->mo->momz, MT_GOTFLAG2);
					else
						P_SpawnMobj(player->mo->x+player->mo->momx, player->mo->y+player->mo->momy,
									player->mo->z - P_GetPlayerHeight(player)+24*FRACUNIT+ player->mo->momz, MT_GOTFLAG2)->eflags |= MFE_VERTICALFLIP;
				}
			}
		}

	}

	//////////////////
	//ANALOG CONTROL//
	//////////////////

	// SRB2CBTODO: Note that the code here asumes that if it's not 2P, the useranalog control refers to
	// the first local player, this may cause issues elsewhere in the code if certain code is done server side?
	if ((cv_useranalog.value && player == &players[consoleplayer])
		|| (cv_useranalog2.value && splitscreen && player == &players[secondarydisplayplayer])) // SRB2CBTODO: inlined G_Second/FirstPlayer(player_t *player)
	{
		if (player->climbing || (player->pflags & PF_MINECART)
			|| (player->pflags & PF_CARRIED) || (player->pflags & PF_SLIDING) || (player->pflags & PF_ITEMHANG)
			|| (player->pflags & PF_MACESPIN) || (player->mo->state == &states[player->mo->info->painstate])
			|| (!cv_chasecam2.value && splitscreen && player == &players[secondarydisplayplayer])
			|| (!cv_chasecam.value && player == &players[consoleplayer])
			|| cv_objectplace.value)
		{
			if (cv_useranalog2.value && splitscreen && player == &players[secondarydisplayplayer])
				cv_analog2.value = 0;
			else
				cv_analog.value = 0;
		}
		else
		{
			if (cv_useranalog2.value && splitscreen && player == &players[secondarydisplayplayer])
				cv_analog2.value = 1;
			else
				cv_analog.value = 1;
		}
	}

	if (!netgame && ((player == &players[consoleplayer] && cv_analog.value) || (splitscreen && player == &players[secondarydisplayplayer] && cv_analog2.value))
		&& (cmd->forwardmove != 0 || cmd->sidemove != 0) && !player->climbing && !twodlevel && !(player->mo && (player->mo->flags2 & MF2_TWOD)))
	{
		// Face the way the controls point if on the ground, more direct
		if (!P_IsObjectOnGround(player->mo))
		{
			tempx = tempy = 0;

			tempangle = thiscam->angle;
			tempangle >>= ANGLETOFINESHIFT;
			tempx += FixedMul(cmd->forwardmove,FINECOSINE(tempangle));
			tempy += FixedMul(cmd->forwardmove,FINESINE(tempangle));

			tempangle = thiscam->angle-ANG90;
			tempangle >>= ANGLETOFINESHIFT;
			tempx += FixedMul(cmd->sidemove,FINECOSINE(tempangle));
			tempy += FixedMul(cmd->sidemove,FINESINE(tempangle));

			tempx = tempx*FRACUNIT;
			tempy = tempy*FRACUNIT;

			player->mo->angle = R_PointToAngle2(player->mo->x, player->mo->y, player->mo->x + tempx, player->mo->y + tempy);
		}
		// Otherwise, face the direction you're travelling, very smooth
		else if ((player->pflags & PF_WALKINGANIM) || (player->pflags & PF_GLIDING) || (player->pflags & PF_RUNNINGANIM) || (player->pflags & PF_SPINNINGANIM)
				 || ((player->mo->state == &states[S_PLAY_ABL1] || player->mo->state == &states[S_PLAY_ABL1]
					  || player->mo->state == &states[S_PLAY_ABL2] || player->mo->state == &states[S_PLAY_SPC1]
					  || player->mo->state == &states[S_PLAY_SPC2] || player->mo->state == &states[S_PLAY_SPC3]
					  || player->mo->state == &states[S_PLAY_SPC4]) && player->charability == CA_FLY))
			player->mo->angle = R_PointToAngle2(player->mo->x, player->mo->y, player->rmomx + player->mo->x, player->rmomy + player->mo->y);

		// Update the local angle control.
		if (player == &players[consoleplayer])
			localangle = player->mo->angle;
		else if (splitscreen && player == &players[secondarydisplayplayer])
			localangle2 = player->mo->angle;
	}

	///////////////////////////
	//BOMB SHIELD ACTIVATION,//
	//HOMING, AND OTHER COOL //
	//STUFF!                 //
	///////////////////////////

	// Jump shield activation
	if (cmd->buttons & BT_USE && !(player->pflags & PF_USEDOWN) && !(player->mo->state == &states[S_PLAY_PLG1])
		&& !(player->mo->state == &states[S_PLAY_PAIN]) && !player->climbing && !(player->pflags & PF_GLIDING) && !(player->pflags & PF_SLIDING)
		&& !(player->pflags & PF_THOKKED) && !player->powers[pw_tailsfly]
		&& !onground && !(player->mo->state >= &states[S_PLAY_FALL1] && player->mo->state <= &states[S_PLAY_FALL2])
		&& !((player->pflags & PF_CARRIED) || (player->pflags & PF_ITEMHANG) || (player->pflags & PF_MACESPIN) || (player->pflags & PF_NIGHTSFALL)
			 || (player->pflags & PF_ROPEHANG) || (player->pflags & PF_MINECART)))
	{
		if (player->powers[pw_jumpshield] && !player->powers[pw_super])
			P_DoJumpShield(player);
#ifdef SRB2K
		else if (player->powers[pw_lightningshield] && !player->powers[pw_super])
			P_DoJumpShield(player);
#endif
		else if (player->powers[pw_super] && ALL7EMERALDS(player->powers[pw_emeralds]) && player->charability == CA_FLY)
		{
			P_DoJumpShield(player);
			player->mo->momz *= 2;
		}
	}

	// Bomb shield and force shield activation and Super Sonic move
	if (cmd->buttons & BT_USE)
	{
		if (player->pflags & PF_JUMPED)
		{
			if (player->skin == 0 && player->powers[pw_super] && player->speed > 5
				&& ((player->mo->momz <= 0 && !(player->mo->eflags & MFE_VERTICALFLIP))
					|| (player->mo->momz >= 0 && (player->mo->eflags & MFE_VERTICALFLIP))))
			{
				if ((player->mo->state >= &states[S_PLAY_ATK1]
					&& player->mo->state <= &states[S_PLAY_ATK4])
					|| player->mo->state == &states[S_PLAY_PAIN])
					P_SetPlayerMobjState(player->mo, S_PLAY_SUPERWALK1);

				player->mo->momz = 0;
				player->pflags &= ~PF_SPINNING;
			}
			else if (!player->powers[pw_super] && (player->powers[pw_bombshield]
#ifdef SRB2K
					|| player->powers[pw_bubbleshield] || player->powers[pw_lightningshield]
#endif
					 ) && !(player->pflags & PF_USEDOWN))
			{
				// Don't let Super Sonic or invincibility use it
				if (!(player->powers[pw_super] || player->powers[pw_invulnerability]))
				{
					if (player->powers[pw_bombshield])
					{
						player->blackow = 1; // This signals for the BOOM to take effect, as seen below.
						player->powers[pw_bombshield] = false;
					}
				}
			}
		}
#if 0 // Shield reflect code
		if (!player->powers[pw_super] && !player->shielddelay && (player->powers[pw_forceshield]) && !(player->pflags & PF_USEDOWN))
		{
			thinker_t *think;
			mobj_t *mo;
			fixed_t dist;

			player->shielddelay = 5*TICRATE;

			S_StartSound(player->mo, sfx_shield);

			for (think = thinkercap.next; think != &thinkercap; think = think->next)
			{
				if (think->function.acp1 != (actionf_p1)P_MobjThinker) // Not a mobj thinker
					continue;

				mo =(mobj_t *)think;

				if (mo->flags & MF_MISSILE)
				{
					dist = P_AproxDistance((mo->x+mo->momx)-player->mo->x, (mo->y+mo->momy)-player->mo->y);

					if (dist > player->mo->radius*8)
						continue;

					if (abs((mo->z+mo->momz)-player->mo->z) > player->mo->height*8)
						continue;

					mo->momx = -mo->momx;
					mo->momy = -mo->momy;
					mo->momz = -mo->momz;

					P_SetTarget(&mo->target, player->mo);

					if (mo->type == MT_DETON)
						P_SetMobjState(mo, mo->info->xdeathstate);
				}
				else if (mo->type == MT_BIGMACE || mo->type == MT_SMALLMACE) // Reverse the direction of a swinging mace
				{
					dist = P_AproxDistance((mo->x+mo->momx)-player->mo->x, (mo->y+mo->momy)-player->mo->y);

					if (dist > player->mo->radius*8)
						continue;

					if (abs((mo->z+mo->momz)-player->mo->z) > player->mo->height*8)
						continue;

					if (mo->target && mo->target->type == MT_MACEPOINT)
						mo->target->lastlook = -mo->target->lastlook;
				}
			}
		}
#endif
	}

#ifdef SRB2K // SRB2K Bubble shield bounce - Kalaron
	// Handle bubble shield, remember that it is possible to check the next move with adding the momx/y/z value to the current position
	// to the player's current coordinates
	if (player->powers[pw_bubbleshield])
	{
		if (!player->dobounce)
		{
			if (cmd->buttons & BT_USE)
			{
				if (player->mo->eflags & MFE_VERTICALFLIP)
				{
					if (onground && player->dobounce)
						player->dobounce = false;
					else if (!player->climbing && (!(player->pflags & PF_GLIDING))
							 && !player->powers[pw_tailsfly] && !player->powers[pw_super]
							 && (player->pflags & PF_JUMPED)
							 && !(player->pflags & PF_USEDOWN)
							 && !onground
							 && player->mo->z+player->mo->momz+player->mo->height < player->mo->ceilingz-FIXEDSCALE(8*FRACUNIT, player->mo->scale))
					{
						if (!((cmd->buttons & BT_USE) && (cmd->buttons & BT_JUMP))
							&& !player->dobounce)
							player->dobounce = true;
					}
				}
				else
				{
					if (onground && player->dobounce)
						player->dobounce = false;
					else if (!player->climbing && (!(player->pflags & PF_GLIDING))
							 && !player->powers[pw_tailsfly] && !player->powers[pw_super]
							 && (player->pflags & PF_JUMPED)
							 && !(player->pflags & PF_USEDOWN)
							 && !onground
							 && player->mo->z+player->mo->momz > player->mo->floorz+FIXEDSCALE(8*FRACUNIT, player->mo->scale))
					{
						if (!((cmd->buttons & BT_USE) && (cmd->buttons & BT_JUMP))
							&& !player->dobounce)
							player->dobounce = true;
					}
				}
			}
		}

		if (player->dobounce)
		{
			if (onground)
				player->dobounce = false;

			if (player->mo->eflags & MFE_VERTICALFLIP)
			{
				if (player->mo->z+player->mo->momz+player->mo->height < player->mo->ceilingz-FIXEDSCALE(8*FRACUNIT, player->mo->scale)
					//|| (player->mo->momz < FIXEDSCALE(18*FRACUNIT, player->mo->scale) && player->mo->z < player->mo->ceilingz-FIXEDSCALE(24*FRACUNIT, player->mo->scale))
					)
				{
					if (player->mo->momz > FIXEDSCALE(48*FRACUNIT, player->mo->scale)) // Speed cap check, go too fast and the player can't bounce back up
					{
						player->mo->momz = FIXEDSCALE(48*FRACUNIT, player->mo->scale);
						P_SetObjectMomZ(player->mo, -2*FRACUNIT, true, false);
					}

					P_SetObjectMomZ(player->mo, -FIXEDSCALE(2*FRACUNIT, player->mo->scale), true, true);

					if (player->mo->momz < 0)
						player->dobounce = false;
				}
				else if (!onground)
				{
					// Bounce back up a little less than you came down, don't scale, this is absolute
					if (player->mo->momz > 0)
						P_SetObjectAbsMomZ(player->mo, -player->mo->momz/2.0f, false);

					player->secondjump = false;
					player->jumping = false;
					player->pflags &= ~PF_JUMPED;
					player->pflags &= ~PF_SPINNING;

					if (player->dobounce && player->mo->momz < 0)
					{
						player->dobounce = false;
						if (//(player->mo->momz < FIXEDSCALE(18*FRACUNIT, player->mo->scale)
							//&& player->mo->z+player->mo->height > player->mo->ceilingz-FIXEDSCALE(28*FRACUNIT, player->mo->scale))
							//||
							player->mo->z+player->mo->momz+player->mo->height > player->mo->ceilingz-FIXEDSCALE(8*FRACUNIT, player->mo->scale))
						{
							//P_SetMobjState(the orb, bendy state);
							S_StartSound(player->mo, sfx_s3k_19);
						}
					}
				}
			}
			else
			{
				if (player->mo->z+player->mo->momz > player->mo->floorz+FIXEDSCALE(8*FRACUNIT, player->mo->scale)
					//|| (player->mo->momz > -FIXEDSCALE(18*FRACUNIT, player->mo->scale) && player->mo->z > player->mo->floorz+FIXEDSCALE(24*FRACUNIT, player->mo->scale))
					)
				{
					if (player->mo->momz < -FIXEDSCALE(48*FRACUNIT, player->mo->scale)) // Speed cap check, go too fast and the player can't bounce back up
					{
						player->mo->momz = -FIXEDSCALE(48*FRACUNIT, player->mo->scale);
						P_SetObjectMomZ(player->mo, 2*FRACUNIT, true, false);
					}
					// SRB2CBTODO: Why is there NEWTICRATERATIO?!!! Remove it when it isn't needed
					P_SetObjectMomZ(player->mo, -FIXEDSCALE(2*FRACUNIT, player->mo->scale), true, true);

					if (player->mo->momz > 0)
						player->dobounce = false;
				}
				else if (!onground)
				{
					// Bounce back up as much as you came down, don't scale, this is absolute
					if (player->mo->momz < 0)
					{
#ifdef VPHYSICS
						// VPHYSICS!
						if (player->mo->subsector->sector && player->mo->subsector->sector->f_slope)
						{
							v3float_t vector = player->mo->subsector->sector->f_slope->normalf;

							player->mo->momx += FLOAT_TO_FIXED(vector.x)*6;
							player->mo->momy += FLOAT_TO_FIXED(vector.y)*6;
							player->mo->momz += FLOAT_TO_FIXED(vector.z);

						}
#endif
						P_SetObjectMomZ(player->mo, -player->mo->momz/2.0f, false, true);
					}

					player->secondjump = false;
					player->jumping = false;
					player->pflags &= ~PF_JUMPED;
					player->pflags &= ~PF_SPINNING;

					if (player->dobounce && player->mo->momz > 0)
					{
						player->dobounce = false;
						if (//(player->mo->momz > -FIXEDSCALE(18*FRACUNIT, player->mo->scale)
							//&& player->mo->z < player->mo->floorz+FIXEDSCALE(28*FRACUNIT, player->mo->scale))
							//||
							player->mo->z+player->mo->momz < player->mo->floorz+FIXEDSCALE(8*FRACUNIT, player->mo->scale))
						{
							//P_SetMobjState(the orb, bendy state);
							S_StartSound(player->mo, sfx_s3k_19);
						}
					}
				}
			}

		}

	}


	// The player's hit range increases when using the character's ability
	if (player->powers[pw_flameshield])
	{
		if (player->pflags & PF_THOKKED)
		{
			if (leveltime % 2 == 0)
				P_FlameTrail(player->mo); // all nearby enemies will be flamed
		}
	}
#endif


	// This is separate so that P_DamageMobj in p_inter.c can call it, too.
	if (player->blackow)
	{
		if (player->blackow == 2)
			S_StartSound (player->mo, sfx_zoom);
		else
			S_StartSound (player->mo, sfx_bkpoof); // Sound the BANG!

		for (i = 0; i < MAXPLAYERS; i++)
			if (playeringame[i] && P_AproxDistance(player->mo->x - players[i].mo->x,
				player->mo->y - players[i].mo->y) < 1536*FRACUNIT)
			{
				players[i].bonuscount += 10; // Flash the palette.
			}

		player->blackow = 3;
		P_NukeEnemies(player); // Search for all nearby enemies and nuke their pants off!
		player->blackow = 0;
	}

	// HOMING option.
	if (player->charability == CA_HOMINGTHOK)
	{
		// If you've got a target, chase after it!
		if (player->homing && player->mo->tracer)
		{
			mobj_t *item = P_SpawnThokMobj(player);

			if (item && (player->charflags & SF_GHOSTTHOKITEM))
			{
				P_SpawnGhostMobj(item);
				P_SetMobjState(item, S_DISS);
			}

			P_HomingAttack(player->mo, player->mo->tracer);

			// But if you don't, then stop homing.
			if (player->mo->tracer->health <= 0 || (player->mo->tracer->flags2 & MF2_FRET))
			{
				if (player->mo->eflags & MFE_UNDERWATER)
				{
					P_SetObjectMomZ(player->mo, FixedDiv(457*FRACUNIT,72*FRACUNIT), false, true);
				}
				else
					P_SetObjectMomZ(player->mo, 10*FRACUNIT/NEWTICRATERATIO, false, true);

                // Reset the player's momentum so he doesn't go catapulting off stuff
				player->mo->momx = player->mo->momy = 0;

				// Special flag that allows you to move more in air when used on the player
				player->mo->flags2 |= MF2_FRET;

				player->homing = 0;

				if (player->mo->tracer->flags2 & MF2_FRET)
					P_InstaThrust(player->mo, player->mo->angle, -(player->speed <<(FRACBITS-3)));

				if (!(player->mo->tracer->flags & MF_BOSS))
					player->pflags &= ~PF_THOKKED;
			}
		}

		// If you're not jumping, then you obviously wouldn't be homing.
		if (!(player->pflags & PF_JUMPED))
			player->homing = 0;
	}
	else
		player->homing = 0;


// Little flag just for better homing
		if (onground)
		{
		player->mo->flags2 &= ~MF2_FRET;
		}

	P_CheckPlayerClimb(player);

	P_CheckTeeter(player);

/////////////////
// FIRING CODE //
/////////////////

// These make stuff WAAAAYY easier to understand! // SRB2CBTODO: CLEAN UP
	// Toss a flag
	if ((gametype == GT_CTF || (gametype == GT_MATCH && cv_matchtype.value))
		&& (cmd->buttons & BT_TOSSFLAG) && !(player->powers[pw_super]) && !(player->tossdelay))
	{
		if (!(player->gotflag & MF_REDFLAG || player->gotflag & MF_BLUEFLAG))
			P_PlayerEmeraldBurst(player, true); // Toss emeralds
		else
			P_PlayerFlagBurst(player, true);
	}

	// check for fire
	if (cmd->buttons & BT_ATTACK || cmd->buttons & BT_FIRENORMAL)
	{
#if 0 // Auto-join a team in team match/CTF
		// Spectator respawn code
		if (((gametype == GT_CTF || (gametype == GT_MATCH && cv_matchtype.value)) && player->spectator)
			&& !player->powers[pw_flashing] && !(player->pflags & PF_JUMPDOWN))
			P_DamageMobj(player->mo, NULL, NULL, 42000);
#endif

		if (mariomode)
		{
			if (!(player->pflags & PF_ATTACKDOWN) && player->powers[pw_fireflower]
				&& !player->climbing)
			{
				player->pflags |= PF_ATTACKDOWN;
				P_SPMAngle(player->mo, MT_FIREBALL, player->mo->angle, true, true, 0, false);
				S_StartSound(player->mo, sfx_thok);
			}
		}
		else if (player->currentweapon == WEP_GRENADE && !player->weapondelay && !(cmd->buttons & BT_FIRENORMAL))
		{
			if (player->tossstrength < 8*FRACUNIT)
				player->tossstrength = 8*FRACUNIT;

			player->tossstrength += FRACUNIT/2;

			if (player->tossstrength > (MAXMOVE/4))
				player->tossstrength = (MAXMOVE/4);
		}
		else if ((((gametype == GT_MATCH || gametype == GT_CTF || cv_ringslinger.value)
			&& player->mo->health > 1 && (((!(player->pflags & PF_ATTACKDOWN)
				|| (player->currentweapon == WEP_AUTO && player->powers[pw_automaticring] && cmd->buttons & BT_ATTACK)) && !player->weapondelay)))
			|| (gametype == GT_TAG &&
			player->mo->health > 1 && (((!(player->pflags & PF_ATTACKDOWN)
				|| (player->currentweapon == WEP_AUTO && player->powers[pw_automaticring] && cmd->buttons & BT_ATTACK)) && !player->weapondelay))
			&& (player->pflags & PF_TAGIT))) && !player->climbing && !player->exiting) // don't fire when you're already done
		{
			player->pflags |= PF_ATTACKDOWN;

			// Just like with jump height, adjust the firing height slightly when in reverse gravity.
			if (player->mo->eflags & MFE_VERTICALFLIP)
				player->mo->z -= 8*FRACUNIT;

			if (cmd->buttons & BT_FIRENORMAL) // No powers, just a regular ring.
			{
				mobj_t *mo;
				player->weapondelay = TICRATE/4;

				if (player->skin == 2) // Knuckles
				{
					player->weapondelay /= 3;
					player->weapondelay *= 2;
				}

				mo = P_SpawnPlayerMissile(player->mo, MT_REDRING, 0, false);

				if (mo)
					P_ColorTeamMissile(mo, player);

				player->mo->health--;
				player->health--;
			}
			else
			{
				mobj_t *mo = NULL;

				if (player->currentweapon == WEP_BOUNCE && player->powers[pw_bouncering])
				{
					// Bounce ring

					player->weapondelay = TICRATE/3;

					if (player->skin == 2) // Knuckles
					{
						player->weapondelay /= 3;
						player->weapondelay *= 2;
					}

					mo = P_SpawnPlayerMissile(player->mo, MT_THROWNBOUNCE, MF2_BOUNCERING, false);

					if (mo)
						mo->fuse = 3*TICRATE; // Bounce Ring time

					player->powers[pw_bouncering]--;
					player->mo->health--;
					player->health--;
				}
				else if (player->currentweapon == WEP_RAIL && player->powers[pw_railring])
				{
					// Rail ring

					player->weapondelay = (3*TICRATE)/2;

					if (player->skin == 2) // Knuckles
					{
						player->weapondelay /= 3;
						player->weapondelay *= 2;
					}

					mo = P_SpawnPlayerMissile(player->mo, MT_REDRING, MF2_RAILRING|MF2_DONTDRAW, false);

#ifdef WEAPON_SFX
					// Due to the fact that the rail has no unique thrown object, this hack is necessary.
					S_StartSound(player->mo, sfx_rail);
#endif

					player->powers[pw_railring]--;
					player->mo->health--;
					player->health--;
				}
				else if (player->currentweapon == WEP_AUTO && player->powers[pw_automaticring])
				{
					// Automatic weapon and the delaw
					player->weapondelay = 2*NEWTICRATERATIO;

					if (player->skin == 2) // Knuckles
						player->weapondelay = 1*NEWTICRATERATIO;

					mo = P_SpawnPlayerMissile(player->mo, MT_THROWNAUTOMATIC, MF2_AUTOMATIC, false);

					mo->flags = MF_NOBLOCKMAP|MF_MISSILE|MF_NOGRAVITY;

					if (mo && cv_ringcolor.value) // LXShadow: Perhaps another consvar should be used to determine how Automatics look.
                        P_ColorTeamMissile(mo, player);

					player->powers[pw_automaticring]--;
					player->mo->health--;
					player->health--;
				}
				else if (player->currentweapon == WEP_EXPLODE && player->powers[pw_explosionring])
				{
					// Exploding

					player->weapondelay = (TICRATE/4)*3;

					if (player->skin == 2) // Knuckles
					{
						player->weapondelay /= 3;
						player->weapondelay *= 2;
					}

					mo = P_SpawnPlayerMissile(player->mo, MT_THROWNEXPLOSION, MF2_EXPLOSION, false);

					player->powers[pw_explosionring]--;
					player->mo->health--;
					player->health--;
				}
				else if (player->currentweapon == WEP_SCATTER && player->powers[pw_scatterring])
				{
					fixed_t oldz;
					angle_t shotangle = player->mo->angle;
					angle_t oldaiming = player->aiming;

					// Scatter

					player->weapondelay = (TICRATE/3)*2;

					if (player->skin == 2) // Knuckles
					{
						player->weapondelay /= 3;
						player->weapondelay *= 2;
					}

					oldz = player->mo->z;

					// Center
					mo = P_SpawnPlayerMissile(player->mo, MT_THROWNSCATTER, MF2_SCATTER, false);
					if (mo)
					{
						//P_ColorTeamMissile(mo, player);
						shotangle = R_PointToAngle2(player->mo->x, player->mo->y, mo->x, mo->y);
					}

					// Left
					mo = P_SPMAngle(player->mo, MT_THROWNSCATTER, shotangle-ANG2, false, true, MF2_SCATTER, false);
					//if (mo)
					//P_ColorTeamMissile(mo, player);

					// Right
					mo = P_SPMAngle(player->mo, MT_THROWNSCATTER, shotangle+ANG2, false, true, MF2_SCATTER, false);
					//if (mo)
					//P_ColorTeamMissile(mo, player);

					// Down
					player->mo->z += 12*FRACUNIT;
					player->aiming += ANG1;
					mo = P_SPMAngle(player->mo, MT_THROWNSCATTER, shotangle, false, true, MF2_SCATTER, false);
					//if (mo)
					//P_ColorTeamMissile(mo, player);

					// Up
					player->mo->z -= 24*FRACUNIT;
					player->aiming -= ANG2;
					mo = P_SPMAngle(player->mo, MT_THROWNSCATTER, shotangle, false, true, MF2_SCATTER, false);
					//if (mo)
					//P_ColorTeamMissile(mo, player);

#ifdef WEAPON_SFX
					// Due to the fact that the scatter has no unique thrown object, this hack is necessary.
					S_StartSound(player->mo, sfx_s3k_26);
#endif

					player->mo->z = oldz;
					player->aiming = oldaiming;

					player->powers[pw_scatterring]--;
					player->mo->health--;
					player->health--;
				}
				else // No powers, just a regular ring.
				{
					player->weapondelay = TICRATE/4;

					if (player->skin == 2) // Knuckles
					{
						player->weapondelay /= 3;
						player->weapondelay *= 2;
					}

					mo = P_SpawnPlayerMissile(player->mo, MT_REDRING, 0, false);

					if (mo)
						P_ColorTeamMissile(mo, player);

					player->mo->health--;
					player->health--;
				}
				if (mo)
				{
					if ((mo->flags & MF_MISSILE) && ((mo->flags2 & MF2_RAILRING)))
					{
						const boolean nblockmap = !(mo->flags & MF_NOBLOCKMAP);
						for (i = 0; i < 256; i++)
						{
							if (nblockmap)
							{
								P_UnsetThingPosition(mo);
								mo->flags |= MF_NOBLOCKMAP;
								P_SetThingPosition(mo);
							}

							if (i&1)
								P_SpawnMobj(mo->x, mo->y, mo->z, MT_SPARK);

							P_RailThinker(mo);
						}
					}
				}
			}

			// Since we adjusted the player's height in reverse gravity, put it back.
			if (player->mo->eflags & MFE_VERTICALFLIP)
				player->mo->z += 8*FRACUNIT;

			return;
		}
	}
	else
	{
		if (player->currentweapon == WEP_GRENADE && player->powers[pw_grenadering] && player->tossstrength
			&& (gametype == GT_MATCH || gametype == GT_CTF || (gametype == GT_TAG && player->pflags & PF_TAGIT) || cv_ringslinger.value)
			&& player->mo->health > 1 && !player->climbing)
		{
			mobj_t *mo;
			angle_t oldaim = player->aiming;

			//Just like with jump height, adjust the firing height slightly when in reverse gravity. // SRB2CBTODO: Checkme
			if (player->mo->eflags & MFE_VERTICALFLIP)
				player->mo->z -= 8*FRACUNIT;

			// Toss the grenade!
			player->weapondelay = TICRATE;

			if (player->skin == 2) // Knuckles
				player->weapondelay /= 2;

			player->aiming += ANG45/2;

			if (player->aiming > ANG90-1)
				player->aiming = ANG90-1;

			mo = P_SPMAngle(player->mo, MT_THROWNGRENADE, player->mo->angle, true, true, MF2_GRENADE, false);

			player->aiming = oldaim;

			if (mo) // LXShadow: Copypasted 2.0.5's code here.
			{
				P_InstaThrust(mo, player->mo->angle, player->tossstrength);
				mo->momz = player->tossstrength;
				if (player->mo->eflags & MFE_VERTICALFLIP)
				{
				    mo->flags2 |= MF2_OBJECTFLIP;
				    mo->momz = -mo->momz;
				}
				mo->fuse = mo->info->mass*NEWTICRATERATIO; // LXShadow: NEWTICRATERATIO was not in 2.0.5's code.
				P_SetTarget(&mo->target, player->mo);
			}

			player->powers[pw_grenadering]--;
			player->mo->health--;
			player->health--;
		}
		player->pflags &= ~PF_ATTACKDOWN;
		player->tossstrength = 0;
	}

	// Less height while spinning. Good for spinning under things...?
	if ((player->mo->state == &states[player->mo->info->painstate]
		|| player->mo->state == &states[S_PLAY_SUPERHIT])
		|| ((player->charability2 == CA2_SPINDASH) && ((player->pflags & PF_SPINNING) || (player->pflags & PF_JUMPED)))
		|| (player->powers[pw_tailsfly])
		|| (player->pflags & PF_GLIDING) || (player->charability == CA_FLY
		&& (player->mo->state >= &states[S_PLAY_SPC1]
		&& player->mo->state <= &states[S_PLAY_SPC4])))
	{
		player->mo->height = P_GetPlayerSpinHeight(player);
	}
	else
		player->mo->height = P_GetPlayerHeight(player);

	// Crush test...
	if ((player->mo->ceilingz - player->mo->floorz < player->mo->height)
		&& !(player->mo->flags & MF_NOCLIP))
	{
		if ((player->charability2 == CA2_SPINDASH) && !(player->pflags & PF_SPINNING))
		{
			P_ResetScore(player);
			player->pflags |= PF_SPINNING;
			P_SetPlayerMobjState(player->mo, S_PLAY_ATK1);
		}
		else if (player->mo->ceilingz - player->mo->floorz < player->mo->height)
		{
			if (netgame && player->spectator)
				P_DamageMobj(player->mo, NULL, NULL, 42000); // Respawn crushed spectators
			else
			{
				mobj_t *killer;

				killer = P_SpawnMobj(player->mo->x, player->mo->y, player->mo->z, MT_DISS);
				killer->threshold = 44; // Special flag that it was crushing which killed you.

				P_DamageMobj(player->mo, killer, killer, 10000);
			}

			if (player->playerstate == PST_DEAD)
				return;
		}
	}

	// Check for taunt button
	if ((netgame || multiplayer) && (cmd->buttons & BT_TAUNT) && !player->taunttimer)
	{
		P_PlayTauntSound(player->mo);
		player->taunttimer = 3*TICRATE; // 3 second pause between any taunts
	}

#ifdef FLOORSPLATS
		R_AddFloorSplat(player->mo->subsector, player->mo, "SHADOW", player->mo->x,
			player->mo->y, player->mo->floorz, SPLATDRAWMODE_OPAQUE);
#endif

	// SRB2CBTODO: Use a special start/end thing to make cool path thingys,
	// like zip-lines or Super Mario Galaxy like star shoots!
	// SRB2CBTODO: Use double jump or second button?
	// Light dashing
#ifdef LIGHTDASH
	if ((player->pflags & PF_JUMPED) && !player->secondjump)
	{
		if (P_RingNearby(player))
		{
			if (cmd->buttons & BT_JUMP)
			{
				P_LookForRings(player);
				//player->lightdash = TICRATE; // This is how much time the player has to get to the first waypoint
			}
		}
		//else if (player->pflags & PF_LIGHTDASH)
		//	player->pflags &= ~PF_LIGHTDASH;
	}
#endif

	// Look for blocks to bust up
	// These need to be constantly checked // SRB2CBTODO: Optimize
	if (CheckForBustableBlocks && !(netgame && player->spectator)) // SRB2CBTODO: It doesn't have to be a netgame for someone to be a spectator
		P_CheckBustBlocks(player->mo);

	// Special handling for
	// gliding in 2D mode
	if ((twodlevel || (player->mo->flags2 & MF2_TWOD)) && (player->pflags & PF_GLIDING) && player->charability == CA_GLIDEANDCLIMB
		&& !(player->mo->flags & MF_NOCLIP))
	{
		fixed_t oldx;
		fixed_t oldy;

		oldx = player->mo->x;
		oldy = player->mo->y;

		P_UnsetThingPosition(player->mo);
		player->mo->x += player->mo->momx;
		player->mo->y += player->mo->momy;
		P_SetThingPosition(player->mo);

		for (node = player->mo->touching_sectorlist; node; node = node->m_snext)
		{
			if (!node->m_sector)
				break;

			if (node->m_sector->ffloors)
			{
				ffloor_t *rover;

				for (rover = node->m_sector->ffloors; rover; rover = rover->next)
				{
					if (!(rover->flags & FF_EXISTS)) continue;

					fixed_t topheight = *rover->topheight;
					fixed_t bottomheight = *rover->bottomheight;

#ifdef ESLOPE
					if (rover->t_slope)
						topheight = P_GetZAt(rover->t_slope, player->mo->x, player->mo->y);

					if (rover->b_slope)
						bottomheight = P_GetZAt(rover->b_slope, player->mo->x, player->mo->y);
#endif

					if ((rover->flags & FF_BLOCKPLAYER))
					{
						if (topheight > player->mo->z && bottomheight < player->mo->z)
						{
							P_ResetPlayer(player);
							player->climbing = 5;
							player->mo->momx = player->mo->momy = player->mo->momz = 0;
							break;
						}
					}
				}
			}

			if (player->mo->z+player->mo->height > node->m_sector->ceilingheight
				&& node->m_sector->ceilingpic == skyflatnum)
				continue;

			if (P_PlayerZAtSecF(player, node->m_sector) > player->mo->z
				|| node->m_sector->ceilingheight < player->mo->z)
			{
				P_ResetPlayer(player);
				player->climbing = 5;
				player->mo->momx = player->mo->momy = player->mo->momz = 0;
				break;
			}
		}
		P_UnsetThingPosition(player->mo);
		player->mo->x = oldx; // SRB2CBTODO: Custom moving here, check this
		player->mo->y = oldy;
		P_SetThingPosition(player->mo);
	}

	// Check for a BOUNCY sector!
	if (CheckForBouncySector)
	{
		fixed_t oldx;
		fixed_t oldy;
		fixed_t oldz;

		oldx = player->mo->x;
		oldy = player->mo->y;
		oldz = player->mo->z;

		P_UnsetThingPosition(player->mo);
		player->mo->x += player->mo->momx;
		player->mo->y += player->mo->momy;
		player->mo->z += player->mo->momz;
		P_SetThingPosition(player->mo);

		for (node = player->mo->touching_sectorlist; node; node = node->m_snext)
		{
			if (!node->m_sector)
				break;

			if (node->m_sector->ffloors)
			{
				ffloor_t *rover;
				boolean top = true;

				for (rover = node->m_sector->ffloors; rover; rover = rover->next)
				{
					fixed_t topheight = *rover->topheight;
					fixed_t bottomheight = *rover->bottomheight;

#ifdef ESLOPE
					if (rover->t_slope)
						topheight = P_GetZAt(rover->t_slope, player->mo->x, player->mo->y);

					if (rover->b_slope)
						bottomheight = P_GetZAt(rover->b_slope, player->mo->x, player->mo->y);
#endif

					if (player->mo->z > topheight)
						continue;

					if (player->mo->z + player->mo->height < bottomheight)
						continue;

					if (oldz < topheight && oldz > bottomheight)
						top = false;

					if (GETSECSPECIAL(rover->master->frontsector->special, 1) == 15)
					{
						fixed_t linedist;

						linedist = P_AproxDistance(rover->master->v1->x-rover->master->v2->x, rover->master->v1->y-rover->master->v2->y);

						linedist = FixedDiv(linedist,100*FRACUNIT);

						if (top)
						{
							fixed_t newmom;

							newmom = -FixedMul(player->mo->momz,linedist);

							if (newmom < (linedist*2)
								&& newmom > -(linedist*2))
							{
								goto bouncydone;
							}

							if (!(rover->master->flags & ML_BOUNCY))
							{
								if (newmom > 0)
								{
									if (newmom < 8*FRACUNIT)
										newmom = 8*FRACUNIT;
								}
								else if (newmom > -8*FRACUNIT && newmom != 0)
									newmom = -8*FRACUNIT;
							}

							if (newmom > P_GetPlayerHeight(player)/2)
								newmom = P_GetPlayerHeight(player)/2;
							else if (newmom < -P_GetPlayerHeight(player)/2)
								newmom = -P_GetPlayerHeight(player)/2;

							player->mo->momz = newmom;

							if (player->pflags & PF_SPINNING)
							{
								player->pflags &= ~PF_SPINNING;
								player->pflags |= PF_JUMPED;
								player->pflags |= PF_THOKKED;
							}
						}
						else
						{
							player->mo->momx = -FixedMul(player->mo->momx,linedist);
							player->mo->momy = -FixedMul(player->mo->momy,linedist);

							if (player->pflags & PF_SPINNING)
							{
								player->pflags &= ~PF_SPINNING;
								player->pflags |= PF_JUMPED;
								player->pflags |= PF_THOKKED;
							}
						}

						if ((player->pflags & PF_SPINNING) && player->speed < 1 && player->mo->momz)
						{
							player->pflags &= ~PF_SPINNING;
							player->pflags |= PF_JUMPED;
						}

						goto bouncydone;
					}
				}
			}
		}
bouncydone:
		P_UnsetThingPosition(player->mo);
		player->mo->x = oldx;
		player->mo->y = oldy;
		player->mo->z = oldz;
		P_SetThingPosition(player->mo);
	}


	// Look for Quicksand!
	if (CheckForQuicksand && player->mo->subsector->sector->ffloors && player->mo->momz <= 0)
	{
		ffloor_t *rover;
		fixed_t sinkspeed, friction;

		for (rover = player->mo->subsector->sector->ffloors; rover; rover = rover->next)
		{
			if (!(rover->flags & FF_EXISTS)) continue;

			if (!(rover->flags & FF_QUICKSAND))
				continue;

			fixed_t topheight = *rover->topheight;
			fixed_t bottomheight = *rover->bottomheight;

#ifdef ESLOPE
			if (rover->t_slope)
				topheight = P_GetZAt(rover->t_slope, player->mo->x, player->mo->y);

			if (rover->b_slope)
				bottomheight = P_GetZAt(rover->b_slope, player->mo->x, player->mo->y);
#endif

			if (topheight >= player->mo->z && bottomheight < player->mo->z + player->mo->height)
			{
				sinkspeed = abs(rover->master->v1->x - rover->master->v2->x)>>1;

				sinkspeed = FixedDiv(sinkspeed,TICRATE*FRACUNIT);

				player->mo->z -= sinkspeed;

				if (player->mo->z < P_PlayerZAtF(player))
					player->mo->z = P_PlayerZAtF(player);

				friction = abs(rover->master->v1->y - rover->master->v2->y)>>6;

				player->mo->momx = FixedMul(player->mo->momx, friction);
				player->mo->momy = FixedMul(player->mo->momy, friction);
			}
		}
	}
}

static void P_DoZoomTube(player_t *player)
{
	int sequence;
	fixed_t speed;
	thinker_t *th;
	mobj_t *mo2;
	mobj_t *waypoint = NULL;
	fixed_t dist;
	boolean reverse;
	fixed_t speedx,speedy,speedz;

	player->mo->height = P_GetPlayerSpinHeight(player);

	if (player->speed > 0)
		reverse = false;
	else
		reverse = true;

	player->powers[pw_flashing] = 1;

	speed = abs(player->speed);

	sequence = player->mo->tracer->threshold;

	// change slope
	dist = P_AproxDistance(P_AproxDistance(player->mo->tracer->x - player->mo->x,
										   player->mo->tracer->y - player->mo->y), player->mo->tracer->z - player->mo->z);

	if (dist < 1)
		dist = 1;

	speedx = FixedMul(FixedDiv(player->mo->tracer->x - player->mo->x, dist), (speed));
	speedy = FixedMul(FixedDiv(player->mo->tracer->y - player->mo->y, dist), (speed));
	speedz = FixedMul(FixedDiv(player->mo->tracer->z - player->mo->z, dist), (speed));

	// Calculate the distance between the player and the waypoint
	// 'dist' already equals this.

	// Will the player be FURTHER away if the momx/momy/momz is added to
	// his current coordinates, or closer? (shift down to fracunits to avoid approximation errors)
	if (dist >> FRACBITS <= P_AproxDistance(P_AproxDistance(player->mo->tracer->x - player->mo->x - speedx, player->mo->tracer->y - player->mo->y - speedy),
											player->mo->tracer->z - player->mo->z - speedz)>>FRACBITS)
	{
		// If further away, set XYZ of player to waypoint location
		P_UnsetThingPosition(player->mo);
		player->mo->x = player->mo->tracer->x;
		player->mo->y = player->mo->tracer->y;
		player->mo->z = player->mo->tracer->z;
		P_SetThingPosition(player->mo);

		// SRB2CBTODO: Should all objects get their floor/ceilheights updated on SetThingPosistion
		player->mo->floorz = P_PlayerZAtF(player);
		player->mo->ceilingz = player->mo->subsector->sector->ceilingheight; // SRB2CBTODO: SLOPES on CEILINGS

		if (cv_devmode == 2)
			CONS_Printf("Looking for next waypoint...\n");

		// Find next waypoint
		for (th = thinkercap.next; th != &thinkercap; th = th->next)
		{
			if (th->function.acp1 != (actionf_p1)P_MobjThinker) // Not a mobj thinker
				continue;

			mo2 = (mobj_t *)th;

			if (mo2->type != MT_TUBEWAYPOINT)
				continue;

			if (mo2->threshold == sequence)
			{
				if ((reverse && mo2->health == player->mo->tracer->health - 1)
					|| (!reverse && mo2->health == player->mo->tracer->health + 1))
				{
					waypoint = mo2;
					break;
				}
			}
		}

		if (waypoint)
		{
			if (cv_devmode == 2)
				CONS_Printf("Found waypoint (sequence %ld, number %ld).\n", waypoint->threshold, waypoint->health);

			// calculate MOMX/MOMY/MOMZ for next waypoint
			// change angle
			player->mo->angle = R_PointToAngle2(player->mo->x, player->mo->y, player->mo->tracer->x, player->mo->tracer->y);

			if (player == &players[consoleplayer])
				localangle = player->mo->angle;
			else if (splitscreen && player == &players[secondarydisplayplayer])
				localangle2 = player->mo->angle;

			// change slope
			dist = P_AproxDistance(P_AproxDistance(player->mo->tracer->x - player->mo->x, player->mo->tracer->y - player->mo->y),
								   player->mo->tracer->z - player->mo->z);

			if (dist < 1)
				dist = 1;

			player->mo->momx = FixedMul(FixedDiv(player->mo->tracer->x - player->mo->x, dist), (speed));
			player->mo->momy = FixedMul(FixedDiv(player->mo->tracer->y - player->mo->y, dist), (speed));
			player->mo->momz = FixedMul(FixedDiv(player->mo->tracer->z - player->mo->z, dist), (speed));

			P_SetTarget(&player->mo->tracer, waypoint);
		}
		else
		{
			P_SetTarget(&player->mo->tracer, NULL); // Else, we just let him fly.

			if (cv_devmode == 2)
				CONS_Printf("Next waypoint not found, releasing from track...\n");
		}
	}
	else
	{
		player->mo->momx = speedx;
		player->mo->momy = speedy;
		player->mo->momz = speedz;
	}

	// change angle
	if (player->mo->tracer)
	{
		player->mo->angle = R_PointToAngle2(player->mo->x, player->mo->y, player->mo->tracer->x, player->mo->tracer->y);

		if (player == &players[consoleplayer])
			localangle = player->mo->angle;
		else if (splitscreen && player == &players[secondarydisplayplayer])
			localangle2 = player->mo->angle;
	}
}



//
// P_DoRopeHang
//
// Kinda like P_DoZoomTube
// but a little different.
//
static void P_DoRopeHang(player_t *player, boolean minecart)
{
	int sequence;
	fixed_t speed;
	thinker_t *th;
	mobj_t *mo2;
	mobj_t *waypoint = NULL;
	fixed_t dist;
	fixed_t speedx,speedy,speedz;
	fixed_t playerz;

	if (!minecart)
	{
		player->mo->height = P_GetPlayerHeight(player);

		if (player->cmd.buttons & BT_USE
			&& !(player->pflags & PF_STASIS || player->powers[pw_nocontrol])) // Drop off of the rope
		{
			P_SetTarget(&player->mo->tracer, NULL);

			player->pflags |= PF_JUMPED;
			player->pflags &= ~PF_ROPEHANG;

			if (!(player->pflags & PF_SLIDING) && (player->pflags & PF_JUMPED) && !player->powers[pw_super]
				&& (player->mo->state - states < S_PLAY_ATK1
				|| player->mo->state - states > S_PLAY_ATK4) && player->charability2 == CA2_SPINDASH)
			{
				P_SetPlayerMobjState(player->mo, S_PLAY_ATK1);
			}

			return;
		}

		// Play the 'clink' sound only if the player is moving.
		if (!(leveltime & 7*NEWTICRATERATIO) && player->speed)
			S_StartSound(player->mo, sfx_s3k_36);

		playerz = player->mo->z + player->mo->height;
	}
	else
		playerz = player->mo->z;

	speed = abs(player->speed);

	sequence = player->mo->tracer->threshold;

	// change slope
	dist = P_AproxDistance(P_AproxDistance(player->mo->tracer->x - player->mo->x, player->mo->tracer->y - player->mo->y),
						   player->mo->tracer->z - playerz);

	if (dist < 1)
		dist = 1;

	speedx = FixedMul(FixedDiv(player->mo->tracer->x - player->mo->x, dist), (speed));
	speedy = FixedMul(FixedDiv(player->mo->tracer->y - player->mo->y, dist), (speed));
	speedz = FixedMul(FixedDiv(player->mo->tracer->z - playerz, dist), (speed));

	// If not allowed to move, we're done here.
	if (!speed)
		return;

	// Calculate the distance between the player and the waypoint
	// 'dist' already equals this.

	// Will the player be FURTHER away if the momx/momy/momz is added to
	// his current coordinates, or closer? (shift down to fracunits to avoid approximation errors)
	if (dist>>FRACBITS <= P_AproxDistance(P_AproxDistance(player->mo->tracer->x - player->mo->x - speedx,
											player->mo->tracer->y - player->mo->y - speedy),
											player->mo->tracer->z - playerz - speedz)>>FRACBITS)
	{
		// If further away, set XYZ of player to waypoint location
		P_UnsetThingPosition(player->mo);
		player->mo->x = player->mo->tracer->x;
		player->mo->y = player->mo->tracer->y;

		if (minecart)
			player->mo->z = player->mo->tracer->z;
		else
			player->mo->z = player->mo->tracer->z - player->mo->height;

		P_SetThingPosition(player->mo);

		if (cv_devmode == 2)
			CONS_Printf("Looking for next waypoint...\n");

		// Find next waypoint
		for (th = thinkercap.next; th != &thinkercap; th = th->next)
		{
			if (th->function.acp1 != (actionf_p1)P_MobjThinker) // Not a mobj thinker
				continue;

			mo2 = (mobj_t *)th;

			if (mo2->type != MT_TUBEWAYPOINT)
				continue;

			if (mo2->threshold == sequence)
			{
				if (mo2->health == player->mo->tracer->health + 1)
				{
					waypoint = mo2;
					break;
				}
			}
		}

		if (!(player->mo->tracer->flags & MF_SLIDEME) && !waypoint)
		{
			if (cv_devmode == 2)
				CONS_Printf("Next waypoint not found, wrapping to start...\n");

			// Wrap around back to first waypoint
			for (th = thinkercap.next; th != &thinkercap; th = th->next)
			{
				if (th->function.acp1 != (actionf_p1)P_MobjThinker) // Not a mobj thinker
					continue;

				mo2 = (mobj_t *)th;

				if (mo2->type != MT_TUBEWAYPOINT)
					continue;

				if (mo2->threshold == sequence)
				{
					if (mo2->health == 0)
					{
						waypoint = mo2;
						break;
					}
				}
			}
		}

		if (waypoint)
		{
			if (cv_devmode == 2)
				CONS_Printf("Found waypoint (sequence %ld, number %ld).\n", waypoint->threshold, waypoint->health);

			// calculate MOMX/MOMY/MOMZ for next waypoint
			// change slope
			dist = P_AproxDistance(P_AproxDistance(player->mo->tracer->x - player->mo->x, player->mo->tracer->y - player->mo->y),
								   player->mo->tracer->z - playerz);

			if (dist < 1)
				dist = 1;

			player->mo->momx = FixedMul(FixedDiv(player->mo->tracer->x - player->mo->x, dist), (speed));
			player->mo->momy = FixedMul(FixedDiv(player->mo->tracer->y - player->mo->y, dist), (speed));
			player->mo->momz = FixedMul(FixedDiv(player->mo->tracer->z - playerz, dist), (speed));

			P_SetTarget(&player->mo->tracer, waypoint);
		}
		else
		{
			if (player->mo->tracer->flags & MF_SLIDEME)
			{
				player->pflags |= PF_JUMPED;
				player->pflags &= ~PF_ROPEHANG;

				if (!(player->pflags & PF_SLIDING) && (player->pflags & PF_JUMPED) && !player->powers[pw_super]
				&& (player->mo->state - states < S_PLAY_ATK1
				|| player->mo->state - states > S_PLAY_ATK4) && player->charability2 == CA2_SPINDASH)
				{
					P_SetPlayerMobjState(player->mo, S_PLAY_ATK1);
				}
			}

			P_SetTarget(&player->mo->tracer, NULL);

			if (cv_devmode)
				CONS_Printf("Next waypoint not found!\n");
		}
	}
	else
	{
		player->mo->momx = speedx;
		player->mo->momy = speedy;
		player->mo->momz = speedz;
	}
}


#ifdef SRB2K
//
// P_FlameTrail
// Sends a trail of fire!
//
void P_FlameTrail(mobj_t *mobj)
{
	int i;
	const fixed_t ns = 8<<FRACBITS;
	mobj_t *mo;
	angle_t fa;

	for (i = 0; i < 4; i++)
	{
		fa = (i*(FINEANGLES/4));
		mo = P_SpawnMobj(mobj->x, mobj->y, mobj->z + (mobj->height/3), MT_SPINFIRE);
		P_SetTarget(&mo->target, mobj);
		mo->flags = MF_NOBLOCKMAP|MF_MISSILE|MF_NOGRAVITY|MF_FIRE;
		if (mo->target->eflags & MFE_VERTICALFLIP)
			mo->flags |= MFE_VERTICALFLIP;
		mo->scale = mo->target->scale;
		mo->scalespeed = 4;
		mo->destscale = mo->target->scale*4; // Scale out :D
		mo->fuse = 3*TICRATE;
		mo->momx = FixedMul(FINESINE(fa), ns)/NEWTICRATERATIO;
		mo->momy = FixedMul(FINECOSINE(fa), ns)/NEWTICRATERATIO;
	}
}

#endif


#if 0 // P_NukeAllPlayers
//
// P_NukeAllPlayers
//
// Hurts all players(or maybe things near the ground)
//
// SRB2CBTODO: P_Floorattack
static void P_NukeAllPlayers(player_t *player)
{
	mobj_t *mo;
	thinker_t *think;

	for (think = thinkercap.next; think != &thinkercap; think = think->next)
	{
		if (think->function.acp1 != (actionf_p1)P_MobjThinker)
			continue; // not a mobj thinker

		mo = (mobj_t *)think;

		if (!mo->player)
			continue;

		if (mo->health <= 0) // dead
			continue;

		if (mo == player->mo)
			continue;

		P_DamageMobj(mo, player->mo, player->mo, 1);
	}

	return;
}
#endif

//
// P_NukeEnemies
// Looks for something you can hit - Used for bomb shield
//
void P_NukeEnemies(player_t *player)
{
	const fixed_t dist = 1536 << FRACBITS;
	const fixed_t ns = 60 << FRACBITS;
	mobj_t *mo;
	angle_t fa;
	thinker_t *think;
	int i;

	for (i = 0; i < 16; i++)
	{
		fa = (i*(FINEANGLES/16));
		mo = P_SpawnMobj(player->mo->x, player->mo->y, player->mo->z, MT_SUPERSPARK);
		mo->momx = FixedMul(FINESINE(fa),ns)/NEWTICRATERATIO;
		mo->momy = FixedMul(FINECOSINE(fa),ns)/NEWTICRATERATIO;
	}

	for (think = thinkercap.next; think != &thinkercap; think = think->next)
	{
		if (think->function.acp1 != (actionf_p1)P_MobjThinker)
			continue; // not a mobj thinker

		mo = (mobj_t *)think;

		if (!(mo->flags & MF_SHOOTABLE))
			continue;

		if (mo->flags & MF_MONITOR)
			continue; // Monitors cannot be 'nuked'.

		if ((gametype == GT_COOP || gametype == GT_RACE) && mo->type == MT_PLAYER)
			continue; // Don't hurt players in Co-Op!

		if (P_AproxDistance(P_AproxDistance(player->mo->x - mo->x, player->mo->y - mo->y), player->mo->z - mo->z) > dist)
			continue;

		if (mo->flags & MF_BOSS || mo->type == MT_PLAYER) // Don't instantly kill bosses or players!
			P_DamageMobj(mo, player->mo, player->mo, 1);
		else
			P_KillMobj(mo, player->mo, player->mo);
	}
}

//
// P_LookForEnemies
// Looks for something you can hit - Used for homing attack
// Includes monitors and springs!
//
boolean P_LookForEnemies(player_t *player)
{
	mobj_t *mo;
	thinker_t *think;
	mobj_t *closestmo = NULL;
	angle_t an;

	for (think = thinkercap.next; think != &thinkercap; think = think->next)
	{
		if (think->function.acp1 != (actionf_p1)P_MobjThinker)
			continue; // not a mobj thinker

		mo = (mobj_t *)think;
		if (!(mo->flags & MF_ENEMY || mo->flags & MF_BOSS || mo->flags & MF_MONITOR
			|| mo->flags & MF_SPRING))
		{
			continue; // not a valid enemy
		}

		if (mo->health <= 0) // dead
			continue;

		if (mo == player->mo)
			continue;

		if (mo->flags2 & MF2_FRET)
			continue;

		if (mo->type == MT_DETON) // Don't be STUPID, Sonic!
			continue;

		if (mo->flags & MF_MONITOR && mo->state == &states[S_MONITOREXPLOSION5])
			continue;

		if (mo->z > player->mo->z+MAXSTEPMOVE)
			continue; // Don't home upwards!

		if (P_AproxDistance(P_AproxDistance(player->mo->x-mo->x, player->mo->y-mo->y),
			player->mo->z-mo->z) > 512*FRACUNIT)
			continue; // out of range

		if (mo->type == MT_PLAYER) // Don't chase after other players!
			continue;

		if (closestmo && P_AproxDistance(P_AproxDistance(player->mo->x-mo->x, player->mo->y-mo->y),
			player->mo->z-mo->z) > P_AproxDistance(P_AproxDistance(player->mo->x-closestmo->x,
			player->mo->y-closestmo->y), player->mo->z-closestmo->z))
			continue;

		an = R_PointToAngle2(player->mo->x, player->mo->y, mo->x, mo->y) - player->mo->angle;

		if (an > ANG90 && an < ANG270)
			continue; // behind back

		player->mo->angle = R_PointToAngle2(player->mo->x, player->mo->y, mo->x, mo->y);

		if (!P_CheckSight(player->mo, mo))
			continue; // out of sight

		closestmo = mo;
	}

	if (closestmo)
	{
		// Found a target enemy
		P_SetTarget(&player->mo->target, P_SetTarget(&player->mo->tracer, closestmo));
		return true;
	}

	return false;
}

void P_HomingAttack(mobj_t *source, mobj_t *enemy) // Home in on your target
{
	fixed_t dist;

	if (!enemy)
		return;

	if (!(enemy->health))
		return;

	// change angle
	source->angle = R_PointToAngle2(source->x, source->y, enemy->x, enemy->y);
	if (source->player)
	{
		if (source->player == &players[consoleplayer])
			localangle = source->angle;
		else if (splitscreen && source->player == &players[secondarydisplayplayer])
			localangle2 = source->angle;
	}

	// change slope
	dist = P_AproxDistance(P_AproxDistance(enemy->x - source->x, enemy->y - source->y),
		enemy->z - source->z);

	if (dist < 1)
		dist = 1;

	if (source->type == MT_DETON && enemy->player) // For Deton Chase
	{
		fixed_t ns = FixedDiv(enemy->player->normalspeed*FRACUNIT, FixedDiv(20*FRACUNIT,17*FRACUNIT));
		source->momx = FixedMul(FixedDiv(enemy->x - source->x, dist), ns);
		source->momy = FixedMul(FixedDiv(enemy->y - source->y, dist), ns);
		source->momz = FixedMul(FixedDiv(enemy->z - source->z, dist), ns);
	}
	else if (source->type != MT_PLAYER)
	{
		if (source->threshold == 32000)
		{
			fixed_t ns = source->info->speed/2;
			source->momx = FixedMul(FixedDiv(enemy->x - source->x, dist), ns);
			source->momy = FixedMul(FixedDiv(enemy->y - source->y, dist), ns);
			source->momz = FixedMul(FixedDiv(enemy->z - source->z, dist), ns);
		}
		else
		{
			source->momx = FixedMul(FixedDiv(enemy->x - source->x, dist), source->info->speed);
			source->momy = FixedMul(FixedDiv(enemy->y - source->y, dist), source->info->speed);
			source->momz = FixedMul(FixedDiv(enemy->z - source->z, dist), source->info->speed);
		}
	}
	else if (source->player)
	{
		const fixed_t ns = source->player->actionspd * FRACUNIT;
		source->momx = FixedMul(FixedDiv(enemy->x - source->x, dist), FixedDiv(ns,3*FRACUNIT/2));
		source->momy = FixedMul(FixedDiv(enemy->y - source->y, dist), FixedDiv(ns,3*FRACUNIT/2));
		source->momz = FixedMul(FixedDiv(enemy->z - source->z, dist), FixedDiv(ns,3*FRACUNIT/2));
	}
}

// Search for emeralds, note, this is only done on level load
void P_FindEmerald(void)
{
	thinker_t *th;
	mobj_t *mo2;

	hunt1 = hunt2 = hunt3 = NULL;

	// scan the remaining thinkers
	// to find all emeralds
	for (th = thinkercap.next; th != &thinkercap; th = th->next)
	{
		if (th->function.acp1 != (actionf_p1)P_MobjThinker)
			continue;

		mo2 = (mobj_t *)th;

		// The way emerald hunts work now is that 3 of the same object type is
		// placed on a map, the game picks the 3 emeralds you must find out of
		// all the emeralds on the map by random, so make sure that the game can not pick the same emerald twice
		if (mo2->type == MT_EMERHUNT) // SRB2CBTODO: Erroneous?
		{
			if (hunt1 == NULL)
				hunt1 = mo2;
			else if (hunt2 == NULL)
			{
				if (hunt1 != NULL && mo2 == hunt1)
					continue;

				hunt2 = mo2;
			}
			else if (hunt3 == NULL)
			{
				if ((hunt1 != NULL && mo2 == hunt1) || (hunt2 != NULL && mo2 == hunt2))
					continue;

				hunt3 = mo2;
			}
		}
	}
	return;
}

//
// P_DeathThink
// Fall on your face when dying.
// Decrease POV height to floor height.
//
static void P_DeathThink(player_t *player)
{
	ticcmd_t *cmd;

	cmd = &player->cmd;

	// fall to the ground
	if (player->viewheight > 6*FRACUNIT)
		player->viewheight -= FRACUNIT;

	if (player->viewheight < 6*FRACUNIT)
		player->viewheight = 6*FRACUNIT;

	player->deltaviewheight = 0;
	onground = P_IsObjectOnGround(player->mo);

	P_CalcHeight(player);

	if (!player->deadtimer)
		player->deadtimer = 60*TICRATE;

	player->deadtimer--;

#ifdef JTEBOTS // Bots should imediately respawn here so it doesn't need to be taken care of in their AI
	if (player->bot)
	{
		player->playerstate = PST_REBORN;
		return;
	}
#endif

	player->pflags &= ~PF_SLIDING;

	if (!(multiplayer || netgame) && (cmd->buttons & BT_USE || cmd->buttons & BT_JUMP)
		&& (player->lives <= 0) && (player->deadtimer > gameovertics+2) && (player->continues > 0))
		player->deadtimer = gameovertics+2;

	// Respawn as spectator?
	if ((splitscreen || netgame) && (cmd->buttons & BT_TOSSFLAG))
	{
		if ((gametype == GT_MATCH && !cv_matchtype.value) || gametype == GT_TAG)
		{
			player->spectator = true;
			player->score = 0;
			player->playerstate = PST_REBORN;
			CONS_Printf("%s became a spectator.\n", player_names[player-players]);

			if (gametype == GT_TAG)
			{
				if (player->pflags & PF_TAGIT)
					player->pflags &= ~PF_TAGIT;

				P_CheckSurvivors(); //see if you still have a game.
			}
		}
	}

	if ((cmd->buttons & BT_JUMP) && (gametype == GT_MATCH
#ifdef CHAOSISNOTDEADYET
		|| gametype == GT_CHAOS
#endif
		|| gametype == GT_TAG || gametype == GT_CTF))
	{
		player->playerstate = PST_REBORN;
	}
	else if (player->deadtimer < 30*TICRATE && (gametype != GT_COOP && gametype != GT_RACE))
	{
		player->playerstate = PST_REBORN;
	}
	else if (player->lives > 0 && !G_IsSpecialStage(gamemap)) // Don't allow "click to respawn" in special stages!
	{
		// Respawn with jump button
		if ((cmd->buttons & BT_JUMP) && player->deadtimer < 59*TICRATE && gametype != GT_RACE)
			player->playerstate = PST_REBORN;

		if ((cmd->buttons & BT_JUMP) && gametype == GT_RACE)
			player->playerstate = PST_REBORN;

		if (player->deadtimer < 56*TICRATE && gametype == GT_COOP)
			player->playerstate = PST_REBORN;

		if (player->mo->z < P_PlayerZAtF(player)
			- 10000*FRACUNIT)
		{
			player->playerstate = PST_REBORN;
		}
	}
	else if ((netgame || multiplayer) && player->deadtimer == 48*TICRATE)
	{
		// In a net/multiplayer game, and out of lives
		if (gametype == GT_RACE)
		{
			int i;

			for (i = 0; i < MAXPLAYERS; i++)
				if (playeringame[i] && !players[i].exiting && players[i].lives > 0)
					break;

			if (i == MAXPLAYERS)
			{
				// Everyone's either done with the race, or dead.
				if (!countdown2)
				{
					// Everyone just.. died. XD
					nextmapoverride = racestage_start;
					countdown2 = 1*TICRATE;
					skipstats = true;
				}
				else if (countdown2 > 1*TICRATE)
					countdown2 = 1*TICRATE;
			}
		}

		// In a coop game, and out of lives
		if (gametype == GT_COOP)
		{
			int i;

			for (i = 0; i < MAXPLAYERS; i++)
				if (playeringame[i] && (players[i].exiting || players[i].lives > 0))
					break;

			if (i == MAXPLAYERS)
			{
				// They're dead, Jim.
				nextmapoverride = spstage_start;
				countdown2 = 1*TICRATE;
				skipstats = true;

				for (i = 0; i < MAXPLAYERS; i++)
				{
					if (playeringame[i])
						players[i].score = 0;
				}

				emeralds = 0;
				tokenbits = 0;
				tokenlist = 0;
				token = 0;
			}
		}
	}

	// Stop music when respawning in single player
	if (cv_resetmusic.value && player->playerstate == PST_REBORN)
	{
		if (!(netgame || multiplayer)
#ifdef JTEBOTS
			&& !player->bot
#endif
			)
			S_StopMusic();
		else
			S_SpeedMusic(1.0f);
	}

	if (player->mo->momz < -30*FRACUNIT)
		player->mo->momz = -30*FRACUNIT;

	if (player->mo->z + player->mo->momz < P_PlayerZAtF(player) - 5120*FRACUNIT)
	{
		player->mo->momz = 0;
		player->mo->z = P_PlayerZAtF(player) - 5120*FRACUNIT;
	}

	if (gametype == GT_RACE || (gametype == GT_COOP && (multiplayer || netgame)))
	{
		// Keep time rolling in race mode
		if (!(countdown2 && !countdown) && !player->exiting && !(player->pflags & PF_TIMEOVER))
		{
			if (gametype == GT_RACE)
			{
				if (leveltime >= 4*TICRATE)
					player->realtime = leveltime - 4*TICRATE;
				else
					player->realtime = 0;
			}
			else
				player->realtime = leveltime;
		}

		// Return to level music
		if (netgame && player->deadtimer == gameovertics && P_IsLocalPlayer(player))
			S_ChangeMusic(mapmusic & 2047, true);
	}
}

//
// P_MoveCamera: make sure the camera is not outside the world and looks at the player avatar
//
camera_t camera, camera2; // Two cameras.. one for splitscreen!

static void CV_CamRotate_OnChange(void)
{
	if (cv_cam_rotate.value > 359)
		CV_SetValue(&cv_cam_rotate, 0);
}

static void CV_CamRotate2_OnChange(void)
{
	if (cv_cam2_rotate.value > 359)
		CV_SetValue(&cv_cam2_rotate, 0);
}

static CV_PossibleValue_t rotation_cons_t[] = {{1, "MIN"}, {45, "MAX"}, {0, NULL}};

consvar_t cv_cam_dist = {"cam_dist", "140", CV_FLOAT, NULL, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_cam_height = {"cam_height", "20", CV_FLOAT, NULL, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_cam_still = {"cam_still", "Off", 0, CV_OnOff, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_cam_speed = {"cam_speed", "0.25", CV_FLOAT, NULL, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_cam_rotate = {"cam_rotate", "0", CV_CALL|CV_NOINIT, CV_Unsigned, CV_CamRotate_OnChange, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_cam_rotspeed = {"cam_rotspeed", "10", 0, rotation_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_cam2_dist = {"cam2_dist", "140", CV_FLOAT, NULL, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_cam2_height = {"cam2_height", "20", CV_FLOAT, NULL, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_cam2_still = {"cam2_still", "Off", 0, CV_OnOff, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_cam2_speed = {"cam2_speed", "0.25", CV_FLOAT, NULL, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_cam2_rotate = {"cam2_rotate", "0", CV_CALL|CV_NOINIT, CV_Unsigned, CV_CamRotate2_OnChange, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_cam2_rotspeed = {"cam2_rotspeed", "10", 0, rotation_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};

fixed_t t_cam_dist = -42;
fixed_t t_cam_height = -42;
fixed_t t_cam_rotate = -42;
fixed_t t_cam2_dist = -42;
fixed_t t_cam2_height = -42;
fixed_t t_cam2_rotate = -42;

#define MAXCAMERADIST 140*FRACUNIT // Max distance the camera can be in front of the player (2D mode)

void P_ResetCamera(player_t *player, camera_t *thiscam)
{
	fixed_t x, y, z;

	if (!player->mo)
		return;

	if (player->mo->health <= 0)
		return;

	thiscam->chase = true;
	x = player->mo->x;
	y = player->mo->y;
	z = player->mo->z + (cv_viewheight.value<<FRACBITS);

	// set bits for the camera
	thiscam->x = x;
	thiscam->y = y;
	thiscam->z = z;

	if (twodlevel || (player->mo->flags2 & MF2_TWOD)) // Set the proper angle for 2D mode
	{
		if (player->twodcamangle && player->twodcamangle != ANG90)
			thiscam->angle = player->twodcamangle;
		else
			thiscam->angle = ANG90;
	}
    else
		thiscam->angle = player->mo->angle;

	thiscam->aiming = 0;
	thiscam->rollangle = 0;

	thiscam->subsector = R_PointInSubsector(thiscam->x,thiscam->y);

#ifdef THINGSCALING
	//Can't just use P_SetScale here, the camera has no info.
	thiscam->scale  = player->mo->scale;
	// The radius and height of the camera should stay constant,
	// visual errors happen otherwise
#else
	thiscam->radius = 20*FRACUNIT;
	thiscam->height = 16*FRACUNIT;
#endif
}

void P_MoveChaseCamera(player_t *player, camera_t *thiscam, boolean netcalled)
{
	angle_t angle = 0, focusangle = 0;
	fixed_t x, y, z, dist, checkdist, viewpointx, viewpointy, camspeed, camdist, camheight, pviewheight;
	int camrotate;
	boolean camstill;
	mobj_t *mo;
	subsector_t *newsubsec;
	float f1, f2;

	if (!cv_chasecam.value && thiscam == &camera)
		return;

	if (!cv_chasecam2.value && thiscam == &camera2)
		return;

	if (!thiscam->chase)
		P_ResetCamera(player, thiscam);

	if (!player)
		return;

	mo = player->mo;

#ifdef THINGSCALING
	// Can't just use P_SetScale here, the camera has no info.
	thiscam->scale  = mo->scale;
#endif

	thiscam->radius = FIXEDSCALE(20*FRACUNIT, mo->scale);
	thiscam->height = FIXEDSCALE(16*FRACUNIT, mo->scale);

	if (!mo)
		return;

	if (leveltime > 0 && timeinmap <= 0) // Don't run while respawning from a starpost
		return;

	if (netcalled && !demoplayback && displayplayer == consoleplayer)
	{
		if (player == &players[consoleplayer])
			focusangle = localangle;
		else if (player == &players[secondarydisplayplayer])
			focusangle = localangle2;
	}
	else
		focusangle = player->mo->angle;

	P_CameraThinker(thiscam);

	if (thiscam == &camera)
	{
		camspeed = cv_cam_speed.value/NEWTICRATERATIO;
		camstill = cv_cam_still.value;
		camrotate = cv_cam_rotate.value;

		if (player->pflags & PF_NIGHTSMODE)
			camdist = cv_cam_dist.value;
		else
			camdist = FIXEDSCALE(cv_cam_dist.value, mo->scale);

		camheight = FIXEDSCALE(cv_cam_height.value, mo->scale);
	}
	else // Camera 2
	{
		camspeed = cv_cam2_speed.value;
		camstill = cv_cam2_still.value;
		camrotate = cv_cam2_rotate.value;

		if (player->pflags & PF_NIGHTSMODE)
			camdist = cv_cam2_dist.value;
		else
			camdist = FIXEDSCALE(cv_cam2_dist.value, mo->scale);

		camheight = FIXEDSCALE(cv_cam2_height.value, mo->scale);
	}

	if (twodlevel || (mo->flags2 & MF2_TWOD))
		angle = player->twodcamangle; // From SRB2CB
	else if (player->mo->flags2 & MF2_TWOSEVEN)
		angle = ANG270;
	else if (camstill)
		angle = thiscam->angle;
	else if (player->pflags & PF_NIGHTSMODE) // NiGHTS Level
	{
		if ((player->pflags & PF_TRANSFERTOCLOSEST) && player->axis1 && player->axis2)
		{
			angle = R_PointToAngle2(player->axis1->x, player->axis1->y, player->axis2->x, player->axis2->y);
			angle += ANG90;
		}
		else if (player->mo->target)
		{
			if (player->mo->target->flags & MF_AMBUSH)
				angle = R_PointToAngle2(player->mo->target->x, player->mo->target->y, player->mo->x, player->mo->y);
			else
				angle = R_PointToAngle2(player->mo->x, player->mo->y, player->mo->target->x, player->mo->target->y);
		}
	}
	else if (((player == &players[consoleplayer] && cv_analog.value)
			  || (splitscreen && player == &players[secondarydisplayplayer] && cv_analog2.value))) // Analog
	{
		angle = R_PointToAngle2(thiscam->x, thiscam->y, mo->x, mo->y);

		// If in SA mode, keep focused on the boss
		// ...except we do it better. ;) Only focus boss on XY.
		// For Z, keep looking at player.
		if ((maptol & TOL_SP))
		{
			thinker_t *th;
			mobj_t *mo2 = NULL;

			// scan the remaining thinkers
			// to find a boss
			for (th = thinkercap.next; th != &thinkercap; th = th->next)
			{
				if (th->function.acp1 != (actionf_p1)P_MobjThinker)
					continue;

				mo2 = (mobj_t *)th;

				if ((mo2->flags & MF_BOSS) && mo2->health > 0 && mo2->target)
				{
					angle = R_PointToAngle2(thiscam->x, thiscam->y, mo2->x, mo2->y);
					break;
				}
			}
		}
	}
	else
		angle = focusangle + FixedAngle(camrotate*FRACUNIT);

	if (cv_analog.value && ((thiscam == &camera && t_cam_rotate != -42) || (thiscam == &camera2
		&& t_cam2_rotate != -42)))
	{
		angle = FixedAngle(camrotate*FRACUNIT);
		thiscam->angle = angle;
	}

	if (!cv_objectplace.value && !((twodlevel || (mo->flags2 & MF2_TWOD)) && player->playerstate == PST_DEAD)) // SRB2CBTODO: Yes you can do it in 2d
	{
		if (player->cmd.buttons & BT_CAMLEFT)
		{
			if (thiscam == &camera)
			{
				if (cv_analog.value)
					angle -= FixedAngle(cv_cam_rotspeed.value*FRACUNIT);
				else
					CV_SetValue(&cv_cam_rotate, camrotate == 0 ? 358
						: camrotate - 2);
			}
			else
			{
				if (cv_analog2.value)
					angle -= FixedAngle(cv_cam2_rotspeed.value*FRACUNIT);
				else
					CV_SetValue(&cv_cam2_rotate, camrotate == 0 ? 358
						: camrotate - 2);
			}
		}
		else if (player->cmd.buttons & BT_CAMRIGHT)
		{
			if (thiscam == &camera)
			{
				if (cv_analog.value)
					angle += FixedAngle(cv_cam_rotspeed.value*FRACUNIT);
				else
					CV_SetValue(&cv_cam_rotate, camrotate + 2);
			}
			else
			{
				if (cv_analog2.value)
					angle += FixedAngle(cv_cam2_rotspeed.value*FRACUNIT);
				else
					CV_SetValue(&cv_cam2_rotate, camrotate + 2);
			}
		}
	}

	// sets ideal cam pos
	// SRB2CBTODO: option to scale the camera? + twodcam dist support
	if (twodlevel || (mo->flags2 & MF2_TWOD))
	{
			dist = (FIXEDSCALE(450 + player->twodcamdist, mo->scale)) << FRACBITS; // Dynamic camera distance

		if (player->exiting)
			dist = (FIXEDSCALE(500, mo->scale)) << FRACBITS; // zoom out a bit....cause it looks cool!
	}
	else
	{
		dist = camdist;

		if (player->climbing || (mo->tracer && mo->tracer->type == MT_EGGTRAP)
			|| (player->pflags & PF_MACESPIN) || (player->pflags & PF_ITEMHANG) || (player->pflags & PF_ROPEHANG))
			dist <<= 1;
	}

#ifdef THINGSCALING
	checkdist = FIXEDSCALE(dist, thiscam->scale);
#else
	checkdist = dist;
#endif

	if (checkdist < 128*FRACUNIT)
		checkdist = 128*FRACUNIT;

	x = mo->x - FixedMul(FINECOSINE((angle>>ANGLETOFINESHIFT) & FINEMASK), dist);
	y = mo->y - FixedMul(FINESINE((angle>>ANGLETOFINESHIFT) & FINEMASK), dist);

	pviewheight = FIXEDSCALE(cv_viewheight.value<<FRACBITS, mo->scale);

	if (mo->eflags & MFE_VERTICALFLIP)
	{
#ifdef SRB2CBTODO
		z = mo->z + FixedDiv(FixedMul(mo->info->height,3),4) -
		((mo->height != mo->info->height) ? mo->info->height - mo->height : 0) - pviewheight -
		(camheight);
#else
		z = mo->z + mo->height - pviewheight -
			(camheight);
#endif
	}
	else
		z = mo->z + pviewheight +
			(camheight);

#ifdef ESLOPE
	pslope_t *playerslope = mo->subsector->sector->f_slope;

	if (playerslope && !(playerslope->sourceline->flags & ML_EFFECT1)) // VPHYSICS: Use real vectors to find diff in camera height
	{
		if (mo->player)
		{
			// Let's make a vector for the mobj!
			v3float_t point1;
			v3float_t point2;

			point1.x = FIXED_TO_FLOAT(thiscam->x);
			point1.y = FIXED_TO_FLOAT(thiscam->y);
			point1.z = FIXED_TO_FLOAT(thiscam->z);

			fixed_t mangle = thiscam->angle>>ANGLETOFINESHIFT;

			pslope_t* cslope = playerslope;

			fixed_t addx, addy;
			addx = FixedMul(200*FRACUNIT, FINECOSINE(mangle));
			addy = FixedMul(200*FRACUNIT, FINESINE(mangle));

			// Make a vector that's level to the ground (NOT THE SLOPE),
			// that way we can get the mobj's pitchangle based on
			// the angle between the player's vector and the slope's normal
			point2.x = FIXED_TO_FLOAT(thiscam->x+addx);
			point2.y = FIXED_TO_FLOAT(thiscam->y+addy);
			point2.z = FIXED_TO_FLOAT(thiscam->z);//P_GetZAtf(cslope, point2.x, point2.y); // TODO: Use this for cool effects like going off a slope

			v3float_t camvec;
			camvec = *M_MakeVec3f(&point1, &point2, &camvec);

			fixed_t pangle = FV_AngleBetweenVectorsf(&cslope->normalf, &camvec)* 180 / M_PI;
			pangle -= 90; // Adjust pitch angle to correct orientation

			if (abs(pangle) > 60)
				z += (pangle*4*FRACUNIT)*(mo->scale / 100.0f); // REALLY steep slope
			else if (abs(pangle) > 50)
				z += (pangle*3.5*FRACUNIT)*(mo->scale / 100.0f); // steep slope
			else if (abs(pangle) > 40)
				z += (pangle*3*FRACUNIT)*(mo->scale / 100.0f); // kina slope
			else
				z += (pangle*2.3*FRACUNIT)*(mo->scale / 100.0f);

			//z += pangle*(pangle/45)*FRACUNIT; // SRB2CBTODO: VPHYSICS This will handle a loop-like camera

			// aiming (pitch) is already handled since the game is setup to automatically point to the player
		}
	}
#endif

	// move camera down to move under lower ceilings
	newsubsec = R_IsPointInSubsector(((mo->x>>FRACBITS) + (thiscam->x>>FRACBITS))<<(FRACBITS-1),
									 ((mo->y>>FRACBITS) + (thiscam->y>>FRACBITS))<<(FRACBITS-1));

	if (!newsubsec)
		newsubsec = thiscam->subsector;

	if (newsubsec)
	{
		fixed_t myfloorz, myceilingz;
		fixed_t midz = thiscam->z + (thiscam->z - mo->z)/2;

		// Cameras use the heightsec's heights rather then the actual sector heights.
		// If you can see through it, why not move the camera through it too?
		if (newsubsec->sector->heightsec >= 0)
		{
			myfloorz = sectors[newsubsec->sector->heightsec].floorheight;
			myceilingz = sectors[newsubsec->sector->heightsec].ceilingheight;
		}
		else
		{
			myfloorz = newsubsec->sector->floorheight;
			myceilingz = newsubsec->sector->ceilingheight;
		}

		// Check list of fake floors and see if floorz/ceilingz need to be altered.
		if (newsubsec->sector->ffloors)
		{
			ffloor_t *rover;
			fixed_t delta1, delta2;
			fixed_t thingtop = midz + thiscam->height;

			for (rover = newsubsec->sector->ffloors; rover; rover = rover->next)
			{
				if (!(rover->flags & FF_BLOCKOTHERS) || !(rover->flags & FF_EXISTS) || !(rover->flags & FF_RENDERALL))
					continue;

				fixed_t topheight = *rover->topheight;
				fixed_t bottomheight = *rover->bottomheight;

#ifdef ESLOPE
				if (rover->t_slope)
					topheight = P_GetZAt(rover->t_slope, player->mo->x, player->mo->y);

				if (rover->b_slope)
					bottomheight = P_GetZAt(rover->b_slope, player->mo->x, player->mo->y);
#endif

				delta1 = midz - (bottomheight
					+ ((topheight - bottomheight)/2));
				delta2 = thingtop - (bottomheight
					+ ((topheight - bottomheight)/2));
				if (topheight > tmfloorz && abs(delta1) < abs(delta2))
				{
					myfloorz = topheight;
				}
				if (bottomheight < tmceilingz && abs(delta1) >= abs(delta2))
				{
					myceilingz = bottomheight;
				}
			}
		}

#ifdef POLYOBJECTS // Collision (for camera)
	// Check polyobjects and see if tmfloorz/tmceilingz need to be altered
	{
		int xl, xh, yl, yh, bx, by;
		validcount++;

		xl = (unsigned int)(tmbbox[BOXLEFT] - bmaporgx)>>MAPBLOCKSHIFT;
		xh = (unsigned int)(tmbbox[BOXRIGHT] - bmaporgx)>>MAPBLOCKSHIFT;
		yl = (unsigned int)(tmbbox[BOXBOTTOM] - bmaporgy)>>MAPBLOCKSHIFT;
		yh = (unsigned int)(tmbbox[BOXTOP] - bmaporgy)>>MAPBLOCKSHIFT;

		for (by = yl; by <= yh; by++)
		{
			for (bx = xl; bx <= xh; bx++)
			{
				long offset;
				polymaplink_t *plink; // haleyjd 02/22/06

				if (bx < 0 || by < 0 || bx >= bmapwidth || by >= bmapheight)
					continue;

				offset = by*bmapwidth + bx;

				// haleyjd 02/22/06: consider polyobject lines
				plink = polyblocklinks[offset];

				while (plink)
				{
					polyobj_t *po = plink->po;

					if (po->validcount != validcount) // if polyobj hasn't been checked
					{
						sector_t *polysec;
						fixed_t delta1, delta2, thingtop;
						fixed_t polytop, polybottom;

						po->validcount = validcount;

						if (!P_PointInsidePolyobj(po, x, y))
						{
							plink = (polymaplink_t *)(plink->link.next);
							continue;
						}

						// We're inside it! Yess...
						polysec = po->lines[0]->backsector;

						// Make the polyobject like an FOF!
						polytop = polysec->ceilingheight;
						polybottom = polysec->floorheight;

						thingtop = midz + thiscam->height;
						delta1 = midz - (polybottom + ((polytop - polybottom)/2));
						delta2 = thingtop - (polybottom + ((polytop - polybottom)/2));

						if (polytop > tmfloorz && abs(delta1) < abs(delta2))
							myfloorz = polytop;

						if (polybottom < tmceilingz && abs(delta1) >= abs(delta2))
							myceilingz = polybottom;
					}
					plink = (polymaplink_t *)(plink->link.next);
				}
			}
		}

	}
#endif

		// camera fit?
		if (myceilingz != myfloorz
			&& myceilingz - thiscam->height < z)
		{
			// no fit
			z = myceilingz - thiscam->height-11*FRACUNIT;
			// is the camera fit is there own sector
		}

		// Make the camera a tad smarter with 3d floors
		if (newsubsec->sector->ffloors)
		{
			ffloor_t *rover;

			for (rover = newsubsec->sector->ffloors; rover; rover = rover->next)
			{
				if ((rover->flags & FF_BLOCKOTHERS) && (rover->flags & FF_RENDERALL) && (rover->flags & FF_EXISTS))
				{
					fixed_t topheight = *rover->topheight;
					fixed_t bottomheight = *rover->bottomheight;

#ifdef ESLOPE
					if (rover->t_slope)
						topheight = P_GetZAt(rover->t_slope, thiscam->x, thiscam->y);

					if (rover->b_slope)
						bottomheight = P_GetZAt(rover->b_slope, thiscam->x, thiscam->y);
#endif

					if (bottomheight - thiscam->height < z
						&& midz < bottomheight)
						z = bottomheight - thiscam->height-11*FRACUNIT;

					else if (topheight + thiscam->height > z
						&& midz > topheight)
						z = topheight;

					if ((mo->z >= topheight && midz < bottomheight)
						|| ((mo->z < bottomheight && mo->z+mo->height < topheight) && midz >= topheight))
					{
						// Can't see
						P_ResetCamera(player, thiscam);
					}
				}
			}
		}
	}

	if (thiscam->z < thiscam->floorz)
		thiscam->z = thiscam->floorz;

	if (thiscam->z > thiscam->ceilingz)
		thiscam->z = thiscam->ceilingz;

	// point viewed by the camera
	// this point is just 64 unit forward the player
	dist = FIXEDSCALE(64 << FRACBITS, mo->scale);
	viewpointx = mo->x + FixedMul(FINECOSINE((angle>>ANGLETOFINESHIFT) & FINEMASK), dist);
	viewpointy = mo->y + FixedMul(FINESINE((angle>>ANGLETOFINESHIFT) & FINEMASK), dist);

	if (!camstill)
		thiscam->angle = R_PointToAngle2(thiscam->x, thiscam->y, viewpointx, viewpointy);




	////////////////// 3D camera reseting ///////////

	// follow the player
	if (!player->playerstate == PST_DEAD && (thiscam == &camera ? cv_cam_speed.value
											 : cv_cam2_speed.value) != 0 && (abs(thiscam->x - mo->x) > (thiscam == &camera ? cv_cam_dist.value : cv_cam2_dist.value)*4
																			 || abs(thiscam->y - mo->y) > (thiscam == &camera ? cv_cam_dist.value : cv_cam2_dist.value)*4 || abs(thiscam->z - mo->z) >
																			 (thiscam == &camera ? cv_cam_dist.value : cv_cam2_dist.value)*4))
	{
        // SRB2CB: better cam reset support for 2D handled below
        if (!twodlevel && !(player->mo->flags2 & MF2_TWOD)) // Make sure that we can smoothly transistion to different camrea distances in 2D
			P_ResetCamera(player, thiscam);
	}

	//////////// 2D camera reset support ////////////

	int twodcamdist = (players[consoleplayer].twodcamdist+10)*30;
	if (twodcamdist < 1)
		twodcamdist = 1;

	if(players[consoleplayer].twodcamdist > 800)
		twodcamdist = (players[consoleplayer].twodcamdist+10)*30;
	else
		twodcamdist = (players[consoleplayer].twodcamdist+10)*15;


	int twodcamdist2 = (players[secondarydisplayplayer].twodcamdist+10)*30;
	if (twodcamdist2 < 1)
		twodcamdist2 = 1;

	if(players[secondarydisplayplayer].twodcamdist > 800)
		twodcamdist2 = (players[secondarydisplayplayer].twodcamdist+10)*30;
	else
		twodcamdist2 = (players[secondarydisplayplayer].twodcamdist+10)*15;

	// Normal distance the camera should be
	float normdist = (400<<FRACBITS)+players[consoleplayer].twodcamdist;
	float normdist2 = (400<<FRACBITS)+players[secondarydisplayplayer].twodcamdist;

	if (!player->playerstate == PST_DEAD && (thiscam == &camera ? cv_cam_speed.value
											 : cv_cam2_speed.value) != 0 && (
																			 // Reset the camera if it goes far off the left or right of the 2D screen
																			 abs(thiscam->x - mo->x) > (thiscam == &camera ? normdist - twodcamdist : normdist2 - twodcamdist2)*1.5
																			 // Checking if the player goes into the foreground isn't needed, probably
																			 // Reset it if it goes too high
																			 || abs(thiscam->z - mo->z) > (thiscam == &camera ? normdist - twodcamdist : normdist2 - twodcamdist2)*1.5)
        )
	{
        // SRB2CBTODO: add better cam reset support for 2D
        // Make sure that we can smoothly transistion to different camrea distances in 2D
        if ((!cv_cam_dist.value < 26 && !players[consoleplayer].twodcamdist) && (!cv_cam2_dist.value < 26 && !players[secondarydisplayplayer].twodcamdist)) // This is for twodcamdist stuffs

			P_ResetCamera(player, thiscam);
	}




	if (twodlevel || (mo->flags2 & MF2_TWOD))
	{
		thiscam->momx = x-thiscam->x;
		thiscam->momy = y-thiscam->y;
		thiscam->momz = z-thiscam->z;
	}
	else
	{
		thiscam->momx = FixedMul(x - thiscam->x, camspeed);
		thiscam->momy = FixedMul(y - thiscam->y, camspeed);

		if (GETSECSPECIAL(thiscam->subsector->sector->special, 1) == 6
			&& thiscam->z < thiscam->subsector->sector->floorheight + 256*FRACUNIT
			&& FixedMul(z - thiscam->z, camspeed) < 0)
		{
			thiscam->momz = 0; // Don't go down a death pit
		}
		else
			thiscam->momz = FixedMul(z - thiscam->z, camspeed);
	}

	// compute aiming to look the viewed point
	f1 = FIXED_TO_FLOAT(viewpointx - thiscam->x);
	f2 = FIXED_TO_FLOAT(viewpointy - thiscam->y);
	dist = (fixed_t)((float)sqrt(f1*f1+f2*f2)*FRACUNIT);

#ifdef SRB2CBTODO // SRB2CBTODO: Give the camera a scale and a player for analog in netgames
	if (player->mo->eflags & MFE_VERTICALFLIP)
		angle = R_PointToAngle2(0, thiscam->z, dist,mo->z + (FixedDiv(FixedMul(mo->info->height,3),4) >> 1)
								- ((mo->height != mo->info->height) ? (mo->info->height - mo->height) >> 1 : 0)
								+ (FINESINE((player->aiming>>ANGLETOFINESHIFT) & FINEMASK) * 64) * thiscam->scale / 100);
	else
#endif


#ifndef THINGSCALING
		angle = R_PointToAngle2(0, thiscam->z, dist,mo->z + (P_GetPlayerHeight(player) >> 1)
								+ (FINESINE((player->aiming>>ANGLETOFINESHIFT) & FINEMASK) * 64) * thiscam->scale / 100);
#else
	angle = R_PointToAngle2(0, thiscam->z, dist,mo->z + (P_GetPlayerHeight(player)>>1)
							+ FINESINE((player->aiming>>ANGLETOFINESHIFT) & FINEMASK) * 64);
#endif

	if (twodlevel || (mo->flags2 & MF2_TWOD) || !camstill) // Keep the view still...
	{
		G_ClipAimingPitch((int *)&angle);
		dist = thiscam->aiming - angle;
		thiscam->aiming -= (dist>>3);
	}

	// Make player translucent if camera is too close (only in single player).
	if (!(multiplayer || netgame) && !splitscreen
		&& P_AproxDistance(thiscam->x - player->mo->x, thiscam->y - player->mo->y) < FIXEDSCALE(48*FRACUNIT, mo->scale))
	{
		player->mo->flags2 |= MF2_TRANSLUCENT;
	}
	else
		player->mo->flags2 &= ~MF2_TRANSLUCENT;

	// SRB2CB: Cool style death, the camera stays stationed while you die in 2D
	// the camera looks cooler when you die in 3D
	if (player->playerstate == PST_DEAD || player->playerstate == PST_REBORN)
	{
		thiscam->momz = 0;
		if (player->mo && ((player->mo->flags2 & MF2_TWOD) || twodlevel))
			thiscam->aiming = 0;
		else if (player->mo && ((!(player->mo->eflags & MFE_VERTICALFLIP) && player->mo->momz <= 1)
				|| ((player->mo->eflags & MFE_VERTICALFLIP) && player->mo->momz >= -1))) //&& (thiscam->aiming>>ANGLETOFINESHIFT) > 2048)
		{
			thiscam->aiming = 0;
		}
	}
}



// JB - JTE Bots
// By Jason the Echidna
// These are the main sets of a computer player's AI

#ifdef JTEBOTS

void A_BubbleSpawn(mobj_t *actor);
void A_BubbleCheck(mobj_t *actor);


// Bot is too far away, so respawn next to the player,
// the bot must be alive to do this to prevent crashes
void JB_CoopSpawnBot(int botnum)
{
	player_t* player;
	bot_t* bot;
	mobj_t* mo;
	int ownernum, i = 256;
	fixed_t botx, boty, botz;

	player = &players[botnum];
	bot = player->bot;
	ownernum = bot->ownernum;
	mo = players[ownernum].mo;

	if (!playeringame[ownernum] || !mo)
		return; // D: No owner?... No spawn...

	// if your owner isn't alive, or you're dead, do nothing, only do stuff if you're in the map
	if (!players[ownernum].playerstate == PST_LIVE || !player->playerstate == PST_LIVE)
		return;

	int r = 0;
	if (leveltime % 4)
		r = -4;
	else if (leveltime % 3)
		r = -10;
	else if (leveltime % 2)
		r = -8;
	else if (leveltime % 1)
		r = -2;

	// Set the starting position of the bot: Try to be 256 units away and 256 units above, but decrease that until you're in the same sector.
	botx = mo->x+32+r*FRACUNIT; //+ P_ReturnThrustX(mo, mo->angle, -i*FRACUNIT);
	boty = mo->y+32+r*FRACUNIT; //+ P_ReturnThrustY(mo, mo->angle, -i*FRACUNIT);

	while (i > 0)
	{
		if (R_PointInSubsector(botx, boty)->sector != R_PointInSubsector(players[ownernum].mo->x, players[ownernum].mo->y)->sector
		   || R_PointInSubsector(botx + mobjinfo[MT_PLAYER].radius, boty + mobjinfo[MT_PLAYER].radius)->sector != R_PointInSubsector(botx, boty)->sector
		   || R_PointInSubsector(botx - mobjinfo[MT_PLAYER].radius, boty - mobjinfo[MT_PLAYER].radius)->sector != R_PointInSubsector(botx, boty)->sector)
		{
			//botx = mo->x //+ P_ReturnThrustX(mo, mo->angle, -i*FRACUNIT);
			//boty = mo->y //+ P_ReturnThrustY(mo, mo->angle, -i*FRACUNIT);
			i--;
		}
		else
			break;
	}

	if (!R_IsPointInSubsector(botx, boty))
	{
		botx = mo->x+0*FRACUNIT;
	    boty = mo->y+0*FRACUNIT;
	}

	botz = mo->z + i*FRACUNIT;

	if (botz > R_PointInSubsector(botx, boty)->sector->ceilingheight)
		botz = R_PointInSubsector(botx, boty)->sector->ceilingheight - mobjinfo[MT_PLAYER].height;

	mo = player->mo;

	// Set your position
	P_Teleport(mo, botx, boty, botz, players[ownernum].mo->angle, 0, true, true);

	// If you were super already, restore yourself to your super state.
	if (player->powers[pw_super])
		P_SetPlayerMobjState(player->mo, S_PLAY_SUPERSTAND);
	else // If you had a shield, restore its visual significance.
		P_SpawnShieldOrb(player);

	// If you have no health, start with your owner's health.
	if (player->health <= 2 && players[bot->ownernum].health > 2)
		player->health = players[bot->ownernum].health;
	else
		player->health = 1;

	mo->health = player->health;
	bot->target = players[ownernum].mo;

	mo->angle = R_PointToAngle2(mo->x, mo->y, players[ownernum].mo->x, players[ownernum].mo->y);

	if (mo->z+mo->height > mo->floorz)
	{
		switch(player->charability)
		{
			case 1:
				// Flying characters fly down
				P_SetPlayerMobjState(mo, S_PLAY_ABL1); // Change to the flying animation
				player->powers[pw_tailsfly] = tailsflytics + 1; // Set the fly timer
				break;

			default:
				// Otherwise, just fall out of the sky. :P
				P_SetPlayerMobjState(mo, S_PLAY_FALL1);
				break;
		}
	}
	else
		P_SetPlayerMobjState(mo, S_PLAY_FALL1);
}





// Bot is too far away, so respawn next to the it's flag,
// the bot must be alive to do this to prevent crashes
#if 0 // SRB2CBTODO: JB_CTFSpawnBot
void JB_CTFSpawnBot(int botnum)
{
	player_t* player;
	bot_t* bot;
	mobj_t* mo;
	int ownernum, i = 256;
	fixed_t botx, boty, botz;

	player = &players[botnum];
	bot = player->bot;
	ownernum = bot->ownernum;
	//mo = ;
	mobjtype_t flagcolor;

	if (player->ctfteam == 1)
		flagcolor = MT_REDFLAG;
	else if (player->ctfteam == 2)
		flagcolor = MT_BLUEFLAG;

	// if your owner isn't alive, or you're dead, do nothing, only do stuff if you're in the map
	//if (!players[ownernum].playerstate == PST_LIVE || !player->playerstate == PST_LIVE)
	//	return;

	// Set the starting position of the bot: Try to be 256 units away and 256 units above, but decrease that until you're in the same sector.
	botx = mo->spawnpoint->+32*FRACUNIT; //+ P_ReturnThrustX(mo, mo->angle, -i*FRACUNIT);
	boty = mo->y+32*FRACUNIT; //+ P_ReturnThrustY(mo, mo->angle, -i*FRACUNIT);

	while (i > 0)
	{
		if (R_PointInSubsector(botx, boty)->sector != R_PointInSubsector(players[ownernum].mo->x, players[ownernum].mo->y)->sector
		   || R_PointInSubsector(botx + mobjinfo[MT_PLAYER].radius, boty + mobjinfo[MT_PLAYER].radius)->sector != R_PointInSubsector(botx, boty)->sector
		   || R_PointInSubsector(botx - mobjinfo[MT_PLAYER].radius, boty - mobjinfo[MT_PLAYER].radius)->sector != R_PointInSubsector(botx, boty)->sector)
		{
			//botx = mo->x //+ P_ReturnThrustX(mo, mo->angle, -i*FRACUNIT);
			//boty = mo->y //+ P_ReturnThrustY(mo, mo->angle, -i*FRACUNIT);
			i--;
		}
		else
			break;
	}

	botz = mo->z + i*FRACUNIT;

	if (botz > R_PointInSubsector(botx, boty)->sector->ceilingheight)
		botz = R_PointInSubsector(botx, boty)->sector->ceilingheight - mobjinfo[MT_PLAYER].height;

	mo = player->mo;

	// Set your position
	P_UnsetThingPosition(mo);
	mo->x = botx;
	mo->y = boty;
	mo->z = botz;
	P_SetThingPosition(mo);

	// If you were super already, restore yourself to your super state.
	if (player->powers[pw_super])
		P_SetPlayerMobjState(player->mo, S_PLAY_SUPERSTAND);
	else // If you had a shield, restore its visual significance.
		P_SpawnShieldOrb(player);

	// If you have no health, start with your owner's health.
	if (player->health <= 2 && players[bot->ownernum].health > 2)
		player->health = players[bot->ownernum].health;
	else
		player->health = 1;

	mo->health = player->health;
	bot->target = players[ownernum].mo;

	mo->angle = R_PointToAngle2(mo->x, mo->y, players[ownernum].mo->x, players[ownernum].mo->y);

	if (mo->z > mo->floorz+mo->height)
	{
		switch(player->charability)
		{
			case 1:
				// Tails flys his way down
				P_SetPlayerMobjState(mo, S_PLAY_ABL1); // Change to the flying animation
				player->powers[pw_tailsfly] = tailsflytics + 1; // Set the fly timer
				break;

			default:
				// Otherwise, just fall out of the sky. :P
				P_SetPlayerMobjState(mo, S_PLAY_FALL1);
				break;
		}
	}
	else
		P_SetPlayerMobjState(mo, S_PLAY_FALL1);
}
#endif




#ifdef BOTWAYPOINTS
static botwaypoint_t *waypoints;
static botwaypoint_t *lastpoint;
#endif

void JB_LevelInit(void)
{
#ifdef BOTWAYPOINTS
	waypoints = NULL;
	lastpoint = NULL;
#endif
	//if (players[i].bot && !players[i].mo)
	if (netgame) // SRB2CBTODO: Netgame needs mem set stuff
		memset(bots, 0,sizeof(bots)); // isn't this already done?
}

static boolean JB_AngleMove(player_t *player)
{
	// This determines if the player is facing the direction they are travelling or not.
	// Didn't your teacher say to pay attention in Geometry/Trigonometry class? ;)
	if ((player->rmomx > 0 && player->rmomy > 0) && (/*player->mo->angle >= 0 &&*/ player->mo->angle < ANG90)) // Quadrant 1
		return 1;
	else if ((player->rmomx < 0 && player->rmomy > 0) && (player->mo->angle >= ANG90 && player->mo->angle < ANG180)) // Quadrant 2
		return 1;
	else if ((player->rmomx < 0 && player->rmomy < 0) && (player->mo->angle >= ANG180 && player->mo->angle < ANG270)) // Quadrant 3
		return 1;
	else if ((player->rmomx > 0 && player->rmomy < 0) && ((player->mo->angle >= ANG270 && (player->mo->angle <= ANGLE_MAX))
														  || (/*player->mo->angle >= 0 &&*/ player->mo->angle <= ANG45))) // Quadrant 4
		return 1;
	else if (player->rmomx > 0 && ((player->mo->angle >= ANG270+ANG45 && player->mo->angle <= ANGLE_MAX)))
		return 1;
	else if (player->rmomx < 0 && (player->mo->angle >= ANG90+ANG45 && player->mo->angle <= ANG180+ANG45))
		return 1;
	else if (player->rmomy > 0 && (player->mo->angle >= ANG45 && player->mo->angle <= ANG90+ANG45))
		return 1;
	else if (player->rmomy < 0 && (player->mo->angle >= ANG180+ANG45 && player->mo->angle <= ANG270+ANG45))
		return 1;
	else
		return 0;
}


#ifdef BOTWAYPOINTS
void JB_CreateWaypoint(fixed_t x, fixed_t y, fixed_t z, boolean spring)
{
	if (!lastpoint)
		waypoints = lastpoint = malloc(sizeof(*lastpoint));
	else
	{
		lastpoint->next = malloc(sizeof(*lastpoint));
		lastpoint = lastpoint->next;
	}
	lastpoint->x = x;
	lastpoint->y = y;
	lastpoint->z = z;
	lastpoint->sec = R_PointInSubsector(x,y)->sector;
	lastpoint->springpoint = spring; //&& (z > lastpoint->z);
	lastpoint->next = NULL;
}

void JB_UpdateWaypoints(void)
{
	USHORT i;
	mobj_t *mo;

	if (gametype != GT_RACE)
		return;

	// Update waypoint list for all bots!
	for (i = 0; i < MAXPLAYERS; i++)
		if (playeringame[i] && !players[i].bot)
		{
			if (players[i].exiting && lastpoint)
				return;

			mo = players[i].mo;
			// Player has gone farther?
			// Add a new waypoint!
			if (!lastpoint //|| mo->subsector->sector != lastpoint->sec
			   || ((players[i].cmd.angleturn != (short)(players[i].mo->angle>>16) || players[i].cmd.sidemove)
				   && P_AproxDistance(lastpoint->x - mo->x, lastpoint->y - mo->y) > 128*FRACUNIT)
			   || (abs(mo->z-lastpoint->z) > MAXSTEPMOVE && mo->player->mfjumped))
			{
				//JB_CreateWaypoint(mo->x, mo->y, mo->z, false);
				//JB_CreateWaypoint(mo->x+mo->momx, mo->y+mo->momy, mo->z+mo->momz, false);
			}
			return;
		}
}
#endif

// Decrease all don't look list entry timers and remove old ones
static inline void JB_UpdateLook(bot_t *bot)
{
#ifdef BOTWAYPOINTS
	mobj_t *mo;
	botwaypoint_t *point;
#endif

	if (!bot || gametype == GT_COOP)
		return;

	if (!bot->player->mo)
		I_Error("JB_UpdateLook: Null bot!");

#ifdef BOTWAYPOINTS
	// Change waypoint?
	mo = bot->player->mo;
	if (!waypoints) // No waypoints at all?
		; // I guess you dun need 'em for this gametype, then!
	else if (!bot->waypoint) // No waypoint?
	{
		bot->waypoint = waypoints; // Start at the beginning.
		bot->waydist = P_AproxDistance(P_AproxDistance(mo->x - bot->waypoint->x, mo->y - bot->waypoint->y),
									   mo->z - bot->waypoint->z);
	}
	else
	{
		if (P_AproxDistance(P_AproxDistance(mo->x - bot->waypoint->x,mo->y - bot->waypoint->y),
							mo->z - bot->waypoint->z) > bot->waydist+32*FRACUNIT)
		{
			// This loop finds the closest waypoint
			bot->waypoint = waypoints;
			for (point = waypoints->next; point; point = point->next)
			{
				if (P_AproxDistance(P_AproxDistance(
												   mo->x - point->x,
												   mo->y - point->y),
								   mo->z - point->z)
				   < P_AproxDistance(P_AproxDistance(
													 mo->x - bot->waypoint->x,
													 mo->y - bot->waypoint->y),
									 mo->z - bot->waypoint->z))
					bot->waypoint = point; // Switch to it, then!
			}
		}

		// This loop goes to next waypoint and accounts for skipping
		// waypoints, both at once.
		for (point = bot->waypoint; point; point = point->next)
		{
			if (mo->subsector->sector == point->sec // In same sector as waypoint?
			   // And close enough to it to switch to the next one?
			   && P_AproxDistance(P_AproxDistance(mo->x - point->x, mo->y - point->y),mo->z - point->z) < 128*FRACUNIT
			   && point->next) // And it has a next one?
			{
				bot->waypoint = point->next; // Switch to it, then!
				bot->waydist = P_AproxDistance(P_AproxDistance(
															   mo->x - bot->waypoint->x,
															   mo->y - bot->waypoint->y),
											   mo->z - bot->waypoint->z);
				// No skipping springpoints.
				if (point->next->springpoint)
					break;
			}
		}

		// Mark the location of your waypoint for this tic.
		if (cv_devmode || devparm)
		{
			mo = P_SpawnMobj(bot->waypoint->x, bot->waypoint->y, bot->waypoint->z, MT_REDXVI);
			mo->flags = MF_NOBLOCKMAP|MF_NOCLIP|MF_NOGRAVITY|MF_SCENERY;
			mo->angle = bot->player->mo->angle;
			mo->fuse = 2*NEWTICRATERATIO;
		}
	}
#endif

	// Add to list?
	bot->targettimer++;

}

//////////////////////
// SEARCH FUNCTIONS //
//////////////////////

static mobj_t *JB_Look4Collect(int botnum)
{
	thinker_t* think;
	mobj_t *mo, *lastmo, *botmo;
	botmo = players[botnum].mo;
	//bot_t *bot = &bots[botnum]; // To identify bot by itself (left here just in case it could be useful)
	fixed_t dist,lastdist;

	for (think = thinkercap.next, mo = lastmo = NULL, lastdist = 0;
		think != &thinkercap; think = think->next)
	{
		if (think->function.acp1 != (actionf_p1)P_MobjThinker) // Not a mobj thinker
			continue;

		mo = (mobj_t*)think;

		if (botmo == mo)
			continue;
		switch(mo->type)
		{
			case MT_PLAYER:
				// If it's not REALLY a player or if it's not alive
				// just skip it... No point in worrying.
				if (!mo->player
				   || mo->player->playerstate != PST_LIVE
				   || mo->player->powers[pw_flashing]
				   || mo->player->powers[pw_invulnerability]
					|| mo->player->spectator)
					continue;
				// Spectator
				if (gametype == GT_CTF && !mo->player->ctfteam)
					continue;
				// Same team
				if (gametype == GT_CTF && mo->player->ctfteam == botmo->player->ctfteam)
					continue;
				if (gametype == GT_MATCH && GTF_TEAMMATCH == 1
				   && mo->player->skincolor == botmo->player->skincolor)
					continue;
				if (gametype == GT_MATCH && GTF_TEAMMATCH == 2
				   && mo->player->skin == botmo->player->skin)
					continue;
				// If player is not close, the bot cannot see them,
				// or the bot doesn't have the rings to do anything about it anyway...
				// So it does not worry.
				dist = P_AproxDistance(P_AproxDistance(botmo->x - mo->x, botmo->y - mo->y), botmo->z - mo->z);
				if (botmo->health <= 2
				   || mo->z > botmo->z + (128<<FRACBITS)
				   // Don't go after them if you're not flashing...
				   || (!botmo->player->powers[pw_flashing]
					   // And they're farther then 1024 units from you.
					   && dist > (1024<<FRACBITS))
				   || !P_CheckSight(botmo,mo))
					continue;
				// Otherwise... I worry.
				// I can't look for rings if I'm being watched.
				// I must fight, ready or not!
				return mo;

			case MT_RING:
			case MT_COIN:
			case MT_FLINGRING:
			case MT_FLINGCOIN:
			case MT_BOUNCERING:
			case MT_RAILRING:
			case MT_SCATTERRING:
			case MT_AUTOMATICRING:
			case MT_GRENADERING:
			case MT_EXPLOSIONRING:

			case MT_BOUNCEPICKUP:
			case MT_RAILPICKUP:
			case MT_SCATTERPICKUP:
			case MT_AUTOPICKUP:
			case MT_GRENADEPICKUP:
			case MT_EXPLODEPICKUP:

			case MT_EXTRALARGEBUBBLE: // Take bubbles too!
				// Yes! It's a ring! Score! :D
				if (mo->z < botmo->z + (128<<FRACBITS)
				   && P_CheckSight(botmo,mo))
					break;
				// Can't see it or don't think you can jump to it? Too bad...
				continue;

			default:
				// Monitor? Go for it.
				if (mo->flags & MF_MONITOR
				   && !(mo->flags & MF_NOCLIP)
				   && mo->health
				   && mo->z < botmo->z + (128<<FRACBITS)
				   && P_CheckSight(botmo,mo))
					break;
				// Check if a spring is the closest thing to you.
				// Only use it if you're within stepping distance
				// as well as closer to you then anything else
				// that you find... Otherwise, forget it.
				if (mo->flags & MF_SPRING
				   && botmo->state-states != S_PLAY_PLG1
				   && abs(botmo->z - mo->z) < 128<<FRACBITS
				   && P_CheckSight(botmo,mo))
					break;
				// Not anything I need to look at.
				continue;
		}
		dist = P_AproxDistance(P_AproxDistance(botmo->x - mo->x, botmo->y - mo->y), botmo->z - mo->z);
		if (lastmo && dist > lastdist) // Last one is closer to you?
			continue;

		// Found a target
		lastmo = mo;
		lastdist = dist;
	}
	return lastmo;
}

static mobj_t *JB_Look4Poppable(int botnum)
{
	thinker_t* think;
	mobj_t *mo, *lastmo, *botmo = players[botnum].mo;
	bot_t *bot = &bots[botnum];
	fixed_t dist,lastdist;

	// Simple co-op hack: Check things against your owner, rather then yourself.
	if (gametype == GT_COOP)
		botmo = players[bot->ownernum].mo;

	for (think = thinkercap.next, mo = lastmo = NULL, lastdist = 0;
		think != &thinkercap; think = think->next)
	{
		if (think->function.acp1 != (actionf_p1)P_MobjThinker) // Not a mobj thinker
			continue;

		mo = (mobj_t*)think;

		if (botmo == mo)
			continue;
		switch(mo->type)
		{
			case MT_RING:
			case MT_COIN:
			case MT_FLINGRING:
			case MT_FLINGCOIN:
			case MT_BOUNCERING:
			case MT_RAILRING:
			case MT_SCATTERRING:
			case MT_GRENADERING:
			case MT_AUTOMATICRING:
			case MT_EXPLOSIONRING:

			case MT_BOUNCEPICKUP:
			case MT_RAILPICKUP:
			case MT_SCATTERPICKUP:
			case MT_AUTOPICKUP:
			case MT_GRENADEPICKUP:
			case MT_EXPLODEPICKUP:

			case MT_EXTRALARGEBUBBLE: // Take bubbles too!
				// Yes! It's a ring! Score! :D
				if (mo->z < botmo->z + (128<<FRACBITS)
				   && P_CheckSight(botmo,mo))
					break;
				// Can't see it or don't think you can jump to it? Too bad...
				continue;

			default:
				// Enemy? Boss? Monitor? Kill it!
				if ((mo->flags & MF_ENEMY
					|| mo->flags & MF_BOSS
					|| (mo->flags & MF_MONITOR
						&& !(mo->flags & MF_NOCLIP)
						&& gametype != GT_COOP))
				   && mo->health
				   && mo->z < botmo->z + (128<<FRACBITS)
				   && P_CheckSight(botmo,mo))
					break;
				// Not anything I need to look at.
				continue;
		}

		// Ah, this makes things easier... Only calculate the distance once per mobj.
		dist = P_AproxDistance(P_AproxDistance(botmo->x - mo->x, botmo->y - mo->y), botmo->z - mo->z);

		// In co-op, never go after anything farther then 512 fracunits away.
		if (gametype == GT_COOP && dist > 512<<FRACBITS)
			continue;

        // Last one is closer to you?
		if (lastmo && dist > lastdist)
			continue;

		// Found a target
		lastmo = mo;
		lastdist = dist;
	}
	return lastmo;
}

static inline mobj_t *JB_Look4Enemy(int botnum)
{
	thinker_t* think;
	mobj_t *mo, *lastmo, *botmo = players[botnum].mo;
	//bot_t *bot = &bots[botnum]; // To identify bot by itself (left here just in case it could be useful)
	fixed_t dist,lastdist;

	for (think = thinkercap.next, mo = NULL, lastmo = NULL; think != &thinkercap; think = think->next)
	{
		if (think->function.acp1 != (actionf_p1)P_MobjThinker) // Not a mobj thinker
			continue;

		mo = (mobj_t*)think;

		if (botmo == mo
			)
			continue;

		// Not an enemy or boss? Dun need it.
		if (!(mo->flags & MF_ENEMY
			 || mo->flags & MF_BOSS)
		   || !mo->health
		   || mo->z >= botmo->z + (128<<FRACBITS)
		   || !P_CheckSight(botmo,mo))
			continue;

		dist = P_AproxDistance(P_AproxDistance(botmo->x - mo->x, botmo->y - mo->y), botmo->z - mo->z);

		// Found a target
		lastmo = mo;
		lastdist = dist;

		if (lastmo && dist > lastdist) // Last one is closer to you?
			continue;


	}
	return lastmo;
}

static mobj_t *JB_Look4Players(int botnum)
{
	thinker_t* think;
	mobj_t *mo, *lastmo, *botmo = players[botnum].mo;
	//bot_t *bot = &bots[botnum]; // To identify bot by itself (left here just in case it could be useful)
	fixed_t dist,lastdist;

	for (think = thinkercap.next, mo = lastmo = NULL, lastdist = 0;
		think != &thinkercap; think = think->next)
	{
		if (think->function.acp1 != (actionf_p1)P_MobjThinker) // Not a mobj thinker
			continue;

		mo = (mobj_t*)think;

		if (botmo == mo
			)
			continue;
		switch(mo->type)
		{
			case MT_PLAYER: // SRB2CBTODO: No spectators!

				// If it's not REALLY a player or if it's not alive
				// just skip it... No point in chasing.
				if (!mo->player
				   || mo->player->playerstate != PST_LIVE
				   || mo->player->powers[pw_flashing]
				   || mo->player->powers[pw_invulnerability]
				   || mo->player->spectator)
					continue;
				// Spectator
				if (gametype == GT_CTF && !mo->player->ctfteam)
					continue;
				// Same team
				if (gametype == GT_CTF && mo->player->ctfteam == botmo->player->ctfteam)
					continue;
				if (gametype == GT_MATCH && GTF_TEAMMATCH == 1
				   && mo->player->skincolor == botmo->player->skincolor)
					continue;
				if (gametype == GT_MATCH && GTF_TEAMMATCH == 2
				   && mo->player->skin == botmo->player->skin)
					continue;
				// If the player is visible, go for it.
				if (P_CheckSight(botmo,mo))
					break;
				continue;

			case MT_RING:
			case MT_COIN:
			case MT_FLINGRING:
			case MT_FLINGCOIN:
			case MT_BOUNCERING:
			case MT_RAILRING:
			case MT_SCATTERRING:
			case MT_GRENADERING:
			case MT_AUTOMATICRING:
			case MT_EXPLOSIONRING:

			case MT_BOUNCEPICKUP:
			case MT_RAILPICKUP:
			case MT_SCATTERPICKUP:
			case MT_AUTOPICKUP:
			case MT_GRENADEPICKUP:
			case MT_EXPLODEPICKUP:

			case MT_EXTRALARGEBUBBLE: // Take bubbles too!
				// A ring?... Only take
				// it if noone's around!
				if ((!lastmo || lastmo->type != MT_PLAYER)
				   && mo->z < botmo->z + (128<<FRACBITS)
				   && P_CheckSight(botmo,mo))
					break;
				// Can't see it?! Too bad...
				continue;

			default:
				// Not anything I need to look at.
				continue;
		}

		dist = P_AproxDistance(P_AproxDistance(botmo->x - mo->x, botmo->y - mo->y), botmo->z - mo->z);
		if (lastmo && dist > lastdist) // Last one is closer to you?
			continue;

		// Found a target
		lastmo = mo;
		lastdist = dist;
	}
	return lastmo;
}

static inline mobj_t *JB_Look4BotOverlap(int botnum)
{
	thinker_t* think;
	mobj_t *mo, *botmo = players[botnum].mo;

	for (think = thinkercap.next, mo = NULL; think != &thinkercap; think = think->next)
	{
		if (think->function.acp1 != (actionf_p1)P_MobjThinker) // Not a mobj thinker
			continue;

		mo = (mobj_t*)think;

		if (botmo == mo)
			continue;

		// If it's not REALLY a bot or if it's not alive
		// just skip it... No point in chasing.
		if (!mo->player || !mo->player->bot
		   || mo->player->playerstate != PST_LIVE)
			continue;

		// If the bot isn't overlapping the current bot
		// Avoiding it isn't needed
		if (botmo->z < mo->z+mo->height
		   && mo->z < botmo->z+botmo->height
		   && P_AproxDistance(botmo->x - mo->x, botmo->y - mo->y)
		   < mo->radius + botmo->radius)
			return mo;
	}
	return NULL;
}

// SRB2CBTODO: make a function for checkpoint search
static mobj_t *JB_Look4StarPost(int botnum)
{
	thinker_t* think;
	mobj_t *mo, *lastmo, *botmo = players[botnum].mo;
	//bot_t *bot = &bots[botnum]; // To identify bot by itself (left here just in case it could be useful)
	fixed_t dist,lastdist;

	// Already springing? Don't look for another!
	//if (botmo->state-states == S_PLAY_PLG1)
	//	if (players[botnum].starpostbit & (1<<(mo->health-1)))
	//		return NULL;

	//	if (botmo->

	for (think = thinkercap.next, mo = lastmo = NULL, lastdist = 0;
		think != &thinkercap; think = think->next)
	{
		if (think->function.acp1 != (actionf_p1)P_MobjThinker) // Not a mobj thinker
			continue;

		mo = (mobj_t*)think;

		if (!(mo->type == MT_STARPOST) // Not a spring...
			)
			continue;

		dist = P_AproxDistance(P_AproxDistance(botmo->x - mo->x, botmo->y - mo->y), botmo->z - mo->z);
		if (lastmo && dist > lastdist) // Last one is closer to you?
			continue;

		if (players[botnum].starpostbit & (1<<(mo->health-1))) // already hit this post
			return NULL;

		// Found a target
		lastmo = mo;
		lastdist = dist;

		// This is probably the one they used, 'cause it's in it's bounce animation!
		//if (mo->state == &states[mo->info->seestate])
		if (players[botnum].starpostbit & (1<<(mo->health-1)))
			break;
	}
	return lastmo;
}



static mobj_t *JB_Look4Spring(int botnum)
{
	thinker_t* think;
	mobj_t *mo, *lastmo, *botmo = players[botnum].mo;
	//bot_t *bot = &bots[botnum]; // To identify bot by itself (left here just in case it could be useful)
	fixed_t dist,lastdist;

	// Already springing? Don't look for another!
	if (botmo->state-states == S_PLAY_PLG1)
		return NULL;

	for (think = thinkercap.next, mo = lastmo = NULL, lastdist = 0;
		think != &thinkercap; think = think->next)
	{
		if (think->function.acp1 != (actionf_p1)P_MobjThinker) // Not a mobj thinker
			continue;

		mo = (mobj_t*)think;

		if (!(mo->flags & MF_SPRING) // Not a spring...
			)
			continue;

		dist = P_AproxDistance(P_AproxDistance(botmo->x - mo->x, botmo->y - mo->y), botmo->z - mo->z);
		if (lastmo && dist > lastdist) // Last one is closer to you?
			continue;

		// Found a target
		lastmo = mo;
		lastdist = dist;

		// This is probably the one they used, 'cause it's in it's bounce animation!
		if (mo->state == &states[mo->info->seestate])
			break;
	}
	return lastmo;
}

static mobj_t *JB_Look4AirBubble(int botnum)
{
	thinker_t* think;
	mobj_t *mo, *lastmo, *botmo = players[botnum].mo;
	fixed_t dist,lastdist;

	for (think = thinkercap.next, mo = lastmo = NULL, lastdist = 0;
		think != &thinkercap; think = think->next)
	{
		if (think->function.acp1 != (actionf_p1)P_MobjThinker) // Not a mobj thinker
			continue;

		mo = (mobj_t*)think;

		if (mo->type != MT_EXTRALARGEBUBBLE // Not air bubble...
		   && !(mo->flags & MF_SPRING) // Not spring...
		   && ((mo->state->action.acp1 != (actionf_p1)A_BubbleSpawn // Not a bubble spawn...
				&& mo->state->action.acp1 != (actionf_p1)A_BubbleCheck)
			   || (lastmo && lastmo->type == MT_EXTRALARGEBUBBLE))) // Or already targetting a bubble...
			continue;

		// Can't get it if you can't see it!
		if (!P_CheckSight(botmo,mo))
			continue;

		dist = P_AproxDistance(P_AproxDistance(botmo->x - mo->x, botmo->y - mo->y), botmo->z - mo->z);
		if (lastmo && dist > lastdist) // Last one is closer to you?
			continue;

		// Found a target
		lastmo = mo;
	}
	return lastmo;
}

/////////////////////
// THINK FUNCTIONS //
/////////////////////

// JB_BotWander is used only for bots that have no target,
// so that bots don't crash if they have nothing to do for targeting
// SRB2CBTODO: Bot AI's need to be updated with new level special numbers
static void JB_BotWander(int botnum)
{
	player_t* player = &players[botnum];
	mobj_t* mo = players[botnum].mo;
	ticcmd_t* cmd = &players[botnum].cmd;

	// Bot info
	boolean jumping = (player->pflags & PF_JUMPDOWN);
	boolean botonground;
	boolean abilityjump;

	if (!player->mo)
		I_Error("JB_BotWander: players[%d].mo == NULL", player - players);

	botonground = P_IsObjectOnGround(player->mo);

	cmd->angleturn = TICCMD_RECEIVED; // Treat the bot AI kinda like a multiplayer netgame
	cmd->forwardmove = 50/NEWTICRATERATIO; // Go full speed. Always.

	// Ability stuff
	abilityjump = false;
	switch(player->charability)
	{
		case 0: // Thok
			// Thok!
			if (!botonground && !jumping && mo->momz <= 0)
				cmd->buttons |= BT_JUMP;
			// Jump to full height!
			else if ((P_InQuicksand(player->mo)) || (((!jumping && botonground)
					|| (jumping && mo->momz > 0)) && leveltime % 13 == 0))
				cmd->buttons |= BT_JUMP;
			// Ready the jump button!
			else
				cmd->buttons &= ~BT_JUMP;
			// Ability is controlling jump button! MWAHAHAHA!
			abilityjump = true;
			break;

		case 1: // Fly
		case 7: // Swim
			if ((player->charability == CA_SWIM // No swim out of water!
				&& !(player->mo->eflags & MFE_UNDERWATER))
			   || mo->health < 10) // No snipe without ammo!
				break;
			// Fly!
			else if (!botonground && !jumping && mo->momz <= 0)
				cmd->buttons |= BT_JUMP;
			// Jump to full height!
			else if ((P_InQuicksand(player->mo)) || (((!jumping && botonground)
					|| (jumping && mo->momz > 0)) && leveltime % 13 == 0))
				cmd->buttons |= BT_JUMP;
			// Ready the jump button!
			else
				cmd->buttons &= ~BT_JUMP;
			// Ability is controlling jump button! MWAHAHAHA!
			abilityjump = true;
			break;

		case 2: // Glide and climb
		case 3: // Glide with no climb
			break;

		case 4: // Double-Jump
			// Jump again at top of jump height!
			if (!botonground && !jumping && mo->momz <= 0)
				cmd->buttons |= BT_JUMP;
			break;

		default:
			break;
	}
}

static void JB_Jump4AirBubble(int botnum)
{
	player_t* player = &players[botnum];
	mobj_t* mo = players[botnum].mo;
	ticcmd_t* cmd = &players[botnum].cmd;

	// Bot info
	boolean jumping = (player->pflags & PF_JUMPDOWN);
	boolean botonground;

	if (!player->mo)
		I_Error("JB_Jump4AirBubble: players[%d].mo == NULL", player - players);

	botonground = P_IsObjectOnGround(player->mo);

	cmd->angleturn = TICCMD_RECEIVED; // Treat the bot AI kinda like a multiplayer netgame
	cmd->forwardmove = 0; // Don't bother moving.

	// Use your ability, whatever it is, at full jump height.
	if (!botonground && !jumping && mo->momz <= 0)
		cmd->buttons |= BT_JUMP;
	// Jump, jump, jump, as high as you can. You can't catch me, I'm in need of air, man!
	else if (botonground || (jumping && mo->momz > 0))
		cmd->buttons |= BT_JUMP;
}

static void JB_CoopThink(int botnum)
{
	player_t* player = &players[botnum];
	bot_t* bot = &bots[botnum];
	mobj_t* mo = players[botnum].mo;
	ticcmd_t* cmd = &players[botnum].cmd;

	fixed_t dist;
	angle_t angle;

	// Bot info
	boolean jumping = (player->pflags & PF_JUMPDOWN);
	boolean botonground;

	if (!player->mo)
		I_Error("JB_CoopThink: players[%d].mo == NULL", player - players);

	botonground = P_IsObjectOnGround(player->mo);

	// Targetting
	bot->target = JB_Look4BotOverlap(botnum);
	if (!bot->target)
	{
		bot->target = players[bot->ownernum].mo;
		if (bot->target && bot->target->state == &states[S_PLAY_PLG1])
		{
			bot->target = JB_Look4Spring(botnum);
			if (!bot->target)
				bot->target = players[bot->ownernum].mo;
		}
	}

	// Should not occur anymore.
	// But just in case...
	if (!bot->target)
	{
		JB_BotWander(botnum);
		return;
	}

	// Target info
	dist = (P_AproxDistance(bot->target->x - mo->x, bot->target->y - mo->y));
	dist /= FRACUNIT;
	if (player->pflags & PF_STARTDASH)
		angle = bot->target->angle; // You facing same direction as spindash target.
	else
		angle = R_PointToAngle2(mo->x, mo->y, bot->target->x, bot->target->y); // You facing target.


	// Too far away? Disappear and wait to respawn, then...
	if (playeringame[botnum] && playeringame[bot->ownernum]
		&& bot->player->mo && bot->player->mo->subsector && bot->player->mo->subsector->sector)
		if (dist > 1512 && !P_CheckSight(mo, bot->target) && !(players[bot->ownernum].pflags & PF_CARRIED))
		{
			//player->playerstate = PST_REBORN;
			if (mo->player && mo->player->bot)
			{
				JB_CoopSpawnBot(botnum);
				//P_UnsetThingPosition(mo);
				//mo->flags |= MF_NOBLOCKMAP;
				//mo->flags |= MF_NOSECTOR;
				//mo->flags2 |= MF2_DONTDRAW;
				//return;
			}
		}

	// Cheating co-op speeds!
	if (bot->target->player)
	{
		// so you can keep up, set your speed to the player's normalspeed
		player->normalspeed = bot->target->player->normalspeed;
		player->thrustfactor = 5;
		player->accelstart = 50;
		player->acceleration = 50;
	}

	// Turning movement
	cmd->angleturn = 0; // Command pending...
	if (!player->climbing)
	{
		if ((mo->angle - ANGLE_10) - angle < angle - (mo->angle - ANGLE_10))
			cmd->angleturn = (short)(cmd->angleturn - (2560/NEWTICRATERATIO)); // Turn right!
		else if ((mo->angle + ANGLE_10) - angle > angle - (mo->angle + ANGLE_10))
			cmd->angleturn = (short)(cmd->angleturn + (2560/NEWTICRATERATIO)); // Turn left!
		mo->angle += (cmd->angleturn<<16); // Set the angle of your mobj
		cmd->angleturn = (short)(mo->angle>>16); // And set that to your turning. For some reason.
	}
	cmd->angleturn |= TICCMD_RECEIVED; // Treat the bot AI kinda like a multiplayer netgame

	// Forward movement
	if ((player->pflags & PF_CARRIED) // If you're being carried
	   || (mo->momz > 0 && !jumping // Or you're bouncing on a spring...
		   && bot->springmove)) // And you're already moving in a direction from it...
		cmd->forwardmove = 0; // Do nothing. Moving could ruin it.
	else if (player->pflags & PF_SPINNING) // If you're spinning...
		cmd->forwardmove = 50/NEWTICRATERATIO; // Go ahead and spin!
	else if (bot->target->player && bot->target->player->bot) // If getting out of the way of another bot...
		cmd->forwardmove = -50/NEWTICRATERATIO; // Backstep, please.

	else if (dist > 256 // If you're farther then 256 units away...
			|| !bot->target->player // Or if you're not chasing a player...
			// Or if you're climbing and I'm not...
			//|| (player->climbing && bot->target->player && !bot->target->player->climbing)
			|| mo->floorz < bot->target->floorz) // Or if you're just below me.
		cmd->forwardmove = 50/NEWTICRATERATIO; // Go full speed
	else if (dist > 128 && bot->target->player && !bot->target->player->bot)
		cmd->forwardmove = 25/NEWTICRATERATIO; // Start slowing down.

	else // If you're closer then 128 units away...
		cmd->forwardmove = 0; // Stop. :|

	// Spindash stuff
	if (bot->target->player)
	{
		// If your player is starting to dash, you should too!
		if (bot->target->player->pflags & PF_STARTDASH)
		{
			if (!mo->momx && !mo->momy)
				cmd->buttons |= BT_USE;
		}
		// If your player is spinning, you should too!
		else if (bot->target->player->pflags & PF_SPINNING
				 && !(player->pflags & PF_STARTDASH))
			cmd->buttons |= BT_USE;
		else // Otherwise...
			cmd->buttons &= ~BT_USE; // I guess you shouldn't be spinning, then...
	}

	// Jumping stuff
	if (!botonground && !jumping) // In the air but not holding the jump button?
		cmd->buttons &= ~BT_JUMP; // Don't press it again, then.
	else if (bot->target->z > mo->z + mo->height // If your target is above your head...
			&& bot->target->state != &states[S_PLAY_PLG1]) // And they didn't just fly off a spring...
		cmd->buttons |= BT_JUMP; // Then jump!
	else if (bot->target->z > mo->z + MAXSTEPMOVE // If your target is higher up then you can step...
			&& (dist >= 256 // And you're far enough that you're worried about being left behind...
				|| player->speed > player->runspeed) // Or going at running speed...
			&& !jumping // And you're not jumping already...
			&& bot->target->state != &states[S_PLAY_PLG1]) // And they didn't just fly off a spring...
		cmd->buttons |= BT_JUMP; // Then jump!
	else if (bot->target->z > mo->z // If your target's still above you...
			&& jumping && mo->momz > 0) // And you're jumping and still going up...
		cmd->buttons |= BT_JUMP; // Continue to do so!
	else // Otherwise...
		cmd->buttons &= ~BT_JUMP; // I guess you shouldn't be jumping, then...

	fixed_t ownerdist = (P_AproxDistance(bot->target->x - players[bot->ownernum].mo->x, bot->target->y - players[bot->ownernum].mo->y));
	ownerdist /= FRACUNIT;

	if (players[bot->ownernum].mo->scale <= player->mo->scale+50)
	{
		if (player->powers[pw_tailsfly] && (players[bot->ownernum].pflags & PF_CARRIED) && players[bot->ownernum].mo->z < player->mo->z)
		{
			ticcmd_t* ownercmd = &players[bot->ownernum].cmd;
			int thrust = abs(ownercmd->forwardmove)*1512/3;
			if (ownercmd->forwardmove > 0)
				P_Thrust(player->mo, players[bot->ownernum].mo->angle, thrust);
			else if (ownercmd->forwardmove < 0)
				P_Thrust(player->mo, players[bot->ownernum].mo->angle, -thrust);
			if (ownercmd->buttons & BT_USE)
				P_SetObjectMomZ(player->mo, -1*FRACUNIT, true, false);
		}

		if (player->powers[pw_tailsfly] && ownerdist < 64*FRACUNIT && !(players[bot->ownernum].pflags & PF_CARRIED))
		{
			if (player->mo->momy > 10)
				player->mo->momy /= 2;
			if (player->mo->momx > 10)
				player->mo->momx /= 2;
		}
	}


	// Ability stuff
	boolean abilityjump = false;

	int abilitytime = 50+P_Random();
	if (abilitytime > 150)
		abilitytime = 150;

	switch(player->charability)
	{
		case 0: // Thok
		{

			// Jump to full height!
			if (dist > 500 && (((!jumping && botonground) || (jumping && mo->momz > 0))))
					cmd->buttons |= BT_JUMP;
				// Thok!
				else if ((!botonground && !jumping && mo->momz <= 0))
				{
					cmd->buttons |= BT_JUMP;
					//ready = false;
				}
				// Ready the jump button!
				else
				{
					cmd->buttons &= ~BT_JUMP;
				}
				// Ability is controlling jump button! MWAHAHAHA!
				abilityjump = true;
			}
			break;

		case 1: // Fly
		case 7: // Swim
		{
			int flytime;
			flytime= 50+P_Random();
			if (flytime > 200)
				flytime = 200;
			if (bot->target->flags & MF_SPRING // No fly over spring!
			   || (player->charability == CA_SWIM // No swim out of water!
				   && !(player->mo->eflags & MFE_UNDERWATER)))
				break;
			// Jump to full height!
			else if (!(bot->player->pflags & PF_CARRIED) // If another player is trying to carry you don't jump
					&& (((!jumping && botonground) // Don't jump if you already did
						 && (leveltime % flytime == 0 // It's time to jump
							 || (players[bot->ownernum].mo->z) > player->mo->z+128*FRACUNIT) // or your owner is higher than you can jump
						 && ((bot->target->player || !bot->target))))) // don't start flying if you're not focusing on your owner
				cmd->buttons |= BT_JUMP;
			// Control while flying in the air
			else if (!(bot->player->pflags & PF_CARRIED) // If another player is trying to carry you don't jump
					&& (!botonground
					&& (!jumping && mo->momz <= 0) // you're flying and you're falling
					))
			{
				if (!(players[bot->ownernum].pflags & PF_CARRIED))
				{
					if (player->mo->z < players[bot->ownernum].mo->z+80*FRACUNIT)
					{
						if (player->mo->z < player->mo->floorz+32*FRACUNIT)
							cmd->buttons |= BT_JUMP;
						else
						{
							if (leveltime % 12 == 0)
								cmd->buttons |= BT_JUMP;
						}
					}
				}
				else
					cmd->buttons |= BT_JUMP;
			}
			else
				cmd->buttons &= ~BT_JUMP; // dont hold the jump button unless the above condidtions are met
			// Ability is controlling jump button! MWAHAHAHA!
			abilityjump = true;
		}
			break;

		case 2: // Glide and climb
		case 3: // Glide with no climb
			if (player->climbing) // Don't get stuck on walls
				cmd->buttons |= BT_USE;

			if (dist > 2 // Target still above you
			   && !botonground && !jumping // You're in the air but not holding the jump button
			   && mo->momz <= 0) // You aren't gonna get high enough
			{ // So what do you do? Glide!... I dunno.
				cmd->buttons |= BT_JUMP;
				abilityjump = true;
			}
			break;

		case 4: // Double-Jump
			// Jump again at top of jump height!
			if (!botonground && !jumping && mo->momz <= 0)
				cmd->buttons |= BT_JUMP;
			break;

		default:
			break;
	}


}

static void JB_SmartCoopThink(int botnum)
{
	player_t* player = &players[botnum];
	bot_t* bot = &bots[botnum];
	mobj_t* mo = players[botnum].mo;
	ticcmd_t* cmd = &players[botnum].cmd;

	fixed_t dist;
	angle_t angle;
	boolean aimed;
	sector_t *nextsector;

	// Bot info
	boolean jumping = (player->pflags & PF_JUMPDOWN);
	boolean botonground;

	if (!player->mo)
		I_Error("JB_SmartCoopThink: players[%d].mo == NULL", player - players);

	botonground = P_IsObjectOnGround(player->mo);

	// Targeting
	if (player->powers[pw_underwater] && player->powers[pw_underwater] < 15*TICRATE)
	{
		bot->target = JB_Look4AirBubble(botnum);
		if (!bot->target) // Uh oh... No air?! Try to jump as high as you can, then!
		{
			JB_Jump4AirBubble(botnum);
			return;
		}
	}
	else
		bot->target = JB_Look4Poppable(botnum);
	// Nothing left in the area to do? Move on, then.
	if (!bot->target ||(player->powers[pw_tailsfly]
		&& (players[bot->ownernum].pflags & PF_CARRIED) && players[bot->ownernum].mo->z < player->mo->z))
	{
		JB_CoopThink(botnum);
		return;
	}

	// Target info
	dist = P_AproxDistance(P_AproxDistance(
										   bot->target->x - player->mo->x,
										   bot->target->y - player->mo->y),
						   bot->target->z - player->mo->z) / FRACUNIT;
	angle = R_PointToAngle2(mo->x, mo->y, bot->target->x, bot->target->y); // You facing target.
	nextsector = R_PointInSubsector(mo->x + mo->momx*2, mo->y + mo->momy*2)->sector;

	// Turning movement
	aimed = false;


	// Too far away? Disappear and wait to respawn, then...
	if (playeringame[botnum] && playeringame[bot->ownernum]
		&& bot->player->mo && bot->player->mo->subsector && bot->player->mo->subsector->sector)
		if (dist > 1512 && !P_CheckSight(mo, bot->target) && !(players[bot->ownernum].pflags & PF_CARRIED))
		{
			//player->playerstate = PST_REBORN;
			if (mo->player && mo->player->bot)
			{
				JB_CoopSpawnBot(botnum);
				//P_UnsetThingPosition(mo);
				//mo->flags |= MF_NOBLOCKMAP;
				//mo->flags |= MF_NOSECTOR;
				//mo->flags2 |= MF2_DONTDRAW;
				//return;
			}
		}


	// Cheating co-op speeds!
	if (bot->target->player)
	{
		// so you can keep up, set your speed to the player's normalspeed
		player->normalspeed = bot->target->player->normalspeed;
		player->thrustfactor = 5;
		player->accelstart = 50;
		player->acceleration = 50;
	}


	cmd->angleturn = 0; // Command pending...
	if (!player->climbing)
	{
		if ((mo->angle - ANGLE_10) - angle < angle - (mo->angle - ANGLE_10))
			cmd->angleturn = (short)(cmd->angleturn - (2560/NEWTICRATERATIO)); // Turn right!
		else if ((mo->angle + ANGLE_10) - angle > angle - (mo->angle + ANGLE_10))
			cmd->angleturn = (short)(cmd->angleturn + (2560/NEWTICRATERATIO)); // Turn left!
		else if (JB_AngleMove(player))
			aimed = true;
		mo->angle += (cmd->angleturn<<16); // Set the angle of your mobj
		cmd->angleturn = (short)(mo->angle>>16); // And set that to your turning. For some reason.
	}
	cmd->angleturn |= TICCMD_RECEIVED; // Treat the bot AI kinda like a multiplayer netgame

	// Forward movement.
	if (mo->momz > 0 && !jumping // If you're bouncing on a spring...
	   && bot->springmove) // And you're already moving in a direction from it...
		cmd->forwardmove = 0; // Do nothing. Moving could ruin it.
	else if (nextsector->special == 4 // If the next sector is HARMFUL to you...
			|| nextsector->special == 5
			|| nextsector->special == 7
			|| nextsector->special == 9
			|| nextsector->special == 11
			|| nextsector->special == 16
			|| nextsector->special == 18
			|| nextsector->special == 519
			|| nextsector->special == 978
			|| nextsector->special == 980
			|| nextsector->special == 983
			|| nextsector->special == 984)
		cmd->forwardmove = -50/NEWTICRATERATIO; // STOP RUNNING TWARDS IT! AGH!
	else if (!aimed) // If you're not aimed properly at something...
		cmd->forwardmove = 25/NEWTICRATERATIO; // Start slowing down.
	else if (dist < 256 // If you're closing in on something
			&& (bot->target->flags & MF_ENEMY // that needs popping...
				|| bot->target->flags & MF_BOSS
				|| bot->target->flags & MF_MONITOR))
		cmd->forwardmove = 25/NEWTICRATERATIO; // Start slowing down.
	else // Otherwise...
		cmd->forwardmove = 50/NEWTICRATERATIO; // Go full speed. Always.

	// Jumping stuff
	if (!botonground && !jumping) // In the air but not holding the jump button?
		cmd->buttons &= ~BT_JUMP; // Don't press it again, then.
	else if (nextsector->floorheight > mo->z // If the next sector is above you...
			&& nextsector->floorheight - mo->z < 128*FRACUNIT) // And you can jump up on it...
		cmd->buttons |= BT_JUMP; // Then jump!
	else if (bot->target->z > mo->z // If your target's still above you...
			&& jumping // And you're already holding the jump button...
			&& mo->momz > 0) // And you're still going up...
		cmd->buttons |= BT_JUMP; // Continue to do so!
	else if (bot->target->z > mo->z + mo->height // If it's above your head...
			&& !jumping) // And you're not jumping already...
		cmd->buttons |= BT_JUMP; // Then jump!
	else if ((bot->target->flags & MF_ENEMY // If the target
			 || bot->target->flags & MF_BOSS // NEEDS to be popped...
			 || bot->target->flags & MF_MONITOR)
			&& dist < 128 // And you're getting close to it...
			&& !jumping) // And you're not jumping already...
		cmd->buttons |= BT_JUMP; // Then jump!
	else // Otherwise...
		cmd->buttons &= ~BT_JUMP; // I guess you shouldn't be jumping, then...


}

static void JB_TimeRaceThink(int botnum)
{
#ifdef BOTWAYPOINTS
	player_t* player = &players[botnum];
	bot_t* bot = &bots[botnum];
	mobj_t* mo = players[botnum].mo;
	ticcmd_t* cmd = &players[botnum].cmd;

	fixed_t dist, dist2;
	angle_t angle;
	boolean aimed = false;
	boolean spring = false;

	// Bot info
	boolean jumping = (player->pflags & PF_JUMPDOWN);
	boolean botonground;

	if (!player->mo)
		I_Error("JB_TimeRaceThink: players[%d].mo == NULL", player - players);

	botonground = P_IsObjectOnGround(player->mo);

	// Targetting
	bot->target = JB_Look4StarPost(botnum);

	// Waypoint info
	if (bot->waypoint)
	{
		dist2 = P_AproxDistance(P_AproxDistance(
												bot->waypoint->x - player->mo->x,
												bot->waypoint->y - player->mo->y),
								bot->waypoint->z - player->mo->z) / FRACUNIT;
		angle = R_PointToAngle2(mo->x, mo->y, bot->waypoint->x, bot->waypoint->y); // You facing waypoint.
		if (bot->waypoint->springpoint && botonground)
		{
			mo = bot->target;
			bot->target = JB_Look4Spring(botnum);
			if (bot->target)
				spring = true;
			else
				bot->target = mo;
			mo = players[botnum].mo;
		}
	}
	else
	{
		dist2 = 0;
		angle = mo->angle;
	}

	// Target info
	if (bot->target)
	{
		dist = P_AproxDistance(P_AproxDistance(
											   bot->target->x - player->mo->x,
											   bot->target->y - player->mo->y),
							   bot->target->z - player->mo->z) / FRACUNIT;
		if (spring)
			angle = R_PointToAngle2(mo->x, mo->y, bot->target->x, bot->target->y); // You facing spring.
	}
	else
		dist = 0; // Just for safety...

	// Turning movement
	cmd->angleturn = 0; // Command pending...
	if (!player->climbing)
	{
		if ((mo->angle - ANGLE_10) - angle < angle - (mo->angle - ANGLE_10))
			cmd->angleturn = (short)(cmd->angleturn - (2560/NEWTICRATERATIO)); // Turn right!
		else if ((mo->angle + ANGLE_10) - angle > angle - (mo->angle + ANGLE_10))
			cmd->angleturn = (short)(cmd->angleturn + (2560/NEWTICRATERATIO)); // Turn left!
		else if (JB_AngleMove(player))
			aimed = true;
		mo->angle += (cmd->angleturn<<16); // Set the angle of your mobj
		cmd->angleturn = (short)(mo->angle>>16); // And set that to your turning. For some reason.
	}
	cmd->angleturn |= TICCMD_RECEIVED; // Treat the bot AI kinda like a multiplayer netgame

	// Forward movement
	if (!bot->waypoint // No waypoint?
	   || (mo->subsector->sector == bot->waypoint->sec // In same sector as waypoint
		   && dist2 < 128)) // And close enough where it should've switched you to the next waypoint already?
		cmd->forwardmove = 0; // Stop...
	else if (mo->momz > 0 && !jumping // If you're bouncing on a spring...
			&& bot->springmove) // And you're already moving in a direction from it...
		cmd->forwardmove = 0; // Do nothing. Moving could ruin it.
	else if (!aimed) // Not aimed properly?
		cmd->forwardmove = 25/NEWTICRATERATIO; // Slow down!
	else // Otherwise...
		cmd->forwardmove = 50/NEWTICRATERATIO; // Go full speed

	// Jumping stuff
	if (!botonground && !jumping) // In the air but not holding the jump button?
		cmd->buttons &= ~BT_JUMP; // Don't press it again, then.
	// waypoint jumping
	else if (bot->waypoint //&& aimed // Aimed at a waypoint?
			&& !bot->waypoint->springpoint // And it's not a spring waypoint
			//&& dist2 < 256 // And you're close to it...
			// And it's floor is above you
			&& (mo->z < bot->waypoint->z || mo->momz > 0))
		cmd->buttons |= BT_JUMP; // Then jump!
	// target jumping
	else if (!bot->target) // No target?
		cmd->buttons &= ~BT_JUMP; // No jump!
	else if (spring // If your target's a spring...
			&& bot->target->z > mo->z + MAXSTEPMOVE) // And it's higher then you can step...
		cmd->buttons |= BT_JUMP; // Then jump!
	else if ((bot->target->flags & MF_ENEMY // If the target
			 || bot->target->flags & MF_BOSS) // NEEDS to be popped...
			&& dist < 128 // And you're getting close to it...
			&& mo->momz >= 0) // And you're either not jumping or still going up...
		cmd->buttons |= BT_JUMP; // Then jump!
	else // Otherwise...
		cmd->buttons &= ~BT_JUMP; // I guess you shouldn't be jumping, then...
#else
	JB_CoopThink(botnum);
#endif
}

static void JB_RaceThink(int botnum)
{
	player_t* player = &players[botnum];
	bot_t* bot = &bots[botnum];
	mobj_t* mo = players[botnum].mo;
	ticcmd_t* cmd = &players[botnum].cmd;

	fixed_t dist;
	angle_t angle;
	boolean aimed;
	sector_t *nextsector;

	// Bot info
	boolean jumping = (player->pflags & PF_JUMPDOWN);
	boolean botonground;

	if (!player->mo)
		I_Error("JB_RaceThink: players[%d].mo == NULL", player - players);

	botonground = P_IsObjectOnGround(player->mo);

#if 0 // SRB2CBTODO: bot waypoint, then starposts
	// Targeting
	if (player->powers[pw_underwater] && player->powers[pw_underwater] < 15*TICRATE)
	{
		bot->target = JB_Look4AirBubble(botnum);
		if (!bot->target) // Uh oh... No air?! Try to jump as high as you can, then!
		{
			JB_Jump4AirBubble(botnum);
			return;
		}
	}
	else
#endif
		bot->target = JB_Look4StarPost(botnum);
	// Nothing left in the area to do? Move on, then.
	if (!bot->target)
	{
		JB_TimeRaceThink(botnum);
		return;
	}

	// Target info
	dist = P_AproxDistance(P_AproxDistance(
										   bot->target->x - player->mo->x,
										   bot->target->y - player->mo->y),
						   bot->target->z - player->mo->z) / FRACUNIT;
	angle = R_PointToAngle2(mo->x, mo->y, bot->target->x, bot->target->y); // You facing target.
	nextsector = R_PointInSubsector(mo->x + mo->momx*2, mo->y + mo->momy*2)->sector;

	// Turning movement
	aimed = false;
	cmd->angleturn = 0; // Command pending...
	if (!player->climbing)
	{
		if ((mo->angle - ANGLE_10) - angle < angle - (mo->angle - ANGLE_10))
			cmd->angleturn = (short)(cmd->angleturn - (2560/NEWTICRATERATIO)); // Turn right!
		else if ((mo->angle + ANGLE_10) - angle > angle - (mo->angle + ANGLE_10))
			cmd->angleturn = (short)(cmd->angleturn + (2560/NEWTICRATERATIO)); // Turn left!
		else if (JB_AngleMove(player))
			aimed = true;
		mo->angle += (cmd->angleturn<<16); // Set the angle of your mobj
		cmd->angleturn = (short)(mo->angle>>16); // And set that to your turning. For some reason.
	}
	cmd->angleturn |= TICCMD_RECEIVED; // Treat the bot AI kinda like a multiplayer netgame

	// Forward movement.
	if (mo->momz > 0 && !jumping // If you're bouncing on a spring...
	   && bot->springmove) // And you're already moving in a direction from it...
		cmd->forwardmove = 0; // Do nothing. Moving could ruin it.
	else if (nextsector->special == 4 // If the next sector is HARMFUL to you...
			|| nextsector->special == 5
			|| nextsector->special == 7
			|| nextsector->special == 9
			|| nextsector->special == 11
			|| nextsector->special == 16
			|| nextsector->special == 18
			|| nextsector->special == 519
			|| nextsector->special == 978
			|| nextsector->special == 980
			|| nextsector->special == 983
			|| nextsector->special == 984)
		cmd->forwardmove = -50/NEWTICRATERATIO; // STOP RUNNING TWARDS IT! AGH!
	else if (!aimed) // If you're not aimed properly at something...
		cmd->forwardmove = 25/NEWTICRATERATIO; // Start slowing down.
	else if (dist < 256 // If you're closing in on something
			&& (bot->target->flags & MF_ENEMY // that needs popping...
				|| bot->target->flags & MF_BOSS
				|| bot->target->flags & MF_MONITOR))
		cmd->forwardmove = 25/NEWTICRATERATIO; // Start slowing down.
	else // Otherwise...
		cmd->forwardmove = 50/NEWTICRATERATIO; // Go full speed. Always.

	// Jumping stuff
	if (!botonground && !jumping) // In the air but not holding the jump button?
		cmd->buttons &= ~BT_JUMP; // Don't press it again, then.
	else if (nextsector->floorheight > mo->z // If the next sector is above you...
			&& nextsector->floorheight - mo->z < 128*FRACUNIT) // And you can jump up on it...
		cmd->buttons |= BT_JUMP; // Then jump!
	else if (bot->target->z > mo->z // If your target's still above you...
			&& jumping // And you're already holding the jump button...
			&& mo->momz > 0) // And you're still going up...
		cmd->buttons |= BT_JUMP; // Continue to do so!
	else if (bot->target->z > mo->z + mo->height // If it's above your head...
			&& !jumping) // And you're not jumping already...
		cmd->buttons |= BT_JUMP; // Then jump!
	else if ((bot->target->flags & MF_ENEMY // If the target
			 || bot->target->flags & MF_BOSS // NEEDS to be popped...
			 || bot->target->flags & MF_MONITOR)
			&& dist < 128 // And you're getting close to it...
			&& !jumping) // And you're not jumping already...
		cmd->buttons |= BT_JUMP; // Then jump!
	else // Otherwise...
		cmd->buttons &= ~BT_JUMP; // I guess you shouldn't be jumping, then...
}

static void JB_MatchThink(int botnum)
{
	player_t* player = &players[botnum];
	bot_t* bot = &bots[botnum];
	mobj_t* mo = players[botnum].mo;
	ticcmd_t* cmd = &players[botnum].cmd;

	fixed_t dist;
	angle_t angle;
	boolean aimed, abilityjump;
	sector_t *nextsector;

	// Bot info
	boolean jumping = (player->pflags & PF_JUMPDOWN);
	boolean botonground;

	if (!player->mo)
		I_Error("JB_MatchThink: players[%d].mo == NULL", player - players);

	botonground = P_IsObjectOnGround(player->mo);

	// Targeting
	// SRB2CBTODO: this is where the bots can look stupid, change targets if you're not moving
	if (!bot->target || !bot->target->player || mo->health <= 5)
	{
		bot->target = NULL;
		if (player->powers[pw_underwater] && player->powers[pw_underwater] < 15*TICRATE)
		{
			bot->target = JB_Look4AirBubble(botnum);
			if (!bot->target) // Uh oh... No air?! Try to jump as high as you can, then!
			{
				JB_Jump4AirBubble(botnum);
				return;
			}
		}
		else if (mo->health <= 10)
			bot->target = JB_Look4Collect(botnum);
		else
		{
			bot->target = JB_Look4Players(botnum);
			if (bot->target && bot->target->state == &states[S_PLAY_PLG1])
				bot->target = JB_Look4Spring(botnum);
			else if (!bot->target)
				bot->target = JB_Look4Collect(botnum);
		}
	}

	// No target?
	if (!bot->target)
	{
		JB_BotWander(botnum);
		return;
	}

	// Target info
	dist = P_AproxDistance(P_AproxDistance(
										   bot->target->x - player->mo->x,
										   bot->target->y - player->mo->y),
						   bot->target->z - player->mo->z) / FRACUNIT;
	angle = R_PointToAngle2(mo->x, mo->y, bot->target->x, bot->target->y); // You facing target.
	nextsector = R_PointInSubsector(mo->x + mo->momx*2, mo->y + mo->momy*2)->sector;

	// Turning movement
	aimed = false;
	cmd->angleturn = 0; // Command pending...
	if (!player->climbing)
	{
		if ((mo->angle - ANGLE_10) - angle < angle - (mo->angle - ANGLE_10))
			cmd->angleturn = (short)(cmd->angleturn - (2560/NEWTICRATERATIO)); // Turn right!
		else if ((mo->angle + ANGLE_10) - angle > angle - (mo->angle + ANGLE_10))
			cmd->angleturn = (short)(cmd->angleturn + (2560/NEWTICRATERATIO)); // Turn left!
		else if (JB_AngleMove(player))
			aimed = true;
		mo->angle += (cmd->angleturn<<16); // Set the angle of your mobj
		cmd->angleturn = (short)(mo->angle>>16); // And set that to your turning. For some reason.
	}
	cmd->angleturn |= TICCMD_RECEIVED; // Treat the bot AI kinda like a multiplayer netgame

	// this makes bots seem much less stupid
	if (bot->player->mo->momy < 3 && cmd->forwardmove)
	{
		bot->player->mo->angle += ANG180;
		if (botonground)
			cmd->buttons |= BT_USE;
		else
			cmd->buttons &= ~BT_USE;
	}

	// helps with getting stuck
	if (bot->player->mo->momx < 1 && cmd->sidemove > 3)
		cmd->angleturn += (640/NEWTICRATERATIO)<<FRACBITS;


	// Ability stuff
	abilityjump = false;
	switch(player->charability)
	{
		case 0: // Thok

			if (bot->target->flags & MF_SPRING // No thok over spring!
			   || bot->target->type == MT_EXTRALARGEBUBBLE)
				break;
			// Thok!
			else if (aimed && !botonground && !jumping && mo->momz <= 0)
				cmd->buttons |= BT_JUMP;
			// Jump to full height!
			else if ((P_InQuicksand(player->mo)) || (((!jumping && botonground) || (jumping && mo->momz > 0)) && leveltime % 13 == 0))
				cmd->buttons |= BT_JUMP;
			// Ready the jump button!
			else
				cmd->buttons &= ~BT_JUMP;
			// Ability is controlling jump button! MWAHAHAHA!
			abilityjump = true;
			break;

		case 1: // Fly
		case 7: // Swim
			if (bot->target->flags & MF_SPRING // No fly over spring!
			   || (player->charability == CA_SWIM // No swim out of water!
				   && !(player->mo->eflags & MFE_UNDERWATER))
			   || mo->health < 10) // No snipe without ammo!
				break;
			// Fly!
			else if (!botonground && !jumping && mo->momz <= 0)
				cmd->buttons |= BT_JUMP;
			// Jump to full height!
			else if ((P_InQuicksand(player->mo)) || bot->target->z > mo->z
					 || (((!jumping && botonground) || (jumping && mo->momz > 0)) && leveltime % 13 == 0))
				cmd->buttons |= BT_JUMP;
			// Ready the jump button!
			else
				cmd->buttons &= ~BT_JUMP;
			// Ability is controlling jump button! MWAHAHAHA!
			abilityjump = true;
			break;

		case 2: // Glide and climb
		case 3: // Glide with no climb
			if (player->climbing && (player->mo->z < player->mo->ceilingz - P_GetPlayerHeight(player) - 2*FRACUNIT)) // Don't get stuck on walls
				cmd->buttons |= BT_USE;

			if (bot->target->z > mo->z // Target still above you
			   && !botonground && !jumping // You're in the air but not holding the jump button
			   && mo->momz <= 0) // You aren't gonna get high enough
			{ // So what do you do? Glide!... I dunno.
				cmd->buttons |= BT_JUMP;
				abilityjump = true;
			}
			break;

		case 4: // Double-Jump
			// Jump again at top of jump height!
			if (!botonground && !jumping && mo->momz <= 0)
				cmd->buttons |= BT_JUMP;
			break;

		default:
			break;
	}

	// Forward movement.
	if (mo->momz > 0 && !jumping // If you're bouncing on a spring...
	   && bot->springmove) // And you're already moving in a direction from it...
		cmd->forwardmove = 0; // Do nothing. Moving could ruin it.
	else if (nextsector->special == 4 // If the next sector is HARMFUL to you...
			|| nextsector->special == 5
			|| nextsector->special == 7
			|| nextsector->special == 9
			|| nextsector->special == 11
			|| nextsector->special == 16
			|| nextsector->special == 18
			|| nextsector->special == 519
			|| nextsector->special == 978
			|| nextsector->special == 980
			|| nextsector->special == 983
			|| nextsector->special == 984)
		cmd->forwardmove = -50/NEWTICRATERATIO; // STOP RUNNING TWARDS IT! AGH!
	else if (!aimed && !bot->target->player) // If you're not aimed properly at something that isn't a person...
		cmd->forwardmove = 25/NEWTICRATERATIO; // Start slowing down.
	else // Otherwise...
		cmd->forwardmove = 50/NEWTICRATERATIO; // Go full speed. Always.

	// Jumping stuff
	if (abilityjump) // Ability has changed the state of your jump button already?
	{} // Then don't mess with it!
	else if (!botonground && !jumping) // In the air but not holding the jump button?
		cmd->buttons &= ~BT_JUMP; // Don't press it again, then.
	else if (nextsector->floorheight > mo->z // If the next sector is above you...
			&& nextsector->floorheight - mo->z < 128*FRACUNIT) // And you can jump up on it...
		cmd->buttons |= BT_JUMP; // Then jump!
	else if (bot->target->z > mo->z // If your target's still above you...
			&& jumping // And you're already holding the jump button...
			&& mo->momz > 0) // And you're jumping and still going up...
		cmd->buttons |= BT_JUMP; // Continue to do so!
	else if (bot->target->z > mo->z + mo->height // If your target is above your head...
			&& !jumping // And you're not jumping already...
			&& bot->target->state != &states[S_PLAY_PLG1]) // And they didn't just fly off a spring...
		cmd->buttons |= BT_JUMP; // Then jump!
	else if ((bot->target->flags & MF_ENEMY // If the target
			 || bot->target->flags & MF_BOSS // NEEDS to be popped...
			 || bot->target->flags & MF_MONITOR)
			&& dist < 128 // And you're getting close to it...
			&& !jumping) // And you're not jumping already...
		cmd->buttons |= BT_JUMP; // Then jump!
	else // Otherwise...
		cmd->buttons &= ~BT_JUMP; // I guess you shouldn't be jumping, then...

	// Weapon stuff

	/*
	cmd->buttons &= ~BT_WEAPONMASK;
	cmd->buttons |= 1; // Normal Ring
	cmd->buttons |= 2; // Automatic Ring
	cmd->buttons |= 3; // Bounce Ring
	cmd->buttons |= 4; // Scatter Ring
	cmd->buttons |= 5; // Grenade Ring
	cmd->buttons |= 6; // Explosion Ring
	cmd->buttons |= 7; // Rail Ring

	if (player->ringweapons & RW_BOUNCE)
	{
		cmd->buttons &= ~BT_WEAPONMASK;
		cmd->buttons |= 3;
	}*/

	// Shooting stuff
	if (cmd->buttons & BT_ATTACK) // If you're holding the button down...
		cmd->buttons &= ~BT_ATTACK; // DO NOT HOLD THE BUTTON DOWN!
	else if (
			// Kalaron: Well you can't really DO anything in spectate mode, so just enter the game
			(bot->player->spectator)
			|| (aimed // If you're properly aimed...
			&& bot->target->player // At a player...
			&& !bot->target->player->spectator // Don't target spectators
			&& mo->health > 2 // And you have at least one ring to spare...
			&& !bot->target->player->powers[pw_flashing] // double check that the player can be hit
			&& !bot->target->player->powers[pw_invulnerability]
			&& !bot->target->player->powers[pw_bombshield] // don't you dare hit them if they have a bomb shield
			// bots shouldn't blind spam you with rings, even if you're targeted,
			// don't fire every second, makes bots feel more player-like
			// fire in scattered intervals
			&& (
				((leveltime/TICRATE % 7 == 0) && cv_botai.value == 0) // Very Easy
				|| ((leveltime/TICRATE % 3 == 0 || leveltime/TICRATE % 10 == 0) && cv_botai.value == 1) // Normal
				|| ((leveltime/TICRATE % 2 == 0) && cv_botai.value == 2) // Hard
				|| ((leveltime/TICRATE % 1 == 0) && cv_botai.value == 3) // Ruthless
				)
			))
		cmd->buttons |= BT_ATTACK; // Conditions are met - fire away!
	// PTR_AimTraverse
}


static void JB_CTFThink(int botnum) // SRB2CBTODO: Guard the flag on your team
{
	player_t* player = &players[botnum];
	bot_t* bot = &bots[botnum];
	mobj_t* mo = players[botnum].mo;
	ticcmd_t* cmd = &players[botnum].cmd;

	fixed_t dist;
	angle_t angle;
	boolean aimed, abilityjump;
	sector_t *nextsector;

	// Bot info
	boolean jumping = (player->pflags & PF_JUMPDOWN);
	boolean botonground;

	if (!player->mo)
		I_Error("JB_CTFThink: players[%d].mo == NULL", player - players);

	botonground = P_IsObjectOnGround(player->mo);

	// Targeting
	// SRB2CBTODO: this is where the bots can look stupid, change targets if you're not moving
	if (!bot->target || !bot->target->player || mo->health <= 5)
	{
		bot->target = NULL;
		if (player->powers[pw_underwater] && player->powers[pw_underwater] < 15*TICRATE)
		{
			bot->target = JB_Look4AirBubble(botnum);
			if (!bot->target) // Uh oh... No air?! Try to jump as high as you can, then!
			{
				JB_Jump4AirBubble(botnum);
				return;
			}
		}
		else if (mo->health <= 10)
			bot->target = JB_Look4Collect(botnum);
		else
		{
			bot->target = JB_Look4Players(botnum);
			if (bot->target && bot->target->state == &states[S_PLAY_PLG1])
				bot->target = JB_Look4Spring(botnum);
			else if (!bot->target)
				bot->target = JB_Look4Collect(botnum);
		}
	}

	// No target?
	if (!bot->target)
	{
		JB_BotWander(botnum);
		return;
	}

	// Target info
	dist = P_AproxDistance(P_AproxDistance(
										   bot->target->x - player->mo->x,
										   bot->target->y - player->mo->y),
						   bot->target->z - player->mo->z) / FRACUNIT;
	angle = R_PointToAngle2(mo->x, mo->y, bot->target->x, bot->target->y); // You facing target.
	nextsector = R_PointInSubsector(mo->x + mo->momx*2, mo->y + mo->momy*2)->sector;

	// Turning movement
	aimed = false;
	cmd->angleturn = 0; // Command pending...
	if (!player->climbing)
	{
		if ((mo->angle - ANGLE_10) - angle < angle - (mo->angle - ANGLE_10))
			cmd->angleturn = (short)(cmd->angleturn - (2560/NEWTICRATERATIO)); // Turn right!
		else if ((mo->angle + ANGLE_10) - angle > angle - (mo->angle + ANGLE_10))
			cmd->angleturn = (short)(cmd->angleturn + (2560/NEWTICRATERATIO)); // Turn left!
		else if (JB_AngleMove(player))
			aimed = true;
		mo->angle += (cmd->angleturn<<16); // Set the angle of your mobj
		cmd->angleturn = (short)(mo->angle>>16); // And set that to your turning. For some reason.
	}
	cmd->angleturn |= TICCMD_RECEIVED; // Treat the bot AI kinda like a multiplayer netgame

	// this makes bots seem much less stupid
	if (bot->player->mo->momy < 3 && cmd->forwardmove)
	{
		bot->player->mo->angle += ANG180;
		if (botonground)
			cmd->buttons |= BT_USE;
		else
			cmd->buttons &= ~BT_USE;
	}

	//if (bot->player->mo->momx < 1 && cmd->sidemove > 3)
	//			cmd->angleturn += (640/NEWTICRATERATIO)<<FRACBITS;


	// Ability stuff
	abilityjump = false;
	switch(player->charability)
	{
		case 0: // Thok

			if (bot->target->flags & MF_SPRING // No thok over spring!
			   || bot->target->type == MT_EXTRALARGEBUBBLE)
				break;
			// Thok!
			else if (aimed && !botonground && !jumping && mo->momz <= 0)
				cmd->buttons |= BT_JUMP;
			// Jump to full height!
			else if ((!jumping && botonground) || (jumping && mo->momz > 0))
				cmd->buttons |= BT_JUMP;
			// Ready the jump button!
			else
				cmd->buttons &= ~BT_JUMP;
			// Ability is controlling jump button! MWAHAHAHA!
			abilityjump = true;
			break;

		case 1: // Fly
		case 7: // Swim
			if (bot->target->flags & MF_SPRING // No fly over spring!
			   || (player->charability == CA_SWIM // No swim out of water!
				   && !(player->mo->eflags & MFE_UNDERWATER))
			   || mo->health < 10) // No snipe without ammo!
				break;
			// Fly!
			else if (!botonground && !jumping && mo->momz <= 0)
				cmd->buttons |= BT_JUMP;
			// Jump to full height!
			else if ((!jumping && botonground) || (jumping && mo->momz > 0))
				cmd->buttons |= BT_JUMP;
			// Ready the jump button!
			else
				cmd->buttons &= ~BT_JUMP;
			// Ability is controlling jump button! MWAHAHAHA!
			abilityjump = true;
			break;

		case 2: // Glide and climb
		case 3: // Glide with no climb
			if (player->climbing &&
				(player->mo->z < player->mo->ceilingz - P_GetPlayerHeight(player) - 2*FRACUNIT)) // Don't get stuck on walls
				cmd->buttons |= BT_USE;

			if (bot->target->z > mo->z // Target still above you
			   && !botonground && !jumping // You're in the air but not holding the jump button
			   && mo->momz <= 0) // You aren't gonna get high enough
			{ // So what do you do? Glide!... I dunno.
				cmd->buttons |= BT_JUMP;
				abilityjump = true;
			}
			break;

		case 4: // Double-Jump
			// Jump again at top of jump height!
			if (!botonground && !jumping && mo->momz <= 0)
				cmd->buttons |= BT_JUMP;
			break;

		default:
			break;
	}

	// Forward movement.
	if (mo->momz > 0 && !jumping // If you're bouncing on a spring...
	   && bot->springmove) // And you're already moving in a direction from it...
		cmd->forwardmove = 0; // Do nothing. Moving could ruin it.
	else if (nextsector->special == 4 // If the next sector is HARMFUL to you...
			|| nextsector->special == 5
			|| nextsector->special == 7
			|| nextsector->special == 9
			|| nextsector->special == 11
			|| nextsector->special == 16
			|| nextsector->special == 18
			|| nextsector->special == 519
			|| nextsector->special == 978
			|| nextsector->special == 980
			|| nextsector->special == 983
			|| nextsector->special == 984)
		cmd->forwardmove = -50/NEWTICRATERATIO; // STOP RUNNING TWARDS IT! AGH!
	else if (!aimed && !bot->target->player) // If you're not aimed properly at something that isn't a person...
		cmd->forwardmove = 25/NEWTICRATERATIO; // Start slowing down.
	else // Otherwise...
		cmd->forwardmove = 50/NEWTICRATERATIO; // Go full speed. Always.

	// Jumping stuff
	if (abilityjump) // Ability has changed the state of your jump button already?
	{} // Then don't mess with it!
	else if (!botonground && !jumping) // In the air but not holding the jump button?
		cmd->buttons &= ~BT_JUMP; // Don't press it again, then.
	else if (nextsector->floorheight > mo->z // If the next sector is above you...
			&& nextsector->floorheight - mo->z < 128*FRACUNIT) // And you can jump up on it...
		cmd->buttons |= BT_JUMP; // Then jump!
	else if (bot->target->z > mo->z // If your target's still above you...
			&& jumping // And you're already holding the jump button...
			&& mo->momz > 0) // And you're jumping and still going up...
		cmd->buttons |= BT_JUMP; // Continue to do so!
	else if (bot->target->z > mo->z + mo->height // If your target is above your head...
			&& !jumping // And you're not jumping already...
			&& bot->target->state != &states[S_PLAY_PLG1]) // And they didn't just fly off a spring...
		cmd->buttons |= BT_JUMP; // Then jump!
	else if ((bot->target->flags & MF_ENEMY // If the target
			 || bot->target->flags & MF_BOSS // NEEDS to be popped...
			 || bot->target->flags & MF_MONITOR)
			&& dist < 128 // And you're getting close to it...
			&& !jumping) // And you're not jumping already...
		cmd->buttons |= BT_JUMP; // Then jump!
	else // Otherwise...
		cmd->buttons &= ~BT_JUMP; // I guess you shouldn't be jumping, then...

	// Shooting stuff
	if (cmd->buttons & BT_ATTACK) // If you're holding the button down...
		cmd->buttons &= ~BT_ATTACK; // DO NOT HOLD THE BUTTON DOWN!
	else if (
			 // Kalaron: Well you can't really DO anything in spectate mode, so just enter the game
			 (bot->player->spectator) ||
			 (aimed // If you're properly aimed...
			&& bot->target->player // At a player...
			&& mo->health > 2 // And you have at least one ring to spare...
			&& !bot->target->player->powers[pw_flashing] // double check that the player can be hit
			&& !bot->target->player->powers[pw_invulnerability]
			&& !bot->target->player->powers[pw_bombshield] // don't you dare hit them if they have a bomb shield
			// bots shouldn't blind spam you with rings, even if you're targeted, don't fire every second, makes bots feel more player-like
			// fire in scattered intervals
			&& (leveltime/TICRATE % 4 == 0 || leveltime/TICRATE % 11 == 0))
			)
		cmd->buttons |= BT_ATTACK; // Conditions are met - fire away!
	// PTR_AimTraverse
}

static inline void JB_TagThink(int botnum)
{
	player_t* player = &players[botnum];
	bot_t* bot = &bots[botnum];
	mobj_t* mo = players[botnum].mo;
	ticcmd_t* cmd = &players[botnum].cmd;

	angle_t angle;
	boolean aimed;
	sector_t *nextsector;

	// Bot info
	boolean jumping = (player->pflags & PF_JUMPDOWN);
	boolean botonground;

	if (!player->mo)
		I_Error("JB_TagThink: players[%d].mo == NULL", player - players);

	botonground = P_IsObjectOnGround(player->mo);

	// Targeting
	if (player->powers[pw_underwater] && player->powers[pw_underwater] < 15*TICRATE)
	{
		bot->target = JB_Look4AirBubble(botnum);
		if (!bot->target) // Uh oh... No air?! Try to jump as high as you can, then!
		{
			JB_Jump4AirBubble(botnum);
			return;
		}
	}
	else if (mo->health <= 5)
		bot->target = JB_Look4Collect(botnum);
	else
	{
		bot->target = JB_Look4Players(botnum);
		if (!bot->target)
			bot->target = JB_Look4Collect(botnum);
	}

	// No target?
	if (!bot->target)
	{
		JB_BotWander(botnum);
		return;
	}

	// Target info
	angle = R_PointToAngle2(mo->x, mo->y, bot->target->x, bot->target->y); // You facing target.
	nextsector = R_PointInSubsector(mo->x + mo->momx*2, mo->y + mo->momy*2)->sector;

	// Turning movement
	aimed = false;
	cmd->angleturn = 0; // Command pending...
	if (!player->climbing)
	{
		if ((mo->angle - ANGLE_10) - angle < angle - (mo->angle - ANGLE_10))
			cmd->angleturn = (short)(cmd->angleturn - (2560/NEWTICRATERATIO)); // Turn right!
		else if ((mo->angle + ANGLE_10) - angle > angle - (mo->angle + ANGLE_10))
			cmd->angleturn = (short)(cmd->angleturn + (2560/NEWTICRATERATIO)); // Turn left!
		else if (JB_AngleMove(player))
			aimed = true;
		mo->angle += (cmd->angleturn<<16); // Set the angle of your mobj
		cmd->angleturn = (short)(mo->angle>>16); // And set that to your turning. For some reason.
	}
	cmd->angleturn |= TICCMD_RECEIVED; // Treat the bot AI kinda like a multiplayer netgame

	// Forward movement.
	if (mo->momz > 0 && !jumping // If you're bouncing on a spring...
	   && bot->springmove) // And you're already moving in a direction from it...
		cmd->forwardmove = 0; // Do nothing. Moving could ruin it.
	else if (nextsector->special == 4 // If the next sector is HARMFUL to you...
			|| nextsector->special == 5
			|| nextsector->special == 7
			|| nextsector->special == 9
			|| nextsector->special == 11
			|| nextsector->special == 16
			|| nextsector->special == 18
			|| nextsector->special == 519
			|| nextsector->special == 978
			|| nextsector->special == 980
			|| nextsector->special == 983
			|| nextsector->special == 984)
		cmd->forwardmove = -50/NEWTICRATERATIO; // STOP RUNNING TWARDS IT! AGH!
	else if (bot->target->player) // If it's a player chasing you...
		cmd->forwardmove = -50/NEWTICRATERATIO; // Go backwards!
	else if (!aimed) // If you're not aimed properly at something that isn't a person...
		cmd->forwardmove = 25/NEWTICRATERATIO; // Start slowing down.
	else // Otherwise...
		cmd->forwardmove = 50/NEWTICRATERATIO; // Go full speed. Always.

	// Jumping stuff
	if (!botonground && !jumping) // In the air but not holding the jump button?
		cmd->buttons &= ~BT_JUMP; // Don't press it again, then.
	else if (nextsector->floorheight > mo->z // If the next sector is above you...
			&& nextsector->floorheight - mo->z < 128*FRACUNIT) // And you can jump up on it...
		cmd->buttons |= BT_JUMP; // Then do so!
	else if (jumping // If you're already holding the jump button...
			&& mo->momz > 0) // And you're still going up...
		cmd->buttons |= BT_JUMP; // Continue to do so!
	else if (bot->target->player // If your target's a player
			&& bot->target->z <= mo->z + mo->height // And it's below your head...
			&& !jumping) // And you're not jumping already...
		cmd->buttons |= BT_JUMP; // Then jump!
	else // Otherwise...
		cmd->buttons &= ~BT_JUMP; // I guess you shouldn't be jumping, then...
}

static inline void JB_ChaosThink(int botnum)
{
	player_t* player = &players[botnum];
	bot_t* bot = &bots[botnum];
	mobj_t* mo = players[botnum].mo;
	ticcmd_t* cmd = &players[botnum].cmd;

	fixed_t dist;
	angle_t angle;
	boolean aimed;
	sector_t *nextsector;

	// Bot info
	boolean jumping = (player->pflags & PF_JUMPDOWN);
	boolean botonground;

	if (!player->mo)
		I_Error("JB_ChaosThink: players[%d].mo == NULL", player - players);

	botonground = P_IsObjectOnGround(player->mo);

	// Targeting
	if (player->powers[pw_underwater] && player->powers[pw_underwater] < 15*TICRATE)
	{
		bot->target = JB_Look4AirBubble(botnum);
		if (!bot->target) // Uh oh... No air?! Try to jump as high as you can, then!
		{
			JB_Jump4AirBubble(botnum);
			return;
		}
	}
	else
		bot->target = JB_Look4Poppable(botnum);

	// Nothing left in the area to do? Move on, then.
	if (!bot->target)
	{
		bot->target = JB_Look4Poppable(botnum); // Check again
		if (!bot->target) // Still no target
		{
			JB_BotWander(botnum); // Wander
			return;
		}
	}

	// Target info
	dist = P_AproxDistance(P_AproxDistance(
										   bot->target->x - player->mo->x,
										   bot->target->y - player->mo->y),
						   bot->target->z - player->mo->z) / FRACUNIT;
	angle = R_PointToAngle2(mo->x, mo->y, bot->target->x, bot->target->y); // You facing target.
	nextsector = R_PointInSubsector(mo->x + mo->momx*2, mo->y + mo->momy*2)->sector;

	// Turning movement
	aimed = false;


	cmd->angleturn = 0; // Command pending...
	if (!player->climbing)
	{
		if ((mo->angle - ANGLE_10) - angle < angle - (mo->angle - ANGLE_10))
			cmd->angleturn = (short)(cmd->angleturn - (2560/NEWTICRATERATIO)); // Turn right!
		else if ((mo->angle + ANGLE_10) - angle > angle - (mo->angle + ANGLE_10))
			cmd->angleturn = (short)(cmd->angleturn + (2560/NEWTICRATERATIO)); // Turn left!
		else if (JB_AngleMove(player))
			aimed = true;
		mo->angle += (cmd->angleturn<<16); // Set the angle of your mobj
		cmd->angleturn = (short)(mo->angle>>16); // And set that to your turning. For some reason.
	}
	cmd->angleturn |= TICCMD_RECEIVED; // Treat the bot AI kinda like a multiplayer netgame

	// Forward movement.
	if (mo->momz > 0 && !jumping // If you're bouncing on a spring...
	   && bot->springmove) // And you're already moving in a direction from it...
		cmd->forwardmove = 0; // Do nothing. Moving could ruin it.
	else if (nextsector->special == 4 // If the next sector is HARMFUL to you...
			|| nextsector->special == 5
			|| nextsector->special == 7
			|| nextsector->special == 9
			|| nextsector->special == 11
			|| nextsector->special == 16
			|| nextsector->special == 18
			|| nextsector->special == 519
			|| nextsector->special == 978
			|| nextsector->special == 980
			|| nextsector->special == 983
			|| nextsector->special == 984)
		cmd->forwardmove = -50/NEWTICRATERATIO; // STOP RUNNING TWARDS IT! AGH!
	else if (!aimed) // If you're not aimed properly at something...
		cmd->forwardmove = 25/NEWTICRATERATIO; // Start slowing down.
	else if (dist < 256 // If you're closing in on something
			&& (bot->target->flags & MF_ENEMY // that needs popping...
				|| bot->target->flags & MF_BOSS
				|| bot->target->flags & MF_MONITOR))
		cmd->forwardmove = 25/NEWTICRATERATIO; // Start slowing down.
	else // Otherwise...
		cmd->forwardmove = 50/NEWTICRATERATIO; // Go full speed. Always.

	// Jumping stuff
	if (!botonground && !jumping) // In the air but not holding the jump button?
		cmd->buttons &= ~BT_JUMP; // Don't press it again, then.
	else if (nextsector->floorheight > mo->z // If the next sector is above you...
			&& nextsector->floorheight - mo->z < 128*FRACUNIT) // And you can jump up on it...
		cmd->buttons |= BT_JUMP; // Then jump!
	else if (bot->target->z > mo->z // If your target's still above you...
			&& jumping // And you're already holding the jump button...
			&& mo->momz > 0) // And you're still going up...
		cmd->buttons |= BT_JUMP; // Continue to do so!
	else if (bot->target->z > mo->z + mo->height // If it's above your head...
			&& !jumping) // And you're not jumping already...
		cmd->buttons |= BT_JUMP; // Then jump!
	else if ((bot->target->flags & MF_ENEMY // If the target
			 || bot->target->flags & MF_BOSS // NEEDS to be popped...
			 || bot->target->flags & MF_MONITOR)
			&& dist < 128 // And you're getting close to it...
			&& !jumping) // And you're not jumping already...
		cmd->buttons |= BT_JUMP; // Then jump!
	else // Otherwise...
		cmd->buttons &= ~BT_JUMP; // I guess you shouldn't be jumping, then...


}

void JB_BotThink(int botnum)
{
	bot_t* bot = &bots[botnum];
	player_t* player = &players[botnum];
	ticcmd_t* cmd = &player->cmd;
	cmd->angleturn = TICCMD_RECEIVED; // Treat the bot AI kinda like a multiplayer netgame


	int ownernum = bot->ownernum;
	playerstate_t botownerstate;
	botownerstate = players[ownernum].playerstate;

	if (!player->mo)
		I_Error("JB_BotThink: players[%d].mo == NULL", player - players);


    // Dead player state not handled in this function

	// if you don't have an owner, make sure to check that
	// make sure you don't think move if you don't need to in objectplace or timefreeze
	if (!cv_objectplace.value)
	{
		mobj_t* lasttarget = bot->target;

		JB_UpdateLook(&bots[botnum]);

		switch(gametype)
		{
			case GT_COOP:
				JB_SmartCoopThink(botnum);
				break;

			case GT_RACE:
				if (cv_racetype.value == 1 || circuitmap)
					JB_TimeRaceThink(botnum);
				else
					JB_RaceThink(botnum);
				break;

			case GT_MATCH:
				JB_MatchThink(botnum);
				break;

			case GT_TAG:
				if (player->pflags & PF_TAGIT)
					JB_MatchThink(botnum);
				else
					JB_TagThink(botnum);
				break;

#ifdef CHAOSISNOTDEADYET
			case GT_CHAOS:
				JB_ChaosThink(botnum);
				break;
#endif
			case GT_CTF:
				JB_CTFThink(botnum);
				break;

			default:
				JB_MatchThink(botnum);
				break;
		}
		if (bot->target != lasttarget)
			bot->targettimer = 0;
	}
}

#endif





#ifdef JTEBOTS
static int botline[MAXPLAYERS];
static int botlinesize = 0;


void JB_BotWaitAdd(int skin)
{
	botline[botlinesize] = skin;
	botlinesize++;
}

void JB_AddWaitingBots(int playernum) // SRB2CBTODO: May cause crash?
{
	if (netgame)
		botlinesize = 0;
	while (botlinesize > 0)
	{
		botlinesize--;
		JB_BotAdd((byte)botline[botlinesize], playernum,
				  (byte)atoi(skins[botline[botlinesize]].prefcolor),
				  skins[botline[botlinesize]].name);
	}
}




void JB_SpawnBot(int botnum) // Used in P_SpawnPlayer and P_SpawnStarpostPlayer
{
	bot_t* bot;

	player_t* player;
	int ownernum;

	if (!players[botnum].mo)
		I_Error("JB_SpawnBot: bot->player->mo == NULL");

	// This is G_DoBotRespawn, pretty much. :P
	player = &players[botnum];
	ownernum = bots[botnum].ownernum;

	// Clear the struct...
	memset(&bots[botnum],0,sizeof(bot_t));

	// And set the variables you saved...
	bot = &bots[botnum];
	bot->player = player;
	bot->ownernum = (byte)ownernum;

	// Set skin and color, dun matter what gametype.
	// No clue why SetPlayerSkinByNum needs to be
	// called here, but it does. Or else.
	SetPlayerSkinByNum(botnum, player->skin);

	player->mo->flags |= MF_TRANSLATION;
	player->mo->color = player->skincolor;

	if (!players[botnum].mo)
		I_Error("JB_SpawnBot: bot->player->mo == NULL");
}


//
// P_BotThink - A special version of P_Playerthink just for bots
//

void P_BotThink(int botnum)
{
	//bot_t* bot = &bots[botnum]; SRB2CBTODO: is this needed?
	player_t* player = &players[botnum];
	ticcmd_t* cmd = &players[botnum].cmd;

	// This should always be checked
	if (!player->mo)
		I_Error("P_BotThink: players[%d].mo == NULL", player - players);

	if (!player->bot)
		I_Error("P_BotThink not used on a bot!");

	// Possible zombie fixes.
	// todo: Figure out what is actually causing these problems in the first place...
	if ((player->health <= 0 || player->mo->health <= 0) && player->playerstate == PST_LIVE) // you should be DEAD!
		player->playerstate = PST_DEAD;


	if (player->pflags & PF_GLIDING)
	{
		if (player->mo->state - states < S_PLAY_ABL1 || player->mo->state - states > S_PLAY_ABL2)
			P_SetPlayerMobjState(player->mo, S_PLAY_ABL1);
	}
	else if ((player->pflags & PF_JUMPED) && !player->powers[pw_super]
			 && (player->mo->state - states < S_PLAY_ATK1 || player->mo->state - states > S_PLAY_ATK4
				 ) && player->charability2 == CA2_SPINDASH)
	{
		P_SetPlayerMobjState(player->mo, S_PLAY_ATK1);
	}

	if (player->bonuscount)
		player->bonuscount--;

	if (player->awayviewtics)
		player->awayviewtics--;

	// Bots don't noclip

	cmd = &player->cmd;

#ifdef PARANOIA
	if (player->playerstate == PST_REBORN)
		I_Error("Bot %d is in PST_REBORN\n", botnum);
#endif

	if (gametype == GT_RACE)
	{
		int i;

		// Check if all the players in the race have finished. If so, end the level.
		for (i = 0; i < MAXPLAYERS; i++)
		{
			if (playeringame[i])
			{
				if (!players[i].exiting && players[i].lives > 0)
					break;
			}
		}

		if (i == MAXPLAYERS && player->exiting == 3*TICRATE) // finished
			player->exiting = (14*TICRATE)/5 + 1;

		// If 10 seconds are left on the timer,
		// begin the drown music for countdown!
		if (countdown == 11*TICRATE - 1)
		{
			if (P_IsLocalPlayer(player))
				S_ChangeMusic(mus_drown, false);
		}

		// If you've hit the countdown and you haven't made
		//  it to the exit, you're a goner!
		else if (countdown == 1 && !player->exiting && player->lives > 0)
		{
			if (netgame && player->health > 0)
				CONS_Printf(text[OUT_OF_TIME], player_names[player-players]);

			player->pflags |= PF_TIMEOVER;

			if (player->pflags & PF_NIGHTSMODE)
			{
				P_DeNightserizePlayer(player);
				S_StartScreamSound(player->mo, sfx_lose);
			}

			player->lives = 1; // Starts the game over music
			P_DamageMobj(player->mo, NULL, NULL, 10000);
			player->lives = 0;

			if (player->playerstate == PST_DEAD)
				return;
		}
	}

	// If it is set, start subtracting
	if (player->exiting && player->exiting < 3*TICRATE)
		player->exiting--;

	if (player->exiting && countdown2)
		player->exiting = 5;

	// check water content, set stuff in mobj
	P_MobjCheckWater(player->mo);

	player->onconveyor = 0;
	// check special sectors : damage & secrets

	if (!player->spectator || (gametype == GT_CTF && player->spectator))
		P_PlayerInSpecialSector(player);

	if (player->playerstate == PST_DEAD)
	{
		player->mo->flags2 &= ~MF2_TRANSLUCENT;
		P_DeathThink(player);

		return;
	}

	// Make sure spectators always have a score and ring count of 0.
	if (player->spectator)
	{
		player->score = 0;
		player->mo->health = 1;
		player->health = 1;
	}

	if (gametype == GT_RACE)
	{
		if (player->lives <= 0)
			player->lives = 3;
	}
	else if (gametype == GT_COOP && player->lives <= 1) // Bots don't get gameovers
	{
		player->lives = 2;
	}

	if (gametype == GT_RACE && leveltime < 4*TICRATE)
	{
		cmd->buttons &= BT_USE; // Remove all buttons except BT_USE
		cmd->forwardmove = 0;
		cmd->sidemove = 0;
	}

	// Move around.
	// Reactiontime is used to prevent movement
	//  for a bit after a teleport.
	if (player->mo->reactiontime)
		player->mo->reactiontime--;
	else if (player->mo->tracer && player->mo->tracer->type == MT_TUBEWAYPOINT)
	{
		if (player->pflags & PF_ROPEHANG)
		{
			// Bots never analog, so don't check
			player->mo->angle = (cmd->angleturn<<16 /* not FRACBITS */);

			ticruned++;
			if ((cmd->angleturn & TICCMD_RECEIVED) == 0)
				ticmiss++;

			P_DoRopeHang(player, false);
			P_SetPlayerMobjState(player->mo, S_PLAY_CARRY);
			P_DoJumpStuff(player, &player->cmd);
		}
		else if (player->pflags & PF_MINECART)
		{
			// Bots never analog, so don't check
			player->mo->angle = (cmd->angleturn<<16 /* not FRACBITS */);

			ticruned++;
			if ((cmd->angleturn & TICCMD_RECEIVED) == 0)
				ticmiss++;

			P_DoRopeHang(player, true);
			P_DoJumpStuff(player, &player->cmd);
		}
		else
		{
			P_DoZoomTube(player);
			if ((player->mo->state - states < S_PLAY_ATK1
				 || player->mo->state - states > S_PLAY_ATK4) && player->charability2 == CA2_SPINDASH)
				P_SetPlayerMobjState(player->mo, S_PLAY_ATK1);
		}

		if (!player->exiting)
		{
			if (gametype == GT_RACE)
			{
				if (leveltime >= 4*TICRATE)
					player->realtime = leveltime - 4*TICRATE;
				else
					player->realtime = 0;
			}
			player->realtime = leveltime;
		}

		P_CheckSneakerAndLivesTimer(player);
		P_CheckUnderwaterAndSpaceTimer(player);
		// Make sure to catch any other music restoring checks
		if (player->powers[pw_invulnerability] == 1
			&& (!player->powers[pw_super] ||  mapheaderinfo[gamemap-1].nossmusic))
			P_RestoreMusic(player);
	}
	else
		P_MovePlayer(player);

	// check for use
	if (!(player->pflags & PF_NIGHTSMODE))
	{
		if (cmd->buttons & BT_USE)
		{
			if (!(player->pflags & PF_USEDOWN))
				player->pflags |= PF_USEDOWN;
		}
		else
			player->pflags &= ~PF_USEDOWN;
	}

	// Counters, time dependent power ups.
	// Time Bonus & Ring Bonus count settings

	if (player->splish)
		player->splish--;

	if (player->tagzone)
		player->tagzone--;

	if (player->taglag)
		player->taglag--;

	// Strength counts up to diminish fade.
	if (player->powers[pw_sneakers])
		player->powers[pw_sneakers]--;

	if (player->powers[pw_invulnerability])
		player->powers[pw_invulnerability]--;

	if (player->powers[pw_flashing] > 0 && ((player->pflags & PF_NIGHTSMODE) || player->powers[pw_flashing] < flashingtics))
		player->powers[pw_flashing]--;

	if (player->powers[pw_tailsfly] && player->charability != CA_SWIM && !(player->powers[pw_super] && ALL7EMERALDS(player->powers[pw_emeralds]))) // tails fly counter
		player->powers[pw_tailsfly]--;

	if (player->powers[pw_underwater] && ((player->pflags & PF_GHOSTMODE) || (player->powers[pw_watershield])))
	{
		if (player->powers[pw_underwater] <= 12*TICRATE+1)
			P_RestoreMusic(player); //incase they were about to drown

		player->powers[pw_underwater] = 0;
	}
	else if (player->powers[pw_underwater] && !(maptol & TOL_NIGHTS) && !(netgame && player->spectator)) // underwater timer
		player->powers[pw_underwater]--;

	if (player->powers[pw_spacetime] && ((player->pflags & PF_GHOSTMODE) || (player->powers[pw_watershield])))
		player->powers[pw_spacetime] = 0;
	else if (player->powers[pw_spacetime] && !(maptol & TOL_NIGHTS) && !(netgame && player->spectator)) // underwater timer
		player->powers[pw_spacetime]--;

	if (player->powers[pw_gravityboots])
		player->powers[pw_gravityboots]--;

	if (player->powers[pw_extralife])
		player->powers[pw_extralife]--;

	if (player->powers[pw_superparaloop])
		player->powers[pw_superparaloop]--;

	if (player->powers[pw_nightshelper])
		player->powers[pw_nightshelper]--;

	if (player->powers[pw_nocontrol] & FRACUNIT)
		player->powers[pw_nocontrol]--;
	else
		player->powers[pw_nocontrol] = 0;

	//pw_super acts as a timer now
	if (player->powers[pw_super])
		player->powers[pw_super]++;

	if (player->powers[pw_ingoop])
	{
		if (player->mo->state == &states[S_PLAY_STND])
			player->mo->tics = 2;

		player->powers[pw_ingoop]--;
	}

	if (player->bumpertime)
		player->bumpertime--;

	if (player->weapondelay)
		player->weapondelay--;

	if (player->tossdelay)
		player->tossdelay--;

	if (player->shielddelay)
		player->shielddelay--;

	if (player->homing)
		player->homing--;

	if (player->taunttimer)
		player->taunttimer--;

	player->jointime++;

	// Flash player after being hit.
	if (!(player->pflags & PF_NIGHTSMODE))
	{
		if (player->powers[pw_flashing] > 0 && player->powers[pw_flashing] < flashingtics && (leveltime & 1*NEWTICRATERATIO))
			player->mo->flags2 |= MF2_DONTDRAW;
		else if (!cv_objectplace.value)
			player->mo->flags2 &= ~MF2_DONTDRAW;
	}
	else
	{
		if (player->powers[pw_flashing] & 1)
			player->mo->tracer->flags2 |= MF2_DONTDRAW;
		else
			player->mo->tracer->flags2 &= ~MF2_DONTDRAW;
	}

	player->mo->pmomz = 0;
	player->pflags &= ~PF_SLIDING;

}

#endif


//
// P_PlayerThink
//

boolean playerdeadview; // show match/chaos/tag/capture the flag rankings while in death view

void P_PlayerThink(player_t *player)
{
	ticcmd_t *cmd;

	if (!player->mo)
		I_Error("P_PlayerThink: players[%d].mo == NULL", player - players);

	// Zombie bug fixes for consistency proctection
	// Conistency protection is a very bad "fix" for the game's poor network code,
	// players can jump back to a state they where in before while in online play to preserve consitency with the server
	if ((player->health <= 0 || player->mo->health <= 0) && player->playerstate == PST_LIVE) // The player should die
		player->playerstate = PST_DEAD;

	if (player->playerstate == PST_DEAD)
	{
#ifdef SPRITEROLL
		if (cv_grmd2.value && md2_playermodels[player->skin].notfound == false)
			if (player->mo->rollangle >= ANG180-ANGLE_1)
				P_RollMobjRelative(player->mo, ANG180, 7, true);
#endif
	}

	if (player->pflags & PF_GLIDING)
	{
		if (player->mo->state - states < S_PLAY_ABL1 || player->mo->state - states > S_PLAY_ABL2)
			P_SetPlayerMobjState(player->mo, S_PLAY_ABL1);
	}
	else if ((player->pflags & PF_JUMPED) && !player->powers[pw_super]
			 && (player->mo->state - states < S_PLAY_ATK1 || player->mo->state - states > S_PLAY_ATK4
				 ) && player->charability2 == CA2_SPINDASH)
	{
		P_SetPlayerMobjState(player->mo, S_PLAY_ATK1);
	}

	if (player->bonuscount)
		player->bonuscount--;

	if (player->awayviewtics)
		player->awayviewtics--;

	// SRB2CBTODO: Change character abilities to a flag thing?
	// Needs to be changeable or at least combinable and activated when needed

	/// \note do this in the cheat code
	if (player->pflags & PF_NOCLIP)
		player->mo->flags |= MF_NOCLIP;
	else if (!cv_objectplace.value)
		player->mo->flags &= ~MF_NOCLIP;

	cmd = &player->cmd;

#ifdef PARANOIA
	if (player->playerstate == PST_REBORN)
		I_Error("player %d is in PST_REBORN\n", players - player);
#endif

	if (gametype == GT_RACE)
	{
		int i;

		// Check if all the players in the race have finished. If so, end the level.
		for (i = 0; i < MAXPLAYERS; i++)
		{
			if (playeringame[i])
			{
				if (!players[i].exiting && players[i].lives > 0)
					break;
			}
		}

		if (i == MAXPLAYERS && player->exiting == 3*TICRATE) // finished
			player->exiting = (14*TICRATE)/5 + 1;

		// If 10 seconds are left on the timer,
		// begin the drown music for countdown!
		if (countdown == 11*TICRATE - 1)
		{
			if (P_IsLocalPlayer(player))
				S_ChangeMusic(mus_drown, false);
		}

		// If you've hit the countdown and you haven't made
		//  it to the exit, you're a goner!
		else if (countdown == 1 && !player->exiting && player->lives > 0)
		{
			if (netgame && player->health > 0)
				CONS_Printf(text[OUT_OF_TIME], player_names[player-players]);

			player->pflags |= PF_TIMEOVER;

			if (player->pflags & PF_NIGHTSMODE)
			{
				P_DeNightserizePlayer(player);
				S_StartScreamSound(player->mo, sfx_lose);
			}

			player->lives = 1; // Starts the game over music
			P_DamageMobj(player->mo, NULL, NULL, 10000);
			player->lives = 0;

			if (player->playerstate == PST_DEAD)
				return;
		}
	}

	// If it is set, start subtracting
	if (player->exiting && player->exiting < 3*TICRATE)
		player->exiting--;

	if (player->exiting && countdown2)
		player->exiting = 5;

	if (player->exiting == 2 || countdown2 == 2)
	{
		if (cv_playersforexit.value) // Count to be sure everyone's exited
		{
			int i;
			int numplayersingame = 0;
			int numplayersexiting = 0;

			for (i = 0; i < MAXPLAYERS; i++)
			{
				if (!playeringame[i])
					continue;

				if (players[i].lives <= 0)
					continue;

#ifdef JTEBOTS // Ignore bots
				if (players[i].bot)
					continue;
#endif

				numplayersingame++;

				if (players[i].exiting)
					numplayersexiting++;
			}

			if (numplayersexiting >= numplayersingame)
			{
				if (server)
					SendNetXCmd(XD_EXITLEVEL, NULL, 0);
			}
			else
				player->exiting++;
		}
		else
		{
			if (server)
				SendNetXCmd(XD_EXITLEVEL, NULL, 0);
		}
	}

	// check water content, set stuff in mobj
	P_MobjCheckWater(player->mo);

	player->onconveyor = 0;
	// check special sectors : damage & secrets

	if (!player->spectator || (gametype == GT_CTF && player->spectator))
		P_PlayerInSpecialSector(player);

	if (player->playerstate == PST_DEAD)
	{
		player->mo->flags2 &= ~MF2_TRANSLUCENT;
		// show the multiplayer rankings while dead
		if (player == &players[displayplayer])
			playerdeadview = true;

		P_DeathThink(player);

		return;
	}

	// Make sure spectators always have a score and ring count of 0.
	if (player->spectator)
	{
		player->score = 0;
		player->mo->health = 1;
		player->health = 1;
	}

	if (gametype == GT_RACE)
	{
		if (player->lives <= 0)
			player->lives = 3;
	}
	else if (gametype == GT_COOP && (netgame || multiplayer) && player->lives <= 0)
	{
		// In Co-Op, replenish a user's lives if they are depleted.
		if (ultimatemode)
			player->lives = 1;
		else
			player->lives = 3;

		if (player->continues == 0 && !ultimatemode)
			player->continues = 1;
	}

	if (player == &players[displayplayer])
		playerdeadview = false;

	if (gametype == GT_RACE && leveltime < 4*TICRATE)
	{
		cmd->buttons &= BT_USE; // Remove all buttons except BT_USE
		cmd->forwardmove = 0;
		cmd->sidemove = 0;
	}

	// Move around.
	// Reactiontime is used to prevent movement
	//  for a bit after a teleport.
	if (player->mo->reactiontime)
		player->mo->reactiontime--;
	else if (player->mo->tracer && player->mo->tracer->type == MT_TUBEWAYPOINT)
	{
		if (player->pflags & PF_ROPEHANG)
		{
			if ((netgame || (player == &players[consoleplayer]
				&& !cv_analog.value) || (splitscreen
				&& player == &players[secondarydisplayplayer] && !cv_analog2.value)))
			{
				player->mo->angle = (cmd->angleturn<<16 /* not FRACBITS */);
			}

			ticruned++;
			if ((cmd->angleturn & TICCMD_RECEIVED) == 0)
				ticmiss++;

			P_DoRopeHang(player, false);
			P_SetPlayerMobjState(player->mo, S_PLAY_CARRY);
			P_DoJumpStuff(player, &player->cmd);
		}
		else if (player->pflags & PF_MINECART)
		{
			if ((netgame || (player == &players[consoleplayer]
				&& !cv_analog.value) || (splitscreen
				&& player == &players[secondarydisplayplayer] && !cv_analog2.value)))
			{
				player->mo->angle = (cmd->angleturn<<16 /* not FRACBITS */);
			}

			ticruned++;
			if ((cmd->angleturn & TICCMD_RECEIVED) == 0)
				ticmiss++;

			P_DoRopeHang(player, true);
			P_DoJumpStuff(player, &player->cmd);
		}
		else
		{
			P_DoZoomTube(player);
			if ((player->mo->state - states < S_PLAY_ATK1
				|| player->mo->state - states > S_PLAY_ATK4) && player->charability2 == CA2_SPINDASH)
				P_SetPlayerMobjState(player->mo, S_PLAY_ATK1);
		}

		if (!player->exiting)
		{
			if (gametype == GT_RACE)
			{
				if (leveltime >= 4*TICRATE)
					player->realtime = leveltime - 4*TICRATE;
				else
					player->realtime = 0;
			}
			player->realtime = leveltime;
		}

		P_CheckSneakerAndLivesTimer(player);
		P_CheckUnderwaterAndSpaceTimer(player);
		// Make sure to catch any other music restoring checks
		if (player->powers[pw_invulnerability] == 1
			&& (!player->powers[pw_super] ||  mapheaderinfo[gamemap-1].nossmusic))
			P_RestoreMusic(player);
	}
	else
		P_MovePlayer(player);

	// check for use
	if (!(player->pflags & PF_NIGHTSMODE))
	{
		if (cmd->buttons & BT_USE)
		{
			if (!(player->pflags & PF_USEDOWN))
				player->pflags |= PF_USEDOWN;
		}
		else
			player->pflags &= ~PF_USEDOWN;
	}

	// Counters, time dependent power ups.
	// Time Bonus & Ring Bonus count settings

	if (player->splish)
		player->splish--;

	if (player->tagzone)
		player->tagzone--;

	if (player->taglag)
		player->taglag--;

	// Strength counts up to diminish fade.
	if (player->powers[pw_sneakers])
		player->powers[pw_sneakers]--;

	if (player->powers[pw_invulnerability])
		player->powers[pw_invulnerability]--;

	if (player->powers[pw_flashing] > 0 && ((player->pflags & PF_NIGHTSMODE) || player->powers[pw_flashing] < flashingtics))
		player->powers[pw_flashing]--;

	if (player->powers[pw_tailsfly] && player->charability != CA_SWIM && !(player->powers[pw_super] && ALL7EMERALDS(player->powers[pw_emeralds]))) // tails fly counter
		player->powers[pw_tailsfly]--;

	if (player->powers[pw_underwater] && ((player->pflags & PF_GHOSTMODE) || (player->powers[pw_watershield])))
	{
		if (player->powers[pw_underwater] <= 12*TICRATE+1)
			P_RestoreMusic(player); //incase they were about to drown

		player->powers[pw_underwater] = 0;
	}
	else if (player->powers[pw_underwater] && !(maptol & TOL_NIGHTS) && !(netgame && player->spectator)) // underwater timer
		player->powers[pw_underwater]--;

	if (player->powers[pw_spacetime] && ((player->pflags & PF_GHOSTMODE) || (player->powers[pw_watershield])))
		player->powers[pw_spacetime] = 0;
	else if (player->powers[pw_spacetime] && !(maptol & TOL_NIGHTS) && !(netgame && player->spectator)) // underwater timer
		player->powers[pw_spacetime]--;

	if (player->powers[pw_gravityboots])
		player->powers[pw_gravityboots]--;

	if (player->powers[pw_extralife])
		player->powers[pw_extralife]--;

	if (player->powers[pw_superparaloop])
		player->powers[pw_superparaloop]--;

	if (player->powers[pw_nightshelper])
		player->powers[pw_nightshelper]--;

    // If the player's nocontrol has one fracunit in it,
    // it's just a marker allowing the to jump,
    // So when subtracting the nocontrol, if it started having one fracunit in it,
    // and subtracting it causes it to have less than a fracunit in it, it should just turn off.
    // Yeah...sort of confusing :P

	if (player->powers[pw_nocontrol] && (player->powers[pw_nocontrol] & FRACUNIT))
	{
          player->powers[pw_nocontrol]--;
          if (player->powers[pw_nocontrol] < FRACUNIT)
              player->powers[pw_nocontrol] = 0;
    }
	else if (player->powers[pw_nocontrol])
		player->powers[pw_nocontrol]--;

	// pw_super acts as a timer now
	if (player->powers[pw_super])
		player->powers[pw_super]++;

	if (player->powers[pw_ingoop])
	{
		if (player->mo->state == &states[S_PLAY_STND])
			player->mo->tics = 2;

		player->powers[pw_ingoop]--;
	}

	if (player->bumpertime)
		player->bumpertime--;

	if (player->weapondelay)
		player->weapondelay--;

	if (player->tossdelay)
		player->tossdelay--;

	if (player->shielddelay)
		player->shielddelay--;

	if (player->homing)
		player->homing--;

	if (player->taunttimer)
		player->taunttimer--;

	player->jointime++;

#ifdef SEENAMES
	int i;
	// SRB2CBTODO: Restrict maxplayers to 255?
	// Is it possible to optimize things so much
	// that the game could ever support like a billion+ interconnected players?
	byte numplayers = 0;

	// Check to see if there's at least 1 bot or 1 other player in the game,
	//
	for (i = 0; i < MAXPLAYERS; i++)
	{
		if (playeringame[i] && players[i].mo && players[i].mo->health > 0 && players[i].playerstate == PST_LIVE
			&& !players[i].exiting && !player->spectator)
		{
			// ignore spectators in CTF
			numplayers++; // if the conditions are met, add to the number of players in game
		}
	}

	if (numplayers == 1) // single player? no seename then
	{
		if (!player->seename[0] == '\0')
			player->seename[0] = '\0';
	}

	// Don't fire a name checker if no other player is ther but you
	// and only fire one if you're a real player
	if ((leveltime % 10 == 0) // Update about 5x every second // TODO: Improve with real math?
#ifdef JTEBOTS
		&& !player->bot // bots don't need to check names
#endif
		&& numplayers > 1 // don't check if there are no other players
		)
	{
		mobj_t *mo;
		USHORT j;
		if (!player->seename[0] == '\0')
			player->seename[0] = '\0';
		mo = P_SpawnPlayerMissile(player->mo, MT_NAMECHECK, 0, false);

		if (mo)
		{
			mo->flags |= MF_NOCLIPHEIGHT;

			if (gametype == GT_MATCH || gametype == GT_CTF || gametype == GT_RACE || gametype == GT_TAG)
			{
				for (j = 0; j < 128; j++) // 128 is how far to check for a player's name!, 128 is far, about 1280 FRACUNITS
				{
					P_RailThinker(mo);
				}

			}
			else if (gametype == GT_COOP
					 // || gametype == GT_CHAOS // CHAOSISNOTDEADYET
				)
			{
				for (j = 0; j < 48; j++) // Don't make such a long trail in COOP, 480 FRACUNITS
				{
					P_RailThinker(mo);
				}
			}
		}
	}
#endif

	// Flash the player after being hit.
	if (!(player->pflags & PF_NIGHTSMODE))
	{
		if (player->powers[pw_flashing] > 0 && player->powers[pw_flashing] < flashingtics && (leveltime & 2*NEWTICRATERATIO))
			player->mo->flags2 |= MF2_DONTDRAW;
		else if (!cv_objectplace.value)
			player->mo->flags2 &= ~MF2_DONTDRAW;
	}
	else
	{
		if (player->powers[pw_flashing] & 2)
			player->mo->tracer->flags2 |= MF2_DONTDRAW;
		else
			player->mo->tracer->flags2 &= ~MF2_DONTDRAW;
	}

	player->mo->pmomz = 0;
	player->pflags &= ~PF_SLIDING;

}

//
// P_PlayerAfterThink
//
// Thinker for player after all other thinkers have run
//
void P_PlayerAfterThink(player_t *player)
{
	ticcmd_t *cmd;
	long oldweapon = player->currentweapon;

#ifdef PARANOIA
	if (!player->mo)
		I_Error("P_PlayerAfterThink: players[%d].mo == NULL", player - players);
#endif

	cmd = &player->cmd;

	if (curWeather == PRECIP_HEATWAVE) // SRB2CB: This is set here so that the postimg can always be overwritten properly
		postimgtype |= postimg_heat;

	if (player->playerstate == PST_DEAD) // SRB2CBTODO: Make sure that if the player is dead, he's dead!
	{
		// camera may still move when guy is dead
		if (splitscreen && player == &players[secondarydisplayplayer] && camera2.chase)
			P_MoveChaseCamera(player, &camera2, false);
		else if (camera.chase && player == &players[displayplayer])
			P_MoveChaseCamera(player, &camera, false);

		if (player->mo->flags & MF_SOLID)
		{
			player->mo->flags &= ~MF_SOLID;
			player->mo->flags |= MF_NOCLIP;
			player->mo->flags |= MF_NOGRAVITY;
			player->mo->flags |= MF_FLOAT;

			P_SetPlayerMobjState(player->mo, S_PLAY_DIE1);
		}

		return;
	}

	if (!player->mo)
		return;

	if (player->pflags & PF_NIGHTSMODE)
	{
		player->powers[pw_gravityboots] = 0;
		player->mo->eflags &= ~MFE_VERTICALFLIP;
	}

	if (!(player->pflags & PF_WPNDOWN))
	{
		if (cmd->buttons & BT_WEAPONNEXT)
		{
			player->currentweapon++;
			player->currentweapon %= NUM_WEAPONS;
			player->pflags |= PF_WPNDOWN;
		}

		if (cmd->buttons & BT_WEAPONPREV)
		{
			player->currentweapon--;
			if (player->currentweapon < 0)
				player->currentweapon = WEP_RAIL;
			player->pflags |= PF_WPNDOWN;

			if (player->currentweapon == WEP_RAIL && (!(player->ringweapons & RW_RAIL) || !player->powers[pw_railring]))
				player->currentweapon = WEP_EXPLODE;
			if (player->currentweapon == WEP_EXPLODE && (!(player->ringweapons & RW_EXPLODE) || !player->powers[pw_explosionring]))
				player->currentweapon = WEP_GRENADE;
			if (player->currentweapon == WEP_GRENADE && (!(player->ringweapons & RW_GRENADE) || !player->powers[pw_grenadering]))
				player->currentweapon = WEP_SCATTER;
			if (player->currentweapon == WEP_SCATTER && (!(player->ringweapons & RW_SCATTER) || !player->powers[pw_scatterring]))
				player->currentweapon = WEP_BOUNCE;
			if (player->currentweapon == WEP_BOUNCE && (!(player->ringweapons & RW_BOUNCE) || !player->powers[pw_bouncering]))
				player->currentweapon = WEP_AUTO;
			if (player->currentweapon == WEP_AUTO && (!(player->ringweapons & RW_AUTO) || !player->powers[pw_automaticring]))
				player->currentweapon = 0;
		}

		if (cmd->buttons & BT_WEAPONMASK)
		{
			// Read the bits to determine individual weapon ring selection.
			switch (cmd->buttons & BT_WEAPONMASK)
			{
				case 1: // Normal Ring
					player->currentweapon = 0;
					player->pflags |= PF_WPNDOWN;
					break;
				case 2: // Automatic Ring
					if ((player->ringweapons & RW_AUTO) && player->powers[pw_automaticring])
					{
						player->currentweapon = WEP_AUTO;
						player->pflags |= PF_WPNDOWN;
					}
					break;
				case 3: // Bounce Ring
					if ((player->ringweapons & RW_BOUNCE) && player->powers[pw_bouncering])
					{
						player->currentweapon = WEP_BOUNCE;
						player->pflags |= PF_WPNDOWN;
					}
					break;
				case 4: // Scatter Ring
					if ((player->ringweapons & RW_SCATTER) && player->powers[pw_scatterring])
					{
						player->currentweapon = WEP_SCATTER;
						player->pflags |= PF_WPNDOWN;
					}
					break;
				case 5: // Grenade Ring
					if ((player->ringweapons & RW_GRENADE) && player->powers[pw_grenadering])
					{
						player->currentweapon = WEP_GRENADE;
						player->pflags |= PF_WPNDOWN;
					}
					break;
				case 6: // Explosion Ring
					if ((player->ringweapons & RW_EXPLODE) && player->powers[pw_explosionring])
					{
						player->currentweapon = WEP_EXPLODE;
						player->pflags |= PF_WPNDOWN;
					}
					break;
				case 7: // Rail Ring
					if ((player->ringweapons & RW_RAIL) && player->powers[pw_railring])
					{
						player->currentweapon = WEP_RAIL;
						player->pflags |= PF_WPNDOWN;
					}
					break;
			}
		}
	}

	if (!(cmd->buttons & BT_WEAPONNEXT) && !(cmd->buttons & BT_WEAPONPREV)
		&& !(cmd->buttons & BT_WEAPONMASK))
		player->pflags &= ~PF_WPNDOWN;

	// Make sure you have ammo for your selected weapon
	if (player->health > 1)
	{
		if (player->currentweapon == WEP_AUTO && (!(player->ringweapons & RW_AUTO) || !player->powers[pw_automaticring]))
			player->currentweapon = WEP_BOUNCE;
		if (player->currentweapon == WEP_BOUNCE && (!(player->ringweapons & RW_BOUNCE) || !player->powers[pw_bouncering]))
			player->currentweapon = WEP_SCATTER;
		if (player->currentweapon == WEP_SCATTER && (!(player->ringweapons & RW_SCATTER) || !player->powers[pw_scatterring]))
			player->currentweapon = WEP_GRENADE;
		if (player->currentweapon == WEP_GRENADE && (!(player->ringweapons & RW_GRENADE) || !player->powers[pw_grenadering]))
			player->currentweapon = WEP_EXPLODE;
		if (player->currentweapon == WEP_EXPLODE && (!(player->ringweapons & RW_EXPLODE) || !player->powers[pw_explosionring]))
			player->currentweapon = WEP_RAIL;
		if (player->currentweapon == WEP_RAIL && (!(player->ringweapons & RW_RAIL) || !player->powers[pw_railring]))
			player->currentweapon = 0;
	}
	else
		player->currentweapon = 0;

	if (P_IsLocalPlayer(player) && (player->pflags & PF_WPNDOWN) && player->currentweapon != oldweapon)
		S_StartSound(NULL, sfx_menu1);

	if (player->pflags & PF_GLIDING)
	{
		if (player->mo->state - states < S_PLAY_ABL1 || player->mo->state - states > S_PLAY_ABL2)
			P_SetPlayerMobjState(player->mo, S_PLAY_ABL1);
	}
	else if (!(player->pflags & PF_SLIDING) && (player->pflags & PF_JUMPED) && !player->powers[pw_super]
		&& (player->mo->state - states < S_PLAY_ATK1
		|| player->mo->state - states > S_PLAY_ATK4) && player->charability2 == CA2_SPINDASH)
	{
		P_SetPlayerMobjState(player->mo, S_PLAY_ATK1);
	}
	else if (player->pflags & PF_SLIDING)
		P_SetPlayerMobjState(player->mo, player->mo->info->painstate);

	if ((player->pflags & PF_CARRIED) && player->mo->tracer)
	{
		player->mo->height = FixedDiv(P_GetPlayerHeight(player), FixedDiv(14*FRACUNIT,10*FRACUNIT));

		if (player->mo->eflags & MFE_VERTICALFLIP)
		{
			if ((player->mo->tracer->z + player->mo->tracer->height + player->mo->height + FRACUNIT) <= player->mo->tracer->ceilingz)
				player->mo->z = player->mo->tracer->z + player->mo->height + FRACUNIT;
			else
				player->pflags &= ~PF_CARRIED;
		}
		else
		{
			if ((player->mo->tracer->z - player->mo->height - FRACUNIT) >= player->mo->tracer->floorz)
				player->mo->z = player->mo->tracer->z - player->mo->height - FRACUNIT;
			else
				player->pflags &= ~PF_CARRIED;
		}

		if (player->mo->tracer->health <= 0 || (player->mo->tracer->player && player->mo->tracer->player->powers[pw_flashing]))
			player->pflags &= ~PF_CARRIED;
		else
		{
			player->mo->momx = player->mo->tracer->x-player->mo->x;
			player->mo->momy = player->mo->tracer->y-player->mo->y;
			P_TryMove(player->mo, player->mo->x+player->mo->momx, player->mo->y+player->mo->momy, true);
			player->mo->momx = player->mo->momy = 0;
			player->mo->momz = player->mo->tracer->momz;
		}

		if (gametype == GT_COOP)
		{
#ifdef JTEBOTS
			if (player->mo->tracer->player->bot) // SRB2CB: you control the direction of a flying bot
				player->mo->tracer->angle = player->mo->angle;
			else
#endif
				player->mo->angle = player->mo->tracer->angle;

			if (player == &players[consoleplayer])
				localangle = player->mo->angle;
			else if (splitscreen && player == &players[secondarydisplayplayer])
				localangle2 = player->mo->angle;
		}

		if (P_AproxDistance(player->mo->x - player->mo->tracer->x, player->mo->y - player->mo->tracer->y) > player->mo->radius)
			player->pflags &= ~PF_CARRIED;

		P_SetPlayerMobjState(player->mo, S_PLAY_CARRY);
	}
	else if (player->pflags & PF_ITEMHANG && player->mo->tracer)
	{
		// tracer is what you're hanging onto
		P_UnsetThingPosition(player->mo);
		player->mo->x = player->mo->tracer->x;
		player->mo->y = player->mo->tracer->y;
		player->mo->z = player->mo->tracer->z - (player->mo->info->height/3*2); // SRB2CBTODO: This should support scaling too
		if (player->mo->eflags & MFE_VERTICALFLIP)
		{
			if (player->mo->tracer->type == MT_HANGGLIDER)
				player->mo->z += ((player->mo->info->height/3*2)+(player->mo->info->height/4*2))*100/player->mo->scale;
			else if (player->mo->tracer->type == MT_HANGGLIDER)
				player->mo->z += ((player->mo->info->height/3*2))*100/player->mo->scale;
		}

		player->mo->momx = player->mo->momy = player->mo->momz = 0;
		P_SetThingPosition(player->mo);
		P_SetPlayerMobjState(player->mo, S_PLAY_CARRY);

		if (player->mo->tracer->type == MT_BLACKEGGMAN_MISSILE
			|| player->mo->tracer->type == MT_HANGGLIDER)
		{
			// Controllable air vehicles
			if (cmd->forwardmove > 0)
				P_SetObjectAbsMomZ(player->mo->tracer, FRACUNIT/4, true);
			else if (cmd->forwardmove < 0)
				P_SetObjectAbsMomZ(player->mo->tracer, -FRACUNIT/4, true);

			player->mo->tracer->angle = player->mo->angle;

			P_InstaThrust(player->mo->tracer, player->mo->tracer->angle, player->mo->tracer->info->speed);

			// Sync the player's speed!
			player->mo->momx = player->mo->tracer->momx;
			player->mo->momy = player->mo->tracer->momy;
			player->mo->momz = player->mo->tracer->momz;

			if (P_IsObjectOnGround(player->mo)
				|| P_IsObjectOnGround(player->mo->tracer)
				|| player->mo->tracer->health <= 0)
			{
				player->itemspeed = 0;
				player->pflags &= ~PF_ITEMHANG;
				P_SetTarget(&player->mo->tracer, NULL);
			}

		}
	}
	else if ((player->pflags & PF_MINECART) && (player->mo->tracer && player->mo->tracer->type != MT_TUBEWAYPOINT))
	{
		// The tracer is what you're skating onto
		P_UnsetThingPosition(player->mo);
		player->mo->x = player->mo->tracer->x;
		player->mo->y = player->mo->tracer->y;
		player->mo->z = player->mo->tracer->z + 1*FRACUNIT;
		player->mo->momx = player->mo->momy = player->mo->momz = 0;
		P_SetThingPosition(player->mo);
		P_SetPlayerMobjState(player->mo, S_PLAY_STND); // SRB2CBTODO: Custom state for grinding/skateboard stuff?
#ifdef VPHYSICS
		player->mo->pitchangle = player->mo->tracer->pitchangle;
#endif

		if (player->mo->tracer && player->mo->tracer->type == MT_SKATEBOARD)
		{
			// Controllable skateboard and other land vehicles
			if (cmd->forwardmove > 0)
				player->itemspeed += FRACUNIT/4;
			else if (cmd->forwardmove < 0)
				player->itemspeed -= FRACUNIT/4;

			// Don't reverse too fast
			if (player->itemspeed < -player->mo->tracer->info->speed - 10*FRACUNIT)
				player->itemspeed = -player->mo->tracer->info->speed - 10*FRACUNIT;

			// Don't go too fast either
			if (player->itemspeed > player->mo->tracer->reactiontime && player->mo->tracer->reactiontime >= 0)
				player->itemspeed = player->mo->tracer->reactiontime;

			player->mo->tracer->angle = player->mo->angle;
			P_InstaThrust(player->mo->tracer, player->mo->tracer->angle, player->mo->tracer->info->speed + player->itemspeed);

			// Sync the player's speed!
			player->mo->momx = player->mo->tracer->momx;
			player->mo->momy = player->mo->tracer->momy;
			player->mo->momz = player->mo->tracer->momz;

			if ((cmd->buttons & BT_USE)
				|| player->mo->tracer->health <= 0)
			{
				player->itemspeed = 0;
				player->pflags &= ~PF_MINECART;
				P_SetTarget(&player->mo->tracer, NULL);
			}
		}

	}
	else if ((player->pflags & PF_MACESPIN) && player->mo->tracer)
	{
		// The player's tracer is what he/she is hanging onto
		P_UnsetThingPosition(player->mo);
		player->mo->momx = (player->mo->tracer->x - player->mo->x)*2;
		player->mo->momy = (player->mo->tracer->y - player->mo->y)*2;
		player->mo->momz = (player->mo->tracer->z - (player->mo->height-player->mo->tracer->height/2) - player->mo->z)*2;
		player->mo->x = player->mo->tracer->x;
		player->mo->y = player->mo->tracer->y;
		player->mo->z = player->mo->tracer->z - (player->mo->height-player->mo->tracer->height/2);
		P_SetThingPosition(player->mo);
		player->pflags |= PF_JUMPED;
		player->secondjump = false;

		if (cmd->forwardmove > 0)
			player->mo->tracer->target->lastlook += 2;
		else if (cmd->forwardmove < 0 && player->mo->tracer->target->lastlook > player->mo->tracer->target->movecount)
			player->mo->tracer->target->lastlook -= 2;

		if (!(player->mo->tracer->target->flags & MF_SLIDEME))
		{
			if (cmd->buttons & BT_USE)
				player->mo->tracer->target->health += 50;

			player->mo->tracer->target->health += cmd->sidemove;
		}
	}

	// bob view only if looking through the player's eyes
	if (splitscreen && player == &players[secondarydisplayplayer] && !camera2.chase)
		P_CalcHeight(player);
	else if (!camera.chase)
		P_CalcHeight(player);

	// calculate the camera movement
	if (splitscreen && player == &players[secondarydisplayplayer] && camera2.chase)
		P_MoveChaseCamera(player, &camera2, false);
	else if (camera.chase && player == &players[displayplayer])
		P_MoveChaseCamera(player, &camera, false);


	// spectator invisibility // SRB2CBTODO: Make an only through spectator's eye's visible ness
	if (netgame && player->spectator)
		player->mo->flags2 |= MF2_TRANSLUCENT;

	if (!cv_chasecam.value)
	{
		if (player == &players[displayplayer])
		{
			sector_t *sector = player->mo->subsector->sector;

			// see if we are in something that requires heat type post processing

			if (P_FindSpecialLineFromTag(13, sector->tag, -1) != -1)
				postimgtype = (postimgtype & ~postimg_water)|(postimgtype & ~postimg_freeze)|postimg_heat;
			else if (sector->ffloors)
			{
				ffloor_t *rover;

				for (rover = sector->ffloors; rover; rover = rover->next)
				{
					if (!(rover->flags & FF_EXISTS))
						continue;

					fixed_t topheight = *rover->topheight;
					fixed_t bottomheight = *rover->bottomheight;

#ifdef ESLOPE
					if (rover->t_slope)
						topheight = P_GetZAt(rover->t_slope, player->mo->x, player->mo->y);

					if (rover->b_slope)
						bottomheight = P_GetZAt(rover->b_slope, player->mo->x, player->mo->y);
#endif

					if (topheight <= player->mo->z + player->viewheight
						|| bottomheight > player->mo->z + player->viewheight)
						continue;

					if (player->mo->z + player->viewheight < topheight)
					{
						if (P_FindSpecialLineFromTag(13, rover->master->frontsector->tag, -1) != -1)
							postimgtype = (postimgtype & ~postimg_water)|(postimgtype & ~postimg_freeze)|postimg_heat;
					}
				}
			}

			// see if we are in water (water trumps heat)
			if (sector->ffloors)
			{
				ffloor_t *rover;

				for (rover = sector->ffloors; rover; rover = rover->next)
				{
					if (!(rover->flags & FF_EXISTS) || !(rover->flags & FF_SWIMMABLE) || rover->flags & FF_BLOCKPLAYER)
						continue;

					fixed_t topheight = *rover->topheight;
					fixed_t bottomheight = *rover->bottomheight;

#ifdef ESLOPE
					if (rover->t_slope)
						topheight = P_GetZAt(rover->t_slope, player->mo->x, player->mo->y);

					if (rover->b_slope)
						bottomheight = P_GetZAt(rover->b_slope, player->mo->x, player->mo->y);
#endif

					if (topheight <= player->mo->z + player->viewheight
						|| bottomheight > player->mo->z + player->viewheight)
						continue;

					if (player->mo->z + player->viewheight < topheight)
						postimgtype = postimg_water|(postimgtype & ~postimg_heat)|(postimgtype & ~postimg_freeze);
				}
			}

			if (player->mo->eflags & MFE_VERTICALFLIP)
			{
#ifdef AWESOME
				// Only software mode needs to use a postprocessor,
				// in OpenGL mode we can actually FLIP!!!!
				if (rendermode == render_soft)
#endif
					postimgtype |= postimg_flip;
			}

#if 0 // Motion blur
			// Motion blur
			if (player->speed > 35)
			{
				postimgtype |= postimg_motion;
				postimgparam = (player->speed - 32)/4;

				if (postimgparam > 5)
					postimgparam = 5;
			}
#endif
		}
	}

}
