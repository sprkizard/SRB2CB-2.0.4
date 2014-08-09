#include "../doomdef.h"
#include "../doomtype.h"
#include "../m_argv.h"
#include "../d_main.h"
#include "../i_system.h"

#ifdef _WIN32
#include <SDL/SDL.h>
#else
#include "SDL.h"
#endif


FILE *logstream = NULL;

int main(int argc,char** argv)
{
	myargc = argc;
	myargv = argv;
	
	const char *logdir = D_Home();
	if (logdir)
		logstream = fopen(va("%s/"DEFAULTDIR"/"LOGNAME".txt",logdir), "w");
	
	
	if (SDL_Init(SDL_INIT_VIDEO) < 0)
		I_Error("SDL_InitSubSystem(): %s\n", SDL_GetError());
	
	if (SDL_Init(SDL_INIT_NOPARACHUTE) < 0)
		I_Error("SDL System Error: %s", SDL_GetError());
	
	
	I_StartupConsole();
	
	D_SRB2Main(); // Host_Init(); // This is the main entry point into our program
	D_SRB2Loop(); // Remember, a main loop never returns unless you exit the program
}





	
	
	
