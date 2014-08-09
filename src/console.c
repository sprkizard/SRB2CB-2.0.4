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
/// \brief console drawing, input

#ifdef __GNUC__
#include <unistd.h>
#ifdef _XBOX
#include <openxdk/debug.h>
#endif
#endif

#include "doomdef.h"
#include "console.h"
#include "g_game.h"
#include "g_input.h"
#include "hu_stuff.h"
#include "keys.h"
#include "r_defs.h"
#include "sounds.h"
#include "st_stuff.h"
#include "s_sound.h"
#include "v_video.h"
#include "i_video.h"
#include "z_zone.h"
#include "i_system.h"
#include "d_main.h"
#include "m_menu.h"

#ifdef _WINDOWS
#include "win32/win_main.h"
#endif

#ifdef HWRENDER
#include "hardware/hw_main.h"
#endif

#define MAXHUDLINES 20

static boolean con_started = false;  // console has been initialised
boolean con_startup = false;  // true at game startup, screen need refreshing
static boolean con_forcepic = true; // at startup toggle console translucency when first off
boolean con_recalc;           // set true when screen size has changed

static tic_t con_tick;  // console ticker for anim or blinking prompt cursor
                        // con_scrollup should use time (currenttime - lasttime)..

static boolean consoletoggle; // true when console key pushed, ticker will handle
static boolean consoleready;  // console prompt is ready

int con_destlines;  // vid lines used by console at final position
static int con_curlines;  // vid lines currently used by console

int con_clipviewtop;  // clip value for planes & sprites, so that the
                            // part of the view covered by the console is not
                            // drawn when not needed, this must be -1 when
                            // console is off

static int con_hudlines;        // number of console heads up message lines
static int con_hudtime[MAXHUDLINES];  // remaining time of display for hud msg lines

int con_clearlines;      // top screen lines to refresh when view reduced
boolean con_hudupdate;   // when messages scroll, we need a backgrnd refresh

// console text output
static char *con_line;             // console text output current line
static size_t con_cx;              // cursor position in current line
static size_t con_cy;              // cursor line number in con_buffer, is always
                                   // increasing, and wrapped around in the text
                                   // buffer using modulo.

static size_t con_totallines;      // lines of console text into the console buffer
static size_t con_width;           // columns of chars, depend on vid mode width

static size_t con_scrollup;        // how many rows of text to scroll up (pgup/pgdn)
#ifdef CONSCALE
size_t con_scalefactor;            // text size scale factor
#endif

// hold 32 last lines of input for history
#define CON_MAXPROMPTCHARS 256
#define CON_PROMPTCHAR '>'

static char inputlines[32][CON_MAXPROMPTCHARS]; // hold last 32 prompt lines

static int inputline;    // current input line number
static int inputhist;    // line number of history input line to restore
static size_t input_cx;  // position in current input line

// protos.
static void CON_InputInit(void);
static void CON_RecalcSize(void);

static void CONS_hudlines_Change(void);
static void CON_DrawBackpic(patch_t *pic, int startx, int destwidth);

//======================================================================
//                   CONSOLE VARS AND COMMANDS
//======================================================================
#ifdef macintosh
#define CON_BUFFERSIZE 4096 // my compiler can't handle local vars > 32k
#else
#define CON_BUFFERSIZE 16384
#endif

static char con_buffer[CON_BUFFERSIZE];

// how many seconds the hud messages lasts on the screen
static consvar_t cons_msgtimeout = {"con_hudtime", "5", CV_SAVE, CV_Unsigned, NULL, 0, NULL, NULL, 0, 0, NULL};

// number of lines displayed on the HUD
static consvar_t cons_hudlines = {"con_hudlines", "5", CV_CALL|CV_SAVE, CV_Unsigned, CONS_hudlines_Change, 0, NULL, NULL, 0, 0, NULL};

// number of lines console move per frame
static consvar_t cons_speed = {"con_speed", "3", CV_SAVE, CV_Byte, NULL, 0, NULL, NULL, 0, 0, NULL};

// percentage of screen height to use for console
static consvar_t cons_height = {"con_height", "16", CV_SAVE, CV_Unsigned, NULL, 0, NULL, NULL, 0, 0, NULL};

// SRB2CBTODO: Console pause option

static CV_PossibleValue_t backpic_cons_t[] = {{0, "Translucent"}, {1, "Picture"}, {0, NULL}};
// Whether to use console background picture, or translucent mode // SRB2CBTODO: Trans only
static consvar_t cons_backpic = {"con_backpic", "Translucent", CV_SAVE, backpic_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};

static CV_PossibleValue_t backcolor_cons_t[] = {{0, "White"}, {1, "Orange"},
												{2, "Blue"}, {3, "Green"}, {4, "Black"},
												{5, "Red"}, {0, NULL}};
consvar_t cons_backcolor = {"con_backcolor", "Black", CV_SAVE, backcolor_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};

static void CON_Print(char *msg);

//
//
static void CONS_hudlines_Change(void)
{
	int i;

	// Clear the currently displayed lines
	for (i = 0; i < con_hudlines; i++)
		con_hudtime[i] = 0;

	if (cons_hudlines.value < 1)
		cons_hudlines.value = 1;
	else if (cons_hudlines.value > MAXHUDLINES)
		cons_hudlines.value = MAXHUDLINES;

	con_hudlines = cons_hudlines.value;

	CONS_Printf("Number of console HUD lines is now %d\n", con_hudlines);
}

// Clear console text buffer
//
static void CONS_Clear_f(void)
{
	memset(con_buffer, 0, CON_BUFFERSIZE);

	con_cx = 0;
	con_cy = con_totallines-1;
	con_line = &con_buffer[con_cy*con_width];
	con_scrollup = 0;
}

// Choose english keymap
//
static void CONS_English_f(void)
{
	shiftxform = english_shiftxform;
	CONS_Printf("English keymap.\n");
}

static char *bindtable[NUMINPUTS];

static void CONS_Bind_f(void)
{
	size_t na;
	int key;

	na = COM_Argc();

	if (na != 2 && na != 3)
	{
		CONS_Printf("bind <keyname> [<command>]\n");
		CONS_Printf("\2bind table :\n");
		na = 0;
		for (key = 0; key < NUMINPUTS; key++)
			if (bindtable[key])
			{
				CONS_Printf("%s : \"%s\"\n", G_KeynumToString(key), bindtable[key]);
				na = 1;
			}
		if (!na)
			CONS_Printf("Empty\n");
		return;
	}

	key = G_KeyStringtoNum(COM_Argv(1));
	if (!key)
	{
		CONS_Printf("Invalid key name\n");
		return;
	}

	Z_Free(bindtable[key]);
	bindtable[key] = NULL;

	if (na == 3)
		bindtable[key] = Z_StrDup(COM_Argv(2));
}

//======================================================================
//                          CONSOLE SETUP
//======================================================================

// Prepare a colormap for GREEN ONLY translucency over background
//
byte *yellowmap;
byte *purplemap;
byte *lgreenmap;
byte *bluemap;
byte *graymap;
byte *redmap;
byte *orangemap;

// XSRB2: new
byte *pinkmap;
byte *skybluemap;
byte *silvermap;

// Console BG colors
byte *cwhitemap;
byte *corangemap;
byte *cbluemap;
byte *cgreenmap;
byte *cgraymap;
byte *credmap;
static void CON_SetupBackColormap(void)
{
	int i, j, k;
	byte *pal;

	cwhitemap   = (byte *)Z_Malloc(256, PU_STATIC, NULL); // SRB2CBTODO: If this is static, make it a normal!
	corangemap  = (byte *)Z_Malloc(256, PU_STATIC, NULL);
	cbluemap    = (byte *)Z_Malloc(256, PU_STATIC, NULL);
	cgreenmap   = (byte *)Z_Malloc(256, PU_STATIC, NULL);
	cgraymap    = (byte *)Z_Malloc(256, PU_STATIC, NULL);
	credmap     = (byte *)Z_Malloc(256, PU_STATIC, NULL);

	yellowmap = (byte *)Z_Malloc(256, PU_STATIC, NULL);
	graymap   = (byte *)Z_Malloc(256, PU_STATIC, NULL);
	purplemap = (byte *)Z_Malloc(256, PU_STATIC, NULL);
	lgreenmap = (byte *)Z_Malloc(256, PU_STATIC, NULL);
	bluemap   = (byte *)Z_Malloc(256, PU_STATIC, NULL);
	redmap    = (byte *)Z_Malloc(256, PU_STATIC, NULL);
	orangemap = (byte *)Z_Malloc(256, PU_STATIC, NULL);
	
	// XSRB2: new
	pinkmap    = (byte *)Z_Malloc(256, PU_STATIC, NULL);
	skybluemap = (byte *)Z_Malloc(256, PU_STATIC, NULL);
	silvermap  = (byte *)Z_Malloc(256, PU_STATIC, NULL);

	pal = W_CacheLumpName("PLAYPAL", PU_CACHE);

	// setup the green translucent background colormaps
	for (i = 0, k = 0; i < 768; i += 3, k++)
	{
		j = pal[i] + pal[i+1] + pal[i+2];
		cwhitemap[k] = (byte)(15 - (j>>6));
		corangemap[k] = (byte)(95 - (j>>6));
		cbluemap[k] = (byte)(239 - (j>>6));
		cgreenmap[k] = (byte)(175 - (j>>6));
		cgraymap[k] = (byte)(31 - (j>>6));
		credmap[k] = (byte)(143 - (j>>6));
	}

	// setup the other colormaps, for console text

	// these don't need to be aligned, unless you convert the
	// V_DrawMappedPatch() into optimised asm.

	for (i = 0; i < 256; i++)
	{
		yellowmap[i] = (byte)i; // remap each color to itself...
		graymap[i] = (byte)i;
		purplemap[i] = (byte)i;
		lgreenmap[i] = (byte)i;
		bluemap[i] = (byte)i;
		redmap[i] = (byte)i;
		orangemap[i] = (byte)i;
		// XSRB2: New
		pinkmap[i] = (byte)i;
		silvermap[i] = (byte)i;
		skybluemap[i] = (byte)i;
	}

	yellowmap[3] = (byte)103;
	yellowmap[9] = (byte)115;
	purplemap[3] = (byte)250;
	purplemap[9] = (byte)253;
	lgreenmap[3] = (byte)162;
	lgreenmap[9] = (byte)170;
	bluemap[3]   = (byte)228;
	bluemap[9]   = (byte)238;
	graymap[3]   = (byte)28;
	graymap[9]   = (byte)28;
	redmap[3]    = (byte)126;
	redmap[9]    = (byte)127;
	orangemap[3] = (byte)86;
	orangemap[9] = (byte)88;
	
	// XSRB2: new
	pinkmap[3] = (byte)250;
	pinkmap[9] = (byte)253;
	skybluemap[3] = (byte)194;
	skybluemap[9] = (byte)198;
	silvermap[3] = (byte)94;
	silvermap[9] = (byte)100;
}

// Setup the console text buffer
//
void CON_Init(void)
{
	int i;

	for (i = 0; i < NUMINPUTS; i++)
		bindtable[i] = NULL;

	// clear all lines
	memset(con_buffer, 0, CON_BUFFERSIZE);

	// make sure it is ready for the loading screen
	con_width = 0;
	CON_RecalcSize();

	CON_SetupBackColormap();

	// NOTE: CON_Ticker should always execute at least once before D_Display()
	con_clipviewtop = -1; // -1 does not clip

	con_hudlines = atoi(cons_hudlines.defaultvalue);

	// setup console input filtering
	CON_InputInit();

	// register our commands
	//
	COM_AddCommand("cls", CONS_Clear_f);
	COM_AddCommand("english", CONS_English_f);
	// set console full screen for game startup MAKE SURE VID_Init() done !!!
	con_destlines = vid.height;
	con_curlines = vid.height;


	if (!dedicated)
	{
		con_started = true;
		con_startup = true; // Need explicit screen refresh until we are in SRB2's main loop
		consoletoggle = false;
		CV_RegisterVar(&cons_msgtimeout);
		CV_RegisterVar(&cons_hudlines);
		CV_RegisterVar(&cons_speed);
		CV_RegisterVar(&cons_height);
		CV_RegisterVar(&cons_backpic);
		CV_RegisterVar(&cons_backcolor);
		COM_AddCommand("bind", CONS_Bind_f);
	}
	else
	{
		con_started = true;
		con_startup = false; // need explicit screen refresh until we are in SRB2's main loop
		consoletoggle = true;
	}
}
// Console input initialization
//
static void CON_InputInit(void)
{
	int i;

	// prepare the first prompt line
	memset(inputlines, 0, sizeof (inputlines));
	for (i = 0; i < 32; i++)
		inputlines[i][0] = CON_PROMPTCHAR;
	inputline = 0;
	input_cx = 1;
}

//======================================================================
//                        CONSOLE EXECUTION
//======================================================================

// Called at screen size change to set the rows and line size of the
// console text buffer.
//
static void CON_RecalcSize(void)
{
	size_t conw, oldcon_width, oldnumlines, i, oldcon_cy;
	XBOXSTATIC char tmp_buffer[CON_BUFFERSIZE];
	XBOXSTATIC char string[CON_BUFFERSIZE]; // BP: it is a line but who know

#ifndef CONSCALE
	con_recalc = false;

	conw = (vid.width>>3) - 2;

	if (con_curlines == 200) // first init
	{
		con_curlines = vid.height;
		con_destlines = vid.height;
	}
#else
	switch (cv_constextsize.value)
	{
		case V_NOSCALEPATCH:
			con_scalefactor = 1;
			break;
		case V_SMALLSCALEPATCH:
			con_scalefactor = vid.smalldupx;
			break;
		case V_MEDSCALEPATCH:
			con_scalefactor = vid.meddupx;
			break;
		default:	// Full scaling
			con_scalefactor = vid.dupx;
			break;
	}
	
	con_recalc = false;
	
	conw = (vid.width>>3) / con_scalefactor - 2;
	
	if (con_curlines == vid.height) // first init
	{
		con_curlines = vid.height;
		con_destlines = vid.height;
	}
#endif

	// check for change of video width
	if (conw == con_width)
		return; // didn't change

	oldcon_width = con_width;
	oldnumlines = con_totallines;
	oldcon_cy = con_cy;
	memcpy(tmp_buffer, con_buffer, CON_BUFFERSIZE);

	if (conw < 1)
		con_width = (BASEVIDWIDTH>>3) - 2;
	else
		con_width = conw;

	con_width += 11; // Graue 06-19-2004 up to 11 control chars per line

	con_totallines = CON_BUFFERSIZE / con_width;
	memset(con_buffer, ' ', CON_BUFFERSIZE);

	con_cx = 0;
	con_cy = con_totallines-1;
	con_line = &con_buffer[con_cy*con_width];
	con_scrollup = 0;

	// re-arrange console text buffer to keep text
	if (oldcon_width) // not the first time
	{
		for (i = oldcon_cy + 1; i < oldcon_cy + oldnumlines; i++)
		{
			if (tmp_buffer[(i%oldnumlines)*oldcon_width])
			{
				memcpy(string, &tmp_buffer[(i%oldnumlines)*oldcon_width], oldcon_width);
				conw = oldcon_width - 1;
				while (string[conw] == ' ' && conw)
					conw--;
				string[conw+1] = '\n';
				string[conw+2] = '\0';
				CON_Print(string);
			}
		}
	}
}

// Handles Console moves in/out of screen (per frame)
//
static void CON_MoveConsole(void)
{
	// up/down move to dest
	if (con_curlines < con_destlines)
	{
		con_curlines += (int)(cons_speed.value*vid.fdupy)/NEWTICRATERATIO;
		if (con_curlines > con_destlines)
			con_curlines = con_destlines;
	}
	else if (con_curlines > con_destlines)
	{
		con_curlines -= (int)(cons_speed.value*vid.fdupy)/NEWTICRATERATIO;
		if (con_curlines < con_destlines)
			con_curlines = con_destlines;
	}
}

// Clear time of console heads up messages
//
void CON_ClearHUD(void)
{
	int i;

	for (i = 0; i < con_hudlines; i++)
		con_hudtime[i] = 0;
}

// Force the console to move out immediately
// NOTE: Con_Ticker will set 'consoleready' false
void CON_ToggleOff(void)
{
	if (!con_destlines)
		return;

	con_destlines = 0;
	con_curlines = 0;
	CON_ClearHUD();
	con_forcepic = 0;
	con_clipviewtop = -1; // remove console clipping of view
}

// Console ticker: handles console move in/out, cursor blinking
//
void CON_Ticker(void)
{
	int i;
#ifdef CONSCALE
	int minheight = 20 * con_scalefactor;	// 20 = 8+8+4
#endif

	// cursor blinking
	con_tick++;

	// console key was pushed
	if (consoletoggle)
	{
		consoletoggle = false;

		// toggle off console
		if (con_destlines > 0)
		{
			con_destlines = 0;
			CON_ClearHUD();
		}
		else
		{
			// toggle console in
			con_destlines = (cons_height.value*vid.height)/100;
#ifndef CONSCALE
			if (con_destlines < 20)
				con_destlines = 20;
#else
			if (con_destlines < minheight)
				con_destlines = minheight;
#endif
			else if (con_destlines > vid.height)
				con_destlines = vid.height;

			con_destlines &= ~0x3; // multiple of text row height
		}
	}

	// console movement
	if (con_destlines != con_curlines)
		CON_MoveConsole();

	// clip the view, so that the part under the console is not drawn
	con_clipviewtop = -1;
	if (cons_backpic.value) // clip only when using an opaque background
	{
		if (con_curlines > 0)
			con_clipviewtop = con_curlines - viewwindowy - 1 - 10;
		// NOTE: BIG HACK::SUBTRACT 10, SO THAT WATER DON'T COPY LINES OF THE CONSOLE // SRB2CBTODO: Hacks are bad...
		//       WINDOW!!! (draw some more lines behind the bottom of the console)
		if (con_clipviewtop < 0)
			con_clipviewtop = -1; // maybe not necessary, provided it's < 0
	}

	// check if console ready for prompt
#ifndef CONSCALE
	if (con_destlines >= 20)
#else
	if (con_destlines >= minheight)
#endif
		consoleready = true;
	else
		consoleready = false;

	// Make overlay messages disappear after a while
	for (i = 0; i < con_hudlines; i++)
	{
		con_hudtime[i]--; // SRB2CBTODO: For hud time, use fade in and out too!
		if (con_hudtime[i] < 0)
			con_hudtime[i] = 0;
	}
}

// Handles console key input
//
boolean CON_Responder(event_t *ev) // SRB2CBTODO: USE CAPSLOCK and moveable text input!
{
	static boolean shiftdown;

	// sequential completions a la 4dos
	static char completion[80];
	static int comskips, varskips;

	const char *cmd = "";
	int key;

	if (chat_on)
		return false;

	// special keys state
	if ((ev->data1 == KEY_LSHIFT || ev->data1 == KEY_RSHIFT) && ev->type == ev_keyup)
	{
		shiftdown = false;
		return false;
	}

	// let go keyup events, don't eat them
	if (ev->type != ev_keydown && ev->type != ev_console)
		return false;

	key = ev->data1;

	// check for console toggle key
	if (ev->type != ev_console)
	{
		if (key == gamecontrol[gc_console][0] || key == gamecontrol[gc_console][1])
		{
			consoletoggle = true;

			if (timeattacking)
			{
				G_CheckDemoStatus();
				timeattacking = true;
				M_StartControlPanel();
				consoletoggle = false;
			}

			return true;
		}

		// check other keys only if console prompt is active
		if (!consoleready && key < NUMINPUTS) // Boundary check!!
		{
			if (bindtable[key] && !timeattacking)
			{
				COM_BufAddText(bindtable[key]);
				COM_BufAddText("\n");
				return true;
			}
			return false;
		}

		// escape key toggle off console
		if (key == KEY_ESCAPE)
		{
			consoletoggle = true;

			if (timeattacking)
				G_CheckDemoStatus();

			return true;
		}

	}

	// eat shift only if console active // SRB2CBTODO: CAPS LOCK!
	if (key == KEY_LSHIFT || key == KEY_RSHIFT)
	{
		shiftdown = true;
		return true;
	}

	// command completion forward (tab) and backward (shift-tab) // SRB2CBTODO: LOOP the COMPLETION
	if (key == KEY_TAB)
	{
		// sequential command completion forward and backward

		// remember typing for several completions (a-la-4dos)
		if (inputlines[inputline][input_cx-1] != ' ')
		{
			if (strlen(inputlines[inputline]+1) < 80)
				strcpy(completion, inputlines[inputline]+1);
			else
				completion[0] = 0;

			comskips = varskips = 0;
		}
		else
		{
			if (shiftdown || capslock)
			{
				if (comskips < 0)
				{
					if (--varskips < 0)
						comskips = -comskips - 2;
				}
				else if (comskips > 0)
					comskips--;
			}
			else
			{
				if (comskips < 0)
					varskips++;
				else
					comskips++;
			}
		}

		if (comskips >= 0)
		{
			cmd = COM_CompleteCommand(completion, comskips);
			if (!cmd)
				// dirty: make sure if comskips is zero, to have a neg value
				comskips = -comskips - 1;
		}
		if (comskips < 0)
			cmd = CV_CompleteVar(completion, varskips);

		if (cmd)
		{
			memset(inputlines[inputline]+1, 0, CON_MAXPROMPTCHARS-1);
			strcpy(inputlines[inputline]+1, cmd);
			input_cx = strlen(cmd) + 1;
			inputlines[inputline][input_cx] = ' ';
			input_cx++;
			inputlines[inputline][input_cx] = 0;
		}
		else
		{
			if (comskips > 0)
				comskips--;
			else if (varskips > 0)
				varskips--;
		}

		return true;
	}

	// move up (backward) in console textbuffer
	if (key == KEY_PGUP)
	{
		if (con_scrollup < (con_totallines-((con_curlines-16)>>3)))
			con_scrollup++;
		return true;
	}
	else if (key == KEY_PGDN)
	{
		if (con_scrollup > 0)
			con_scrollup--;
		return true;
	}

	if (key == KEY_HOME) // oldest text in buffer
	{
		con_scrollup = (con_totallines-((con_curlines-16)>>3));
		return true;
	}
	else if (key == KEY_END) // most recent text in buffer
	{
		con_scrollup = 0;
		return true;
	}

	// command enter
	if (key == KEY_ENTER)
	{
		if (input_cx < 2)
			return true;

		// push the command
		COM_BufAddText(inputlines[inputline]+1);
		COM_BufAddText("\n");

		CONS_Printf("%s\n", inputlines[inputline]);

		inputline = (inputline+1) & 31;
		inputhist = inputline;

		memset(inputlines[inputline], 0, CON_MAXPROMPTCHARS);
		inputlines[inputline][0] = CON_PROMPTCHAR;
		input_cx = 1;

		return true;
	}

	// backspace command prompt
	if (key == KEY_BACKSPACE)
	{
		if (input_cx > 1)
		{
			input_cx--;
			inputlines[inputline][input_cx] = 0;
		}
		return true;
	}
	
#define AWESOMECONSOLE
	
	// SRB2CBTODO: Back and forth console
#ifdef AWESOMECONSOLE
	if (key == KEY_LEFTARROW)
	{
		if (input_cx > 1)
		{
			input_cx--;
			//inputlines[inputline][input_cx] = 0;
		}
		return true;
	}
	
	if (key == KEY_RIGHTARROW)
	{
		// SRB2CBTODO: This restricts the input to the
		// last line in the console correctly, but how?
		if (input_cx < (size_t)inputlines[inputline][input_cx])
		{
			input_cx++;
			//inputlines[inputline][input_cx] = 0;
		}
		return true;
	}
#endif

	// move back in input history
	if (key == KEY_UPARROW)
	{
		// copy one of the previous inputlines to the current
		do
		{
			inputhist = (inputhist - 1) & 31; // cycle back
		} while (inputhist != inputline && !inputlines[inputhist][1]);

		// stop at the last history input line, which is the
		// current line + 1 because we cycle through the 32 input lines
		if (inputhist == inputline)
			inputhist = (inputline + 1) & 31;

		memcpy(inputlines[inputline], inputlines[inputhist], CON_MAXPROMPTCHARS);
		input_cx = strlen(inputlines[inputline]);

		return true;
	}

	// move forward in input history
	if (key == KEY_DOWNARROW)
	{
		if (inputhist == inputline)
			return true;
		do
		{
			inputhist = (inputhist + 1) & 31;
		} while (inputhist != inputline && !inputlines[inputhist][1]);

		memset(inputlines[inputline], 0, CON_MAXPROMPTCHARS);

		// back to currentline
		if (inputhist == inputline)
		{
			inputlines[inputline][0] = CON_PROMPTCHAR;
			input_cx = 1;
		}
		else
		{
			strcpy(inputlines[inputline], inputlines[inputhist]);
			input_cx = strlen(inputlines[inputline]);
		}
		return true;
	}

	// allow people to use keypad in console (good for typing IP addresses) - Calum
	if (key >= KEY_KEYPAD7 && key <= KEY_KPADDEL)
	{
		XBOXSTATIC char keypad_translation[] = {'7','8','9','-',
		                                        '4','5','6','+',
		                                        '1','2','3',
		                                        '0','.'};

		key = keypad_translation[key - KEY_KEYPAD7];
	}
	else if (key == KEY_KPADSLASH)
		key = '/';

	if (shiftdown || capslock)
		key = shiftxform[key];

	// enter a char into the command prompt
	if (key < 32 || key > 127)
		return false;

	// add key to cmd line here
	if (input_cx < CON_MAXPROMPTCHARS)
	{
		if (key >= 'A' && key <= 'Z' && !(shiftdown || capslock)) // This is only really necessary for dedicated servers
			key = key + 'a' - 'A';

		inputlines[inputline][input_cx] = (char)key; // Replace the current posistion in the console
#ifndef AWESOMECONSOLE
		inputlines[inputline][input_cx + 1] = 0; // SRB2CBTODO: The next char in input should insert to move over all lines
#else
		// SRB2CBTODO: Check for moving over all chars in front on the line
#endif
		input_cx++;
	}

	return true;
}

// Insert a new line in the console text buffer
//
static void CON_Linefeed(void) // SRB2CBTODO: Support full console left to right insert and stuff
{
	// set time for heads up messages
	con_hudtime[con_cy%con_hudlines] = cons_msgtimeout.value*TICRATE;

	con_cy++;
	con_cx = 0;

	con_line = &con_buffer[(con_cy%con_totallines)*con_width];
	memset(con_line, ' ', con_width);

	// make sure the view borders are refreshed if hud messages scroll
	con_hudupdate = true; // see HU_Erase()
}

// Outputs text into the console text buffer
static void CON_Print(char *msg)
{
	size_t l;
	int controlchars = 0; // for color changing

	if (*msg == '\3') // chat text, makes ding sound
		S_StartSound(NULL, sfx_radio);
	else if (*msg == '\4') // chat action, dings and is in yellow
	{
		*msg = '\x82'; // yellow
		S_StartSound(NULL, sfx_radio);
	}

	if (!(*msg & 0x80))
	{
		con_line[con_cx++] = '\x80';
		controlchars = 1;
	}

	while (*msg)
	{
		// skip non-printable characters and white spaces
		while (*msg && *msg <= ' ')
		{
			if (*msg & 0x80)
			{
				con_line[con_cx++] = *(msg++);
				controlchars++;
				continue;
			}
			else if (*msg == '\r') // carriage return
			{
				con_cy--;
				CON_Linefeed();
				controlchars = 0;
			}
			else if (*msg == '\n') // linefeed
			{
				CON_Linefeed();
				controlchars = 0;
			}
			else if (*msg == ' ') // space
			{
				con_line[con_cx++] = ' ';
				if (con_cx - controlchars >= con_width-11)
				{
					CON_Linefeed();
					controlchars = 0;
				}
			}
			else if (*msg == '\t')
			{
				// adds tab spaces for nice layout in console

				do
				{
					con_line[con_cx++] = ' ';
				} while ((con_cx - controlchars) % 4 != 0);

				if (con_cx - controlchars >= con_width-11)
				{
					CON_Linefeed();
					controlchars = 0;
				}
			}
			msg++;
		}

		if (*msg == '\0')
			return;

		// printable character
		for (l = 0; l < (con_width-11) && msg[l] > ' '; l++)
			;

		// word wrap
		if ((con_cx - controlchars) + l > con_width-11)
		{
			CON_Linefeed();
			controlchars = 0;
		}

		// a word at a time
		for (; l > 0; l--)
			con_line[con_cx++] = *(msg++);
	}
}

// Console print! Wahooo! Lots o fun!
//

void CONS_Printf(const char *fmt, ...)
{
	va_list argptr;
	XBOXSTATIC char txt[8192];

	va_start(argptr, fmt);
	vsprintf(txt, fmt, argptr);
	va_end(argptr);

	// Store all CONS_Printf to the game's log
	DEBFILE(txt);
	
	const char *stringy = NULL;

	// Write message into the console text buffer,
	// only print to the game's console if it has already started
	if (con_started)
		CON_Print(txt);
	
	CONS_LogPrintf(txt, stringy);

	// Text can be visible even when the console isn't
	con_scrollup = 0;

	// If the game is not in the main display loop(The main game rendering),
	// force screen update
	if (con_startup)
	{
		// Here we display the console background and console text
		// (no hardware accelerated support for these versions)
		CON_Drawer();
		I_FinishUpdate();
	}
}

// Print an error message, and wait for ENTER key to continue.
// To make sure the user has seen the message
// If not on Windows, don't press enter to continue,
// (because it causes some OS's to stall)
//
// SRB2CBTODO: Make CONS_Error integrate into the game,
// either making a window on all OS's or by some other means
void CONS_Error(const char *msg)
{
#ifdef RPC_NO_WINDOWS_H
	if (!graphics_started)
	{
		MessageBoxA(vid.WndParent, msg, "SRB2 Warning", MB_OK);
		return;
	}
#endif
	CONS_Printf("\2%s", msg); // write error msg in different colour
}

// -----------------+
// CONS_LogPrintf       : Outputs messages to the game's log file as long as LOGMESSAGES is defined,
//                  : otherwise it does nothing
// Returns          :
// -----------------+
FUNCPRINTF void CONS_LogPrintf(const char *lpFmt, ...)
{
#ifdef LOGMESSAGES
	char    str[4096] = "";
	va_list arglist;
	
	va_start (arglist, lpFmt);
	vsnprintf (str, 4096, lpFmt, arglist);
	va_end   (arglist);
#ifdef _WINDOWS
	{
		DWORD bytesWritten;
		if (logstream != INVALID_HANDLE_VALUE)
			WriteFile(logstream, str, (DWORD)strlen(str), &bytesWritten, NULL);
	}
#else
	if (logstream)
	{
		size_t d;
		d = fwrite(str, strlen(str), 1, logstream);
	}
#endif
	
#else // LOGMESSAGES
	(void)lpFmt;
#endif
}

//======================================================================
//                          CONSOLE DRAW
//======================================================================

// draw console prompt line
//
#ifdef CONSCALE
static void CON_DrawInput(void)
{
	char *p;
	size_t x;
	int y;
	int charwidth = (int)con_scalefactor << 3;
	
	// input line scrolls left if it gets too long
	p = inputlines[inputline];
	if (input_cx >= con_width-11)
		p += input_cx - (con_width-11) + 1;
	
	y = con_curlines - 12 * con_scalefactor;
	
	for (x = 0; x < con_width-11; x++)
		V_DrawCharacter((int)x*charwidth, y, p[x]|cv_constextsize.value|V_NOSCALESTART, !cv_allcaps.value);
	
	// Draw a blinking cursor for the console
	x = (input_cx >= con_width-11) ? (con_width-11) - 1 : input_cx;
	if (con_tick & 4*NEWTICRATERATIO)
		V_DrawCharacter((int)(x*charwidth), y, '_'|cv_constextsize.value|V_NOSCALESTART, !cv_allcaps.value);
}

// draw the last lines of console text to the top of the screen
static void CON_DrawHudlines(void)
{
	byte *p;
	size_t i;
	int y;
	int charflags = 0;
	int charwidth = (int)con_scalefactor << 3;
	int charheight = charwidth;
	
	if (con_hudlines <= 0)
		return;
	
	if (chat_on)
		y = charheight; // leave place for chat input in the first row of text
	else
		y = 0;
	
	for (i = con_cy - con_hudlines+1; i <= con_cy; i++)
	{
		size_t c;
		int x;
		
		if ((signed)i < 0)
			continue;
		if (con_hudtime[i%con_hudlines] == 0)
			continue;
		
		p = (byte *)&con_buffer[(i%con_totallines)*con_width];
		
		for (c = 0, x = 0; c < con_width; c++, x += charwidth, p++)
		{
			while (*p & 0x80) // Graue 06-19-2004
			{
				charflags = (*p & 0x7f) << V_CHARCOLORSHIFT;
				p++;
			}
			V_DrawCharacter(x, y, (int)(*p) | charflags | cv_constextsize.value | V_NOSCALESTART, !cv_allcaps.value);
		}
		
		V_DrawCharacter(x, y, (p[c]&0xff) | cv_constextsize.value | V_NOSCALESTART, !cv_allcaps.value);
		y += charheight;
	}
	
	// top screen lines that might need clearing when view is reduced
	con_clearlines = y; // this is handled by HU_Erase();
}


#else



static void CON_DrawInput(void)
{
	char *p;
	size_t x;
	int y;

	// input line scrolls left if it gets too long
	p = inputlines[inputline];
	if (input_cx >= con_width-11)
		p += input_cx - (con_width-11) + 1;

	y = con_curlines - 12;

	for (x = 0; x < con_width-11; x++)
		V_DrawCharacter((int)(x+1)<<3, y, p[x]|V_NOSCALEPATCH|V_NOSCALESTART, !cv_allcaps.value);

	// Draw a blinking cursor for the console
	x = (input_cx >= con_width-11) ? (con_width-11) - 1 : input_cx;
	if (con_tick & 4*NEWTICRATERATIO)
		V_DrawCharacter((int)(x+1)<<3, y+2, '_'|V_NOSCALEPATCH|V_NOSCALESTART, !cv_allcaps.value);
}

// Draw the last lines of console text to the top of the screen
// when the console isn't even out
static void CON_DrawHudlines(void) // SRB2CBTODO!:! Fade this stuff in and out with con_hudtime stuff and alpha!
{
	byte *p;
	size_t i, x;
	int y;
	int charflags = 0;

	if (con_hudlines <= 0)
		return;

	if (chat_on)
		y = 8; // leave place for chat input in the first row of text
	else
		y = 0;

	for (i = con_cy - con_hudlines+1; i <= con_cy; i++)
	{
		if ((signed)i < 0)
			continue;
		if (con_hudtime[i%con_hudlines] == 0)
			continue;

		p = (byte *)&con_buffer[(i%con_totallines)*con_width];

		for (x = 0; x < con_width; x++, p++)
		{
			while (*p & 0x80) // Graue 06-19-2004
			{
				charflags = (*p & 0x7f) << 8;
				p++;
			}
			V_DrawCharacter((int)(x)<<3, y, (int)(*p) | charflags | V_NOSCALEPATCH|V_NOSCALESTART, !cv_allcaps.value);
		}

		V_DrawCharacter((int)(x)<<3, y, (p[x]&0xff)|V_NOSCALEPATCH|V_NOSCALESTART, !cv_allcaps.value); // SRB2CBTODO: ALLCAPS is stupid, no one wants to use it
		y += 8;
	}

	// top screen lines that might need clearing when view is reduced
	con_clearlines = y; // this is handled by HU_Erase();
}

#endif

// Scale a pic_t at 'startx' pos, to 'destwidth' columns.
//   startx, destwidth is resolution dependent
// Used to draw console borders, console background.
// The pic must be sized BASEVIDHEIGHT height.
static void CON_DrawBackpic(patch_t *pic, int startx, int destwidth)
{
	startx = destwidth = 0;
	V_DrawScaledPatch(0, 0, 0, pic);
}

// draw the console background, text, and prompt if enough place
//
#ifdef CONSCALE
static void CON_DrawConsole(void)
{
	byte *p;
	size_t i;
	int y;
	int w = 0, x2 = 0;
	int charflags = 0;
	int charwidth = (int)con_scalefactor << 3;
	int charheight = charwidth;
	int minheight = 20 * con_scalefactor;	// 20 = 8+8+4
	
	if (con_curlines <= 0)
		return;
	
	//FIXME: refresh borders only when console bg is translucent
	con_clearlines = con_curlines; // clear console draw from view borders
	con_hudupdate = true; // always refresh while console is on
	
	// draw console background
#if 0
	// draw console background
	if (cons_backpic.value || con_forcepic)
	{
		if (rendermode == render_opengl)
			CON_DrawBackpic(con_backpic, 0, vid.width);
		else if (rendermode == render_soft)
			V_DrawScaledPatch(0, 0, 0, con_backpic); // picture as background
	}
#else
	// draw console background
	if (cons_backpic.value || con_forcepic)
	{
		static lumpnum_t con_backpic_lumpnum = UINT32_MAX;
		patch_t *con_backpic;
		
		if (con_backpic_lumpnum == UINT32_MAX)
			con_backpic_lumpnum = W_GetNumForName("CONSBACK");
		
		con_backpic = (patch_t*)W_CachePatchNum(con_backpic_lumpnum, PU_CACHE);
		
		if (rendermode == render_opengl)
			V_DrawScaledPatch(0, 0, 0, con_backpic);
		else if (rendermode == render_soft)
			CON_DrawBackpic(con_backpic, 0, vid.width); // picture as background
	}
#endif
	else
	{
		x2 = vid.width;
		if (rendermode != render_none)
			V_DrawFadeConsBack(w, 0, x2, con_curlines, cons_backcolor.value); // translucent background
	}
	
	// draw console text lines from top to bottom
	if (con_curlines < minheight)
		return;
	
	i = con_cy - con_scrollup;
	
	// skip the last empty line due to the cursor being at the start of a new line
	if (!con_scrollup && !con_cx)
		i--;
	
	i -= (con_curlines - minheight) / charheight;
	
	if (rendermode == render_none) return;
	
	for (y = (con_curlines-minheight) % charheight; y <= con_curlines-minheight; y += charheight, i++)
	{
		int x;
		size_t c;
		
		p = (byte *)&con_buffer[((i > 0 ? i : 0)%con_totallines)*con_width];
		
		for (c = 0, x = charwidth; c < con_width; c++, x += charwidth, p++)
		{
			while (*p & 0x80)
			{
				charflags = (*p & 0x7f) << V_CHARCOLORSHIFT;
				p++;
			}
			if (con_startup)
				V_DrawCharacter(x, y, (int)(*p) | charflags | V_NOSCALEPATCH | V_NOSCALESTART, !cv_allcaps.value);
			else
				V_DrawCharacter(x, y, (int)(*p) | charflags | cv_constextsize.value | V_NOSCALESTART, !cv_allcaps.value);
		}
	}
	
	// draw prompt if enough place (not while game startup)
	if ((con_curlines == con_destlines) && (con_curlines >= minheight) && !con_startup)
		CON_DrawInput();
}

#else

static void CON_DrawConsole(void)
{
	byte *p;
	size_t i, x;
	int y;
	int w = 0, x2 = 0;
	int charflags = 0;

	if (con_curlines <= 0)
		return;

	//FIXME: refresh borders only when console bg is translucent
	con_clearlines = con_curlines; // clear console draw from view borders
	con_hudupdate = true; // always refresh while console is on
	
#if 0
	// draw console background
	if (cons_backpic.value || con_forcepic)
	{
		if (rendermode == render_opengl)
			CON_DrawBackpic(con_backpic, 0, vid.width);
		else if (rendermode == render_soft)
			V_DrawScaledPatch(0, 0, 0, con_backpic); // picture as background
	}
#else
	// draw console background
	if (cons_backpic.value || con_forcepic)
	{
		static lumpnum_t con_backpic_lumpnum = UINT32_MAX;
		patch_t *con_backpic;
		
		if (con_backpic_lumpnum == UINT32_MAX)
			con_backpic_lumpnum = W_GetNumForName("CONSBACK");
		
		con_backpic = (patch_t*)W_CachePatchNum(con_backpic_lumpnum, PU_CACHE);
		
		if (rendermode != render_soft)
			V_DrawScaledPatch(0, 0, 0, con_backpic);
		else if (rendermode != render_none)
			CON_DrawBackpic(con_backpic, 0, vid.width); // picture as background
	}
#endif
	else
	{
		x2 = vid.width;
		if (rendermode != render_none)
			V_DrawFadeConsBack(w, 0, x2, con_curlines, cons_backcolor.value); // translucent background
	}

	// draw console text lines from top to bottom
	if (con_curlines < 20) // 8+8+4
		return;

	i = con_cy - con_scrollup;

	// skip the last empty line due to the cursor being at the start of a new line
	if (!con_scrollup && !con_cx)
		i--;

	i -= (con_curlines - 20) / 8;

	if (rendermode == render_none) return;

	for (y = (con_curlines-20) % 8; y <= con_curlines-20; y += 8, i++)
	{
		p = (byte *)&con_buffer[((i > 0 ? i : 0)%con_totallines)*con_width];

		for (x = 0; x < con_width; x++, p++)
		{
			while (*p & 0x80)
			{
				charflags = (*p & 0x7f) << 8;
				p++;
			}
			V_DrawCharacter((int)(x+1)<<3, y, (int)(*p)|charflags|V_NOSCALEPATCH|V_NOSCALESTART, !cv_allcaps.value);
		}
	}

	// draw prompt if enough place (not while game startup)
	if ((con_curlines == con_destlines) && (con_curlines >= 20) && !con_startup)
		CON_DrawInput();
}
#endif

// Console refresh drawer, call each frame
//
void CON_Drawer(void)
{
	if (!con_started || !graphics_started)
		return;

	if (con_recalc)
		CON_RecalcSize();

	if (con_curlines > 0)
		CON_DrawConsole();
	else if (gamestate == GS_LEVEL || gamestate == GS_INTERMISSION || gamestate == GS_CUTSCENE || gamestate == GS_CREDITS)
		CON_DrawHudlines();
}
