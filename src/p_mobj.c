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
// MERCHANTABILITFY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//-----------------------------------------------------------------------------
/// \file
/// \brief Moving object handling. Spawn functions

#include "doomdef.h"
#include "g_game.h"
#include "g_input.h"
#include "st_stuff.h"
#include "hu_stuff.h"
#include "p_local.h"
#include "p_setup.h"
#include "r_main.h"
#include "r_things.h"
#include "r_sky.h"
#include "r_splats.h"
#include "s_sound.h"
#include "z_zone.h"
#include "m_random.h"
#include "m_misc.h"
#include "info.h"
#include "i_video.h"
#include "dstrings.h"
#include "f_finale.h" // For screen wipes
#include "v_video.h"
#include "hardware/hw_main.h"
#ifdef ESLOPE
#include "p_slopes.h"
#endif

// Real Prototypes to A_*
void A_Boss1Chase(mobj_t *actor);
void A_Boss2Chase(mobj_t *actor);
void A_Boss2Pogo(mobj_t *actor);
void A_BossJetFume(mobj_t *actor);

// protos.
static CV_PossibleValue_t viewheight_cons_t[] = {{16, "MIN"}, {56, "MAX"}, {0, NULL}};

consvar_t cv_viewheight = {"viewheight", VIEWHEIGHTS, 0, viewheight_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};

consvar_t cv_splats = {"splats", "On", CV_SAVE, CV_OnOff, NULL, 0, NULL, NULL, 0, 0, NULL};

long playerstatetics[MAXPLAYERS][S_PLAY_SUPERTRANS9+1];

actioncache_t actioncachehead;

void P_InitCachedActions(void)
{
	actioncachehead.prev = actioncachehead.next = &actioncachehead;
}

void P_RunCachedActions(void)
{
	actioncache_t *ac;
	actioncache_t *next;

	for (ac = actioncachehead.next; ac != &actioncachehead; ac = next)
	{
		var1 = states[ac->statenum].var1;
		var2 = states[ac->statenum].var2;
		states[ac->statenum].action.acp1(ac->mobj);
		next = ac->next;
		Z_Free(ac);
	}
}

void P_AddCachedAction(mobj_t *mobj, int statenum)
{
	//actioncache_t *newaction;
	//newaction = Z_Calloc(sizeof(actioncache_t), PU_LEVEL, NULL); // SRB2CBTODO: Why note sifof(*newaction)?
	actioncache_t *newaction = Z_Calloc(sizeof(actioncache_t), PU_LEVEL, NULL); //SRB2CBTODO: Is this ok?
	newaction->mobj = mobj;
	newaction->statenum = statenum;
	actioncachehead.prev->next = newaction;
	newaction->next = &actioncachehead;
	newaction->prev = actioncachehead.prev;
	actioncachehead.prev = newaction;
}

//
// P_CycleMobjState
//
static void P_CycleMobjState(mobj_t *mobj)
{
	// cycle through states,
	// calling action functions at transitions
	if (mobj->tics != -1)
	{
		mobj->tics--;

		// you can cycle through multiple states in a tic
		if (!mobj->tics && mobj->state)
			if (!P_SetMobjState(mobj, mobj->state->nextstate))
				return; // freed itself
	}
}

//
// P_SetMobjState
// Returns true if the mobj is still present.
//
// Separate from P_SetMobjState because of the pw_flashing check
//
boolean P_SetPlayerMobjState(mobj_t *mobj, statenum_t state)
{
	state_t *st;

	// remember states seen, to detect cycles:
	static statenum_t seenstate_tab[NUMSTATES]; // fast transition table
	statenum_t *seenstate = seenstate_tab; // pointer to table
	static int recursion; // detects recursion
	statenum_t i; // initial state
	boolean ret = true; // return value
	statenum_t tempstate[NUMSTATES]; // for use with recursion

	if (mobj->player)
	{
		// Catch state changes for Super Sonic
		if (mobj->player->powers[pw_super] && (mobj->player->charflags & SF_SUPERANIMS))
		{
			switch (state)
			{
			case S_PLAY_STND:
			case S_PLAY_GASP:
				P_SetPlayerMobjState(mobj, S_PLAY_SUPERSTAND);
				return true;
				break;
			case S_PLAY_FALL1:
			case S_PLAY_PLG1:
			case S_PLAY_RUN1:
				P_SetPlayerMobjState(mobj, S_PLAY_SUPERWALK1);
				return true;
				break;
			case S_PLAY_SPD1:
				P_SetPlayerMobjState(mobj, S_PLAY_SUPERFLY1);
				return true;
				break;
			case S_PLAY_TEETER1:
				P_SetPlayerMobjState(mobj, S_PLAY_SUPERTEETER);
				return true;
				break;
			case S_PLAY_ATK1:
				if (!(mobj->player->charflags & SF_SUPERSPIN))
					return true;
				break;
			default:
				break;
			}
		}
		else if (mobj->state == &states[mobj->info->painstate]
				 && mobj->player->powers[pw_flashing] == flashingtics && state != mobj->info->painstate)
			mobj->player->powers[pw_flashing] = flashingtics-1;

		if ((!(mobj->player->charability2 == CA2_SPINDASH))
			&& (state >= S_PLAY_ATK1 && state <= S_PLAY_ATK4))
			P_SetPlayerMobjState(mobj, S_PLAY_PLG1);
	}

	if (recursion++) // if recursion detected,
		memset(seenstate = tempstate, 0, sizeof tempstate); // clear state table

	i = state;

	do
	{
		if (state == S_NULL)
		{
			mobj->state = (state_t *)S_NULL;
			P_RemoveMobj(mobj);
			ret = false;
			break;
		}

		st = &states[state];
		mobj->state = st;
		mobj->tics = playerstatetics[mobj->player-players][state];
		mobj->sprite = st->sprite;
		mobj->frame = st->frame;

		// Modified handling.
		// Call action functions when the state is set

		if (st->action.acp1)
		{
			var1 = st->var1;
			var2 = st->var2;
			st->action.acp1(mobj);
		}

		seenstate[state] = 1 + st->nextstate;

		state = st->nextstate;
	} while (!mobj->tics && !seenstate[state]);

	if (ret && !mobj->tics)
		CONS_Printf("%s", text[CYCLE_DETECT]);

	if (!--recursion)
		for (;(state = seenstate[i]) > S_NULL; i = state - 1)
			seenstate[i] = S_NULL; // erase memory of states

	return ret;
}


boolean P_SetMobjState(mobj_t *mobj, statenum_t state)
{
	state_t *st;

	// remember states seen, to detect cycles:
	static statenum_t seenstate_tab[NUMSTATES]; // fast transition table
	statenum_t *seenstate = seenstate_tab; // pointer to table
	static int recursion; // detects recursion
	statenum_t i = state; // initial state
	boolean ret = true; // return value
	statenum_t tempstate[NUMSTATES]; // for use with recursion

//#ifdef PARANOIA
	if (mobj->player != NULL)
	{
		CONS_Printf("P_SetMobjState used for player mobj, use P_SetPlayerMobjState instead!\n(State called: %d)", state);
		P_SetPlayerMobjState(mobj, state);
		return false;
	}
//#endif

	if (recursion++) // if recursion detected,
		memset(seenstate = tempstate, 0, sizeof tempstate); // clear state table

	do
	{
		if (state == S_NULL)
		{
			mobj->state = (state_t *)S_NULL;
			P_RemoveMobj(mobj);
			ret = false;
			break;
		}

		st = &states[state];
		mobj->state = st;
		mobj->tics = st->tics;
		mobj->sprite = st->sprite;
		mobj->frame = st->frame;

		// Modified handling.
		// Call action functions when the state is set

		if (st->action.acp1)
		{
			var1 = st->var1;
			var2 = st->var2;
			st->action.acp1(mobj);
		}

		seenstate[state] = 1 + st->nextstate;

		state = st->nextstate;
	} while (!mobj->tics && !seenstate[state]);

	if (ret && !mobj->tics)
		CONS_Printf("%s", text[CYCLE_DETECT]);

	if (!--recursion)
		for (;(state = seenstate[i]) > S_NULL; i = state - 1)
			seenstate[i] = S_NULL; // erase memory of states

	return ret;
}

//----------------------------------------------------------------------------
//
// FUNC P_SetMobjStateNF
//
// Same as P_SetMobjState, but does not call the state function.
//
//----------------------------------------------------------------------------

boolean P_SetMobjStateNF(mobj_t *mobj, statenum_t state)
{
	state_t *st;

	if (state == S_NULL)
	{ // Remove mobj
		P_RemoveMobj(mobj);
		return false;
	}
	st = &states[state];
	mobj->state = st;
	mobj->tics = st->tics;
	mobj->sprite = st->sprite;
	mobj->frame = st->frame;
	return true;
}

static boolean P_SetPrecipMobjState(precipmobj_t *mobj, statenum_t state)
{
	state_t *st;

	if (state == S_NULL)
	{ // Remove mobj
		P_RemovePrecipMobj(mobj);
		return false;
	}
	st = &states[state];
	mobj->state = st;
	mobj->tics = st->tics;
	mobj->sprite = st->sprite;
	mobj->frame = st->frame;
	return true;
}

//
// P_EmeraldManager
//
// Power Stone emerald management
//
void P_EmeraldManager(void)
{
	thinker_t *think;
	mobj_t *mo;
	int i,j;
	int numtospawn;
	int emeraldsspawned = 0;

	boolean hasemerald[MAXHUNTEMERALDS];
	int numwithemerald = 0;

	// Record empty spawn points
	mobj_t *spawnpoints[MAXHUNTEMERALDS];
	int numspawnpoints = 0;

	for (i = 0; i < MAXHUNTEMERALDS; i++)
	{
		hasemerald[i] = false;
		spawnpoints[i] = NULL;
	}

	for (think = thinkercap.next; think != &thinkercap; think = think->next)
	{
		if (think->function.acp1 != (actionf_p1)P_MobjThinker)
			continue; // not a mobj thinker

		mo = (mobj_t *)think;

		if (mo->type == MT_EMERALDSPAWN)
		{
			if (mo->threshold || mo->target) // Either has the emerald spawned or is spawning
			{
				numwithemerald++;
				emeraldsspawned |= mobjinfo[mo->reactiontime].speed;
			}
			else if (numspawnpoints < MAXHUNTEMERALDS)
				spawnpoints[numspawnpoints++] = mo; // Empty spawn points
		}
		else if (mo->type == MT_FLINGEMERALD)
		{
			numwithemerald++;
			emeraldsspawned |= mo->threshold;
		}
	}

	if (numspawnpoints == 0)
		return;

	// But wait! We need to check all the players too, to see if anyone has some of the emeralds.
	for (i = 0; i < MAXPLAYERS; i++)
	{
	    INT32* powers = players[i].powers;

		if (!playeringame[i])
			continue;

		if (!players[i].mo)
			continue;

		if (powers[pw_emeralds] & EMERALD1)
		{
			numwithemerald++;
			emeraldsspawned |= EMERALD1;
		}
		if (powers[pw_emeralds] & EMERALD2)
		{
			numwithemerald++;
			emeraldsspawned |= EMERALD2;
		}
		if (powers[pw_emeralds] & EMERALD3)
		{
			numwithemerald++;
			emeraldsspawned |= EMERALD3;
		}
		if (powers[pw_emeralds] & EMERALD4)
		{
			numwithemerald++;
			emeraldsspawned |= EMERALD4;
		}
		if (powers[pw_emeralds] & EMERALD5)
		{
			numwithemerald++;
			emeraldsspawned |= EMERALD5;
		}
		if (powers[pw_emeralds] & EMERALD6)
		{
			numwithemerald++;
			emeraldsspawned |= EMERALD6;
		}
		if (powers[pw_emeralds] & EMERALD7)
		{
			numwithemerald++;
			emeraldsspawned |= EMERALD7;
		}
	}

	// All emeralds spawned, no worries
	if (numwithemerald >= 7)
		return;

	// Set up spawning for the emeralds
	numtospawn = 7 - numwithemerald;

	if (numtospawn <= 0) // ???
		I_Error("P_EmeraldManager: numtospawn is %d!\n", numtospawn);

	for (i = 0, j = 0; i < numtospawn; i++)
	{
		int tries = 0;
		while (true)
		{
			tries++;

			if (tries > 50)
				break;

			j = P_Random() % numspawnpoints;

			if (hasemerald[j])
				continue;

			hasemerald[j] = true;

			if (!(emeraldsspawned & EMERALD1))
			{
				spawnpoints[j]->reactiontime = MT_EMERALD1;
				emeraldsspawned |= EMERALD1;
			}
			else if (!(emeraldsspawned & EMERALD2))
			{
				spawnpoints[j]->reactiontime = MT_EMERALD2;
				emeraldsspawned |= EMERALD2;
			}
			else if (!(emeraldsspawned & EMERALD3))
			{
				spawnpoints[j]->reactiontime = MT_EMERALD3;
				emeraldsspawned |= EMERALD3;
			}
			else if (!(emeraldsspawned & EMERALD4))
			{
				spawnpoints[j]->reactiontime = MT_EMERALD4;
				emeraldsspawned |= EMERALD4;
			}
			else if (!(emeraldsspawned & EMERALD5))
			{
				spawnpoints[j]->reactiontime = MT_EMERALD5;
				emeraldsspawned |= EMERALD5;
			}
			else if (!(emeraldsspawned & EMERALD6))
			{
				spawnpoints[j]->reactiontime = MT_EMERALD6;
				emeraldsspawned |= EMERALD6;
			}
			else if (!(emeraldsspawned & EMERALD7))
			{
				spawnpoints[j]->reactiontime = MT_EMERALD7;
				emeraldsspawned |= EMERALD7;
			}
			else
				break;

			if (leveltime < TICRATE) // Start of map
				spawnpoints[j]->threshold = 60*TICRATE + P_Random() * (TICRATE/5);
			else
				spawnpoints[j]->threshold = P_Random() * (TICRATE/5);

			break;
		}
	}
}

//
// P_ExplodeMissile
//
void P_ExplodeMissile(mobj_t *mo)
{
	mobj_t *explodemo;
	mo->momx = mo->momy = mo->momz = 0;

	if (mo->flags & MF_NOCLIPTHING)
		return;

	P_SetMobjState(mo, mobjinfo[mo->type].deathstate);

	if (mo->type == MT_DETON)
	{
		explodemo = P_SpawnMobj(mo->x, mo->y, mo->z, MT_EXPLODE);
		explodemo->momx += (P_Random() % 32) * FRACUNIT/8;
		explodemo->momy += (P_Random() % 32) * FRACUNIT/8;
		S_StartSound(explodemo, sfx_pop);
		explodemo = P_SpawnMobj(mo->x, mo->y, mo->z, MT_EXPLODE);
		explodemo->momx += (P_Random() % 64) * FRACUNIT/8;
		explodemo->momy -= (P_Random() % 64) * FRACUNIT/8;
		S_StartSound(explodemo, sfx_dmpain);
		explodemo = P_SpawnMobj(mo->x, mo->y, mo->z, MT_EXPLODE);
		explodemo->momx -= (P_Random() % 128) * FRACUNIT/8;
		explodemo->momy += (P_Random() % 128) * FRACUNIT/8;
		S_StartSound(explodemo, sfx_pop);
		explodemo = P_SpawnMobj(mo->x, mo->y, mo->z, MT_EXPLODE);
		explodemo->momx -= (P_Random() % 96) * FRACUNIT/8;
		explodemo->momy -= (P_Random() % 96) * FRACUNIT/8;
		S_StartSound(explodemo, sfx_cybdth);
	}

	mo->flags &= ~MF_MISSILE;

	mo->flags |= MF_NOCLIPTHING; // Dummy flag to indicate that this was already called.

	if (mo->info->deathsound && !(mo->flags2 & MF2_DEBRIS))
		S_StartSound(mo, mo->info->deathsound);
}

// P_InsideANonSolidFFloor
//
// Returns TRUE if mobj is inside a non-solid 3d floor.
boolean P_InsideANonSolidFFloor(mobj_t *mobj, ffloor_t *rover)
{
	if (!(rover->flags & FF_EXISTS))
		return false;

	if ((((rover->flags & FF_BLOCKPLAYER) && mobj->player)
		|| ((rover->flags & FF_BLOCKOTHERS) && !mobj->player)))
		return false;

	fixed_t topheight = *rover->topheight;
	fixed_t bottomheight = *rover->bottomheight;

#ifdef ESLOPE
	if (rover->t_slope)
		topheight = P_GetZAt(rover->t_slope, mobj->x, mobj->y);
	if (rover->b_slope)
		bottomheight = P_GetZAt(rover->b_slope, mobj->x, mobj->y);
#endif

	if (mobj->z > topheight)
		return false;

	if (mobj->z + mobj->height < bottomheight)
		return false;

	return true;
}

fixed_t P_GetMobjZAtSecF(mobj_t *mobj, sector_t *sector) // SRB2CBTODO: This needs to be over all the code
{
	if(!mobj)
		I_Error("P_GetMobjZAtSecF: No mobj!");
#ifdef ESLOPE
	if (sector->f_slope)
		return P_GetZAt(sector->f_slope, mobj->x, mobj->y);
	else
#endif
		return sector->floorheight;
}

fixed_t P_GetMobjZAtF(mobj_t *mobj) // SRB2CBTODO: This needs to be over all the code
{
	sector_t *sector;
	sector = R_PointInSubsector(mobj->x, mobj->y)->sector;

#ifdef ESLOPE
	if (sector->f_slope)
		return P_GetZAt(sector->f_slope, mobj->x, mobj->y);
	else
#endif
		return sector->floorheight;
}

fixed_t P_GetMobjZAtSecC(mobj_t *mobj, sector_t *sector) // SRB2CBTODO: This needs to be over all the code
{
	if(!mobj)
		I_Error("P_GetMobjZAtSecF: No mobj!");
#ifdef ESLOPE
	if (sector->c_slope)
		return P_GetZAt(sector->c_slope, mobj->x, mobj->y);
	else
#endif
		return sector->ceilingheight;
}

fixed_t P_GetMobjZAtC(mobj_t *mobj) // SRB2CBTODO: This needs to be over all the code
{
	sector_t *sector;
	sector = R_PointInSubsector(mobj->x, mobj->y)->sector;

#ifdef ESLOPE
	if (sector->c_slope)
		return P_GetZAt(sector->c_slope, mobj->x, mobj->y);
	else
#endif
		return sector->ceilingheight;
}


// Take the player's momementem and add it to our special movement vector!
// This stores the full definition of the player's movement in one measurement
// This gets the direction and intensity of the player's movement,
// what's even cooler is we can orient it to a sloped plane this way!
static void P_VectorUpdate(player_t *player, angle_t angle, fixed_t move)
{

    			{


			//#ifdef REALV
            // Let's make a vector for the mobj!
			v3float_t point1;
			v3float_t point2;

			point2.x = FIXED_TO_FLOAT(player->mo->x);
			point2.y = FIXED_TO_FLOAT(player->mo->y);
			point2.z = FIXED_TO_FLOAT(player->mo->z);

			fixed_t mangle = angle>>ANGLETOFINESHIFT;




            fixed_t addx, addy;
			addx = FixedMul(move, FINECOSINE(mangle));
			addy = FixedMul(move, FINESINE(mangle));

			// Make a vector that's level to the ground (NOT LEVEL TO THE SLOPE),
			// that way we can get the mobj's pitchangle based on
			// the angle between the mobj's vector and the slope's normal
			point1.x = FIXED_TO_FLOAT(player->mo->x+move);
			point1.y = FIXED_TO_FLOAT(player->mo->y+move);
			point1.z = FIXED_TO_FLOAT(player->mo->z);


			player->controlvec = *M_MakeVec3f(&point1, &point2, &player->controlvec);

			//#endif
			}





}


//
// P_Thrust
// Moves the given origin along a given angle by adding.
// // SRB2CBTODO: better math formula?
//
void P_Thrust(mobj_t *mo, angle_t angle, fixed_t move)
{
	angle >>= ANGLETOFINESHIFT;

	move *= NEWTICRATERATIO;

	mo->momx += FixedMul(move, FINECOSINE(angle));

	if (!(twodlevel || (mo->flags2 & MF2_TWOD)))
		mo->momy += FixedMul(move, FINESINE(angle));

		if (mo->player)
		P_VectorUpdate(mo->player, angle, move);
}

//
// P_InstaThrust
// Moves the given origin along a given angle instantly.
//
//
void P_InstaThrust(mobj_t *mo, angle_t angle, fixed_t move)
{
	angle >>= ANGLETOFINESHIFT;

	mo->momx = FixedMul(move, FINECOSINE(angle));

	if (!(twodlevel || (mo->flags2 & MF2_TWOD)))
		mo->momy = FixedMul(move,FINESINE(angle));
}

void P_InstaThrustEvenIn2D(mobj_t *mo, angle_t angle, fixed_t move)
{
	angle >>= ANGLETOFINESHIFT;

	mo->momx = FixedMul(move, FINECOSINE(angle));
	mo->momy = FixedMul(move, FINESINE(angle));
}

// Returns the coordinates of X or Y as if the object had used P_Thrust to move there
fixed_t P_ReturnThrustX(mobj_t *mo, angle_t angle, fixed_t move)
{
	(void)mo;
	angle >>= ANGLETOFINESHIFT;
	return FixedMul(move, FINECOSINE(angle));
}
fixed_t P_ReturnThrustY(mobj_t *mo, angle_t angle, fixed_t move)
{
	(void)mo;
	angle >>= ANGLETOFINESHIFT;
	return FixedMul(move, FINESINE(angle));
}


static void P_CheckMobjSpeed(mobj_t *mo)
{
	// VPHYSICS: Remove this (for objects) MWAHAHA
#ifdef THINGSCALING
	if (mo->momx > FIXEDSCALE(MAXMOVE, mo->scale))
		mo->momx = FIXEDSCALE(MAXMOVE, mo->scale);
	else if (mo->momx < -FIXEDSCALE(MAXMOVE, mo->scale))
		mo->momx = -FIXEDSCALE(MAXMOVE, mo->scale);

	if (mo->momy > FIXEDSCALE(MAXMOVE, mo->scale))
		mo->momy = FIXEDSCALE(MAXMOVE, mo->scale);
	else if (mo->momy < -FIXEDSCALE(MAXMOVE, mo->scale))
		mo->momy = -FIXEDSCALE(MAXMOVE, mo->scale);
#else
	if (mo->momx > MAXMOVE)
		mo->momx = MAXMOVE;
	else if (mo->momx < -MAXMOVE)
		mo->momx = -MAXMOVE;

	if (mo->momy > MAXMOVE)
		mo->momy = MAXMOVE;
	else if (mo->momy < -MAXMOVE)
		mo->momy = -MAXMOVE;
#endif
}


void P_CheckBustBlocks(mobj_t *mo)
{
	msecnode_t *node;

	fixed_t oldx;
	fixed_t oldy;
	boolean bustupdone = false;
	//boolean spinonfloor = (player->mo->z == player->mo->floorz);

	oldx = mo->x;
	oldy = mo->y;

	P_UnsetThingPosition(mo);
	mo->x += mo->momx;
	mo->y += mo->momy;
	P_SetThingPosition(mo);

	for (node = mo->touching_sectorlist; node; node = node->m_snext)
	{
		if (bustupdone)
			break;

		if (!node->m_sector)
			break;

		if (node->m_sector->ffloors)
		{
			ffloor_t *rover;

			for (rover = node->m_sector->ffloors; rover; rover = rover->next)
			{
				if (bustupdone)
					break;

				if (!(rover->flags & FF_EXISTS))
					continue;

				if (!(rover->flags & FF_BUSTUP))
					continue;

				// The FOF needs ML_EFFECT4 flag for things other than players to break it
				if ((!mo->player) && !(rover->master->flags & ML_EFFECT4))
					continue;

				if ((mo->player && (rover->flags & FF_BUSTUP)) || (!mo->player && !rover->master->frontsector->crumblestate))
				{
					// Special block checks for a player
					if (mo->player)
					{
						// If it's an FF_SPINBUST, you have to either be jumping, or coming down
						// onto the top from a spin.
						if (rover->flags & FF_SPINBUST && ((!(mo->player->pflags & PF_JUMPED)
															&& !(mo->player->pflags & PF_SPINNING)) || (mo->player->pflags & PF_STARTDASH)))
							continue;

						// if it's not an FF_SHATTER, you must be spinning
						// or have Knuckles's abilities (or Super Sonic)
						// ...or are drilling in NiGHTS.
						if (!(rover->flags & FF_SHATTER) && !(rover->flags & FF_SPINBUST)
							&& !((mo->player->pflags & PF_SPINNING)/* && spinonfloor*/)
							&& (mo->player->charability != CA_GLIDEANDCLIMB && !mo->player->powers[pw_super])
							&& !(mo->player->pflags & PF_DRILLING))
							continue;

						// Only Knuckles can break this rock...
						if (!(rover->flags & FF_SHATTER) && (rover->flags & FF_ONLYKNUX) && !(mo->player->charability == CA_GLIDEANDCLIMB))
							continue;
					}

					// Height checks
					if (rover->flags & FF_SHATTERBOTTOM)
					{
						if (mo->z+mo->momz + mo->height < *rover->bottomheight)
							continue;

						if (mo->z+mo->height > *rover->bottomheight)
							continue;
					}
					else if (rover->flags & FF_SPINBUST)
					{
						if (mo->z+mo->momz > *rover->topheight)
							continue;

						if (mo->z+mo->height < *rover->bottomheight)
							continue;
					}
					else if (rover->flags & FF_SHATTER)
					{
						if (mo->z+mo->momz > *rover->topheight)
							continue;

						if (mo->z+mo->momz + mo->height < *rover->bottomheight)
							continue;
					}
					else
					{
						if (mo->z >= *rover->topheight)
							continue;

						if (mo->z+mo->height < *rover->bottomheight)
							continue;
					}

					// Make the mobj slightly slow down if hitting the block from the top
					if (((rover->flags & FF_SPINBUST) || (rover->flags & FF_SHATTER)) && mo->z >= *rover->topheight)
						mo->momz >>= 1;

					EV_CrumbleChain(node->m_sector, rover);

					// Run a linedef executor??
					if (rover->master->flags & ML_EFFECT5)
						P_LinedefExecute(P_AproxDistance(rover->master->dx, rover->master->dy)>>FRACBITS, mo, node->m_sector);

					bustupdone = true;
				}
			}
		}
	}

	P_UnsetThingPosition(mo);
	mo->x = oldx;
	mo->y = oldy;
	P_SetThingPosition(mo);
}

//
// P_CheckGravity
//
// Checks the current gravity state
// of the object. If affect is true,
// a gravity force will be applied.
//
void P_CheckGravity(mobj_t *mo, boolean affect)
{
	fixed_t gravityadd = 0;
	boolean no3dfloorgrav = true; // Custom gravity

#ifdef SPRITEROLL
	boolean hadvflip = (mo->eflags & MFE_VERTICALFLIP);
#endif

	if (mo->type != MT_SPINFIRE && !(mo->player && mo->player->playerstate == PST_DEAD))
		mo->eflags &= ~MFE_VERTICALFLIP;

	if (mo->subsector->sector->ffloors) // Check for 3D floor gravity too.
	{
		ffloor_t *rover;

		for (rover = mo->subsector->sector->ffloors; rover; rover = rover->next)
		{
			if (!(rover->flags & FF_EXISTS))
				continue;

			if (P_InsideANonSolidFFloor(mo, rover))
			{
				if (rover->master->frontsector->gravity)
				{
					gravityadd = -FixedMul(gravity,
						(FixedDiv(*rover->master->frontsector->gravity>>FRACBITS, 1000)));

					if (rover->master->frontsector->verticalflip && gravityadd > 0)
					{
						if (!(mo->player && mo->player->playerstate == PST_DEAD))
							mo->eflags |= MFE_VERTICALFLIP;
					}

					no3dfloorgrav = false;
					break;
				}
			}
		}
	}

	if (no3dfloorgrav)
	{
		if (mo->subsector->sector->gravity)
			gravityadd = -FixedMul(gravity,
				(FixedDiv(*mo->subsector->sector->gravity>>FRACBITS, 1000)));
		else
			gravityadd = -gravity;

		if (mo->subsector->sector->verticalflip && gravityadd > 0)
		{
			if (!(mo->player && mo->player->playerstate == PST_DEAD))
				mo->eflags |= MFE_VERTICALFLIP;
		}
	}

	// Particles are special!
	if (mo->type == MT_LIGHTPARTICLE || mo->type == MT_PARTICLE)
	{
		gravityadd = gravityadd/2;
	}

	// Less gravity while underwater.
	if (mo->eflags & MFE_UNDERWATER)
#ifdef CUSTOMWATER
		gravityadd = gravityadd/5;
#else
		gravityadd = gravityadd/3;
#endif

	if (!mo->momz) // mobj at stop, no floor, so feel the push of gravity!
		gravityadd <<= 1;

	if (mo->player)
	{
		if ((mo->player->charability == CA_FLY)
			&& ((mo->player->powers[pw_tailsfly])
				|| (mo->player->mo->state == &states[S_PLAY_SPC1])
				|| (mo->player->mo->state == &states[S_PLAY_SPC2])
				|| (mo->player->mo->state == &states[S_PLAY_SPC3])
				|| (mo->player->mo->state == &states[S_PLAY_SPC4])))
			gravityadd = gravityadd/3; // less gravity while flying
		if ((mo->player->pflags & PF_GLIDING) && mo->player->playerstate != PST_DEAD)
			gravityadd = gravityadd/3; // less gravity while gliding
		if (mo->player->climbing
#ifdef WALLRUN
			|| mo->player->wallrunning
#endif
			)
			gravityadd = 0;
		if (mo->player->pflags & PF_NIGHTSMODE)
			gravityadd = 0;

		if (mo->player->powers[pw_gravityboots]
			&& mo->player->playerstate != PST_DEAD)
		{
			gravityadd = -gravityadd;

			if (mo->eflags & MFE_VERTICALFLIP)
				mo->eflags &= ~MFE_VERTICALFLIP;
			else
				mo->eflags |= MFE_VERTICALFLIP;
		}
	}
	else
	{
		// Objects with permanent reverse gravity (MF2_OBJECTFLIP)
		if (mo->flags2 & MF2_OBJECTFLIP)
		{
			mo->eflags |= MFE_VERTICALFLIP;
			gravityadd *= -1;
			if (mo->z + mo->height >= mo->ceilingz) // Must be mo->z + mo->height >= otherwise objects fall to bottom
				gravityadd = 0;
		}
		else // Otherwise, sort through the other exceptions.
		{
			switch (mo->type)
			{
				case MT_FLINGRING:
				case MT_FLINGCOIN:
#ifdef BLUE_SPHERES
				case MT_FLINGBALL:
#endif
				case MT_FLINGEMERALD:
				case MT_BOUNCERING:
				case MT_RAILRING:
				case MT_AUTOMATICRING:
				case MT_EXPLOSIONRING:
				case MT_SCATTERRING:
				case MT_GRENADERING:
				case MT_BOUNCEPICKUP:
				case MT_RAILPICKUP:
				case MT_AUTOPICKUP:
				case MT_EXPLODEPICKUP:
				case MT_SCATTERPICKUP:
				case MT_GRENADEPICKUP:
				case MT_REDFLAG:
				case MT_BLUEFLAG:
					if (mo->target)
					{
						// Flung items copy the gravity of their tosser. // SRB2CBTODO: Only when they first spawn?
						if ((mo->target->eflags & MFE_VERTICALFLIP) && !(mo->eflags & MFE_VERTICALFLIP))
						{
							gravityadd = -gravityadd;
							mo->eflags |= MFE_VERTICALFLIP;
						}
					}
					break;
				case MT_CEILINGSPIKE:
					gravityadd *= -1; // Reverse gravity for ceiling spikes
					if (mo->z + mo->height > mo->ceilingz)
						gravityadd = 0;
					break;
				case MT_WATERDROP:
					gravityadd >>= 1;
				default:
					break;
			}
		}
	}

	if (mo->player && mo->player->playerstate == PST_DEAD)
	{
		if (mo->eflags & MFE_VERTICALFLIP)
			gravityadd = gravity;
		else
			gravityadd = -gravity;
	}

	// SRB2CB: For the record, I did this before JTE :P
	if (affect)
		P_SetObjectAbsMomZ(mo, FIXEDSCALE(gravityadd, mo->scale), true);

	if (mo->type == MT_SKIM && mo->z + mo->momz <= mo->watertop && mo->z >= mo->watertop)
	{
		mo->momz = 0;
		mo->flags |= MF_NOGRAVITY;
	}

#ifdef SPRITEROLL // Turtle Man: Extra coolness, really filp!
	if (!hadvflip && (mo->eflags & MFE_VERTICALFLIP))
	{
		P_RollMobjRelative(mo, ANG180, 12, false);
	}
	else if (hadvflip && !(mo->eflags & MFE_VERTICALFLIP))
	{
		P_RollMobjRelative(mo, 0, 12, false);
	}
#endif
}

#define STOPSPEED (FRACUNIT/NEWTICRATERATIO)
#define FRICTION (ORIG_FRICTION/NEWTICRATERATIO) // 0.90625

//
// P_SceneryXYFriction
//
static void P_SceneryXYFriction(mobj_t *mo, fixed_t oldx, fixed_t oldy)
{
	if (mo->momx > -(STOPSPEED*(mo->scale/100))/32 && mo->momx < (STOPSPEED*(mo->scale/100))/32 &&
		mo->momy > -(STOPSPEED*(mo->scale/100))/32 && mo->momy < (STOPSPEED*(mo->scale/100))/32)
	{
		mo->momx = 0;
		mo->momy = 0;
	}
	else
	{
		if ((oldx == mo->x) && (oldy == mo->y)) // didn't go anywhere
		{
			mo->momx = FixedMul(mo->momx, ORIG_FRICTION);
			mo->momy = FixedMul(mo->momy, ORIG_FRICTION);
		}
		else
		{
			mo->momx = FixedMul(mo->momx, mo->friction);
			mo->momy = FixedMul(mo->momy, mo->friction);
		}

		if (mo->type == MT_CANNONBALLDECOR)
		{
			// Stolen from P_SpawnFriction
			mo->friction = FRACUNIT - 0x100;
			mo->movefactor = ((0x10092 - mo->friction)*(0x70))/0x158;
		}
		else
			mo->friction = ORIG_FRICTION;
	}
}

//
// P_XYFriction
//
// adds friction on the xy plane
//
static void P_XYFriction(mobj_t *mo, fixed_t oldx, fixed_t oldy)
{
	player_t *player = mo->player; // valid only if player avatar

	if (player)
	{
		if (player->rmomx > -(STOPSPEED*mo->scale/100) && player->rmomx < (STOPSPEED*mo->scale/100)
			&& player->rmomy > -(STOPSPEED*mo->scale/100) && player->rmomy < (STOPSPEED*mo->scale/100)
			&& (!(player->cmd.forwardmove && !(twodlevel || (player->mo->flags2 & MF2_TWOD)))
				&& !player->cmd.sidemove && !(player->pflags & PF_SPINNING)))
		{
			// if in a walking frame, stop moving
			if (player && (player->pflags & PF_WALKINGANIM))
				P_SetPlayerMobjState(player->mo, S_PLAY_STND);
			mo->momx = player->cmomx;
			mo->momy = player->cmomy;
		}
		else
		{
			if ((oldx == mo->x) && (oldy == mo->y)) // didn't go anywhere
			{
				mo->momx = FixedMul(mo->momx, ORIG_FRICTION);
				mo->momy = FixedMul(mo->momy, ORIG_FRICTION);
			}
			else
			{
				mo->momx = FixedMul(mo->momx, mo->friction);
				mo->momy = FixedMul(mo->momy, mo->friction);
			}

			if (mo->momx || mo->momy)
			{
				int direction = P_GetPlayerControlDirection(player);

				if (direction == 2)
				{
					mo->momx >>= 1;
					mo->momy >>= 1;
				}
			}

			mo->friction = ORIG_FRICTION; // VPHYSICS: Decrase(*1.1+) the friction when going really fast?
		}
	}
	else
		P_SceneryXYFriction(mo, oldx, oldy);
}

//
// P_XYMovement
//
void P_XYMovement(mobj_t *mo)
{
	fixed_t ptryx, ptryy;
	player_t *player;
	fixed_t xmove, ymove;

	fixed_t zmove; // For true vector based movement along planes

	fixed_t oldx, oldy; // reducing bobbing/momentum on ice when up against walls

	fixed_t oldz;

	boolean moved = true;

	if ((mo->type == MT_FLINGRING
		|| mo->type == MT_FLINGCOIN
#ifdef BLUE_SPHERES
		 || mo->type == MT_FLINGBALL
#endif
		|| mo->type == MT_FALLINGROCK
		|| mo->type == MT_BIGTUMBLEWEED
		|| mo->type == MT_LITTLETUMBLEWEED
		|| ((mo->type == MT_BOUNCERING
		|| mo->type == MT_RAILRING
		|| mo->type == MT_AUTOMATICRING
		|| mo->type == MT_EXPLOSIONRING
		|| mo->type == MT_SCATTERRING
		|| mo->type == MT_GRENADERING
		|| mo->type == MT_BOUNCEPICKUP
		|| mo->type == MT_RAILPICKUP
		|| mo->type == MT_AUTOPICKUP
		|| mo->type == MT_FLINGEMERALD
		|| mo->type == MT_EXPLODEPICKUP
		|| mo->type == MT_SCATTERPICKUP
		|| mo->type == MT_GRENADEPICKUP) && mo->flags2 & MF2_DONTRESPAWN))
		&& ((mo->z <= P_GetMobjZAtF(mo)
			 && !(mo->eflags & MFE_VERTICALFLIP))
		|| (mo->z + mo->height >= P_GetMobjZAtC(mo)
			&& (mo->eflags & MFE_VERTICALFLIP)))
		&& (GETSECSPECIAL(mo->subsector->sector->special, 1) == 6
		|| GETSECSPECIAL(mo->subsector->sector->special, 1) == 7))
	{
		mo->fuse = 1*NEWTICRATERATIO; // Remove flingrings if in death pit.
	}
	else if ((mo->type == MT_REDFLAG || mo->type == MT_BLUEFLAG
		|| (mo->flags & MF_PUSHABLE))
		&& mo->z <= mo->subsector->sector->floorheight
		&& (GETSECSPECIAL(mo->subsector->sector->special, 1) == 6
		|| GETSECSPECIAL(mo->subsector->sector->special, 1) == 7
		|| GETSECSPECIAL(mo->subsector->sector->special, 1) == 3
		|| GETSECSPECIAL(mo->subsector->sector->special, 1) == 5
		|| GETSECSPECIAL(mo->subsector->sector->special, 1) == 1))
	{
		// Remove CTF flag if in death pit
		mo->fuse = 1*NEWTICRATERATIO;
	}

	// if it's stopped
	if (!mo->momx && !mo->momy)
	{
		if (mo->flags2 & MF2_SKULLFLY)
		{
			// the skull slammed into something
			mo->flags2 &= ~MF2_SKULLFLY;
			mo->momx = mo->momy = mo->momz = 0;

			// set in 'search new direction' state?
			if (mo->type != MT_EGGMOBILE)
				P_SetMobjState(mo, mo->info->spawnstate);

			return;
		}
	}

	player = mo->player; // valid only if player avatar

	P_CheckMobjSpeed(mo);

// Now when the player moves, it's based on the length(magnitude) of the vector
#ifdef REALV
if (player)
{
    //v3float_t *playerv = player->controlvec;
    float veclength = FV_Magnitudef(&player->controlvec);

    //if (player->mo->subsector && player->mo->subsector->sector->f_slope)
    //FV_Rotate(&player->controlvec, &player->mo->subsector->sector->f_slope->normalf, ANG45);

    /*float rotx = 180;
    float y = player->controlvec.y;
    float z = player->controlvec.z;

    player->controlvec.y = (double)(y*cos(rotx) - z*sin(rotx));
    player->controlvec.z = (double)(y*sin(rotx) + z*cos(rotx));

*/

    xmove = player->controlvec.x*FRACUNIT;
    ymove = player->controlvec.y*FRACUNIT;
    zmove = player->controlvec.z*FRACUNIT;

    mo->z = mo->z+zmove;
}
else
#endif
{
    xmove = mo->momx;
	ymove = mo->momy;
	zmove = 0;
}

	oldx = mo->x;
	oldy = mo->y;
	// Vector
	oldz = mo->z;

	// Pushables can break some blocks
	if (CheckForBustableBlocks && (mo->flags & MF_PUSHABLE)
		&& !(netgame && mo->player && mo->player->spectator))
	{
		P_CheckBustBlocks(mo);
	}

	do
	{
		if (xmove > MAXMOVE/2 || ymove > MAXMOVE/2)
		{
			ptryx = mo->x + xmove/2;
			ptryy = mo->y + ymove/2;
			xmove >>= 1;
			ymove >>= 1;
		}
		else
		{
			ptryx = mo->x + xmove;
			ptryy = mo->y + ymove;
			xmove = ymove = 0;
		}

		if (!P_TryMove(mo, ptryx, ptryy, true) && !tmsprung)
		{
			// blocked move

			if (mo->player)
				moved = false;

			if (mo->flags & MF_BOUNCE)
			{
				P_BounceMove(mo);
				xmove = ymove = 0;
				S_StartSound(mo, mo->info->activesound);

				// Bounce ring algorithm
				if (mo->type == MT_THROWNBOUNCE)
				{
					mo->threshold++;

					// Gain lower amounts of time on each bounce.
					if (mo->threshold < 6)
						mo->fuse += ((6 - mo->threshold) * TICRATE);

					// Check for hit against sky here
					if (ceilingline && ceilingline->backsector
						&& ceilingline->backsector->ceilingpic == skyflatnum
						&& ceilingline->frontsector
						&& ceilingline->frontsector->ceilingpic == skyflatnum
						&& mo->subsector->sector->ceilingheight == mo->ceilingz)
					{
						if (mo->z > ceilingline->backsector->ceilingheight) // demos
						{
							// Hack to prevent missiles exploding
							// against the sky.
							// Does not handle sky floors.
							// Check frontsector as well.

							P_SetMobjState(mo, S_DISS);
							//P_RemoveMobj(mo);
							return;
						}
					}
				}
			}
			else if ((mo->player) || (mo->flags & MF_SLIDEME) // SRB2CBTODO: This is cool, optimize
				|| (mo->flags & MF_PUSHABLE))
			{ // try to slide along it
				P_SlideMove(mo);
				xmove = ymove = 0;
			}
			else if ((mo->flags & MF_MISSILE))
			{
				// explode a missile
				if (ceilingline && ceilingline->backsector
					&& ceilingline->backsector->ceilingpic == skyflatnum
					&& ceilingline->frontsector
					&& ceilingline->frontsector->ceilingpic == skyflatnum
					&& mo->subsector->sector->ceilingheight == mo->ceilingz)
				{
					if (mo->z > ceilingline->backsector->ceilingheight) // demos
					{
						// Hack to prevent missiles exploding
						// against the sky.
						// Does not handle sky floors.
						// Check frontsector as well.

						P_SetMobjState(mo, S_DISS);
						//P_RemoveMobj(mo);
						return;
					}
				}

				// draw damage on wall
				//SPLAT TEST ----------------------------------------------------------
#ifdef WALLSPLATS
				// Make sure namechecks aren't putting anything on the wall
				if (!(mo->flags & MF_NOCLIPHEIGHT))
				{
					divline_t divl;
					divline_t misl;
					fixed_t frac;
					const char *splatpicname;
					// SRB2CBTODO: Make a custom graphic for stuff
					// and extend the function for other use
					splatpicname = ("CONSBACK");

					P_MakeDivline(blockingline, &divl);
					misl.x = mo->x;
					misl.y = mo->y;
					misl.dx = mo->momx;
					misl.dy = mo->momy;
					frac = P_InterceptVector(&divl, &misl);
					R_AddWallSplat(blockingline, P_PointOnLineSide(mo->x,mo->y,blockingline),
								   splatpicname, mo->z, frac, SPLATDRAWMODE_TRANS);
				}
#endif
				// --------------------------------------------------------- SPLAT TEST

				P_ExplodeMissile(mo);
			}
			else if (mo->type == MT_FIREBALL)
			{
				// explode a missile
				if (ceilingline &&
					ceilingline->backsector &&
					ceilingline->backsector->ceilingpic == skyflatnum &&
					ceilingline->frontsector &&
					ceilingline->frontsector->ceilingpic == skyflatnum &&
					mo->subsector->sector->ceilingheight == mo->ceilingz)
				{
					if (mo->z > ceilingline->backsector->ceilingheight) // demos
					{
						// Hack to prevent missiles exploding
						// against the sky.
						// Does not handle sky floors.
						// Check frontsector as well.

						P_SetMobjState(mo, S_DISS);
						return;
					}
				}

				S_StartSound(mo, sfx_tink);

				P_ExplodeMissile(mo);
			}
			else
				mo->momx = mo->momy = 0;
		}
		else if (mo->player)
			moved = true;

	} while (xmove || ymove);

	// Check the gravity status.
	P_CheckGravity(mo, false);

	if (mo->player && !moved && (mo->player->pflags & PF_NIGHTSMODE) && mo->target)
	{
		angle_t fa;

		P_UnsetThingPosition(mo);
		mo->player->angle_pos = mo->player->old_angle_pos;
		mo->player->speed /= 5;
		mo->player->speed *= 4;

		player->flyangle %= 360;

		if (player->pflags & PF_TRANSFERTOCLOSEST)
		{
			mo->x -= mo->momx;
			mo->y -= mo->momy;
		}
		else
		{
			fa = player->old_angle_pos>>ANGLETOFINESHIFT;

			mo->x = mo->target->x + FixedMul(FINECOSINE(fa),mo->target->radius);
			mo->y = mo->target->y + FixedMul(FINESINE(fa),mo->target->radius);
		}

		mo->momx = mo->momy = 0;
		P_SetThingPosition(mo);
	}

	if (mo->type == MT_FIREBALL || mo->type == MT_SHELL)
		return;

	if (((mo->flags & MF_MISSILE) || (mo->flags2 & MF2_SKULLFLY))
		&& !mo->type == MT_DETON)
	{
		return; // no friction for missiles ever
	}

	if (mo->flags & MF_MISSILE)
		return;

	if (mo->player && mo->player->homing) // no friction for homing
		return;

	if (((!(mo->eflags & MFE_VERTICALFLIP) && mo->z > mo->floorz)
		 || (mo->eflags & MFE_VERTICALFLIP && mo->z+mo->height < mo->ceilingz))
		&& !(mo->flags2 & MF2_ONMOBJ)
		&& !(mo->player && (mo->player->pflags & PF_SLIDING)))
		return; // no friction when airborne

	// spinning friction
	if (player)
	{
		if ((player->pflags & PF_SPINNING) && (player->rmomx || player->rmomy) && !(player->pflags & PF_STARTDASH))
		{
			const fixed_t ns = FixedDiv(549*FRICTION, 500*FRACUNIT)*NEWTICRATERATIO;
			mo->momx = FixedMul(mo->momx, ns);
			mo->momy = FixedMul(mo->momy, ns);
			return;
		}
	}

	if (((!(mo->eflags & MFE_VERTICALFLIP) && mo->z > mo->floorz)
		 || (mo->eflags & MFE_VERTICALFLIP && mo->z+mo->height < mo->ceilingz))
		&& mo->type != MT_CRAWLACOMMANDER
		&& mo->type != MT_EGGMOBILE && mo->type != MT_EGGMOBILE2
		&& !(mo->player && (mo->player->pflags & PF_SLIDING)))
		return; // no friction when airborne

	P_XYFriction(mo, oldx, oldy);
}

static void P_RingXYMovement(mobj_t *mo)
{
	fixed_t ptryx, ptryy, xmove, ymove;

	P_CheckMobjSpeed(mo);

	xmove = mo->momx;
	ymove = mo->momy;

	do
	{
		if (xmove > MAXMOVE/2 || ymove > MAXMOVE/2)
		{
			ptryx = mo->x + xmove/2;
			ptryy = mo->y + ymove/2;
			xmove >>= 1;
			ymove >>= 1;
		}
		else
		{
			ptryx = mo->x + xmove;
			ptryy = mo->y + ymove;
			xmove = ymove = 0;
		}

		if (!P_SceneryTryMove(mo, ptryx, ptryy))
			P_SlideMove(mo);
	} while (xmove || ymove);
}

static void P_SceneryXYMovement(mobj_t *mo)
{
	fixed_t ptryx, ptryy, xmove, ymove;
	fixed_t oldx, oldy; // reducing bobbing/momentum on ice when up against walls

	P_CheckMobjSpeed(mo);

	xmove = mo->momx;
	ymove = mo->momy;

	oldx = mo->x;
	oldy = mo->y;

	do
	{
		if (xmove > MAXMOVE/2 || ymove > MAXMOVE/2)
		{
			ptryx = mo->x + xmove/2;
			ptryy = mo->y + ymove/2;
			xmove >>= 1;
			ymove >>= 1;
		}
		else
		{
			ptryx = mo->x + xmove;
			ptryy = mo->y + ymove;
			xmove = ymove = 0;
		}

		if (!P_SceneryTryMove(mo, ptryx, ptryy))
			mo->momx = mo->momy = 0; // blocked move

	} while (xmove || ymove);

	if (mo->z > mo->floorz && !(mo->flags2 & MF2_ONMOBJ))
		return; // no friction when airborne

	if (mo->z > mo->floorz)
		return; // no friction when airborne

	P_SceneryXYFriction(mo, oldx, oldy);
}

static void P_RingZMovement(mobj_t *mo)
{
	// Intercept the stupid 'fall through 3dfloors' bug
	if (mo->subsector->sector->ffloors)
	{
		ffloor_t *rover;
		fixed_t delta1, delta2;
		fixed_t thingtop = mo->z + mo->height;

		// Kalaron: For slopes
		fixed_t topheight = 0;
		fixed_t bottomheight = 0;

		for (rover = mo->subsector->sector->ffloors; rover; rover = rover->next)
		{
			if (!(rover->flags & FF_EXISTS))
				continue;

			if ((!(rover->flags & FF_BLOCKOTHERS || rover->flags & FF_QUICKSAND) || (rover->flags & FF_SWIMMABLE)))
				continue;

			topheight = *rover->topheight;
			bottomheight = *rover->bottomheight;

			if (rover->t_slope)
				topheight = P_GetZAt(rover->t_slope, mo->x, mo->y);

			if (rover->b_slope)
				bottomheight = P_GetZAt(rover->b_slope, mo->x, mo->y);

			if (rover->flags & FF_QUICKSAND)
			{
				if (mo->z < topheight && bottomheight < thingtop)
				{
					if (tmfloorz < mo->z)
						mo->floorz = mo->z;
				}
				// Quicksand blocks never change objects' heights otherwise.
				continue;
			}

			delta1 = mo->z - (bottomheight + ((topheight - bottomheight)/2));
			delta2 = thingtop - (bottomheight + ((topheight - bottomheight)/2));
			if (topheight > mo->floorz && abs(delta1) < abs(delta2)
				&& (!(rover->flags & FF_REVERSEPLATFORM)))
			{
				mo->floorz = topheight;
			}
			if (bottomheight < mo->ceilingz && abs(delta1) >= abs(delta2)
				&& (/*mo->z + mo->height <= bottomheight ||*/ !(rover->flags & FF_PLATFORM)))
			{
				mo->ceilingz = bottomheight;
			}
		}
	}

	// adjust height
	if (mo->pmomz && mo->z != mo->floorz)
	{
		P_SetObjectAbsMomZ(mo, mo->pmomz, true);
		mo->pmomz = 0;
	}

	// Move the object
	fixed_t zmove;

	// Natural speed resistance underwater
	if ((mo->eflags & MFE_UNDERWATER) && !(mo->player && mo->player->pflags & PF_NIGHTSMODE))
	{
		if (mo->momz > 8*FRACUNIT)
			zmove = 2*FRACUNIT;
		else if (mo->momz < -8*FRACUNIT)
			zmove = -2*FRACUNIT;
		else
			zmove = mo->momz/2/NEWTICRATERATIO;
	}
	else
		zmove = mo->momz/NEWTICRATERATIO;

	mo->z += zmove;

	// clip movement
	if (mo->z <= mo->floorz && !(mo->flags & MF_NOCLIPHEIGHT))
	{
		if (mo->z < mo->floorz)
			mo->z = mo->floorz;

		mo->momz = 0;
	}
	else if (mo->z + mo->height > mo->ceilingz && !(mo->flags & MF_NOCLIPHEIGHT))
	{
		mo->momz = 0;

		mo->z = mo->ceilingz - mo->height;
	}
}


//
// P_ZMovement
//
// An important thing to note is that this function handles upside down objects the same way,
// The z movement it sets is absolute, this function handles movement, then checks if a floor/ceiling has been hit
// SRB2CBTODO: NOTE: A HUGE thing to note is that, currently,
// anything upside down with a certain height less than its graphic offset will appear to go through the floor
static void P_ZMovement(mobj_t *mo)
{
	if (!mo)
		I_Error("P_ZMovement: No mobj!");

	fixed_t dist, delta;

	// Intercept the stupid 'fall through 3dfloors' bug
	if (mo->subsector->sector->ffloors)
	{
		ffloor_t *rover;
		fixed_t delta1, delta2;
		fixed_t thingtop = mo->z + mo->height;

		// Kalaron: For slopes
		fixed_t topheight = 0;
		fixed_t bottomheight = 0;

		for (rover = mo->subsector->sector->ffloors; rover; rover = rover->next)
		{
			if (!(rover->flags & FF_EXISTS))
				continue;

			topheight = *rover->topheight;
			bottomheight = *rover->bottomheight;

			if (rover->t_slope)
				topheight = P_GetZAt(rover->t_slope, mo->x, mo->y);

			if (rover->b_slope)
				bottomheight = P_GetZAt(rover->b_slope, mo->x, mo->y);

			if ((!((((rover->flags & FF_BLOCKPLAYER) && mo->player)
					|| ((rover->flags & FF_BLOCKOTHERS) && !mo->player)) || rover->flags & FF_QUICKSAND)
				 || (rover->flags & FF_SWIMMABLE)))
			{
				if (!(mo->type == MT_SKATEBOARD
					  && (mo->ceilingz - topheight >= mo->height
						  && mo->z < topheight + 30*FRACUNIT
						  && mo->z > topheight - 30*FRACUNIT
						  && (rover->flags & FF_SWIMMABLE))))
					continue;
			}

			if (rover->flags & FF_QUICKSAND)
			{
				if (mo->z < topheight && bottomheight < thingtop)
				{
					if (tmfloorz < mo->z)
						mo->floorz = mo->z;
				}
				// Quicksand blocks never change objects' heights otherwise.
				continue;
			}
			delta1 = mo->z - (bottomheight + ((topheight - bottomheight)/2));
			delta2 = thingtop - (bottomheight + ((topheight - bottomheight)/2));

			if (topheight > mo->floorz && abs(delta1) < abs(delta2)
				&& (!(rover->flags & FF_REVERSEPLATFORM)))
			{
				mo->floorz = topheight;
			}
			if (bottomheight < mo->ceilingz && abs(delta1) >= abs(delta2)
				&& (!(rover->flags & FF_PLATFORM)))
			{
				mo->ceilingz = bottomheight;
			}
		}
	}

	// adjust height // SRB2CBTODO: What is this?
	if (mo->pmomz && !P_IsObjectOnGround(mo))
	{
		P_SetObjectAbsMomZ(mo, mo->pmomz, true);
		mo->pmomz = 0;
	}

	// Move the object
	fixed_t zmove;

	// Natural speed resistance underwater
#ifdef CUSTOMWATER
	if ((mo->eflags & MFE_UNDERWATER) && !(mo->player && mo->player->pflags & PF_NIGHTSMODE))
	{
		if (mo->momz > 8*FRACUNIT)
			zmove = 2*FRACUNIT;
		else if (mo->momz < -8*FRACUNIT)
			zmove = -2*FRACUNIT;
		else
			zmove = mo->momz/2/NEWTICRATERATIO;
	}
	else
#endif
		zmove = mo->momz/NEWTICRATERATIO;

	mo->z += zmove;

	switch (mo->type)
	{
		case MT_THROWNBOUNCE:
			if ((mo->flags & MF_BOUNCE) && (mo->z <= mo->floorz || mo->z+mo->height >= mo->ceilingz)
				&& mo->threshold <= 3)
			{
				mo->momz = -mo->momz;
				mo->z += (mo->momz/NEWTICRATERATIO);
				S_StartSound(mo, mo->info->activesound);
				mo->threshold++;
			}
			break;

		case MT_SKIM:
			// skims don't bounce
			if (mo->z > mo->watertop && mo->z - mo->momz <= mo->watertop)
			{
				mo->z = mo->watertop;
				mo->momz = 0;
				mo->flags |= MF_NOGRAVITY;
			}
			break;
		case MT_GOOP:
			if (P_IsObjectOnGround(mo) && mo->momz)
			{
				P_SetMobjState(mo, mo->info->meleestate);
				mo->momx = mo->momy = mo->momz = 0;
				mo->z = mo->floorz;
				if (mo->info->painsound)
					S_StartSound(mo, mo->info->painsound);
			}
			break;
		case MT_SMALLBUBBLE:
			if (mo->z-mo->momz <= mo->floorz) // Hit the floor, so POP!
			{
				byte prandom;

				P_SetMobjState(mo, S_DISS);

				prandom = P_Random();

				if (mo->threshold == 42) // Don't make pop sound.
					break;

				if (prandom <= 51)
					S_StartSound(mo, sfx_bubbl1);
				else if (prandom <= 102)
					S_StartSound(mo, sfx_bubbl2);
				else if (prandom <= 153)
					S_StartSound(mo, sfx_bubbl3);
				else if (prandom <= 204)
					S_StartSound(mo, sfx_bubbl4);
				else
					S_StartSound(mo, sfx_bubbl5);
			}
			break;
		case MT_MEDIUMBUBBLE:
			if (mo->z-mo->momz <= mo->floorz) // Hit the floor, so split!
			{
				// split
				mobj_t *explodemo;

				explodemo = P_SpawnMobj(mo->x, mo->y, mo->z, MT_SMALLBUBBLE);
				explodemo->momx += (P_Random() % 32) * FRACUNIT/8;
				explodemo->momy += (P_Random() % 32) * FRACUNIT/8;
				explodemo->destscale = mo->scale;
				P_SetScale(explodemo,mo->scale);
				explodemo = P_SpawnMobj(mo->x, mo->y, mo->z, MT_SMALLBUBBLE);
				explodemo->momx += (P_Random() % 64) * FRACUNIT/8;
				explodemo->momy -= (P_Random() % 64) * FRACUNIT/8;
				explodemo->destscale = mo->scale;
				P_SetScale(explodemo,mo->scale);
				explodemo = P_SpawnMobj(mo->x, mo->y, mo->z, MT_SMALLBUBBLE);
				explodemo->momx -= (P_Random() % 128) * FRACUNIT/8;
				explodemo->momy += (P_Random() % 128) * FRACUNIT/8;
				explodemo->destscale = mo->scale;
				P_SetScale(explodemo,mo->scale);
				explodemo = P_SpawnMobj(mo->x, mo->y, mo->z, MT_SMALLBUBBLE);
				explodemo->momx -= (P_Random() % 96) * FRACUNIT/8;
				explodemo->momy -= (P_Random() % 96) * FRACUNIT/8;
				explodemo->destscale = mo->scale;
				P_SetScale(explodemo,mo->scale);
			}
			break;
		case MT_RING: // Ignore still rings
		case MT_COIN:
#ifdef BLUE_SPHERES
		case MT_BLUEBALL:
#endif
		case MT_REDTEAMRING:
		case MT_BLUETEAMRING:
		case MT_FLINGCOIN:
		case MT_FLINGRING:
#ifdef BLUE_SPHERES
		case MT_FLINGBALL:
#endif
		case MT_BOUNCERING:
		case MT_AUTOMATICRING:
		case MT_RAILRING:
		case MT_EXPLOSIONRING:
		case MT_SCATTERRING:
		case MT_GRENADERING:
		case MT_BOUNCEPICKUP:
		case MT_RAILPICKUP:
		case MT_AUTOPICKUP:
		case MT_EXPLODEPICKUP:
		case MT_SCATTERPICKUP:
		case MT_GRENADEPICKUP:
		case MT_FLINGEMERALD:
		case MT_NIGHTSWING:
			if (!(mo->momx || mo->momy || mo->momz))
				return;
			break;
		case MT_FLAMEJET:
		case MT_VERTICALFLAMEJET:
			if (!(mo->flags & MF_BOUNCE))
				return;
			break;
		default:
			break;
	}

	if (mo->momz < 0 && (mo->flags2 & MF2_CLASSICPUSH))
		mo->momx = mo->momy = 0;

	if ((mo->flags & MF_FLOAT) && mo->target && mo->health
		&& !(mo->type == MT_DETON || mo->type == MT_JETTBOMBER
			 || mo->type == MT_JETTGUNNER || mo->type == MT_CRAWLACOMMANDER
			 || mo->type == MT_EGGMOBILE2) && mo->target->health > 0)
	{
		// Float down towards target if its close // SRB2CBTODO: Do the new mine enemies work too?
		if (!(mo->flags2 & MF2_SKULLFLY) && !(mo->flags2 & MF2_INFLOAT))
		{
			dist = P_AproxDistance(mo->x - mo->target->x, mo->y - mo->target->y);

			delta = (mo->target->z + (mo->height>>1)) - mo->z;

			if (delta < 0 && dist < -(delta*3)
				&& (mo->type != MT_EGGMOBILE || mo->z - MOBJFLOATSPEED >= mo->floorz+33*FRACUNIT))
				mo->z -= MOBJFLOATSPEED;
			else if (delta > 0 && dist < (delta*3))
				mo->z += MOBJFLOATSPEED;

			if (mo->type == MT_EGGMOBILE && mo->z < mo->floorz+33*FRACUNIT)
				mo->z = mo->floorz+33*FRACUNIT;
			else if (mo->type == MT_JETJAW && mo->z + mo->height > mo->watertop)
				mo->z = mo->watertop - mo->height;
		}

	}

	// Clip movement

	// Handle contact with the floor
	if ((mo->z <= mo->floorz) && !(mo->flags & MF_NOCLIPHEIGHT)) // SRB2CBTODO: Greater than floorz only? Seems strange to have <= and make it = floorz again
	{
		if (mo->flags & MF_MISSILE)
		{
			if (mo->z != mo->floorz)
				mo->z = mo->floorz;
			if (!(mo->flags & MF_NOCLIP))
			{
				P_ExplodeMissile(mo);
				return;
			}
		}
		else if (mo->type == MT_FIREBALL)
			mo->momz = 5*FRACUNIT;

		if (mo->z != mo->floorz)
			mo->z = mo->floorz;

		// Note (id):
		//  somebody left this after the setting momz to 0,
		//  kinda useless there.
		if (mo->flags2 & MF2_SKULLFLY) // the skull slammed into something // SRB2CBTODO: RENAME
			mo->momz = -mo->momz;

		// SRB2CBTODO: This needs to be outside of this if
		if (mo->momz < 0) // falling
		{
			if (mo->type == MT_SEED)
			{
				byte prandom = P_Random();

				if (prandom < 64)
					P_SpawnMobj(mo->x, mo->y, mo->floorz, MT_GFZFLOWER3);
				else if (prandom < 192)
					P_SpawnMobj(mo->x, mo->y, mo->floorz, MT_GFZFLOWER1);
				else
					P_SpawnMobj(mo->x, mo->y, mo->floorz, MT_GFZFLOWER2);

				P_SetMobjState(mo, S_DISS);
				return;
			}

			// Set it once and not continuously
			if (tmfloorthing)
			{
				// Bouncing boxes
				if (tmfloorthing->z > tmfloorthing->floorz)
				{
					if ((tmfloorthing->flags & MF_MONITOR) || (tmfloorthing->flags & MF_PUSHABLE))
						mo->momz = 4*FRACUNIT;
				}
			}
			if ((mo->z <= mo->floorz)
				&& (!(tmfloorthing) || (((tmfloorthing->flags & MF_PUSHABLE)
										 || (tmfloorthing->flags2 & MF2_STANDONME)) || tmfloorthing->type == MT_PLAYER
										|| tmfloorthing->type == MT_FLOORSPIKE)))
			{
				if (!tmfloorthing || mo->momz)
					mo->eflags |= MFE_JUSTHITFLOOR;
			}

			// Flingrings bounce
			if (mo->type == MT_FLINGRING
				|| mo->type == MT_FLINGCOIN
#ifdef BLUE_SPHERES
				|| mo->type == MT_FLINGBALL
#endif
				|| mo->type == MT_BOUNCERING
				|| mo->type == MT_AUTOMATICRING
				|| mo->type == MT_RAILRING
				|| mo->type == MT_EXPLOSIONRING
				|| mo->type == MT_SCATTERRING
				|| mo->type == MT_GRENADERING
				|| mo->type == MT_BOUNCEPICKUP
				|| mo->type == MT_RAILPICKUP
				|| mo->type == MT_AUTOPICKUP
				|| mo->type == MT_EXPLODEPICKUP
				|| mo->type == MT_SCATTERPICKUP
				|| mo->type == MT_GRENADEPICKUP
				|| mo->type == MT_FLINGEMERALD
				|| mo->type == MT_BIGTUMBLEWEED
				|| mo->type == MT_LITTLETUMBLEWEED
				|| mo->type == MT_CANNONBALLDECOR
				|| mo->type == MT_FALLINGROCK)
			{
				if (maptol & TOL_NIGHTS)
					mo->momz = -FixedDiv(mo->momz, 10*FRACUNIT);
				else
					mo->momz = -FixedMul(mo->momz, FixedDiv(17*FRACUNIT,20*FRACUNIT));

				if (mo->type == MT_BIGTUMBLEWEED || mo->type == MT_LITTLETUMBLEWEED)
				{
					if (mo->momx < (STOPSPEED*mo->scale/100) && mo->momx > -(STOPSPEED*mo->scale/100)
						&& mo->momy < (STOPSPEED*mo->scale/100) && mo->momy > -(STOPSPEED*mo->scale/100)
						&& mo->momz < (STOPSPEED*mo->scale/100)*3 && mo->momz > -(STOPSPEED*mo->scale/100)*3)
					{
						if (!(mo->flags & MF_AMBUSH))
						{
							mo->momx = mo->momy = mo->momz = 0;
							P_SetMobjState(mo, mo->info->spawnstate);
						}
						else
						{
							// If deafed, give the tumbleweed another random kick if it runs out of steam.
							mo->momz += 6*FRACUNIT;

							if (P_Random() % 2)
								mo->momx += 6*FRACUNIT;
							else
								mo->momx -= 6*FRACUNIT;

							if (P_Random() % 2)
								mo->momy += 6*FRACUNIT;
							else
								mo->momy -= 6*FRACUNIT;
						}
					}

					// Stolen from P_SpawnFriction
					mo->friction = FRACUNIT - 0x100;
					mo->movefactor = ((0x10092 - mo->friction)*(0x70))/0x158;
				}
				else if (mo->type == MT_FALLINGROCK)
				{
					if (mo->momz > 2*FRACUNIT)
						S_StartSound(mo, mo->info->activesound + (P_Random() % mo->info->mass));

					mo->momz /= 2; // Rocks not so bouncy

					if (mo->momx < (STOPSPEED*mo->scale/100) && mo->momx > -(STOPSPEED*mo->scale/100)
						&& mo->momy < (STOPSPEED*mo->scale/100) && mo->momy > -(STOPSPEED*mo->scale/100)
						&& mo->momz < (STOPSPEED*mo->scale/100)*3 && mo->momz > -(STOPSPEED*mo->scale/100)*3)
						P_SetMobjState(mo, S_DISS);
				}
				else if (mo->type == MT_CANNONBALLDECOR)
					mo->momz /= 2;
			}
			else if (!(tmfloorthing) || (((tmfloorthing->flags & MF_PUSHABLE) || (tmfloorthing->flags2 & MF2_STANDONME))
										 || tmfloorthing->type == MT_PLAYER || tmfloorthing->type == MT_FLOORSPIKE))
				mo->momz = 0;
		}

		if (mo->type == MT_STEAM)
			return;

		mo->z = mo->floorz;
	}
	else if (!(mo->flags & MF_NOGRAVITY)) // Gravity here!
	{
		/// \todo may not be needed (done in P_MobjThinker normally)
		mo->eflags &= ~MFE_JUSTHITFLOOR;

		P_CheckGravity(mo, true);
	}

	if (mo->z + mo->height > mo->ceilingz && !(mo->flags & MF_NOCLIPHEIGHT))
	{
		if (mo->momz > 0) // hit the ceiling
		{
			// Flingrings bounce
			if ((mo->eflags & MFE_VERTICALFLIP)
				&& (mo->type == MT_FLINGRING
					|| mo->type == MT_FLINGCOIN
#ifdef BLUE_SPHERES
					|| mo->type == MT_FLINGBALL
#endif
					|| mo->type == MT_BOUNCERING
					|| mo->type == MT_AUTOMATICRING
					|| mo->type == MT_RAILRING
					|| mo->type == MT_EXPLOSIONRING
					|| mo->type == MT_SCATTERRING
					|| mo->type == MT_GRENADERING
					|| mo->type == MT_BOUNCEPICKUP
					|| mo->type == MT_RAILPICKUP
					|| mo->type == MT_AUTOPICKUP
					|| mo->type == MT_EXPLODEPICKUP
					|| mo->type == MT_SCATTERPICKUP
					|| mo->type == MT_GRENADEPICKUP
					|| mo->type == MT_FLINGEMERALD))
			{
				if (maptol & TOL_NIGHTS)
					mo->momz = -FixedDiv(mo->momz, 10*FRACUNIT);
				else
					mo->momz = -FixedMul(mo->momz, FixedDiv(17*FRACUNIT,20*FRACUNIT));
			}
			else
				mo->momz = 0;
		}
		else
			mo->momz = 0;

		if (mo->z + mo->height > mo->ceilingz)
			mo->z = mo->ceilingz - mo->height; // SRB2CBTODO: Vertical flip issues!

		if (mo->flags2 & MF2_SKULLFLY)
		{ // the skull slammed into something
			mo->momz = -mo->momz;
		}

		if (mo->type == MT_FIREBALL)
		{
			// Don't explode on the sky!
			if (mo->subsector->sector->ceilingpic == skyflatnum &&
				mo->subsector->sector->ceilingheight == mo->ceilingz)
			{
				P_SetMobjState(mo, S_DISS);
				return;
			}

			S_StartSound(mo, sfx_tink);

			P_ExplodeMissile(mo);
			return;
		}
		else if ((mo->flags & MF_MISSILE) && !(mo->flags & MF_NOCLIP))
		{
			// Don't explode on the sky!
			if (mo->subsector->sector->ceilingpic == skyflatnum &&
				mo->subsector->sector->ceilingheight == mo->ceilingz)
			{
				P_SetMobjState(mo, S_DISS);
				return;
			}

			P_ExplodeMissile(mo);
			return;
		}
	}

	// Mines explode upon ground contact
	if ((mo->type == MT_MINE) && P_IsObjectOnGround(mo)
		&& !(mo->state == &states[S_MINE_BOOM1]
			 || mo->state == &states[S_MINE_BOOM2] || mo->state == &states[S_MINE_BOOM3]
			 || mo->state == &states[S_MINE_BOOM4] || mo->state == &states[S_DISS]))
	{
		P_ExplodeMissile(mo);
	}
}

static void P_PlayerZMovement(mobj_t *mo)
{
	if (!mo->player)
		return; // mobj was removed

	// Intercept the stupid 'fall through 3dfloors' bug
	if (mo->subsector->sector->ffloors)
	{
		ffloor_t *rover;
		fixed_t delta1, delta2;
		fixed_t thingtop = mo->z + mo->height;


		for (rover = mo->subsector->sector->ffloors; rover; rover = rover->next)
		{
			if (!(rover->flags & FF_EXISTS))
				continue;

			// Kalaron: For slopes
			fixed_t topheight = *rover->topheight;
			fixed_t bottomheight = *rover->bottomheight;

			if (rover->t_slope)
				topheight = P_GetZAt(rover->t_slope, mo->x, mo->y);

			if (rover->b_slope)
				bottomheight = P_GetZAt(rover->b_slope, mo->x, mo->y);

			if ((rover->flags & FF_SWIMMABLE) && GETSECSPECIAL(rover->master->frontsector->special, 1) == 3
				&& !(rover->master->flags & ML_BLOCKMONSTERS) && ((rover->master->flags & ML_EFFECT3)
				|| mo->z-mo->momz > topheight - 16*FRACUNIT))
				;
			else if ((!(rover->flags & FF_BLOCKPLAYER || rover->flags & FF_QUICKSAND) && !(mo->player && !(mo->player->pflags & PF_NIGHTSMODE)
				&& !mo->player->homing && (((mo->player->charability == CA_SWIM) || mo->player->powers[pw_super]) && mo->ceilingz-topheight >= mo->height)
				&& (rover->flags & FF_SWIMMABLE) && !(mo->player->pflags & PF_SPINNING) && mo->player->speed > mo->player->runspeed
				// SRB2CBTODO: Vertical runonwater support
				&& ((mo->eflags & MFE_VERTICALFLIP && mo->z > bottomheight - 30*FRACUNIT && mo->z < bottomheight + 30*FRACUNIT)
				|| (mo->z < topheight + 30*FRACUNIT && mo->z > topheight - 30*FRACUNIT))
					  )))
				continue;

			if (rover->flags & FF_QUICKSAND)
			{
				if (mo->z < topheight && bottomheight < thingtop)
				{
					mo->floorz = mo->z;
				}
				continue; // This is so you can jump/spring up through quicksand from below.
			}

			delta1 = mo->z - (bottomheight + ((topheight - bottomheight)/2));
			delta2 = thingtop - (bottomheight + ((topheight - bottomheight)/2));

			if (topheight > mo->floorz && abs(delta1) < abs(delta2)
				&& (!(rover->flags & FF_REVERSEPLATFORM)))
			{
				mo->floorz = topheight;
			}
			if (bottomheight < mo->ceilingz && abs(delta1) >= abs(delta2)
				&& (/*mo->z + mo->height <= bottomheight ||*/ !(rover->flags & FF_PLATFORM)))
			{
				mo->ceilingz = bottomheight;
			}
		}
	}

	// Adjust viewheight to the player's
	if ((mo->eflags & MFE_VERTICALFLIP && mo->z + mo->height > mo->ceilingz)
		|| (!(mo->eflags & MFE_VERTICALFLIP) && mo->z < mo->floorz))
	{
		if (mo->eflags & MFE_VERTICALFLIP)
		{
			mo->player->viewheight -= (mo->z+mo->height) - mo->ceilingz;

			mo->player->deltaviewheight =
				((cv_viewheight.value<<FRACBITS) - mo->player->viewheight)>>3;
		}
		else
		{
			mo->player->viewheight -= mo->floorz - mo->z;

			mo->player->deltaviewheight =
				((cv_viewheight.value<<FRACBITS) - mo->player->viewheight)>>3;
		}
	}

	// adjust height
/*	if (mo->pmomz && mo->z > mo->floorz && !(mo->player->pflags & PF_JUMPED))
	{
		mo->momz += mo->pmomz;
		mo->pmomz = 0;
	}*/

	// Move the object

	fixed_t zmove;

	// Natural speed resistance underwater // SRB2CBTODO: SPECIAL water option
#ifdef CUSTOMWATER
	if ((mo->eflags & MFE_UNDERWATER) && !(mo->player && mo->player->pflags & PF_NIGHTSMODE))
	{
		if (mo->momz > 8*FRACUNIT)
			zmove = 2*FRACUNIT;
		else if (mo->momz < -8*FRACUNIT)
			zmove = -2*FRACUNIT;
		else
			zmove = mo->momz/2/NEWTICRATERATIO;
	}
	else
#endif
		zmove = mo->momz/NEWTICRATERATIO;

	mo->z += zmove;

	// Have player fall through floor?
	if (mo->player->playerstate == PST_DEAD
		|| mo->player->playerstate == PST_REBORN)
		goto playergravity;

	// clip movement
	if (P_IsObjectOnGround(mo))
	{
		if (mo->player && (mo->player->pflags & PF_NIGHTSMODE))
		{
			if (mo->player->flyangle < 90 || mo->player->flyangle >= 270)
				mo->player->flyangle += 90;
			else
				mo->player->flyangle -= 90;
			mo->z = mo->floorz;
			mo->player->speed /= 5;
			mo->player->speed *= 4;
			goto nightsdone;
		}
		// Get up if you fell.
		if (mo->state == &states[mo->info->painstate] || mo->state == &states[S_PLAY_SUPERHIT])
			P_SetPlayerMobjState(mo, S_PLAY_STND);

		if (mo->eflags & MFE_VERTICALFLIP)
			mo->z = mo->ceilingz - mo->height;
		else
			mo->z = mo->floorz;

		if ((!(mo->eflags & MFE_VERTICALFLIP) && mo->momz < 0) || (mo->eflags & MFE_VERTICALFLIP && mo->momz > 0)) // falling
		{
			// Squat down. Decrease viewheight for a moment after hitting the ground (hard),
			if (mo->player)
			{
				if (mo->eflags & MFE_VERTICALFLIP)
				{
					if (mo->momz > 8*FRACUNIT)
						mo->player->deltaviewheight = mo->momz>>3;
				}
				else if (mo->momz < -8*FRACUNIT)
				{
					mo->player->deltaviewheight = mo->momz>>3;
				}
			}

			// set it once and not continuously
			if (tmfloorthing)
			{
				if ((tmfloorthing->flags & MF_MONITOR) || (tmfloorthing->flags & MF_PUSHABLE)
					|| (tmfloorthing->flags2 & MF2_STANDONME))
				{
					if (mo->player)
					{
						if (!(mo->player->pflags & PF_JUMPED))
							tmfloorthing = 0;
					}
				}
			}

			if (P_IsObjectOnGround(mo) && (!(tmfloorthing) || (((tmfloorthing->flags & MF_PUSHABLE)
				|| (tmfloorthing->flags2 & MF2_STANDONME)) || tmfloorthing->type == MT_PLAYER
				|| tmfloorthing->type == MT_FLOORSPIKE))) // Spin Attack
			{
				if ((tmfloorthing && mo->momz) || !tmfloorthing)
					mo->eflags |= MFE_JUSTHITFLOOR; // Spin Attack

				if (mo->eflags & MFE_JUSTHITFLOOR)
				{
#ifdef POLYOBJECTS // Collision (standing on one, Z movement)
					// Check if we're on a polyobject
					// that triggers a linedef executor. // SRB2CBTODO: Needs fixing
					msecnode_t *node;

					for (node = mo->touching_sectorlist; node; node = node->m_snext)
					{
						sector_t *sec = node->m_sector;
						subsector_t *newsubsec;
						size_t i;

						for (i = 0; i < numsubsectors; i++)
						{
							newsubsec = &subsectors[i];

							if (newsubsec->sector != sec)
								continue;

							if (newsubsec->polyList)
							{
								polyobj_t *po = newsubsec->polyList;
								sector_t *polysec;

								while (po)
								{
									if (!(po->flags & POF_LDEXEC)
										|| !(po->flags & POF_SOLID))
									{
										po = (polyobj_t *)(po->link.next);
										continue;
									}

									if (!P_MobjInsidePolyobj(po, mo))
									{
										po = (polyobj_t *)(po->link.next);
										continue;
									}

									// We're inside it! Yess...
									polysec = po->lines[0]->backsector;

									if (mo->z == polysec->ceilingheight)
									{
										// We're landing on a PO, so check for
										// a linedef executor.
										// Trigger tags are 32000 + the PO's ID number.
										P_LinedefExecute(32000 + po->id, mo, NULL);
									}

									po = (polyobj_t *)(po->link.next);
								}
							}
						}
					}
#endif

					// Cut momentum in half when you hit the ground and
					// aren't pressing any controls.
					if (!(mo->player->cmd.forwardmove || mo->player->cmd.sidemove) && !mo->player->cmomx && !mo->player->cmomy && !(mo->player->pflags & PF_SPINNING))
					{
						mo->momx = mo->momx/2;
						mo->momy = mo->momy/2;
					}
				}

				if (mo->health)
				{
					if (!(mo->player->pflags & PF_SPINNING) || !(mo->player->pflags & PF_USEDOWN) || (mo->player->pflags & PF_JUMPED))
					{
						if (mo->player->cmomx || mo->player->cmomy)
						{
							if (mo->player->speed > mo->player->runspeed && !(mo->player->pflags & PF_RUNNINGANIM))
								P_SetPlayerMobjState(mo, S_PLAY_SPD1);
							else if ((mo->player->rmomx > (STOPSPEED*mo->scale/100)
								|| mo->player->rmomy > (STOPSPEED*mo->scale/100)) && !(mo->player->pflags & PF_WALKINGANIM))
								P_SetPlayerMobjState(mo, S_PLAY_RUN1);
							else if ((mo->player->rmomx < -(STOPSPEED*mo->scale/100)
								|| mo->player->rmomy < -(STOPSPEED*mo->scale/100)) && !(mo->player->pflags & PF_WALKINGANIM))
								P_SetPlayerMobjState(mo, S_PLAY_RUN1);
							else if ((mo->player->rmomx < (FRACUNIT*mo->scale/100)
								&& mo->player->rmomx > -(FRACUNIT*mo->scale/100) && mo->player->rmomy < (FRACUNIT*mo->scale/100) && mo->player->rmomy > -(FRACUNIT*mo->scale/100)) && !((mo->player->pflags & PF_WALKINGANIM) || (mo->player->pflags & PF_RUNNINGANIM)))
								P_SetPlayerMobjState(mo, S_PLAY_STND);
						}
						else
						{
							if (mo->player->speed > mo->player->runspeed && !(mo->player->pflags & PF_RUNNINGANIM))
								P_SetPlayerMobjState(mo, S_PLAY_SPD1);
							else if ((mo->momx || mo->momy) && !(mo->player->pflags & PF_WALKINGANIM))
								P_SetPlayerMobjState(mo, S_PLAY_RUN1);
							else if (!mo->momx && !mo->momy && !((mo->player->pflags & PF_WALKINGANIM) || (mo->player->pflags & PF_RUNNINGANIM)))
								P_SetPlayerMobjState(mo, S_PLAY_STND);
						}
					}

					if (mo->player->pflags & PF_JUMPED)
						mo->player->pflags &= ~PF_SPINNING;
					else if (!(mo->player->pflags & PF_USEDOWN))
						mo->player->pflags &= ~PF_SPINNING;

					if (!((mo->player->pflags & PF_SPINNING) && (mo->player->pflags & PF_USEDOWN)))
						P_ResetScore(mo->player);

					mo->player->pflags &= ~PF_JUMPED;
					mo->player->pflags &= ~PF_THOKKED;
					mo->player->pflags &= ~PF_GLIDING;
					mo->player->secondjump = false;
					mo->player->glidetime = 0;
					mo->player->climbing = 0;
				}
			}
			if (mo->player && !(mo->player->pflags & PF_SPINNING))
				mo->player->pflags &= ~PF_STARTDASH;

			if (!(tmfloorthing) || (((tmfloorthing->flags & MF_PUSHABLE) || (tmfloorthing->flags2 & MF2_STANDONME)) || tmfloorthing->type == MT_PLAYER || tmfloorthing->type == MT_FLOORSPIKE))
				mo->momz = 0;
		}
	}
	else if (!(mo->flags & MF_NOGRAVITY)) // Gravity here!
	{
		/// \todo may not be needed (done in P_MobjThinker normally)
		mo->eflags &= ~MFE_JUSTHITFLOOR;

playergravity:
		P_CheckGravity(mo, true);

		if (mo->player->playerstate == PST_DEAD)
			return;
	}

nightsdone:

	if ((mo->eflags & MFE_VERTICALFLIP && mo->z < mo->floorz) || (!(mo->eflags & MFE_VERTICALFLIP) && mo->z + mo->height > mo->ceilingz))
	{
		if (mo->player && (mo->player->pflags & PF_NIGHTSMODE))
		{
			if (mo->player->flyangle < 90 || mo->player->flyangle >= 270)
				mo->player->flyangle -= 90;
			else
				mo->player->flyangle += 90;
			mo->player->flyangle %= 360;
			mo->z = mo->ceilingz - mo->height;
			mo->player->speed /= 5;
			mo->player->speed *= 4;
		}

		// Check for "Mario" blocks to hit and bounce them
		if ((mo->eflags & MFE_VERTICALFLIP && mo->momz < 0)
			|| (!(mo->eflags & MFE_VERTICALFLIP) && mo->momz > 0))
		{
			msecnode_t *node;

			if (CheckForMarioBlocks && mo->player && !(netgame && mo->player->spectator)) // Only let the player punch
			{
				// Search the touching sectors, from side-to-side...
				for (node = mo->touching_sectorlist; node; node = node->m_snext)
				{
					if (node->m_sector->ffloors)
					{
						ffloor_t *rover;

						for (rover = node->m_sector->ffloors; rover; rover = rover->next)
						{
							if (!(rover->flags & FF_EXISTS))
								continue;

							// Come on, it's time to go...
							if (rover->flags & FF_MARIO
								&& *rover->bottomheight == mo->ceilingz) // The player's head hit the bottom!
							{
								// DO THE MARIO!
								EV_MarioBlock(rover->master->frontsector, node->m_sector, *rover->topheight, mo);
							}
						}
					}
				} // Ugly ugly billions of braces! Argh!
			}

			// hit the ceiling
			if (mariomode)
				S_StartSound(mo, sfx_mario1);

			if (!(mo->player && (mo->player->climbing
#ifdef WALLRUN
								 || mo->player->wallrunning
#endif
				  )))
				mo->momz = 0;
		}

		if (mo->eflags & MFE_VERTICALFLIP)
			mo->z = mo->floorz;
		else
			mo->z = mo->ceilingz - mo->height;
	}
}


static void P_SceneryZMovement(mobj_t *mo)
{
	// Intercept the stupid 'fall through 3dfloors' bug
	if (mo->subsector->sector->ffloors)
	{
		ffloor_t *rover;
		fixed_t delta1, delta2;
		fixed_t thingtop = mo->z + mo->height;

		// Kalaron: slopes
		fixed_t topheight = 0;
		fixed_t bottomheight = 0;

		for (rover = mo->subsector->sector->ffloors; rover; rover = rover->next)
		{
			if (!(rover->flags & FF_EXISTS))
				continue;

			if ((!(rover->flags & FF_BLOCKOTHERS || rover->flags & FF_QUICKSAND) || (rover->flags & FF_SWIMMABLE)))
				continue;

			topheight = *rover->topheight;
			bottomheight = *rover->bottomheight;

			if (rover->t_slope)
				topheight = P_GetZAt(rover->t_slope, mo->x, mo->y);

			if (rover->b_slope)
				bottomheight = P_GetZAt(rover->b_slope, mo->x, mo->y);

			if (rover->flags & FF_QUICKSAND)
			{
				if (mo->z < topheight && bottomheight < thingtop)
				{
					if (tmfloorz < mo->z)
						mo->floorz = mo->z;
				}
				// Quicksand blocks never change objects' heights otherwise.
				continue;
			}

			delta1 = mo->z - (bottomheight + ((topheight - bottomheight)/2));
			delta2 = thingtop - (bottomheight + ((topheight - bottomheight)/2));
			if (topheight > mo->floorz && abs(delta1) < abs(delta2)
				&& (!(rover->flags & FF_REVERSEPLATFORM)))
			{
				mo->floorz = topheight;
			}
			if (bottomheight < mo->ceilingz && abs(delta1) >= abs(delta2)
				&& (/*mo->z + mo->height <= bottomheight ||*/ !(rover->flags & FF_PLATFORM)))
			{
				mo->ceilingz = bottomheight;
			}
		}
	}

	// adjust height
	if (mo->pmomz && mo->z != mo->floorz)
	{
		P_SetObjectAbsMomZ(mo, mo->pmomz, true);
		mo->pmomz = 0;
	}

	// Move the object
	fixed_t zmove;

	// Natural speed resistance underwater // SRB2CBTODO: Can this be made better?
#ifdef CUSTOMWATER
	if ((mo->eflags & MFE_UNDERWATER) && !(mo->player && mo->player->pflags & PF_NIGHTSMODE))
	{
		if (mo->momz > 8*FRACUNIT)
			zmove = 2*FRACUNIT;
		else if (mo->momz < -8*FRACUNIT)
			zmove = -2*FRACUNIT;
		else
			zmove = mo->momz/2/NEWTICRATERATIO;
	}
	else
#endif
		zmove = mo->momz/NEWTICRATERATIO;

	mo->z += zmove;

	switch (mo->type)
	{
		case MT_SMALLBUBBLE:
			if (mo->z <= mo->floorz) // Hit the floor, so POP!
			{
				byte prandom;

				P_SetMobjState(mo, S_DISS);

				if (mo->threshold == 42) // Don't make pop sound.
					break;

				prandom = P_Random();

				if (prandom <= 51)
					S_StartSound(mo, sfx_bubbl1);
				else if (prandom <= 102)
					S_StartSound(mo, sfx_bubbl2);
				else if (prandom <= 153)
					S_StartSound(mo, sfx_bubbl3);
				else if (prandom <= 204)
					S_StartSound(mo, sfx_bubbl4);
				else
					S_StartSound(mo, sfx_bubbl5);
			}
			break;
		case MT_MEDIUMBUBBLE:
			if (mo->z <= mo->floorz) // Hit the floor, so split!
			{
				// split
				mobj_t *explodemo;

				explodemo = P_SpawnMobj(mo->x, mo->y, mo->z, MT_SMALLBUBBLE);
				explodemo->momx += (P_Random() % 32) * FRACUNIT/8;
				explodemo->momy += (P_Random() % 32) * FRACUNIT/8;
				explodemo->destscale = mo->scale;
				P_SetScale(explodemo,mo->scale);
				explodemo = P_SpawnMobj(mo->x, mo->y, mo->z, MT_SMALLBUBBLE);
				explodemo->momx += (P_Random() % 64) * FRACUNIT/8;
				explodemo->momy -= (P_Random() % 64) * FRACUNIT/8;
				explodemo->destscale = mo->scale;
				P_SetScale(explodemo,mo->scale);
				explodemo = P_SpawnMobj(mo->x, mo->y, mo->z, MT_SMALLBUBBLE);
				explodemo->momx -= (P_Random() % 128) * FRACUNIT/8;
				explodemo->momy += (P_Random() % 128) * FRACUNIT/8;
				explodemo->destscale = mo->scale;
				P_SetScale(explodemo,mo->scale);
				explodemo = P_SpawnMobj(mo->x, mo->y, mo->z, MT_SMALLBUBBLE);
				explodemo->momx -= (P_Random() % 96) * FRACUNIT/8;
				explodemo->momy -= (P_Random() % 96) * FRACUNIT/8;
				explodemo->destscale = mo->scale;
				P_SetScale(explodemo,mo->scale);
			}
			break;
		default:
			break;
	}

	// Clip movement

	// Handle contact with the floor
	if (mo->z <= mo->floorz && !(mo->flags & MF_NOCLIPHEIGHT))
	{
		// Correct the z if it's too low
		if (mo->z < mo->floorz)
			mo->z = mo->floorz;

		if (mo->momz < 0) // falling
		{
			if ((!(tmfloorthing) || (((tmfloorthing->flags & MF_PUSHABLE)
									  || (tmfloorthing->flags2 & MF2_STANDONME)) || tmfloorthing->type == MT_PLAYER
									 || tmfloorthing->type == MT_FLOORSPIKE)))
			{
				if (!tmfloorthing || mo->momz)
					mo->eflags |= MFE_JUSTHITFLOOR; // Spin Attack
			}

			if (!tmfloorthing)
				mo->momz = 0;
		}
	}
	else if (!(mo->flags & MF_NOGRAVITY)) // Gravity here!
	{
		/// \todo may not be needed (done in P_MobjThinker normally)
		mo->eflags &= ~MFE_JUSTHITFLOOR;

		P_CheckGravity(mo, true);
	}
	if (mo->z + mo->height > mo->ceilingz && !(mo->flags & MF_NOCLIPHEIGHT))
	{
		if (mo->momz > 0)
		{
			// Hit the ceiling
			mo->momz = 0;
		}

		mo->z = mo->ceilingz - mo->height;
	}
}

//
// P_MobjCheckWater
//
// Check for water, set stuff in mobj_t struct for movement code later.
// This is called either by P_MobjThinker() or P_PlayerThink()
void P_MobjCheckWater(mobj_t *mobj)
{
	sector_t *sector;
	ULONG oldeflags;
	ULONG wasinwater;

	wasinwater = (mobj->eflags & MFE_UNDERWATER); // important: not boolean!

	/*mobj->subsector->sector->f_slope ?
	mobj->subsector->sector->f_slope->lowz - 1000*FRACUNIT :
	mobj->subsector->sector->floorheight - 1000*FRACUNIT;*/

	// Default if no water exists.
	//mobj->watertop = mobj->waterbottom = MINLONG; // This caused problems,
	// FUTRE REFERENCE: if a weird value is in the code and you feel it should be 0 or -1 instead, JUST LEAVE IT
	mobj->watertop = mobj->waterbottom = mobj->subsector->sector->floorheight - 1000*FRACUNIT;

#ifdef ESLOPE // Set the correct waterbottom/top to be below the lowest point of the slope
	if (mobj->subsector->sector->f_slope)
		mobj->watertop = mobj->waterbottom = mobj->subsector->sector->f_slope->lowz - 1000*FRACUNIT;
#endif

	// see if we are in water, and set some flags for later
	sector = mobj->subsector->sector;
	oldeflags = mobj->eflags;

	if (sector->ffloors) // 3D water
	{
		ffloor_t *rover;

		mobj->eflags &= ~(MFE_UNDERWATER|MFE_TOUCHWATER);

		fixed_t topheight = 0;
		fixed_t bottomheight = 0;

		for (rover = sector->ffloors; rover; rover = rover->next)
		{
			if (!(rover->flags & FF_EXISTS) || !(rover->flags & FF_SWIMMABLE)
				|| (((rover->flags & FF_BLOCKPLAYER) && mobj->player)
				|| ((rover->flags & FF_BLOCKOTHERS) && !mobj->player)))
				continue;

			topheight = *rover->topheight;
			bottomheight = *rover->bottomheight;

			if (rover->t_slope)
				topheight = P_GetZAt(rover->t_slope, mobj->x, mobj->y);

			if (rover->b_slope)
				bottomheight = P_GetZAt(rover->b_slope, mobj->x, mobj->y);

			if (mobj->eflags & MFE_VERTICALFLIP)
			{
				if (topheight < (mobj->z + FIXEDSCALE(mobj->info->height/2, mobj->scale))
					|| bottomheight > (mobj->z + FIXEDSCALE(mobj->info->height, mobj->scale)))
					continue;
			}
			else if (topheight < mobj->z
					 || bottomheight > (mobj->z + FIXEDSCALE(mobj->info->height/2, mobj->scale)))
				continue;

			if (((mobj->eflags & MFE_VERTICALFLIP) && mobj->z < bottomheight)
				|| (!(mobj->eflags & MFE_VERTICALFLIP) && mobj->z + FIXEDSCALE(mobj->info->height, mobj->scale) > topheight))
				mobj->eflags |= MFE_TOUCHWATER;
			else
				mobj->eflags &= ~MFE_TOUCHWATER;

			// Set the watertop and waterbottom
			mobj->watertop = topheight;
			mobj->waterbottom = bottomheight;

			if (((mobj->eflags & MFE_VERTICALFLIP) && mobj->z + FIXEDSCALE(mobj->info->height/1.5, mobj->scale) > bottomheight)
				|| (!(mobj->eflags & MFE_VERTICALFLIP) && mobj->z + FIXEDSCALE(mobj->info->height/1.5, mobj->scale) < topheight))
			{
				mobj->eflags |= MFE_UNDERWATER;

				if (mobj->player)
				{
					if (!((mobj->player->powers[pw_super]) || (mobj->player->powers[pw_invulnerability])))
					{
						if (mobj->player->powers[pw_ringshield]
#ifdef SRB2K
							|| mobj->player->powers[pw_lightningshield]
#endif
							)
						{
							mobj->player->powers[pw_ringshield] = false;
#ifdef SRB2K
							mobj->player->powers[pw_lightningshield] = false;
#endif
							mobj->player->bonuscount = 5;
						}
					}
					if (mobj->player->powers[pw_underwater] <= 0
						&& !(mobj->player->powers[pw_watershield])
#ifdef SRB2K
						&& !(mobj->player->powers[pw_bubbleshield])
#endif
						&& !(mobj->player->exiting)
						&& mobj->player->powers[pw_underwater] < underwatertics + 1)
					{
						if (!(maptol & TOL_NIGHTS) && !mariomode)
							mobj->player->powers[pw_underwater] = underwatertics + 1;
					}
				}
			}
			else
				mobj->eflags &= ~MFE_UNDERWATER;
		}
	}
	else
		mobj->eflags &= ~(MFE_UNDERWATER|MFE_TOUCHWATER);

	if (leveltime < 1)
		wasinwater = mobj->eflags & MFE_UNDERWATER;

	if (((mobj->player && mobj->player->playerstate != PST_DEAD) || (mobj->flags & MF_PUSHABLE) ||
		(mobj->info->flags & MF_PUSHABLE && mobj->fuse))
		&& ((mobj->eflags & MFE_UNDERWATER) != wasinwater))
	{
		int i, bubblecount;
		byte prandom[6];

		// Check to make sure you didn't just cross into a sector to jump out of
		// that has shallower water than the block you were originally in.
		if (!(mobj->eflags & MFE_VERTICALFLIP) && mobj->watertop-mobj->floorz <= FIXEDSCALE(mobj->info->height, mobj->scale)>>1)
			return;

		if ((mobj->eflags & MFE_VERTICALFLIP) && mobj->ceilingz-mobj->waterbottom <= FIXEDSCALE(mobj->info->height, mobj->scale)>>1)
			return;

		if (wasinwater && ((mobj->eflags & MFE_VERTICALFLIP && mobj->momz < 0) || (!(mobj->eflags & MFE_VERTICALFLIP) && mobj->momz > 0)))
		{
			if (!(mobj->eflags & MFE_VERTICALFLIP))
				mobj->momz = FixedMul(mobj->momz, FixedDiv(780*FRACUNIT,457*FRACUNIT)); // Give the mobj a little out-of-water boost.
			else
				mobj->momz = -FixedMul(-mobj->momz, FixedDiv(780*FRACUNIT,457*FRACUNIT)); // Give the mobj a little out-of-water boost.
		}

		if ((mobj->eflags & MFE_VERTICALFLIP && mobj->momz > 0) || (!(mobj->eflags & MFE_VERTICALFLIP) && mobj->momz < 0))
		{
			if ((mobj->eflags & MFE_VERTICALFLIP && mobj->z + (FIXEDSCALE(mobj->info->height, mobj->scale)>>1) - mobj->momz <= mobj->waterbottom)
				|| (!(mobj->eflags & MFE_VERTICALFLIP) && mobj->z + (FIXEDSCALE(mobj->info->height, mobj->scale)>>1) - mobj->momz >= mobj->watertop))
			{
				// Spawn a splash
				if (!(mobj->type == MT_PLAYER && mobj->player && mobj->player->spectator))
				{
					mobj_t *splish;
					if (mobj->eflags & MFE_VERTICALFLIP)
						splish = P_SpawnMobj(mobj->x, mobj->y, mobj->waterbottom - (FIXEDSCALE(mobjinfo[MT_SPLISH].height*(16*FRACUNIT), mobj->scale)), MT_SPLISH);
					else
						splish = P_SpawnMobj(mobj->x, mobj->y, mobj->watertop, MT_SPLISH);
					splish->destscale = mobj->scale;
					P_SetScale(splish, mobj->scale);
				}
			}

			// skipping stone!
			if (mobj->player && (mobj->player->charability2 == CA2_SPINDASH) && !(mobj->player->pflags & PF_JUMPED) && mobj->player->speed/2 > abs(mobj->momz>>FRACBITS)
				&& (mobj->player->pflags & PF_SPINNING) && mobj->z + FIXEDSCALE(mobj->info->height, mobj->scale) - mobj->momz > mobj->watertop)
			{
				mobj->momz = -mobj->momz/2;

				if (!(mobj->eflags & MFE_VERTICALFLIP) && mobj->momz > FIXEDSCALE(6*FRACUNIT, mobj->scale))
					mobj->momz = FIXEDSCALE(6*FRACUNIT, mobj->scale);
				else if (mobj->eflags & MFE_VERTICALFLIP && mobj->momz < FIXEDSCALE(-6*FRACUNIT, mobj->scale))
					mobj->momz = FIXEDSCALE(-6*FRACUNIT, mobj->scale);
			}

		}
		else if ((mobj->eflags & MFE_VERTICALFLIP && mobj->momz < 0) || (!(mobj->eflags & MFE_VERTICALFLIP) && mobj->momz > 0))
		{
			if ((mobj->eflags & MFE_VERTICALFLIP && mobj->z + (FIXEDSCALE(mobj->info->height, mobj->scale)>>1) - mobj->momz > mobj->waterbottom)
				|| (!(mobj->eflags & MFE_VERTICALFLIP) && mobj->z + (FIXEDSCALE(mobj->info->height, mobj->scale)>>1) - mobj->momz < mobj->watertop))
			{
				// Spawn a splash
				if (!(mobj->type == MT_PLAYER && mobj->player && mobj->player->spectator))
				{
					mobj_t *splish;
					if (mobj->eflags & MFE_VERTICALFLIP)
						splish = P_SpawnMobj(mobj->x, mobj->y, mobj->waterbottom - FIXEDSCALE(mobjinfo[MT_SPLISH].height*(16*FRACUNIT), mobj->scale), MT_SPLISH);
					else
						splish = P_SpawnMobj(mobj->x, mobj->y, mobj->watertop, MT_SPLISH);
					splish->destscale = mobj->scale;
					P_SetScale(splish, mobj->scale);
				}
			}
		}

		if (!(mobj->type == MT_PLAYER && mobj->player && mobj->player->spectator))
			S_StartSound(mobj, sfx_splish); // And make a sound!

		bubblecount = FIXEDSCALE(abs(mobj->momz), mobj->scale)>>FRACBITS;
		// Create tons of bubbles
		for (i = 0; i < bubblecount; i++)
		{
			mobj_t *bubble;
			// P_Random()s are called individually
			// to allow consistency across various
			// compilers, since the order of function
			// calls in C is not part of the ANSI
			// specification.
			prandom[0] = P_Random();
			prandom[1] = P_Random();
			prandom[2] = P_Random();
			prandom[3] = P_Random();
			prandom[4] = P_Random();
			prandom[5] = P_Random();

			if (prandom[0] < 32)
				bubble =
				P_SpawnMobj(mobj->x + (prandom[1]<<(FRACBITS-3)) * (prandom[2]&1 ? 1 : -1),
					mobj->y + (prandom[3]<<(FRACBITS-3)) * (prandom[4]&1 ? 1 : -1),
					mobj->z + (prandom[5]<<(FRACBITS-2)), MT_MEDIUMBUBBLE);
			else
				bubble =
				P_SpawnMobj(mobj->x + (prandom[1]<<(FRACBITS-3)) * (prandom[2]&1 ? 1 : -1),
					mobj->y + (prandom[3]<<(FRACBITS-3)) * (prandom[4]&1 ? 1 : -1),
					mobj->z + (prandom[5]<<(FRACBITS-2)), MT_SMALLBUBBLE);

			if (bubble)
			{
				if ((mobj->eflags & MFE_VERTICALFLIP && mobj->momz > 0)
					|| (!(mobj->eflags & MFE_VERTICALFLIP) && mobj->momz < 0))
					bubble->momz = mobj->momz >> 4;
				else
					bubble->momz = 0;
			}
		}
	}
}

static void P_SceneryCheckWater(mobj_t *mobj)
{
	sector_t *sector;

	/*mobj->subsector->sector->f_slope ?
	 mobj->subsector->sector->f_slope->lowz - 1000*FRACUNIT :
	 mobj->subsector->sector->floorheight - 1000*FRACUNIT;*/

	// Default if no water exists.
	//mobj->watertop = mobj->waterbottom = MINLONG; // This caused problems,
	// FUTRE REFERENCE: if a weird value is in the code and you feel it should be 0 or -1 instead, JUST LEAVE IT
	mobj->watertop = mobj->waterbottom = mobj->subsector->sector->floorheight - 1000*FRACUNIT;

	// see if we are in water, and set some flags for later
	sector = mobj->subsector->sector;

	if (sector->ffloors)
	{
		ffloor_t *rover;

		mobj->eflags &= ~(MFE_UNDERWATER|MFE_TOUCHWATER);

		// Kalaron: slopes
		fixed_t topheight = 0;
		fixed_t bottomheight = 0;

		for (rover = sector->ffloors; rover; rover = rover->next)
		{
			if (!(rover->flags & FF_EXISTS) || !(rover->flags & FF_SWIMMABLE) || rover->flags & FF_BLOCKOTHERS)
				continue;

			topheight = *rover->topheight;
			bottomheight = *rover->bottomheight;

			if (rover->t_slope)
				topheight = P_GetZAt(rover->t_slope, mobj->x, mobj->y);

			if (rover->b_slope)
				bottomheight = P_GetZAt(rover->b_slope, mobj->x, mobj->y);

			if (topheight <= mobj->z
				|| bottomheight > (mobj->z + (mobj->info->height >> 1)))
				continue;

			if (mobj->z + mobj->info->height > topheight)
				mobj->eflags |= MFE_TOUCHWATER;
			else
				mobj->eflags &= ~MFE_TOUCHWATER;

			// Set the watertop and waterbottom
			mobj->watertop = topheight;
			mobj->waterbottom = bottomheight;

			if (mobj->z + (FIXEDSCALE(mobj->info->height, mobj->scale) >> 1) < topheight)
				mobj->eflags |= MFE_UNDERWATER;
			else
				mobj->eflags &= ~MFE_UNDERWATER;
		}
	}
	else
		mobj->eflags &= ~(MFE_UNDERWATER|MFE_TOUCHWATER);
}

static boolean P_CameraCheckHeat(camera_t *thiscam)
{
	sector_t *sector;

	// see if we are in water
	sector = thiscam->subsector->sector;

	if (P_FindSpecialLineFromTag(13, sector->tag, -1) != -1)
		return true;

	if (sector->ffloors)
	{
		ffloor_t *rover;

		// Kalaron: slopes
		fixed_t topheight = 0;
		fixed_t bottomheight = 0;

		for (rover = sector->ffloors; rover; rover = rover->next)
		{
			if (!(rover->flags & FF_EXISTS) || !(rover->flags & FF_SWIMMABLE) || rover->flags & FF_BLOCKOTHERS)
				continue;

			topheight = *rover->topheight;
			bottomheight = *rover->bottomheight;

			if (rover->t_slope)
				topheight = P_GetZAt(rover->t_slope, thiscam->x, thiscam->y);

			if (rover->b_slope)
				bottomheight = P_GetZAt(rover->b_slope, thiscam->x, thiscam->y);

			if (topheight <= thiscam->z
				|| bottomheight > (thiscam->z + (thiscam->height >> 1)))
				continue;

			if (thiscam->z + (thiscam->height >> 1) < topheight)
			{
				if (P_FindSpecialLineFromTag(13, rover->master->frontsector->tag, -1) != -1)
					return true;
			}
		}
	}

	return false;
}

static boolean P_CameraCheckWater(camera_t *thiscam) // KALARONTODO: So many duplicate blocks of code, CLEAN UP
{
	sector_t *sector;

	// see if we are in water
	sector = thiscam->subsector->sector;

	// Kalaron: slopes
	fixed_t topheight = 0;
	fixed_t bottomheight = 0;

	if (sector->ffloors)
	{
		ffloor_t *rover;

		for (rover = sector->ffloors; rover; rover = rover->next)
		{
			if (!(rover->flags & FF_EXISTS) || !(rover->flags & FF_SWIMMABLE) || rover->flags & FF_BLOCKOTHERS)
				continue;

			topheight = *rover->topheight;
			bottomheight = *rover->bottomheight;

			if (rover->t_slope)
				topheight = P_GetZAt(rover->t_slope, thiscam->x, thiscam->y);

			if (rover->b_slope)
				bottomheight = P_GetZAt(rover->b_slope, thiscam->x, thiscam->y);

			if (topheight <= thiscam->z
				|| bottomheight > (thiscam->z + (thiscam->height >> 1)))
				continue;

			if (thiscam->z + (thiscam->height >> 1) < *rover->topheight)
				return true;
		}
	}

	return false;
}

void P_DestroyRobots(void)
{
	// Search through all the thinkers for enemies.
	int count;
	mobj_t *mo;
	thinker_t *think;

	count = 0;
	for (think = thinkercap.next; think != &thinkercap; think = think->next)
	{
		if (think->function.acp1 != (actionf_p1)P_MobjThinker)
			continue; // not a mobj thinker

		mo = (mobj_t *)think;
		if (mo->health <= 0 || !(mo->flags & MF_ENEMY || mo->flags & MF_BOSS))
			continue; // not a valid enemy

		if (mo->type == MT_PLAYER) // Don't chase after other players!
			continue;

		// Found a target enemy
		P_DamageMobj(mo, players[consoleplayer].mo, players[consoleplayer].mo, 10000);
	}

	G_ModifyGame();
}

//
// PlayerLandedOnThing
//
static void PlayerLandedOnThing(mobj_t *mo, mobj_t *onmobj) // SRB2CBTODO: What is this for?
{
	(void)onmobj;
	mo->player->deltaviewheight = mo->momz>>3;
}

// P_CameraThinker
//
// Process the mobj-ish required functions of the camera
// P_CameraThinker
//
// Process the mobj-ish required functions of the camera
void P_CameraThinker(camera_t *thiscam)
{
	if (thiscam->momx || thiscam->momy)
	{
		fixed_t ptryx, ptryy, xmove, ymove;
		fixed_t oldx, oldy; // reducing bobbing/momentum on ice when up against walls


		if (!twodlevel)
		{
			if (thiscam->momx > FIXEDSCALE(MAXMOVE, thiscam->scale))
				thiscam->momx = FIXEDSCALE(MAXMOVE, thiscam->scale);
			else if (thiscam->momx < -FIXEDSCALE(MAXMOVE, thiscam->scale))
				thiscam->momx = -FIXEDSCALE(MAXMOVE, thiscam->scale);

			if (thiscam->momy > FIXEDSCALE(MAXMOVE, thiscam->scale))
				thiscam->momy = FIXEDSCALE(MAXMOVE, thiscam->scale);
			else if (thiscam->momy < -FIXEDSCALE(MAXMOVE, thiscam->scale))
				thiscam->momy = -FIXEDSCALE(MAXMOVE, thiscam->scale);
		}
		else
		{

		if (thiscam->momx > MAXMOVE)
			thiscam->momx = MAXMOVE;
		else if (thiscam->momx < -MAXMOVE)
			thiscam->momx = -MAXMOVE;

		if (thiscam->momy > MAXMOVE)
			thiscam->momy = MAXMOVE;
		else if (thiscam->momy < -MAXMOVE)
			thiscam->momy = -MAXMOVE;
		}

		xmove = thiscam->momx;
		ymove = thiscam->momy;

		oldx = thiscam->x;
		oldy = thiscam->y;

		do
		{
			if (xmove > FIXEDSCALE(MAXMOVE, thiscam->scale) || ymove > FIXEDSCALE(MAXMOVE, thiscam->scale))
			{
				ptryx = thiscam->x + xmove/2;
				ptryy = thiscam->y + ymove/2;
				xmove >>= 1;
				ymove >>= 1;
			}
			else
			{
				ptryx = thiscam->x + xmove;
				ptryy = thiscam->y + ymove;
				xmove = ymove = 0;
			}

			if (!P_TryCameraMove(ptryx, ptryy, thiscam))
				P_SlideCameraMove(thiscam);

		} while (xmove || ymove);
	}

	P_CheckCameraPosition(thiscam->x, thiscam->y, thiscam);

	thiscam->subsector = R_PointInSubsector(thiscam->x, thiscam->y);
	thiscam->floorz = tmfloorz;
	thiscam->ceilingz = tmceilingz;

	if (thiscam->momz)
	{
		// adjust height
		thiscam->z += thiscam->momz;

		// clip movement
#ifdef ESLOPE
			if (thiscam->subsector->sector->f_slope)
			{
				if(thiscam->z < P_GetZAt(thiscam->subsector->sector->f_slope, thiscam->x, thiscam->y)+thiscam->height)
					thiscam->z = P_GetZAt(thiscam->subsector->sector->f_slope, thiscam->x, thiscam->y)+thiscam->height;
			}
			else
#endif
			{
		if (thiscam->z <= thiscam->floorz) // hit the floor
			thiscam->z = thiscam->floorz;
			}


		if (thiscam->z + thiscam->height > thiscam->ceilingz)
		{
			if (thiscam->momz > 0)
			{
				// hit the ceiling
				thiscam->momz = 0;
			}

			thiscam->z = thiscam->ceilingz - thiscam->height;
		}
	}

	if (thiscam->ceilingz - thiscam->z < thiscam->height
		&& thiscam->ceilingz >= thiscam->z)
	{
		thiscam->ceilingz = thiscam->z + thiscam->height;
		thiscam->floorz = thiscam->z;
	}

	// Are we in water?
	if (P_CameraCheckWater(thiscam))
		postimgtype = (postimgtype & ~postimg_heat)|(postimgtype & ~postimg_freeze)|postimg_water;
	else if (P_CameraCheckHeat(thiscam))
		postimgtype = (postimgtype & ~postimg_water)|(postimgtype & ~postimg_freeze)|postimg_heat;
}

//
// P_PlayerMobjThinker
//
// Called every game tic, note that this function does not need mobj->player and only mobj to work
static void P_PlayerMobjThinker(mobj_t *mobj) // SRB2CBTODO: Make sure floorz and ceilingz gets synced here
{
	msecnode_t *node;

	if (!mobj)
		I_Error("P_PlayerMobjThinker: Null player mobj!");

	// NOTE: Always check for mobj->player, because the game won't always
	// have that in this function

	// Make sure player shows dead
	if (mobj->health <= 0)
	{
		if (mobj->state == &states[S_DISS])
		{
			P_RemoveMobj(mobj);
			return;
		}

		P_SetPlayerMobjState(mobj, S_PLAY_DIE3);
		mobj->flags2 &= ~MF2_DONTDRAW;
		P_PlayerZMovement(mobj);
		return;
	}

	P_MobjCheckWater(mobj);

	// momentum movement
	mobj->eflags &= ~MFE_JUSTSTEPPEDDOWN;


#ifdef HUDFADE

	// The player is moving
	if (mobj->player && (mobj->player == &players[consoleplayer])) // SRB2CBTODO: Make sure that this only affects the view of the correct player
	{
		if (!(mobj->player->pflags & PF_NIGHTSMODE) && !mobj->player->exiting && (abs(mobj->momx) + abs(mobj->momy) > 4))
		{
			mobj->player->powers[pw_laststill] = 0;

			if (mobj->player->powers[pw_movingtime] < 108*NEWTICRATERATIO)
				mobj->player->powers[pw_movingtime] += 2/NEWTICRATERATIO;

			if (mobj->player->powers[pw_movingtime] >= 106*NEWTICRATERATIO)
			{
				if (mobj->player->powers[pw_stilltime] > 0)
					mobj->player->powers[pw_stilltime] -= 8/NEWTICRATERATIO;
			}
		}
		// The player is not moving, or the hud needs to be seen
		else
		{
			mobj->player->powers[pw_movingtime] = 0;

			if (mobj->player->powers[pw_laststill] < 16*NEWTICRATERATIO)
				mobj->player->powers[pw_laststill] += 2/NEWTICRATERATIO;

			if (mobj->player->powers[pw_laststill] >= 14*NEWTICRATERATIO)
				if (mobj->player->powers[pw_stilltime] < 248)
					mobj->player->powers[pw_stilltime] += 8/NEWTICRATERATIO;
		}

		// Start out at a multiple of the addition at the level start
		if (leveltime < 2*TICRATE)
			mobj->player->powers[pw_stilltime] = 248;

		hudtrans = mobj->player->powers[pw_stilltime];
	}
#endif


	// Zoom tube
	if (mobj->tracer && mobj->tracer->type == MT_TUBEWAYPOINT)
	{
		P_UnsetThingPosition(mobj);
		// SRB2CBTODO: UFRAME support and floorz here
		mobj->x += mobj->momx;
		mobj->y += mobj->momy;
		mobj->z += mobj->momz;
		P_SetThingPosition(mobj);
		P_CheckPosition(mobj, mobj->x, mobj->y);
		mobj->floorz = tmfloorz;
		mobj->ceilingz = tmceilingz;
		goto animonly;
	}
	else if (mobj->player && (mobj->player->pflags & PF_MACESPIN) && mobj->tracer)
	{
		P_CheckPosition(mobj, mobj->x, mobj->y);
		goto animonly;
	}

	// Needed for gravity boots
	P_CheckGravity(mobj, false);

	// If the player isn't on the ground(or on a semi-steep slope), make sure they aren't in a "starting dash" position.
	if (mobj->player && (!P_IsObjectOnGround(mobj)))
	{
		mobj->player->pflags &= ~PF_STARTDASH;
		mobj->player->dashspeed = 0;
	}

	// Sliding down steep slopes
#ifdef VPHYSICS
	if (mobj->subsector->sector->f_slope && P_IsObjectOnGround(mobj))
	{
		v3float_t vector = mobj->subsector->sector->f_slope->normalf;

#if 0
		if ((mobj->subsector->sector->f_slope->zangle > 25) || (mobj->player && (mobj->player->pflags & PF_SPINNING))) // Start sliding at steep angles
		{
			mobj->momx += FLOAT_TO_FIXED(vector.x);
			mobj->momy += FLOAT_TO_FIXED(vector.y);
		}
#endif

		// Always move down a hill while spinning
		if (mobj->player && (mobj->player->pflags & PF_SPINNING))
		{
			// A little extra speed ;)
			mobj->momx += FLOAT_TO_FIXED(vector.x*1.5f);
			mobj->momy += FLOAT_TO_FIXED(vector.y*1.5f);

			// Spinning fast? MOAHR SPEED (not physically acurate, but it IS fun :D)
			if (P_AproxDistance(mobj->momx, mobj->momy)>>FRACBITS > 20)
			{
				mobj->momx += FLOAT_TO_FIXED(vector.x*(mobj->player->speed/15.0f));
				mobj->momy += FLOAT_TO_FIXED(vector.y*(mobj->player->speed/15.0f));
			}
		}

#if 0
		if (mobj->pitchangle/(ANG45/45) > 0) // You're going downhill (running or spinning)
		{
			float vadd = (1.0f+mobj->pitchangle/(ANG45/45)/30.0f);

			fixed_t vspeed = P_AproxDistance(mobj->momx, mobj->momy)>>FRACBITS;
			if (vadd > 1.15f) // If the vadd is too low, you don't gain much momentum from this slope
			{
				mobj->momx += FLOAT_TO_FIXED(vector.x*vadd);
				mobj->momy += FLOAT_TO_FIXED(vector.y*vadd);

				if (vspeed > 35) // Running fast? MOAHR - ER SPEED!!!!
				{
					mobj->momx += FLOAT_TO_FIXED(vector.x);
					mobj->momy += FLOAT_TO_FIXED(vector.y);
				}
			}
		}
#endif
	}
#endif


	// EXTREMELY Overly complicated (and thankfully unneeded) angle-based code is GONE :D

#ifdef VPHYSICS
	v3float_t mobjvec;
		// Way easier and less complicated VECTOR BASED CODE!!! YESS!!
		if (mobj->subsector->sector->f_slope && P_IsObjectOnGround(mobj))
		{
			// Let's make a vector for the mobj!
			v3float_t point1;
			v3float_t point2;

			point1.x = FIXED_TO_FLOAT(mobj->x);
			point1.y = FIXED_TO_FLOAT(mobj->y);
			point1.z = FIXED_TO_FLOAT(mobj->z);

			fixed_t mangle = mobj->angle>>ANGLETOFINESHIFT;

			pslope_t* cslope = mobj->subsector->sector->f_slope;

			fixed_t addx, addy;
			addx = FixedMul(200*FRACUNIT, FINECOSINE(mangle));
			addy = FixedMul(200*FRACUNIT, FINESINE(mangle));

			// Make a vector that's level to the ground (NOT LEVEL TO THE SLOPE),
			// that way we can get the mobj's pitchangle based on
			// the angle between the mobj's vector and the slope's normal
			point2.x = FIXED_TO_FLOAT(mobj->x+addx);
			point2.y = FIXED_TO_FLOAT(mobj->y+addy);
			point2.z = FIXED_TO_FLOAT(mobj->z);//P_GetZAtf(cslope, point2.x, point2.y); // TODO: Use this for cool effects like going off a slope


			mobjvec = *M_MakeVec3f(&point1, &point2, &mobjvec);

			fixed_t pangle = FV_AngleBetweenVectorsf(&cslope->normalf, &mobjvec)* 180 / M_PI;
			pangle -= 90; // Adjust pitch angle to correct orientation

			mobj->pitchangle = pangle*(ANG45/45);
			//mobj->rollangle = pangle*(ANG45/45);
			//P_SetMobjRoll(mobj, (10)*(ANG45/45), 3); // VPHYSICS
			//P_SetMobjRoll(mobj, (pangle)*(ANG45/45), 0);
		}
		else
			mobj->pitchangle = 0; // Otherwise, you're on flat ground TODO: Make a smoother transistion like spriteroll





#if 0 // Ug... rotate the entire model :<
	if (mobj->subsector->sector->f_slope && P_IsObjectOnGround(mobj))
	{

		//v3float_t Vector1 = mobj->subsector->sector->f_slope->alignf;
		//fixed_t pangle = FV_AngleBetweenVectorsf(&mobj->subsector->sector->f_slope->normalf, &mobjvec)* 180 / M_PI;


		fixed_t mobja = mobj->angle/(ANG45/45);



		// must = 25 when angle is 0
		// and 0 when angle is 180
		mobj->rollangle = ((25.0f/360.0f)*mobja)*(ANG45/45);
	}
	else if (!mobj->subsector->sector->f_slope && P_IsObjectOnGround(mobj))
		P_SetMobjRoll(mobj, 0, 0);
#endif







#endif

	// Modify the object's momentum to a steep slope
	if (!(mobj->momx || mobj->momy))
	{
		if (P_IsObjectOnSlope(mobj, false))
		{
			v3float_t vector = mobj->subsector->sector->f_slope->normalf;

			if (mobj->subsector->sector->f_slope->zangle > 55) // The slope is too step, so you can't stay on it
			{
				mobj->momx += FLOAT_TO_FIXED(vector.x);
				mobj->momy += FLOAT_TO_FIXED(vector.y);
			}
		}
	}

	if (mobj->momx || mobj->momy)
	{
		// Modify the object's momentum to the slope
		if (P_IsObjectOnSlope(mobj, false))
		{
			v3float_t vector = mobj->subsector->sector->f_slope->normalf;

			if (mobj->subsector->sector->f_slope->zangle > 10)
			{
				mobj->momx += FLOAT_TO_FIXED(vector.x);
				mobj->momy += FLOAT_TO_FIXED(vector.y);
			}
		}

		P_XYMovement(mobj);

		// check after XYmovement had already been done
#ifdef ESLOPE // Speed limit protection so objects can't go inside of a slope
		if (mobj->subsector->sector && mobj->subsector->sector->f_slope)
		{
			if (mobj->z < P_GetZAt(mobj->subsector->sector->f_slope, mobj->x, mobj->y))
				mobj->z = P_GetZAt(mobj->subsector->sector->f_slope, mobj->x, mobj->y);
			if (mobj->floorz < P_GetZAt(mobj->subsector->sector->f_slope, mobj->x, mobj->y))
				mobj->floorz = P_GetZAt(mobj->subsector->sector->f_slope, mobj->x, mobj->y);
		}
		if (mobj->subsector->sector && mobj->subsector->sector->c_slope)
		{
			if (mobj->z > P_GetZAt(mobj->subsector->sector->c_slope, mobj->x, mobj->y))
				mobj->z = P_GetZAt(mobj->subsector->sector->c_slope, mobj->x, mobj->y);
			if (mobj->ceilingz > P_GetZAt(mobj->subsector->sector->c_slope, mobj->x, mobj->y))
				mobj->ceilingz = P_GetZAt(mobj->subsector->sector->c_slope, mobj->x, mobj->y);
		}
#endif

		if (mobj->thinker.function.acv == P_RemoveThinkerDelayed)
			return; // mobj was removed
	}
	else
		P_TryMove(mobj, mobj->x, mobj->y, true);

	if (!(netgame && mobj->player && mobj->player->spectator))
	{
		// Crumbling platforms
		for (node = mobj->touching_sectorlist; node; node = node->m_snext)
		{
			ffloor_t *rover;

			for (rover = node->m_sector->ffloors; rover; rover = rover->next)
			{
				if (!(rover->flags & FF_EXISTS))
					continue;

				if ((rover->flags & FF_CRUMBLE)
					&& ((*rover->topheight == mobj->z && !(mobj->eflags & MFE_VERTICALFLIP))
						|| (*rover->bottomheight == mobj->z + mobj->height && (mobj->eflags & MFE_VERTICALFLIP)))) // You nut. // SRB2CBTODO: What is this?
					EV_StartCrumble(rover->master->frontsector, rover, (rover->flags & FF_FLOATBOB), mobj->player, rover->alpha, !(rover->flags & FF_NORETURN));
			}
		}
	}

	// Check for floating water platforms and bounce them
	if (CheckForFloatBob && mobj->momz < 0)
	{
		fixed_t watertop;
		fixed_t waterbottom;
		boolean roverfound;

		watertop = waterbottom = 0;
		roverfound = false;

		for (node = mobj->touching_sectorlist; node; node = node->m_snext)
		{
			if (node->m_sector->ffloors)
			{
				ffloor_t *rover;
				// Get water boundaries first
				for (rover = node->m_sector->ffloors; rover; rover = rover->next)
				{
					if (!(rover->flags & FF_EXISTS))
						continue;

					if (rover->flags & FF_SWIMMABLE) // Is there water?
					{
						watertop = *rover->topheight;
						waterbottom = *rover->bottomheight;
						roverfound = true;
						break;
					}
				}
			}
		}
		if (watertop)
		{
			for (node = mobj->touching_sectorlist; node; node = node->m_snext)
			{
				if (node->m_sector->ffloors)
				{
					ffloor_t *rover;
					for (rover = node->m_sector->ffloors; rover; rover = rover->next)
					{
						if (!(rover->flags & FF_EXISTS))
							continue;

						if (rover->flags & FF_FLOATBOB
							&& *rover->topheight <= mobj->z+abs(mobj->momz)
							&& *rover->topheight >= mobj->z-abs(mobj->momz)) // The player is landing on the cheese!
						{
							// Initiate a 'bouncy' elevator function
							// which slowly diminishes.
							EV_BounceSector(rover->master->frontsector, mobj->momz, rover->master);
						}
					}
				}
			}
		} // Ugly ugly billions of braces! Argh!
	}

	// always do the gravity bit now, that's simpler
	// BUT CheckPosition only if wasn't done before.
	if (!(mobj->eflags & MFE_ONGROUND) || mobj->momz // SRB2CBTODO: Momz too?
		|| ((mobj->eflags & MFE_VERTICALFLIP) && mobj->z + mobj->height != mobj->ceilingz)
		|| (!(mobj->eflags & MFE_VERTICALFLIP) && mobj->z != mobj->floorz))
	{
		mobj_t *onmo;
		onmo = P_CheckOnmobj(mobj);
		if (!onmo)
		{
			P_PlayerZMovement(mobj);
			P_CheckPosition(mobj, mobj->x, mobj->y); // Need this to pick up objects!
			if (mobj->flags2 & MF2_ONMOBJ)
				mobj->flags2 &= ~MF2_ONMOBJ;
		}
		else
		{
			if (mobj->momz < -8*FRACUNIT)
				PlayerLandedOnThing(mobj, onmo);
			if (onmo->z + onmo->height - mobj->z <= 24*FRACUNIT)
			{
				mobj->player->viewheight -= onmo->z+onmo->height
					-mobj->z;
				mobj->player->deltaviewheight =
					(VIEWHEIGHT-mobj->player->viewheight)>>3;
				mobj->z = onmo->z+onmo->height;
				mobj->flags2 |= MF2_ONMOBJ;
				mobj->momz = 0;
			}
			else // hit the bottom of the blocking mobj
				mobj->momz = 0;
		}

		if (mobj->thinker.function.acv == P_RemoveThinkerDelayed)
			return; // mobj was removed
	}
	else
		mobj->eflags &= ~MFE_JUSTHITFLOOR;

animonly:
	// cycle through states,
	// calling action functions at transitions
	if (mobj->tics != -1)
	{
		mobj->tics--;

		// you can cycle through multiple states in a tic
		if (!mobj->tics)
			if (!P_SetPlayerMobjState(mobj, mobj->state->nextstate))
				return; // freed itself
	}
}

static void CalculatePrecipFloor(precipmobj_t *mobj)
{
	// Recalculate floorz each time
	const sector_t *mobjsecsubsec;
	if (mobj && mobj->subsector && mobj->subsector->sector)
		mobjsecsubsec = mobj->subsector->sector;
	else
		return;
#ifdef ESLOPE
	// NOTE: P_GetMobjZAtF can't be used here
	mobj->floorz = (mobjsecsubsec->f_slope ?
	 P_GetZAt(mobjsecsubsec->f_slope, mobj->x, mobj->y):
	 mobjsecsubsec->floorheight);
#else
	mobj->floorz = mobjsecsubsec->floorheight;
#endif

	if (mobjsecsubsec->ffloors)
	{
		ffloor_t *rover;

		for (rover = mobjsecsubsec->ffloors; rover; rover = rover->next)
		{
			// If it exists, it'll get rained on.
			if (!(rover->flags & FF_EXISTS))
				continue;

			if (!(rover->flags & FF_BLOCKOTHERS) && !(rover->flags & FF_SWIMMABLE))
				continue;

			if (*rover->topheight > mobj->floorz)
				mobj->floorz = *rover->topheight;
		}
	}
}

void P_RecalcPrecipInSector(sector_t *sector)
{
	mprecipsecnode_t *psecnode;

	if (!sector)
		return;

	sector->moved = true; // Recalc lighting and things too, maybe

	for (psecnode = sector->touching_preciplist; psecnode; psecnode = psecnode->m_snext)
		CalculatePrecipFloor(psecnode->m_thing);
}

//
// P_NullPrecipThinker
//
// For "Blank" precipitation
//
void P_NullPrecipThinker(precipmobj_t *mobj)
{
	(void)mobj;
}

// NOTE: A very important thing to know is that precip actually goes
// back to the top of where it spawned so that new precip does not have
// to be regenerated once it hits the floor
void P_SnowThinker(precipmobj_t *mobj)
{
	if (P_FreezeObjectplace())
		return;

	// adjust height
	mobj->z += mobj->momz;

	if (mobj->z <= mobj->floorz) // Precipmobjs can't use P_IsObjectOnGround
		mobj->z = mobj->subsector->sector->ceilingheight;

	return;
}

void P_RainThinker(precipmobj_t *mobj)
{
	if (P_FreezeObjectplace())
		return;

	// adjust height
	mobj->z += mobj->momz;

	if (mobj->state != &states[S_RAIN1])
	{
		// cycle through states,
		// calling action functions at transitions
		if (mobj->tics != -1)
		{
			mobj->tics--;

			// you can cycle through multiple states in a tic
			if (!mobj->tics)
				if (!P_SetPrecipMobjState(mobj, mobj->state->nextstate))
					return; // freed itself
		}

		if (mobj->state == &states[S_RAINRETURN])
		{
			mobj->z = mobj->subsector->sector->ceilingheight;
			mobj->momz = mobjinfo[MT_RAIN].speed/NEWTICRATERATIO;
			P_SetPrecipMobjState(mobj, S_RAIN1);
		}
	}
	// Precipmobjs can't use P_IsObjectOnGround
	else if (mobj->z <= mobj->floorz && mobj->momz)
	{
		// No splashes on sky or bottomless pits
		if (mobj->z <= mobj->subsector->sector->floorheight
			&& (GETSECSPECIAL(mobj->subsector->sector->special, 1) == 7 || GETSECSPECIAL(mobj->subsector->sector->special, 1) == 6
			|| mobj->subsector->sector->floorpic == skyflatnum))
			mobj->z = mobj->subsector->sector->ceilingheight;
		else
		{
			mobj->momz = 0;
			mobj->z = mobj->floorz;
			P_SetPrecipMobjState(mobj, S_SPLASH1);
		}
	}

	return;
}

static void P_RingThinker(mobj_t *mobj)
{
	if (mobj->momx || mobj->momy)
	{
		P_RingXYMovement(mobj);

		if (mobj->thinker.function.acv == P_RemoveThinkerDelayed)
			return; // mobj was removed
	}

	// always do the gravity bit now, that's simpler
	// BUT CheckPosition only if wasn't done before.
	if (mobj->momz)
	{
		P_RingZMovement(mobj);
		P_CheckPosition(mobj, mobj->x, mobj->y); // Need this to pick up objects!

		if (mobj->thinker.function.acv == P_RemoveThinkerDelayed)
			return; // mobj was removed
	}

	P_CycleMobjState(mobj);
}

//
// P_Look4Players
// If allaround is false, only look 180 degrees in front.
// Returns true if a player is targeted.
//
static boolean P_Look4Players(mobj_t *actor, boolean allaround)
{
	int stop, c = 0;
	player_t *player;
	sector_t *sector;
	angle_t an;
	fixed_t dist;

	sector = actor->subsector->sector;

	// first time init, this allow minimum lastlook changes
	if (actor->lastlook < 0)
		actor->lastlook = P_Random();

	actor->lastlook %= MAXPLAYERS;

	stop = (actor->lastlook-1) & PLAYERSMASK;

	for (; ; actor->lastlook = (actor->lastlook+1) & PLAYERSMASK)
	{
		// done looking
		if (actor->lastlook == stop)
			return false;

		if (!playeringame[actor->lastlook])
			continue;

		if (c++ == 2)
			return false;

		player = &players[actor->lastlook];

		if (player->health <= 0)
			continue; // dead

		if (!player->mo)
			continue;

		if (!P_CheckSight(actor, player->mo))
			continue; // out of sight

		if (!allaround)
		{
			an = R_PointToAngle2(actor->x, actor->y, player->mo->x, player->mo->y) - actor->angle;

			if (an > ANG90 && an < ANG270)
			{
				dist = P_AproxDistance(player->mo->x - actor->x, player->mo->y - actor->y);
				// if real close, react anyway
				if (dist > MELEERANGE)
					continue; // behind back
			}
		}

		P_SetTarget(&actor->target, player->mo);
		return true;
	}

	//return false;
}

// Finds the player no matter what they're hiding behind (even lead!)
boolean P_SupermanLook4Players(mobj_t *actor)
{
	int c, stop = 0;
	player_t *playersinthegame[MAXPLAYERS];

	for (c = 0; c < MAXPLAYERS; c++)
	{
		if (playeringame[c])
		{
			if (players[c].health <= 0)
				continue; // dead

			if (!players[c].mo)
				continue;

			playersinthegame[stop] = &players[c];
			stop++;
		}
	}

	if (!stop)
		return false;

	P_SetTarget(&actor->target, playersinthegame[P_Random()%stop]->mo);
	return true;
}

// AI for a generic boss.
static void P_GenericBossThinker(mobj_t *mobj)
{
	if (mobj->state->nextstate == mobj->info->spawnstate && mobj->tics == 1)
	{
		mobj->flags2 &= ~MF2_FRET;
		mobj->flags &= ~MF_TRANSLATION;
	}

	if (!mobj->target || !(mobj->target->flags & MF_SHOOTABLE))
	{
		if (mobj->health <= 0)
		{
			// look for a new target
			if (P_Look4Players(mobj, true) && mobj->info->mass) // Bid farewell!
				S_StartSound(mobj, mobj->info->mass);
			return;
		}

		// look for a new target
		if (P_Look4Players(mobj, true) && mobj->info->seesound)
			S_StartSound(mobj, mobj->info->seesound);

		return;
	}

	if (mobj->state == &states[mobj->info->spawnstate])
		A_Boss1Chase(mobj);

	if (mobj->state == &states[mobj->info->meleestate]
		|| (mobj->state == &states[mobj->info->missilestate]
		&& mobj->health > mobj->info->damage))
	{
		mobj->angle = R_PointToAngle2(mobj->x, mobj->y, mobj->target->x, mobj->target->y);
	}
}

// AI for the first boss.
static void P_Boss1Thinker(mobj_t *mobj)
{
	if (mobj->state->nextstate == mobj->info->spawnstate && mobj->tics == 1)
	{
		mobj->flags2 &= ~MF2_FRET;
		mobj->flags &= ~MF_TRANSLATION;
	}

	if (!mobj->tracer)
	{
		var1 = 0;
		A_BossJetFume(mobj);
	}

	if (!mobj->target || !(mobj->target->flags & MF_SHOOTABLE))
	{
		if (mobj->health <= 0)
		{
			if (P_Look4Players(mobj, true) && mobj->info->mass) // Bid farewell!
				S_StartSound(mobj, mobj->info->mass);
			return;
		}

		// look for a new target
		if (P_Look4Players(mobj, true) && mobj->info->seesound)
			S_StartSound(mobj, mobj->info->seesound);

		return;
	}

	if (mobj->state == &states[mobj->info->spawnstate])
		A_Boss1Chase(mobj);

	if (mobj->state == &states[mobj->info->meleestate]
		|| (mobj->state == &states[mobj->info->missilestate]
		&& mobj->health > mobj->info->damage))
	{
		mobj->angle = R_PointToAngle2(mobj->x, mobj->y, mobj->target->x, mobj->target->y);
	}
}

// AI for the second boss.
// No, it does NOT convert "Boss" to a "Thinker". =P
static void P_Boss2Thinker(mobj_t *mobj)
{
	if (mobj->movecount)
		mobj->movecount--;

	if (!(mobj->movecount))
	{
		mobj->flags2 &= ~MF2_FRET;
		mobj->flags &= ~MF_TRANSLATION;
	}

	if (!mobj->tracer
#ifdef CHAOSISNOTDEADYET
		&& gametype != GT_CHAOS
#endif
		)
	{
		var1 = 0;
		A_BossJetFume(mobj);
	}

	if (mobj->health <= mobj->info->damage && (!mobj->target || !(mobj->target->flags & MF_SHOOTABLE)))
	{
		if (mobj->health <= 0)
		{
			// look for a new target
			if (P_Look4Players(mobj, true) && mobj->info->mass) // Bid farewell!
				S_StartSound(mobj, mobj->info->mass);
			return;
		}

		// look for a new target
		if (P_Look4Players(mobj, true) && mobj->info->seesound)
			S_StartSound(mobj, mobj->info->seesound);

		return;
	}

#ifdef CHAOSISNOTDEADYET
	if (gametype == GT_CHAOS && (mobj->state == &states[S_EGGMOBILE2_POGO1]
		|| mobj->state == &states[S_EGGMOBILE2_POGO2]
		|| mobj->state == &states[S_EGGMOBILE2_POGO3]
		|| mobj->state == &states[S_EGGMOBILE2_POGO4]
		|| mobj->state == &states[S_EGGMOBILE2_STND])) // Chaos mode, he pogos only
	{
		mobj->flags &= ~MF_NOGRAVITY;
		A_Boss2Pogo(mobj);
	}
	else if (gametype != GT_CHAOS)
#endif
	{
		if (mobj->state == &states[mobj->info->spawnstate] && mobj->health > mobj->info->damage)
			A_Boss2Chase(mobj);
		else if (mobj->state == &states[mobj->info->raisestate]
			|| mobj->state == &states[S_EGGMOBILE2_POGO2]
			|| mobj->state == &states[S_EGGMOBILE2_POGO3]
			|| mobj->state == &states[S_EGGMOBILE2_POGO4]
			|| mobj->state == &states[mobj->info->spawnstate])
		{
			mobj->flags &= ~MF_NOGRAVITY;
			A_Boss2Pogo(mobj);
		}
	}
}

// AI for the third boss.
static void P_Boss3Thinker(mobj_t *mobj)
{
	if (mobj->state->nextstate == mobj->info->spawnstate && mobj->tics == 1)
	{
		mobj->flags2 &= ~MF2_FRET;
		mobj->flags &= ~MF_TRANSLATION;
	}

	if (mobj->flags2 & MF2_FRET)
	{
		mobj->movedir = 1;
		if (mobj->health <= mobj->info->damage)
		{
			var1 = 100;
			var2 = 0;
			A_LinedefExecute(mobj);
		}
	}

	if (mobj->movefactor > ORIG_FRICTION_FACTOR)
	{
		mobj->movefactor--;
		return;
	}

	if (!mobj->tracer)
	{
		var1 = 1;
		A_BossJetFume(mobj);
	}

	if (mobj->health <= 0)
	{
		mobj->movecount = 0;
		mobj->reactiontime = 0;

		if (mobj->state < &states[mobj->info->xdeathstate])
			return;

		if (mobj->threshold == -1)
		{
			mobj->momz = mobj->info->speed;
			return;
		}
	}

	if (mobj->reactiontime && mobj->health > mobj->info->damage) // Shock mode
	{
		unsigned int i;

		if (mobj->state != &states[mobj->info->spawnstate])
			P_SetMobjState(mobj, mobj->info->spawnstate);

		if (leveltime % 2*TICRATE == 0)
		{
			ffloor_t *rover;

			// Shock the water
			for (i = 0; i < MAXPLAYERS; i++)
			{
				if (!playeringame[i])
					continue;

				if (!players[i].mo)
					continue;

				if (players[i].mo->health <= 0)
					continue;

				if (players[i].mo->eflags & MFE_UNDERWATER)
					P_DamageMobj(players[i].mo, mobj, mobj, 1);
			}

			// Make the water flash
			for (i = 0; i < numsectors; i++)
			{
				if (!sectors[i].ffloors)
					continue;

				for (rover = sectors[i].ffloors; rover; rover = rover->next)
				{
					if (!(rover->flags & FF_EXISTS))
						continue;

					if (!(rover->flags & FF_SWIMMABLE))
						continue;

					P_SpawnLightningFlash(rover->master->frontsector);
					break;
				}
			}

			if (leveltime % 35 == 0)
				S_StartSound(0, sfx_buzz1);
		}

		// If in the center, check to make sure
		// none of the players are in the water
		for (i = 0; i < MAXPLAYERS; i++)
		{
			if (!playeringame[i])
				continue;

			if (!players[i].mo)
				continue;

			if (players[i].mo->health <= 0)
				continue;

			if (players[i].mo->eflags & MFE_UNDERWATER)
				return; // Stay put
		}

		mobj->reactiontime = 0;
	}
	else if (mobj->movecount) // Firing mode
	{
		unsigned int i;

		// look for a new target
		P_Look4Players(mobj, true);

		if (!mobj->target || !mobj->target->player)
			return;

		// Are there any players underwater? If so, shock them!
		for (i = 0; i < MAXPLAYERS; i++)
		{
			if (!playeringame[i])
				continue;

			if (!players[i].mo)
				continue;

			if (players[i].mo->health <= 0)
				continue;

			if (players[i].mo->eflags & MFE_UNDERWATER)
			{
				mobj->movecount = 0;
				P_SetMobjState(mobj, mobj->info->spawnstate);
				break;
			}
		}

		// Always face your target.
		A_FaceTarget(mobj);

		// Check if the attack animation is running. If not, play it.
		if (mobj->state < &states[mobj->info->missilestate] || mobj->state > &states[mobj->info->raisestate])
			P_SetMobjState(mobj, mobj->info->missilestate);
	}
	else if (mobj->threshold >= 0) // Traveling mode
	{
		thinker_t *th;
		mobj_t *mo2;
		fixed_t dist, dist2;
		fixed_t speed;

		P_SetTarget(&mobj->target, NULL);

		if (mobj->state != &states[mobj->info->spawnstate] && mobj->health > 0
			&& !(mobj->flags2 & MF2_FRET))
			P_SetMobjState(mobj, mobj->info->spawnstate);

		// scan the thinkers
		// to find a point that matches
		// the number
		for (th = thinkercap.next; th != &thinkercap; th = th->next)
		{
			if (th->function.acp1 != (actionf_p1)P_MobjThinker)
				continue;

			mo2 = (mobj_t *)th;
			if (mo2->type == MT_BOSS3WAYPOINT && mo2->spawnpoint && mo2->spawnpoint->angle == mobj->threshold)
			{
				P_SetTarget(&mobj->target, mo2);
				break;
			}
		}

		if (!mobj->target) // Should NEVER happen
		{
			CONS_Printf("Error: Boss 3 was unable to find specified waypoint: %ld\n", mobj->threshold);
			return;
		}

		dist = P_AproxDistance(P_AproxDistance(mobj->target->x - mobj->x, mobj->target->y - mobj->y), mobj->target->z - mobj->z);

		if (dist < 1)
			dist = 1;

		if ((mobj->movedir) || (mobj->health <= mobj->info->damage))
			speed = mobj->info->speed * 2;
		else
			speed = mobj->info->speed;

		mobj->momx = FixedMul(FixedDiv(mobj->target->x - mobj->x, dist), speed);
		mobj->momy = FixedMul(FixedDiv(mobj->target->y - mobj->y, dist), speed);
		mobj->momz = FixedMul(FixedDiv(mobj->target->z - mobj->z, dist), speed);

		mobj->angle = R_PointToAngle(mobj->momx, mobj->momy);

		dist2 = P_AproxDistance(P_AproxDistance(mobj->target->x - (mobj->x + mobj->momx), mobj->target->y - (mobj->y + mobj->momy)), mobj->target->z - (mobj->z + mobj->momz));

		if (dist2 < 1)
			dist2 = 1;

		if ((dist >> FRACBITS) <= (dist2 >> FRACBITS))
		{
			// If further away, set XYZ of mobj to waypoint location
			P_UnsetThingPosition(mobj);
			mobj->x = mobj->target->x;
			mobj->y = mobj->target->y;
			mobj->z = mobj->target->z;
			mobj->momx = mobj->momy = mobj->momz = 0;
			P_SetThingPosition(mobj);

			if (mobj->threshold == 0)
			{
				mobj->reactiontime = 1; // Bzzt! Shock the water!
				mobj->movedir = 0;

				if (mobj->health <= 0)
				{
					mobj->flags |= MF_NOGRAVITY|MF_NOCLIP;
					mobj->flags |= MF_NOCLIPHEIGHT;
					mobj->threshold = -1;
					return;
				}
			}

			// Set to next waypoint in sequence
			if (mobj->target->spawnpoint)
			{
				// From the center point, choose one of the five paths
				if (mobj->target->spawnpoint->angle == 0)
					mobj->threshold = (P_Random()%5) + 1;
				else
					mobj->threshold = mobj->target->spawnpoint->extrainfo;

				// If the deaf flag is set, go into firing mode
				if (mobj->target->spawnpoint->options & MTF_AMBUSH)
				{
					if (mobj->health <= mobj->info->damage)
						mobj->movefactor = ORIG_FRICTION_FACTOR + 5*TICRATE;
					else
						mobj->movecount = 1;
				}
			}
			else // This should never happen, as well
				CONS_Printf("Error: Boss 3 waypoint has no spawnpoint associated with it.\n");
		}
	}
}

// AI for the 4th boss
static void P_Boss4Thinker(mobj_t *mobj)
{
	(void)mobj; //no AI for now
}

// AI for Black Eggman
//
// Sorry for all of the code copypaste from p_enemy.c
// Maybe someone can un-static a lot of this stuff so it
// can be called from here?
//
// Note: You CANNOT have more than ONE Black Eggman
// in a level! Just don't try it!
//
//
typedef enum
{
	DI_NODIR = -1,
	DI_EAST = 0,
	DI_NORTHEAST = 1,
	DI_NORTH = 2,
	DI_NORTHWEST = 3,
	DI_WEST = 4,
	DI_SOUTHWEST = 5,
	DI_SOUTH = 6,
	DI_SOUTHEAST = 7,
	NUMDIRS = 8,
} dirtype_t;

//
// P_NewChaseDir related LUT.
//
static dirtype_t opposite[] =
{
	DI_WEST, DI_SOUTHWEST, DI_SOUTH, DI_SOUTHEAST,
	DI_EAST, DI_NORTHEAST, DI_NORTH, DI_NORTHWEST, DI_NODIR
};

static dirtype_t diags[] =
{
	DI_NORTHWEST, DI_NORTHEAST, DI_SOUTHWEST, DI_SOUTHEAST
};

static const fixed_t xspeed[NUMDIRS] = {FRACUNIT, 47000, 0, -47000, -FRACUNIT, -47000, 0, 47000};
static const fixed_t yspeed[NUMDIRS] = {0, 47000, FRACUNIT, 47000, 0, -47000, -FRACUNIT, -47000};

/** Moves an actor in its current direction.
  *
  * \param actor Actor object to move.
  * \return False if the move is blocked, otherwise true.
  * // SRB2CBTODO: Can't a mobj's momxy just be used?
  */
static boolean P_Move(mobj_t *actor, fixed_t speed)
{
	fixed_t tryx, tryy;
	dirtype_t movedir = actor->movedir;

	if (movedir == DI_NODIR || !actor->health)
		return false;

	I_Assert((unsigned int)movedir < 8);

	tryx = actor->x + speed*xspeed[movedir];
	tryy = actor->y + speed*yspeed[movedir];

	if (!P_TryMove(actor, tryx, tryy, false))
	{
		if (actor->flags & MF_FLOAT && floatok)
		{
			// must adjust height
			if (actor->z < tmfloorz)
				actor->z += MOBJFLOATSPEED;
			else
				actor->z -= MOBJFLOATSPEED;

			actor->flags2 |= MF2_INFLOAT;
			return true;
		}

		return false;
	}
	else
		actor->flags2 &= ~MF2_INFLOAT;

	return true;
}

/** Attempts to move an actor on in its current direction.
  * If the move succeeds, the actor's move count is reset
  * randomly to a value from 0 to 15.
  *
  * \param actor Actor to move.
  * \return True if the move succeeds, false if the move is blocked.
  */
static boolean P_TryWalk(mobj_t *actor)
{
	if (!P_Move(actor, actor->info->speed))
		return false;
	actor->movecount = P_Random() & 15;
	return true;
}

static void P_NewChaseDir(mobj_t *actor)
{
	fixed_t deltax, deltay;
	dirtype_t d[3];
	dirtype_t tdir = DI_NODIR, olddir, turnaround;

#ifdef PARANOIA
	if (!actor->target)
		I_Error("P_NewChaseDir: called with no target");
#endif

	olddir = actor->movedir;

	if (olddir >= NUMDIRS)
		olddir = DI_NODIR;

	if (olddir != DI_NODIR)
		turnaround = opposite[olddir];
	else
		turnaround = olddir;

	deltax = actor->target->x - actor->x;
	deltay = actor->target->y - actor->y;

	if (deltax > 10*FRACUNIT)
		d[1] = DI_EAST;
	else if (deltax < -10*FRACUNIT)
		d[1] = DI_WEST;
	else
		d[1] = DI_NODIR;

	if (deltay < -10*FRACUNIT)
		d[2] = DI_SOUTH;
	else if (deltay > 10*FRACUNIT)
		d[2] = DI_NORTH;
	else
		d[2] = DI_NODIR;

	// try direct route
	if (d[1] != DI_NODIR && d[2] != DI_NODIR)
	{
		dirtype_t newdir = diags[((deltay < 0)<<1) + (deltax > 0)];

		actor->movedir = newdir;
		if ((newdir != turnaround) && P_TryWalk(actor))
			return;
	}

	// try other directions
	if (P_Random() > 200 || abs(deltay) > abs(deltax))
	{
		tdir = d[1];
		d[1] = d[2];
		d[2] = tdir;
	}

	if (d[1] == turnaround)
		d[1] = DI_NODIR;
	if (d[2] == turnaround)
		d[2] = DI_NODIR;

	if (d[1] != DI_NODIR)
	{
		actor->movedir = d[1];

		if (P_TryWalk(actor))
			return; // either moved forward or attacked
	}

	if (d[2] != DI_NODIR)
	{
		actor->movedir = d[2];

		if (P_TryWalk(actor))
			return;
	}

	// there is no direct path to the player, so pick another direction.
	if (olddir != DI_NODIR)
	{
		actor->movedir =olddir;

		if (P_TryWalk(actor))
			return;
	}

	// randomly determine direction of search
	if (P_Random() & 1)
	{
		for (tdir = DI_EAST; tdir <= DI_SOUTHEAST; tdir++)
		{
			if (tdir != turnaround)
			{
				actor->movedir = tdir;

				if (P_TryWalk(actor))
					return;
			}
		}
	}
	else
	{
		for (tdir = DI_SOUTHEAST; tdir >= DI_EAST; tdir--)
		{
			if (tdir != turnaround)
			{
				actor->movedir = tdir;

				if (P_TryWalk(actor))
					return;
			}
		}
	}

	if (turnaround != DI_NODIR)
	{
		actor->movedir = turnaround;

		if (P_TryWalk(actor))
			return;
	}

	actor->movedir = (angle_t)DI_NODIR; // cannot move
}

static void P_Boss7Thinker(mobj_t *mobj)
{
	if (!mobj->target || !(mobj->target->flags & MF_SHOOTABLE))
	{
		// look for a new target
		if (P_LookForPlayers(mobj, true, false, 0))
			return; // got a new target

		P_SetMobjStateNF(mobj, mobj->info->spawnstate);
		return;
	}

	if (mobj->health >= 8 && (leveltime & 14*NEWTICRATERATIO) == 0)
		P_SpawnMobj(mobj->x, mobj->y, mobj->z + mobj->height, MT_SMOK)->momz = FRACUNIT;

	if (mobj->state == &states[S_BLACKEGG_STND] && mobj->tics == mobj->state->tics)
	{
		mobj->reactiontime += P_Random();

		if (mobj->health <= 2)
			mobj->reactiontime /= 4;
	}
	else if (mobj->state == &states[S_BLACKEGG_DIE4] && mobj->tics == mobj->state->tics)
	{
		int i;
		thinker_t *th;
		mobj_t *mo2;

		mobj->health = 0;

		// make sure there is a player alive for victory
		for (i = 0; i < MAXPLAYERS; i++)
			if (playeringame[i] && (players[i].health > 0
				|| ((netgame || multiplayer) && (players[i].lives > 0 || players[i].continues > 0))))
				break;

		if (i == MAXPLAYERS)
			return; // no one left alive, so do not end game

		// scan the remaining thinkers to see
		// if all bosses are dead
		for (th = thinkercap.next; th != &thinkercap; th = th->next)
		{
			if (th->function.acp1 != (actionf_p1)P_MobjThinker)
				continue;

			mo2 = (mobj_t *)th;
			if (mo2 != mobj && (mo2->flags & MF_BOSS) && mo2->health > 0)
				return; // other boss not dead
		}

		for (i = 0; i < MAXPLAYERS; i++)
				P_DoPlayerExit(&players[i]);

		P_SetTarget(&mobj->target, NULL);

		mobj->flags |= MF_NOCLIP;
		mobj->flags &= ~MF_SPECIAL;

		S_StartSound(0, sfx_befall);
	}
	else if (mobj->state >= &states[S_BLACKEGG_WALK1]
		&& mobj->state <= &states[S_BLACKEGG_WALK6])
	{
		// Chase
		int delta;
		int i;

		if (mobj->z != mobj->floorz)
			return;

		// Self-adjust if stuck on the edge
		if (mobj->tracer)
		{
			if (P_AproxDistance(mobj->x - mobj->tracer->x, mobj->y - mobj->tracer->y) > 128*FRACUNIT - mobj->radius)
				P_InstaThrust(mobj, R_PointToAngle2(mobj->x, mobj->y, mobj->tracer->x, mobj->tracer->y), FRACUNIT);
		}

		if (mobj->flags2 & MF2_FRET)
		{
			P_SetMobjState(mobj, S_BLACKEGG_DESTROYPLAT1);
			S_StartSound(0, sfx_s3k_34);
			mobj->flags2 &= ~MF2_FRET;
			return;
		}

		// turn towards movement direction if not there yet
		if (mobj->movedir < NUMDIRS)
		{
			mobj->angle &= (7<<29);
			delta = mobj->angle - (mobj->movedir << 29);

			if (delta > 0)
				mobj->angle -= ANG90/2;
			else if (delta < 0)
				mobj->angle += ANG90/2;
		}

		// Is a player on top of us?
		for (i = 0; i < MAXPLAYERS; i++)
		{
			if (!playeringame[i])
				continue;

			if (!players[i].mo)
				continue;

			if (players[i].mo->health <= 0)
				continue;

			if (P_AproxDistance(players[i].mo->x - mobj->x, players[i].mo->y - mobj->y) > mobj->radius)
				continue;

			if (players[i].mo->z > mobj->z + mobj->height - 2*FRACUNIT
				&& players[i].mo->z < mobj->z + mobj->height + 32*FRACUNIT)
			{
				// Punch him!
				P_SetMobjState(mobj, mobj->info->meleestate);
				S_StartSound(0, sfx_begrnd); // warning sound
				return;
			}
		}

		if (mobj->health <= 2
			&& mobj->target
			&& mobj->target->player
			&& (mobj->target->player->pflags & PF_ITEMHANG))
		{
			A_FaceTarget(mobj);
			P_SetMobjState(mobj, S_BLACKEGG_SHOOT1);
			mobj->movecount = TICRATE + P_Random()/2;
			return;
		}

		if (mobj->reactiontime)
			mobj->reactiontime--;

		if (mobj->reactiontime <= 0 && mobj->z == mobj->floorz)
		{
			// Here, we'll call P_Random() and decide what kind of attack to do
RetryAttack:
			switch(mobj->threshold)
			{
				case 0: // Lob cannon balls
					if (mobj->z < 1056*FRACUNIT)
					{
						A_FaceTarget(mobj);
						P_SetMobjState(mobj, mobj->info->xdeathstate);
						mobj->movecount = 7*TICRATE + P_Random();
					}
					else
					{
						mobj->threshold++;
						goto RetryAttack;
					}
					break;
				case 1: // Chaingun Goop
					A_FaceTarget(mobj);
					P_SetMobjState(mobj, S_BLACKEGG_SHOOT1);

					if (mobj->health > 2)
						mobj->movecount = TICRATE + P_Random()/3;
					else
						mobj->movecount = TICRATE + P_Random()/2;
					break;
				case 2: // Homing Missile
					A_FaceTarget(mobj);
					P_SetMobjState(mobj, mobj->info->missilestate);
					S_StartSound(0, sfx_beflap);
					break;
			}

			mobj->threshold++;
			mobj->threshold %= 3;
			return;
		}

		// possibly choose another target
		if (multiplayer && (mobj->target->health <= 0 || !P_CheckSight(mobj, mobj->target))
			&& P_LookForPlayers(mobj, true, false, 0))
			return; // got a new target

		if (leveltime & 1*NEWTICRATERATIO)
		{
			// chase towards player
			if (--mobj->movecount < 0 || !P_Move(mobj, mobj->info->speed))
				P_NewChaseDir(mobj);
		}
	}
	else if (mobj->state == &states[S_BLACKEGG_MISSILE3] && mobj->tics == states[S_BLACKEGG_MISSILE3].tics)
	{
		if (!mobj->target)
		{
			P_SetMobjState(mobj, mobj->info->spawnstate);
			return;
		}

		A_FaceTarget(mobj);

		P_SpawnXYZMissile(mobj, mobj->target, MT_BLACKEGGMAN_MISSILE,
			mobj->x + P_ReturnThrustX(mobj, mobj->angle-ANG90, mobj->radius/3*2+(4*FRACUNIT)),
			mobj->y + P_ReturnThrustY(mobj, mobj->angle-ANG90, mobj->radius/3*2+(4*FRACUNIT)),
			mobj->z + mobj->height/3*2);

		P_SpawnXYZMissile(mobj, mobj->target, MT_BLACKEGGMAN_MISSILE,
			mobj->x + P_ReturnThrustX(mobj, mobj->angle+ANG90, mobj->radius/3*2+(4*FRACUNIT)),
			mobj->y + P_ReturnThrustY(mobj, mobj->angle+ANG90, mobj->radius/3*2+(4*FRACUNIT)),
			mobj->z + mobj->height/3*2);

		P_SpawnXYZMissile(mobj, mobj->target, MT_BLACKEGGMAN_MISSILE,
			mobj->x + P_ReturnThrustX(mobj, mobj->angle-ANG90, mobj->radius/3*2+(4*FRACUNIT)),
			mobj->y + P_ReturnThrustY(mobj, mobj->angle-ANG90, mobj->radius/3*2+(4*FRACUNIT)),
			mobj->z + mobj->height/2);

		P_SpawnXYZMissile(mobj, mobj->target, MT_BLACKEGGMAN_MISSILE,
			mobj->x + P_ReturnThrustX(mobj, mobj->angle+ANG90, mobj->radius/3*2+(4*FRACUNIT)),
			mobj->y + P_ReturnThrustY(mobj, mobj->angle+ANG90, mobj->radius/3*2+(4*FRACUNIT)),
			mobj->z + mobj->height/2);
	}
	else if (mobj->state == &states[S_BLACKEGG_PAIN1] && mobj->tics == mobj->state->tics)
	{
		if (mobj->health > 0)
			mobj->health--;

		mobj->reactiontime /= 3;

		if (mobj->health <= 0)
		{
			int i;

			P_KillMobj(mobj, NULL, NULL);

			// It was a team effort
			for (i = 0; i < MAXPLAYERS; i++)
			{
				if (playeringame[i])
					continue;

				players[i].score += 1000;
			}
		}
	}
	else if (mobj->state == &states[S_BLACKEGG_PAIN35] && mobj->tics == 1)
	{
		if (mobj->health == 2)
		{
			// Begin platform destruction
			mobj->flags2 |= MF2_FRET;
			P_SetMobjState(mobj, mobj->info->raisestate);
		}
	}
	else if (mobj->state == &states[S_BLACKEGG_HITFACE4] && mobj->tics == mobj->state->tics)
	{
		// This is where Black Eggman hits his face.
		// If a player is on top of him, the player gets hurt.
		// But, if the player has managed to escape,
		// Black Eggman gets hurt!
		int i;
		mobj->state->nextstate = mobj->info->painstate; // Reset

		S_StartSound(0, sfx_bedeen);

		for (i = 0; i < MAXPLAYERS; i++)
		{
			if (!playeringame[i])
				continue;

			if (!players[i].mo)
				continue;

			if (players[i].mo->health <= 0)
				continue;

			if (P_AproxDistance(players[i].mo->x - mobj->x, players[i].mo->y - mobj->y) > (mobj->radius + players[i].mo->radius))
				continue;

			if (players[i].mo->z > mobj->z + mobj->height - FRACUNIT
				&& players[i].mo->z < mobj->z + mobj->height + 128*FRACUNIT) // You can't be in the vicinity, either...
			{
				// Punch him!
				P_DamageMobj(players[i].mo, mobj, mobj, 1);
				mobj->state->nextstate = mobj->info->spawnstate;

				// Laugh
				S_StartSound(0, sfx_bewar1 + (P_Random() % 4));
			}
		}
	}
	else if (mobj->state == &states[S_BLACKEGG_GOOP])
	{
		// Lob cannon balls
		if (mobj->movecount-- <= 0 || !mobj->target)
		{
			P_SetMobjState(mobj, mobj->info->spawnstate);
			return;
		}

		if ((leveltime & 15*NEWTICRATERATIO) == 0)
		{
			var1 = MT_CANNONBALL;

			var2 = 2*TICRATE + (80<<16);

			A_LobShot(mobj);
			S_StartSound(0, sfx_begoop);
		}
	}
	else if (mobj->state == &states[S_BLACKEGG_SHOOT2])
	{
		// Chaingun goop
		mobj_t *missile;

		if (mobj->movecount-- <= 0 || !mobj->target)
		{
			P_SetMobjState(mobj, mobj->info->spawnstate);
			return;
		}

		A_FaceTarget(mobj);

		missile = P_SpawnXYZMissile(mobj, mobj->target, MT_BLACKEGGMAN_GOOPFIRE,
			mobj->x + P_ReturnThrustX(mobj, mobj->angle-ANG90, mobj->radius/3*2+(4*FRACUNIT)),
			mobj->y + P_ReturnThrustY(mobj, mobj->angle-ANG90, mobj->radius/3*2+(4*FRACUNIT)),
			mobj->z + mobj->height/3*2);

		S_StopSound(missile);

		if (leveltime & 1*NEWTICRATERATIO)
			S_StartSound(0, sfx_beshot);
	}
	else if (mobj->state == &states[S_BLACKEGG_JUMP1] && mobj->tics == 1)
	{
		mobj_t *hitspot = NULL, *mo2;
		angle_t an;
		fixed_t dist, closestdist;
		fixed_t vertical, horizontal;
		fixed_t airtime = 5*TICRATE;
		int waypointNum = 0;
		thinker_t *th;
		int i;
		boolean foundgoop = false;
		int closestNum;

		// Looks for players in goop. If you find one, try to jump on him.
		for (i = 0; i < MAXPLAYERS; i++)
		{
			if (!playeringame[i])
				continue;

			if (!players[i].mo)
				continue;

			if (players[i].mo->health <= 0)
				continue;

			if (players[i].powers[pw_ingoop])
			{
				closestNum = -1;
				closestdist = 16384*FRACUNIT; // Just in case...

				// Find waypoint he is closest to
				for (th = thinkercap.next; th != &thinkercap; th = th->next)
				{
					if (th->function.acp1 != (actionf_p1)P_MobjThinker)
						continue;

					mo2 = (mobj_t *)th;
					if (mo2->type == MT_BOSS3WAYPOINT && mo2->spawnpoint)
					{
						dist = P_AproxDistance(players[i].mo->x - mo2->x, players[i].mo->y - mo2->y);

						if (closestNum == -1 || dist < closestdist)
						{
							closestNum = (mo2->spawnpoint->options & 7);
							closestdist = dist;
							foundgoop = true;
						}
					}
				}
				waypointNum = closestNum;
				break;
			}
		}

		if (!foundgoop)
		{
			if (mobj->z > 1056*FRACUNIT)
				waypointNum = 0;
			else
				waypointNum = 1 + (P_Random() % 4);
		}

		// Don't jump to the center when health is low.
		// Force the player to beat you with missiles.
		if (mobj->health <= 2 && waypointNum == 0)
			waypointNum = 1 + (P_Random() %4);

		if (mobj->tracer && mobj->tracer->type == MT_BOSS3WAYPOINT
			&& mobj->tracer->spawnpoint && (mobj->tracer->spawnpoint->options & 7) == waypointNum)
		{
			if (P_Random() & 1)
				waypointNum++;
			else
				waypointNum--;

			waypointNum %= 5;
		}

		if (waypointNum == 0 && mobj->health <= 2)
			waypointNum = 1 + (P_Random() & 1);

		// scan the thinkers to find
		// the waypoint to use
		for (th = thinkercap.next; th != &thinkercap; th = th->next)
		{
			if (th->function.acp1 != (actionf_p1)P_MobjThinker)
				continue;

			mo2 = (mobj_t *)th;
			if (mo2->type == MT_BOSS3WAYPOINT && mo2->spawnpoint && (mo2->spawnpoint->options & 7) == waypointNum)
			{
				hitspot = mo2;
				break;
			}
		}

		if (hitspot == NULL)
		{
			CONS_Printf("BlackEggman unable to find waypoint #%d!\n", waypointNum);
			P_SetMobjState(mobj, mobj->info->spawnstate);
			return;
		}

		P_SetTarget(&mobj->tracer, hitspot);

		mobj->angle = R_PointToAngle2(mobj->x, mobj->y, hitspot->x, hitspot->y);

		an = mobj->angle;
		an >>= ANGLETOFINESHIFT;

		dist = P_AproxDistance(hitspot->x - mobj->x, hitspot->y - mobj->y);

		horizontal = dist / airtime;
		vertical = (gravity*airtime)/2;

		mobj->momx = FixedMul(horizontal, FINECOSINE(an));
		mobj->momy = FixedMul(horizontal, FINESINE(an));
		mobj->momz = vertical;

//		mobj->momz = 10*FRACUNIT;
	}
	else if (mobj->state == &states[S_BLACKEGG_JUMP2] && P_IsObjectOnGround(mobj))
	{
		// BANG! onto the ground
		int i,j;
		fixed_t ns;
		fixed_t x,y,z;
		mobj_t *mo2;

		S_StartSound(0, sfx_befall);

		z = mobj->floorz;
		for (j = 0; j < 2; j++)
		{
			for (i = 0; i < 32; i++)
			{
				const angle_t fa = (i*FINEANGLES/16) & FINEMASK;
				ns = 64 * FRACUNIT;
				x = mobj->x + FixedMul(FINESINE(fa),ns);
				y = mobj->y + FixedMul(FINECOSINE(fa),ns);

				mo2 = P_SpawnMobj(x, y, z, MT_EXPLODE);
				ns = 16 * FRACUNIT;
				mo2->momx = FixedMul(FINESINE(fa),ns);
				mo2->momy = FixedMul(FINECOSINE(fa),ns);
			}
			z -= 32*FRACUNIT;
		}

		// Hurt player??
		for (i = 0; i < MAXPLAYERS; i++)
		{
			if (!playeringame[i])
				continue;

			if (!players[i].mo)
				continue;

			if (players[i].mo->health <= 0)
				continue;

			if (P_AproxDistance(players[i].mo->x - mobj->x, players[i].mo->y - mobj->y) > mobj->radius*4)
				continue;

			if (players[i].mo->z > mobj->z + 128*FRACUNIT)
				continue;

			if (players[i].mo->z < mobj->z - 64*FRACUNIT)
				continue;

			P_DamageMobj(players[i].mo, mobj, mobj, 1);

			// Laugh
			S_StartSound(0, sfx_bewar1 + (P_Random() % 4));
		}

		P_SetMobjState(mobj, mobj->info->spawnstate);
	}
	else if (mobj->state == &states[mobj->info->deathstate] && mobj->tics == mobj->state->tics)
	{
		S_StartSound(0, sfx_bedie1 + (P_Random() & 1));
	}

}

// Fun function stuff to make NiGHTS hoops!
typedef fixed_t TVector[4];
typedef fixed_t TMatrix[4][4];

static TVector *VectorMatrixMultiply(TVector v, TMatrix m)
{
	static TVector ret;

	ret[0] = FixedMul(v[0],m[0][0]) + FixedMul(v[1],m[1][0]) + FixedMul(v[2],m[2][0]) + FixedMul(v[3],m[3][0]);
	ret[1] = FixedMul(v[0],m[0][1]) + FixedMul(v[1],m[1][1]) + FixedMul(v[2],m[2][1]) + FixedMul(v[3],m[3][1]);
	ret[2] = FixedMul(v[0],m[0][2]) + FixedMul(v[1],m[1][2]) + FixedMul(v[2],m[2][2]) + FixedMul(v[3],m[3][2]);
	ret[3] = FixedMul(v[0],m[0][3]) + FixedMul(v[1],m[1][3]) + FixedMul(v[2],m[2][3]) + FixedMul(v[3],m[3][3]);

	return &ret;
}

// Here is how computing the transformation regarding the tilt is handled
static TMatrix *RotateXMatrix(angle_t rad)
{
	static TMatrix ret;
	const angle_t fa = rad>>ANGLETOFINESHIFT;
	const fixed_t cosrad = FINECOSINE(fa), sinrad = FINESINE(fa);

	ret[0][0] = FRACUNIT; ret[0][1] =       0; ret[0][2] = 0;        ret[0][3] = 0;
	ret[1][0] =        0; ret[1][1] =  cosrad; ret[1][2] = sinrad;   ret[1][3] = 0;
	ret[2][0] =        0; ret[2][1] = -sinrad; ret[2][2] = cosrad;   ret[2][3] = 0;
	ret[3][0] =        0; ret[3][1] =       0; ret[3][2] = 0;        ret[3][3] = FRACUNIT;

	return &ret;
}

#if 0 // RotateYMatrix, just as useful as the other, just..not used right now
static TMatrix *RotateYMatrix(angle_t rad)
{
	static TMatrix ret;
	const angle_t fa = rad>>ANGLETOFINESHIFT;
	const fixed_t cosrad = FINECOSINE(fa), sinrad = FINESINE(fa);

	ret[0][0] = cosrad;   ret[0][1] =        0; ret[0][2] = -sinrad;   ret[0][3] = 0;
	ret[1][0] = 0;        ret[1][1] = FRACUNIT; ret[1][2] = 0;         ret[1][3] = 0;
	ret[2][0] = sinrad;   ret[2][1] =        0; ret[2][2] = cosrad;    ret[2][3] = 0;
	ret[3][0] = 0;        ret[3][1] =        0; ret[3][2] = 0;         ret[3][3] = FRACUNIT;

	return &ret;
}
#endif

static TMatrix *RotateZMatrix(angle_t rad)
{
	static TMatrix ret;
	const angle_t fa = rad>>ANGLETOFINESHIFT;
	const fixed_t cosrad = FINECOSINE(fa), sinrad = FINESINE(fa);

	ret[0][0] = cosrad;    ret[0][1] = sinrad;   ret[0][2] =        0; ret[0][3] = 0;
	ret[1][0] = -sinrad;   ret[1][1] = cosrad;   ret[1][2] =        0; ret[1][3] = 0;
	ret[2][0] = 0;         ret[2][1] = 0;        ret[2][2] = FRACUNIT; ret[2][3] = 0;
	ret[3][0] = 0;         ret[3][1] = 0;        ret[3][2] =        0; ret[3][3] = FRACUNIT;

	return &ret;
}

//
// P_GetClosestAxis
//
// Finds the CLOSEST axis to the source mobj
mobj_t *P_GetClosestAxis(mobj_t *source)
{
	thinker_t *th;
	mobj_t *mo2;
	mobj_t *closestaxis = NULL;
	fixed_t dist1, dist2 = 0;

	// scan the thinkers to find the closest axis point
	for (th = thinkercap.next; th != &thinkercap; th = th->next)
	{
		if (th->function.acp1 != (actionf_p1)P_MobjThinker)
			continue;

		mo2 = (mobj_t *)th;

		if (mo2->type == MT_AXIS)
		{
			if (closestaxis == NULL)
			{
				closestaxis = mo2;
				dist2 = R_PointToDist2(source->x, source->y, mo2->x, mo2->y)-mo2->radius;
			}
			else
			{
				dist1 = R_PointToDist2(source->x, source->y, mo2->x, mo2->y)-mo2->radius;

				if (dist1 < dist2)
				{
					closestaxis = mo2;
					dist2 = dist1;
				}
			}
		}
	}

	if (closestaxis == NULL)
		CONS_Printf("ERROR: No axis points found!\n");

	return closestaxis;
}

static void P_GimmeAxisXYPos(mobj_t *closestaxis, degenmobj_t *mobj)
{
	const angle_t fa = R_PointToAngle2(closestaxis->x, closestaxis->y, mobj->x, mobj->y)>>ANGLETOFINESHIFT;

	mobj->x = closestaxis->x + FixedMul(FINECOSINE(fa),closestaxis->radius);
	mobj->y = closestaxis->y + FixedMul(FINESINE(fa),closestaxis->radius);
}

static void P_MoveHoop(mobj_t *mobj)
{
	const fixed_t fuse = (mobj->fuse*8*FRACUNIT);
	const angle_t fa = mobj->movedir*(FINEANGLES/32);
	TVector v;
	TVector *res;
	fixed_t finalx, finaly, finalz;
	fixed_t mthingx, mthingy, mthingz;

	mthingx = mobj->target->x;
	mthingy = mobj->target->y;
	mthingz = mobj->target->z+mobj->target->height/2;

	// Make the sprite travel towards the center of the hoop
	v[0] = FixedMul(FINECOSINE(fa),fuse);
	v[1] = 0;
	v[2] = FixedMul(FINESINE(fa),fuse);
	v[3] = FRACUNIT;

	res = VectorMatrixMultiply(v, *RotateXMatrix(FixedAngle(mobj->target->movedir*FRACUNIT)));
	M_Memcpy(&v, res, sizeof (v));
	res = VectorMatrixMultiply(v, *RotateZMatrix(FixedAngle(mobj->target->movecount*FRACUNIT)));
	M_Memcpy(&v, res, sizeof (v));

	finalx = mthingx + v[0];
	finaly = mthingy + v[1];
	finalz = mthingz + v[2];

	P_UnsetThingPosition(mobj);
	mobj->x = finalx;
	mobj->y = finaly;
	P_SetThingPosition(mobj);
	mobj->z = finalz - mobj->height/2;
}

// SRB2CBTODO: Use this
void P_SpawnMobjHoop(fixed_t x, fixed_t y, fixed_t z, fixed_t radius, int number, mobjtype_t type, angle_t rotangle)
{
	mobj_t *mobj;
	int i;
	TVector v;
	TVector *res;
	fixed_t finalx, finaly, finalz;
	mobj_t hoopcenter;
	mobj_t *axis;
	degenmobj_t xypos;
	angle_t degrees, fa, closestangle;

	hoopcenter.x = x;
	hoopcenter.y = y;
	hoopcenter.z = z;

	axis = P_GetClosestAxis(&hoopcenter);

	if (!axis)
	{
		CONS_Printf("You forgot to put axis points in the map!\n");
		return;
	}

	xypos.x = x;
	xypos.y = y;

	P_GimmeAxisXYPos(axis, &xypos);

	x = xypos.x;
	y = xypos.y;

	hoopcenter.z = z - mobjinfo[type].height/2;

	hoopcenter.x = x;
	hoopcenter.y = y;

	closestangle = R_PointToAngle2(x, y, axis->x, axis->y);

	degrees = FINEANGLES/number;

	radius >>= FRACBITS;

	// Create the hoop!
	for (i = 0; i < number; i++)
	{
		fa = (i*degrees);
		v[0] = FixedMul(FINECOSINE(fa),radius);
		v[1] = 0;
		v[2] = FixedMul(FINESINE(fa),radius);
		v[3] = FRACUNIT;

		res = VectorMatrixMultiply(v, *RotateXMatrix(rotangle));
		M_Memcpy(&v, res, sizeof (v));
		res = VectorMatrixMultiply(v, *RotateZMatrix(closestangle));
		M_Memcpy(&v, res, sizeof (v));

		finalx = x + v[0];
		finaly = y + v[1];
		finalz = z + v[2];

		mobj = P_SpawnMobj(finalx, finaly, finalz, type);
		mobj->z -= mobj->height/2;
	}
}

void P_SpawnParaloop(fixed_t x, fixed_t y, fixed_t z, fixed_t radius, int number, mobjtype_t type, angle_t rotangle, boolean spawncenter, boolean ghostit)
{
	mobj_t *mobj;
	mobj_t *ghost = NULL;
	int i;
	TVector v;
	TVector *res;
	fixed_t finalx, finaly, finalz, dist;
	mobj_t hoopcenter;
	angle_t degrees, fa, closestangle;
	fixed_t mobjx, mobjy, mobjz;

	hoopcenter.x = x;
	hoopcenter.y = y;
	hoopcenter.z = z;

	hoopcenter.z = z - mobjinfo[type].height/2;

	degrees = FINEANGLES/number;

	radius = FixedDiv(radius,5*(FRACUNIT/4));

	closestangle = 0;

	// Create the hoop!
	for (i = 0; i < number; i++)
	{
		fa = (i*degrees);
		v[0] = FixedMul(FINECOSINE(fa),radius);
		v[1] = 0;
		v[2] = FixedMul(FINESINE(fa),radius);
		v[3] = FRACUNIT;

		res = VectorMatrixMultiply(v, *RotateXMatrix(rotangle));
		M_Memcpy(&v, res, sizeof (v));
		res = VectorMatrixMultiply(v, *RotateZMatrix(closestangle));
		M_Memcpy(&v, res, sizeof (v));

		finalx = x + v[0];
		finaly = y + v[1];
		finalz = z + v[2];

		mobj = P_SpawnMobj(finalx, finaly, finalz, type);

		mobj->z -= mobj->height>>1;

		// change angle
		mobj->angle = R_PointToAngle2(mobj->x, mobj->y, x, y);

		// change slope
		dist = P_AproxDistance(P_AproxDistance(x - mobj->x, y - mobj->y), z - mobj->z);

		if (dist < 1)
			dist = 1;

		mobjx = mobj->x;
		mobjy = mobj->y;
		mobjz = mobj->z;

		if (ghostit)
		{
			ghost = P_SpawnGhostMobj(mobj);
			P_SetMobjState(mobj, S_DISS);
			mobj = ghost;
		}

		mobj->momx = FixedMul(FixedDiv(x - mobjx, dist), 5*FRACUNIT);
		mobj->momy = FixedMul(FixedDiv(y - mobjy, dist), 5*FRACUNIT);
		mobj->momz = FixedMul(FixedDiv(z - mobjz, dist), 5*FRACUNIT);
		mobj->fuse = (radius>>(FRACBITS+2)) + 1;

		if (spawncenter)
		{
			mobj->x = x;
			mobj->y = y;
			mobj->z = z;
		}

		if (mobj->fuse <= 1)
			mobj->fuse = 2;

		mobj->flags |= MF_NOCLIPTHING;
		mobj->flags &= ~MF_SPECIAL;

		if (mobj->fuse > 7)
			mobj->tics = mobj->fuse - 7;
		else
			mobj->tics = 1;
	}
}

//
// P_ScaleMomentum
// Momentum just for scaling certain things when division of int is too off
//
fixed_t P_ScaleMomentum(fixed_t momentum, USHORT scale) // SRB2CBTODO: Rename?
{
	// For non gravity related uses, such as scaling the player's running speed, the game shouldn't just directly scale things
	if (scale > 100)
		momentum += FIXEDSCALE(momentum, FixedMul((scale - 100), (FRACUNIT * 0.2f)));
	// Small scaling, get more
	else if (scale < 100 && scale > 70)
		momentum -= FIXEDSCALE(momentum, FixedMul((100 - scale), (FRACUNIT * 0.60f)));
	else if (scale <= 70 && scale > 60)
		momentum -= FIXEDSCALE(momentum, FixedMul((100 - scale), (FRACUNIT * 0.55f)));
	else if (scale <= 60 && scale > 45)
		momentum -= FIXEDSCALE(momentum, FixedMul((100 - scale), (FRACUNIT * 0.48f)));
	else if (scale <= 45 && scale > 35)
		momentum -= FIXEDSCALE(momentum, FixedMul((100 - scale), (FRACUNIT * 0.46f)));
	else if (scale <= 35)
		momentum -= FIXEDSCALE(momentum, FixedMul((100 - scale), (FRACUNIT * 0.42f)));

	return momentum;
}

#ifdef SPRITEROLL
/*
 P_SetMobjRoll
 P_RollMobjRelative

 mo    - map object to change roll angle of.
 angle - 0 = normal, 90 = right (feet at center, head at left), 180 = equal vflip,
 270 = left (feet at center, head at left), 360 = 0
 speed - number of angle degrees to move each frame (35 frames per second)
 (speed * 35 equals total angel to move per second)
 */
void P_SetMobjRoll(mobj_t *mo, angle_t angle, byte speed)
{
	if (!mo)
		return;

	if (angle > ANG180)// SRB2CBTODO: Is this needed?
	{
		angle -= ANG180 * 2;
	}

	mo->rollangle = mo->destrollangle = angle;
	mo->rollspeed = speed;
}

void P_RollMobjRelative(mobj_t *mo, angle_t angle, byte speed, boolean add)
{
	if (!mo)
		return;

	// Inside the range is -ANG180 to ANG180
	if (angle > ANG180) // SRB2CBTODO: Is this needed?
	{
		angle -= ANG180 * 2;
	}

	if (add)
	{
		// note: if angle is less then zero it goes down.
		mo->destrollangle = mo->rollangle + angle;
	}
	else
	{
		mo->destrollangle = angle;
	}
	mo->rollspeed = speed;
}
#endif

//
// P_SetScale
//
// Sets the mobj scaling
//
void P_SetScale(mobj_t *mobj, USHORT newscale)
{
	player_t *player;

	if (!mobj)
		return;

	mobj->scale = newscale;

	mobj->radius = (fixed_t)FIXEDSCALE(mobj->info->radius, newscale);
	mobj->height = (fixed_t)FIXEDSCALE(mobj->info->height, newscale);

	player = mobj->player;

	// This allows scaling momentum to be capped within the limits of the game's engine
	USHORT scalespeed = newscale;
	if (scalespeed > 250)
		scalespeed = 250;

	if (player)
	{
		if (FIXEDSCALE(atoi(skins[player->skin].normalspeed), scalespeed) < (MAXMOVE*2)/FRACUNIT)
			player->normalspeed = atoi(skins[player->skin].normalspeed)*scalespeed/100;
		else
			player->normalspeed = (MAXMOVE*2)/FRACUNIT;

		player->runspeed = atoi(skins[player->skin].runspeed)*scalespeed/100;

		// Scale the running more tightly, do not change the running speed if it was greater than the normalspeed in the first place
		if (!((atoi(skins[player->skin].runspeed) > (atoi(skins[player->skin].normalspeed)))))
		{
			if (!(atoi(skins[player->skin].runspeed) < player->normalspeed - 5) && player->runspeed > player->normalspeed - 5)
				player->runspeed = player->normalspeed - 5;
			else if (!(atoi(skins[player->skin].runspeed) <= 0) && player->runspeed <= 0) // Extra check
				player->runspeed = 1;
		}

		player->actionspd = atoi(skins[player->skin].actionspd)*newscale/100;

		if (player->actionspd > 300)
			player->actionspd = 300;

		// Acceleration needs P_ScaleMomentum, otherwise it would be way to slow, also scale for FPS scaling
		player->accelstart = P_ScaleMomentum(atoi(skins[player->skin].accelstart), scalespeed); // SRB2CBTODO: What is with this?
		//player->accelstart = P_ScaleMomentum(player->accelstart, 100/NEWTICRATERATIO);
		player->acceleration = P_ScaleMomentum(atoi(skins[player->skin].acceleration), scalespeed);

		// Calculate camera and viewheight elsewhere
	}
}

// Returns true if no boss with health is in the level.
// Used for Chaos mode

#ifdef CHAOSISNOTDEADYET
static boolean P_BossDoesntExist(void)
{
	thinker_t *th;
	mobj_t *mo2;

	// scan the thinkers
	for (th = thinkercap.next; th != &thinkercap; th = th->next)
	{
		if (th->function.acp1 != (actionf_p1)P_MobjThinker)
			continue;

		mo2 = (mobj_t *)th;

		if (mo2->flags & MF_BOSS && mo2->health)
			return false;
	}

	// No boss found!
	return true;
}
#endif

void P_Attract(mobj_t *source, mobj_t *enemy, boolean nightsgrab) // Home in on your target
{
	fixed_t dist, speedmul;
	mobj_t *dest;

	if (
		!enemy->health || !enemy->player ||
		!source->tracer)
		return;

	// adjust direction
	dest = source->tracer;

	if (
		!dest
		|| dest->health <= 0
		)
		return;

	// change angle
	source->angle = R_PointToAngle2(source->x, source->y, enemy->x, enemy->y);

	// change slope
	dist = P_AproxDistance(P_AproxDistance(dest->x - source->x, dest->y - source->y),
		dest->z - source->z);

	if (dist < 1)
		dist = 1;

	if (nightsgrab)
		speedmul = P_AproxDistance(enemy->momx, enemy->momy) + 8*FRACUNIT;
	else
		speedmul = source->info->speed;

	// If an attracted item was given MF_NOCLIP, remove it when close to dest.
	// That way they don't spazz out all over the place.
	if (source->flags & MF_NOCLIP && dist < 32*FRACUNIT)
		source->flags &= ~MF_NOCLIP;

	source->momx = FixedMul(FixedDiv(dest->x - source->x, dist), speedmul);
	source->momy = FixedMul(FixedDiv(dest->y - source->y, dist), speedmul);
	source->momz = FixedMul(FixedDiv(dest->z - source->z, dist), speedmul);

	return;
}

static void P_NightsItemChase(mobj_t *thing)
{
	if (!thing->tracer)
	{
		P_SetTarget(&thing->tracer, NULL);
		thing->flags2 &= ~MF2_NIGHTSPULL;
		return;
	}

	if (!thing->tracer->player)
		return;

	P_Attract(thing, thing->tracer, true);
}

void A_BossDeath(mobj_t *mo);
// AI for the Koopa boss. AKA BOWSER
static void P_KoopaThinker(mobj_t *koopa)
{
	P_MobjCheckWater(koopa);

	if (koopa->watertop > koopa->z + koopa->height + 128*FRACUNIT && koopa->health > 0)
	{
		A_BossDeath(koopa);
		P_SetMobjState(koopa, S_DISS);
		koopa->health = 0;
		return;
	}

	// Koopa moves ONLY on the X axis!
	if (koopa->threshold > 0)
	{
		koopa->threshold--;

		koopa->momx = FRACUNIT;

		if (!koopa->threshold)
			koopa->threshold = -TICRATE*2;
	}
	else if (koopa->threshold < 0)
	{
		koopa->threshold++;

		koopa->momx = -FRACUNIT;

		if (!koopa->threshold)
			koopa->threshold = TICRATE*2;
	}
	else
		koopa->threshold = TICRATE*2;

	P_XYMovement(koopa);
#ifdef ESLOPE // Speed limit protection so objects can't go inside of a slope
	if (koopa->subsector->sector && koopa->subsector->sector->f_slope)
	{
		if (koopa->z < P_GetZAt(koopa->subsector->sector->f_slope, koopa->x, koopa->y))
			koopa->z = P_GetZAt(koopa->subsector->sector->f_slope, koopa->x, koopa->y);
		if (koopa->floorz < P_GetZAt(koopa->subsector->sector->f_slope, koopa->x, koopa->y))
			koopa->floorz = P_GetZAt(koopa->subsector->sector->f_slope, koopa->x, koopa->y);
	}
	if (koopa->subsector->sector && koopa->subsector->sector->c_slope)
	{
		if (koopa->z > P_GetZAt(koopa->subsector->sector->c_slope, koopa->x, koopa->y))
			koopa->z = P_GetZAt(koopa->subsector->sector->c_slope, koopa->x, koopa->y);
		if (koopa->ceilingz > P_GetZAt(koopa->subsector->sector->c_slope, koopa->x, koopa->y))
			koopa->ceilingz = P_GetZAt(koopa->subsector->sector->c_slope, koopa->x, koopa->y);
	}
#endif

	if (P_Random() < 8 && koopa->z <= koopa->floorz)
		koopa->momz = 5*FRACUNIT;

	if (koopa->z > koopa->floorz)
		koopa->momz += (FRACUNIT/4)/NEWTICRATERATIO;

	if (P_Random() < 4)
	{
		mobj_t *flame;
		flame = P_SpawnMobj(koopa->x - koopa->radius + 5*FRACUNIT, koopa->y, koopa->z + (P_Random()<<(FRACBITS-2)), MT_KOOPAFLAME);
		flame->momx = -flame->info->speed;
		S_StartSound(flame, sfx_koopfr);
	}
	else if (P_Random() > 250)
	{
		mobj_t *hammer;
		hammer = P_SpawnMobj(koopa->x - koopa->radius, koopa->y, koopa->z + koopa->height, MT_HAMMER);
		hammer->momx = -5*FRACUNIT;
		hammer->momz = 7*FRACUNIT;
	}
}

//
// P_MobjThinker
//
void P_MobjThinker(mobj_t *mobj)
{
	if (!mobj)
		return;

	// NOTE: MF_NOTHINK will only occur here if a mobj's MF_NOTHINK flag
	// is changed after it was spawned
	if (mobj->flags & MF_NOTHINK)
		return;

	mobj->flags2 &= ~MF2_PUSHED;

	if (cv_objectplace.value)
	{
		if (mobj->player && mobj->target)
		{
			if (mobj->z < mobj->floorz
			&& mobj->floorz + mobj->target->height <= mobj->ceilingz)
				mobj->z = mobj->floorz;
			else if (mobj->z > mobj->ceilingz - mobj->target->height
			&& mobj->floorz + mobj->target->height <= mobj->ceilingz)
				mobj->z = mobj->ceilingz - mobj->target->height;
		}
		else
		{
			switch(mobj->type)
			{
				case MT_BLACKORB:
				case MT_WHITEORB:
				case MT_GREENORB:
				case MT_YELLOWORB:
				case MT_BLUEORB:
#ifdef SRB2K
				case MT_BOUNCEORB:
				case MT_REDFIREORB:
				case MT_ELECTRICORB:
#endif
					mobj->flags2 |= MF2_DONTDRAW;
				default:
					break;
			}
		}
	}

	if (P_FreezeObjectplace())
		return;

	// 970 allows ANY mobj to trigger a linedef exec
	if (mobj->subsector && GETSECSPECIAL(mobj->subsector->sector->special, 2) == 8)
	{
		sector_t *sec2;

		sec2 = P_ThingOnSpecial3DFloor(mobj);
		if (sec2 && GETSECSPECIAL(sec2->special, 2) == 1)
			P_LinedefExecute(sec2->tag, mobj, sec2);
	}


	// SRB2CBTODO: For special mobjs that need more checking for fuses than normal
#ifdef RINGSCALE
	switch (mobj->type)
	{
		case MT_RING:
		case MT_FLINGRING:
			if (mobj->fuse)
			{
				mobj->fuse--;

				if (!mobj->fuse)
				{
					P_UnsetThingPosition(mobj);
					P_SetMobjState(mobj, S_DISS);
				}

			}
			break;
		default:
			break;
	}
#endif

	// Slowly scale up/down to reach your destscale.
	if (mobj->scale != mobj->destscale)
	{
		const unsigned int abspeed = abs(mobj->scale - mobj->destscale);
		unsigned int speed = (abspeed>>8);

		// If a mobj has a scale speed of 0, instantly scale it
		if (mobj->scalespeed == 0)
			P_SetScale(mobj, mobj->destscale);
		else
		{
			if (speed > 1)
				speed *= mobj->scalespeed;
			else
				speed = mobj->scalespeed;

			if (abspeed < speed)
				P_SetScale(mobj, mobj->destscale);
			else if (mobj->scale > mobj->destscale)
				P_SetScale(mobj, (USHORT)(mobj->scale - speed));
			else if (mobj->scale < mobj->destscale)
				P_SetScale(mobj, (USHORT)(mobj->scale + speed));
		}
	}

#ifdef SPRITEROLL
	// Turtle Man: Lets roll.
	if (mobj->rollangle != mobj->destrollangle)
	{
		angle_t speed = mobj->rollspeed * (ANG45/45);
		boolean rolldone = false;

		// Check if we have finished rolling.
		// angle 0 is special... Other check don't work for it.
		if (mobj->destrollangle == 0)
		{
			short roll = mobj->rollangle/(ANG45/45);
			short want_roll = mobj->destrollangle/(ANG45/45);
			short rollspeed = mobj->rollspeed;

			if (roll < want_roll &&
				roll + rollspeed >= want_roll)
			{
				mobj->rollangle = mobj->destrollangle;
				rolldone = true;
			}
			else if (roll > want_roll &&
					 roll - rollspeed <= want_roll)
			{
				mobj->rollangle = mobj->destrollangle;
				rolldone = true;
			}
		}
		else if (mobj->rollangle < mobj->destrollangle &&
				 mobj->rollangle + speed >= mobj->destrollangle)
		{
			mobj->rollangle = mobj->destrollangle;
			rolldone = true;
		}
		else if (mobj->rollangle > mobj->destrollangle &&
				 mobj->rollangle - speed <= mobj->destrollangle)
		{
			mobj->rollangle = mobj->destrollangle;
			rolldone = true;
		}

		// Turn the shorter direction
		if (!rolldone)
		{
			// Find dist for both ways.
			angle_t ang1 = mobj->destrollangle - mobj->rollangle;
			angle_t ang2 = ANGLE_MAX - mobj->destrollangle + mobj->rollangle;

			if (ang2 < ang1)
				mobj->rollangle -= speed;
			else
				mobj->rollangle += speed;
		}
	}
#endif

	// Special thinker for scenery objects
	if (mobj->flags & MF_SCENERY)
	{
		switch (mobj->type)
		{
			case MT_HOOP:
				if (mobj->fuse > 1)
					P_MoveHoop(mobj);
				else if (mobj->fuse == 1)
					mobj->movecount = 1;

				if (mobj->movecount)
				{
					mobj->fuse++;

					if (mobj->fuse > 32*NEWTICRATERATIO)
					{
						if (mobj->target)
							P_RemoveMobj(mobj->target);

						P_RemoveMobj(mobj);
					}
				}
				else
					mobj->fuse--;
				return;
			case MT_NIGHTSPARKLE:
				if (mobj->tics != -1)
				{
					mobj->tics--;

					// you can cycle through multiple states in a tic
					if (!mobj->tics)
						if (!P_SetMobjState(mobj, mobj->state->nextstate))
							return; // freed itself
				}
				if (mobj->flags & MF_SPECIAL)
					return;

				P_UnsetThingPosition(mobj);
				mobj->x += mobj->momx;
				mobj->y += mobj->momy;
				mobj->z += mobj->momz;
				P_SetThingPosition(mobj);
				return;

#ifdef PARTICLES
			case MT_PARTICLEFOUNTAIN:
				if (mobj->flags & MF_AMBUSH) // Light particle variable
				{
					P_Particles(mobj, MT_LIGHTPARTICLE, 100, 2*FRACUNIT, 9*FRACUNIT, TICRATE+1, true, false, 13, 1, 0);
					P_Particles(mobj, MT_LIGHTPARTICLE, 75, 2*FRACUNIT, 10*FRACUNIT, TICRATE+1, true, false, 13, 1, 0);
					P_Particles(mobj, MT_LIGHTPARTICLE, 100, 2*FRACUNIT, 11*FRACUNIT, TICRATE+1, true, false, 13, 1, 0);
				}
				else
				{
					P_Particles(mobj, MT_PARTICLE, 100, 2*FRACUNIT, 9*FRACUNIT, TICRATE+1, true, false, 13, 1, 0);
					P_Particles(mobj, MT_PARTICLE, 75, 2*FRACUNIT, 10*FRACUNIT, TICRATE+1, true, false, 13, 1, 0);
					P_Particles(mobj, MT_PARTICLE, 100, 2*FRACUNIT, 11*FRACUNIT, TICRATE+1, true, false, 13, 1, 0);
				}
				break;

			case MT_SPIRALFOUNTAIN:
				if (leveltime%(TICRATE/3)==0)
				{
					if (mobj->flags & MF_AMBUSH)
					{
						P_SpiralParticles(mobj, 2*FRACUNIT, 10*FRACUNIT, TICRATE+1, true, 13, 0, MT_LIGHTPARTICLE);
						P_SpiralParticles(mobj, 2*FRACUNIT, 10*FRACUNIT, TICRATE+1, true, 13, 0, MT_LIGHTPARTICLE);
					}
					else
					{
						P_SpiralParticles(mobj, 2*FRACUNIT, 10*FRACUNIT, TICRATE+1, true, 13, 0, MT_PARTICLE);
						P_SpiralParticles(mobj, 2*FRACUNIT, 10*FRACUNIT, TICRATE+1, true, 13, 0, MT_PARTICLE);
					}
				}
				break;



			case MT_FPARTICLEFOUNTAIN:
				if (mobj->flags & MF_AMBUSH) // Light particle variable
				{
					P_Particles(mobj, MT_LIGHTPARTICLE, 100, 2*FRACUNIT, 9*FRACUNIT, TICRATE+1, false, false, 13, 1, 0);
					P_Particles(mobj, MT_LIGHTPARTICLE, 75, 2*FRACUNIT, 10*FRACUNIT, TICRATE+1, false, false, 13, 1, 0);
					P_Particles(mobj, MT_LIGHTPARTICLE, 100, 2*FRACUNIT, 11*FRACUNIT, TICRATE+1, false, false, 13, 1, 0);
				}
				else
				{
					P_Particles(mobj, MT_PARTICLE, 100, 2*FRACUNIT, 9*FRACUNIT, TICRATE+1, false, false, 13, 1, 0);
					P_Particles(mobj, MT_PARTICLE, 75, 2*FRACUNIT, 10*FRACUNIT, TICRATE+1, false, false, 13, 1, 0);
					P_Particles(mobj, MT_PARTICLE, 100, 2*FRACUNIT, 11*FRACUNIT, TICRATE+1, false, false, 13, 1, 0);
				}
				break;

			case MT_FSPIRALFOUNTAIN:
				if (leveltime%(TICRATE/3)==0)
				{
					if (mobj->flags & MF_AMBUSH)
					{
						P_SpiralParticles(mobj, 2*FRACUNIT, 10*FRACUNIT, TICRATE+1, false, 13, 0, MT_LIGHTPARTICLE);
						P_SpiralParticles(mobj, 2*FRACUNIT, 10*FRACUNIT, TICRATE+1, false, 13, 0, MT_LIGHTPARTICLE);
					}
					else
					{
						P_SpiralParticles(mobj, 2*FRACUNIT, 10*FRACUNIT, TICRATE+1, false, 13, 0, MT_PARTICLE);
						P_SpiralParticles(mobj, 2*FRACUNIT, 10*FRACUNIT, TICRATE+1, false, 13, 0, MT_PARTICLE);
					}
				}
				break;
#endif

			case MT_BALLOONFOUNTAIN: // SRB2CBTODO: 2 particle ballon types, one that allows custom sprites
			{
				byte speed, zspeed;
				zspeed = P_Random() % 10;
				speed = P_Random() % 4;
				byte color = P_Random() % MAXSKINCOLORS-1;
				byte frequency = P_Random() % 255;

				if (zspeed < 2)
					zspeed = 2;

				if (!color)
					color = 1;

				if (frequency < 120)
					frequency = 120;

				short amount = 1;

				// Set the strength by the angle of the source
				amount = mobj->angle/(ANG45/45);

				if (amount > 200)
					amount = 200;

				if (amount < 1)
					amount = 1;

				if (mobj->flags & MF_AMBUSH) // Balloons fall on floor?
					P_BalloonParticles(mobj, MT_BALLOON, frequency, speed*FRACUNIT, zspeed*FRACUNIT, 5*TICRATE, true, false, color, amount);
				else
					P_BalloonParticles(mobj, MT_BALLOON, frequency, speed*FRACUNIT, zspeed*FRACUNIT, 5*TICRATE, false, false, color, amount);

				//P_BalloonParticles(mobj, MT_BALLOON, frequency, speed*FRACUNIT, 9*FRACUNIT, TICRATE+1, false, false, color, 1, 0);
				//P_BalloonParticles(mobj, MT_BALLOON, frequency, speed*FRACUNIT, 10*FRACUNIT, TICRATE+1, false, false, color, 1, 0);
				//P_BalloonParticles(mobj, MT_BALLOON, frequency, speed*FRACUNIT, 11*FRACUNIT, TICRATE+1, false, false, color, 1, 0);
			}
				break;

			case MT_BALLOONFOUNTAIN2: // SRB2CBTODO: 2 particle ballon types, one that allows custom sprites
			{
				byte speed, zspeed;
				zspeed = P_Random() % 10;
				speed = P_Random() % 4;
				byte color = P_Random() % MAXSKINCOLORS-1;
				byte frequency = P_Random() % 255;
				//repeat = P_Random() % 3;

				if (zspeed < 2)
					zspeed = 2;

				if (!color)
					color = 1;

				if (frequency < 120)
					frequency = 120;

				if (mobj->flags & MF_AMBUSH) // Balloons fall on floor?
					P_BalloonParticles(mobj, MT_BALLOON, frequency, speed*FRACUNIT, zspeed*FRACUNIT, 5*TICRATE, true, false, color, 8);
				else
					P_BalloonParticles(mobj, MT_BALLOON, frequency, speed*FRACUNIT, zspeed*FRACUNIT, 5*TICRATE, false, false, color, 8);

				//P_BalloonParticles(mobj, MT_BALLOON, frequency, speed*FRACUNIT, 9*FRACUNIT, TICRATE+1, false, false, color, 1, 0);
				//P_BalloonParticles(mobj, MT_BALLOON, frequency, speed*FRACUNIT, 10*FRACUNIT, TICRATE+1, false, false, color, 1, 0);
				//P_BalloonParticles(mobj, MT_BALLOON, frequency, speed*FRACUNIT, 11*FRACUNIT, TICRATE+1, false, false, color, 1, 0);
			}
				break;

			// Shields
			case MT_BLACKORB:
			case MT_WHITEORB:
			case MT_GREENORB:
			case MT_YELLOWORB:
			case MT_BLUEORB:
#ifdef SRB2K
			case MT_BOUNCEORB:
			case MT_REDFIREORB:
			case MT_ELECTRICORB:
#endif
				// NOTE: This was static boolean P_ShieldLook(mobj_t *thing, powertype_t power)

				if (mobj->state == &states[S_DISS])
					return;
				else
				{
					powertype_t power = mobj->info->speed;
					fixed_t destx, desty;

					if (!mobj->target || mobj->target->health <= 0 || !mobj->target->player
						|| !mobj->target->player->powers[power] || mobj->target->player->powers[pw_super]
						|| mobj->target->player->powers[pw_invulnerability] > 1)
					{
						P_SetMobjState(mobj, S_DISS);
						return;
					}

					// Update the player's shield coords
					// Don't draw shields when you're invisible
					if (mobj->target->flags2 & MF2_DONTDRAW)
						mobj->flags2 |= MF2_DONTDRAW;
					else
						mobj->flags2 &= ~MF2_DONTDRAW;

					// SRB2CBTODO: OpenGL needs an even method to allow some sprites to overlap!
					// There's a slight issue with this in splitscreen mode!
					if (rendermode == render_opengl)
					{
						angle_t viewingangle;

						viewingangle = R_PointToAngle2(mobj->target->x, mobj->target->y, viewx, viewy);

						destx = mobj->target->x + P_ReturnThrustX(mobj->target, viewingangle, FRACUNIT);
						desty = mobj->target->y + P_ReturnThrustY(mobj->target, viewingangle, FRACUNIT);
					}
					else
					{
						destx = mobj->target->x;
						desty = mobj->target->y;
					}


					if (power == pw_forceshield)
					{
						if (mobj->target->player->powers[pw_forceshield] == 1)
							mobj->flags2 |= MF2_TRANSLUCENT;
						else
							mobj->flags2 &= ~MF2_TRANSLUCENT;
					}

					mobj->flags |= MF_NOCLIPHEIGHT;

					P_UnsetThingPosition(mobj);
					mobj->x = destx;
					mobj->y = desty;
					if (mobj->target->player && (mobj->target->player->pflags & PF_SPINNING)
						&& P_IsObjectOnGround(mobj->target) && !(mobj->target->player->pflags & PF_JUMPED))
					{
						mobj->z = mobj->target->z; //- (FixedDiv(FIXEDSCALE(mobj->target->info->height, mobj->target->scale), 1*FRACUNIT)
						//   - mobj->target->height) / 3 + FIXEDSCALE(2*FRACUNIT, mobj->target->scale);
					}
					else
						mobj->z = mobj->target->z - (FIXEDSCALE(mobj->target->info->height, mobj->target->scale)
													 - mobj->target->height) / 3 + FIXEDSCALE(2*FRACUNIT, mobj->target->scale);

					// Use the target's floorz & ceilingz for proper syncing!
					mobj->floorz = mobj->target->floorz;
					mobj->ceilingz = mobj->target->ceilingz;

					P_SetScale(mobj, mobj->target->scale); // Scale according to the target's scale

					P_SetThingPosition(mobj); // Finaly, set the position
				}
				break;


			case MT_WATERDROP:
				if (P_IsObjectOnGround(mobj)
					&& mobj->health > 0)
				{
					mobj->health = 0;
					P_SetMobjState(mobj, mobj->info->deathstate);
					S_StartSound(mobj, mobj->info->deathsound+(P_Random() % mobj->info->mass));
					return;
				}
				break;
			case MT_BUBBLES:
				P_SceneryCheckWater(mobj);
				break;
			case MT_SMALLBUBBLE:
			case MT_MEDIUMBUBBLE:
			case MT_EXTRALARGEBUBBLE:	// start bubble dissipate
				P_SceneryCheckWater(mobj);
				if (!(mobj->eflags & MFE_UNDERWATER) || mobj->z + mobj->height >= mobj->ceilingz)
				{
					byte prandom;

					P_SetMobjState(mobj, S_DISS);

					if (mobj->threshold == 42) // Don't make pop sound.
						break;

					prandom = P_Random();

					if (prandom <= 51)
						S_StartSound(mobj, sfx_bubbl1);
					else if (prandom <= 102)
						S_StartSound(mobj, sfx_bubbl2);
					else if (prandom <= 153)
						S_StartSound(mobj, sfx_bubbl3);
					else if (prandom <= 204)
						S_StartSound(mobj, sfx_bubbl4);
					else
						S_StartSound(mobj, sfx_bubbl5);
				}
				if (!(mobj->fuse)) // Bubbles eventually dissipate if they can't reach the surface.
					P_SetMobjState(mobj, S_DISS);
				else
					mobj->fuse--;
				break;
			case MT_DROWNNUMBERS:
				if (!mobj->target)
				{
					P_SetMobjState(mobj, S_DISS);
					break;
				}
				if (!mobj->target->player || !(mobj->target->player->powers[pw_underwater] || mobj->target->player->powers[pw_spacetime]))
				{
					P_SetMobjState(mobj, S_DISS);
					break;
				}
				mobj->x = mobj->target->x;
				mobj->y = mobj->target->y;

				if (mobj->target->eflags & MFE_VERTICALFLIP)
					mobj->z = mobj->target->z - 16*FRACUNIT - mobj->height;
				else
					mobj->z = mobj->target->z + (mobj->target->height) + 8*FRACUNIT; // Adjust height for height changes

				if (mobj->threshold <= 35)
					mobj->flags2 |= MF2_DONTDRAW;
				else
					mobj->flags2 &= ~MF2_DONTDRAW;
				if (mobj->threshold <= 30)
					mobj->threshold = 40;
				mobj->threshold--;
				break;
			case MT_FLAMEJET:
				if ((mobj->flags2 & MF2_FIRING) && (leveltime & 3*NEWTICRATERATIO) == 0)
				{
					mobj_t *flame;
					fixed_t strength;

					// Wave the flames back and forth. Reactiontime determines which direction it's going.
					if (mobj->fuse <= -16)
						mobj->reactiontime = 1;
					else if (mobj->fuse >= 16)
						mobj->reactiontime = 0;

					if (mobj->reactiontime)
						mobj->fuse += 2;
					else
						mobj->fuse -= 2;

					flame = P_SpawnMobj(mobj->x, mobj->y, mobj->z, MT_FLAMEJETFLAME);

					flame->angle = mobj->angle;

					if (mobj->flags & MF_AMBUSH) // Wave up and down instead of side-to-side
						flame->momz = mobj->fuse << (FRACBITS-2);
					else
						flame->angle += FixedAngle(mobj->fuse*FRACUNIT);

					strength = 20*FRACUNIT;
					strength -= ((20*FRACUNIT)/16)*mobj->movedir;

					P_InstaThrust(flame, flame->angle, strength);
					S_StartSound(flame, sfx_fire);
				}
				break;
			case MT_VERTICALFLAMEJET:
				if ((mobj->flags2 & MF2_FIRING) && (leveltime & 3*NEWTICRATERATIO) == 0)
				{
					mobj_t *flame;
					fixed_t strength;

					// Wave the flames back and forth. Reactiontime determines which direction it's going.
					if (mobj->fuse <= -16)
						mobj->reactiontime = 1;
					else if (mobj->fuse >= 16)
						mobj->reactiontime = 0;

					if (mobj->reactiontime)
							mobj->fuse++;
					else
						mobj->fuse--;

					flame = P_SpawnMobj(mobj->x, mobj->y, mobj->z, MT_FLAMEJETFLAME);

					strength = 20*FRACUNIT;
					strength -= ((20*FRACUNIT)/16)*mobj->movedir;

					// If deaf'd, the object spawns on the ceiling.
					if (mobj->flags & MF_AMBUSH)
					{
						mobj->z = mobj->ceilingz-mobj->height; // SRB2BTODO: Thinker and things
						flame->momz = -strength;
					}
					else
						flame->momz = strength;
					P_InstaThrust(flame, mobj->angle, mobj->fuse*FRACUNIT/3);
					S_StartSound(flame, sfx_fire);
				}
				break;
			default:
				break;
		}

		P_SceneryThinker(mobj);
		return;
	}

	// if it's pushable, or if it would be pushable other than temporary disablement, use the
	// separate thinker
	if (mobj->flags & MF_PUSHABLE || (mobj->info->flags & MF_PUSHABLE && mobj->fuse))
	{
		P_MobjCheckWater(mobj);
		P_PushableThinker(mobj);
	}
	else if (mobj->flags & MF_BOSS)
	{
		switch (mobj->type)
		{
			case MT_EGGMOBILE:
				if (mobj->health < mobj->info->damage+1 && leveltime & 1*NEWTICRATERATIO && mobj->health > 0)
					P_SpawnMobj(mobj->x, mobj->y, mobj->z, MT_SMOK);

				if (mobj->flags2 & MF2_SKULLFLY)
					P_SpawnGhostMobj(mobj);

				P_Boss1Thinker(mobj);

				if (mobj->flags2 & MF2_BOSSFLEE)
					P_InstaThrust(mobj, mobj->angle, 12*FRACUNIT);
				break;
			case MT_EGGMOBILE2:
				P_Boss2Thinker(mobj);
				if (mobj->flags2 & MF2_BOSSFLEE)
					P_InstaThrust(mobj, mobj->angle, 12*FRACUNIT);
				break;
			case MT_EGGMOBILE3:
				P_Boss3Thinker(mobj);
				break;
			case MT_EGGMOBILE4:
				P_Boss4Thinker(mobj);
				if (mobj->flags2 & MF2_BOSSFLEE)
					P_InstaThrust(mobj, mobj->angle, 12*FRACUNIT);
				break;
			case MT_BLACKEGGMAN:
				P_Boss7Thinker(mobj);
				break;
			default: // Generic SOC-made boss
				if (mobj->flags2 & MF2_SKULLFLY)
					P_SpawnGhostMobj(mobj);

				P_GenericBossThinker(mobj);
				if (mobj->flags2 & MF2_BOSSFLEE)
					P_InstaThrust(mobj, mobj->angle, 12*FRACUNIT);
				break;
		}
	}
	else switch (mobj->type)
	{
		case MT_ROCKCRUMBLE1:
		case MT_ROCKCRUMBLE2:
		case MT_ROCKCRUMBLE3:
		case MT_ROCKCRUMBLE4:
		case MT_ROCKCRUMBLE5:
		case MT_ROCKCRUMBLE6:
		case MT_ROCKCRUMBLE7:
		case MT_ROCKCRUMBLE8:
		case MT_ROCKCRUMBLE9:
		case MT_ROCKCRUMBLE10:
		case MT_ROCKCRUMBLE11:
		case MT_ROCKCRUMBLE12:
		case MT_ROCKCRUMBLE13:
		case MT_ROCKCRUMBLE14:
		case MT_ROCKCRUMBLE15:
		case MT_ROCKCRUMBLE16:
			if (mobj->z <= P_FloorzAtPos(mobj->x, mobj->y, mobj->z, mobj->height)
				&& mobj->state != &states[mobj->info->deathstate])
			{
				P_SetMobjState(mobj, mobj->info->deathstate);
				return;
			}
			break;
		case MT_EGGSHIELD:
			if (!mobj->tracer)
				P_SetMobjState(mobj, S_DISS);
			break;
		case MT_FLOORSPIKE: // SRB2CBTODO:
		{
			if ((mobj->flags & MF_AMBUSH) && !(mobj->flags & (MF_NOBLOCKMAP|MF_NOGRAVITY|MF_SCENERY|MF_NOCLIPHEIGHT)))
			{
				mobj->flags &= ~MF_NOBLOCKMAP|MF_SCENERY|MF_NOCLIPHEIGHT;
				mobj->flags |= MF_SOLID;
			}
			break;
		}
		case MT_EMERALDSPAWN:
			if (mobj->threshold)
			{
				mobj->threshold--;

				if (!mobj->threshold && !mobj->target && mobj->reactiontime)
				{
					mobj_t *emerald = P_SpawnMobj(mobj->x, mobj->y, mobj->z, mobj->reactiontime);
					emerald->threshold = 42;
					P_SetTarget(&mobj->target, emerald);
					P_SetTarget(&emerald->target, mobj);
				}
			}
			break;
		case MT_EGGTRAP: // Egg Capsule animal release
			if (mobj->fuse > 0 && mobj->fuse < 2*TICRATE-(TICRATE/7)
				&& (mobj->fuse & 3))
			{
				if (!mobj->subsector)
					break;
				int i,j;
				fixed_t x,y,z;
				fixed_t ns;
				mobj_t *mo2;

				i = P_Random();
				z = mobj->subsector->sector->floorheight + ((P_Random()&63)*FRACUNIT);
				for (j = 0; j < 2; j++)
				{
					const angle_t fa = (P_Random()*FINEANGLES/16) & FINEMASK;
					ns = 64 * FRACUNIT;
					x = mobj->x + FixedMul(FINESINE(fa),ns);
					y = mobj->y + FixedMul(FINECOSINE(fa),ns);

					mo2 = P_SpawnMobj(x, y, z, MT_EXPLODE);
					ns = 4 * FRACUNIT;
					mo2->momx = FixedMul(FINESINE(fa),ns);
					mo2->momy = FixedMul(FINECOSINE(fa),ns);

					i = P_Random();

					if (i % 5 == 0)
						P_SpawnMobj(x, y, z, MT_CHICKEN);
					else if (i % 4 == 0)
						P_SpawnMobj(x, y, z, MT_COW);
					else if (i % 3 == 0)
					{
						P_SpawnMobj(x, y, z, MT_BIRD);
						S_StartSound(mo2, sfx_pop);
					}
					else if ((i & 1) == 0)
						P_SpawnMobj(x, y, z, MT_BUNNY);
					else
						P_SpawnMobj(x, y, z, MT_MOUSE);
				}

				mobj->fuse--;
			}
				break;
		case MT_BIGAIRMINE:
		{
			if (mobj->tracer != NULL && mobj->tracer->player && mobj->tracer->health > 0
				&& P_AproxDistance(P_AproxDistance(mobj->tracer->x - mobj->x, mobj->tracer->y - mobj->y),
								   mobj->tracer->z - mobj->z) <= mobj->info->radius * (16 + (mobj->spawnpoint->angle/(ANG45/45))))
			{
				// Home in on any player targets.
				P_HomingAttack(mobj, mobj->tracer);

				if (mobj->z < mobj->floorz)
					mobj->z = mobj->floorz;
				else if (mobj->z + mobj->height > mobj->ceilingz)
					mobj->z = mobj->ceilingz - mobj->height;

				if (leveltime % mobj->info->painchance == 0)
					S_StartSound(mobj, mobj->info->activesound);
			}
			else
			{
				// Try to find a player
				if (mobj->spawnpoint != NULL)
					P_LookForPlayers(mobj, true, true, mobj->info->radius * (16+mobj->spawnpoint->angle/(ANG45/45)));
				else
					P_LookForPlayers(mobj, true, true, mobj->info->radius * 16);

				mobj->momx >>= 1;
				mobj->momy >>= 1;
				mobj->momz >>= 1;
			}
		}
			break;
		case MT_BIGMINE:
			{
				if (mobj->z + mobj->height > mobj->watertop)
					mobj->z = mobj->watertop - mobj->height;

				if (mobj->z < mobj->floorz)
					mobj->z = mobj->floorz;
				else if (mobj->z + mobj->height > mobj->ceilingz)
					mobj->z = mobj->ceilingz - mobj->height;

				if (mobj->tracer != NULL && mobj->tracer->player && mobj->tracer->health > 0
					&& P_AproxDistance(P_AproxDistance(mobj->tracer->x - mobj->x, mobj->tracer->y - mobj->y),
									   mobj->tracer->z - mobj->z) <= mobj->info->radius * (16 + (mobj->spawnpoint->angle/(ANG45/45))))
				{
					P_MobjCheckWater(mobj); // SRB2CBTODO: Do a real check for a water top by setting a value for -1 if no water exists

					// Home in on any player targets.
					P_HomingAttack(mobj, mobj->tracer);

					// Don't let water mines go out of water, only air mines can
					if (mobj->z + mobj->height > mobj->watertop)
						mobj->z = mobj->watertop - mobj->height;

					if (mobj->z < mobj->floorz)
						mobj->z = mobj->floorz;
					else if (mobj->z + mobj->height > mobj->ceilingz)
						mobj->z = mobj->ceilingz - mobj->height;

					if (leveltime % mobj->info->painchance == 0)
						S_StartSound(mobj, mobj->info->activesound);
				}
				else
				{
					// Try to find a player
					if (mobj->spawnpoint != NULL)
						P_LookForPlayers(mobj, true, true, mobj->info->radius * (16+mobj->spawnpoint->angle/(ANG45/45)));
					else
						P_LookForPlayers(mobj, true, true, mobj->info->radius * 16);

					mobj->momx >>= 1;
					mobj->momy >>= 1;
					mobj->momz >>= 1;
				}
			}
			break;
		case MT_SPINMACEPOINT:
			if (leveltime & 1)
			{
				if (mobj->lastlook > mobj->movecount)
					mobj->lastlook--;
/*
				if (mobj->threshold > mobj->movefactor)
					mobj->threshold -= FRACUNIT;
				else if (mobj->threshold < mobj->movefactor)
					mobj->threshold += FRACUNIT;*/
			}
			break;
		case MT_EGGCAPSULE:
		case MT_HAMMER:
			if (P_IsObjectOnGround(mobj))
				P_SetMobjState(mobj, S_DISS);
			break;
		case MT_KOOPA:
			P_KoopaThinker(mobj);
			break;
		case MT_REDRING:
			if (((mobj->z < mobj->floorz) || (mobj->z + mobj->height > mobj->ceilingz))
				&& mobj->flags & MF_MISSILE)
			{
				P_ExplodeMissile(mobj);
			}
			break;
		case MT_BOSSFLYPOINT:
			return;
		case MT_NIGHTSCORE:
			mobj->flags |= MF_TRANSLATION;
			mobj->color = leveltime % 13;
			break;
		case MT_JETFUME1:
			{
				fixed_t jetx, jety;

				if (!mobj->target)
				{
					P_SetMobjState(mobj, S_DISS);
					return;
				}

#ifdef CHAOSISNOTDEADYET
				if (gametype == GT_CHAOS && mobj->target->health <= 0)
					P_SetMobjState(mobj, S_DISS);
#endif

				jetx = mobj->target->x + P_ReturnThrustX(mobj->target, mobj->target->angle, -64*FRACUNIT);
				jety = mobj->target->y + P_ReturnThrustY(mobj->target, mobj->target->angle, -64*FRACUNIT);

				if (mobj->fuse == 56) // First one
				{
					P_UnsetThingPosition(mobj);
					mobj->x = jetx;
					mobj->y = jety;
					mobj->z = mobj->target->z + 38*FRACUNIT;
					mobj->floorz = mobj->z;
					mobj->ceilingz = mobj->z+mobj->height;
					P_SetThingPosition(mobj);
				}
				else if (mobj->fuse == 57)
				{
					P_UnsetThingPosition(mobj);
					mobj->x = jetx + P_ReturnThrustX(mobj->target, mobj->target->angle-ANG90, 24*FRACUNIT);
					mobj->y = jety + P_ReturnThrustY(mobj->target, mobj->target->angle-ANG90, 24*FRACUNIT);
					mobj->z = mobj->target->z + 12*FRACUNIT;
					mobj->floorz = mobj->z;
					mobj->ceilingz = mobj->z+mobj->height;
					P_SetThingPosition(mobj);
				}
				else if (mobj->fuse == 58)
				{
					P_UnsetThingPosition(mobj);
					mobj->x = jetx + P_ReturnThrustX(mobj->target, mobj->target->angle+ANG90, 24*FRACUNIT);
					mobj->y = jety + P_ReturnThrustY(mobj->target, mobj->target->angle+ANG90, 24*FRACUNIT);
					mobj->z = mobj->target->z + 12*FRACUNIT;
					mobj->floorz = mobj->z;
					mobj->ceilingz = mobj->z+mobj->height;
					P_SetThingPosition(mobj);
				}
				mobj->fuse++;
			}
			break;
		case MT_PROPELLER:
			{
				fixed_t jetx, jety;

				if (!mobj->target)
				{
					P_SetMobjState(mobj, S_DISS);
					return;
				}

#ifdef CHAOSISNOTDEADYET
				if (gametype == GT_CHAOS && mobj->target->health <= 0)
					P_SetMobjState(mobj, S_DISS);
#endif

				jetx = mobj->target->x + P_ReturnThrustX(mobj->target, mobj->target->angle, -60*FRACUNIT);
				jety = mobj->target->y + P_ReturnThrustY(mobj->target, mobj->target->angle, -60*FRACUNIT);

				P_UnsetThingPosition(mobj);
				mobj->x = jetx;
				mobj->y = jety;
				mobj->z = mobj->target->z + 17*FRACUNIT;
				mobj->angle = mobj->target->angle - ANG180;
				mobj->floorz = mobj->z;
				mobj->ceilingz = mobj->z+mobj->height;
				P_SetThingPosition(mobj);
			}
			break;
		case MT_SEED:
			P_SetObjectMomZ(mobj, mobj->info->speed, false, false);
			break;
		case MT_NIGHTSDRONE:
			if (mobj->tracer && mobj->tracer->player && !(mobj->tracer->player->pflags & PF_NIGHTSMODE))
				mobj->flags2 &= ~MF2_DONTDRAW;
			mobj->angle += ANGLE_10;
			if (P_IsObjectOnGround(mobj))
				P_SetObjectMomZ(mobj, 5*FRACUNIT, false, false);
			break;
		case MT_PLAYER:
			P_PlayerMobjThinker(mobj);
			return;
		case MT_FAN: // Fans spawn bubbles underwater
			// check mobj against possible water content
			P_MobjCheckWater(mobj);
			if (mobj->eflags & MFE_UNDERWATER)
			{
				fixed_t hz = mobj->z + (4*mobj->height)/5;
				mobj_t *bubble = NULL;
				if (!(P_Random() % 16))
					bubble = P_SpawnMobj(mobj->x, mobj->y, hz, MT_SMALLBUBBLE);
				else if (!(P_Random() % 96))
					bubble = P_SpawnMobj(mobj->x, mobj->y, hz, MT_MEDIUMBUBBLE);
				if (bubble)
				{
					bubble->destscale = mobj->scale;
					P_SetScale(bubble,mobj->scale);
				}
			}
			break;
		case MT_SKIM:
			// check mobj against possible water content, before movement code
			P_MobjCheckWater(mobj);

			// Keep Skim at water surface
			if (mobj->z <= mobj->watertop)
			{
				mobj->flags |= MF_NOGRAVITY;
				if (mobj->z < mobj->watertop)
				{
					if (mobj->watertop - mobj->z <= mobj->info->speed*FRACUNIT)
						mobj->z = mobj->watertop;
					else
						mobj->momz = mobj->info->speed*FRACUNIT;
				}
			}
			else
			{
				mobj->flags &= ~MF_NOGRAVITY;
				if (mobj->z > mobj->watertop && mobj->z - mobj->watertop < MAXSTEPMOVE)
					mobj->z = mobj->watertop;
			}
			break;
		case MT_RING:
		case MT_COIN:
#ifdef BLUE_SPHERES
		case MT_BLUEBALL:
#endif
		case MT_REDTEAMRING:
		case MT_BLUETEAMRING:
			// No need to check water. Who cares?
			P_RingThinker(mobj);
			if (mobj->flags2 & MF2_NIGHTSPULL)
				P_NightsItemChase(mobj);
			return;
		case MT_NIGHTSWING:
			if (mobj->flags2 & MF2_NIGHTSPULL)
				P_NightsItemChase(mobj);
			break;
		case MT_SHELL:
			if (mobj->threshold > TICRATE)
				mobj->threshold--;

			if (mobj->state != &states[S_SHELL])
			{
				mobj->angle = R_PointToAngle2(mobj->x, mobj->y, mobj->x+mobj->momx, mobj->y+mobj->momy);
				P_InstaThrust(mobj, mobj->angle, mobj->info->speed);
			}
			break;
		case MT_TURRET:
			P_MobjCheckWater(mobj);
			P_CheckPosition(mobj, mobj->x, mobj->y);
			mobj->floorz = tmfloorz;
			mobj->ceilingz = tmceilingz; // SRB2CBTODO: ZMOVE

			if ((mobj->eflags & MFE_UNDERWATER) && mobj->health > 0)
			{
				P_SetMobjState(mobj, mobj->info->deathstate);
				mobj->health = 0;
				mobj->flags2 &= ~MF2_FIRING;
			}
			else if (mobj->health > 0 && mobj->z + mobj->height > mobj->ceilingz) // Crushed
			{
				int i,j;
				fixed_t ns;
				fixed_t x,y,z;
				mobj_t *mo2;

				z = mobj->subsector->sector->floorheight + 64*FRACUNIT;
				for (j = 0; j < 2; j++)
				{
					for (i = 0; i < 32; i++)
					{
						const angle_t fa = (i*FINEANGLES/16) & FINEMASK;
						ns = 64 * FRACUNIT;
						x = mobj->x + FixedMul(FINESINE(fa),ns);
						y = mobj->y + FixedMul(FINECOSINE(fa),ns);

						mo2 = P_SpawnMobj(x, y, z, MT_EXPLODE);
						ns = 16 * FRACUNIT;
						mo2->momx = FixedMul(FINESINE(fa),ns);
						mo2->momy = FixedMul(FINECOSINE(fa),ns);
					}
					z -= 32*FRACUNIT;
				}
				P_SetMobjState(mobj, mobj->info->deathstate);
				mobj->health = 0;
				mobj->flags2 &= ~MF2_FIRING;
			}
			break;
		case MT_BLUEFLAG:
		case MT_REDFLAG:
			{
				sector_t *sec2;
				sec2 = P_ThingOnSpecial3DFloor(mobj);
				if ((sec2 && GETSECSPECIAL(sec2->special, 4) == 2) || (GETSECSPECIAL(mobj->subsector->sector->special, 4) == 2))
					mobj->fuse = 1; // Return to base.
				break;
			}
		case MT_SKYBOXVIEW:
		{
			// SRB2CBTODO: If the skybox has been set, all skybox mobjs in a map should be set to MF_RESPAWN off,
			// and a special call to change the sky should be called here by a special linedef special
			break;
		}
		case MT_SKYBOXMAPCENTER:
		{
			// SRB2CBTODO: If the skybox has been set, all skybox mobjs in a map should be set to MF_RESPAWN off,
			// and a special call to change the sky should be called here by a special linedef special
			break;
		}
		case MT_HANGGLIDER:
		case MT_SKATEBOARD:
		{
			P_MobjCheckWater(mobj);

			if (mobj->watertop)
			{
				if (mobj->z <= mobj->watertop + mobj->height && !(mobj->eflags & MFE_UNDERWATER)
					&& (P_AproxDistance(mobj->momx, mobj->momy) > 20*FRACUNIT)
					&& leveltime % (TICRATE/7) == 0 && !(mobj->target && mobj->target->player->spectator))
				{
					mobj_t *water = P_SpawnMobj(mobj->x, mobj->y, mobj->watertop, MT_SPLISH);
					S_StartSound(water, sfx_wslap);
					water->destscale = mobj->scale;
					P_SetScale(water, mobj->scale);
				}
			}

			if (mobj->target && mobj->target->eflags & MFE_VERTICALFLIP)
				mobj->eflags |= MFE_VERTICALFLIP;

			// These mobjs should disappear in death pits
			if (mobj->subsector && (GETSECSPECIAL(mobj->subsector->sector->special, 1) == 6
									|| GETSECSPECIAL(mobj->subsector->sector->special, 1) == 7))
			{
				if (mobj->z <= mobj->subsector->sector->floorheight)
				{
					if (mobj->target && mobj->target->player && mobj->target->player->mo->tracer)
					{
						if (mobj->target->player->mo->tracer->type == MT_SKATEBOARD)
							mobj->target->player->pflags &= ~PF_MINECART;
						else if (mobj->target->player->mo->tracer->type == MT_HANGGLIDER)
							mobj->target->player->pflags &= ~PF_ITEMHANG;

						mobj->target->player->itemspeed = 0;
						P_SetTarget(&mobj->target->player->mo->tracer, NULL);
					}
					P_SetMobjState(mobj, S_DISS);
				}
			}


			// If there is no glider controler,
			// Slowly lower the glider to the floor
			if ((mobj->flags & MF_NOCLIPTHING) && !P_IsObjectOnGround(mobj) && (!mobj->target || (mobj->target && mobj->target->player && !(mobj->target->player->pflags & PF_ITEMHANG))))
			{
				P_SetObjectMomZ(mobj, -FRACUNIT/4, true, false);
				if (!mobj->target)
					P_SetTarget(&mobj->target, NULL);
			}


			// Special case for things that respawn when out of place
			// Check if the object already has a target, if so, check for respawning // SRB2CBTODO: Check extensively
			if (mobj->spawnpoint && mobj->target)
			{
				fixed_t x, y, z;
				mobj_t *newmobj;
				subsector_t *ss;

				x = mobj->spawnpoint->x << FRACBITS;
				y = mobj->spawnpoint->y << FRACBITS;

				ss = R_PointInSubsector(x, y);

				if (!mobj->spawnpoint->z)
					z = ((ss->sector->floorheight>>FRACBITS) + (mobj->spawnpoint->options >> ZSHIFT));
				else
					z = mobj->spawnpoint->z;

				z *= FRACUNIT;

				//CONS_Printf("Spawnpoint Angle: %d; Mobj Angle: %d\n", mobj->spawnpoint->angle*(ANG45/45), mobj->angle);
				//CONS_Printf("Spawnpoint z %d; Mobj z %d\n", z/FRACUNIT, mobj->z/FRACUNIT);

				// Check the Z distance of the last object too?
				if ((mobj->type == MT_HANGGLIDER)
					&& ((P_AproxDistance(P_AproxDistance(mobj->x - x, mobj->y - y),
										 mobj->z - z) / FRACUNIT) > 128))
				{
					if (!P_CheckSameMobjAtPos(mobj->type, x, y, z, true)) // Check the z
					{
						//CONS_Printf("Spawning\n"); // VPHYSICS WALLRUN fix this
						newmobj = P_SpawnMobj(x, y, z, mobj->type);
						newmobj->spawnpoint = mobj->spawnpoint;
						newmobj->angle = mobj->spawnpoint->angle*(ANG45/45);
					}
				}
				// Just check the XY distance of the last object, because these may fall from the spawnpoint
				else if (P_AproxDistance(mobj->x - x, mobj->y - y)/FRACUNIT > 320)
				{
					// SRB2CBTODO: P_CheckSameMobj
					if (!P_CheckSameMobjAtPos(mobj->type, x, y, z, false)) // Check the z
					{
						//CONS_Printf("Spawning\n");
						newmobj = P_SpawnMobj(x, y, z, mobj->type);
						newmobj->spawnpoint = mobj->spawnpoint;
						newmobj->angle = mobj->spawnpoint->angle*(ANG45/45);
					}
				}

			}
		}
			break;
		case MT_CANNONBALL:
#ifdef FLOORSPLATS
			R_AddFloorSplat(mobj->tracer->subsector, mobj->tracer, "TARGET", mobj->tracer->x,
				mobj->tracer->y, mobj->tracer->floorz, SPLATDRAWMODE_SHADE);
#endif
			break;
		case MT_WATERDROP:
			// water drops that hit water should disappear
			if ((mobj->eflags & MFE_UNDERWATER) || (mobj->eflags & MFE_TOUCHWATER))
				P_SetMobjState(mobj, S_DISS);
			break; // SRB2CBTODO: Break needed?
		case MT_SPINFIRE:
			if (!(mobj->target && mobj->target->player && mobj->target->player->powers[pw_flameshield]))
			{
			if (mobj->eflags & MFE_VERTICALFLIP)
				mobj->z = mobj->ceilingz - mobj->height;
			else
				mobj->z = mobj->floorz+1;
			}
			// THERE IS NO BREAK HERE ON PURPOSE // SRB2CBTODO: Why?
		default:
			// check mobj against possible water content, before movement code
			P_MobjCheckWater(mobj);

			// Extinguish fire objects in water
			if ((mobj->flags & MF_FIRE) && mobj->type != MT_PUMA
				&& ((mobj->eflags & MFE_UNDERWATER) || (mobj->eflags & MFE_TOUCHWATER)))
				P_SetMobjState(mobj, S_DISS);
			break;
	}


	if (mobj->flags2 & MF2_FIRING && mobj->target)
	{
		if (mobj->health > 0 && (leveltime & 1*NEWTICRATERATIO)) // Fire mode
		{
			mobj_t *missile;

			if (mobj->target->player && mobj->target->player->nightstime)
			{
				fixed_t oldval = mobjinfo[mobj->eflags >> 16].speed;

				mobj->angle = R_PointToAngle2(mobj->x, mobj->y, mobj->target->x+mobj->target->momx, mobj->target->y+mobj->target->momy);
				mobjinfo[mobj->eflags >> 16].speed = 60*FRACUNIT; // SRB2CBTODO: Why was this MAXMOVE ?
				missile = P_SpawnMissile(mobj, mobj->target, mobj->eflags >> 16); // ? bad
				mobjinfo[mobj->eflags >> 16].speed = oldval;
			}
			else
			{
				mobj->angle = R_PointToAngle2(mobj->x, mobj->y, mobj->target->x, mobj->target->y);
				missile = P_SpawnMissile(mobj, mobj->target, mobj->eflags >> 16); // ? bad
			}

			if (missile)
			{
				if (mobj->flags2 & MF2_SUPERFIRE)
					missile->flags2 |= MF2_SUPERFIRE;

				if (mobj->info->attacksound)
					S_StartSound(missile, mobj->info->attacksound);
			}
		}
		else if (mobj->health > 0)
			mobj->angle = R_PointToAngle2(mobj->x, mobj->y, mobj->target->x, mobj->target->y);
	}

	if (mobj->flags & MF_AMBIENT)
	{
		if (!(leveltime % mobj->health) && mobj->info->seesound)
			S_StartSound(mobj, mobj->info->seesound);
		return;
	}

	// Check fuse
	if (mobj->fuse)
	{
		mobj->fuse--;

		if (!mobj->fuse)
		{
			subsector_t *ss;
			fixed_t x, y, z;
			mobj_t *flagmo, *newmobj;

			switch (mobj->type)
			{
				byte prandom;

				// gargoyle and snowman handled in P_PushableThinker, not here
				case MT_THROWNGRENADE:
					P_SetMobjState(mobj, mobj->info->deathstate);
					break;
				case MT_BLUEFLAG:
				case MT_REDFLAG:
					if (mobj->spawnpoint)
					{
						x = mobj->spawnpoint->x << FRACBITS;
						y = mobj->spawnpoint->y << FRACBITS;
						ss = R_PointInSubsector(x, y);
						z = ss->sector->floorheight;
						if (mobj->spawnpoint->z)
							z += mobj->spawnpoint->z << FRACBITS;
						flagmo = P_SpawnMobj(x, y, z, mobj->type);
						flagmo->spawnpoint = mobj->spawnpoint;

						if (mobj->type == MT_REDFLAG)
						{
							if (!(mobj->flags2 & MF2_JUSTATTACKED))
								CONS_Printf("The red flag has returned to base.\n");

							if (players[consoleplayer].ctfteam == 1)
								S_StartSound(NULL, sfx_hoop1);

							redflag = NULL;
						}
						else // MT_BLUEFLAG
						{
							if (!(mobj->flags2 & MF2_JUSTATTACKED))
								CONS_Printf("The blue flag has returned to base.\n");

							if (players[consoleplayer].ctfteam == 2)
								S_StartSound(NULL, sfx_hoop1);

							blueflag = NULL;
						}
					}
					P_SetMobjState(mobj, S_DISS);
					break;
				case MT_YELLOWTV: // Ring shield box
				case MT_BLUETV: // Force shield box
				case MT_GREENTV: // Water shield box
				case MT_BLACKTV: // Bomb shield box
				case MT_WHITETV: // Jump shield box
				case MT_SNEAKERTV: // Super Sneaker box
				case MT_SUPERRINGBOX: // 10-Ring box
				case MT_REDRINGBOX: // Red Team 10-Ring box
				case MT_BLUERINGBOX: // Blue Team 10-Ring box
				case MT_INV: // Invincibility box
				case MT_MIXUPBOX: // Teleporter Mixup box
				case MT_RECYCLETV: // Recycler box
				case MT_PRUP: // 1up!
				case MT_EGGMANBOX:
				case MT_GRAVITYBOX:
#ifdef SRB2K
				case MT_REDFIRETV:
				case MT_BOUNCETV:
				case MT_ELECTRICTV:
#endif
					P_SetMobjState(mobj, S_DISS); // make sure they disappear

					if ((mobj->flags & MF_AMBUSH) || (mobj->flags2 & MF2_STRONGBOX))
					{
						mobjtype_t spawnchance[64];
						int i = 0;
						int oldi = 0;
						int increment = 0;
						int numchoices = 0;

						prandom = P_Random(); // Gotta love those random numbers!

						//if (cv_superring.value)
						{
							oldi = i;

							if (mobj->flags2 & MF2_STRONGBOX) //strong box
								increment = 0;
							else //weak box
								increment = 16;

							for (; i < oldi + increment; i++)
							{
								spawnchance[i] = MT_SUPERRINGBOX;
								numchoices++;
							}
						}
						//if (cv_supersneakers.value)
						{
							oldi = i;

							if (mobj->flags2 & MF2_STRONGBOX) //strong box
								increment = 0;
							else //weak box
								increment = 14;

							for (; i < oldi + increment; i++)
							{
								spawnchance[i] = MT_SNEAKERTV;
								numchoices++;
							}
						}
						//if (cv_invincibility.value)
						{
							oldi = i;

							if (mobj->flags2 & MF2_STRONGBOX) //strong box
								increment = 6;
							else //weak box
								increment = 0;

							for (; i < oldi + increment; i++)
							{
								spawnchance[i] = MT_INV;
								numchoices++;
							}
						}
						//if (cv_jumpshield.value)
						{
							oldi = i;

							if (mobj->flags2 & MF2_STRONGBOX) //strong box
								increment = 12;
							else //weak box
								increment = 12;

							for (; i < oldi + increment; i++)
							{
								spawnchance[i] = MT_WHITETV;
								numchoices++;
							}
						}
						//if (cv_watershield.value)
						{
							oldi = i;

							if (mobj->flags2 & MF2_STRONGBOX) //strong box
								increment = 12;
							else //weak box
								increment = 12;

							for (; i < oldi + increment; i++)
							{
								spawnchance[i] = MT_GREENTV;
								numchoices++;
							}
						}
						//if (cv_ringshield.value)
						{
							oldi = i;

							if (mobj->flags2 & MF2_STRONGBOX) //strong box
								increment = 8;
							else //weak box
								increment = 2;

							for (; i < oldi + increment; i++)
							{
								spawnchance[i] = MT_YELLOWTV;
								numchoices++;
							}
						}
						//if (cv_forceshield.value)
						{
							oldi = i;

							if (mobj->flags2 & MF2_STRONGBOX) //strong box
								increment = 12;
							else //weak box
								increment = 4;

							for (; i < oldi + increment; i++)
							{
								spawnchance[i] = MT_BLUETV;
								numchoices++;
							}
						}
						//if (cv_bombshield.value)
						{
							oldi = i;

							if (mobj->flags2 & MF2_STRONGBOX) //strong box
								increment = 8;
							else //weak box
								increment = 0;

							for (; i < oldi + increment; i++)
							{
								spawnchance[i] = MT_BLACKTV;
								numchoices++;
							}
						}
						//if (cv_teleporters.value)
						{
							oldi = i;

							if (mobj->flags2 & MF2_STRONGBOX) //strong box
								increment = 0;
							else //weak box
								increment = 2;

							for (; i < oldi + increment; i++)
							{
								spawnchance[i] = MT_MIXUPBOX;
								numchoices++;
							}
						}

						//if (cv_recycler.value)
						{
							oldi = i;

							if (mobj->flags2 & MF2_STRONGBOX) //strong box
								increment = 0;
							else //weak box
								increment = 2;

							for (; i < oldi + increment; i++)
							{
								spawnchance[i] = MT_RECYCLETV;
								numchoices++;
							}
						}

						//if (cv_1up.value)
						{
							oldi = i;

							if (mobj->flags2 & MF2_STRONGBOX) //strong box
								increment = 6;
							else //weak box
								increment = 0;

							for (; i < oldi + increment; i++)
							{
								spawnchance[i] = MT_PRUP;
								numchoices++;
							}
						}

						newmobj = P_SpawnMobj(mobj->x, mobj->y, mobj->z, spawnchance[prandom%numchoices]);

						// If the monitor respawns randomly, transfer the flag.
						if (mobj->flags & MF_AMBUSH)
							newmobj->flags |= MF_AMBUSH;

						// Transfer flags2 (strongbox, objectflip)
						newmobj->flags2 = mobj->flags2;
					}
					else
					{
						newmobj = P_SpawnMobj(mobj->x, mobj->y, mobj->z, mobj->type);

						// Transfer flags2 (strongbox, objectflip)
						newmobj->flags2 = mobj->flags2;
					}
					break;
				case MT_QUESTIONBOX:
					newmobj = P_SpawnMobj(mobj->x, mobj->y, mobj->z, MT_QUESTIONBOX);

					// Transfer flags2 (strongbox, objectflip)
					newmobj->flags2 = mobj->flags2;

					break;
				case MT_CHAOSSPAWNER: // Chaos Mode spawner thingy
				{
					// 8 enemies: Blue Crawla, Red Crawla, Crawla Commander,
					//            Jett-Synn Bomber, Jett-Synn Gunner, Skim,
					//            Egg Mobile, Egg Slimer.
					// Max. 3 chances per enemy.

#ifdef CHAOSISNOTDEADYET

					mobjtype_t spawnchance[8*3], enemy;
					mobj_t *spawnedmo;
					int i = 0, numchoices = 0, stop;
					fixed_t sfloorz, space, airspace, spawnz[8*3];

					sfloorz = mobj->floorz;
					space = mobj->ceilingz - sfloorz;

					// This makes the assumption there is no gravity-defying water.
					// A fair assumption to make, if you ask me.
					airspace = min(space, mobj->ceilingz - mobj->watertop);

					mobj->fuse = cv_chaos_spawnrate.value*TICRATE;
					prandom = P_Random(); // Gotta love those random numbers!

					if (cv_chaos_bluecrawla.value && space >= mobjinfo[MT_BLUECRAWLA].height)
					{
						stop = i + cv_chaos_bluecrawla.value;
						for (; i < stop; i++)
						{
							spawnchance[i] = MT_BLUECRAWLA;
							spawnz[i] = sfloorz;
							numchoices++;
						}
					}
					if (cv_chaos_redcrawla.value && space >= mobjinfo[MT_REDCRAWLA].height)
					{
						stop = i + cv_chaos_redcrawla.value;
						for (; i < stop; i++)
						{
							spawnchance[i] = MT_REDCRAWLA;
							spawnz[i] = sfloorz;
							numchoices++;
						}
					}
					if (cv_chaos_crawlacommander.value
						&& space >= mobjinfo[MT_CRAWLACOMMANDER].height + 33*FRACUNIT)
					{
						stop = i + cv_chaos_crawlacommander.value;
						for (; i < stop; i++)
						{
							spawnchance[i] = MT_CRAWLACOMMANDER;
							spawnz[i] = sfloorz + 33*FRACUNIT;
							numchoices++;
						}
					}
					if (cv_chaos_jettysynbomber.value
						&& airspace >= mobjinfo[MT_JETTBOMBER].height + 33*FRACUNIT)
					{
						stop = i + cv_chaos_jettysynbomber.value;
						for (; i < stop; i++)
						{
							spawnchance[i] = MT_JETTBOMBER;
							spawnz[i] = max(sfloorz, mobj->watertop) + 33*FRACUNIT;
							numchoices++;
						}
					}
					if (cv_chaos_jettysyngunner.value
						&& airspace >= mobjinfo[MT_JETTGUNNER].height + 33*FRACUNIT)
					{
						stop = i + cv_chaos_jettysyngunner.value;
						for (; i < stop; i++)
						{
							spawnchance[i] = MT_JETTGUNNER;
							spawnz[i] = max(sfloorz, mobj->watertop) + 33*FRACUNIT;
							numchoices++;
						}
					}
					if (cv_chaos_skim.value
						&& mobj->watertop < mobj->ceilingz - mobjinfo[MT_SKIM].height
						&& mobj->watertop - sfloorz > mobjinfo[MT_SKIM].height/2)
					{
						stop = i + cv_chaos_skim.value;
						for (; i < stop; i++)
						{
							spawnchance[i] = MT_SKIM;
							spawnz[i] = mobj->watertop;
							numchoices++;
						}
					}
					if (P_BossDoesntExist())
					{
						if (cv_chaos_eggmobile1.value
							&& space >= mobjinfo[MT_EGGMOBILE].height + 33*FRACUNIT)
						{
							stop = i + cv_chaos_eggmobile1.value;
							for (; i < stop; i++)
							{
								spawnchance[i] = MT_EGGMOBILE;
								spawnz[i] = sfloorz + 33*FRACUNIT;
								numchoices++;
							}
						}
						if (cv_chaos_eggmobile2.value
							&& space >= mobjinfo[MT_EGGMOBILE2].height + 33*FRACUNIT)
						{
							stop = i + cv_chaos_eggmobile2.value;
							for (; i < stop; i++)
							{
								spawnchance[i] = MT_EGGMOBILE2;
								spawnz[i] = sfloorz + 33*FRACUNIT;
								numchoices++;
							}
						}
					}

					if (numchoices)
					{
						fixed_t fogz;

						i = prandom % numchoices;
						enemy = spawnchance[i];

						fogz = spawnz[i] - 32*FRACUNIT;
						if (fogz < sfloorz)
							fogz = sfloorz;

						spawnedmo = P_SpawnMobj(mobj->x, mobj->y, spawnz[i], enemy);
						P_SpawnMobj(mobj->x, mobj->y, fogz, MT_TFOG);

						P_SupermanLook4Players(spawnedmo);
						if (spawnedmo->target && spawnedmo->type != MT_SKIM)
							P_SetMobjState(spawnedmo, spawnedmo->info->seestate);
					}
#endif
					break;
				}
				case MT_EGGTRAP: // Don't remove
					break;
				default:
					if (mobj->info->deathstate)
						P_ExplodeMissile(mobj);
					else
					{
						//P_UnsetThingPosition(mobj); // SRB2CBTODO: Is this better than just S_DISS?
						P_SetMobjState(mobj, S_DISS);
					}
					break;
			}
		}
	}

#ifdef VPHYSICS
	if (mobj->subsector && mobj->subsector->sector->f_slope && P_IsObjectOnGround(mobj))
	{
		v3float_t vector = mobj->subsector->sector->f_slope->normalf;

		if (mobj->subsector->sector->f_slope->zangle > 25) // Start sliding at steep angles
		{
			mobj->momx += FLOAT_TO_FIXED(vector.x);
			mobj->momy += FLOAT_TO_FIXED(vector.y);
		}

		if (mobj->pitchangle/(ANG45/45) > 0) // You're going downhill
		{
			float vadd = (1.0f+(mobj->pitchangle/(ANG45/45))/30.0f);

			fixed_t vspeed = P_AproxDistance(mobj->momx, mobj->momy)>>FRACBITS;
			if (vadd > 1.15f) // If the vadd is too low, you don't gain much momentum from this slope
			{
				mobj->momx += FLOAT_TO_FIXED(vector.x*vadd);
				mobj->momy += FLOAT_TO_FIXED(vector.y*vadd);

				if (vspeed > 35) // Going fast? MOAHR SPEED!!!!
				{
					mobj->momx += FLOAT_TO_FIXED(vector.x);
					mobj->momy += FLOAT_TO_FIXED(vector.y);
				}
			}
		}
	}

	// Way easier and less complicated VECTOR BASED CODE!!! YESS!!
	if (mobj->subsector && mobj->subsector->sector->f_slope && P_IsObjectOnGround(mobj))
	{
		// Let's make a vector for the mobj!
		v3float_t point1;
		v3float_t point2;

		point1.x = FIXED_TO_FLOAT(mobj->x);
		point1.y = FIXED_TO_FLOAT(mobj->y);
		point1.z = FIXED_TO_FLOAT(mobj->z);

		fixed_t mangle = mobj->angle>>ANGLETOFINESHIFT;

		pslope_t* cslope = mobj->subsector->sector->f_slope;

		fixed_t addx, addy;
		addx = FixedMul(200*FRACUNIT, FINECOSINE(mangle));
		addy = FixedMul(200*FRACUNIT, FINESINE(mangle));

		// Make a vector that's level to the ground (NOT THE SLOPE),
		// that way we can get the mobj's pitchangle based on
		// the angle between the mobj's vector and the slope's normal
		point2.x = FIXED_TO_FLOAT(mobj->x+addx);
		point2.y = FIXED_TO_FLOAT(mobj->y+addy);
		point2.z = FIXED_TO_FLOAT(mobj->z);//P_GetZAtf(cslope, point2.x, point2.y); // TODO: Use this for cool effects like going off a slope

		v3float_t mobjvec;
		mobjvec = *M_MakeVec3f(&point1, &point2, &mobjvec);

		fixed_t pangle = FV_AngleBetweenVectorsf(&cslope->normalf, &mobjvec)* 180 / M_PI;
		pangle -= 90; // Adjust pitch angle to correct orientation

		mobj->pitchangle = pangle*(ANG45/45);
	}
	else
		mobj->pitchangle = 0; // Otherwise, you're on flat ground TODO: Make a smoother transistion like spriteroll
#endif

	if (mobj->momx || mobj->momy || (mobj->flags2 & MF2_SKULLFLY))
	{
		P_XYMovement(mobj);
#ifdef ESLOPE // Speed limit protection so objects can't go inside of a slope
		if (mobj->subsector->sector && mobj->subsector->sector->f_slope)
		{
			if (mobj->z < P_GetZAt(mobj->subsector->sector->f_slope, mobj->x, mobj->y))
				mobj->z = P_GetZAt(mobj->subsector->sector->f_slope, mobj->x, mobj->y);
			if (mobj->floorz < P_GetZAt(mobj->subsector->sector->f_slope, mobj->x, mobj->y))
				mobj->floorz = P_GetZAt(mobj->subsector->sector->f_slope, mobj->x, mobj->y);
		}
		if (mobj->subsector->sector && mobj->subsector->sector->c_slope)
		{
			if (mobj->z > P_GetZAt(mobj->subsector->sector->c_slope, mobj->x, mobj->y))
				mobj->z = P_GetZAt(mobj->subsector->sector->c_slope, mobj->x, mobj->y);
			if (mobj->ceilingz > P_GetZAt(mobj->subsector->sector->c_slope, mobj->x, mobj->y))
				mobj->ceilingz = P_GetZAt(mobj->subsector->sector->c_slope, mobj->x, mobj->y);
		}
#endif

		if (mobj->thinker.function.acv == P_RemoveThinkerDelayed)
			return; // mobj was removed
	}

	// always do the gravity bit now, that's simpler
	// BUT CheckPosition only if wasn't done before.

	// SRB2CBTODO: It is important to use != floorz, need a ceiling eqivalent for flipped objects?
	if (!(mobj->eflags & MFE_ONGROUND) || mobj->momz
		|| ((mobj->eflags & MFE_VERTICALFLIP) && mobj->z + mobj->height != mobj->ceilingz)
		|| (!(mobj->eflags & MFE_VERTICALFLIP) && mobj->z != mobj->floorz))
	{
		mobj_t *onmo;
		onmo = P_CheckOnmobj(mobj);
		if (!onmo)
		{
			P_ZMovement(mobj); // SRB2CBTODO: I think this always needs to be done before position checked
			P_CheckPosition(mobj, mobj->x, mobj->y); // Need this to pick up objects!
			if (mobj->flags2 & MF2_ONMOBJ)
				mobj->flags2 &= ~MF2_ONMOBJ;
		}

		if (mobj->thinker.function.acv == P_RemoveThinkerDelayed)
			return; // mobj was removed
	}
	else
		mobj->eflags &= ~MFE_JUSTHITFLOOR;

	// Crush enemies!
	if ((mobj->flags & MF_ENEMY || mobj->flags & MF_BOSS)
		&& mobj->flags & MF_SHOOTABLE && mobj->health > 0)
	{
		if (mobj->ceilingz - mobj->floorz < mobj->height)
			P_DamageMobj(mobj, NULL, NULL, 1);
	}

	P_CycleMobjState(mobj);

	// The color of a ballon can be set by the ballon's angle if the balloon doesn't aleady have a color
#if 0 // SRB2CBTODO: Different color changing balloons could be used for something?
	if (mobj->type == MT_BALLOON || mobj->type == MT_BALLOONR)
	{
		if (!mobj->color)
		{
			byte bcolor;
			bcolor = mobj->angle/(ANG45/45);

			if (!bcolor)
				bcolor = 1;

			if (bcolor > MAXSKINCOLORS-1)
				bcolor = MAXSKINCOLORS-1;

			mobj->flags |= MF_TRANSLATION;
			mobj->color = bcolor;
		}

		// Remove a balloon's spring properties when it's deaf'd
		if (mobj->flags & MF_AMBUSH)
			mobj->flags &= ~MF_SPRING;
	}
#endif

	switch (mobj->type)
	{
		case MT_BOUNCEPICKUP:
		case MT_RAILPICKUP:
		case MT_AUTOPICKUP:
		case MT_EXPLODEPICKUP:
		case MT_SCATTERPICKUP:
		case MT_GRENADEPICKUP:
			if (mobj->health == 0) // Fading tile
			{
				int value = mobj->info->damage/10;
				value = mobj->fuse/value;
				value = 10-value;
				value--;

				if (value <= 0)
					value = 1;

				mobj->frame &= ~FF_TRANSMASK;
				mobj->frame |= value << FF_TRANSSHIFT;
			}
			break;
		case MT_GHOST: // fade out...
			if ((mobj->fuse % mobj->info->painchance) == 0)
			{
				int value = mobj->frame >> FF_TRANSSHIFT;

				value++;

				if (value >= NUMTRANSMAPS)
					P_SetMobjState(mobj, S_DISS);
				else
				{
					mobj->frame &= ~FF_TRANSMASK;
					mobj->frame |= value << FF_TRANSSHIFT;
				}
			}
			break;
		default:
			break;
	}
}

// Quick, optimized function for the Rail Rings
void P_RailThinker(mobj_t *mobj)
{
	// momentum movement
	if (mobj->momx || mobj->momy)
	{
		P_XYMovement(mobj);

		if (mobj->thinker.function.acv == P_RemoveThinkerDelayed)
			return; // mobj was removed
	}

	// always do the gravity bit now, that's simpler
	// BUT CheckPosition only if wasn't done before.
	if (mobj->momz)
	{
		P_ZMovement(mobj);
		P_CheckPosition(mobj, mobj->x, mobj->y); // Need this to pick up objects!

		if (mobj->thinker.function.acv == P_RemoveThinkerDelayed)
			return; // mobj was removed
	}
}

// Unquick, unoptimized function for pushables
void P_PushableThinker(mobj_t *mobj)
{
	sector_t *sec;
	sec = mobj->subsector->sector;

	fixed_t mfloorz;
	fixed_t mceilingz;

	mfloorz = sec->floorheight;
#ifdef ESLOPE
	if (sec->f_slope)
		mfloorz = P_GetZAt(sec->f_slope, mobj->x, mobj->y);
#endif

	mceilingz = sec->ceilingheight;
#ifdef ESLOPE
	if (sec->c_slope)
		mceilingz = P_GetZAt(sec->c_slope, mobj->x, mobj->y);
#endif

	if (GETSECSPECIAL(sec->special, 2) == 1 && mobj->z == mfloorz)
		P_LinedefExecute(sec->tag, mobj, sec);
//	else if (GETSECSPECIAL(sec->special, 2) == 8)
	{
		sector_t *sec2;

		sec2 = P_ThingOnSpecial3DFloor(mobj);
		if (sec2 && GETSECSPECIAL(sec2->special, 2) == 1)
			P_LinedefExecute(sec2->tag, mobj, sec2);
	}

	// it has to be pushable RIGHT NOW for this part to happen
	if (mobj->flags & MF_PUSHABLE && !(mobj->momx || mobj->momy))
		P_TryMove(mobj, mobj->x, mobj->y, true);

	if (mobj->fuse == 1) // it would explode in the MobjThinker code
	{
		mobj_t *spawnmo;
		fixed_t x, y, z;
		subsector_t *ss;

		// Left here just in case we'd
		// want to make pushable bombs
		// or something in the future.
		switch (mobj->type)
		{
			case MT_SNOWMAN:
			case MT_GARGOYLE:
				x = mobj->spawnpoint->x << FRACBITS;
				y = mobj->spawnpoint->y << FRACBITS;

				ss = R_PointInSubsector(x, y);

				fixed_t ssfloorz;
				ssfloorz = ss->sector->floorheight;
#ifdef ESLOPE
				if (ss->sector->f_slope)
					ssfloorz = P_GetZAt(ss->sector->f_slope, x, y);
#endif

				if (mobj->spawnpoint->z != 0)
					z = mobj->spawnpoint->z << FRACBITS;
				else
					z = ssfloorz;

				spawnmo = P_SpawnMobj(x, y, z, mobj->type);
				spawnmo->spawnpoint = mobj->spawnpoint;
				P_SetMobjState(mobj, S_DISS);
				P_UnsetThingPosition(spawnmo);
				if (sector_list)
				{
					P_DelSeclist(sector_list);
					sector_list = NULL;
				}
				spawnmo->flags = mobj->flags;
				P_SetThingPosition(spawnmo);
				spawnmo->flags2 = mobj->flags2;
				spawnmo->flags |= MF_PUSHABLE;
				break;
			default:
				break;
		}
	}
}

// Quick, optimized function for scenery
void P_SceneryThinker(mobj_t *mobj)
{
	if (mobj->flags & MF_BOXICON)
	{
		if (!(mobj->eflags & MFE_VERTICALFLIP))  // SRB2CBCHECK: needs SCALE! // SRB2CBTODO: Could be cause of bugs?
		{
			if (mobj->z < mobj->floorz + mobj->info->damage)
				mobj->momz = mobj->info->speed;
			else
				mobj->momz = 0;
		}
		else
		{
			if (mobj->z > mobj->ceilingz - mobj->info->damage)
				mobj->momz = -mobj->info->speed;
			else
				mobj->momz = 0;
		}
	}

	// momentum movement
	if (mobj->momx || mobj->momy)
	{
		P_SceneryXYMovement(mobj);
#ifdef ESLOPE // Speed limit protection so objects can't go inside of a slope
		if (mobj->subsector->sector && mobj->subsector->sector->f_slope)
		{
			if (mobj->z < P_GetZAt(mobj->subsector->sector->f_slope, mobj->x, mobj->y))
				mobj->z = P_GetZAt(mobj->subsector->sector->f_slope, mobj->x, mobj->y);
			if (mobj->floorz < P_GetZAt(mobj->subsector->sector->f_slope, mobj->x, mobj->y))
				mobj->floorz = P_GetZAt(mobj->subsector->sector->f_slope, mobj->x, mobj->y);
		}
		if (mobj->subsector->sector && mobj->subsector->sector->c_slope)
		{
			if (mobj->z > P_GetZAt(mobj->subsector->sector->c_slope, mobj->x, mobj->y))
				mobj->z = P_GetZAt(mobj->subsector->sector->c_slope, mobj->x, mobj->y);
			if (mobj->ceilingz > P_GetZAt(mobj->subsector->sector->c_slope, mobj->x, mobj->y))
				mobj->ceilingz = P_GetZAt(mobj->subsector->sector->c_slope, mobj->x, mobj->y);
		}
#endif

		if (mobj->thinker.function.acv == P_RemoveThinkerDelayed)
			return; // mobj was removed
	}

	// always do the gravity bit now, that's simpler
	// BUT CheckPosition only if wasn't done before.
	if (!(mobj->eflags & MFE_ONGROUND) || mobj->momz
		|| ((mobj->eflags & MFE_VERTICALFLIP) && mobj->z + mobj->height != mobj->ceilingz)
		|| (!(mobj->eflags & MFE_VERTICALFLIP) && mobj->z != mobj->floorz))
	{
		mobj_t *onmo;
		onmo = P_CheckOnmobj(mobj);
		if (!onmo)
		{
			P_SceneryZMovement(mobj);
			P_CheckPosition(mobj, mobj->x, mobj->y); // Need this to pick up objects!
			// ESLOPE: The slope's z for tmfloor/ceilingz is already set in P_CheckPosition
			mobj->floorz = tmfloorz;
			mobj->ceilingz = tmceilingz;

			if (mobj->flags2 & MF2_ONMOBJ)
				mobj->flags2 &= ~MF2_ONMOBJ;
		}

		if (mobj->thinker.function.acv == P_RemoveThinkerDelayed)
			return; // mobj was removed
	}
	else
		mobj->eflags &= ~MFE_JUSTHITFLOOR;

	P_CycleMobjState(mobj);
}

//
// P_SpawnMobj
//
mobj_t *P_SpawnMobj(fixed_t x, fixed_t y, fixed_t z, mobjtype_t type) // TODO: Make sure there's enough memory first!
{
	const mobjinfo_t *info = &mobjinfo[type];
	state_t *st;
	mobj_t *mobj = NULL;

	mobj = Z_Calloc(sizeof(*mobj), PU_LEVEL, NULL);

	mobj->type = type;
	mobj->info = info;

	mobj->x = x;
	mobj->y = y;

	mobj->radius = info->radius;
	mobj->height = info->height;
	mobj->flags = info->flags;

	mobj->health = info->spawnhealth;

	mobj->reactiontime = info->reactiontime;

	mobj->lastlook = -1; // stuff moved in P_enemy.P_LookForPlayer

	// do not set the state with P_SetMobjState,
	// because action routines can not be called yet
	st = &states[info->spawnstate];

	mobj->state = st;
	mobj->tics = st->tics;
	mobj->sprite = st->sprite;
	mobj->frame = st->frame; // FF_FRAMEMASK for frame, and other bits..
	mobj->friction = ORIG_FRICTION;

	// All mobjs are created at 100% scale.
	mobj->scale = 100;
	mobj->destscale = mobj->scale; // NOTE: This is only called once when spawning a mobj
	mobj->scalespeed = 8;

	fixed_t mfloorz;
	fixed_t mceilingz;
	mfloorz = R_PointInSubsector(x, y)->sector->floorheight;
	mceilingz = R_PointInSubsector(x, y)->sector->ceilingheight;
#ifdef ESLOPE
	if (R_PointInSubsector(x, y)->sector->f_slope)
		mfloorz = P_GetZAt(R_PointInSubsector(x, y)->sector->f_slope, x, y);
	if (R_PointInSubsector(x, y)->sector->c_slope)
		mceilingz = P_GetZAt(R_PointInSubsector(x, y)->sector->c_slope, x, y);
#endif

	// SRB2CBTODO: Depending on the type of object, they may scale up/down faster.
	switch (type)
	{
		case MT_THOK:
			mobj->scalespeed = 3;
			break;
		case MT_BLACKORB:
		case MT_WHITEORB:
		case MT_GREENORB:
		case MT_YELLOWORB:
		case MT_BLUEORB:
#ifdef SRB2K
		case MT_BOUNCEORB:
		case MT_REDFIREORB:
		case MT_ELECTRICORB:
#endif
			mobj->scalespeed = 1;
			break;
		//case MT_PARTICLE: // Particles can't scale up or their z values are effected too much
		case MT_SMALLBUBBLE:
		case MT_MEDIUMBUBBLE:
		case MT_EXTRALARGEBUBBLE:
			mobj->scale = 8;
			mobj->scalespeed = 8;
			mobj->destscale = 100;
			break;
		default:
			break;
	}

#ifdef SPRITEROLL
	// All mobjs are created upright.
	mobj->rollangle = 0;
	mobj->destrollangle = mobj->rollangle;
	mobj->rollspeed = 8;
#endif
#ifdef VPHYSICS
	mobj->pitchangle = 0;
#endif

	mobj->movefactor = ORIG_FRICTION_FACTOR;

	// SRB2CBTODO: TODO: Make this a special map header
	if ((maptol & TOL_ERZ3) && !(mobj->type == MT_BLACKEGGMAN
	|| mobj->type == MT_BLACKEGGMAN_GOOPFIRE || mobj->type == MT_BLACKEGGMAN_HELPER || mobj->type == MT_BLACKEGGMAN_MISSILE
	|| mobj->type == MT_SMOK || mobj->type == MT_GOOP || mobj->type == MT_POP))
	{
		// 0 scalespeed means you instantly scale
		mobj->scalespeed = 0;
		mobj->destscale = 25;
		P_SetScale(mobj, 25);
	}

	switch (mobj->type)
	{
		case MT_BLACKEGGMAN:
			{
				mobj_t *spawn = P_SpawnMobj(mobj->x, mobj->z, mobj->z+mobj->height-16*FRACUNIT, MT_BLACKEGGMAN_HELPER);
				P_SetTarget(&spawn->target, mobj);
			}
			break;
		case MT_DETON:
			mobj->movedir = 0;
			break;
		case MT_EGGGUARD:
			{
				mobj_t *spawn = P_SpawnMobj(x, y, z, MT_EGGSHIELD);
				P_SetTarget(&mobj->tracer, spawn);
				P_SetTarget(&spawn->tracer, mobj);
			}
			break;
		case MT_BIRD:
		case MT_BUNNY:
		case MT_MOUSE:
		case MT_CHICKEN:
		case MT_COW:
			mobj->fuse = 300 + (P_Random() % 50);
			break;
		case MT_REDRING: // Make MT_REDRING red by default
			mobj->flags |= MF_TRANSLATION;
			mobj->color = 6;
			break;
		case MT_SMALLBUBBLE: // Bubbles eventually dissipate, in case they get caught somewhere.
		case MT_MEDIUMBUBBLE:
		case MT_EXTRALARGEBUBBLE:
			mobj->fuse += 30 * TICRATE;
			break;
		case MT_BUSH:
		case MT_BERRYBUSH:
		case MT_THZPLANT:
		case MT_GFZFLOWER1:
		case MT_GFZFLOWER2:
		case MT_GFZFLOWER3:
		case MT_CEZFLOWER:
		case MT_SEAWEED:
		case MT_CORAL1:
		case MT_CORAL2:
		case MT_CORAL3:
			//mobj->flags |= MF_NOBLOCKMAP; // SRB2CBTODO: BLOCKMAP is needed if any object needs to be around a sector
			break;
		case MT_REDTEAMRING:
			mobj->flags |= MF_TRANSLATION;
			mobj->color = 6;
			mobj->flags |= MF_NOCLIPHEIGHT;
			break;
		case MT_BLUETEAMRING:
			mobj->flags |= MF_TRANSLATION;
			mobj->color = 7;
			mobj->flags |= MF_NOCLIPHEIGHT;
			break;
		case MT_RING:
		case MT_COIN:
#ifdef BLUE_SPHERES
		case MT_BLUEBALL:
#endif
			nummaprings++;
			break;
		case MT_BOUNCERING:
		case MT_RAILRING:
		case MT_AUTOMATICRING:
		case MT_EXPLOSIONRING:
		case MT_SCATTERRING:
		case MT_GRENADERING:
		case MT_BOUNCEPICKUP:
		case MT_RAILPICKUP:
		case MT_AUTOPICKUP:
		case MT_EXPLODEPICKUP:
		case MT_SCATTERPICKUP:
		case MT_GRENADEPICKUP:
		case MT_NIGHTSWING:
		case MT_BLUEORB:
		case MT_BLACKORB:
		case MT_WHITEORB:
		case MT_YELLOWORB:
		case MT_GREENORB:
#ifdef SRB2K
		case MT_BOUNCEORB:
		case MT_REDFIREORB:
		case MT_ELECTRICORB:
#endif
		case MT_THOK:
		case MT_GHOST:
			mobj->flags |= MF_NOCLIPHEIGHT;
		default:
			break;
	}

	// Give the mobj a ->subsector and/or block links
	P_SetThingPosition(mobj);
	//P_CheckPosition(mobj, x, y); // SRB2CBTODO: Needed?

	mobj->floorz = mfloorz;
	mobj->ceilingz = mceilingz;

	if (z == ONFLOORZ)
	{
		// defaults onground
		mobj->eflags |= MFE_ONGROUND;

		if ((mobj->type == MT_RING || mobj->type == MT_COIN || mobj->type == MT_REDTEAMRING || mobj->type == MT_BLUETEAMRING ||
			mobj->type == MT_BOUNCERING || mobj->type == MT_RAILRING || mobj->type == MT_AUTOMATICRING ||
			mobj->type == MT_EXPLOSIONRING || mobj->type == MT_SCATTERRING || mobj->type == MT_GRENADERING ||
			mobj->type == MT_BOUNCEPICKUP || mobj->type == MT_RAILPICKUP || mobj->type == MT_AUTOPICKUP ||
			mobj->type == MT_EXPLODEPICKUP || mobj->type == MT_SCATTERPICKUP || mobj->type == MT_GRENADEPICKUP
			|| mobj->type == MT_EMMY) && mobj->flags & MF_AMBUSH)
			mobj->z = mfloorz + 32*FRACUNIT;
		else
			mobj->z = mfloorz;
	}
	// SRB2CBTODO: Upside-down stuff is slightly strange because the mobjs' heights need to be more acurate,
	// this is not related to this function but instead the object's hard-coded height data, this is not an issue
	// when moving to a total conversion
	else if (z == ONCEILINGZ)
	{
		mobj->z = mceilingz - mobj->height;
	}
	else
		mobj->z = z;

#if 0 // was stupid
	if (mobj->z + mobj->height > mobj->ceilingz)
		mobj->z = mobj->ceilingz - mobj->height;
	if (mobj->z < mobj->floorz)
		mobj->z = mobj->floorz;
#endif

	// Kalaron: Always add a thinker, even if the mobj has MF_NOTHINK
	mobj->thinker.function.acp1 = (actionf_p1)P_MobjThinker;
	P_AddThinker(&mobj->thinker);

	// Call action functions when the state is set
	if (st->action.acp1 && (mobj->flags & MF_RUNSPAWNFUNC))
	{
		if (LoadingCachedActions)
		{
			// Cache actions in a linked list
			// with function pointer, and
			// var1 & var2, which will be executed
			// when the level finishes loading.
			P_AddCachedAction(mobj, mobj->info->spawnstate);
		}
		else
		{
			var1 = st->var1;
			var2 = st->var2;
			st->action.acp1(mobj);
		}
	}

	if (CheckForReverseGravity && !(mobj->flags & MF_NOBLOCKMAP))
		P_CheckGravity(mobj, false);

	return mobj;
}

static inline precipmobj_t *P_SpawnRainMobj(fixed_t x, fixed_t y, fixed_t z, mobjtype_t type)
{
	state_t *st;
	// PU_LEVEL is more effecient than a manual calloc since the mobj cannot be freed until the level ends
	precipmobj_t *mobj = Z_Calloc(sizeof (*mobj), PU_LEVEL, NULL);

	mobj->x = x;
	mobj->y = y;
	mobj->flags = mobjinfo[type].flags;

	// Do not set the state with P_SetMobjState,
	// because action routines can not be called yet
	st = &states[mobjinfo[type].spawnstate];

	mobj->state = st;
	mobj->tics = st->tics;
	mobj->sprite = st->sprite;
	mobj->frame = st->frame; // FF_FRAMEMASK for frame, and other bits..

	// set subsector and/or block links
	P_SetPrecipitationThingPosition(mobj);

	fixed_t mfloorz;
	fixed_t mceilingz;
	mfloorz = R_PointInSubsector(x, y)->sector->floorheight;
	mceilingz = R_PointInSubsector(x, y)->sector->ceilingheight;
#ifdef ESLOPE
	if (R_PointInSubsector(x, y)->sector->f_slope)
		mfloorz = P_GetZAt(R_PointInSubsector(x, y)->sector->f_slope, x, y);
	if (R_PointInSubsector(x, y)->sector->c_slope)
		mceilingz = P_GetZAt(R_PointInSubsector(x, y)->sector->c_slope, x, y);
#endif

	mobj->floorz = mfloorz;
	mobj->ceilingz = mceilingz;

	mobj->z = z;
	mobj->momz = mobjinfo[type].speed/NEWTICRATERATIO;

	mobj->thinker.function.acp1 = (actionf_p1)P_RainThinker;
	P_AddThinker(&mobj->thinker);

	CalculatePrecipFloor(mobj);

	return mobj;
}

static precipmobj_t *P_SpawnSnowMobj(fixed_t x, fixed_t y, fixed_t z, mobjtype_t type)
{
	state_t *st;
	// PU_LEVEL is more effecient than a manual calloc since the mobj cannot be freed until the level ends
	precipmobj_t *mobj = Z_Calloc(sizeof (*mobj), PU_LEVEL, NULL);

	mobj->x = x;
	mobj->y = y;
	mobj->flags = mobjinfo[type].flags;

	// Do not set the state with P_SetMobjState,
	// because action routines can not be called yet
	st = &states[mobjinfo[type].spawnstate];

	mobj->state = st;
	mobj->tics = st->tics;
	mobj->sprite = st->sprite;
	mobj->frame = st->frame; // FF_FRAMEMASK for frame, and other bits..

	// set subsector and/or block links
	P_SetPrecipitationThingPosition(mobj);

	fixed_t mfloorz;
	fixed_t mceilingz;
	mfloorz = R_PointInSubsector(x, y)->sector->floorheight;
	mceilingz = R_PointInSubsector(x, y)->sector->ceilingheight;
#ifdef ESLOPE
	if (R_PointInSubsector(x, y)->sector->f_slope)
		mfloorz = P_GetZAt(R_PointInSubsector(x, y)->sector->f_slope, x, y);
	if (R_PointInSubsector(x, y)->sector->c_slope)
		mceilingz = P_GetZAt(R_PointInSubsector(x, y)->sector->c_slope, x, y);
#endif

	mobj->floorz = mfloorz;
	mobj->ceilingz = mceilingz;

	mobj->z = z;
	mobj->momz = mobjinfo[type].speed/NEWTICRATERATIO;

	mobj->thinker.function.acp1 = (actionf_p1)P_SnowThinker;
	P_AddThinker(&mobj->thinker);

	CalculatePrecipFloor(mobj);

	return mobj;
}

//
// P_RemoveMobj
//
mapthing_t *itemrespawnque[ITEMQUESIZE];
tic_t itemrespawntime[ITEMQUESIZE];
size_t iquehead, iquetail;

void P_RemoveMobj(mobj_t *mobj)
{
	// Rings only, please!
	if (mobj->spawnpoint &&
	  (mobj->type == MT_RING
	  || mobj->type == MT_COIN
#ifdef BLUE_SPHERES
	  || mobj->type == MT_BLUEBALL
#endif
	  || mobj->type == MT_REDTEAMRING || mobj->type == MT_BLUETEAMRING
	  || mobj->type == MT_BOUNCERING
	  || mobj->type == MT_RAILRING
	  || mobj->type == MT_AUTOMATICRING
	  || mobj->type == MT_EXPLOSIONRING
	  || mobj->type == MT_SCATTERRING
	  || mobj->type == MT_GRENADERING
	  || mobj->type == MT_BOUNCEPICKUP
	  || mobj->type == MT_RAILPICKUP
	  || mobj->type == MT_AUTOPICKUP
	  || mobj->type == MT_EXPLODEPICKUP
	  || mobj->type == MT_SCATTERPICKUP
	  || mobj->type == MT_GRENADEPICKUP)
		&& !(mobj->flags2 & MF2_DONTRESPAWN))
	{
		itemrespawnque[iquehead] = mobj->spawnpoint;
		itemrespawntime[iquehead] = leveltime;
		iquehead = (iquehead+1)&(ITEMQUESIZE-1);
		// lose one off the end?
		if (iquehead == iquetail)
			iquetail = (iquetail+1)&(ITEMQUESIZE-1);
	}

	mobj->health = 0; // Just because

	// unlink from sector and block lists
	P_UnsetThingPosition(mobj);

	// Remove touching_sectorlist from mobj.
	if (sector_list)
	{
		P_DelSeclist(sector_list);
		sector_list = NULL;
	}

	// stop any playing sound
	S_StopSound(mobj);

	if (mobj->type == MT_EGGGUARD && mobj->tracer)
		P_SetMobjState(mobj->tracer, S_DISS);

	// killough 11/98:
	//
	// Remove any references to other mobjs.
	P_SetTarget(&mobj->target, P_SetTarget(&mobj->tracer, NULL));

	// free block
	P_RemoveThinker((thinker_t *)mobj);
}

void P_RemovePrecipMobj(precipmobj_t *mobj)
{
	// unlink from sector and block lists
	P_UnsetPrecipThingPosition(mobj);

	if (precipsector_list)
	{
		P_DelPrecipSeclist(precipsector_list);
		precipsector_list = NULL;
	}

	// free block
	P_RemoveThinker((thinker_t *)mobj);
}

// Clearing out stuff for savegames
void P_RemoveSavegameMobj(mobj_t *mobj)
{
	// unlink from sector and block lists
	P_UnsetThingPosition(mobj);

	// Remove touching_sectorlist from mobj.
	if (sector_list)
	{
		P_DelSeclist(sector_list);
		sector_list = NULL;
	}

	// stop any playing sound
	S_StopSound(mobj);

	// free block
	P_RemoveThinker((thinker_t *)mobj);
}


boolean P_CheckSameMobjAtPos(mobjtype_t type, fixed_t x, fixed_t y, fixed_t z, boolean checkz)
{
	thinker_t *th;

	for (th = thinkercap.next; th != &thinkercap; th = th->next)
	{
		if (th->function.acp1 != (actionf_p1)P_MobjThinker)
			continue;

		if (((mobj_t *)th)->type != type)
			continue;

		if (checkz)
		{
			if (((mobj_t *)th)->x == x
				&& ((mobj_t *)th)->y == y
				&& ((mobj_t *)th)->z == z)
				return true;
		}
		else
		{

			if (((mobj_t *)th)->x == x
				&& ((mobj_t *)th)->y == y)
				return true;
		}
	}

	return false;
}

static void Respawn_OnChange(void)
{
	// Only restrict this in single player unless in devmode, whew.
	if (!(netgame || multiplayer) && !cv_devmode
		&& gametype == GT_COOP && cv_itemrespawn.value)
		CV_SetValue(&cv_itemrespawn, 0);
}

static CV_PossibleValue_t respawnitemtime_cons_t[] = {{1, "MIN"}, {300, "MAX"}, {0, NULL}};
consvar_t cv_itemrespawntime = {"respawnitemtime", "30", CV_NETVAR|CV_CHEAT, respawnitemtime_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_itemrespawn = {"respawnitem", "Off", CV_NETVAR|CV_CALL, CV_OnOff, Respawn_OnChange, 0, NULL, NULL, 0, 0, NULL};
static CV_PossibleValue_t flagtime_cons_t[] = {{0, "MIN"}, {300, "MAX"}, {0, NULL}};
consvar_t cv_flagtime = {"flagtime", "30", CV_NETVAR, flagtime_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_suddendeath = {"suddendeath", "Off", CV_NETVAR, CV_OnOff, NULL, 0, NULL, NULL, 0, 0, NULL};

static fixed_t P_Rand(void)
{
	// Do not use RAND_MAX, it equals 0x7fffffff(2147483647) on some OS's,
	// but may equal 0x7FFF(32767) under Window's rand(), this difference may cause undesired operation,
	// so just use Window's random // SRB2CBTODO: Use this for any other differences
	const unsigned int d = (unsigned int)rand()*FRACUNIT;
	const fixed_t t = (fixed_t)(d/0x7FFF);
	return (t-FRACUNIT/2)<<FRACBITS;
}

static boolean P_ObjectInWater(sector_t *sector, fixed_t z)
{
	if (sector->ffloors)
	{
		ffloor_t *rover;

		for (rover = sector->ffloors; rover; rover = rover->next)
		{
			if (!(rover->flags & FF_EXISTS))
				continue;

			if (rover->flags & FF_SWIMMABLE)
			{
				if (*rover->topheight >= z
					&& *rover->bottomheight <= z)
					return true;
			}
		}
	}
	return false;
}

//
// P_IsObjectOnGround
//
// Returns true if the player is
// on the ground. Takes reverse
// gravity into account.
//
boolean P_IsObjectOnGround(mobj_t *mo)
{
	if (mo->eflags & MFE_VERTICALFLIP)
	{
		if (mo->z + mo->height >= mo->ceilingz) // SRB2CBTODO: allow being on underside of mobj too?
			return true;
	}
	else
	{
		if ((mo->z <= mo->floorz) || (mo->flags2 & MF2_ONMOBJ))
			return true;
	}

	return false;
}

#ifdef ESLOPE
//
// P_IsObjectOnSlope
//
// Returns true if the player is
// on a slope. Takes reverse
// gravity into account.
//
boolean P_IsObjectOnSlope(mobj_t *mo, boolean ceiling)
{
	if (ceiling && (mo->eflags & MFE_VERTICALFLIP))
	{
		if ((mo->z + mo->height >= mo->ceilingz) && mo->subsector->sector->c_slope) // SRB2CBTODO: allow being on underside of mobj too?
			return true;
	}
	else
	{
		if (((mo->z <= mo->floorz) || (mo->flags2 & MF2_ONMOBJ)) && mo->subsector->sector->f_slope)
			return true;
	}

	return false;
}


//
// P_SlopeGreaterThan
//
// Returns true if the object is on a slope
// that has an angle greater than the value
//
boolean P_SlopeGreaterThan(mobj_t *mo, boolean ceiling, int value)
{
	if (ceiling && (mo->eflags & MFE_VERTICALFLIP))
	{
		if ((mo->z + mo->height >= mo->ceilingz) && mo->subsector->sector->c_slope)
		{
			if (value < mo->subsector->sector->c_slope->zangle)
				return true;
		}
	}
	else
	{
		if (((mo->z <= mo->floorz) || (mo->flags2 & MF2_ONMOBJ)) && mo->subsector->sector->f_slope)
		{
			if (value < mo->subsector->sector->f_slope->zangle)
				return true;
		}
	}

	return false;
}

//
// P_SlopeLessThan
//
// Returns true if the object is on a slope
// that has an angle less than the value
//
boolean P_SlopeLessThan(mobj_t *mo, boolean ceiling, int value)
{
	if (ceiling && (mo->eflags & MFE_VERTICALFLIP))
	{
		if ((mo->z + mo->height >= mo->ceilingz) && mo->subsector->sector->c_slope)
		{
			if (value < mo->subsector->sector->c_slope->zangle)
				return true;
		}
	}
	else
	{
		if (((mo->z <= mo->floorz) || (mo->flags2 & MF2_ONMOBJ)) && mo->subsector->sector->f_slope)
		{
			if (value < mo->subsector->sector->f_slope->zangle)
				return true;
		}
	}

	return false;
}
#endif

//
// P_IsObjectOnGround
//
// Returns true if the player is
// on the ceiling. Takes reverse
// gravity into account.
//
boolean P_IsObjectOnCeiling(mobj_t *mo) // SRB2CBTODO: USE THIS!!! Actually, no
{
	if (mo->eflags & MFE_VERTICALFLIP)
	{
		if (mo->z <= mo->floorz)
			return true;
	}
	else
	{
		if (mo->z + mo->height >= mo->ceilingz)
			return true;
	}

	return false;
}

//
// P_SetObjectMomZ
//
// Sets the player momz appropriately. // SRB2CBTODO: All objects?
// Takes reverse gravity into account.
//
void P_SetObjectMomZ(mobj_t *mo, fixed_t value, boolean relative, boolean noscale)
{
	if (mo->eflags & MFE_VERTICALFLIP)
		value = -value;

	if (!noscale)
	{
		if (mo->scale != 100)
			value = FIXEDSCALE(value, mo->scale);
	}

	if (relative)
		mo->momz += value/NEWTICRATERATIO;
	else
		mo->momz = value;
}

//
// P_SetObjectMomZ
//
// Sets an objects momz absoulutely.
// The effect only changes with the game's ratio.
//
void P_SetObjectAbsMomZ(mobj_t *mo, fixed_t value, boolean add)
{
	if (add)
		mo->momz += value/NEWTICRATERATIO;
	else
		mo->momz = value;
}

//
// P_SpawnGhostMobj
//
// Spawns a ghost object on a mobj
//
mobj_t *P_SpawnGhostMobj(mobj_t *mobj)
{
	mobj_t *ghost;

	ghost = P_SpawnMobj(mobj->x, mobj->y, mobj->z, MT_GHOST);

	// Sync proper colors with the after image
	// if the target mobj has a color
	if (mobj->player)
	{
		P_SetTarget(&ghost->target, mobj);

		if (mobj->player->powers[pw_fireflower])
		{
			ghost->flags |= MF_TRANSLATION;
			ghost->color = 13;
		}
		else if (mariomode && mobj->player->powers[pw_invulnerability] && !mobj->player->powers[pw_super])
		{
			ghost->flags |= MF_TRANSLATION;
			ghost->color = (leveltime % MAXSKINCOLORS);
		}
		else
		{
			ghost->flags |= MF_TRANSLATION;
			ghost->color = ((mobj->player->powers[pw_super]) ? 15 : mobj->player->skincolor);
		}
	}
	else
	{
		P_SetTarget(&ghost->target, mobj);

		if (mobj->color)
		{
			ghost->flags |= MF_TRANSLATION;
			ghost->color = mobj->color;
		}
	}

	ghost->rollangle = mobj->rollangle;
	ghost->destrollangle = mobj->destrollangle;
	ghost->rollspeed = mobj->rollspeed;

#ifdef VPHYSICS
	ghost->pitchangle = mobj->pitchangle;
#endif

	if (mobj->eflags & MFE_VERTICALFLIP)
		ghost->eflags |= MFE_VERTICALFLIP;

	ghost->flags |= MF_NOBLOCKMAP;
	ghost->flags |= MF_NOGRAVITY;
	ghost->flags |= MF_NOCLIP;
	ghost->flags |= MF_NOCLIPTHING;
	ghost->flags |= MF_NOCLIPHEIGHT;

	ghost->angle = mobj->angle;
	ghost->sprite = mobj->sprite;
	ghost->frame = mobj->frame;
	ghost->tics = ghost->info->damage;
	ghost->frame &= ~FF_TRANSMASK;
	// Set the transparency of the afterimage
	ghost->frame |= tr_trans50<<FF_TRANSSHIFT;
	ghost->fuse = ghost->info->damage;
	ghost->skin = mobj->skin;

	P_SetScale(ghost, mobj->scale);
	ghost->destscale = mobj->scale;

	return ghost;
}

//
// P_SpawnGhostMobj
//
// Spawns a ghost object on a mobj, with special xyz coords
//
mobj_t *P_SpawnGhostMobjXYZ(mobj_t *mobj, fixed_t x, fixed_t y, fixed_t z)
{
	mobj_t *ghost;

	ghost = P_SpawnMobj(x, y, z, MT_GHOST);

	// Sync proper colors with the after image
	// if the target mobj has a color
	if (mobj->player)
	{
		P_SetTarget(&ghost->target, mobj);

		if (mobj->player->powers[pw_fireflower])
		{
			ghost->flags |= MF_TRANSLATION;
			ghost->color = 13;
		}
		else if (mariomode && mobj->player->powers[pw_invulnerability] && !mobj->player->powers[pw_super])
		{
			ghost->flags |= MF_TRANSLATION;
			ghost->color = (leveltime % MAXSKINCOLORS);
		}
		else
		{
			ghost->flags |= MF_TRANSLATION;
			ghost->color = ((mobj->player->powers[pw_super]) ? 15 : mobj->player->skincolor);
		}
	}
	else
	{
		P_SetTarget(&ghost->target, mobj);

		if (mobj->color)
		{
			ghost->flags |= MF_TRANSLATION;
			ghost->color = mobj->color;
		}
	}

	if (mobj->eflags & MFE_VERTICALFLIP)
		ghost->eflags |= MFE_VERTICALFLIP;

	ghost->flags |= MF_NOBLOCKMAP;
	ghost->flags |= MF_NOGRAVITY;
	ghost->flags |= MF_NOCLIP;
	ghost->flags |= MF_NOCLIPTHING;
	ghost->flags |= MF_NOCLIPHEIGHT;

	ghost->angle = mobj->angle;
	ghost->sprite = mobj->sprite;
	ghost->frame = mobj->frame;
	ghost->tics = ghost->info->damage;
	ghost->frame &= ~FF_TRANSMASK;
	// Set the transparency of the afterimage
	ghost->frame |= tr_trans50<<FF_TRANSSHIFT;
	ghost->fuse = ghost->info->damage;
	ghost->skin = mobj->skin;

	P_SetScale(ghost, mobj->scale);
	ghost->destscale = mobj->scale;

	return ghost;
}

void P_SpawnPrecipitation(void)
{
	const int preloop = 1048576*8; // 0x100000 * 12
	int i;
	fixed_t x = 0, y = 0, height;
	subsector_t *precipsector = NULL;

	if (dedicated) return;

	if (curWeather == PRECIP_SNOW)
	{
		byte z = 0;

		if (rendermode != render_none)
		{
			if (gamestate != wipegamestate)
			{
				F_WipeStartScreen();

					if (!(mapheaderinfo[gamemap-1].interscreen[0] == '#'
						  && gamestate == GS_INTERMISSION))
					{
						V_DrawFill(0, 0, vid.width, vid.height, 31);
#ifdef HWRENDER
						if (rendermode == render_opengl)
							HWR_PrepFadeToBlack(false);
#endif
					}

				F_WipeEndScreen(0, 0, vid.width, vid.height);

				F_RunWipe(TICRATE);

				F_WipeStartScreen();

				WipeInAction = false;
			}
		}

		for (i = 0; i < preloop; i++)
		{
			x = P_Rand();
			y = P_Rand();
			height = P_Rand();

			precipsector = R_IsPointInSubsector(x, y);

			if (!precipsector)
				continue;

			if (!(maptol & TOL_NIGHTS) && !(precipsector->sector->ceilingpic == skyflatnum))
				continue;

			if (!(precipsector->sector->floorheight <= precipsector->sector->ceilingheight - (32<<FRACBITS)))
				continue;

			if (height < precipsector->sector->floorheight ||
				height >= precipsector->sector->ceilingheight)
				continue;

			if (P_ObjectInWater(precipsector->sector, height))
				continue;

			if (!(i % 1000) || !i)
				P_LoadingScreen("Precipitation", ((i*100)/preloop));

			z = M_Random(); // Doesn't need to use P_Random().
			if (z < 64)
				P_SetPrecipMobjState(P_SpawnSnowMobj(x, y, height, MT_SNOWFLAKE), S_SNOW3);
			else if (z < 144)
				P_SetPrecipMobjState(P_SpawnSnowMobj(x, y, height, MT_SNOWFLAKE), S_SNOW2);
			else
				P_SpawnSnowMobj(x, y, height, MT_SNOWFLAKE);
		}
	}
	else if (curWeather == PRECIP_STORM || curWeather == PRECIP_RAIN || curWeather == PRECIP_BLANK
		|| curWeather == PRECIP_STORM_NORAIN || curWeather == PRECIP_STORM_NOSTRIKES)
	{
		if (gamestate != wipegamestate)
		{
			F_WipeStartScreen();

			if (!(mapheaderinfo[gamemap-1].interscreen[0] == '#'
				  && gamestate == GS_INTERMISSION))
			{
				V_DrawFill(0, 0, vid.width, vid.height, 31);
#ifdef HWRENDER
				if (rendermode == render_opengl)
					HWR_PrepFadeToBlack(false);
#endif
			}

			F_WipeEndScreen(0, 0, vid.width, vid.height);

			F_RunWipe(TICRATE);

			F_WipeStartScreen();

			WipeInAction = false;
		}

		for (i = 0; i < preloop; i++)
		{
			x = P_Rand();
			y = P_Rand();
			height = P_Rand();

			precipsector = R_IsPointInSubsector(x, y);

			if (!precipsector)
				continue;

			if (!(precipsector->sector->ceilingpic == skyflatnum
				  && precipsector->sector->floorheight < precipsector->sector->ceilingheight))
				continue;

			if (height < precipsector->sector->floorheight ||
					height >= precipsector->sector->ceilingheight)
				continue;

			if (P_ObjectInWater(precipsector->sector, height))
				continue;

			if (!(i % 1000) || !i)
				P_LoadingScreen("Precipitation", ((i*100)/preloop));

			P_SpawnRainMobj(x, y, height, MT_RAIN);
		}
	}

	if (curWeather == PRECIP_BLANK)
	{
		curWeather = PRECIP_RAIN;
		P_SwitchWeather(PRECIP_BLANK);
	}
	else if (curWeather == PRECIP_STORM_NORAIN)
	{
		curWeather = PRECIP_RAIN;
		P_SwitchWeather(PRECIP_STORM_NORAIN);
	}
}

//
// P_RespawnSpecials
//
void P_RespawnSpecials(void)
{
	fixed_t x, y, z;
	mobj_t *mo = NULL;
	mapthing_t *mthing = NULL;
	size_t i;

	// SRB2CBTODO: Make hanggliders auto respawn when they are out of place for 5 seconds

	// Rain spawning
	if (curWeather == PRECIP_STORM || curWeather == PRECIP_RAIN || curWeather == PRECIP_STORM_NORAIN
		|| curWeather == PRECIP_STORM_NOSTRIKES)
	{
		boolean spawnlightning = false;
		int volume;

		// This code can be reached before the player has entered the game.
		// This gives rise to two problems: it calls P_Random, so it has to run;
		// but it also needs an mobj for the player. So, we split it into two,
		// and run just enough for consistency purposes if we're not in the game
		// yet.

		// Remember this for later.
		if (curWeather == PRECIP_STORM)
		{
			if (globalweather)
				spawnlightning = (P_Random() < 2);
			else
				spawnlightning = (M_Random() < 2);

			if (spawnlightning)
			{
				sector_t *ss = sectors;

				for (i = 0; i <= numsectors; i++, ss++)
					if (ss->ceilingpic == skyflatnum) // Only for the sky.
						P_SpawnLightningFlash(ss); // Spawn a quick flash thinker
			}
		}

		if (!dedicated && playeringame[displayplayer])
		{
			if (players[displayplayer].mo->subsector->sector->ceilingpic == skyflatnum)
				volume = 255;
			else if (nosound || sound_disabled)
				volume = 0;
			else
			{
				fixed_t yl, yh, xl, xh;
				fixed_t closex, closey, closedist, newdist;

				// Essentially check in a 1024 unit radius of the player for an outdoor area.
				yl = players[displayplayer].mo->y - 1024*FRACUNIT;
				yh = players[displayplayer].mo->y + 1024*FRACUNIT;
				xl = players[displayplayer].mo->x - 1024*FRACUNIT;
				xh = players[displayplayer].mo->x + 1024*FRACUNIT;
				closex = players[displayplayer].mo->x + 2048*FRACUNIT;
				closey = players[displayplayer].mo->y + 2048*FRACUNIT;
				closedist = 2048*FRACUNIT;
				for (y = yl; y <= yh; y += FRACUNIT*64)
					for (x = xl; x <= xh; x += FRACUNIT*64)
					{
						if (R_PointInSubsector(x, y)->sector->ceilingpic == skyflatnum) // Found the outdoors!
						{
							newdist = S_CalculateSoundDistance(players[displayplayer].mo->x, players[displayplayer].mo->y, 0, x, y, 0);
							if (newdist < closedist)
							{
								closex = x;
								closey = y;
								closedist = newdist;
							}
						}
					}
				volume = 255 - (closedist>>(FRACBITS+2));
			}
			if (volume < 0)
				volume = 0;
			else if (volume > 255)
				volume = 255;

			if (!(curWeather == PRECIP_STORM_NORAIN) && (!leveltime || leveltime % 80 == 1))
				S_StartSoundAtVolume(players[displayplayer].mo, sfx_rainin, volume);

			if (curWeather == PRECIP_STORM || curWeather == PRECIP_STORM_NORAIN || curWeather == PRECIP_STORM_NOSTRIKES)
			{
				if (spawnlightning && curWeather != PRECIP_STORM_NOSTRIKES)
				{
					i = M_Random(); // This doesn't need to use P_Random().

					if (i < 128 && leveltime & 1*NEWTICRATERATIO)
						S_StartSoundAtVolume(players[displayplayer].mo, sfx_litng1, volume);
					else if (i < 128)
						S_StartSoundAtVolume(players[displayplayer].mo, sfx_litng2, volume);
					else if (leveltime & 1*NEWTICRATERATIO)
						S_StartSoundAtVolume(players[displayplayer].mo, sfx_litng3, volume);
					else
						S_StartSoundAtVolume(players[displayplayer].mo, sfx_litng4, volume);
				}
				else if (leveltime & 1*NEWTICRATERATIO)
				{
					i = M_Random(); // This doesn't need to use P_Random().

					if (i > 253)
					{
						if (i & 1)
							S_StartSoundAtVolume(players[displayplayer].mo, sfx_athun1, volume);
						else
							S_StartSoundAtVolume(players[displayplayer].mo, sfx_athun2, volume);
					}
				}
			}
		}
	}

	// only respawn certain items when cv_itemrespawn is on
	if (!cv_itemrespawn.value)
		return;

	// Don't respawn rings in special stages!
	if (G_IsSpecialStage(gamemap))
		return;

	// nothing left to respawn?
	if (iquehead == iquetail)
		return;

	// the first item in the queue is the first to respawn
	// wait at least 30 seconds
	if (leveltime - itemrespawntime[iquetail] < (tic_t)cv_itemrespawntime.value*TICRATE)
		return;

	mthing = itemrespawnque[iquetail];

#ifdef PARANOIA
	if (!mthing)
		I_Error("itemrespawnque[iquetail] is NULL!");
#endif

	if (mthing)
	{
		x = mthing->x << FRACBITS;
		y = mthing->y << FRACBITS;

		// find which type to spawn
		for (i = 0; i < NUMMOBJTYPES; i++)
			if (mthing->type == mobjinfo[i].doomednum)
				break;

		z = (mthing->options >> ZSHIFT) * FRACUNIT;

		// CTF rings should continue to respawn as normal rings outside of CTF.
		if (gametype != GT_CTF)
		{
			if (i == MT_REDTEAMRING || i == MT_BLUETEAMRING)
				i = MT_RING;
		}

		mo = P_SpawnMobj(x, y, z, i);
		mo->spawnpoint = mthing;
		mo->angle = ANG45 * (mthing->angle/45);

		// Clunky. :(
		// One more reason why this junk needs to be cleaned up. -Jazz
		if (mthing->options & MTF_OBJECTFLIP)
		{
			if ((mthing->options & MTF_AMBUSH) &&
			(i == MT_RING || i == MT_REDTEAMRING || i == MT_BLUETEAMRING || i == MT_COIN ||
			i == MT_BOUNCERING || i == MT_RAILRING || i == MT_AUTOMATICRING ||
			i == MT_EXPLOSIONRING || i == MT_SCATTERRING || i == MT_GRENADERING ||
			i == MT_BOUNCEPICKUP || i == MT_RAILPICKUP || i == MT_AUTOPICKUP ||
			i == MT_EXPLODEPICKUP || i == MT_SCATTERPICKUP || i == MT_GRENADEPICKUP))
				mo->z = mo->ceilingz - (mthing->options >> ZSHIFT) * FRACUNIT - (32 * FRACUNIT);
			else
				mo->z = mo->ceilingz - (mthing->options >> ZSHIFT) * FRACUNIT;

			mo->z -= mobjinfo[i].height; // Don't forget the height!

			mo->eflags |= MFE_VERTICALFLIP;
			mo->flags2 = MF2_OBJECTFLIP;
		}
		else
		{
			if ((mthing->options & MTF_AMBUSH) &&
			(i == MT_RING || i == MT_REDTEAMRING || i == MT_BLUETEAMRING || i == MT_COIN ||
			i == MT_BOUNCERING || i == MT_RAILRING || i == MT_AUTOMATICRING ||
			i == MT_EXPLOSIONRING || i == MT_SCATTERRING || i == MT_GRENADERING ||
			i == MT_BOUNCEPICKUP || i == MT_RAILPICKUP || i == MT_AUTOPICKUP ||
			i == MT_EXPLODEPICKUP || i == MT_SCATTERPICKUP || i == MT_GRENADEPICKUP))
				mo->z = mo->floorz + (mthing->options >> ZSHIFT) * FRACUNIT + (32 * FRACUNIT);
			else
				mo->z = mo->floorz + (mthing->options >> ZSHIFT) * FRACUNIT;
		}
	}
	// pull it from the que
	iquetail = (iquetail+1)&(ITEMQUESIZE-1);
}

// SRB2CBTODO: May need to be updated for what LXShadow really wants
void P_SecretMove(mobj_t* mo)
{
    mo->x += mo->momx;
    mo->y += mo->momy;
    P_ZMovement(mo);
}

//
// P_SpawnPlayer
// Called when a player is spawned on the level.
// Most of the player structure stays unchanged between levels.
//
// spawn it at a playerspawn mapthing
void P_SpawnPlayer(mapthing_t *mthing, int playernum)
{
	player_t *p;
	fixed_t x, y, z;
	mobj_t *mobj;

	// not playing?
	if (!playeringame[playernum])
		return;

	I_Assert(playernum >= 0 && playernum < MAXPLAYERS);

	p = &players[playernum];

	if (p->playerstate == PST_REBORN)
		G_PlayerReborn(playernum);

	// spawn as spectator determination
	if (gametype == GT_RACE || gametype == GT_COOP)
		p->spectator = false;
	else if ((netgame && (p->jointime < 1 && (gametype == GT_TAG || gametype == GT_MATCH || gametype == GT_CTF))))
		p->spectator = true;

	if (mthing)
	{
		x = mthing->x << FRACBITS;
		y = mthing->y << FRACBITS;

		// Flagging a player's ambush will make them start on the ceiling
		if (mthing->options & MTF_AMBUSH)
			z = ONCEILINGZ;
		else if (mthing->options >> (ZSHIFT+1))
			z = R_PointInSubsector(x, y)->sector->floorheight + ((mthing->options >> (ZSHIFT+1)) << FRACBITS);
		else
			z = mthing->z << FRACBITS;

		mthing->z = (short)(z>>FRACBITS);
		mobj = P_SpawnMobj(x, y, z, MT_PLAYER);
		mthing->mobj = mobj;
	}
	else
	{
		x = y = 0; // Spawn at the origin as a desperation move if there is no mapthing
		z = R_PointInSubsector(x, y)->sector->floorheight;
		mobj = P_SpawnMobj(x, y, z, MT_PLAYER);
	}

	// set color translations for player sprites
	mobj->flags |= MF_TRANSLATION;
	mobj->color = p->skincolor;

	// set 'spritedef' override in mobj for player skins.. (see ProjectSprite)
	// (usefulness: when body mobj is detached from player (who respawns),
	// the dead body mobj retains the skin through the 'spritedef' override).
	if (atoi(skins[p->skin].highres))
		mobj->flags |= MF_HIRES;
	else
		mobj->flags &= ~MF_HIRES;
	mobj->skin = &skins[p->skin];

	if (mthing)
		mobj->angle = FixedAngle(mthing->angle*FRACUNIT);
	else
		mobj->angle = 0;

	if (playernum == consoleplayer)
		localangle = mobj->angle;
	else if (splitscreen && playernum == secondarydisplayplayer)
		localangle2 = mobj->angle;
	mobj->player = p;
	mobj->health = p->health;

	p->mo = mobj;
	p->playerstate = PST_LIVE;
	p->bonuscount = 0;
	p->viewheight = cv_viewheight.value<<FRACBITS;

	if (p->mo->eflags & MFE_VERTICALFLIP)
		p->viewz = p->mo->z + p->mo->height - p->viewheight;
	else
		p->viewz = p->mo->z + p->viewheight;

	p->bonustime = 0;
	p->realtime = leveltime;

	//awayview stuff
	p->awayviewmobj = NULL;
	p->awayviewtics = 0;

	if (playernum == consoleplayer)
	{
		// wake up the status bar
		ST_Start();
		// wake up the heads up text
		HU_Start();
	}

#ifdef JTEBOTS
	if (p->bot)
		JB_SpawnBot(playernum);
#endif

	SV_SpawnPlayer(playernum, mobj->x, mobj->y, mobj->angle);

	if (cv_chasecam.value)
	{
		if (displayplayer == playernum)
			P_ResetCamera(p, &camera);
	}
	if (cv_chasecam2.value && splitscreen)
	{
		if (secondarydisplayplayer == playernum)
			P_ResetCamera(p, &camera2);
	}

	// This is needed, otherwise, the player can have stats issues when switching levels
	P_SetScale(p->mo, 100);

}

void P_SpawnStarpostPlayer(mobj_t *mobj, int playernum)
{
	player_t *p;
	fixed_t x, y, z;
	angle_t angle;
	int starposttime;

	// Not playing?
	if (!playeringame[playernum])
		return;

	I_Assert(playernum >= 0 && playernum < MAXPLAYERS);

	p = &players[playernum];

	x = p->starpostx << FRACBITS;
	y = p->starposty << FRACBITS;
	z = p->starpostz << FRACBITS;
	angle = p->starpostangle;
	starposttime = p->starposttime;

	if (p->playerstate == PST_REBORN)
		G_PlayerReborn(playernum);

	mobj = P_SpawnMobj(x, y, z, MT_PLAYER);

	// Set color translations for player sprites
	mobj->flags |= MF_TRANSLATION;
	mobj->color = p->skincolor;

	// Set 'spritedef' override in mobjy for player skins.. (see ProjectSprite)
	// (usefulness : when body mobjy is detached from player (who respawns),
	// the dead body mobj retains the skin through the 'spritedef' override).
	if (atoi(skins[p->skin].highres))
		mobj->flags |= MF_HIRES;
	else
		mobj->flags &= ~MF_HIRES;
	mobj->skin = &skins[p->skin];

	mobj->angle = angle;
	if (playernum == consoleplayer)
		localangle = mobj->angle;
	else if (splitscreen && playernum == secondarydisplayplayer)
		localangle2 = mobj->angle;

	mobj->player = p;
	mobj->health = p->health;

	p->mo = mobj;
	p->playerstate = PST_LIVE;
	p->bonuscount = 0;
	p->viewheight = cv_viewheight.value<<FRACBITS;

	if (p->mo->eflags & MFE_VERTICALFLIP)
		p->viewz = p->mo->z + p->mo->height - p->viewheight;
	else
		p->viewz = p->mo->z + p->viewheight;

	if (playernum == consoleplayer)
	{
		// wake up the status bar
		ST_Start();
		// wake up the heads up text
		HU_Start();
	}

#ifdef JTEBOTS
	if (p->bot)
		JB_SpawnBot(playernum);
#endif

	SV_SpawnPlayer(playernum, mobj->x, mobj->y, mobj->angle);

	if (cv_chasecam.value)
	{
		if (displayplayer == playernum)
			P_ResetCamera(p, &camera);
	}
	if (cv_chasecam2.value && splitscreen)
	{
		if (secondarydisplayplayer == playernum)
			P_ResetCamera(p, &camera2);
	}

	if (!(netgame || multiplayer))
		leveltime = starposttime;

	P_SetScale(mobj, 100);
}

mapthing_t *huntemeralds[MAXHUNTEMERALDS];
byte numhuntemeralds = 0;

//
// P_SpawnMapThing
// The fields of the mapthing should
// already be in host byte order.
//
void P_SpawnMapThing(mapthing_t *mthing) // SRB2CBTODO: Needs TOTAL cleanup!
{
	mobjtype_t i;
	mobj_t *mobj;
	fixed_t x, y, z;
	sector_t *sector;
	fixed_t mfloorz;
	fixed_t mceilingz;

	x = mthing->x << FRACBITS;
	y = mthing->y << FRACBITS;

	sector = R_PointInSubsector(x, y)->sector;

	mfloorz = sector->floorheight;
#ifdef ESLOPE
	if (sector->f_slope)
		mfloorz = P_GetZAt(sector->f_slope, x, y);
#endif

	mceilingz = sector->ceilingheight;
#ifdef ESLOPE
	if (sector->c_slope)
		mceilingz = P_GetZAt(sector->c_slope, x, y);
#endif

	if (!mthing->type)
		return; // Ignore type-0 things as NOPs

	// clear ctf pointers
	redflag = blueflag = NULL;

	// count deathmatch start positions
	if (mthing->type == 33) // SRB2CBTODO: Use real type names!
	{
		if (numdmstarts < MAX_DM_STARTS)
		{
			deathmatchstarts[numdmstarts] = mthing;
			mthing->type = 0;
			numdmstarts++;
		}
		return;
	}

	else if (mthing->type == 36) // "IT" Start Locations
	{
		if (numtagstarts < MAXPLAYERS)
		{
			tagstarts[numtagstarts] = mthing;
			mthing->type = 0;
			numtagstarts++;
		}
		return;
	}

	else if (mthing->type == 34) // Red CTF Starts
	{
		if (numredctfstarts < MAXPLAYERS)
		{
			redctfstarts[numredctfstarts] = mthing;
			mthing->type = 0;
			numredctfstarts++;
		}
		return;
	}

	else if (mthing->type == 35) // Blue CTF Starts
	{
		if (numbluectfstarts < MAXPLAYERS)
		{
			bluectfstarts[numbluectfstarts] = mthing;
			mthing->type = 0;
			numbluectfstarts++;
		}
		return;
	}

	// SRB2CBTODO: mobj[type].doomednum here too.,
	// actually, is there some list to actually visibly define this?!!
	// SRB2CBTODO: Yes! mthing->type == a mobj's doomednum,
	// make a loop just to replace the number with mobjinfo[MT_MOBJ].doomednum
	else if (mthing->type == 600
		|| mthing->type == 601 || mthing->type == 602
		|| mthing->type == 603 || mthing->type == 604
		|| mthing->type == 300 || mthing->type == 605
		|| mthing->type == 308 || mthing->type == 309
		|| mthing->type == 606 || mthing->type == 607
		|| mthing->type == 608 || mthing->type == 609
		|| mthing->type == 1705 || mthing->type == 1706 // SRB2CBTODO: MT_HOOP and MT_HOOPCOLLIDE?
		|| mthing->type == 1800)
	{
		// Don't spawn hoops, wings, or rings yet!
		return;
	}

	// SRB2CBTODO: make some special list so looking
	// at the game's map editor config file isn't needed?
	// check for players specially
	if (mthing->type > 0 && mthing->type <= 32)
	{
		// save spots for respawning in network games
		playerstarts[mthing->type-1] = mthing;
		return;
	}

	// find which type to spawn
	for (i = 0; i < NUMMOBJTYPES; i++)
		if (mthing->type == mobjinfo[i].doomednum)
			break;

	if (i == NUMMOBJTYPES)
	{
		if (mthing->type != 3328) // 3D Thing Mode start // SRB2CBTODO: Check more stuff like this
			if (cv_devmode)
				CONS_Printf("\2P_SpawnMapThing: Unknown type %d at (%d, %d)\n", mthing->type, mthing->x, mthing->y);

		return;
	}

	// Hunt should only work in Cooperative // SRB2CBTODO: No, it could be for another mode too!
	if (gametype != GT_COOP)
	{
		switch (i)
		{
			case MT_EMERHUNT:
				return;
			default:
				break;
		}
	}

	if (i == MT_EMERHUNT)
	{
		mthing->z = (short)((mfloorz>>FRACBITS) + (mthing->options >> ZSHIFT));

		// Add to the list of emeralds to be hunted,
		// It MUST start at 1 - (numhuntemeralds starts at 0, so just add everything up by 1)
		// otherwise, a NULL crash occurs,
		// because there is a for loop where x < numhuntemeralds, this would cause one to be skipped
		// for the first increment in the loop
		if (numhuntemeralds < MAXHUNTEMERALDS)
		{
			huntemeralds[numhuntemeralds+1] = mthing;
			numhuntemeralds++;
		}

		return;
	}

	if (i >= MT_EMERALD1 && i <= MT_EMERALD7) // Pickupable Emeralds
	{
		if (emeralds & mobjinfo[i].speed) // You already have this emerald!
			return;
	}

	if ((!(gametype == GT_MATCH || gametype == GT_CTF	|| gametype == GT_TAG)
		&& (!cv_ringslinger.value)) || (!cv_specialrings.value))
	{
		switch (i)
		{
			case MT_BOUNCERING:
			case MT_RAILRING:
			case MT_AUTOMATICRING:
			case MT_EXPLOSIONRING:
			case MT_SCATTERRING:
			case MT_GRENADERING:
			case MT_BOUNCEPICKUP:
			case MT_RAILPICKUP:
			case MT_AUTOPICKUP:
			case MT_EXPLODEPICKUP:
			case MT_SCATTERPICKUP:
			case MT_GRENADEPICKUP:
				return;
			default:
				break;
		}
	}

	if (i == MT_EMERALDSPAWN)
	{
		if (!cv_powerstones.value)
			return;

		if (!(gametype == GT_MATCH || gametype == GT_CTF))
			return;

		runemeraldmanager = true;
	}

	// No outright emerald placement in match, CTF or tag.
	if ((mthing->type >= mobjinfo[MT_EMERALD1].doomednum &&
	     mthing->type <= mobjinfo[MT_EMERALD7].doomednum) &&
	    (gametype == GT_MATCH || gametype == GT_CTF || gametype == GT_TAG))
		return;

	if (gametype == GT_MATCH || gametype == GT_TAG || gametype == GT_CTF) // No enemies in match or CTF modes
		if ((mobjinfo[i].flags & MF_ENEMY) || (mobjinfo[i].flags & MF_BOSS) || i == MT_EGGGUARD)
			return;

	// Set powerup boxes to user settings for race.
	if (gametype == GT_RACE)
	{
		if (cv_raceitemboxes.value) // not Normal
		{
			if (mobjinfo[i].flags & MF_MONITOR)
			{
				if (cv_raceitemboxes.value == 1) // Random
					i = MT_QUESTIONBOX;
				else if (cv_raceitemboxes.value == 2) // Teleports
					i = MT_MIXUPBOX;
				else if (cv_raceitemboxes.value == 3) // None
					return; // Don't spawn!
			}
		}
	}

	// Set powerup boxes to user settings for other netplay modes
	else if (gametype == GT_MATCH || gametype == GT_TAG || gametype == GT_CTF
#ifdef CHAOSISNOTDEADYET
		|| gametype == GT_CHAOS
#endif
		)
	{
		if (cv_matchboxes.value) // not Normal
		{
			if (cv_matchboxes.value == 1) // Random
			{
				if (mobjinfo[i].flags & MF_MONITOR)
					i = MT_QUESTIONBOX;
			}
			else if (cv_matchboxes.value == 3) // Don't spawn
			{
				if (mobjinfo[i].flags & MF_MONITOR)
					return;
			}
			else // cv_matchboxes.value == 2, Non-Random
			{
				if (i == MT_QUESTIONBOX) return; // don't spawn in Non-Random

				if (mobjinfo[i].flags & MF_MONITOR)
					mthing->options &= ~(MTF_AMBUSH + MTF_OBJECTSPECIAL); // no random respawning!
			}
		}
	}

	if (i == MT_SIGN && gametype != GT_COOP && gametype != GT_RACE)
		return; // Don't spawn the level exit sign when it isn't needed.

#ifdef BLUE_SPHERES
	// Spawn rings as blue spheres in special stages.
	if (G_IsSpecialStage(gamemap))
		if (i == MT_RING)
			i = MT_BLUEBALL;
#endif

	if ((i == MT_BLUETEAMRING || i == MT_REDTEAMRING) && gametype != GT_CTF)
		i = MT_RING; //spawn team rings as regular rings in non-CTF modes

	if ((i == MT_BLUERINGBOX || i == MT_REDRINGBOX) && gametype != GT_CTF)
		i = MT_SUPERRINGBOX; //spawn team boxes as regular boxes in non-CTF modes

	if ((i == MT_SUPERRINGBOX || i == MT_GREENTV
		|| i == MT_YELLOWTV || i == MT_BLUETV || i == MT_BLACKTV || i == MT_WHITETV
#ifdef SRB2K
		 || i == MT_REDFIRETV || i == MT_BOUNCETV || i == MT_ELECTRICTV
#endif
		 )
		&& ultimatemode && !G_IsSpecialStage(gamemap))
	{
		// Don't have rings/shields in Ultimate mode
		return;
	}

	if ((i == MT_BLUEFLAG || i == MT_REDFLAG) && gametype != GT_CTF)
		return; // Don't spawn flags if you aren't in CTF Mode!

	if (i == MT_EMMY && (tokenbits == 30 || tokenlist & (1<<tokenbits) || gametype != GT_COOP || ultimatemode))
		return; // you already got this token, or there are too many, or the gametype's not right

	// spawn it
	if (i == MT_NIGHTSBUMPER)
		z = mfloorz + ((mthing->options >> ZSHIFT) << FRACBITS);
	else if (i != MT_AXIS && i != MT_AXISTRANSFER && i != MT_AXISTRANSFERLINE)
	{
		if (i == MT_SPECIALSPIKEBALL
			|| i == MT_BOUNCERING || i == MT_RAILRING
			|| i == MT_AUTOMATICRING
			|| i == MT_EXPLOSIONRING || i == MT_SCATTERRING
			|| i == MT_GRENADERING || i == MT_BOUNCEPICKUP
			|| i == MT_RAILPICKUP || i == MT_AUTOPICKUP
			|| i == MT_EXPLODEPICKUP || i == MT_SCATTERPICKUP
			|| i == MT_GRENADEPICKUP || i == MT_EMERALDSPAWN
			|| i == MT_EMMY)
		{
			if (!(mthing->options & MTF_OBJECTFLIP))
			{
				z = mfloorz;

				if (mthing->options & MTF_AMBUSH) // Special flag for rings
					z += 32*FRACUNIT;
				if (mthing->options >> ZSHIFT)
					z += (mthing->options >> ZSHIFT)*FRACUNIT;
			}
			else
			{
				z = mceilingz;

				if (mthing->options & MTF_AMBUSH) // Special flag for rings
					z -= 32*FRACUNIT;
				if (mthing->options >> ZSHIFT)
					z -= (mthing->options >> ZSHIFT)*FRACUNIT;

				z -= mobjinfo[i].height; // Don't forget the height!
			}
		}
		else if (mobjinfo[i].flags & MF_SPAWNCEILING || mthing->options & MTF_OBJECTFLIP)
		{
			if (mthing->options >> ZSHIFT)
			{
				z = mceilingz - ((mthing->options >> ZSHIFT) << FRACBITS) - mobjinfo[i].height; // Subtract the height too!
			}
			else
				z = ONCEILINGZ;
		}
		else if (mthing->options >> ZSHIFT)
		{
			z = mfloorz + ((mthing->options >> ZSHIFT) << FRACBITS);
		}
		// SRB2CBTODO: P_SpawnMapThing is way too hacky! Make it streamlined for thing on the floor somehow!
		else if (i == MT_CRAWLACOMMANDER || i == MT_DETON || i == MT_JETTBOMBER || i == MT_JETTGUNNER || i == MT_EGGMOBILE || i == MT_EGGMOBILE2)
		{
			z = mfloorz + 33*FRACUNIT;
		}
		else if (i == MT_GOLDBUZZ || i == MT_REDBUZZ)
		{
			z = mfloorz + 288*FRACUNIT;
		}
		// Skateboards cannot be directly on the floor,
        // or else they won't attach to the player very well
		else if (i == MT_SKATEBOARD)
		{
            z = mfloorz + 32*FRACUNIT;
		}
        else
			z = ONFLOORZ;

		if (z == ONFLOORZ)
			mthing->z = 0; // SRB2CBTODO: Is this right?
		else
			mthing->z = (short)(z>>FRACBITS);
	}
	else
		z = ONFLOORZ;

	mobj = P_SpawnMobj(x, y, z, i);
	mobj->spawnpoint = mthing;

	if (mobj->type == MT_FAN)
	{
		if (mthing->angle)
			mobj->health = mthing->angle;
		else
		{
			mobj->health = sector->ceilingheight - (sector->ceilingheight - sector->floorheight)/4;
			mobj->health -= sector->floorheight;
			mobj->health >>= FRACBITS;
		}
	}
	else if (mobj->type == MT_WATERDRIP)
	{
		if (mthing->angle)
			mobj->tics = 3*TICRATE + mthing->angle;
		else
			mobj->tics = 3*TICRATE;
	}
	else if (mobj->type == MT_FLAMEJET || mobj->type == MT_VERTICALFLAMEJET)
	{
		mobj->threshold = (mthing->angle >> 10) & 7;
		mobj->movecount = (mthing->angle >> 13);

		mobj->threshold *= (TICRATE/2);
		mobj->movecount *= (TICRATE/2);

		mobj->movedir = mthing->extrainfo;
	}
	else if (mobj->type == MT_POINTY)
	{
		int q;
		mobj_t *ball, *lastball = mobj;

		for (q = 0; q < mobj->info->painchance; q++)
		{
			ball = P_SpawnMobj(x, y, z, mobj->info->mass);

			P_SetTarget(&lastball->tracer, ball);

			P_SetTarget(&ball->target, mobj);

			lastball = ball;
		}
	}
	else if (mobj->type == MT_MACEPOINT
		|| mobj->type == MT_SWINGMACEPOINT
		|| mobj->type == MT_HANGMACEPOINT
		|| mobj->type == MT_SPINMACEPOINT)
	{
		fixed_t mlength, mspeed, mxspeed, mzspeed, mstartangle, mmaxspeed;
		mobjtype_t chainlink = MT_SMALLMACECHAIN;
		mobjtype_t macetype = MT_SMALLMACE;
		boolean firsttime;
		mobj_t *spawnee;
		size_t line;

		// Why does P_FindSpecialLineFromTag not work here?!?
		for (line = 0; line < numlines; line++)
		{
			if ((lines[line].special == 9) && lines[line].tag == mthing->angle)
				break;
		}

		if (line == numlines)
		{
			CONS_Printf("Mace chain (mapthing #%d) needs tagged to a #9 parameter line (trying to find tag %d).\n", mthing-mapthings, mthing->angle);
			return;
		}
/*
No deaf - small mace
Deaf - big mace

ML_NOCLIMB : Direction not controllable
*/
		mlength = abs(lines[line].dx >> FRACBITS);
		mspeed = abs(lines[line].dy >> FRACBITS);
		mxspeed = sides[lines[line].sidenum[0]].textureoffset >> FRACBITS;
		mzspeed = sides[lines[line].sidenum[0]].rowoffset >> FRACBITS;
		mstartangle = lines[line].frontsector->floorheight >> FRACBITS;
		mmaxspeed = lines[line].frontsector->ceilingheight >> FRACBITS;

		mstartangle %= 360;
		mxspeed %= 360;
		mzspeed %= 360;

		if (cv_devmode == 2)
		{
			CONS_Printf("Mace Chain (mapthing #%d):\n", mthing-mapthings);
			CONS_Printf("Length is %d\n", mlength);
			CONS_Printf("Speed is %d\n", mspeed);
			CONS_Printf("Xspeed is %d\n", mxspeed);
			CONS_Printf("Zspeed is %d\n", mzspeed);
			CONS_Printf("startangle is %d\n", mstartangle);
			CONS_Printf("maxspeed is %d\n", mmaxspeed);
		}

		mobj->lastlook = mspeed << 4;
		mobj->movecount = mobj->lastlook;
		mobj->health = (FixedAngle(mzspeed*FRACUNIT)>>ANGLETOFINESHIFT) + (FixedAngle(mstartangle*FRACUNIT)>>ANGLETOFINESHIFT);
		mobj->threshold = (FixedAngle(mxspeed*FRACUNIT)>>ANGLETOFINESHIFT) + (FixedAngle(mstartangle*FRACUNIT)>>ANGLETOFINESHIFT);
		mobj->movefactor = mobj->threshold;
		mobj->friction = mmaxspeed;

		if (lines[line].flags & ML_NOCLIMB)
			mobj->flags |= MF_SLIDEME;

		mobj->reactiontime = 0;

		if (mthing->options & MTF_AMBUSH)
		{
			chainlink = MT_BIGMACECHAIN;
			macetype = MT_BIGMACE;
		}

		if (mobj->type == MT_HANGMACEPOINT || mobj->type == MT_SPINMACEPOINT)
			firsttime = true;
		else
		{
			firsttime = false;

			spawnee = P_SpawnMobj(mobj->x, mobj->y, mobj->z, macetype);
			P_SetTarget(&spawnee->target, mobj);

			if (mobj->type == MT_SWINGMACEPOINT)
				spawnee->movecount = FixedAngle(mstartangle*FRACUNIT)>>ANGLETOFINESHIFT;
			else
				spawnee->movecount = 0;

			spawnee->threshold = FixedAngle(mstartangle*FRACUNIT)>>ANGLETOFINESHIFT;
			spawnee->reactiontime = mlength+1;
		}

		while (mlength > 0)
		{
			spawnee = P_SpawnMobj(mobj->x, mobj->y, mobj->z, chainlink);

			P_SetTarget(&spawnee->target, mobj);

			if (mobj->type == MT_HANGMACEPOINT || mobj->type == MT_SWINGMACEPOINT)
				spawnee->movecount = FixedAngle(mstartangle*FRACUNIT)>>ANGLETOFINESHIFT;
			else
				spawnee->movecount = 0;

			spawnee->threshold = FixedAngle(mstartangle*FRACUNIT)>>ANGLETOFINESHIFT;
			spawnee->reactiontime = mlength;

			if (firsttime)
			{
				// This is the outermost link in the chain
				spawnee->flags |= MF_AMBUSH;
				firsttime = false;
			}

			mlength--;
		}
	}
	else if (mobj->type == MT_ROCKSPAWNER)
	{
		mobj->threshold = mthing->angle;
		mobj->movecount = mthing->extrainfo;
	}
	else if (mobj->type == MT_POPUPTURRET)
	{
		if (mthing->angle)
			mobj->threshold = mthing->angle*NEWTICRATERATIO;
		else
			mobj->threshold = (TICRATE*2)-1;
	}
	else if (mobj->type == MT_NIGHTSBUMPER)
	{
		// Lower 4 bits specify the angle of
		// the bumper in 30 degree increments.
		mobj->threshold = (mthing->options & 15) % 12; // It loops over, etc
		P_SetMobjState(mobj, mobj->info->spawnstate+mobj->threshold);

		// you can shut up now, OBJECTFLIP.  And all of the other options, for that matter.
		mthing->options &= ~0xF;
	}

	if (mobj->flags & MF_BOSS)
	{
		if (mthing->options & MTF_OBJECTSPECIAL) // No egg trap for this boss
			mobj->flags2 |= MF2_BOSSNOTRAP;

		z = R_PointInSubsector(x, y)->sector->floorheight + ((mthing->options >> (ZSHIFT)) << FRACBITS);

		mthing->z = (short)(z>>FRACBITS);
	}
	else if (mobj->type == MT_EGGCAPSULE)
	{
		mobj->health = mthing->angle & 1023;
		mobj->threshold = mthing->angle >> 10;
	}
	else if (mobj->type == MT_TUBEWAYPOINT)
	{
		mobj->health = mthing->angle & 255;
		mobj->threshold = mthing->angle >> 8;
	}
	else if (mobj->type == MT_NIGHTSDRONE)
	{
		if (mthing->angle > 0)
			mobj->health = mthing->angle;
	}

	// Special condition for the 2nd boss.
	if (mobj->type == MT_EGGMOBILE2)
		mobj->watertop = mobj->info->speed;
	else if (mobj->type == MT_CHAOSSPAWNER)
	{
#ifndef CHAOSISNOTDEADYET
		return;
#else
		if (gametype != GT_CHAOS)
			return;
		mobj->fuse = P_Random()*2;
#endif
	}

	if (i == MT_AXIS || i == MT_AXISTRANSFER || i == MT_AXISTRANSFERLINE) // Axis Points
	{
		// Mare it belongs to
		if (mthing->options >> 10)
			mobj->threshold = mthing->options >> 10;

		// # in the mare
		mobj->health = mthing->options & 1023;

		mobj->flags2 |= MF2_AXIS;

		if (i == MT_AXIS)
		{
			// Inverted if uppermost bit is set
			if (mthing->angle & 16384)
				mobj->flags |= MF_AMBUSH;

			if (mthing->angle > 0)
				mobj->radius = (mthing->angle & 16383)*FRACUNIT;
		}
	}
	else if (i == MT_EMMY)
	{
		if (timeattacking)
		{
			P_SetMobjState (mobj, S_DISS);
			return;
		}

		mobj->health = 1 << tokenbits++;
		P_SpawnMobj(x, y, z, MT_TOKEN);
	}
	else if (i == MT_EGGMOBILE && mthing->options & MTF_AMBUSH)
	{
		mobj_t *spikemobj;
		spikemobj = P_SpawnMobj(x, y, z, MT_SPIKEBALL);
		P_SetTarget(&spikemobj->target, mobj);
		spikemobj->angle = 0;
		spikemobj = P_SpawnMobj(x, y, z, MT_SPIKEBALL);
		P_SetTarget(&spikemobj->target, mobj);
		spikemobj->angle = ANG90;
		spikemobj = P_SpawnMobj(x, y, z, MT_SPIKEBALL);
		P_SetTarget(&spikemobj->target, mobj);
		spikemobj->angle = ANG180;
		spikemobj = P_SpawnMobj(x, y, z, MT_SPIKEBALL);
		P_SetTarget(&spikemobj->target, mobj);
		spikemobj->angle = ANG270;
	}
	else if (i == MT_STARPOST)
	{
		thinker_t *th;
		mobj_t *mo2;
		boolean foundanother = false;
		mobj->health = (mthing->angle / 360) + 1;

		if (timeattacking)
		{
			P_SetMobjState (mobj, S_DISS);
			return;
		}

		// See if other starposts exist in this level that have the same value.
		for (th = thinkercap.next; th != &thinkercap; th = th->next)
		{
			if (th->function.acp1 != (actionf_p1)P_MobjThinker)
				continue;

			mo2 = (mobj_t *)th;

			if (mo2 == mobj)
				continue;

			if (mo2->type == MT_STARPOST && mo2->health == mobj->health)
			{
				foundanother = true;
				break;
			}
		}

		if (!foundanother)
			numstarposts++;
	}

	// count 10 ring boxes into the number of rings equation too
	if (i == MT_SUPERRINGBOX)
		nummaprings += 10;

	if (i == MT_BIGTUMBLEWEED || i == MT_LITTLETUMBLEWEED)
	{
		if (mthing->options & MTF_AMBUSH)
		{
			mobj->momz += 16*FRACUNIT;

			if (P_Random() % 2)
				mobj->momx += 16*FRACUNIT;
			else
				mobj->momx -= 16*FRACUNIT;

			if (P_Random() % 2)
				mobj->momy += 16*FRACUNIT;
			else
				mobj->momy -= 16*FRACUNIT;
		}
	}

	// CTF flag pointers
	if (i == MT_REDFLAG)
	{
		if (redflag)
			I_Error("Only one flag per team allowed in CTF!");
		else
		{
			redflag = mobj;
			rflagpoint = mobj->spawnpoint;
		}
	}
	if (i == MT_BLUEFLAG)
	{
		if (blueflag)
			I_Error("Only one flag per team allowed in CTF!");
		else
		{
			blueflag = mobj;
			bflagpoint = mobj->spawnpoint;
		}
	}

	// special push/pull stuff
	if (i == MT_PUSH || i == MT_PULL)
	{
		mobj->health = 0; // Default behaviour: pushing uses XY, fading uses XYZ

		if (mthing->options & MTF_AMBUSH)
			mobj->health |= 1; // If ambush is set, push using XYZ
		if (mthing->options & MTF_OBJECTSPECIAL)
			mobj->health |= 2; // If object special is set, fade using XY

		if (G_IsSpecialStage(gamemap))
		{
			if (i == MT_PUSH)
				P_SetMobjState(mobj, S_GRAVWELLGREEN);
			if (i == MT_PULL)
				P_SetMobjState(mobj, S_GRAVWELLRED);
		}
	}

	mobj->angle = FixedAngle(mthing->angle*FRACUNIT);

	if ((mthing->options & MTF_AMBUSH)
		&& (mthing->options & MTF_OBJECTSPECIAL)
		&& (mobj->flags & MF_PUSHABLE))
	{
		mobj->flags2 |= MF2_CLASSICPUSH;
	}
	else
	{
		if (mthing->options & MTF_AMBUSH)
		{
			switch (i)
			{
				case MT_YELLOWDIAG:
				case MT_YELLOWDIAGDOWN:
				case MT_REDDIAG:
				case MT_REDDIAGDOWN:
					mobj->angle += ANG45/2;
					break;
				default:
					break;
			}

			if (mobj->flags & MF_PUSHABLE)
			{
				mobj->flags &= ~MF_PUSHABLE;
				mobj->flags2 |= MF2_STANDONME;
			}

			// SRB2CBTODO: replace the rest with mobjinfo[].doomednum
			if (mthing->type != 1700 && mthing->type != 1701 && mthing->type != 1702
				&& mthing->type != 1704 && mthing->type != 502 &&
				mthing->type != mobjinfo[MT_GRAVITYBOX].doomednum &&
				mthing->type != mobjinfo[MT_EGGMANBOX].doomednum)
				mobj->flags |= MF_AMBUSH;
		}

		// flag for strong/weak random boxes
		if (mthing->options & MTF_OBJECTSPECIAL)
		{
			if (mthing->type == mobjinfo[MT_QUESTIONBOX].doomednum || mthing->type == mobjinfo[MT_SUPERRINGBOX].doomednum ||
				mthing->type == mobjinfo[MT_SNEAKERTV].doomednum || mthing->type == mobjinfo[MT_INV].doomednum ||
				mthing->type == mobjinfo[MT_WHITETV].doomednum || mthing->type == mobjinfo[MT_GREENTV].doomednum ||
				mthing->type == mobjinfo[MT_YELLOWTV].doomednum || mthing->type == mobjinfo[MT_BLUETV].doomednum ||
				mthing->type == mobjinfo[MT_RECYCLETV].doomednum ||
				mthing->type == mobjinfo[MT_BLACKTV].doomednum || mthing->type == mobjinfo[MT_MIXUPBOX].doomednum ||
				mthing->type == mobjinfo[MT_PRUP].doomednum)
				mobj->flags2 |= MF2_STRONGBOX;
		}

		// Generic reverse gravity for individual objects flag.
		if (mthing->options & MTF_OBJECTFLIP)
		{
			mobj->eflags |= MFE_VERTICALFLIP;
			mobj->flags2 |= MF2_OBJECTFLIP;
		}

		// SRB2CBTODO: Give everything a proper MF_SPECIAL1 and 2 flag for map options check

		// Pushables bounce and slide coolly with object special flag set // SRB2CBTODO: What is this?
		if ((mthing->options & MTF_OBJECTSPECIAL) && (mobj->flags & MF_PUSHABLE))
		{
			mobj->flags2 |= MF2_SLIDEPUSH;
			mobj->flags |= MF_BOUNCE;
		}
	}

	mthing->mobj = mobj;
}

void P_SpawnHoopsAndRings(mapthing_t *mthing)
{
	mobj_t *mobj = NULL;
	int r, i;
	fixed_t x, y, z, finalx, finaly, finalz, mthingx, mthingy, mthingz;
	TVector v, *res;
	angle_t closestangle, fa;
	sector_t *sector;
	fixed_t mfloorz;
	fixed_t mceilingz;

	x = mthing->x << FRACBITS;
	y = mthing->y << FRACBITS;

	sector = R_PointInSubsector(x, y)->sector;

	mfloorz = sector->floorheight;
#ifdef ESLOPE
	if (sector->f_slope)
		mfloorz = P_GetZAt(sector->f_slope, x, x);
#endif

	mceilingz = sector->ceilingheight;
#ifdef ESLOPE
	if (sector->c_slope)
		mceilingz = P_GetZAt(sector->c_slope, x, y);
#endif

	if (mthing->type == 1705) // NiGHTS hoop! // SRB2CBTODO: MT_HOOP?
	{
		mobj_t *nextmobj = NULL;
		mobj_t *hoopcenter;
		short spewangle;

		mthingx = x;
		mthingy = y;

		mthingz = mthing->options << FRACBITS;

		hoopcenter = P_SpawnMobj(mthingx, mthingy, mthingz, MT_HOOPCENTER);

		hoopcenter->spawnpoint = mthing;

		hoopcenter->flags |= MF_NOTHINK;

		mthingz += mfloorz;

		hoopcenter->z = mthingz - hoopcenter->height/2;

		P_UnsetThingPosition(hoopcenter);
		hoopcenter->x = mthingx;
		hoopcenter->y = mthingy;
		P_SetThingPosition(hoopcenter);

		// Scale 0-255 to 0-359 =(
		closestangle = FixedAngle(FixedMul((mthing->angle>>8)*FRACUNIT,
			360*(FRACUNIT/256)));

		hoopcenter->movedir = FixedMul(FixedMul((mthing->angle&255)*FRACUNIT,
			360*(FRACUNIT/256)),1);
		hoopcenter->movecount = FixedMul(AngleFixed(closestangle),1);

		spewangle = (short)hoopcenter->movedir;

		// Create the hoop!
		for (i = 0; i < 32; i++)
		{
			fa = i*(FINEANGLES/32);
			v[0] = FixedMul(FINECOSINE(fa),96*FRACUNIT);
			v[1] = 0;
			v[2] = FixedMul(FINESINE(fa),96*FRACUNIT);
			v[3] = FRACUNIT;

			res = VectorMatrixMultiply(v, *RotateXMatrix(FixedAngle(spewangle*FRACUNIT)));
			M_Memcpy(&v, res, sizeof (v));
			res = VectorMatrixMultiply(v, *RotateZMatrix(closestangle));
			M_Memcpy(&v, res, sizeof (v));

			finalx = mthingx + v[0];
			finaly = mthingy + v[1];
			finalz = mthingz + v[2];

			mobj = P_SpawnMobj(finalx, finaly, finalz, MT_HOOP);
			mobj->z -= mobj->height/2;
			P_SetTarget(&mobj->target, hoopcenter); // Link the sprite to the center.
			mobj->fuse = 0;

			// Link all the sprites in the hoop together
			if (nextmobj)
			{
				mobj->hprev = nextmobj;
				mobj->hprev->hnext = mobj;
			}
			else
				mobj->hprev = mobj->hnext = NULL;

			nextmobj = mobj;
		}

		// Create the collision detectors!
		for (i = 0; i < 16; i++)
		{
			fa = i*FINEANGLES/16;
			v[0] = FixedMul(FINECOSINE(fa),32*FRACUNIT);
			v[1] = 0;
			v[2] = FixedMul(FINESINE(fa),32*FRACUNIT);
			v[3] = FRACUNIT;
			res = VectorMatrixMultiply(v, *RotateXMatrix(FixedAngle(spewangle*FRACUNIT)));
			M_Memcpy(&v, res, sizeof (v));
			res = VectorMatrixMultiply(v, *RotateZMatrix(closestangle));
			M_Memcpy(&v, res, sizeof (v));

			finalx = mthingx + v[0];
			finaly = mthingy + v[1];
			finalz = mthingz + v[2];

			mobj = P_SpawnMobj(finalx, finaly, finalz, MT_HOOPCOLLIDE);
			mobj->z -= mobj->height/2;

			// Link all the collision sprites together.
			mobj->hnext = NULL;
			mobj->hprev = nextmobj;
			mobj->hprev->hnext = mobj;

			nextmobj = mobj;
		}
		// Create the collision detectors!
		for (i = 0; i < 16; i++)
		{
			fa = i*FINEANGLES/16;
			v[0] = FixedMul(FINECOSINE(fa),64*FRACUNIT);
			v[1] = 0;
			v[2] = FixedMul(FINESINE(fa),64*FRACUNIT);
			v[3] = FRACUNIT;
			res = VectorMatrixMultiply(v, *RotateXMatrix(FixedAngle(spewangle*FRACUNIT)));
			M_Memcpy(&v, res, sizeof (v));
			res = VectorMatrixMultiply(v, *RotateZMatrix(closestangle));
			M_Memcpy(&v, res, sizeof (v));

			finalx = mthingx + v[0];
			finaly = mthingy + v[1];
			finalz = mthingz + v[2];

			mobj = P_SpawnMobj(finalx, finaly, finalz, MT_HOOPCOLLIDE);
			mobj->z -= mobj->height/2;

			// Link all the collision sprites together.
			mobj->hnext = NULL;
			mobj->hprev = nextmobj;
			mobj->hprev->hnext = mobj;

			nextmobj = mobj;
		}
		return;
	}
	else if (mthing->type == 1706) // Wing logo item.
	{
		if (mthing->options >> ZSHIFT)
			mthing->z = (short)((mfloorz + ((mthing->options >> ZSHIFT) << FRACBITS))>>FRACBITS);
		else
			mthing->z = (short)(mfloorz>>FRACBITS);

		mobj = P_SpawnMobj(x, y, mthing->z << FRACBITS, MT_NIGHTSWING);
		mobj->spawnpoint = mthing;

		if (mobj->tics > 0)
			mobj->tics = 1 + (P_Random() % mobj->tics);
		mobj->angle = FixedAngle(mthing->angle*FRACUNIT);
		mobj->flags |= MF_AMBUSH;
		mthing->mobj = mobj;
	}
	else if (mthing->type == 606) // A ring of wing items (NiGHTS stuff)
	{
		mthingx = x;
		mthingy = y;

		if (mthing->options >> ZSHIFT)
			mthingz = (mfloorz + ((mthing->options >> ZSHIFT) << FRACBITS));
		else
			mthingz = mfloorz;

		closestangle = FixedAngle(mthing->angle*FRACUNIT);

		// Create the hoop!
		for (i = 0; i < 8; i++)
		{
			fa = i*FINEANGLES/8;
			v[0] = FixedMul(FINECOSINE(fa),96*FRACUNIT);
			v[1] = 0;
			v[2] = FixedMul(FINESINE(fa),96*FRACUNIT);
			v[3] = FRACUNIT;

			res = VectorMatrixMultiply(v, *RotateZMatrix(closestangle));
			M_Memcpy(&v, res, sizeof (v));

			finalx = mthingx + v[0];
			finaly = mthingy + v[1];
			finalz = mthingz + v[2];

			mobj = P_SpawnMobj(finalx, finaly, finalz, MT_NIGHTSWING);
			mobj->z -= mobj->height/2;
		}
		return;
	}
	else if (mthing->type == 607) // A BIGGER ring of wing items (NiGHTS stuff)
	{
		mthingx = x;
		mthingy = y;

		if (mthing->options >> ZSHIFT)
			mthingz = (mfloorz + ((mthing->options >> ZSHIFT) << FRACBITS));
		else
			mthingz = mfloorz;

		closestangle = FixedAngle(mthing->angle*FRACUNIT);

		// Create the hoop!
		for (i = 0; i < 16; i++)
		{
			fa = (i*FINEANGLES/8) & FINEMASK;
			v[0] = FixedMul(FINECOSINE(fa),192*FRACUNIT);
			v[1] = 0;
			v[2] = FixedMul(FINESINE(fa),192*FRACUNIT);
			v[3] = FRACUNIT;

			res = VectorMatrixMultiply(v, *RotateZMatrix(closestangle));
			M_Memcpy(&v, res, sizeof (v));

			finalx = mthingx + v[0];
			finaly = mthingy + v[1];
			finalz = mthingz + v[2];

			mobj = P_SpawnMobj(finalx, finaly, finalz, MT_NIGHTSWING);
			mobj->z -= mobj->height/2;
		}
		return;
	}
	else
	{
		if (ultimatemode && !(G_IsSpecialStage(gamemap) || (maptol & TOL_NIGHTS))) // No rings in Ultimate!
			return;

		// Take care of rings and coins.
		if (mthing->type == mobjinfo[MT_RING].doomednum || mthing->type == mobjinfo[MT_COIN].doomednum ||
			mthing->type == mobjinfo[MT_REDTEAMRING].doomednum || mthing->type == mobjinfo[MT_BLUETEAMRING].doomednum) // Your basic ring.
		{
			if (mthing->options >> ZSHIFT)
			{
				if (!(mthing->options & MTF_OBJECTFLIP))
					mthing->z = (short)((mfloorz + ((mthing->options >> ZSHIFT) << FRACBITS))>>FRACBITS);
				else
					mthing->z = (short)((mceilingz - ((mthing->options >> ZSHIFT) << FRACBITS))>>FRACBITS);
			}
			else
			{
				if (!(mthing->options & MTF_OBJECTFLIP))
					mthing->z = (short)(mfloorz>>FRACBITS);
				else
					mthing->z = (short)(mceilingz>>FRACBITS);
			}

			if (mthing->options & MTF_AMBUSH) // Special flag for rings
			{
				if (!(mthing->options & MTF_OBJECTFLIP))
					mthing->z += 32;
				else
					mthing->z -= 56;
			}

			// Handle all of this in one block so we don't need individual blocks for every ring type.
			switch (mthing->type)
			{
				case 300: //MT_RING
#ifdef BLUE_SPHERES
					// Spawn rings as blue spheres in special stages, ala S3+K.
					if (G_IsSpecialStage(gamemap))
						mobj = P_SpawnMobj(x, y, mthing->z << FRACBITS, MT_BLUEBALL);
					else
#endif
						mobj = P_SpawnMobj(x, y, mthing->z << FRACBITS, MT_RING);
					break;
				case 1800: //MT_COIN
					mobj = P_SpawnMobj(x, y, mthing->z << FRACBITS, MT_COIN);
					break;
				case 308: //MT_REDTEAMRING
					if (gametype == GT_CTF) //No team-specific rings outside of CTF!
						mobj = P_SpawnMobj(x, y,mthing->z << FRACBITS, MT_REDTEAMRING);
					else
						mobj = P_SpawnMobj(x, y,mthing->z << FRACBITS, MT_RING);
					break;
				case 309: //MT_BLUETEAMRING
					if (gametype == GT_CTF) //No team-specific rings outside of CTF!
						mobj = P_SpawnMobj(x, y,mthing->z << FRACBITS, MT_BLUETEAMRING);
					else
						mobj = P_SpawnMobj(x, y,mthing->z << FRACBITS, MT_RING);
					break;
			}

			if (!mobj)
				return;

			mobj->spawnpoint = mthing;

			if (mthing->options & MTF_OBJECTFLIP)
			{
				mobj->eflags |= MFE_VERTICALFLIP;
				mobj->flags2 |= MF2_OBJECTFLIP;
			}

			if (mobj->tics > 0)
				mobj->tics = 1 + (P_Random() % mobj->tics);
			mobj->angle = FixedAngle(mthing->angle*FRACUNIT);
			mobj->flags |= MF_AMBUSH;
			mthing->mobj = mobj;
		}
		else if (mthing->type == 600) // Vertical Rings - Stack of 5 (suitable for Yellow Spring)
		{
			for (r = 1; r <= 5; r++)
			{
				if (mthing->options >> ZSHIFT)
					z = (mfloorz + ((mthing->options >> ZSHIFT) << FRACBITS)) + 64*FRACUNIT*r;
				else
					z = mfloorz + 64*FRACUNIT*r;
#ifdef BLUE_SPHERES
				// Spawn rings as blue spheres in special stages, ala S3+K.
				if (G_IsSpecialStage(gamemap))
					mobj = P_SpawnMobj(x, y, z, MT_BLUEBALL);
				else
#endif
					mobj = P_SpawnMobj(x, y, z, MT_RING);

				if (mobj->tics > 0)
					mobj->tics = 1 + (P_Random() % mobj->tics);

				mobj->angle = FixedAngle(mthing->angle*FRACUNIT);
				if (mthing->options & MTF_AMBUSH)
					mobj->flags |= MF_AMBUSH;
			}
		}
		else if (mthing->type == 601) // Vertical Rings - Stack of 5 (suitable for Red Spring)
		{
			for (r = 1; r <= 5; r++)
			{
				if (mthing->options >> ZSHIFT)
					z = (mfloorz + ((mthing->options >> ZSHIFT) << FRACBITS)) + 128*FRACUNIT*r;
				else
					z = mfloorz + 128*FRACUNIT*r;
#ifdef BLUE_SPHERES
				// Spawn rings as blue spheres in special stages, ala S3+K.
				if (G_IsSpecialStage(gamemap))
					mobj = P_SpawnMobj(x, y, z, MT_BLUEBALL);
				else
#endif
					mobj = P_SpawnMobj(x, y, z, MT_RING);

				if (mobj->tics > 0)
					mobj->tics = 1 + (P_Random() % mobj->tics);

				mobj->angle = FixedAngle(mthing->angle*FRACUNIT);
				if (mthing->options & MTF_AMBUSH)
					mobj->flags |= MF_AMBUSH;
			}
		}
		else if (mthing->type == 602) // Diagonal rings (5)
		{
			angle_t angle = ANG45 * (mthing->angle/45);
			angle >>= ANGLETOFINESHIFT;

			for (r = 1; r <= 5; r++)
			{
				x += FixedMul(64*FRACUNIT, FINECOSINE(angle));
				y += FixedMul(64*FRACUNIT, FINESINE(angle));
				if (mthing->options >> ZSHIFT)
					z = (mfloorz + ((mthing->options >> ZSHIFT) << FRACBITS)) + 64*FRACUNIT*r;
				else
					z = mfloorz + 64*FRACUNIT*r;
#ifdef BLUE_SPHERES
				// Spawn rings as blue spheres in special stages, ala S3+K.
				if (G_IsSpecialStage(gamemap))
					mobj = P_SpawnMobj(x, y, z, MT_BLUEBALL);
				else
#endif
					mobj = P_SpawnMobj(x, y, z, MT_RING);

				if (mobj->tics > 0)
					mobj->tics = 1 + (P_Random() % mobj->tics);

				mobj->angle = FixedAngle(mthing->angle*FRACUNIT);
				if (mthing->options & MTF_AMBUSH)
					mobj->flags |= MF_AMBUSH;
			}
		}
		else if (mthing->type == 603) // Diagonal rings (10)
		{
			angle_t angle = ANG45 * (mthing->angle/45);
			angle >>= ANGLETOFINESHIFT;

			for (r = 1; r <= 10; r++)
			{
				x += FixedMul(64*FRACUNIT, FINECOSINE(angle));
				y += FixedMul(64*FRACUNIT, FINESINE(angle));
				if (mthing->options >> ZSHIFT)
					z = (mfloorz + ((mthing->options >> ZSHIFT) << FRACBITS)) + 64*FRACUNIT*r;
				else
					z = mfloorz + 64*FRACUNIT*r;
#ifdef BLUE_SPHERES
				// Spawn rings as blue spheres in special stages, ala S3+K.
				if (G_IsSpecialStage(gamemap))
					mobj = P_SpawnMobj(x, y, z, MT_BLUEBALL);
				else
#endif
					mobj = P_SpawnMobj(x, y, z, MT_RING);

				if (mobj->tics > 0)
					mobj->tics = 1 + (P_Random() % mobj->tics);

				mobj->angle = FixedAngle(mthing->angle*FRACUNIT);
				if (mthing->options & MTF_AMBUSH)
					mobj->flags |= MF_AMBUSH;
			}
		}
		else if (mthing->type == 604) // A ring of rings (NiGHTS stuff)
		{
			if (mthing->options >> ZSHIFT)
				mthingz = (mfloorz + ((mthing->options >> ZSHIFT) << FRACBITS));
			else
				mthingz = mfloorz;

			closestangle = FixedAngle(mthing->angle*FRACUNIT);

			// Create the hoop!
			for (i = 0; i < 8; i++)
			{
				fa = i*FINEANGLES/8;
				v[0] = FixedMul(FINECOSINE(fa),96*FRACUNIT);
				v[1] = 0;
				v[2] = FixedMul(FINESINE(fa),96*FRACUNIT);
				v[3] = FRACUNIT;

				res = VectorMatrixMultiply(v, *RotateZMatrix(closestangle));
				M_Memcpy(&v, res, sizeof (v));

				finalx = x + v[0];
				finaly = y + v[1];
				finalz = mthingz + v[2];
#ifdef BLUE_SPHERES
				// Spawn rings as blue spheres in special stages, ala S3+K.
				if (G_IsSpecialStage(gamemap))
					mobj = P_SpawnMobj(finalx, finaly, finalz, MT_BLUEBALL);
				else
#endif
					mobj = P_SpawnMobj(finalx, finaly, finalz, MT_RING);

				mobj->z -= mobj->height/2;
			}

			return;
		}
		else if (mthing->type == 605) // A BIGGER ring of rings (NiGHTS stuff)
		{
			mthingx = x;
			mthingy = y;

			if (mthing->options >> ZSHIFT)
				mthingz = (mfloorz + ((mthing->options >> ZSHIFT) << FRACBITS));
			else
				mthingz = mfloorz;

			closestangle = FixedAngle(mthing->angle*FRACUNIT);

			// Create the hoop!
			for (i = 0; i < 16; i++)
			{
				fa = i*FINEANGLES/16;
				v[0] = FixedMul(FINECOSINE(fa),192*FRACUNIT);
				v[1] = 0;
				v[2] = FixedMul(FINESINE(fa),192*FRACUNIT);
				v[3] = FRACUNIT;

				res = VectorMatrixMultiply(v, *RotateZMatrix(closestangle));
				M_Memcpy(&v, res, sizeof (v));

				finalx = mthingx + v[0];
				finaly = mthingy + v[1];
				finalz = mthingz + v[2];
#ifdef BLUE_SPHERES
				// Spawn rings as blue spheres in special stages, ala S3+K.
				if (G_IsSpecialStage(gamemap))
					mobj = P_SpawnMobj(finalx, finaly, finalz, MT_BLUEBALL);
				else
#endif
					mobj = P_SpawnMobj(finalx, finaly, finalz, MT_RING);

				mobj->z -= mobj->height/2;
			}

			return;
		}
		else if (mthing->type == 608) // A ring of rings and wings (alternating) (NiGHTS stuff)
		{
			mthingx = x;
			mthingy = y;

			if (mthing->options >> ZSHIFT)
				mthingz = (mfloorz + ((mthing->options >> ZSHIFT) << FRACBITS));
			else
				mthingz = mfloorz;

			closestangle = FixedAngle(mthing->angle*FRACUNIT);

			// Create the hoop!
			for (i = 0; i < 8; i++)
			{
				fa = i*FINEANGLES/8;
				v[0] = FixedMul(FINECOSINE(fa),96*FRACUNIT);
				v[1] = 0;
				v[2] = FixedMul(FINESINE(fa),96*FRACUNIT);
				v[3] = FRACUNIT;

				res = VectorMatrixMultiply(v, *RotateZMatrix(closestangle));
				M_Memcpy(&v, res, sizeof (v));

				finalx = mthingx + v[0];
				finaly = mthingy + v[1];
				finalz = mthingz + v[2];

				if (i & 1)
				{
#ifdef BLUE_SPHERES
					// Spawn rings as blue spheres in special stages, ala S3+K.
					if (G_IsSpecialStage(gamemap))
						mobj = P_SpawnMobj(finalx, finaly, finalz, MT_BLUEBALL);
					else
#endif
						mobj = P_SpawnMobj(finalx, finaly, finalz, MT_RING);
				}
				else
					mobj = P_SpawnMobj(finalx, finaly, finalz, MT_NIGHTSWING);

				mobj->z -= mobj->height/2;
			}

			return;
		}
		else if (mthing->type == 609) // A BIGGER ring of rings and wings (alternating) (NiGHTS stuff)
		{
			mthingx = x;
			mthingy = y;

			if (mthing->options >> ZSHIFT)
				mthingz = (mfloorz + ((mthing->options >> ZSHIFT) << FRACBITS));
			else
				mthingz = mfloorz;

			closestangle = FixedAngle(mthing->angle*FRACUNIT);

			// Create the hoop!
			for (i = 0; i < 16; i++)
			{
				fa = i*FINEANGLES/16;
				v[0] = FixedMul(FINECOSINE(fa),192*FRACUNIT);
				v[1] = 0;
				v[2] = FixedMul(FINESINE(fa),192*FRACUNIT);
				v[3] = FRACUNIT;

				res = VectorMatrixMultiply(v, *RotateZMatrix(closestangle));
				M_Memcpy(&v, res, sizeof (v));

				finalx = mthingx + v[0];
				finaly = mthingy + v[1];
				finalz = mthingz + v[2];

				if (i & 1)
				{
#ifdef BLUE_SPHERES
					// Spawn rings as blue spheres in special stages, ala S3+K.
					if (G_IsSpecialStage(gamemap))
						mobj = P_SpawnMobj(finalx, finaly, finalz, MT_BLUEBALL);
					else
#endif
						mobj = P_SpawnMobj(finalx, finaly, finalz, MT_RING);
				}
				else
					mobj = P_SpawnMobj(finalx, finaly, finalz, MT_NIGHTSWING);
				mobj->z -= mobj->height/2;
			}

			return;
		}
	}
}

//
// GAME SPAWN FUNCTIONS
//

//
// P_CheckMissileSpawn
// Moves the missile forward a bit and possibly explodes it right there.
//
boolean P_CheckMissileSpawn(mobj_t *th)
{
	// move a little forward so an angle can be computed if it immediately explodes
	// don't do this for grenades, or they'll spawn on the other side of a linedef.
	if (!(th->flags2 & MF2_GRENADE))
	{
		th->x += th->momx>>1;
		th->y += th->momy>>1;
		th->z += th->momz>>1;
	}

	if (!P_TryMove(th, th->x, th->y, true))
	{
		P_ExplodeMissile(th);
		return false;
	}
	return true;
}

//
// P_SpawnXYZMissile
//
// Spawns missile at specific coords
//
mobj_t *P_SpawnXYZMissile(mobj_t *source, mobj_t *dest, mobjtype_t type,
	fixed_t x, fixed_t y, fixed_t z)
{
	mobj_t *th;
	angle_t an;
	int dist;
	int speed;

	I_Assert(source != NULL);
	I_Assert(dest != NULL);

	th = P_SpawnMobj(x, y, z, type);

	speed = th->info->speed;

	if (speed == 0) // Backwards compatibility with 1.09.2
	{
		CONS_Printf("P_SpawnXYZMissile - projectile has 0 speed! (mobj type %d)\nPlease update this SOC.", type);
		speed = mobjinfo[MT_ROCKET].speed;
	}

	if (th->info->seesound)
		S_StartSound(th, th->info->seesound);

	P_SetTarget(&th->target, source); // where it came from
	an = R_PointToAngle2(x, y, dest->x, dest->y);

	th->angle = an;
	an >>= ANGLETOFINESHIFT;
	th->momx = FixedMul(speed, FINECOSINE(an));
	th->momy = FixedMul(speed, FINESINE(an));

	dist = P_AproxDistance(dest->x - x, dest->y - y);
	dist = dist / speed;

	if (dist < 1)
		dist = 1;

	th->momz = (dest->z - z) / dist;

	if (th->flags & MF_MISSILE)
		dist = P_CheckMissileSpawn(th);
	else
		dist = 1;

	return dist ? th : NULL;
}

//
// P_SpawnMissile
//
mobj_t *P_SpawnMissile(mobj_t *source, mobj_t *dest, mobjtype_t type)
{
	mobj_t *th;
	angle_t an;
	int dist;
	fixed_t z;
	const fixed_t gsf = (fixed_t)6;
	fixed_t speed;

	I_Assert(source != NULL);
	I_Assert(dest != NULL);
	switch (type)
	{
		case MT_JETTBULLET:
			if (source->type == MT_JETTGUNNER)
				z = source->z - 12*FRACUNIT;
			else
				z = source->z + source->height/2;
			break;
		case MT_TURRETLASER:
			z = source->z + source->height/2;
		default:
			z = source->z + 32*FRACUNIT;
			break;
	}

	th = P_SpawnMobj(source->x, source->y, z, type);

	speed = th->info->speed;

	if (speed == 0) // Backwards compatibility with 1.09.2
	{
		CONS_Printf("P_SpawnMissile - projectile has 0 speed! (mobj type %d)\nPlease update this SOC.", type);
		speed = mobjinfo[MT_TURRETLASER].speed;
	}

	if (th->info->seesound)
		S_StartSound(source, th->info->seesound);

	P_SetTarget(&th->target, source); // where it came from

	if (type == MT_TURRETLASER) // More accurate!
		an = R_PointToAngle2(source->x, source->y,
			dest->x + (dest->momx*gsf),
			dest->y + (dest->momy*gsf));
	else
		an = R_PointToAngle2(source->x, source->y, dest->x, dest->y);

	th->angle = an;
	an >>= ANGLETOFINESHIFT;
	th->momx = FixedMul(speed, FINECOSINE(an));
	th->momy = FixedMul(speed, FINESINE(an));

	if (type == MT_TURRETLASER) // More accurate!
		dist = P_AproxDistance(dest->x+(dest->momx*gsf) - source->x, dest->y+(dest->momy*gsf) - source->y);
	else
		dist = P_AproxDistance(dest->x - source->x, dest->y - source->y);

	dist = dist / speed;

	if (dist < 1)
		dist = 1;

	if (type == MT_TURRETLASER) // More accurate!
		th->momz = (dest->z + (dest->momz*gsf) - z) / dist;
	else
		th->momz = (dest->z - z) / dist;

	dist = P_CheckMissileSpawn(th);
	return dist ? th : NULL;
}

//
// P_ColorTeamMissile
// Colors a player's ring based on their team
//
void P_ColorTeamMissile(mobj_t *missile, player_t *source)
{
	if (gametype == GT_CTF || (gametype == GT_MATCH && cv_matchtype.value))
	{
        missile->flags |= MF_TRANSLATION;

	    if (source->ctfteam == 1)
	        missile->color = 6; // This is for colouring Automatic rings which are usually green.
		else
			missile->color = 8; // Note: Could just do missile->color = source->mo->color, really.
	}
	else if (cv_ringcolor.value)
	{
		missile->flags |= MF_TRANSLATION;
		missile->color = source->mo->color; // copy color
	}
}

//
// P_SPMAngle
// Tries to aim at a nearby object
//
mobj_t *P_SPMAngle(mobj_t *source, mobjtype_t type, angle_t angle, boolean noaiming, boolean noautoaiming, int flags2, boolean reflected)
{
	mobj_t *th;
	angle_t an;
	fixed_t x, y, z, slope = 0;

	// angle at which you fire, is player angle
	an = angle;

	if (!noaiming)
	{
		if (!noautoaiming)
		{
			if
#ifdef JTEBOTS // Bots ALWAYS auto target (they need it :P)
				(source->player->bot ||
#endif
				((source->player->pflags & PF_AUTOAIM) && cv_allowautoaim.value
				&& !source->player->powers[pw_railring])
#ifdef JTEBOTS
				 )
#endif
			{
				// see which target is to be aimed at
				slope = P_AimLineAttack(source, an, 16*64*FRACUNIT);

				if (!linetarget)
				{
					an += 1<<26;
					slope = P_AimLineAttack(source, an, 16*64*FRACUNIT);

					if (!linetarget)
					{
						an -= 2<<26;
						slope = P_AimLineAttack(source, an, 16*64*FRACUNIT);
					}
					if (!linetarget)
					{
						an = angle;
						slope = 0;
					}
				}
			}
		}
		else
			slope = AIMINGTOSLOPE(source->player->aiming);

		// if not autoaim, or if the autoaim didn't aim something, use the mouseaiming
		if ((!((source->player->pflags & PF_AUTOAIM) && cv_allowautoaim.value)
			|| (!linetarget)) || source->player->powers[pw_railring])
		{
#ifdef JTEBOTS
			if (!source->player->bot)
#endif
			slope = AIMINGTOSLOPE(source->player->aiming);
		}
	}

	x = source->x;
	y = source->y;
	z = source->z + source->height/3;

	th = P_SpawnMobj(x, y, z, type);

	th->flags2 |= flags2;

	if (reflected)
		th->flags2 |= MF2_REFLECTED;

#ifdef WEAPON_SFX
	//Since rail and bounce have no thrown objects, this hack is necessary.
	//Is creating thrown objects for rail and bounce more or less desirable than this?
	if (th->info->seesound && !(th->flags2 & MF2_RAILRING) && !(th->flags2 & MF2_SCATTER))
		S_StartSound(source, th->info->seesound);
#else
	if (th->info->seesound)
		S_StartSound(source, th->info->seesound);
#endif

	P_SetTarget(&th->target, source);

	th->angle = an;
	th->momx = FixedMul(th->info->speed, FINECOSINE(an>>ANGLETOFINESHIFT));
	th->momy = FixedMul(th->info->speed, FINESINE(an>>ANGLETOFINESHIFT));

	if (!noaiming)
	{
		th->momx = FixedMul(th->momx,FINECOSINE(source->player->aiming>>ANGLETOFINESHIFT));
		th->momy = FixedMul(th->momy,FINECOSINE(source->player->aiming>>ANGLETOFINESHIFT));
	}

	th->momz = FixedMul(th->info->speed, slope);

	slope = P_CheckMissileSpawn(th);

	return slope ? th : NULL;
}


// Simple Particles
// By Shuffle
// Extended by SRB2CB

// Todo: add more states then just "size"

// P_Particles
// Emits particles from the source.
// Frequency defines how often a call is ignored. Alive is how long the particle is alive.
// Color and speed are self-explainatory.
// For size, 0 is small, 1 is large(ish). (Size is only for MT_PARTICLE and MT_LIGHTPARTICLE)
// SRB2CBTODO: Merge and clean up
void P_Particles(mobj_t* source, mobjtype_t type, byte frequency, fixed_t speed, fixed_t zspeed, int alive, boolean pgravity, boolean addmomentum, byte color, byte repeat, int particlesprite)
{
	// Important thing to note, type is the kind of object that gets spawned,
	// what you actually end up touching is "particle"
	byte ignore;
	int prand, i;

	for (i = 0; i < repeat; i++)
	{
		mobj_t* particle;

		prand = P_Random()*2;
		ignore = P_Random();

		if (ignore > frequency)
			return;

		particle = P_SpawnMobj(source->x, source->y, source->z, type);
		particle->z++; // Particle can't be directly on the floor or it disappears!

		// Thrust the object in a random direction
		P_InstaThrust(particle, FixedAngle(((leveltime + P_Random()) % 360)*FRACUNIT), speed);
		P_SetObjectMomZ(particle, zspeed, false, false);

		// Randomize particles just a bit
		particle->momx += (leveltime % 5);
		particle->momy += (leveltime % 8);

		// Used for following the object that spawned it.
		if (addmomentum)
		{
			particle->momx += source->momx;
			particle->momy += source->momy;
			P_SetObjectMomZ(particle, source->momz, true, false);
		}

		// Change color, tics, flags, and gravity
		particle->tics = alive;
		particle->flags = 0;
		if (color)
		{
			particle->flags |= MF_TRANSLATION;
			particle->color = color;
		}
		particle->flags |= MF_NOBLOCKMAP|MF_NOCLIP|MF_SCENERY|MF_NOCLIPHEIGHT;
		particle->flags &= ~MF_NOGRAVITY;

		// Make sure you only try to change the states of particles
		if (particlesprite && (type == MT_PARTICLE)) // SRB2CBTODO: More sprites with states
		{
			// Change particle sprites with a switch{} of particle states
		}

		if (!pgravity)
			particle->flags |= MF_NOGRAVITY|MF_FLOAT;

		particle->fuse = alive; // Make sure the particles disappear

		if (particle->z <= particle->floorz)
			P_SetMobjState(particle, S_DISS); // make sure they disappear
	}
}

// Makes a spiral out of particles.
void P_SpiralParticles(mobj_t* source, fixed_t speed, fixed_t zspeed, int alive, boolean pgravity, byte color, int particlesprite, mobjtype_t type)
{
	mobj_t* particle;

	// Every other particle should have a light.
	particle = P_SpawnMobj(source->x, source->y, source->z, type);

	// Particle can't be directly on the floor/ceiling or it disappears!
	particle->z++;

	// Every other particle thrusts in back
	P_InstaThrust(particle, FixedAngle((source->angle*FRACUNIT) + ANG90), speed);

	P_SetObjectMomZ(particle, zspeed, false, false);

	// Change color, tics, and gravity
	particle->flags = 0;
	particle->flags |= MF_TRANSLATION;
	particle->color = color;
	particle->flags |= MF_NOBLOCKMAP|MF_NOCLIP|MF_SCENERY|MF_NOCLIPHEIGHT;

	particle->tics = alive;

	if (!pgravity)
		particle->flags |= MF_NOGRAVITY;

	// Make sure you only try to change the states of particles
	if (particlesprite && (type == MT_PARTICLE)) // SRB2CBTODO: More sprites with states
	{
		// Change particle sprites with a switch{} of particle states
	}

	source->angle+= 20; // Rotate
	particle->fuse = alive; // Make sure the particles disappear

	if (particle->z <= particle->floorz)
		P_SetMobjState(particle, S_DISS); // make sure they disappear

}


void P_BalloonParticles(mobj_t* source, mobjtype_t type, byte frequency, fixed_t speed, fixed_t zspeed, int alive, boolean pgravity, boolean addmomentum, byte color, byte repeat)
{
    // Important thing to note, type is the kind of object that gets spawned,
	// what you actually end up touching is "particle"
	byte ignore;
	short i;
	addmomentum = 0; // SRB2CBTODO: Remove this
	for (i = 0; i < repeat; i++)
	{
		ignore = P_Random();

		if (ignore > frequency)
			return;

		mobj_t* particle;

		particle = P_SpawnMobj(source->x, source->y, source->z, type);
		particle->z++; // Particle can't be directly on the floor or it disappears!

		source->angle += ANGLE_10; // Rotate

		zspeed = ((P_Random() % 4) + 1)*FRACUNIT;
		speed = ((P_Random() % 3) +1)*FRACUNIT;

		color = ((P_Random()+1) % MAXSKINCOLORS-1);

		if (!color)
			color = 1;
		if (color > MAXSKINCOLORS-1)
			color = MAXSKINCOLORS-1;

		// Thrust the object in a random direction
		P_Thrust(particle, source->angle, speed);
		P_SetObjectMomZ(particle, zspeed, false, false);

		// Used for following the object that spawned it.
		//if (addmomentum)
		{
			particle->momx += source->momx;
			particle->momy += source->momy;
			P_SetObjectMomZ(particle, source->momz, true, false);
		}

		// Change color, flags, and gravity
		//particle->flags = 0;
		particle->flags |= MF_TRANSLATION;
		particle->color = color;
		//particle->angle = color*(ANG45/45);

		// This method allows it to still collide with the player!
		particle->flags |= MF_SOLID;
		particle->flags |= MF_SPRING;
		particle->flags |= MF_NOCLIP;
		particle->fuse = alive;

		if (!pgravity)
			particle->flags |= MF_NOGRAVITY|MF_FLOAT;

		// Make sure you only try to change the states of particles
		//if (particlesprite && (type == MT_PARTICLE)) // SRB2CBTODO: More sprites with states
		{
			// Change particle sprites with a switch{} of particle states
		}
	}
}







