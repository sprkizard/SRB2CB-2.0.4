 ///
 /// JTEBOTS
 /// By: JTE (Jason the Echidna)
 ///
 /// \file
 /// \brief Definition of the player bot structure

#ifdef JTEBOTS
#ifndef __P_BOTS__
#define __P_BOTS__
#include "r_defs.h"

#ifdef BOTWAYPOINTS
typedef struct botwaypoint_s
	{
		fixed_t x,y,z;
		sector_t *sec;
		boolean springpoint;
		struct botwaypoint_s *next;
	} botwaypoint_t;
#endif
typedef struct bot_s
	{
		player_t* player; // Your player struct
		byte ownernum; // Your owner's number
		boolean springmove; // If you hit a diagonal spring or not
		mobj_t* target; // The mobj you're following
#ifdef BOTWAYPOINTS
		botwaypoint_t *waypoint; // Your waypoint in race
		fixed_t waydist; // Distance from the last waypoint to the next one
#endif
		short targettimer; // How long you've been trying to get to the same mobj
	} bot_t;

extern bot_t bots[MAXPLAYERS];
extern char charselbots[15][16];
extern boolean jb_cmdwait;
#endif

#endif // __P_BOTS__


