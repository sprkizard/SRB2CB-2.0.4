/*   SDLMain.m - main entry point for our Cocoa-ized SDL app
       Initial Version: Darrell Walisser <dwaliss1@purdue.edu>
       Non-NIB-Code & other changes: Max Horn <max@quendi.de>

    Feel free to customize this file to suit your needs
*/

#import "SDL.h"
#import "SDL_macosx_main.h"
#import <sys/param.h> /* for MAXPATHLEN */
#import <unistd.h>

/* For some reaon, Apple removed setAppleMenu from the headers in 10.4,
 but the method still is there and works. To avoid warnings, we declare
 it ourselves here. */
@interface NSApplication(SDL_Missing_Methods)
- (void)setAppleMenu:(NSMenu *)menu;
@end

static int    gArgc;
static char  **gArgv;
static BOOL   gFinderLaunch;

// Sends a parameter to the main application
// it is used as: addArgument("-param");
static void addArgument(const char *value)
{
	if (!gArgc)
		gArgv = (char **)malloc(sizeof(*gArgv));
	else
	{
		char **newgArgv = (char **)realloc(gArgv, sizeof(*gArgv) * (gArgc + 1));
		if (!newgArgv)
		{
			newgArgv = malloc(sizeof(*gArgv) * (gArgc + 1));
			memcpy(newgArgv, gArgv, sizeof(*gArgv) * gArgc);
			free(gArgv);
		}
		gArgv = newgArgv;
	}
	gArgc++;
	gArgv[gArgc - 1] = (char *)malloc(sizeof(char) * (strlen(value) + 1));
	strcpy(gArgv[gArgc - 1], value);
}

static NSString *getApplicationName(void)
{
    NSDictionary *dict;
    NSString *appName = NULL;

    /* Determine the application name */
    dict = (NSDictionary *)CFBundleGetInfoDictionary(CFBundleGetMainBundle());
    if (dict)
        appName = [dict objectForKey: @"CFBundleName"];

    if (![appName length])
        appName = [[NSProcessInfo processInfo] processName];

    return appName;
}


@interface SDLApplication : NSApplication
@end

@implementation SDLApplication
/* Invoked from the Quit menu item */
- (void)terminate:(id)sender
{
#if (__GNUC__ > 3) || (__GNUC__ == 3 && __GNUC_MINOR__ >= 1)
    (void)sender;
#endif
    /* Post a SDL_QUIT event */
    SDL_Event event;
    event.type = SDL_QUIT;
    SDL_PushEvent(&event);
}
@end

/* The main class of the application, the application's delegate */
@implementation SDLMain

/* Set the working directory to the .app's parent directory */
- (void) setupWorkingDirectory:(BOOL)shouldChdir
{
    if (shouldChdir)
    {
        char parentdir[MAXPATHLEN];
		CFURLRef url = CFBundleCopyBundleURL(CFBundleGetMainBundle());
		CFURLRef url2 = CFURLCreateCopyDeletingLastPathComponent(0, url);
		
		if (CFURLGetFileSystemRepresentation(url2, true, (UInt8 *)parentdir, MAXPATHLEN))
			assert(chdir(parentdir) == 0);   /* chdir to the binary app's parent */

		CFRelease(url);
		CFRelease(url2);
    }
	
}


static void setApplicationMenu(void)
{
    /* warning: this code is very odd */
    NSMenu *appleMenu;
    NSMenuItem *menuItem;
    NSString *title;
    NSString *appName;

    appName = getApplicationName();
    appleMenu = [[NSMenu alloc] initWithTitle:@""];

    /* Add menu items */
    title = [@"About " stringByAppendingString:appName];
    [appleMenu addItemWithTitle:title action:@selector(orderFrontStandardAboutPanel:) keyEquivalent:@""];

    [appleMenu addItem:[NSMenuItem separatorItem]];

    title = [@"Hide " stringByAppendingString:appName];
    [appleMenu addItemWithTitle:title action:@selector(hide:) keyEquivalent:@"h"];

    menuItem = (NSMenuItem *)[appleMenu addItemWithTitle:@"Hide Others" action:@selector(hideOtherApplications:) keyEquivalent:@"h"];
    [menuItem setKeyEquivalentModifierMask:(NSAlternateKeyMask|NSCommandKeyMask)];

    [appleMenu addItemWithTitle:@"Show All" action:@selector(unhideAllApplications:) keyEquivalent:@""];

    [appleMenu addItem:[NSMenuItem separatorItem]];

    title = [@"Quit " stringByAppendingString:appName];
    [appleMenu addItemWithTitle:title action:@selector(terminate:) keyEquivalent:@"q"];


    /* Put menu into the menubar */
    menuItem = [[NSMenuItem alloc] initWithTitle:@"" action:nil keyEquivalent:@""];
    [menuItem setSubmenu:appleMenu];
    [[NSApp mainMenu] addItem:menuItem];

    /* Tell the application object that this is now the application menu */
    [NSApp setAppleMenu:appleMenu];

    /* Finally give up our references to the objects */
    [appleMenu release];
    [menuItem release];
}

/* Create a window menu */
static void setupWindowMenu(void)
{
    NSMenu      *windowMenu;
    NSMenuItem  *windowMenuItem;
    NSMenuItem  *menuItem;

    windowMenu = [[NSMenu alloc] initWithTitle:@"Window"];

    /* "Minimize" item */
    menuItem = [[NSMenuItem alloc] initWithTitle:@"Minimize" action:@selector(performMiniaturize:) keyEquivalent:@"m"];
    [windowMenu addItem:menuItem];
    [menuItem release];

    /* Put menu into the menubar */
    windowMenuItem = [[NSMenuItem alloc] initWithTitle:@"Window" action:nil keyEquivalent:@""];
    [windowMenuItem setSubmenu:windowMenu];
    [[NSApp mainMenu] addItem:windowMenuItem];

    /* Tell the application object that this is now the window menu */
    [NSApp setWindowsMenu:windowMenu];

    /* Finally give up our references to the objects */
    [windowMenu release];
    [windowMenuItem release];
}

/* Replacement for NSApplicationMain */
static void CustomApplicationMain (int argc, char **argv)
{
#if (__GNUC__ > 3) || (__GNUC__ == 3 && __GNUC_MINOR__ >= 1)
    (void)argc;
    (void)argv;
#endif
    NSAutoreleasePool	*pool = [[NSAutoreleasePool alloc] init];
    SDLMain				*sdlMain;

    /* Ensure the application object is initialised */
    [SDLApplication sharedApplication];

    /* Set up the menubar */
    [NSApp setMainMenu:[[NSMenu alloc] init]];
    setApplicationMenu();
    setupWindowMenu();

    /* Create SDLMain and make it the app delegate */
    sdlMain = [[SDLMain alloc] init];
    [NSApp setDelegate:sdlMain];

    /* Start the main event loop */
    [NSApp run]; // SRB2CBTODO!: Confusing startup: 3 from the 2nd "main" call, we call a DUPLICATE "main" function elsewhere in the code

    [sdlMain release]; // SRB2CBTODO!: Confusing startup: 4 We "release" the program and have it check if it finished launching
    [pool release];
}

- (BOOL)application:(NSApplication *)theApplication openFile:(NSString *)filename
{
#if (__GNUC__ > 3) || (__GNUC__ == 3 && __GNUC_MINOR__ >= 1)
    (void)theApplication;
#endif
    addArgument("-iwad");
    addArgument([filename UTF8String]);
    return YES;
}

/* Called when the internal event loop has just started running */
- (void) applicationDidFinishLaunching: (NSNotification *) note
{
#if (__GNUC__ > 3) || (__GNUC__ == 3 && __GNUC_MINOR__ >= 1)
    (void)note;
#endif
    int status;

    /* Set the working directory to the .app's parent directory */
    [self setupWorkingDirectory:gFinderLaunch];

    if (!getenv("SRB2WADPATH")) // SRB2CBTODO: SRB2WADPATH?
        setenv("SRB2WADDIR", [[[NSBundle mainBundle] resourcePath] UTF8String], 1);

    /* Hand off to main application code */
    status = SDL_main (gArgc, gArgv); // SRB2CBTODO!: Confusing startup: 5 tell that our "status" is passed off to SDL main

    /* We're done, thank you for playing */
    exit(status);
}
@end


@implementation NSString (ReplaceSubString)

- (NSString *)stringByReplacingRange:(NSRange)aRange with:(NSString *)aString
{
    unsigned int bufferSize;
    unsigned int selfLen = [self length];
    unsigned int aStringLen = [aString length];
    unichar *buffer;
    NSRange localRange;
    NSString *result;

    bufferSize = selfLen + aStringLen - aRange.length;
    buffer = NSAllocateMemoryPages(bufferSize*sizeof(unichar));

    /* Get first part into buffer */
    localRange.location = 0;
    localRange.length = aRange.location;
    [self getCharacters:buffer range:localRange];

    /* Get middle part into buffer */
    localRange.location = 0;
    localRange.length = aStringLen;
    [aString getCharacters:(buffer+aRange.location) range:localRange];

    /* Get last part into buffer */
    localRange.location = aRange.location + aRange.length;
    localRange.length = selfLen - localRange.location;
    [self getCharacters:(buffer+aRange.location+aStringLen) range:localRange];

    /* Build output string */
    result = [NSString stringWithCharacters:buffer length:bufferSize];

    NSDeallocateMemoryPages(buffer, bufferSize);

    return result;
}

@end


// This is important, un-define any other "main" program loops
#ifdef main
#  undef main
#endif


/* Main entry point to executable - should *not* be SDL_main! */
int main(int argc, char **argv) // SRB2CBTODO!: Confusing startup: 1 This is the "starting point" of the program
{
    /* Copy the arguments into a global variable */

    /* This is passed if we are launched by double-clicking */
    if (argc >= 2 && strncmp (argv[1], "-psn", 4) == 0)
	{
        gArgc = 1;
        gFinderLaunch = YES;
    }
	else
	{
        gArgc = argc;
        gFinderLaunch = NO;
    }
    gArgv = argv;

    CustomApplicationMain (argc, argv); // SRB2CBTODO!: Confusing startup: 2 This starts the main loop for everything in the actual program

    return 0;
}
