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
/// \brief The hardware rendering interface for SRB2 to convert data for OpenGL to render

#include <math.h>

#include "../doomstat.h"

#ifdef HWRENDER
#include "hw_glob.h"
#include "hw_light.h"
#include "hw_drv.h"

#include "../i_video.h"
#include "../v_video.h"
#include "../p_local.h"
#include "../p_setup.h"
#include "../r_local.h"
#include "../r_bsp.h"
#include "../d_clisrv.h"
#include "../w_wad.h"
#include "../z_zone.h"
#include "../r_splats.h"
#include "../g_game.h"
#include "../st_stuff.h"
#include "../i_system.h" // I_OsPolling()
#include "../m_misc.h"

#include "r_opengl/r_opengl.h"

#include "hw_md2.h"

#ifdef ESLOPE
#include "../p_slopes.h"
#endif

#ifdef NEWCLIP
#include "hw_clip.h"
#endif

#define R_FAKEFLOORS
//#define SKYWALL
//#define SORTING // SRB2CBTODO: Sort sprites with stuff

boolean hw_doorclosed;

// ==========================================================================
//                                                                     PROTOS
// ==========================================================================

// SRB2CBTODO: Just place this in the right order!
static void HWR_AddSprites(sector_t *sec);
static void HWR_ProjectSprite(mobj_t *thing);
static void HWR_ProjectPrecipitationSprite(precipmobj_t *thing);

static void HWR_AddTransparentFloor(const sector_t *sector, lumpnum_t lumpnum, extrasubsector_t *xsub,	fixed_t fixedheight,
                             byte lightlevel, byte alpha, sector_t *FOFSector, FBITFIELD blendmode, extracolormap_t *planecolormap);
static void HWR_AddPeggedFloor(sector_t *sector, lumpnum_t lumpnum, extrasubsector_t *xsub,
							   fixed_t fixedheight, byte lightlevel, byte alpha, sector_t *FOFSector, extracolormap_t *planecolormap);
static void HWR_FoggingOn(void);

static unsigned int atohex(const char *s); // SRB2CBTODO: Use for timeofday!

static void CV_filtermode_ONChange(void);
static void CV_anisotropic_ONChange(void);
static void CV_FogDensity_ONChange(void);
// ==========================================================================
//                                          3D ENGINE COMMANDS & CONSOLE VARS
// ==========================================================================

static CV_PossibleValue_t grfov_cons_t[] = {{30, "MIN"}, {130, "MAX"}, {0, NULL}};
static CV_PossibleValue_t grfiltermode_cons_t[]= {{HWD_SET_TEXTUREFILTER_POINTSAMPLED, "Pixellated"},
	{HWD_SET_TEXTUREFILTER_MIXED1, "Smoothed"},
{0, NULL}};
CV_PossibleValue_t granisotropicmode_cons_t[] = {{1, "MIN"}, {16, "MAX"}, {0, NULL}};

// For limiting OpenGL features if on minigl
boolean minigl = false;

// For OpenGL testing
consvar_t cv_grtest = {"grtest", "0", 0, CV_Unsigned, NULL, 0, NULL, NULL, 0, 0, NULL};

consvar_t cv_motionblur = {"motionblur", "On", 0, CV_OnOff, NULL, 0, NULL, NULL, 0, 0, NULL};

float grfovadjust = 0.0f;
// For OpenGL screen cross fades
static float HWRWipeCounter = 1.0f;

// The player should have at least a fog density of 16
// otherwise colormaps don't look as cool ;)
static CV_PossibleValue_t CV_Fogdensity[] = {{16, "MIN"}, {255, "MAX"}, {0, NULL}}; // SRB2CB

consvar_t cv_grfov = {"gr_fov", "90", CV_FLOAT, grfov_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_grfogdensity = {"gr_fogdensity", "64", CV_CALL|CV_NOINIT|CV_SAVE, CV_Fogdensity,
                             CV_FogDensity_ONChange, 0, NULL, NULL, 0, 0, NULL};

// OpenGL's default filter can be saved
consvar_t cv_grfiltermode = {"gr_filtermode", "Smoothed", CV_CALL|CV_SAVE, grfiltermode_cons_t, // default texture filter
                             CV_filtermode_ONChange, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_granisotropicmode = {"gr_anisotropicmode", "16", CV_CALL|CV_SAVE, granisotropicmode_cons_t,
                             CV_anisotropic_ONChange, 0, NULL, NULL, 0, 0, NULL};

// Console variables for things in development
consvar_t cv_grmd2 = {"gr_md2", "Off", CV_SAVE, CV_OnOff, NULL, 0, NULL, NULL, 0, 0, NULL};

static void CV_FogDensity_ONChange(void)
{
	if (rendermode == render_opengl) // Special check so we can save the fog value
		GL_SetSpecialState(HWD_SET_FOG_DENSITY, cv_grfogdensity.value);
}

static void CV_filtermode_ONChange(void)
{
	if (minigl)
		GL_SetSpecialState(HWD_SET_TEXTUREFILTERMODE, HWD_SET_TEXTUREFILTER_MIXED1);
	else
		GL_SetSpecialState(HWD_SET_TEXTUREFILTERMODE, cv_grfiltermode.value);
}

static void CV_anisotropic_ONChange(void)
{
	GL_SetSpecialState(HWD_SET_TEXTUREANISOTROPICMODE, cv_granisotropicmode.value);
}

/*
 * lookuptable for lightvalues
 * calculated as follow:
 * floatlight = (1.0-exp((light^3)*gamma)) / (1.0-exp(1.0*gamma));
 * gamma=-0,2;-2,0;-4,0;-6,0;-8,0
 * light = 0,0 .. 1,0
 */
static const float lighttable[5][256] =
{
  {
    0.00000f,0.00000f,0.00000f,0.00000f,0.00000f,0.00001f,0.00001f,0.00002f,0.00003f,0.00004f,
    0.00006f,0.00008f,0.00010f,0.00013f,0.00017f,0.00020f,0.00025f,0.00030f,0.00035f,0.00041f,
    0.00048f,0.00056f,0.00064f,0.00073f,0.00083f,0.00094f,0.00106f,0.00119f,0.00132f,0.00147f,
    0.00163f,0.00180f,0.00198f,0.00217f,0.00237f,0.00259f,0.00281f,0.00305f,0.00331f,0.00358f,
    0.00386f,0.00416f,0.00447f,0.00479f,0.00514f,0.00550f,0.00587f,0.00626f,0.00667f,0.00710f,
    0.00754f,0.00800f,0.00848f,0.00898f,0.00950f,0.01003f,0.01059f,0.01117f,0.01177f,0.01239f,
    0.01303f,0.01369f,0.01437f,0.01508f,0.01581f,0.01656f,0.01734f,0.01814f,0.01896f,0.01981f,
    0.02069f,0.02159f,0.02251f,0.02346f,0.02444f,0.02544f,0.02647f,0.02753f,0.02862f,0.02973f,
    0.03088f,0.03205f,0.03325f,0.03448f,0.03575f,0.03704f,0.03836f,0.03971f,0.04110f,0.04252f,
    0.04396f,0.04545f,0.04696f,0.04851f,0.05009f,0.05171f,0.05336f,0.05504f,0.05676f,0.05852f,
    0.06031f,0.06214f,0.06400f,0.06590f,0.06784f,0.06981f,0.07183f,0.07388f,0.07597f,0.07810f,
    0.08027f,0.08248f,0.08473f,0.08702f,0.08935f,0.09172f,0.09414f,0.09659f,0.09909f,0.10163f,
    0.10421f,0.10684f,0.10951f,0.11223f,0.11499f,0.11779f,0.12064f,0.12354f,0.12648f,0.12946f,
    0.13250f,0.13558f,0.13871f,0.14188f,0.14511f,0.14838f,0.15170f,0.15507f,0.15850f,0.16197f,
    0.16549f,0.16906f,0.17268f,0.17635f,0.18008f,0.18386f,0.18769f,0.19157f,0.19551f,0.19950f,
    0.20354f,0.20764f,0.21179f,0.21600f,0.22026f,0.22458f,0.22896f,0.23339f,0.23788f,0.24242f,
    0.24702f,0.25168f,0.25640f,0.26118f,0.26602f,0.27091f,0.27587f,0.28089f,0.28596f,0.29110f,
    0.29630f,0.30156f,0.30688f,0.31226f,0.31771f,0.32322f,0.32879f,0.33443f,0.34013f,0.34589f,
    0.35172f,0.35761f,0.36357f,0.36960f,0.37569f,0.38185f,0.38808f,0.39437f,0.40073f,0.40716f,
    0.41366f,0.42022f,0.42686f,0.43356f,0.44034f,0.44718f,0.45410f,0.46108f,0.46814f,0.47527f,
    0.48247f,0.48974f,0.49709f,0.50451f,0.51200f,0.51957f,0.52721f,0.53492f,0.54271f,0.55058f,
    0.55852f,0.56654f,0.57463f,0.58280f,0.59105f,0.59937f,0.60777f,0.61625f,0.62481f,0.63345f,
    0.64217f,0.65096f,0.65984f,0.66880f,0.67783f,0.68695f,0.69615f,0.70544f,0.71480f,0.72425f,
    0.73378f,0.74339f,0.75308f,0.76286f,0.77273f,0.78268f,0.79271f,0.80283f,0.81304f,0.82333f,
    0.83371f,0.84417f,0.85472f,0.86536f,0.87609f,0.88691f,0.89781f,0.90880f,0.91989f,0.93106f,
    0.94232f,0.95368f,0.96512f,0.97665f,0.98828f,1.00000f
  },
  {
    0.00000f,0.00000f,0.00000f,0.00000f,0.00001f,0.00002f,0.00003f,0.00005f,0.00007f,0.00010f,
    0.00014f,0.00019f,0.00024f,0.00031f,0.00038f,0.00047f,0.00057f,0.00069f,0.00081f,0.00096f,
    0.00112f,0.00129f,0.00148f,0.00170f,0.00193f,0.00218f,0.00245f,0.00274f,0.00306f,0.00340f,
    0.00376f,0.00415f,0.00456f,0.00500f,0.00547f,0.00597f,0.00649f,0.00704f,0.00763f,0.00825f,
    0.00889f,0.00957f,0.01029f,0.01104f,0.01182f,0.01264f,0.01350f,0.01439f,0.01532f,0.01630f,
    0.01731f,0.01836f,0.01945f,0.02058f,0.02176f,0.02298f,0.02424f,0.02555f,0.02690f,0.02830f,
    0.02974f,0.03123f,0.03277f,0.03436f,0.03600f,0.03768f,0.03942f,0.04120f,0.04304f,0.04493f,
    0.04687f,0.04886f,0.05091f,0.05301f,0.05517f,0.05738f,0.05964f,0.06196f,0.06434f,0.06677f,
    0.06926f,0.07181f,0.07441f,0.07707f,0.07979f,0.08257f,0.08541f,0.08831f,0.09126f,0.09428f,
    0.09735f,0.10048f,0.10368f,0.10693f,0.11025f,0.11362f,0.11706f,0.12056f,0.12411f,0.12773f,
    0.13141f,0.13515f,0.13895f,0.14281f,0.14673f,0.15072f,0.15476f,0.15886f,0.16303f,0.16725f,
    0.17153f,0.17587f,0.18028f,0.18474f,0.18926f,0.19383f,0.19847f,0.20316f,0.20791f,0.21272f,
    0.21759f,0.22251f,0.22748f,0.23251f,0.23760f,0.24274f,0.24793f,0.25318f,0.25848f,0.26383f,
    0.26923f,0.27468f,0.28018f,0.28573f,0.29133f,0.29697f,0.30266f,0.30840f,0.31418f,0.32001f,
    0.32588f,0.33179f,0.33774f,0.34374f,0.34977f,0.35585f,0.36196f,0.36810f,0.37428f,0.38050f,
    0.38675f,0.39304f,0.39935f,0.40570f,0.41207f,0.41847f,0.42490f,0.43136f,0.43784f,0.44434f,
    0.45087f,0.45741f,0.46398f,0.47057f,0.47717f,0.48379f,0.49042f,0.49707f,0.50373f,0.51041f,
    0.51709f,0.52378f,0.53048f,0.53718f,0.54389f,0.55061f,0.55732f,0.56404f,0.57075f,0.57747f,
    0.58418f,0.59089f,0.59759f,0.60429f,0.61097f,0.61765f,0.62432f,0.63098f,0.63762f,0.64425f,
    0.65086f,0.65746f,0.66404f,0.67060f,0.67714f,0.68365f,0.69015f,0.69662f,0.70307f,0.70948f,
    0.71588f,0.72224f,0.72857f,0.73488f,0.74115f,0.74739f,0.75359f,0.75976f,0.76589f,0.77199f,
    0.77805f,0.78407f,0.79005f,0.79599f,0.80189f,0.80774f,0.81355f,0.81932f,0.82504f,0.83072f,
    0.83635f,0.84194f,0.84747f,0.85296f,0.85840f,0.86378f,0.86912f,0.87441f,0.87964f,0.88482f,
    0.88995f,0.89503f,0.90005f,0.90502f,0.90993f,0.91479f,0.91959f,0.92434f,0.92903f,0.93366f,
    0.93824f,0.94276f,0.94723f,0.95163f,0.95598f,0.96027f,0.96451f,0.96868f,0.97280f,0.97686f,
    0.98086f,0.98481f,0.98869f,0.99252f,0.99629f,1.00000f
  },
  {
    0.00000f,0.00000f,0.00000f,0.00001f,0.00002f,0.00003f,0.00005f,0.00008f,0.00013f,0.00018f,
    0.00025f,0.00033f,0.00042f,0.00054f,0.00067f,0.00083f,0.00101f,0.00121f,0.00143f,0.00168f,
    0.00196f,0.00227f,0.00261f,0.00299f,0.00339f,0.00383f,0.00431f,0.00483f,0.00538f,0.00598f,
    0.00661f,0.00729f,0.00802f,0.00879f,0.00961f,0.01048f,0.01140f,0.01237f,0.01340f,0.01447f,
    0.01561f,0.01680f,0.01804f,0.01935f,0.02072f,0.02215f,0.02364f,0.02520f,0.02682f,0.02850f,
    0.03026f,0.03208f,0.03397f,0.03594f,0.03797f,0.04007f,0.04225f,0.04451f,0.04684f,0.04924f,
    0.05172f,0.05428f,0.05691f,0.05963f,0.06242f,0.06530f,0.06825f,0.07129f,0.07441f,0.07761f,
    0.08089f,0.08426f,0.08771f,0.09125f,0.09487f,0.09857f,0.10236f,0.10623f,0.11019f,0.11423f,
    0.11836f,0.12257f,0.12687f,0.13125f,0.13571f,0.14027f,0.14490f,0.14962f,0.15442f,0.15931f,
    0.16427f,0.16932f,0.17445f,0.17966f,0.18496f,0.19033f,0.19578f,0.20130f,0.20691f,0.21259f,
    0.21834f,0.22417f,0.23007f,0.23605f,0.24209f,0.24820f,0.25438f,0.26063f,0.26694f,0.27332f,
    0.27976f,0.28626f,0.29282f,0.29944f,0.30611f,0.31284f,0.31962f,0.32646f,0.33334f,0.34027f,
    0.34724f,0.35426f,0.36132f,0.36842f,0.37556f,0.38273f,0.38994f,0.39718f,0.40445f,0.41174f,
    0.41907f,0.42641f,0.43378f,0.44116f,0.44856f,0.45598f,0.46340f,0.47084f,0.47828f,0.48573f,
    0.49319f,0.50064f,0.50809f,0.51554f,0.52298f,0.53042f,0.53784f,0.54525f,0.55265f,0.56002f,
    0.56738f,0.57472f,0.58203f,0.58932f,0.59658f,0.60381f,0.61101f,0.61817f,0.62529f,0.63238f,
    0.63943f,0.64643f,0.65339f,0.66031f,0.66717f,0.67399f,0.68075f,0.68746f,0.69412f,0.70072f,
    0.70726f,0.71375f,0.72017f,0.72653f,0.73282f,0.73905f,0.74522f,0.75131f,0.75734f,0.76330f,
    0.76918f,0.77500f,0.78074f,0.78640f,0.79199f,0.79751f,0.80295f,0.80831f,0.81359f,0.81880f,
    0.82393f,0.82898f,0.83394f,0.83883f,0.84364f,0.84836f,0.85301f,0.85758f,0.86206f,0.86646f,
    0.87078f,0.87502f,0.87918f,0.88326f,0.88726f,0.89118f,0.89501f,0.89877f,0.90245f,0.90605f,
    0.90957f,0.91301f,0.91638f,0.91966f,0.92288f,0.92601f,0.92908f,0.93206f,0.93498f,0.93782f,
    0.94059f,0.94329f,0.94592f,0.94848f,0.95097f,0.95339f,0.95575f,0.95804f,0.96027f,0.96244f,
    0.96454f,0.96658f,0.96856f,0.97049f,0.97235f,0.97416f,0.97591f,0.97760f,0.97924f,0.98083f,
    0.98237f,0.98386f,0.98530f,0.98669f,0.98803f,0.98933f,0.99058f,0.99179f,0.99295f,0.99408f,
    0.99516f,0.99620f,0.99721f,0.99817f,0.99910f,1.00000f
  },
  {
    0.00000f,0.00000f,0.00000f,0.00001f,0.00002f,0.00005f,0.00008f,0.00012f,0.00019f,0.00026f,
    0.00036f,0.00048f,0.00063f,0.00080f,0.00099f,0.00122f,0.00148f,0.00178f,0.00211f,0.00249f,
    0.00290f,0.00335f,0.00386f,0.00440f,0.00500f,0.00565f,0.00636f,0.00711f,0.00793f,0.00881f,
    0.00975f,0.01075f,0.01182f,0.01295f,0.01416f,0.01543f,0.01678f,0.01821f,0.01971f,0.02129f,
    0.02295f,0.02469f,0.02652f,0.02843f,0.03043f,0.03252f,0.03469f,0.03696f,0.03933f,0.04178f,
    0.04433f,0.04698f,0.04973f,0.05258f,0.05552f,0.05857f,0.06172f,0.06498f,0.06834f,0.07180f,
    0.07537f,0.07905f,0.08283f,0.08672f,0.09072f,0.09483f,0.09905f,0.10337f,0.10781f,0.11236f,
    0.11701f,0.12178f,0.12665f,0.13163f,0.13673f,0.14193f,0.14724f,0.15265f,0.15817f,0.16380f,
    0.16954f,0.17538f,0.18132f,0.18737f,0.19351f,0.19976f,0.20610f,0.21255f,0.21908f,0.22572f,
    0.23244f,0.23926f,0.24616f,0.25316f,0.26023f,0.26739f,0.27464f,0.28196f,0.28935f,0.29683f,
    0.30437f,0.31198f,0.31966f,0.32740f,0.33521f,0.34307f,0.35099f,0.35896f,0.36699f,0.37506f,
    0.38317f,0.39133f,0.39952f,0.40775f,0.41601f,0.42429f,0.43261f,0.44094f,0.44929f,0.45766f,
    0.46604f,0.47443f,0.48283f,0.49122f,0.49962f,0.50801f,0.51639f,0.52476f,0.53312f,0.54146f,
    0.54978f,0.55807f,0.56633f,0.57457f,0.58277f,0.59093f,0.59905f,0.60713f,0.61516f,0.62314f,
    0.63107f,0.63895f,0.64676f,0.65452f,0.66221f,0.66984f,0.67739f,0.68488f,0.69229f,0.69963f,
    0.70689f,0.71407f,0.72117f,0.72818f,0.73511f,0.74195f,0.74870f,0.75536f,0.76192f,0.76839f,
    0.77477f,0.78105f,0.78723f,0.79331f,0.79930f,0.80518f,0.81096f,0.81664f,0.82221f,0.82768f,
    0.83305f,0.83832f,0.84347f,0.84853f,0.85348f,0.85832f,0.86306f,0.86770f,0.87223f,0.87666f,
    0.88098f,0.88521f,0.88933f,0.89334f,0.89726f,0.90108f,0.90480f,0.90842f,0.91194f,0.91537f,
    0.91870f,0.92193f,0.92508f,0.92813f,0.93109f,0.93396f,0.93675f,0.93945f,0.94206f,0.94459f,
    0.94704f,0.94941f,0.95169f,0.95391f,0.95604f,0.95810f,0.96009f,0.96201f,0.96386f,0.96564f,
    0.96735f,0.96900f,0.97059f,0.97212f,0.97358f,0.97499f,0.97634f,0.97764f,0.97888f,0.98007f,
    0.98122f,0.98231f,0.98336f,0.98436f,0.98531f,0.98623f,0.98710f,0.98793f,0.98873f,0.98949f,
    0.99021f,0.99090f,0.99155f,0.99218f,0.99277f,0.99333f,0.99387f,0.99437f,0.99486f,0.99531f,
    0.99575f,0.99616f,0.99654f,0.99691f,0.99726f,0.99759f,0.99790f,0.99819f,0.99847f,0.99873f,
    0.99897f,0.99920f,0.99942f,0.99963f,0.99982f,1.00000f
  },
  {
    0.00000f,0.00000f,0.00000f,0.00001f,0.00003f,0.00006f,0.00010f,0.00017f,0.00025f,0.00035f,
    0.00048f,0.00064f,0.00083f,0.00106f,0.00132f,0.00163f,0.00197f,0.00237f,0.00281f,0.00330f,
    0.00385f,0.00446f,0.00513f,0.00585f,0.00665f,0.00751f,0.00845f,0.00945f,0.01054f,0.01170f,
    0.01295f,0.01428f,0.01569f,0.01719f,0.01879f,0.02048f,0.02227f,0.02415f,0.02614f,0.02822f,
    0.03042f,0.03272f,0.03513f,0.03765f,0.04028f,0.04303f,0.04589f,0.04887f,0.05198f,0.05520f,
    0.05855f,0.06202f,0.06561f,0.06933f,0.07318f,0.07716f,0.08127f,0.08550f,0.08987f,0.09437f,
    0.09900f,0.10376f,0.10866f,0.11369f,0.11884f,0.12414f,0.12956f,0.13512f,0.14080f,0.14662f,
    0.15257f,0.15865f,0.16485f,0.17118f,0.17764f,0.18423f,0.19093f,0.19776f,0.20471f,0.21177f,
    0.21895f,0.22625f,0.23365f,0.24117f,0.24879f,0.25652f,0.26435f,0.27228f,0.28030f,0.28842f,
    0.29662f,0.30492f,0.31329f,0.32175f,0.33028f,0.33889f,0.34756f,0.35630f,0.36510f,0.37396f,
    0.38287f,0.39183f,0.40084f,0.40989f,0.41897f,0.42809f,0.43723f,0.44640f,0.45559f,0.46479f,
    0.47401f,0.48323f,0.49245f,0.50167f,0.51088f,0.52008f,0.52927f,0.53843f,0.54757f,0.55668f,
    0.56575f,0.57479f,0.58379f,0.59274f,0.60164f,0.61048f,0.61927f,0.62799f,0.63665f,0.64524f,
    0.65376f,0.66220f,0.67056f,0.67883f,0.68702f,0.69511f,0.70312f,0.71103f,0.71884f,0.72655f,
    0.73415f,0.74165f,0.74904f,0.75632f,0.76348f,0.77053f,0.77747f,0.78428f,0.79098f,0.79756f,
    0.80401f,0.81035f,0.81655f,0.82264f,0.82859f,0.83443f,0.84013f,0.84571f,0.85117f,0.85649f,
    0.86169f,0.86677f,0.87172f,0.87654f,0.88124f,0.88581f,0.89026f,0.89459f,0.89880f,0.90289f,
    0.90686f,0.91071f,0.91445f,0.91807f,0.92157f,0.92497f,0.92826f,0.93143f,0.93450f,0.93747f,
    0.94034f,0.94310f,0.94577f,0.94833f,0.95081f,0.95319f,0.95548f,0.95768f,0.95980f,0.96183f,
    0.96378f,0.96565f,0.96744f,0.96916f,0.97081f,0.97238f,0.97388f,0.97532f,0.97669f,0.97801f,
    0.97926f,0.98045f,0.98158f,0.98266f,0.98369f,0.98467f,0.98560f,0.98648f,0.98732f,0.98811f,
    0.98886f,0.98958f,0.99025f,0.99089f,0.99149f,0.99206f,0.99260f,0.99311f,0.99359f,0.99404f,
    0.99446f,0.99486f,0.99523f,0.99559f,0.99592f,0.99623f,0.99652f,0.99679f,0.99705f,0.99729f,
    0.99751f,0.99772f,0.99792f,0.99810f,0.99827f,0.99843f,0.99857f,0.99871f,0.99884f,0.99896f,
    0.99907f,0.99917f,0.99926f,0.99935f,0.99943f,0.99951f,0.99958f,0.99964f,0.99970f,0.99975f,
    0.99980f,0.99985f,0.99989f,0.99993f,0.99997f,1.00000f
  }
};

#define gld_CalcLightLevel(lightlevel) (lighttable[1][max(min((lightlevel),255),0)])

// ==========================================================================
//                                                               VIEW GLOBALS
// ==========================================================================
// Fineangles in the SCREENWIDTH wide window.
#define FIELDOFVIEW 2048
#define ABS(x) ((x) < 0 ? -(x) : (x)) // Absolute value for non int's

static angle_t gr_clipangle;
static angle_t gr_doubleclipangle;

// The viewangletox[viewangle + FINEANGLES/4] lookup
// maps the visible view angles to screen X coordinates,
// flattening the arc to a flat projection plane.
// There will be many angles mapped to the same X.
static int gr_viewangletox[FINEANGLES/2];

// The xtoviewangleangle[] table maps a screen pixel
// to the lowest viewangle that maps back to x ranges
// from clipangle to -clipangle.
static angle_t gr_xtoviewangle[MAXVIDWIDTH+1];

// ==========================================================================
//                                                                    GLOBALS
// ==========================================================================

// base values set at SetViewSize
static float gr_basecentery;

float gr_baseviewwindowy, gr_basewindowcentery;
float gr_viewwidth, gr_viewheight; // viewport clipping boundaries (screen coords)
float gr_viewwindowx;

static float gr_centerx, gr_centery;
static float gr_viewwindowy; // top left corner of view window
static float gr_windowcenterx; // center of view window, for projection
static float gr_windowcentery;

static float gr_pspritexscale, gr_pspriteyscale;

static seg_t *gr_curline;
static side_t *gr_sidedef;
static line_t *gr_linedef;
static sector_t *gr_frontsector;
static sector_t *gr_backsector;

static void HWR_AddLine(seg_t * line);

// --------------------------------------------------------------------------
//                                              STUFF FOR THE PROJECTION CODE
// --------------------------------------------------------------------------

FTransform atransform;
// Duplicates of the main code, set after R_SetupFrame() passed them into sharedstruct,
// copied here for local use
float gr_viewx, gr_viewy, gr_viewz;
float gr_viewsin, gr_viewcos;

float gr_viewludsin, gr_viewludcos;
float gr_fovlud, gr_fov;

// ==========================================================================
//                                    LIGHT stuffs
// ==========================================================================

// This actually makes things darker
// Just returning l makes things brighter
// SRB2CBTODO: Interpoliate darkness levels like software mode?
FUNCMATH static byte LightLevelToLum(int l)
{
	return (byte)(255*gld_CalcLightLevel(l));
}

#define NORMALFOG 0x00000000 // When no colormap is used, then use NORMALFOG
#define CALCFOGALPHA(x,y) (unsigned int)(((float)(x)/((0x19 - (y))/12.0f+1.0f)))
// SRB2TODO: Fog values shall ONLY be set here,
// use special function to make certain colormapped stuff have more color in their colormap :D
// Sets fog values PER colormapped area, so colormaps can be far more apparent!
static void HWR_FoggedLight(ULONG color)
{
	// Super sweet and awesome software-like fog that glows in the dark!
	RGBA_t rgbcolor; // Convert the value from ULONG to RGA_t
	rgbcolor.rgba = color;

	ULONG fogvalue; // convert the color to FOG from RGBA to RGB
	fogvalue = (rgbcolor.s.red*0x10000)+(rgbcolor.s.green*0x100)+rgbcolor.s.blue;

	GL_SetSpecialState(HWD_SET_FOG_COLOR, fogvalue);
	GL_SetSpecialState(HWD_SET_FOG_DENSITY, cv_grfogdensity.value*1.2);
}

// Sets colormap for the rest of the game
// fogcolor - gives per-polygon fogging!
static ULONG HWR_Lighting(int light, ULONG color, boolean lighter, boolean fogcolor)
{
	// RealColor is the source color, SurfColor is the final color returned
	RGBA_t RealColor, SurfColor;
	RealColor.rgba = color;

	// When there's no color (completely black), use a different, more simple modulation
	if (RealColor.rgba == NORMALFOG) // Default fog
	{
		int alpha; // Amount to modulate the darkness of the color
		alpha = (26 - RealColor.s.alpha)*light;

		SurfColor.s.red =
		(byte)((alpha + RealColor.s.alpha*RealColor.s.red)/26);
		SurfColor.s.blue =
		(byte)((alpha + RealColor.s.alpha*RealColor.s.blue)/26);
		SurfColor.s.green =
		(byte)((alpha + RealColor.s.alpha*RealColor.s.green)/26);

    if (cv_grfog.value)
    {

		GL_SetSpecialState(HWD_SET_FOG_MODE, 1); // SRB2CBTODO: Fade in and out lighting like software mode?
		GL_SetSpecialState(HWD_SET_FOG_DENSITY, cv_grfogdensity.value/2.0f); // Need fog for light distancing when light is less than 192
		GL_SetSpecialState(HWD_SET_FOG_COLOR, atohex(cv_grfogcolor.string)); // Default fog color SRB2CBTODO: Time of day + SEASONS!
		//HWR_ProcessFog(); // Ridiculously cool post fog effect when looking in colormapped FOFs
    }
    else
        GL_SetSpecialState(HWD_SET_FOG_MODE, 0);

	}
	else
	{
		// Modulate the colors by alpha
		RealColor.s.red = (RealColor.s.red)     / ((25 - RealColor.s.alpha) / 12.7f + 1.0f);
		RealColor.s.green = (RealColor.s.green) / ((25 - RealColor.s.alpha) / 12.7f + 1.0f);
		RealColor.s.blue = (RealColor.s.blue)   / ((25 - RealColor.s.alpha) / 12.7f + 1.0f);


		if (lighter) // This is needed for smoother light transistions
		{
			// For colormapped casted things, less intense than a direct copy
			// SRB2CBTODO: Make it so dark colormaps aren't too dark or too light
			if (cv_grtest.value == 15)
			{
				SurfColor.s.red = (RealColor.s.red) / ((255 - light) / 127.5f + 1.0f);
				SurfColor.s.green = (RealColor.s.green) / ((255 - light) / 127.5f + 1.0f);
				SurfColor.s.blue = (RealColor.s.blue) / ((255 - light) / 127.5f + 1.0f);
			}
			else
			{
				// Lighter color copy
				SurfColor.s.red = (255 - (RealColor.s.green + RealColor.s.blue)/2.0f) / ((255 - light) / 127.5f + 1.0f);
				SurfColor.s.green = (255 - (RealColor.s.red + RealColor.s.blue)/2.0f) / ((255 - light) / 127.5f + 1.0f);
				SurfColor.s.blue = (255 - (RealColor.s.red + RealColor.s.green)/2.0f) / ((255 - light) / 127.5f + 1.0f);
			}
		}
		else
		{
			// This is a very direct colormap copy, light level is less aparent here
			SurfColor.s.red = (RealColor.s.red) / ((25 - light) / 12.7f + 1.0f);
			SurfColor.s.green = (RealColor.s.green) / ((25 - light) / 12.7f + 1.0f);
			SurfColor.s.blue = (RealColor.s.blue) / ((25 - light) / 12.7f + 1.0f);
		}

		if (!cv_grfog.value)
		{
			GL_SetSpecialState(HWD_SET_FOG_DENSITY, 0);
			GL_SetSpecialState(HWD_SET_FOG_COLOR, NORMALFOG);
		}
		else if (fogcolor)
		{
			HWR_FoggingOn();
			HWR_FoggedLight(color);
		}
	}

	// The alpha of the RGBA returns as fully opaque by default
	// A polygon's alpha must be set again after this function if it is not opaque
	SurfColor.s.alpha = 255;

	return SurfColor.rgba;
}

// ==========================================================================
//                                   FLOOR/CEILING GENERATION FROM SUBSECTORS
// ==========================================================================

// Maximum number of verts around a convex floor/ceiling polygon
static FOutVector planeVerts[MAXPLANEVERTICES];


// -----------------+
// HWR_RenderPlane  : Render a floor or ceiling convex polygon
//
// NOTE: Polygons rendered here ARE NOT the same as regular sectors, these planes
// have been split into pieces for the BSP
// -----------------+
static void HWR_RenderPlane(const sector_t *sector, extrasubsector_t *xsub, fixed_t fixedheight, // SRB2CBTODO: Use less memory by using HWR_Rendersloped plane method
							FBITFIELD blendmode, byte lightlevel, lumpnum_t lumpnum, sector_t *FOFsector, byte alpha, extracolormap_t *planecolormap)
{
	polyvertex_t *  pv;
	float           height; // constant y for all points on the convex flat polygon
	FOutVector      *v3d;
	size_t          nrPlaneVerts = 0;   // Vertexes in the plane
	size_t          i;
	float           flatxref, flatyref;
	float fflatsize;
	int flatflag;
	long planescale = 1000;

	FSurfaceInfo    Surf;

	// No convex polygons were generated for this subsector, or this plane has no vertexes generated
	if (!xsub->planepoly)
		return;

	height = FIXED_TO_FLOAT(fixedheight); // The normal height of this plane

	// For planes using a pre-created vertex list,
	// this is read from to get plane polygons, it is not modified
	pv = xsub->planepoly->pts;

	// Get the ammount of plane vertexes for this plane,
	// a sector's vertexcount is equal to xsub->planepoly->numpts
	nrPlaneVerts = xsub->planepoly->numpts;

	// This plane can't even be rendered if it's not a polygon
	if (nrPlaneVerts < 3)
	{
		CONS_Printf("HWR_RenderPlane: A plane does not have 3 sides\n");
		return;
	}

	if (nrPlaneVerts > MAXPLANEVERTICES)
	{
		CONS_Printf("HWR_RenderPlane: A map polygon with %lu vertexes exceeds the max value of %lu vertices!\n",
					(ULONG)nrPlaneVerts, (ULONG)MAXPLANEVERTICES);
		return;
	}

	// SRB2CBTODO: Horizon support here?
	if (blendmode & PF_NoTexture)
	{
		// Transform
		v3d = planeVerts;
		for (i = 0; i < nrPlaneVerts; i++, v3d++, pv++)
		{
			// No texture, make sow and tow 0 so there's no rendering issues
			v3d->sow = 0;
			v3d->tow = 0;
			v3d->x = pv->x;
			v3d->y = height;
			v3d->z = pv->y;
		}
	}
	else
	{
		// Get the size of the texture this flat is using for tow and sow
        // When loading from a data file, these textures are always saved in power of two

        // NEWCB: Ok, now we have PNG support!...This complicates this code a bit

		if (W_LumpIsPng(lumpnum))
		{
		// Get the resolution of a flat texture,
		// OPTIMIZATION: don't constantly read the PNG, just get the data from the mipmap which is stored in memory!
			// Example, if resolution(the textures width or the height) = 512, flat flag = 511, NOTE: This expects floors to be power of 2 for now
			int rez = HWR_GetFlatRez(lumpnum);
			fflatsize = rez;
			flatflag = rez-1;
		}
        else
        {


		size_t len;
		len = W_LumpLength(lumpnum);

		switch (len)
		{
			case 4194304: // 2048x2048 lump
				fflatsize = 2048.0f;
				flatflag = 2047;
				break;
			case 1048576: // 1024x1024 lump
				fflatsize = 1024.0f;
				flatflag = 1023;
				break;
			case 262144:// 512x512 lump
				fflatsize = 512.0f;
				flatflag = 511;
				break;
			case 65536: // 256x256 lump
				fflatsize = 256.0f;
				flatflag = 255;
				break;
			case 16384: // 128x128 lump
				fflatsize = 128.0f;
				flatflag = 127;
				break;
			case 1024: // 32x32 lump
				fflatsize = 32.0f;
				flatflag = 31;
				break;
			default: // 64x64 lump
				fflatsize = 64.0f;
				flatflag = 63;
				break;
		}
        }

		// Reference point for the flat texture coordinates for each vertex that make up the polygon
		// Use the first vertex as a reference point
		flatxref = (float)(((fixed_t)pv->x & (~flatflag)) / fflatsize);
		flatyref = (float)(((fixed_t)pv->y & (~flatflag)) / fflatsize);

		// Transform
		v3d = planeVerts;

		for (i = 0; i < nrPlaneVerts; i++, v3d++, pv++)
		{
			// Scroll the texture of a floor/ceiling
			float scrollx = 0.0f, scrolly = 0.0f;
			// Setup the texture, scale, and rotate coords
			if (FOFsector != NULL)
			{
				if (fixedheight == FOFsector->floorheight) // it's a floor
				{
					scrollx = FIXED_TO_FLOAT(FOFsector->floor_xoffs)/fflatsize;
					scrolly = FIXED_TO_FLOAT(FOFsector->floor_yoffs)/fflatsize;

					if (FOFsector->floor_scale != 0)
						planescale = FOFsector->floor_scale;
				}
				else // it's a ceiling
				{
					scrollx = FIXED_TO_FLOAT(FOFsector->ceiling_xoffs)/fflatsize;
					scrolly = FIXED_TO_FLOAT(FOFsector->ceiling_yoffs)/fflatsize;

					if (FOFsector->ceiling_scale != 0)
						planescale = FOFsector->ceiling_scale;
				}
			}
			else if (gr_frontsector)
			{
				if (fixedheight < viewz) // it's a floor
				{
					scrollx = FIXED_TO_FLOAT(gr_frontsector->floor_xoffs)/fflatsize;
					scrolly = FIXED_TO_FLOAT(gr_frontsector->floor_yoffs)/fflatsize;

					if (gr_frontsector->floor_scale != 0)
						planescale = gr_frontsector->floor_scale;
				}
				else // it's a ceiling
				{
					scrollx = FIXED_TO_FLOAT(gr_frontsector->ceiling_xoffs)/fflatsize;
					scrolly = FIXED_TO_FLOAT(gr_frontsector->ceiling_yoffs)/fflatsize;

					if (gr_frontsector->ceiling_scale != 0)
						planescale = gr_frontsector->ceiling_scale;
				}
			}

			// Assign the values to a vertex
			v3d->sow = (float)((pv->x / fflatsize) - flatxref + scrollx);
			v3d->tow = (float)(flatyref - (pv->y / fflatsize) + scrolly);
			// NOTE: These values are switched like this because the game engine uses z/y differently than OpenGL
			// the game's z value = OpenGL's y value
			v3d->x = pv->x;
			v3d->z = pv->y;

			v3d->y = height; //If this sector isn't sloped, just continue on as normal

#ifdef ESLOPE
			// It's funny how sometimes there's a really simple solution to a problem you've just been stuck on for months
			// Originally I tried making a huge block of for loops to check if a vertex's coords matched v3d's
			// But this was slow and unneccesary, it turns out all I had to was plug in the plane's slope coords and just use the vertexes that were
			// already here!
			if (cv_grtest.value != 9)
			{
				// Really simple, you got a vertex on a sloped sector?
				// Give the slope data and XY value of a vertex to P_GetZAt and the function will give you back the z value
				if (sector)
				{
					// Yes this fixedheight check is needed again here
					if (sector->f_slope && sector->floorheight == fixedheight)
						v3d->y = P_GetZAtf(sector->f_slope, v3d->x, v3d->z);
					else if (sector->c_slope && sector->ceilingheight == fixedheight)
						v3d->y = P_GetZAtf(sector->c_slope, v3d->x, v3d->z);
				}
				else if (FOFsector)
				{
					// Yes this fixedheight check is needed again here
					if (FOFsector->f_slope && FOFsector->floorheight == fixedheight)
						v3d->y = P_GetZAtf(FOFsector->f_slope, v3d->x, v3d->z);
					else if (FOFsector->c_slope && FOFsector->ceilingheight == fixedheight)
						v3d->y = P_GetZAtf(FOFsector->c_slope, v3d->x, v3d->z);
				}
			}
#endif

		}

	}


	if (planecolormap && planecolormap->fog)
	{
		if (FOFsector && FOFsector->extra_colormap && FOFsector->extra_colormap->fog)
			Surf.FlatColor.rgba = HWR_Lighting(LightLevelToLum(lightlevel), planecolormap->rgba, false, false);
		else
			Surf.FlatColor.rgba = HWR_Lighting(LightLevelToLum(lightlevel), planecolormap->rgba, true, true);
	}
	else if (planecolormap)
		Surf.FlatColor.rgba = HWR_Lighting(LightLevelToLum(lightlevel), planecolormap->rgba, true, true);
	else
		Surf.FlatColor.rgba = HWR_Lighting(LightLevelToLum(lightlevel), NORMALFOG, false, false);

	// Recopy the alpha level, it gets reset after HWR_lighting
	Surf.FlatColor.s.alpha = alpha;

	// Now send the final values to the OpenGL system
	// and render it
	if (blendmode & PF_Environment)
		blendmode |= PF_Modulated|PF_Occlude;
	else if (blendmode & PF_Translucent)
		blendmode |= PF_Modulated;
	else
		blendmode |= PF_Masked|PF_Modulated;

	if (planescale != 0 && planescale != 1000)
	{
		Surf.TexScale = planescale/1000.0f;
		blendmode |= PF_TexScale;
	}

	// If it's a ceiling, cull the back, if it's a floor, cull the front

	GL_DrawPolygon(&Surf, planeVerts, nrPlaneVerts, blendmode, 0);

	// SRB2CBTODO: dynamic lighting on planes, polyobjects too
	HWR_PlaneLighting(planeVerts, nrPlaneVerts);
	HWR_RenderFloorSplat(planeVerts, nrPlaneVerts);
}

// SRB2CBTODO: Don't draw any parts of a map through the sky
#ifdef SKYWALL
static void HWR_RenderSkyWall(wallVert3D   * wallVerts)
{
	FOutVector  trVerts[4];
	FOutVector  *wv;

	// transform
	wv = trVerts;

	// More messy to unwrap, but it's also quicker, uses less memory.

	// It starts at 0 for the first wv
	wv->sow = wallVerts->s;
	wv->tow = wallVerts->t;
	wv->x   = wallVerts->x;
	wv->y   = wallVerts->y;
	wv->z   = wallVerts->z;
	wv++;
	wallVerts++;

	wv->sow = wallVerts->s;
	wv->tow = wallVerts->t;
	wv->x   = wallVerts->x;
	wv->y   = wallVerts->y;
	wv->z   = wallVerts->z;
	wv++;
	wallVerts++;

	wv->sow = wallVerts->s;
	wv->tow = wallVerts->t;
	wv->x   = wallVerts->x;
	wv->y   = wallVerts->y;
	wv->z   = wallVerts->z;
	wv++;
	wallVerts++;

	wv->sow = wallVerts->s;
	wv->tow = wallVerts->t;
	wv->x   = wallVerts->x;
	wv->y   = wallVerts->y;
	wv->z   = wallVerts->z;

	GL_DrawPolygon(NULL, trVerts, 4, PF_Invisible|PF_NoTexture|PF_Occlude, 0);
}
#endif

static void HWR_RenderSkyPlane(extrasubsector_t *xsub, fixed_t fixedheight)
{
	polyvertex_t *  pv;
	float           height; // All y values for vetexes on non-sloped planes will be the same floor/ceiling height
	FOutVector      *v3d;
	size_t             nrPlaneVerts = 0; // Stores the precalculated points for this polygon
	size_t             i;

	// No convex poly were generated for this subsector
	if (!xsub->planepoly)
		return;

	height = FIXED_TO_FLOAT(fixedheight);

	pv  = xsub->planepoly->pts;
	nrPlaneVerts = xsub->planepoly->numpts;

	// This plane can't even be rendered if it's not a polygon
	if (nrPlaneVerts < 3)
	{
		CONS_Printf("HWR_RenderSkyPlane: A plane does not have 3 sides\n");
		return;
	}

	if (nrPlaneVerts > MAXPLANEVERTICES)
	{
		CONS_Printf("HWR_RenderSkyPlane: A map polygon with %lu vertexes exceeds the max value of %lu vertices!\n",
					(ULONG)nrPlaneVerts, (ULONG)MAXPLANEVERTICES);
		return;
	}

	// transform
	v3d = planeVerts;
	for (i = 0; i < nrPlaneVerts; i++, v3d++, pv++)
	{
		// no texture, make sow and tow 0 so there's no rendering issues on certain angles
		v3d->sow = 0;
		v3d->tow = 0;
		v3d->x = pv->x;
		v3d->y = height;
		v3d->z = pv->y;
	}

	GL_DrawPolygon(NULL, planeVerts, nrPlaneVerts,
				PF_Invisible|PF_NoTexture|PF_Occlude, 0);
}



/*
   wallVerts order is :
		3--2
		| /|
		|/ |
		0--1
*/
#ifdef WALLSPLATS
static void HWR_DrawSegsSplats(FSurfaceInfo * pSurf)
{
	FOutVector trVerts[4], *wv;
	wallVert3D wallVerts[4];
	wallVert3D *pwallVerts;
	wallsplat_t *splat;
	GLPatch_t *gpatch;
	fixed_t i;
	FSurfaceInfo pSurf2;
	// seg bbox
	fixed_t segbbox[4];

	M_ClearBox(segbbox);
	M_AddToBox(segbbox,
		(fixed_t)(((polyvertex_t *)gr_curline->v1)->x*FRACUNIT),
		(fixed_t)(((polyvertex_t *)gr_curline->v1)->y*FRACUNIT));
	M_AddToBox(segbbox,
		(fixed_t)(((polyvertex_t *)gr_curline->v2)->x*FRACUNIT),
		(fixed_t)(((polyvertex_t *)gr_curline->v2)->y*FRACUNIT));

	// splat are drawn by line but this func is called for eatch segs of a line
	/* BP: DOESN'T WORK BECAUSE Z-buffer !!!!
		   FIXME : the splat must be stored by segs !
	if (gr_curline->linedef->splatdrawn == validcount)
		return;
	gr_curline->linedef->splatdrawn = validcount;
	*/

	splat = (wallsplat_t *)gr_curline->linedef->splats;
	for (; splat; splat = splat->next)
	{
		//BP: don't draw splat extern to this seg
		//    this is quick fix best is explain in logboris.txt at 12-4-2000
		if (!M_PointInBox(segbbox,splat->v1.x,splat->v1.y) && !M_PointInBox(segbbox,splat->v2.x,splat->v2.y))
			continue;

		gpatch = W_CachePatchNum(splat->patch, PU_CACHE);
		HWR_GetPatch(gpatch);

		wallVerts[0].x = wallVerts[3].x = FIXED_TO_FLOAT(splat->v1.x);
		wallVerts[0].z = wallVerts[3].z = FIXED_TO_FLOAT(splat->v1.y);
		wallVerts[2].x = wallVerts[1].x = FIXED_TO_FLOAT(splat->v2.x);
		wallVerts[2].z = wallVerts[1].z = FIXED_TO_FLOAT(splat->v2.y);

		i = splat->top;
		if (splat->yoffset)
			i += *splat->yoffset;

		wallVerts[2].y = wallVerts[3].y = FIXED_TO_FLOAT(i)+(gpatch->height>>1);
		wallVerts[0].y = wallVerts[1].y = FIXED_TO_FLOAT(i)-(gpatch->height>>1);

		wallVerts[3].s = wallVerts[3].t = wallVerts[2].s = wallVerts[0].t = 0.0f;
		wallVerts[1].s = wallVerts[1].t = wallVerts[2].t = wallVerts[0].s = 1.0f;

		// transform
		wv = trVerts;
		pwallVerts = wallVerts;
		for (i = 0; i < 4; i++,wv++,pwallVerts++)
		{
			wv->x   = pwallVerts->x;
			wv->z	= pwallVerts->z;
			wv->y   = pwallVerts->y;

			// SRB2CBTOFO: TOW and SOW needed to be switched
			wv->sow = pwallVerts->t;
			wv->tow = pwallVerts->s;
		}
		memcpy(&pSurf2,pSurf,sizeof (FSurfaceInfo));
		switch (splat->flags & SPLATDRAWMODE_MASK)
		{
			case SPLATDRAWMODE_OPAQUE :
				pSurf2.FlatColor.s.alpha = 0xff;
				i = PF_Translucent;
				break;
			case SPLATDRAWMODE_TRANS :
				pSurf2.FlatColor.s.alpha = 128;
				i = PF_Translucent;
				break;
			case SPLATDRAWMODE_SHADE :
				pSurf2.FlatColor.s.alpha = 0xff;
				i = PF_Substractive;
				break;
		}

		GL_DrawPolygon(&pSurf2, trVerts, 4, i|PF_Modulated|PF_Decal, 0);
	}
}
#endif

// ==========================================================================
//                                        WALL GENERATION FROM SUBSECTOR SEGS
// ==========================================================================


FBITFIELD HWR_TranstableToAlpha(int transtablenum, FSurfaceInfo *pSurf)
{
	switch (transtablenum)
	{
		case tr_trans10 : pSurf->FlatColor.s.alpha = 0xe6;return  PF_Translucent;
		case tr_trans20 : pSurf->FlatColor.s.alpha = 0xcc;return  PF_Translucent;
		case tr_trans30 : pSurf->FlatColor.s.alpha = 0xb3;return  PF_Translucent;
		case tr_trans40 : pSurf->FlatColor.s.alpha = 0x99;return  PF_Translucent;
		case tr_trans50 : pSurf->FlatColor.s.alpha = 0x80;return  PF_Translucent;
		case tr_trans60 : pSurf->FlatColor.s.alpha = 0x66;return  PF_Translucent;
		case tr_trans70 : pSurf->FlatColor.s.alpha = 0x4c;return  PF_Translucent;
		case tr_trans80 : pSurf->FlatColor.s.alpha = 0x33;return  PF_Translucent;
		case tr_trans90 : pSurf->FlatColor.s.alpha = 0x19;return  PF_Translucent;
	}
	return PF_Translucent;
}

// v1,v2 : the start & end vertices along the original wall segment, that may have been
//         clipped so that only a visible portion of the wall seg is drawn.
// floorheight, ceilingheight : depend on wall upper/lower/middle, comes from the sectors.

static void HWR_AddTransparentWall(wallVert3D *wallVerts, FSurfaceInfo * pSurf, FBITFIELD blendmode, int texnum, const sector_t *sector, boolean fogwall);
static void HWR_AddPeggedWall(wallVert3D *wallVerts, FSurfaceInfo *pSurf, int texnum);

// -----------------+
// HWR_ProjectWall  :
// -----------------+
/*
   wallVerts order is :
		3--2
		| /|
		|/ |
		0--1
*/
void HWR_ProjectWall(wallVert3D   * wallVerts,
                                    FSurfaceInfo * pSurf,
                                    FBITFIELD blendmode)
{
	FOutVector  trVerts[4];
	FOutVector  *wv;

	// transform
	wv = trVerts;

	// It starts at 0 for the first wv
	wv->sow = wallVerts->s;
	wv->tow = wallVerts->t;
	wv->x   = wallVerts->x;
	wv->y   = wallVerts->y;
	wv->z   = wallVerts->z;
	wv++;
	wallVerts++;

	wv->sow = wallVerts->s;
	wv->tow = wallVerts->t;
	wv->x   = wallVerts->x;
	wv->y   = wallVerts->y;
	wv->z   = wallVerts->z;
	wv++;
	wallVerts++;

	wv->sow = wallVerts->s;
	wv->tow = wallVerts->t;
	wv->x   = wallVerts->x;
	wv->y   = wallVerts->y;
	wv->z   = wallVerts->z;
	wv++;
	wallVerts++;

	wv->sow = wallVerts->s;
	wv->tow = wallVerts->t;
	wv->x   = wallVerts->x;
	wv->y   = wallVerts->y;
	wv->z   = wallVerts->z;

	GL_DrawPolygon(pSurf, trVerts, 4, blendmode|PF_Modulated|PF_Occlude, PF2_CullBack);

#ifdef WALLSPLATS
	if (gr_curline->linedef->splats && cv_splats.value)
		HWR_DrawSegsSplats(pSurf);
#endif

	// Setup dynamic lighting for walls too
	HWR_WallLighting(trVerts);
	HWR_WallShading(trVerts);

	// SRB2CBTODO: for 'real' dynamic light in dark area, we should draw the light first
	//         and then the wall with the right blending func
	//GL_DrawPolygon(pSurf, trVerts, 4, PF_Additive|PF_Modulated|PF_Occlude);
}

// ==========================================================================
//                                                          BSP, CULL, ETC..
// ==========================================================================

//
// HWR_SplitWall
//
// Handles a polygon of a wall, and if needed, splits it into multiple pieces
// acoording to colormaps and/or lightlevels
// SRB2CBTODO: this method can be used for sprites and patches
// Kalaron: Updated for slopes! SRB2CBTODO: Could be a little more acurate, but eh...
static void HWR_SplitWall(sector_t *sector, wallVert3D *wallVerts, int texnum, FSurfaceInfo* Surf, ffloortype_e cutflag, const byte alpha)
{
	// The cutflag contains the FOF flags of the wall that this function is handling,
	// which is the wall being split up into different lightlevels and/or colormaps
	GLTexture_t * glTex = NULL;
	float realtop1, realtop2,
		  realbot1, realbot2,
		  top1, top2,
		  bot1, bot2;
	float pegt1, pegt2,
		  pegb1, pegb2,
		  pegmul1, pegmul2;
	float height1, height2,
		  bheight1 = 0, bheight2 = 0; // SRB2CBTODO: check bheight1
	int i;
	boolean solid = false;

	// v1f.x == v2f.x && v1f.y == v12.y
	if (wallVerts[0].x == wallVerts[1].x && wallVerts[3].z == wallVerts[1].z)
	{
		return;
	}

	// NOTE: sector can be gr_frontsector, gr_backsector, or any sector

	realtop1 = top1 = wallVerts[2].y;
	realtop2 = top2 = wallVerts[3].y;

	realbot1 = bot1 = wallVerts[0].y;
	realbot2 = bot2 = wallVerts[1].y;

	pegt1 = wallVerts[2].t;
	pegt2 = wallVerts[3].t;

	pegb1 = wallVerts[0].t;
	pegb2 = wallVerts[1].t;

	pegmul1 = (pegb1 - pegt1) / (top1 - bot1);
	pegmul2 = (pegb2 - pegt2) / (top2 - bot2);

	// Split the wall up (The parts affected by FOFs)
	for (i = 1; i < sector->numlights; i++)
	{
		if (top1 < realbot1 && top2 < realbot2)
			return;

		if (sector->lightlist[i].flags & FF_NOSHADE)
			continue;



		// The "solid" boolean here is more like a "merge" check, check for conditions where
		// the two walls touching should merge (for things like water FOFs)
		// Check if the caster cuts extra, and the wall passed to HWR_SplitWall is has the FF_EXTRA flag.
		// Only merge with your own kind, also, water FOFs(FF_SWIMMABLE) can always merge
		if (sector->lightlist[i].caster)
		{
			if ((sector->lightlist[i].caster->flags & FF_CUTEXTRA) && (cutflag & FF_EXTRA)
				&& ((sector->lightlist[i].caster->flags == cutflag)
					|| ((sector->lightlist[i].caster->flags & FF_SWIMMABLE) && (cutflag & FF_SWIMMABLE))))
				solid = true;
		}


		height1 = FIXED_TO_FLOAT(sector->lightlist[i].height); // SRB2CBTODO: FOF SLOPES
		height2 = FIXED_TO_FLOAT(sector->lightlist[i].height);

		if (sector->lightlist[i].heightslope)
		{
			height1 = P_GetZAtf(sector->lightlist[i].heightslope, wallVerts[2].x, wallVerts[2].z);
			height2 = P_GetZAtf(sector->lightlist[i].heightslope, wallVerts[3].x, wallVerts[3].z);
		}

		// SRB2CBTODO: This one solid check needs to occur for some blocks,
		// see DSZ1, a block solid is false which causes a waterblock over it
		if (sector->lightlist[i].caster && solid)
		{
			bheight1 = FIXED_TO_FLOAT(*sector->lightlist[i].caster->bottomheight);
			bheight2 = FIXED_TO_FLOAT(*sector->lightlist[i].caster->bottomheight);

			if (sector->lightlist[i].caster->b_slope)
			{
				bheight1 = P_GetZAtf(sector->lightlist[i].caster->b_slope, wallVerts[0].x, wallVerts[0].z);
				bheight2 = P_GetZAtf(sector->lightlist[i].caster->b_slope, wallVerts[1].x, wallVerts[1].z);
			}
		}

		if (height1 >= top1 && height2 >= top2)
		{
			if (solid
				&& (top1 > bheight1 && top2 > bheight2))
			{
				top1 = bheight1;
				top2 = bheight2;
			}
			continue;
		}

		// Split up rest of wall not effected by light/colormap

		// Found a break;

		// Setup the bot again instead of just = height1 & height2
		// Vertexes need to be addressed individually
		bot1 = FIXED_TO_FLOAT(sector->lightlist[i].height);
		bot2 = FIXED_TO_FLOAT(sector->lightlist[i].height);

		if (sector->lightlist[i].heightslope)
		{
			bot1 = P_GetZAtf(sector->lightlist[i].heightslope, wallVerts[0].x, wallVerts[0].z);
			bot2 = P_GetZAtf(sector->lightlist[i].heightslope, wallVerts[1].x, wallVerts[1].z);
		}

		if (bot1 < realbot1 && bot2 < realbot2)
		{
			bot1 = realbot1;
			bot2 = realbot2;
		}

		// Set the poly flat color
		byte lightnum;

		// light levels
		if (sector->lightlist[i-1].caster)
			lightnum = LightLevelToLum(*sector->lightlist[i-1].lightlevel);
		else
			lightnum = LightLevelToLum(sector->lightlevel);

		// colormaps
		if (sector->lightlist[i-1].extra_colormap)
		{
			if (sector->lightlist[i-1].extra_colormap->fog) // SRB2CBTODO: Better fog colormap casts
				Surf->FlatColor.rgba = HWR_Lighting(lightnum, sector->lightlist[i-1].extra_colormap->rgba, true, true);
			else
				Surf->FlatColor.rgba = HWR_Lighting(lightnum, sector->lightlist[i-1].extra_colormap->rgba, true, true);
		}
		else
			Surf->FlatColor.rgba = HWR_Lighting(lightnum, NORMALFOG, false, true);

		// Recopy the alpha level, it gets reset after HWR_lighting
		Surf->FlatColor.s.alpha = alpha;

		wallVerts[3].t = pegt2 + ((realtop2 - top2) * pegmul2);
		wallVerts[2].t = pegt1 + ((realtop1 - top1) * pegmul1);

		wallVerts[1].t = pegt2 + ((realtop2 - bot2) * pegmul2);
		wallVerts[0].t = pegt1 + ((realtop1 - bot1) * pegmul1);

		// set top/bottom coords
		wallVerts[3].y = top2;
		wallVerts[2].y = top1;

		wallVerts[1].y = bot2;
		wallVerts[0].y = bot1;

		glTex = HWR_GetTexture(texnum, true);

		if (cutflag & FF_TRANSLUCENT)
			HWR_AddTransparentWall(wallVerts, Surf, PF_Translucent, texnum, sector, false); // Never a fogblock, because those are never split
		else if (glTex->mipmap.flags & TF_TRANSPARENT)
			HWR_AddPeggedWall(wallVerts, Surf, texnum);
		else
			HWR_ProjectWall(wallVerts, Surf, PF_Masked);

		if (solid)
		{
			top1 = bheight1;
			top2 = bheight2;
		}
		else
		{
			top1 = height1;
			top2 = height2;
		}
	}

	bot1 = realbot1;
	bot2 = realbot2;

	// Render the rest of the wall that's not in the FOF

	if (top1 <= realbot1 && top2 <= realbot2)
		return;

	// REWRITETODO: HWR_PutWall
	{
		// Set the poly flat color
		byte lightnum;

		// lightlevels
		// If an FOF is casting above the wall, render it by the caster's lightlevel all the way down
		if (sector->lightlist[i-1].caster)
			lightnum = LightLevelToLum(*sector->lightlist[i-1].lightlevel);
		else
			lightnum = LightLevelToLum(sector->lightlevel);

		// colormaps
		// Check if there's an FOF that touches the linedef with a colormap
		if (sector->lightlist[i-1].extra_colormap)
		{
			if (sector->lightlist[i-1].extra_colormap->fog) // SRB2CBTODO: Better fog colormap casts
				Surf->FlatColor.rgba = HWR_Lighting(lightnum, sector->lightlist[i-1].extra_colormap->rgba, true, true);
			else
				Surf->FlatColor.rgba = HWR_Lighting(lightnum, sector->lightlist[i-1].extra_colormap->rgba, true, true);
		}
		else if (sector->extra_colormap)
			Surf->FlatColor.rgba = HWR_Lighting(lightnum, sector->extra_colormap->rgba, true, true);
		else
			Surf->FlatColor.rgba = HWR_Lighting(lightnum, NORMALFOG, false, true);

		// Recopy the alpha level, it gets reset after HWR_lighting
		Surf->FlatColor.s.alpha = alpha;

		wallVerts[3].t = pegt2 + ((realtop2 - top2) * pegmul2);
		wallVerts[2].t = pegt1 + ((realtop1 - top1) * pegmul1);

		wallVerts[1].t = pegt2 + ((realtop2 - bot2) * pegmul2);
		wallVerts[0].t = pegt1 + ((realtop1 - bot1) * pegmul1);

		// set top/bottom coords
		wallVerts[3].y = top2;
		wallVerts[2].y = top1;

		wallVerts[1].y = bot2;
		wallVerts[0].y = bot1;

		glTex = HWR_GetTexture(texnum, true);

		if (cutflag & FF_TRANSLUCENT)
			HWR_AddTransparentWall(wallVerts, Surf, PF_Translucent, texnum, sector, false);
		else if (glTex->mipmap.flags & TF_TRANSPARENT)
			HWR_AddPeggedWall(wallVerts, Surf, texnum);
		else
			HWR_ProjectWall(wallVerts, Surf, PF_Masked);
	}
}


// Sweetness
static void HWR_FoFSeg(wallVert3D *wallVerts, FSurfaceInfo Surf) // Split FOF code so the code is more maintainable
{
#ifdef R_FAKEFLOORS // Render the sides of 3D floors(FOFs)
	GLTexture_t *grTex = NULL;
	float cliplow = 0.0f, cliphigh = 0.0f;
	FBITFIELD blendmode = PF_Masked;
	ffloor_t *rover;
	ffloor_t *r2; // For special handling when certain FOFs touch
	fixed_t   lowcut, highcut;
	float h, l;

	// x offset the texture
	fixed_t texturehpeg = gr_sidedef->textureoffset + gr_curline->offset;

	cliplow = (float)texturehpeg;
	cliphigh = texturehpeg + gr_curline->len;

	lowcut = gr_frontsector->floorheight > gr_backsector->floorheight ? gr_frontsector->floorheight : gr_backsector->floorheight;
	highcut = gr_frontsector->ceilingheight < gr_backsector->ceilingheight ? gr_frontsector->ceilingheight : gr_backsector->ceilingheight;

	const sector_t *sector = gr_frontsector; // For tracking where to get lighting from

	if (gr_frontsector->ffloors && gr_backsector->ffloors)
	{
		// This renders the backsides of FOFs (for things like water)
		for (rover = gr_backsector->ffloors; rover; rover = rover->next)
		{
			if (!(rover->flags & FF_RENDERSIDES) || !(rover->flags & FF_EXISTS))
				continue;
			if (rover->flags & FF_INVERTSIDES)
				continue;
			if (*rover->topheight < lowcut || *rover->bottomheight > highcut)
				continue;

			if (rover->norender == leveltime)
				continue;

			for (r2 = gr_frontsector->ffloors; r2; r2 = r2->next)
			{
				if (!(r2->flags & FF_EXISTS) || !(r2->flags & FF_RENDERSIDES)
					|| *r2->topheight < lowcut || *r2->bottomheight > highcut)
					continue;

				if (r2->norender == leveltime)
					continue;

				if (rover->flags & FF_EXTRA)
				{
					if (!(r2->flags & FF_CUTEXTRA))
						continue;

					if (r2->flags & FF_EXTRA && (r2->flags & (FF_TRANSLUCENT|FF_FOG)) != (rover->flags & (FF_TRANSLUCENT|FF_FOG)))
						continue;
				}
				else
				{
					if (!(r2->flags & FF_CUTSOLIDS))
						continue;
				}

				if (*rover->topheight > *r2->topheight || *rover->bottomheight < *r2->bottomheight)
					continue;

				break;
			}
			if (r2)
				continue;

			// Draw the outside of an FOF (the backsector)
			h = *rover->topheight;
			l = *rover->bottomheight;

			wallVerts[2].y = wallVerts[3].y = FIXED_TO_FLOAT(h);
			wallVerts[0].y = wallVerts[1].y = FIXED_TO_FLOAT(l);


#ifdef ESLOPE
			pslope_t *topslope = rover->master->frontsector->c_slope;
			pslope_t *bottomslope = rover->master->frontsector->f_slope;

			if (cv_grtest.value != 9)
			{
				// Adjust vertexes to slopes

				if (topslope)
				{
					wallVerts[3].y = P_GetZAtf(topslope, wallVerts[3].x, wallVerts[3].z);
					wallVerts[2].y = P_GetZAtf(topslope, wallVerts[2].x, wallVerts[2].z);
				}

				if (bottomslope)
				{
					// Top
					// Add hl for perfectly slanted walls!
					//wallVerts[3].y = P_GetZAtf(bottomslope, wallVerts[3].x, wallVerts[3].z)+FIXED_TO_FLOAT(h-l); // Hack for now ESLOPETODO
					//wallVerts[2].y = P_GetZAtf(bottomslope, wallVerts[2].x, wallVerts[2].z)+FIXED_TO_FLOAT(h-l);

					// Bottom
					wallVerts[0].y = P_GetZAtf(bottomslope, wallVerts[0].x, wallVerts[0].z);
					wallVerts[1].y = P_GetZAtf(bottomslope, wallVerts[1].x, wallVerts[1].z);
				}

			}
#endif

			if (rover->flags & FF_FOG)
			{
				// grTex is still NULL
				wallVerts[3].t = wallVerts[2].t = 0;
				wallVerts[0].t = wallVerts[1].t = 0;
				wallVerts[0].s = wallVerts[3].s = 0;
				wallVerts[2].s = wallVerts[1].s = 0;
			}
			else
			{
				grTex = HWR_GetTexture(texturetranslation[sides[rover->master->sidenum[0]].midtexture], true);

				line_t *newline = NULL;
				fixed_t offsetvalue;

				if (rover->master->flags & ML_TFERLINE) // SRB2CBTODO: Better method to do this than just line #?
				{
					int linenum = gr_curline->linedef - gr_backsector->lines[0]; // SRB2CBTODO: fixed_t?
					newline = rover->master->frontsector->lines[0] + linenum;
					grTex = HWR_GetTexture(texturetranslation[sides[newline->sidenum[0]].midtexture], true);
				}

				// If this linedef wall has a seperate linedef option, use its row offset too
				if (newline)
					offsetvalue = sides[newline->sidenum[0]].rowoffset;
				else
					offsetvalue = sides[rover->master->sidenum[0]].rowoffset;

				// Setup scrolling and texture coords
				wallVerts[3].t = wallVerts[2].t = (*rover->topheight - h + offsetvalue) * grTex->scaleY;
				wallVerts[0].t = wallVerts[1].t = (h - l + (*rover->topheight - h + offsetvalue)) * grTex->scaleY;
				wallVerts[0].s = wallVerts[3].s = cliplow * grTex->scaleX;
				wallVerts[2].s = wallVerts[1].s = cliphigh * grTex->scaleX;

#ifdef ESLOPE
				if (cv_grtest.value != 9)
				{
					if (topslope || bottomslope) // SRB2CBTODO: ESLOPE: Make this work with scrolling! & linedef option for this!
					{
						float texheight = HWR_GetTexture(texturetranslation[sides[rover->master->sidenum[0]].midtexture], false)->mipmap.height;

						// Kalaron: When we slope linedefs, we need to make the linedef textures matchup and not stretch with the linedef
						// All we have to do is change the vertical texture offsets (tow),
						// the horizontal offsets (sow) do not need to be changed
						// because only vertical heights are changed anyway when something is sloped
						float yoffset = offsetvalue;
						wallVerts[0].t = (float)((wallVerts[0].y / texheight) + yoffset);
						wallVerts[1].t = (float)((wallVerts[1].y / texheight) + yoffset);
						wallVerts[2].t = (float)((wallVerts[2].y / texheight) + yoffset);
						wallVerts[3].t = (float)((wallVerts[3].y / texheight) + yoffset);
					}
				}
#endif
			}


			if (rover->flags & FF_TRANSLUCENT)
			{
				if ((grTex) && ((grTex->mipmap.flags & TF_TRANSPARENT) || rover->alpha >= 243))
					blendmode = PF_Environment;
				else
					blendmode = PF_Translucent;

				Surf.FlatColor.s.alpha = rover->alpha;
			}
			else if (rover->flags & FF_FOG)
			{
				blendmode = PF_Translucent|PF_NoTexture;

				// In software mode, fog blocks are simply a colormapped area of the map,
				// they can never be visibly fully opaque, they use rover's master frontsector's
				// to modulate the transparency because software renders darker things more opqaue,
				// so basically, OpenGL mode can't render a colormaped area without polygons,
				// so use software's alpha darkness-colormap thingy method :P

				if (rover->master->frontsector->extra_colormap)
					Surf.FlatColor.rgba = HWR_Lighting(rover->master->frontsector->lightlevel, rover->master->frontsector->extra_colormap->rgba, false, false);
				else
					Surf.FlatColor.rgba = HWR_Lighting(rover->master->frontsector->lightlevel, NORMALFOG, false, false);

				Surf.FlatColor.s.alpha = rover->master->frontsector->lightlevel/1.4;

				// Important check, the game does not draw non-colored fog blocks at full brightness
				// these fog blocks are not drawn as part of special visual effects for certain levels
				// full brightness = 255 brightness
				// (0 alpha walls are automatically not rendered when passed to HWR_AddTransparentWall
				if (!(rover->master->frontsector->extra_colormap && rover->master->frontsector->extra_colormap->fog))
				{
					if (rover->master->frontsector->lightlevel == 255)
						Surf.FlatColor.s.alpha = 0;
				}

			}
			else
				Surf.FlatColor.s.alpha = 255;

			if (rover->flags & FF_FOG)
				HWR_AddTransparentWall(wallVerts, &Surf, blendmode, 0, rover->master->frontsector, true); // HWR_AddNoTexWall
			else if (gr_frontsector->numlights)
				HWR_SplitWall(gr_frontsector, wallVerts, texturetranslation[sides[rover->master->sidenum[0]].midtexture], &Surf,
							  rover->flags, Surf.FlatColor.s.alpha);
			else
			{
				if ((blendmode & PF_Translucent) || (rover->flags & FF_TRANSLUCENT && (blendmode & PF_Environment) && (rover->alpha < 243)))
					HWR_AddTransparentWall(wallVerts, &Surf, blendmode, texturetranslation[sides[rover->master->sidenum[0]].midtexture], sector, false);
				else if (blendmode & PF_Environment)
					HWR_AddPeggedWall(wallVerts, &Surf, texturetranslation[sides[rover->master->sidenum[0]].midtexture]);
				else
					HWR_ProjectWall(wallVerts, &Surf, PF_Masked);
			}

		}

		// This renders the front side of FOFs
		for (rover = gr_frontsector->ffloors; rover; rover = rover->next)
		{
			if (!(rover->flags & FF_RENDERSIDES) || !(rover->flags & FF_EXISTS))
				continue;
			if (!(rover->flags & FF_ALLSIDES))
				continue;

			if (*rover->topheight < lowcut || *rover->bottomheight > highcut)
				continue;

			if (rover->norender == leveltime)
				continue;

			for (r2 = gr_backsector->ffloors; r2; r2 = r2->next)
			{
				if (!(r2->flags & FF_EXISTS) || !(r2->flags & FF_RENDERSIDES)
					|| *r2->topheight < lowcut || *r2->bottomheight > highcut)
					continue;

				if (r2->norender == leveltime)
					continue;

				if (rover->flags & FF_EXTRA)
				{
					if (!(r2->flags & FF_CUTEXTRA))
						continue;

					if (r2->flags & FF_EXTRA && (r2->flags & (FF_TRANSLUCENT|FF_FOG)) != (rover->flags & (FF_TRANSLUCENT|FF_FOG)))
						continue;
				}
				else
				{
					if (!(r2->flags & FF_CUTSOLIDS))
						continue;
				}

				if (*rover->topheight > *r2->topheight || *rover->bottomheight < *r2->bottomheight)
					continue;

				break;
			}
			if (r2)
				continue;

			// Draw the inside of an FOF (the frontsector)
			h = *rover->topheight;
			l = *rover->bottomheight;
			if (h > highcut)
				h = highcut;
			if (l < lowcut)
				l = lowcut;

			wallVerts[2].y = wallVerts[3].y = FIXED_TO_FLOAT(h);
			wallVerts[0].y = wallVerts[1].y = FIXED_TO_FLOAT(l);


#ifdef ESLOPE
			pslope_t *topslope = rover->master->frontsector->c_slope;
			pslope_t *bottomslope = rover->master->frontsector->f_slope;

			if (cv_grtest.value != 9)
			{
				// Adjust vertexes to slopes

				if (topslope)
				{
					wallVerts[3].y = P_GetZAtf(topslope, wallVerts[3].x, wallVerts[3].z);
					wallVerts[2].y = P_GetZAtf(topslope, wallVerts[2].x, wallVerts[2].z);
				}

				if (bottomslope)
				{
					// Top
					// Add hl for perfectly slanted walls!
					//wallVerts[3].y = P_GetZAtf(bottomslope, wallVerts[3].x, wallVerts[3].z)+FIXED_TO_FLOAT(h-l); // Hack for now ESLOPETODO
					//wallVerts[2].y = P_GetZAtf(bottomslope, wallVerts[2].x, wallVerts[2].z)+FIXED_TO_FLOAT(h-l);

					// Bottom
					wallVerts[0].y = P_GetZAtf(bottomslope, wallVerts[0].x, wallVerts[0].z);
					wallVerts[1].y = P_GetZAtf(bottomslope, wallVerts[1].x, wallVerts[1].z);
				}

			}
#endif

			if (rover->flags & FF_FOG)
			{
				// grTex is still NULL
				wallVerts[3].t = wallVerts[2].t = 0;
				wallVerts[0].t = wallVerts[1].t = 0;
				wallVerts[0].s = wallVerts[3].s = 0;
				wallVerts[2].s = wallVerts[1].s = 0;
			}
			else
			{
				grTex = HWR_GetTexture(texturetranslation[sides[rover->master->sidenum[0]].midtexture], true);
				line_t *newline = NULL;
				fixed_t offsetvalue;

				if (rover->master->flags & ML_TFERLINE) // SRB2CBTODO: Better method to do this than just line #?
				{
					int linenum = gr_curline->linedef - gr_backsector->lines[0]; // SRB2CBTODO: fixed_t?
					newline = rover->master->frontsector->lines[0] + linenum;
					grTex = HWR_GetTexture(texturetranslation[sides[newline->sidenum[0]].midtexture], true);
				}

				// If this linedef wall has a seperate linedef option, use its row offset too
				if (newline)
					offsetvalue = sides[newline->sidenum[0]].rowoffset;
				else
					offsetvalue = sides[rover->master->sidenum[0]].rowoffset;

				// Setup scrolling and texture coords
				wallVerts[3].t = wallVerts[2].t = (*rover->topheight - h + offsetvalue) * grTex->scaleY;
				wallVerts[0].t = wallVerts[1].t = (h - l + (*rover->topheight - h + offsetvalue)) * grTex->scaleY;
				wallVerts[0].s = wallVerts[3].s = cliplow * grTex->scaleX;
				wallVerts[2].s = wallVerts[1].s = cliphigh * grTex->scaleX;


#ifdef ESLOPE
				if (cv_grtest.value != 9)
				{
					if (topslope || bottomslope) // SRB2CBTODO: ESLOPE: Make this work with scrolling! & linedef option for this!
					{
						float texheight = HWR_GetTexture(texturetranslation[sides[rover->master->sidenum[0]].midtexture], true)->mipmap.height;

						// Kalaron: When we slope linedefs, we need to make the linedef textures matchup and not stretch with the linedef
						// All we have to do is change the vertical texture offsets (tow),
						// the horizontal offsets (sow) do not need to be changed
						// because only vertical heights are changed anyway when something is sloped
						float yoffset = offsetvalue;
						wallVerts[0].t = (float)((wallVerts[0].y / texheight) + yoffset);
						wallVerts[1].t = (float)((wallVerts[1].y / texheight) + yoffset);
						wallVerts[2].t = (float)((wallVerts[2].y / texheight) + yoffset);
						wallVerts[3].t = (float)((wallVerts[3].y / texheight) + yoffset);
					}
				}
#endif
			}

			if (rover->flags & FF_TRANSLUCENT)
			{
				if ((grTex) && ((grTex->mipmap.flags & TF_TRANSPARENT) || rover->alpha >= 243))
					blendmode = PF_Environment;
				else
					blendmode = PF_Translucent;

				Surf.FlatColor.s.alpha = rover->alpha;
			}
			else if (rover->flags & FF_FOG)
			{
				blendmode = PF_Translucent|PF_NoTexture;

				// In software mode, fog blocks are simply a colormapped area of the map,
				// they can never be visibly fully opaque, they use rover's master frontsector's
				// to modulate the transparency because software renders darker things more opqaue,
				// so basically, OpenGL mode can't render a colormaped area without polygons,
				// so use software's alpha darkness-colormap thingy method :P

				if (rover->master->frontsector->extra_colormap)
					Surf.FlatColor.rgba = HWR_Lighting(rover->master->frontsector->lightlevel, rover->master->frontsector->extra_colormap->rgba, false, false);
				else
					Surf.FlatColor.rgba = HWR_Lighting(rover->master->frontsector->lightlevel, NORMALFOG, false, false);

				Surf.FlatColor.s.alpha = rover->master->frontsector->lightlevel/1.4;

				// Important check, the game does not draw non-colored fog blocks at full brightness
				// these fog blocks are not drawn as part of special visual effects for certain levels
				// full brightness = 255 brightness
				// (0 alpha walls are automatically not rendered when passed to HWR_AddTransparentWall
				if (!(rover->master->frontsector->extra_colormap && rover->master->frontsector->extra_colormap->fog))
				{
					if (rover->master->frontsector->lightlevel == 255)
						Surf.FlatColor.s.alpha = 0;
				}

			}
			else
				Surf.FlatColor.s.alpha = 255;


			if (rover->flags & FF_FOG)
				HWR_AddTransparentWall(wallVerts, &Surf, blendmode, 0, rover->master->frontsector, true); // HWR_AddNoTexWall
			else if (gr_backsector->numlights)
				HWR_SplitWall(gr_backsector, wallVerts, texturetranslation[sides[rover->master->sidenum[0]].midtexture], &Surf,
							  rover->flags, Surf.FlatColor.s.alpha);
			else
			{
				if ((blendmode & PF_Translucent) || (rover->flags & FF_TRANSLUCENT && (blendmode & PF_Environment) && (rover->alpha < 243)))
					HWR_AddTransparentWall(wallVerts, &Surf, blendmode, texturetranslation[sides[rover->master->sidenum[0]].midtexture], sector, false);
				else if (blendmode & PF_Environment)
					HWR_AddPeggedWall(wallVerts, &Surf, texturetranslation[sides[rover->master->sidenum[0]].midtexture]);
				else
					HWR_ProjectWall(wallVerts, &Surf, PF_Masked);
			}

		}
	}

	// Draw only the outside of an FOF (the backsector)
	else if (gr_backsector->ffloors)
	{
		for (rover = gr_backsector->ffloors; rover; rover = rover->next)
		{
			if (!(rover->flags & FF_EXISTS) || !(rover->flags & FF_RENDERSIDES))
				continue;
			if (rover->flags & FF_INVERTSIDES)
				continue;
			if (*rover->topheight <= gr_frontsector->floorheight || *rover->bottomheight >= gr_frontsector->ceilingheight)
				continue;
			if (rover->norender == leveltime)
				continue;

			h = *rover->topheight;
			l = *rover->bottomheight;
			if (h > highcut)
				h = highcut;
			if (l < lowcut)
				l = lowcut;

			wallVerts[2].y = wallVerts[3].y = FIXED_TO_FLOAT(h);
			wallVerts[0].y = wallVerts[1].y = FIXED_TO_FLOAT(l);

#ifdef ESLOPE
			pslope_t *topslope = rover->master->frontsector->c_slope;
			pslope_t *bottomslope = rover->master->frontsector->f_slope;

			if (cv_grtest.value != 9)
			{
				// Adjust vertexes to slopes

				if (topslope)
				{
					wallVerts[3].y = P_GetZAtf(topslope, wallVerts[3].x, wallVerts[3].z);
					wallVerts[2].y = P_GetZAtf(topslope, wallVerts[2].x, wallVerts[2].z);
				}

				if (bottomslope)
				{
					// Top
					// Add hl for perfectly slanted walls!
					//wallVerts[3].y = P_GetZAtf(bottomslope, wallVerts[3].x, wallVerts[3].z)+FIXED_TO_FLOAT(h-l); // Hack for now ESLOPETODO
					//wallVerts[2].y = P_GetZAtf(bottomslope, wallVerts[2].x, wallVerts[2].z)+FIXED_TO_FLOAT(h-l);

					// Bottom
					wallVerts[0].y = P_GetZAtf(bottomslope, wallVerts[0].x, wallVerts[0].z);
					wallVerts[1].y = P_GetZAtf(bottomslope, wallVerts[1].x, wallVerts[1].z);
				}

			}
#endif

			if (rover->flags & FF_FOG)
			{
				// grTex is still NULL
				wallVerts[3].t = wallVerts[2].t = 0;
				wallVerts[0].t = wallVerts[1].t = 0;
				wallVerts[0].s = wallVerts[3].s = 0;
				wallVerts[2].s = wallVerts[1].s = 0;
			}
			else
			{
				grTex = HWR_GetTexture(texturetranslation[sides[rover->master->sidenum[0]].midtexture], true);

				line_t *newline = NULL;
				fixed_t offsetvalue;

				if (rover->master->flags & ML_TFERLINE) // SRB2CBTODO: Better method to do this than just line #?
				{
					int linenum = gr_curline->linedef - gr_backsector->lines[0]; // SRB2CBTODO: fixed_t?
					newline = rover->master->frontsector->lines[0] + linenum;
					grTex = HWR_GetTexture(texturetranslation[sides[newline->sidenum[0]].midtexture], true);
				}

				// If this linedef wall has a seperate linedef option, use its row offset too
				if (newline)
					offsetvalue = sides[newline->sidenum[0]].rowoffset;
				else
					offsetvalue = sides[rover->master->sidenum[0]].rowoffset;

				// Setup scrolling and texture coords
				wallVerts[3].t = wallVerts[2].t = (*rover->topheight - h + offsetvalue) * grTex->scaleY;
				wallVerts[0].t = wallVerts[1].t = (h - l + (*rover->topheight - h + offsetvalue)) * grTex->scaleY;
				wallVerts[0].s = wallVerts[3].s = cliplow * grTex->scaleX;
				wallVerts[2].s = wallVerts[1].s = cliphigh * grTex->scaleX;


#ifdef ESLOPE
				if (cv_grtest.value != 9)
				{
					if (topslope || bottomslope) // SRB2CBTODO: ESLOPE: Make this work with scrolling! & linedef option for this!
					{
						float texheight = HWR_GetTexture(texturetranslation[sides[rover->master->sidenum[0]].midtexture], true)->mipmap.height;

						// Kalaron: When we slope linedefs, we need to make the linedef textures matchup and not stretch with the linedef
						// All we have to do is change the vertical texture offsets (tow),
						// the horizontal offsets (sow) do not need to be changed
						// because only vertical heights are changed anyway when something is sloped
						float yoffset = offsetvalue;
						wallVerts[0].t = (float)((wallVerts[0].y / texheight) + yoffset);
						wallVerts[1].t = (float)((wallVerts[1].y / texheight) + yoffset);
						wallVerts[2].t = (float)((wallVerts[2].y / texheight) + yoffset);
						wallVerts[3].t = (float)((wallVerts[3].y / texheight) + yoffset);
					}
				}
#endif
			}


			if (rover->flags & FF_TRANSLUCENT)
			{
				if ((grTex) && ((grTex->mipmap.flags & TF_TRANSPARENT) || rover->alpha >= 243))
					blendmode = PF_Environment;
				else
					blendmode = PF_Translucent;

				Surf.FlatColor.s.alpha = rover->alpha;
			}
			else if (rover->flags & FF_FOG)
			{
				blendmode = PF_Translucent|PF_NoTexture;

				// In software mode, fog blocks are simply a colormapped area of the map,
				// they can never be visibly fully opaque, they use rover's master frontsector's
				// to modulate the transparency because software renders darker things more opqaue,
				// so basically, OpenGL mode can't render a colormaped area without polygons,
				// so use software's alpha darkness-colormap thingy method :P

				if (rover->master->frontsector->extra_colormap)
					Surf.FlatColor.rgba = HWR_Lighting(rover->master->frontsector->lightlevel, rover->master->frontsector->extra_colormap->rgba, false, false);
				else
					Surf.FlatColor.rgba = HWR_Lighting(rover->master->frontsector->lightlevel, NORMALFOG, false, false);

				Surf.FlatColor.s.alpha = rover->master->frontsector->lightlevel/1.4;

				// Important check, the game does not draw non-colored fog blocks at full brightness
				// these fog blocks are not drawn as part of special visual effects for certain levels
				// full brightness = 255 brightness
				// (0 alpha walls are automatically not rendered when passed to HWR_AddTransparentWall
				if (!(rover->master->frontsector->extra_colormap && rover->master->frontsector->extra_colormap->fog))
				{
					if (rover->master->frontsector->lightlevel == 255)
						Surf.FlatColor.s.alpha = 0;
				}

			}
			else
				Surf.FlatColor.s.alpha = 255;

			// This one right here has a foggish problem

			if (rover->flags & FF_FOG)
				HWR_AddTransparentWall(wallVerts, &Surf, blendmode, 0, rover->master->frontsector, true); // HWR_AddNoTexWall
			else if (gr_frontsector->numlights)
				HWR_SplitWall(gr_frontsector, wallVerts, texturetranslation[sides[rover->master->sidenum[0]].midtexture], &Surf,
							  rover->flags, Surf.FlatColor.s.alpha);
			else
			{
				if ((blendmode & PF_Translucent) || (rover->flags & FF_TRANSLUCENT && (blendmode & PF_Environment) && (rover->alpha < 243)))
					HWR_AddTransparentWall(wallVerts, &Surf, blendmode, texturetranslation[sides[rover->master->sidenum[0]].midtexture], sector, false);
				else if (blendmode & PF_Environment)
					HWR_AddPeggedWall(wallVerts, &Surf, texturetranslation[sides[rover->master->sidenum[0]].midtexture]);
				else
					HWR_ProjectWall(wallVerts, &Surf, PF_Masked);
			}

		}
	}

	// Draw only the inside of an FOF (the frontsector)
	else if (gr_frontsector->ffloors)
	{
		for (rover = gr_frontsector->ffloors; rover; rover = rover->next)
		{
			if (!(rover->flags & FF_EXISTS) || !(rover->flags & FF_RENDERSIDES))
				continue;
			if (!(rover->flags & FF_ALLSIDES))
				continue;
			if (*rover->topheight <= gr_frontsector->floorheight || *rover->bottomheight >= gr_frontsector->ceilingheight)
				continue;
			if (*rover->topheight <= gr_backsector->floorheight || *rover->bottomheight >= gr_backsector->ceilingheight)
				continue;
			if (rover->norender == leveltime)
				continue;

			h = *rover->topheight;
			l = *rover->bottomheight;
			if (h > highcut)
				h = highcut;
			if (l < lowcut)
				l = lowcut;

			wallVerts[2].y = wallVerts[3].y = FIXED_TO_FLOAT(h);
			wallVerts[0].y = wallVerts[1].y = FIXED_TO_FLOAT(l);


#ifdef ESLOPE
			pslope_t *topslope = rover->master->frontsector->c_slope;
			pslope_t *bottomslope = rover->master->frontsector->f_slope;

			if (cv_grtest.value != 9)
			{
				// Adjust vertexes to slopes

				if (topslope)
				{
					wallVerts[3].y = P_GetZAtf(topslope, wallVerts[3].x, wallVerts[3].z);
					wallVerts[2].y = P_GetZAtf(topslope, wallVerts[2].x, wallVerts[2].z);
				}

				if (bottomslope)
				{
					// Top
					// Add hl for perfectly slanted walls!
					//wallVerts[3].y = P_GetZAtf(bottomslope, wallVerts[3].x, wallVerts[3].z)+FIXED_TO_FLOAT(h-l); // Hack for now ESLOPETODO
					//wallVerts[2].y = P_GetZAtf(bottomslope, wallVerts[2].x, wallVerts[2].z)+FIXED_TO_FLOAT(h-l);

					// Bottom
					wallVerts[0].y = P_GetZAtf(bottomslope, wallVerts[0].x, wallVerts[0].z);
					wallVerts[1].y = P_GetZAtf(bottomslope, wallVerts[1].x, wallVerts[1].z);
				}

			}
#endif


			if (rover->flags & FF_FOG)
			{
				// grTex is still NULL
				wallVerts[3].t = wallVerts[2].t = 0;
				wallVerts[0].t = wallVerts[1].t = 0;
				wallVerts[0].s = wallVerts[3].s = 0;
				wallVerts[2].s = wallVerts[1].s = 0;
			}
			else
			{
				grTex = HWR_GetTexture(texturetranslation[sides[rover->master->sidenum[0]].midtexture], true);

				line_t *newline = NULL;
				fixed_t offsetvalue;

				if (rover->master->flags & ML_TFERLINE) // SRB2CBTODO: Better method to do this than just line #?
				{
					int linenum = gr_curline->linedef - gr_backsector->lines[0]; // SRB2CBTODO: fixed_t?
					newline = rover->master->frontsector->lines[0] + linenum;
					grTex = HWR_GetTexture(texturetranslation[sides[newline->sidenum[0]].midtexture], true);
				}

				// If this linedef wall has a seperate linedef option, use its row offset too
				if (newline)
					offsetvalue = sides[newline->sidenum[0]].rowoffset;
				else
					offsetvalue = sides[rover->master->sidenum[0]].rowoffset;

				// Setup scrolling and texture coords
				wallVerts[3].t = wallVerts[2].t = (*rover->topheight - h + offsetvalue) * grTex->scaleY;
				wallVerts[0].t = wallVerts[1].t = (h - l + (*rover->topheight - h + offsetvalue)) * grTex->scaleY;
				wallVerts[0].s = wallVerts[3].s = cliplow * grTex->scaleX;
				wallVerts[2].s = wallVerts[1].s = cliphigh * grTex->scaleX;

#ifdef ESLOPE
				if (cv_grtest.value != 9)
				{
					if (topslope || bottomslope) // SRB2CBTODO: ESLOPE: Make this work with scrolling! & linedef option for this!
					{
						float texheight = HWR_GetTexture(texturetranslation[sides[rover->master->sidenum[0]].midtexture], true)->mipmap.height;

						// Kalaron: When we slope linedefs, we need to make the linedef textures matchup and not stretch with the linedef
						// All we have to do is change the vertical texture offsets (tow),
						// the horizontal offsets (sow) do not need to be changed
						// because only vertical heights are changed anyway when something is sloped
						float yoffset = offsetvalue;
						wallVerts[0].t = (float)((wallVerts[0].y / texheight) + yoffset);
						wallVerts[1].t = (float)((wallVerts[1].y / texheight) + yoffset);
						wallVerts[2].t = (float)((wallVerts[2].y / texheight) + yoffset);
						wallVerts[3].t = (float)((wallVerts[3].y / texheight) + yoffset);
					}
				}
#endif
			}

			if (rover->flags & FF_TRANSLUCENT)
			{
				if ((grTex) && ((grTex->mipmap.flags & TF_TRANSPARENT) || rover->alpha >= 243))
					blendmode = PF_Environment;
				else
					blendmode = PF_Translucent;

				Surf.FlatColor.s.alpha = rover->alpha;
			}
			else if (rover->flags & FF_FOG)
			{
				blendmode = PF_Translucent|PF_NoTexture;

				// In software mode, fog blocks are simply a colormapped area of the map,
				// they can never be visibly fully opaque, they use rover's master frontsector's
				// to modulate the transparency because software renders darker things more opqaue,
				// so basically, OpenGL mode can't render a colormaped area without polygons,
				// so use software's alpha darkness-colormap thingy method :P

				if (rover->master->frontsector->extra_colormap)
					Surf.FlatColor.rgba = HWR_Lighting(rover->master->frontsector->lightlevel, rover->master->frontsector->extra_colormap->rgba, false, false);
				else
					Surf.FlatColor.rgba = HWR_Lighting(rover->master->frontsector->lightlevel, NORMALFOG, false, false);

				Surf.FlatColor.s.alpha = rover->master->frontsector->lightlevel/1.4;

				// Important check, the game does not draw non-colored fog blocks at full brightness
				// these fog blocks are not drawn as part of special visual effects for certain levels
				// full brightness = 255 brightness
				// (0 alpha walls are automatically not rendered when passed to HWR_AddTransparentWall
				// No fog should occur with this, do the planes too
				if (!(rover->master->frontsector->extra_colormap && rover->master->frontsector->extra_colormap->fog))
				{
					if (rover->master->frontsector->lightlevel == 255)
						Surf.FlatColor.s.alpha = 0;
				}

			}
			else
				Surf.FlatColor.s.alpha = 255;

			if (rover->flags & FF_FOG)
				HWR_AddTransparentWall(wallVerts, &Surf, blendmode, 0, rover->master->frontsector, true); // HWR_AddNoTexWall
			else if (gr_backsector->numlights)
				HWR_SplitWall(gr_backsector, wallVerts, texturetranslation[sides[rover->master->sidenum[0]].midtexture], &Surf,
							  rover->flags, Surf.FlatColor.s.alpha);
			else
			{
				if ((blendmode & PF_Translucent) || (rover->flags & FF_TRANSLUCENT && (blendmode & PF_Environment) && (rover->alpha < 243)))
					HWR_AddTransparentWall(wallVerts, &Surf, blendmode, texturetranslation[sides[rover->master->sidenum[0]].midtexture], sector, false);
				else if (blendmode & PF_Environment)
					HWR_AddPeggedWall(wallVerts, &Surf, texturetranslation[sides[rover->master->sidenum[0]].midtexture]);
				else
					HWR_ProjectWall(wallVerts, &Surf, PF_Masked);
			}

		}
	}
#endif // R_FAKEFLOORS sides
}


// This renders one seg, which is a wall in the game that can have up to 3 layers (or infinite if you count FOFs)
// bottom, middle, and upper texture
// This doesn't need to take any arguments because
// the values it needs are globally defined in this hw_main.c
static void HWR_ProcessSeg(void) // Sort of like GLWall::Process in GZDoom
{
	wallVert3D wallVerts[4];

	static fixed_t worldtop, worldbottom, worldhigh, worldlow;
	static sector_t *worldtopsec, *worldbottomsec, *worldhighsec, *worldlowsec;

	fixed_t frontceil1; // worldtop1
	fixed_t frontfloor1; // worldbottom1
	fixed_t frontceil2; // worldtop2
	fixed_t frontfloor2; // worldbottom2

	GLTexture_t *grTex = NULL;
	float cliplow = 0.0f, cliphigh = 0.0f;
	int gr_midtexture;
	fixed_t h, l; // 3D sides and 2s middle textures

	FSurfaceInfo Surf;
	Surf.PolyFlags = 0;
	Surf.TexRotate = 0;
	Surf.PolyRotate = 0;
	Surf.TexScale = 0;
	FBITFIELD blendmode = PF_Masked;

	gr_sidedef = gr_curline->sidedef;
	gr_linedef = gr_curline->linedef;

	worldtop = gr_frontsector->ceilingheight;
	worldbottom = gr_frontsector->floorheight;
	worldtopsec = gr_frontsector;
	worldbottomsec = gr_frontsector;

	// x offset the texture
	fixed_t texturehpeg = gr_sidedef->textureoffset + gr_curline->offset;

	cliplow = (float)texturehpeg;

	cliphigh = texturehpeg + gr_curline->len;

	// Perform lighting and colormapping
	byte lightnum;
	lightnum = gr_frontsector->lightlevel;
	extracolormap_t *colormap;
	colormap = gr_frontsector->extra_colormap;
	Surf.FlatColor.s.alpha = 0xff;
	if (!gr_backsector)
	{
		if (colormap)
		{
			if (colormap->fog)
				Surf.FlatColor.rgba = HWR_Lighting(LightLevelToLum(lightnum), colormap->rgba, false, false);
			else
				Surf.FlatColor.rgba = HWR_Lighting(LightLevelToLum(lightnum), colormap->rgba, true, true);
		}
		else
			Surf.FlatColor.rgba = HWR_Lighting(LightLevelToLum(lightnum), NORMALFOG, false, false);
	}
	else // Two sided line
	{
		if (colormap)
		{
			if (colormap->fog)
				Surf.FlatColor.rgba = HWR_Lighting(LightLevelToLum(lightnum), colormap->rgba, false, false);
			else
				Surf.FlatColor.rgba = HWR_Lighting(LightLevelToLum(lightnum), colormap->rgba, true, true);
		}
		else
			Surf.FlatColor.rgba = HWR_Lighting(LightLevelToLum(lightnum), NORMALFOG, false, false);
	}

	v2d_t v1f, v2f;
	v1f.x = ((polyvertex_t *)gr_curline->v1)->x;
	v1f.y = ((polyvertex_t *)gr_curline->v1)->y;
	v2f.x = ((polyvertex_t *)gr_curline->v2)->x;
	v2f.y = ((polyvertex_t *)gr_curline->v2)->y;

	// remember vertices ordering
	//  3--2
	//  | /|
	//  |/ |
	//  0--1
	// make a wall polygon (with 2 triangles), using the floor/ceiling heights,
	// and the 2d map coords of the start/end vertices (z is not handled here)
	//
	// The top and the bottom should share the same x/z(y) coords
	wallVerts[0].x =
	wallVerts[3].x = v1f.x;
	wallVerts[0].z =
	wallVerts[3].z = v1f.y;
	wallVerts[2].x =
	wallVerts[1].x = v2f.x;
	wallVerts[2].z =
	wallVerts[1].z = v2f.y;
	wallVerts[0].w = wallVerts[1].w = wallVerts[2].w = wallVerts[3].w = 1.0f; // SRB2CBTODO: WHAT IS THIS?


	// Geometry setup, slope optimizations ala GZDoom
	// But we can't just copy the vertexes directly like GZDoom,
	// the precise coords of a vertex are adjusted when solving T-Joins
	// so gr_curline->v1 does not equal gr_curline->linedef->v1 !


	if ((cv_grtest.value != 9) && (gr_frontsector->f_slope && (gr_frontsector->f_slope->secplane.a | gr_frontsector->f_slope->secplane.b)))
	{
		frontfloor1=P_GetZAt(gr_frontsector->f_slope, FLOAT_TO_FIXED(wallVerts[0].x), FLOAT_TO_FIXED(wallVerts[0].z));
		frontfloor2=P_GetZAt(gr_frontsector->f_slope, FLOAT_TO_FIXED(wallVerts[1].x), FLOAT_TO_FIXED(wallVerts[1].z));
	}
	else
	{
		frontfloor1=frontfloor2= gr_frontsector->floorheight; // or secplane's -d
	}

	if ((cv_grtest.value != 9) && (gr_frontsector->c_slope && (gr_frontsector->c_slope->secplane.a | gr_frontsector->c_slope->secplane.b)))
	{
		frontceil1=P_GetZAt(gr_frontsector->c_slope, FLOAT_TO_FIXED(wallVerts[2].x), FLOAT_TO_FIXED(wallVerts[2].z));
		frontceil2=P_GetZAt(gr_frontsector->c_slope, FLOAT_TO_FIXED(wallVerts[3].x), FLOAT_TO_FIXED(wallVerts[3].z));
	}
	else
		frontceil1 = frontceil2 = gr_frontsector->ceilingheight;



	// Setup texture and polygon coordinates

	if (!gr_backsector || !(gr_linedef->flags & ML_TWOSIDED)) // Single sided linedef
	{
#ifdef SKYWALLO
		// Draw sky wall if there's no texture and this wall is part of the sky
		if (//!texturetranslation[gr_sidedef->midtexture]
			//&& gr_frontsector->floorheight >= gr_frontsector->ceilingheight
			//&&
			(gr_frontsector->floorpic == skyflatnum || gr_frontsector->ceilingpic == skyflatnum)
			)
		{
			wallVerts->t = 0;
			wallVerts->s = 0;
			wallVerts[2].y = wallVerts[3].y = FIXED_TO_FLOAT(worldtop);
			wallVerts[0].y = wallVerts[1].y = FIXED_TO_FLOAT(worldbottom);
			HWR_RenderSkyWall(wallVerts);
		}
#endif

		// Single sided lines only have a midtexture that gets rendered
		gr_midtexture = texturetranslation[gr_sidedef->midtexture];

		if (gr_midtexture)
		{
			// Texture
			fixed_t     texturevpeg;
			// PEGGING
			if (gr_linedef->flags & ML_DONTPEGBOTTOM)
				texturevpeg = gr_frontsector->floorheight + textureheight[gr_sidedef->midtexture];
			else
				texturevpeg = gr_sidedef->rowoffset;

			grTex = HWR_GetTexture(gr_midtexture, true);


			// Set the polygon coords
			wallVerts[2].y = FIXED_TO_FLOAT(frontceil1);
			wallVerts[3].y = FIXED_TO_FLOAT(frontceil2);
			wallVerts[0].y = FIXED_TO_FLOAT(frontfloor1);
			wallVerts[1].y = FIXED_TO_FLOAT(frontfloor2);


			// Set the texture coords
			wallVerts[3].t = wallVerts[2].t = texturevpeg * grTex->scaleY;
			wallVerts[0].t = wallVerts[1].t = (texturevpeg + worldtop - worldbottom) * grTex->scaleY;
			wallVerts[0].s = wallVerts[3].s = cliplow * grTex->scaleX;
			wallVerts[2].s = wallVerts[1].s = cliphigh * grTex->scaleX;

			if ((cv_grtest.value != 9) && (gr_frontsector->f_slope || gr_frontsector->c_slope))
			{
				float texheight = HWR_GetTexture(texturetranslation[gr_sidedef->midtexture], true)->mipmap.height;

				// Kalaron: When we slope linedefs, we need to make the linedef textures matchup and not stretch with the linedef
				// All we have to do is change the vertical texture offsets (tow),
				// the horizontal offsets (sow) do not need to be changed
				// because only vertical heights are changed anyway when something is sloped
				float yoffset = texturevpeg;
				wallVerts[0].t = (float)((wallVerts[0].y / texheight) + yoffset);
				wallVerts[1].t = (float)((wallVerts[1].y / texheight) + yoffset);
				wallVerts[2].t = (float)((wallVerts[2].y / texheight) + yoffset);
				wallVerts[3].t = (float)((wallVerts[3].y / texheight) + yoffset);
			}

			// Single sided lines can't use translucent walls
			if (gr_frontsector->numlights)
				HWR_SplitWall(gr_frontsector, wallVerts, gr_midtexture, &Surf, FF_CUTSOLIDS, 255);
			else
			{
				if (grTex->mipmap.flags & TF_TRANSPARENT)
					HWR_AddPeggedWall(wallVerts, &Surf, gr_midtexture);
				else
					HWR_ProjectWall(wallVerts, &Surf, PF_Masked);
			}
		}

	}
	else // Two sided linedef
	{
		worldhigh = gr_backsector->ceilingheight;
		worldlow = gr_backsector->floorheight;
		worldhighsec = gr_backsector;
		worldlowsec = gr_backsector;

		fixed_t backceil1; // worldhigh1
		fixed_t backfloor1; // worldlow1
		fixed_t backceil2; // worldhigh2
		fixed_t backfloor2; // worldlow2

#ifdef SKYWALLO
		// SRB2CBTODO: OH! You gotta do the same thing, except let everything in the back of this skywall's sector to render...
		// Draw sky wall if this wall is part of the sky
		if (gr_frontsector->ceilingpic == skyflatnum)
		{
			if (gr_backsector->ceilingpic == skyflatnum)
			{
				// if the back sector is closed the sky must be drawn!
				/*if (bs->ceilingplane.ZatPoint(v1) > bs->floorplane.ZatPoint(v1) ||
					bs->ceilingplane.ZatPoint(v2) > bs->floorplane.ZatPoint(v2) || bs->transdoor)
					return;*/
			}

			//if (gr_backsector->ceilingpic != skyflatnum)

			if (gr_backsector->floorheight >= gr_backsector->ceilingheight)
			{
			wallVerts->t = 0;
			wallVerts->s = 0;
			wallVerts[2].y = wallVerts[3].y = FIXED_TO_FLOAT(worldtop);
			wallVerts[0].y = wallVerts[1].y = FIXED_TO_FLOAT(worldbottom);
			HWR_RenderSkyWall(wallVerts);
			}
		}
#endif

		if ((cv_grtest.value != 9) && (gr_backsector->f_slope && (gr_backsector->f_slope->secplane.a | gr_backsector->f_slope->secplane.b)))
		{
			backfloor1=P_GetZAt(gr_backsector->f_slope, FLOAT_TO_FIXED(wallVerts[2].x), FLOAT_TO_FIXED(wallVerts[2].z));
			backfloor2=P_GetZAt(gr_backsector->f_slope, FLOAT_TO_FIXED(wallVerts[3].x), FLOAT_TO_FIXED(wallVerts[3].z));
		}
		else
		{
			backfloor1=backfloor2= gr_backsector->floorheight; // or secplane's -d
		}

		if ((cv_grtest.value != 9) && (gr_backsector->c_slope && (gr_backsector->c_slope->secplane.a | gr_backsector->c_slope->secplane.b)))
		{
			backceil1=P_GetZAt(gr_backsector->c_slope, FLOAT_TO_FIXED(wallVerts[0].x), FLOAT_TO_FIXED(wallVerts[0].z));
			backceil2=P_GetZAt(gr_backsector->c_slope, FLOAT_TO_FIXED(wallVerts[1].x), FLOAT_TO_FIXED(wallVerts[1].z));
		}
		else
			backceil1 = backceil2 = gr_backsector->ceilingheight;


		// check TOP TEXTURE
		if (gr_frontsector->ceilingpic != skyflatnum || gr_backsector->ceilingpic != skyflatnum)
		{
			fixed_t backceil1a=backceil1, backceil2a=backceil2;

			if (gr_frontsector->floorpic != skyflatnum || gr_backsector->floorpic != skyflatnum)
			{
				// the back sector's floor obstructs part of this wall
				if (frontfloor1>backceil1 && frontfloor2>backceil2)
				{
					backceil2a=frontfloor2;
					backceil1a=frontfloor1;
				}
			}

			if (backceil1a<frontceil1 || backceil2a<frontceil2)
			{
				// Vertical pegging of the linedef, used for rendering texture coords
				fixed_t texturevpegtop;

				if ((gr_linedef->flags & (ML_DONTPEGTOP) && (gr_linedef->flags & ML_DONTPEGBOTTOM))
					&& gr_linedef->sidenum[1] != 0xffff)
				{
					// Special case... use offsets from 2nd side but only if it has a texture.
					side_t *def = &sides[gr_linedef->sidenum[1]];

					if (!texturetranslation[def->toptexture]) // Second side has no texture, use the first side's instead
					{
						if (texturetranslation[gr_sidedef->toptexture])
							grTex = HWR_GetTexture(texturetranslation[gr_sidedef->toptexture], true);
						else if ((cv_grtest.value != 9) && (((gr_frontsector->c_slope && (gr_frontsector->c_slope->secplane.a | gr_frontsector->c_slope->secplane.b)) // Special slope check!
								  || (gr_backsector->c_slope && (gr_backsector->c_slope->secplane.a | gr_backsector->c_slope->secplane.b)))
								 && !(gr_frontsector->ceilingpic == skyflatnum || gr_backsector->ceilingpic == skyflatnum)))
						{
							grTex = HWR_GetTexture(R_TextureNumForName("GFZROCK", (USHORT)(&sides[gr_linedef->sidenum[0]] - sides)), true); // SRB2CBTODO: Special grayed-out texture!
							//HWR_GetFlat(levelflats[gr_backsector->ceilingpic].lumpnum, true); // ESLOPE: Use the flat of the sector instead?
						}
						else
							grTex = NULL;
					}
					else
					{
						if (texturetranslation[def->toptexture])
							grTex = HWR_GetTexture(texturetranslation[def->toptexture], true); // otherwise, use the special
						else if ((cv_grtest.value != 9) && (((gr_frontsector->c_slope && (gr_frontsector->c_slope->secplane.a | gr_frontsector->c_slope->secplane.b)) // Special slope check!
								  || (gr_backsector->c_slope && (gr_backsector->c_slope->secplane.a | gr_backsector->c_slope->secplane.b)))
								 && !(gr_frontsector->ceilingpic == skyflatnum || gr_backsector->ceilingpic == skyflatnum)))
						{
							grTex = HWR_GetTexture(R_TextureNumForName("GFZROCK", (USHORT)(&sides[gr_linedef->sidenum[0]] - sides)), true); // SRB2CBTODO: Special grayed-out texture!
							//HWR_GetFlat(levelflats[gr_backsector->ceilingpic].lumpnum, true); // ESLOPE: Use the flat of the sector instead?
						}
						else
							grTex = NULL;
					}

					if (gr_linedef->flags & ML_DONTPEGTOP)
						texturevpegtop = 0;
					else
						texturevpegtop = gr_backsector->ceilingheight + textureheight[def->toptexture];
				}
				else
				{
					if (texturetranslation[gr_sidedef->toptexture])
						grTex = HWR_GetTexture(texturetranslation[gr_sidedef->toptexture], true);
					else if ((cv_grtest.value != 9) && (((gr_frontsector->c_slope && (gr_frontsector->c_slope->secplane.a | gr_frontsector->c_slope->secplane.b)) // Special slope check!
							  || (gr_backsector->c_slope && (gr_backsector->c_slope->secplane.a | gr_backsector->c_slope->secplane.b)))
							 && !(gr_frontsector->ceilingpic == skyflatnum || gr_backsector->ceilingpic == skyflatnum)))
					{
						grTex = HWR_GetTexture(R_TextureNumForName("GFZROCK", (USHORT)(&sides[gr_linedef->sidenum[0]] - sides)), true); // SRB2CBTODO: Special grayed-out texture!
						//HWR_GetFlat(levelflats[gr_backsector->ceilingpic].lumpnum, true); // ESLOPE: Use the flat of the sector instead?
					}
					else
						grTex = NULL;

					// PEGGING
					if (gr_linedef->flags & ML_DONTPEGTOP)
						texturevpegtop = 0;
					else
						// This is used for things like crushers and stuff
						texturevpegtop = worldhigh + textureheight[gr_sidedef->toptexture] - worldtop;
				}

				texturevpegtop += gr_sidedef->rowoffset;

				if (grTex)
				{
					wallVerts[3].t = wallVerts[2].t = texturevpegtop * grTex->scaleY;
					wallVerts[0].t = wallVerts[1].t = (texturevpegtop + worldtop - worldhigh) * grTex->scaleY;
					wallVerts[0].s = wallVerts[3].s = cliplow * grTex->scaleX;
					wallVerts[2].s = wallVerts[1].s = cliphigh * grTex->scaleX;

					// set top/bottom coords
					wallVerts[2].y = FIXED_TO_FLOAT(frontceil1);
					wallVerts[3].y = FIXED_TO_FLOAT(frontceil2);
					wallVerts[0].y = FIXED_TO_FLOAT(backceil1);
					wallVerts[1].y = FIXED_TO_FLOAT(backceil2);
#ifdef ESLOPE

					if ((cv_grtest.value != 9) && (gr_frontsector->c_slope || gr_backsector->c_slope))
					{
						float texheight = HWR_GetTexture(texturetranslation[gr_sidedef->toptexture], true)->mipmap.height;

						// Kalaron: When we slope linedefs, we need to make the linedef textures matchup and not stretch with the linedef
						// All we have to do is change the vertical texture offsets (tow),
						// the horizontal offsets (sow) do not need to be changed
						// because only vertical heights are changed anyway when something is sloped
						float yoffset = FIXED_TO_FLOAT(gr_sidedef->textureoffset + gr_curline->offset);
						wallVerts[0].t = (float)((wallVerts[0].y / texheight) + yoffset);
						wallVerts[1].t = (float)((wallVerts[1].y / texheight) + yoffset);
						wallVerts[2].t = (float)((wallVerts[2].y / texheight) + yoffset);
						wallVerts[3].t = (float)((wallVerts[3].y / texheight) + yoffset);
					}
#endif

					if (gr_frontsector->numlights)
						HWR_SplitWall(gr_frontsector, wallVerts, texturetranslation[gr_sidedef->toptexture], &Surf, FF_CUTSOLIDS, 255);
					else if (grTex->mipmap.flags & TF_TRANSPARENT)
						HWR_AddPeggedWall(wallVerts, &Surf, texturetranslation[gr_sidedef->toptexture]);
					else
						HWR_ProjectWall(wallVerts, &Surf, PF_Masked);

				} // grtex




			}
		}

		// check BOTTOM TEXTURE

#if 0 // Doesn't really work right for some reason (It's probably because on of these wallVerts vertexes REFUSE TO WORK!
		// the back sector's ceiling obstructs part of this wall (specially important for sky sectors)
		if (frontceil1 < backfloor1 && frontceil2 < backfloor2)
		{
			backfloor1=frontceil1;
			backfloor2=frontceil2;
		}
#endif

		if (backfloor1>frontfloor1 || backfloor2>frontfloor2)
		{
			// worldlow    // Top of this texture
			// worldbottom // Bottom of this texture

			fixed_t texturevpegbottom = 0; // bottom

			if (texturetranslation[gr_sidedef->bottomtexture])
				grTex = HWR_GetTexture(texturetranslation[gr_sidedef->bottomtexture], true);
			else if ((cv_grtest.value != 9) && (((gr_frontsector->f_slope && (gr_frontsector->f_slope->secplane.a | gr_frontsector->f_slope->secplane.b)) // Special slope check!
					 || (gr_backsector->f_slope && (gr_backsector->f_slope->secplane.a | gr_backsector->f_slope->secplane.b)))
				&& !(gr_frontsector->floorpic == skyflatnum || gr_backsector->floorpic == skyflatnum)))
			{
				// Yeah, we CAN'T do a replace for textures here, this doesn't work with some crazy maps :P
				grTex = HWR_GetTexture(R_TextureNumForName("GFZROCK", (USHORT)(&sides[gr_linedef->sidenum[0]] - sides)), true); // SRB2CBTODO: Special grayed-out texture!
				//HWR_GetFlat(levelflats[gr_backsector->floorpic].lumpnum, true); // ESLOPE: Use the flat of the sector instead?
			}
			else
				grTex = NULL;

			// PEGGING // SRB2CBTODO: This would have to be done for each point
			if (gr_linedef->flags & ML_DONTPEGBOTTOM)
				texturevpegbottom = worldtop - worldlow;
			else
				texturevpegbottom = 0;

			texturevpegbottom += gr_sidedef->rowoffset;

			if (grTex)
			{
				// worldlow = gr_backsector->floorheight
				// worldbottom = gr_frontsector->floorheight
				// worldhigh = gr_backsector->ceilingheight
				// worldtop = gr_frontsector->ceilingheight

				// worldlow    // Top of this texture
				// worldbottom // Bottom of this texture

				// Vertical
				wallVerts[3].t = wallVerts[2].t = texturevpegbottom * grTex->scaleY;
				wallVerts[0].t = wallVerts[1].t = (texturevpegbottom + worldlow - worldbottom) * grTex->scaleY;

				// Horizontal
				wallVerts[0].s = wallVerts[3].s = cliplow * grTex->scaleX;
				wallVerts[2].s = wallVerts[1].s = cliphigh * grTex->scaleX;


				// Set default top/bottom coords
				wallVerts[2].y = FIXED_TO_FLOAT(backfloor1);
				wallVerts[3].y = FIXED_TO_FLOAT(backfloor2);
				wallVerts[0].y = FIXED_TO_FLOAT(frontfloor1);
				wallVerts[1].y = FIXED_TO_FLOAT(frontfloor2);

				// SRB2CBTODO: wallVerts[0].y is a sticky vertex and wallVerts[1].y doesn't work, WHY?!!!

#ifdef ESLOPE
				if (cv_grtest.value == 10 || (gr_backsector->f_slope || gr_frontsector->f_slope)) // SRB2TODO: don't make this default? (It's awesome)
				{
					float texheight = HWR_GetTexture(texturetranslation[gr_sidedef->bottomtexture], true)->mipmap.height;

					// Kalaron: When we slope linedefs, we need to make the linedef textures matchup and not stretch with the linedef
					// All we have to do is change the vertical texture offsets (tow),
					// the horizontal offsets (sow) do not need to be changed
					// because only vertical heights are changed anyway when something is sloped
					wallVerts[0].t = (float)(wallVerts[0].y / texheight);
					wallVerts[1].t = (float)(wallVerts[1].y / texheight);

					wallVerts[2].t = (float)(wallVerts[2].y / texheight);
					wallVerts[3].t = (float)(wallVerts[3].y / texheight);
				}
#endif

				if (gr_frontsector->numlights)
					HWR_SplitWall(gr_frontsector, wallVerts, texturetranslation[gr_sidedef->bottomtexture], &Surf, FF_CUTSOLIDS, 255);
				else if (grTex->mipmap.flags & TF_TRANSPARENT)
					HWR_AddPeggedWall(wallVerts, &Surf, texturetranslation[gr_sidedef->bottomtexture]);
				else
					HWR_ProjectWall(wallVerts, &Surf, PF_Masked);
			} // grTex


		}

		gr_midtexture = texturetranslation[gr_sidedef->midtexture];

		if (gr_midtexture)
		{
			fixed_t  popentop, popenbottom, polytop, polybottom;
			fixed_t     texturevpeg = 0;

			popentop = worldtop < worldhigh ? worldtop : worldhigh;
			popenbottom = worldbottom > worldlow ? worldbottom : worldlow;

#ifdef POLYOBJECTS
			// NOTE: With polyobjects, whenever you need to check the properties of the polyobject sector it belongs to,
			// you must use the linedef's backsector to be correct
			if (gr_curline->polyseg)
			{
				popentop = gr_linedef->backsector->ceilingheight;
				popenbottom = gr_linedef->backsector->floorheight;
			}
#endif

			// The linedef has a special 'lower unpegged' flag
			if (gr_linedef->flags & ML_DONTPEGBOTTOM)
			{
				polybottom = popenbottom + gr_sidedef->rowoffset;
				polytop = polybottom + textureheight[gr_midtexture];
			}
			else
			{
				polytop = popentop + gr_sidedef->rowoffset;
				polybottom = polytop - textureheight[gr_midtexture];
			}

			h = polytop;
			l = polybottom;

			// The cut-off values of a linedef can always be constant, since every line has an absoulute front and or back sector
			fixed_t lowcut = gr_frontsector->floorheight > gr_backsector->floorheight ? gr_frontsector->floorheight : gr_backsector->floorheight;
			fixed_t highcut = gr_frontsector->ceilingheight < gr_backsector->ceilingheight ? gr_frontsector->ceilingheight : gr_backsector->ceilingheight;

			//sector_t *lowcutsec = gr_frontsector->floorheight > gr_backsector->floorheight ? gr_frontsector : gr_backsector; // ESLOPE: Support cutoff for slopes? // YESS we need support!!!
			//sector_t *highcutsec = gr_frontsector->ceilingheight < gr_backsector->ceilingheight ? gr_frontsector : gr_backsector;

			if (h > highcut) // Cut the texture so it doesn't overlap with any textures above it
				h = highcut;
			if (l < lowcut) // Cut the texture so it doesn't overlap with any textures below it
				l = lowcut;

			// Handle other pegging including the texture repeat feature
			if (gr_linedef->flags & ML_DONTPEGBOTTOM)
				texturevpeg = polybottom + textureheight[gr_sidedef->midtexture] - h;
			else
				texturevpeg = polytop - h;

			// This determines how many times to repeat drawing this linedef vertically
			// It's normally one unless a linedef has a specific repeat count
			int repeats;

			// Calculate the repeat count
			if (gr_sidedef->repeatcnt)
                repeats = 1 + gr_sidedef->repeatcnt;
			else if (gr_linedef->flags & ML_EFFECT5)
			{
				fixed_t high, low;

				if (gr_frontsector->ceilingheight > gr_backsector->ceilingheight)
					high = gr_backsector->ceilingheight;
				else
					high = gr_frontsector->ceilingheight;

				if (gr_frontsector->floorheight > gr_backsector->floorheight)
					low = gr_frontsector->floorheight;
				else
					low = gr_backsector->floorheight;

				repeats = (high - low)/textureheight[gr_sidedef->midtexture];
			}
			else
			    repeats = 1;

			grTex = HWR_GetTexture(gr_midtexture, false);

			// Draw the coords of the polygon
			wallVerts[2].y = wallVerts[3].y = FIXED_TO_FLOAT(h);
			wallVerts[0].y = wallVerts[1].y = FIXED_TO_FLOAT(l);

			// ESLOPE: Can middle textures be sloped?
#ifdef ESLOPE
			if (cv_grtest.value != 9)
			{
				// Adjust vertexes to slopes
				if (gr_backsector->f_slope)
				{
					// Top
					wallVerts[3].y = P_GetZAtf(gr_backsector->f_slope, wallVerts[3].x, wallVerts[3].z)+FIXED_TO_FLOAT(h-l);
					wallVerts[2].y = P_GetZAtf(gr_backsector->f_slope, wallVerts[2].x, wallVerts[2].z)+FIXED_TO_FLOAT(h-l);

					// Bottom
					wallVerts[0].y = P_GetZAtf(gr_backsector->f_slope, wallVerts[0].x, wallVerts[0].z);
					wallVerts[1].y = P_GetZAtf(gr_backsector->f_slope, wallVerts[1].x, wallVerts[1].z);
				}
				// Adjust vertexes to slopes
				/*if (gr_frontsector->f_slope)
				{
					// Top
					wallVerts[3].y = P_GetZAtf(gr_frontsector->f_slope, wallVerts[3].x, wallVerts[3].z)+FIXED_TO_FLOAT(h-l);
					wallVerts[2].y = P_GetZAtf(gr_frontsector->f_slope, wallVerts[2].x, wallVerts[2].z)+FIXED_TO_FLOAT(h-l);

					// Bottom
					wallVerts[0].y = P_GetZAtf(gr_frontsector->f_slope, wallVerts[0].x, wallVerts[0].z);
					wallVerts[1].y = P_GetZAtf(gr_frontsector->f_slope, wallVerts[1].x, wallVerts[1].z);
				}*/
				if (gr_backsector->c_slope)
				{
					// Top
					wallVerts[2].y = P_GetZAtf(gr_backsector->c_slope, wallVerts[2].x, wallVerts[2].z);
					wallVerts[3].y = P_GetZAtf(gr_backsector->c_slope, wallVerts[3].x, wallVerts[3].z);

					// Bottom
					wallVerts[0].y = P_GetZAtf(gr_backsector->c_slope, wallVerts[0].x, wallVerts[0].z)-FIXED_TO_FLOAT(h-l);
					wallVerts[1].y = P_GetZAtf(gr_backsector->c_slope, wallVerts[1].x, wallVerts[1].z)-FIXED_TO_FLOAT(h-l);
				}
				if (gr_frontsector->c_slope)
				{
					// Top
					wallVerts[2].y = P_GetZAtf(gr_frontsector->c_slope, wallVerts[2].x, wallVerts[2].z);
					wallVerts[3].y = P_GetZAtf(gr_frontsector->c_slope, wallVerts[3].x, wallVerts[3].z);

					// Bottom
					wallVerts[0].y = P_GetZAtf(gr_frontsector->c_slope, wallVerts[0].x, wallVerts[0].z)-FIXED_TO_FLOAT(h-l);
					wallVerts[1].y = P_GetZAtf(gr_frontsector->c_slope, wallVerts[1].x, wallVerts[1].z)-FIXED_TO_FLOAT(h-l);
				}

				if (gr_frontsector->f_slope || gr_backsector->f_slope || gr_frontsector->c_slope || gr_backsector->c_slope)
				{
					float texheight = HWR_GetTexture(texturetranslation[gr_sidedef->bottomtexture], true)->mipmap.height;

					// Kalaron: When we slope linedefs, we need to make the linedef textures matchup and not stretch with the linedef
					// All we have to do is change the vertical texture offsets (tow),
					// the horizontal offsets (sow) do not need to be changed
					// because only vertical heights are changed anyway when something is sloped
					float yoffset = FIXED_TO_FLOAT(gr_sidedef->textureoffset + gr_curline->offset);
					wallVerts[0].t = (float)((wallVerts[0].y / texheight) + yoffset);
					wallVerts[1].t = (float)((wallVerts[1].y / texheight) + yoffset);
					wallVerts[2].t = (float)((wallVerts[2].y / texheight) + yoffset);
					wallVerts[3].t = (float)((wallVerts[3].y / texheight) + yoffset);
				}
			}
#endif

			wallVerts[3].t = wallVerts[2].t = texturevpeg * grTex->scaleY;
			wallVerts[0].t = wallVerts[1].t = (h - l + texturevpeg) * grTex->scaleY;
			wallVerts[0].s = wallVerts[3].s = cliplow * grTex->scaleX;
			wallVerts[2].s = wallVerts[1].s = cliphigh * grTex->scaleX;

			if (grTex->mipmap.flags & TF_TRANSPARENT)
				blendmode = PF_Environment;

			blendmode |= PF_RemoveYWrap;

			if (gr_frontsector->numlights)
				HWR_SplitWall(gr_frontsector, wallVerts, gr_midtexture, &Surf, FF_CUTSOLIDS, 255);
			else if (blendmode & PF_Environment)
				HWR_AddPeggedWall(wallVerts, &Surf, gr_midtexture);
			//else if (blendmode & PF_Translucent)
			//	HWR_AddTransparentWall(wallVerts, &Surf, gr_midtexture, 0, sector, false);
			else
				HWR_ProjectWall(wallVerts, &Surf, blendmode);

			// If the linedef's draw repeat is greater than 1, draw the repeated walls
			if (repeats > 1) // SRB2CBTODO: draw the last wall but cut it to any wall and things
			{
				int repeatsleft;

				for (repeatsleft = repeats; repeatsleft > 1; repeatsleft--)
				{
					// Redraw the linedef, keep drawing the same linedef right below the
					// previous one until you reached the proper repeat count
					fixed_t hi, lo;
					hi = h;
					lo = l;
					hi -= (textureheight[gr_sidedef->midtexture]) *(repeatsleft - 1) + gr_sidedef->rowoffset;
					lo -= (textureheight[gr_sidedef->midtexture]) *(repeatsleft - 1) + gr_sidedef->rowoffset;

					if (hi > highcut) // Skip rendering any more texture repeats if they will overlap with any textures above them
						continue;
					if (lo < lowcut) // Skip rendering any more texture repeats if they will overlap with any textures below them
						continue;

					wallVerts[2].y = wallVerts[3].y = FIXED_TO_FLOAT(hi);
					wallVerts[0].y = wallVerts[1].y = FIXED_TO_FLOAT(lo);
#ifdef ESLOPE
					if (cv_grtest.value != 9)
					{
						// Adjust vertexes to slopes
						if (gr_backsector->f_slope)
						{
							// Top
							wallVerts[3].y = P_GetZAtf(gr_backsector->f_slope, wallVerts[3].x, wallVerts[3].z)+FIXED_TO_FLOAT(h-l);
							wallVerts[2].y = P_GetZAtf(gr_backsector->f_slope, wallVerts[2].x, wallVerts[2].z)+FIXED_TO_FLOAT(h-l);

							// Bottom
							wallVerts[0].y = P_GetZAtf(gr_backsector->f_slope, wallVerts[0].x, wallVerts[0].z);
							wallVerts[1].y = P_GetZAtf(gr_backsector->f_slope, wallVerts[1].x, wallVerts[1].z);
						}
						if (gr_backsector->c_slope)
						{
							// Top
							wallVerts[2].y = P_GetZAtf(gr_backsector->c_slope, wallVerts[2].x, wallVerts[2].z);
							wallVerts[3].y = P_GetZAtf(gr_backsector->c_slope, wallVerts[3].x, wallVerts[3].z);

							// Bottom
							wallVerts[0].y = P_GetZAtf(gr_backsector->c_slope, wallVerts[0].x, wallVerts[0].z)-FIXED_TO_FLOAT(h-l);
							wallVerts[1].y = P_GetZAtf(gr_backsector->c_slope, wallVerts[1].x, wallVerts[1].z)-FIXED_TO_FLOAT(h-l);
						}
						// Adjust vertexes to slopes
						/*if (gr_frontsector->f_slope)
						{
							// Top
							wallVerts[3].y = P_GetZAtf(gr_frontsector->f_slope, wallVerts[3].x, wallVerts[3].z)+FIXED_TO_FLOAT(h-l);
							wallVerts[2].y = P_GetZAtf(gr_frontsector->f_slope, wallVerts[2].x, wallVerts[2].z)+FIXED_TO_FLOAT(h-l);

							// Bottom
							wallVerts[0].y = P_GetZAtf(gr_frontsector->f_slope, wallVerts[0].x, wallVerts[0].z);
							wallVerts[1].y = P_GetZAtf(gr_frontsector->f_slope, wallVerts[1].x, wallVerts[1].z);
						}*/
						if (gr_frontsector->c_slope)
						{
							// Top
							wallVerts[2].y = P_GetZAtf(gr_frontsector->c_slope, wallVerts[2].x, wallVerts[2].z);
							wallVerts[3].y = P_GetZAtf(gr_frontsector->c_slope, wallVerts[3].x, wallVerts[3].z);

							// Bottom
							wallVerts[0].y = P_GetZAtf(gr_frontsector->c_slope, wallVerts[0].x, wallVerts[0].z)-FIXED_TO_FLOAT(h-l);
							wallVerts[1].y = P_GetZAtf(gr_frontsector->c_slope, wallVerts[1].x, wallVerts[1].z)-FIXED_TO_FLOAT(h-l);
						}

						if (gr_frontsector->f_slope || gr_backsector->f_slope || gr_frontsector->c_slope || gr_backsector->c_slope)
						{
							float texheight = HWR_GetTexture(texturetranslation[gr_sidedef->bottomtexture], true)->mipmap.height;

							// Kalaron: When we slope linedefs, we need to make the linedef textures matchup and not stretch with the linedef
							// All we have to do is change the vertical texture offsets (tow),
							// the horizontal offsets (sow) do not need to be changed
							// because only vertical heights are changed anyway when something is sloped
							float yoffset = FIXED_TO_FLOAT(gr_sidedef->textureoffset + gr_curline->offset);
							wallVerts[0].t = (float)((wallVerts[0].y / texheight) + yoffset);
							wallVerts[1].t = (float)((wallVerts[1].y / texheight) + yoffset);
							wallVerts[2].t = (float)((wallVerts[2].y / texheight) + yoffset);
							wallVerts[3].t = (float)((wallVerts[3].y / texheight) + yoffset);
						}
					}
#endif

					wallVerts[3].t = wallVerts[2].t = texturevpeg * grTex->scaleY;
					wallVerts[0].t = wallVerts[1].t = (h - l + texturevpeg) * grTex->scaleY;
					wallVerts[0].s = wallVerts[3].s = cliplow * grTex->scaleX;
					wallVerts[2].s = wallVerts[1].s = cliphigh * grTex->scaleX;

					// Render the extra wall repeats here
					if (grTex->mipmap.flags & TF_TRANSPARENT)
						blendmode = PF_Environment;

					blendmode |= PF_RemoveYWrap;

					if (gr_frontsector->numlights)
						HWR_SplitWall(gr_frontsector, wallVerts, gr_midtexture, &Surf, FF_CUTSOLIDS, 255);
					else if (blendmode & PF_Environment)
						HWR_AddPeggedWall(wallVerts, &Surf, gr_midtexture);
					else
						HWR_ProjectWall(wallVerts, &Surf, blendmode);
				}
			}

		}
	}


	// Render any FOFs along this sector
	if (gr_frontsector && gr_backsector
		&& gr_frontsector->tag != gr_backsector->tag
		&& (gr_backsector->ffloors || gr_frontsector->ffloors))
	{
		// XZ coords of the vertexes are calculated above, so we can keep using them,
		// the vertical Y coords will be modified in this function
		HWR_FoFSeg(wallVerts, Surf);
	}



}

// From PrBoom:
//
// e6y: Check whether the player can look beyond this line
//
#ifdef NEWCLIP
// Don't modify anything here, just check
// Kalaron: Modified for sloped linedefs
static boolean CheckClip(seg_t * seg, sector_t * afrontsector, sector_t * abacksector)
{
	static sector_t tempsec_back, tempsec_front;

	abacksector = R_FakeFlat(abacksector, &tempsec_back, NULL, NULL, true);
	afrontsector = R_FakeFlat(afrontsector, &tempsec_front, NULL, NULL, false);

	line_t *linedef = seg->linedef;
	fixed_t bs_floorheight1;
	fixed_t bs_floorheight2;
	fixed_t bs_ceilingheight1;
	fixed_t bs_ceilingheight2;
	fixed_t fs_floorheight1;
	fixed_t fs_floorheight2;
	fixed_t fs_ceilingheight1;
	fixed_t fs_ceilingheight2;

	// GZDoom method of sloped line clipping

	if (afrontsector->c_slope && (afrontsector->c_slope->secplane.a | afrontsector->c_slope->secplane.b))
	{
		fs_ceilingheight1=P_GetZAt(afrontsector->c_slope, linedef->v1->x, linedef->v1->y);
		fs_ceilingheight2=P_GetZAt(afrontsector->c_slope, linedef->v2->x, linedef->v2->y);
	}
	else
	{
		fs_ceilingheight2=fs_ceilingheight1= afrontsector->ceilingheight; // or secplane's d
	}

	if (afrontsector->f_slope && (afrontsector->f_slope->secplane.a | afrontsector->f_slope->secplane.b))
	{
		fs_floorheight1=P_GetZAt(afrontsector->f_slope, linedef->v1->x, linedef->v1->y);
		fs_floorheight2=P_GetZAt(afrontsector->f_slope, linedef->v2->x, linedef->v2->y);
	}
	else
	{
		fs_floorheight2=fs_floorheight1= afrontsector->floorheight; // or secplane's -d
	}

	if (abacksector->c_slope && (abacksector->c_slope->secplane.a | abacksector->c_slope->secplane.b))
	{
		bs_ceilingheight1=P_GetZAt(abacksector->c_slope, linedef->v1->x, linedef->v1->y);
		bs_ceilingheight2=P_GetZAt(abacksector->c_slope, linedef->v2->x, linedef->v2->y);
	}
	else
	{
		bs_ceilingheight2=bs_ceilingheight1= abacksector->ceilingheight;
	}

	if (abacksector->f_slope && (abacksector->f_slope->secplane.a | abacksector->f_slope->secplane.b))
	{
		bs_floorheight1=P_GetZAt(abacksector->f_slope, linedef->v1->x, linedef->v1->y);
		bs_floorheight2=P_GetZAt(abacksector->f_slope, linedef->v2->x, linedef->v2->y);
	}
	else
	{
		bs_floorheight2=bs_floorheight1= abacksector->floorheight; // or secplanes's -d
	}

	// now check for closed sectors!
	if (bs_ceilingheight1<=fs_floorheight1 && bs_ceilingheight2<=fs_floorheight2)
	{
		if (!seg->sidedef->toptexture)
			return false;

		if (abacksector->ceilingpic == skyflatnum && afrontsector->ceilingpic == skyflatnum)
			return false;

		return true;
	}

	if (fs_ceilingheight1<=bs_floorheight1 && fs_ceilingheight2<=bs_floorheight2)
	{
		if (!seg->sidedef->bottomtexture)
			return false;

		// properly render skies (consider door "open" if both floors are sky):
		if (abacksector->ceilingpic == skyflatnum && afrontsector->ceilingpic == skyflatnum)
			return false;

		return true;
	}

	if (bs_ceilingheight1<=bs_floorheight1 && bs_ceilingheight2<=bs_floorheight2)
	{
		// preserve a kind of transparent door/lift special effect:
		if (bs_ceilingheight1 < fs_ceilingheight1 || bs_ceilingheight2 < fs_ceilingheight2)
		{
			if (!seg->sidedef->toptexture)
				return false;
		}
		if (bs_floorheight1 > fs_floorheight1 || bs_floorheight2 > fs_floorheight2)
		{
			if (!seg->sidedef->bottomtexture)
				return false;
		}
		if (abacksector->ceilingpic == skyflatnum && afrontsector->ceilingpic == skyflatnum)
			return false;

		if (abacksector->floorpic == skyflatnum && afrontsector->floorpic == skyflatnum)
			return false;

		return true;
	}


	// Reject empty lines used for triggers and special events.
	// Identical floor and ceiling on both sides,
	//  identical light levels on both sides,
	//  and no middle texture.
	if (seg->linedef->flags & ML_EFFECT6) // Don't even draw these lines
		return false;

	if (
#ifdef POLYOBJECTS
		!seg->polyseg &&
#endif
		gr_backsector->ceilingpic == gr_frontsector->ceilingpic
		&& gr_backsector->floorpic == gr_frontsector->floorpic
		&& gr_backsector->lightlevel == gr_frontsector->lightlevel
		&& !gr_curline->sidedef->midtexture
		// Check offsets too!
		&& gr_backsector->floor_xoffs == gr_frontsector->floor_xoffs
		&& gr_backsector->floor_yoffs == gr_frontsector->floor_yoffs
		&& gr_backsector->floorpic_angle == gr_frontsector->floorpic_angle
		&& gr_backsector->floor_scale == gr_frontsector->floor_scale
		&& gr_backsector->ceiling_xoffs == gr_frontsector->ceiling_xoffs
		&& gr_backsector->ceiling_yoffs == gr_frontsector->ceiling_yoffs
		&& gr_backsector->ceilingpic_angle == gr_frontsector->ceilingpic_angle
		&& gr_backsector->ceiling_scale == gr_frontsector->ceiling_scale
		// Consider altered lighting.
		&& gr_backsector->floorlightsec == gr_frontsector->floorlightsec
		&& gr_backsector->ceilinglightsec == gr_frontsector->ceilinglightsec
		// Consider slopes
#ifdef ESLOPE
		&& gr_backsector->f_slope == gr_frontsector->f_slope
		&& gr_backsector->c_slope == gr_frontsector->c_slope
#endif
		// Consider colormaps
		&& gr_backsector->extra_colormap == gr_frontsector->extra_colormap
		&& ((!gr_frontsector->ffloors && !gr_backsector->ffloors)
			|| gr_frontsector->tag == gr_backsector->tag))
	{
		return false;
	}


	return false;
}
#endif

// -----------------+
// HWR_AddLine      : Clips the given segment and adds any visible pieces to the line list.
// -----------------+
// Kalaron: PrBoom extreme optimization!
static void HWR_AddLine(seg_t * line) // SRB2CBTODO: Extra line clipping happens? // SRB2CBTODO: Crash when PNG is used TX_START
{
	angle_t  angle1;
	angle_t  angle2;
	static sector_t tempsec;

#ifdef POLYOBJECTS
	if (line->polyseg && !(line->polyseg->flags & POF_RENDERSIDES))
		return;
#endif

	gr_curline = line;

	fixed_t
	lx1 = (fixed_t)(((polyvertex_t *)gr_curline->v1)->x*FRACUNIT),
	lx2 = (fixed_t)(((polyvertex_t *)gr_curline->v2)->x*FRACUNIT),
	ly1 = (fixed_t)(((polyvertex_t *)gr_curline->v1)->y*FRACUNIT),
	ly2 = (fixed_t)(((polyvertex_t *)gr_curline->v2)->y*FRACUNIT);

	angle1 = R_PointToAngle(lx1, ly1);
	angle2 = R_PointToAngle(lx2, ly2);

	 // PrBoom: Back side, i.e. backface culling - read: endAngle >= startAngle!
	if (angle2 - angle1 < ANG180)
		return;

	// PrBoom: use REAL clipping math YAYYYYYYY!!!

	if (!gld_clipper_SafeCheckRange(angle2, angle1))
    {
		return;
    }

	// Important difference from PrBoom's code,
	// make sure that you set the backsector to be the line's backsector even if it's NULL!
	// Set the backsector so the line can render!
	gr_backsector = line->backsector;

	// SRB2CBTODO: Some strange subsector thing goes here

	if (!line->backsector)
    {
		gld_clipper_SafeAddClipRange(angle2, angle1);
    }
    else
    {
		gr_backsector = R_FakeFlat(gr_backsector, &tempsec, NULL, NULL, true); // For those sector spikes ;)
		if (line->frontsector == line->backsector)
		{
			if (!line->sidedef->midtexture)
			{
				//e6y: nothing to do here!
				//return;
			}
		}
		if (CheckClip(line, line->frontsector, line->backsector))
		{
			gld_clipper_SafeAddClipRange(angle2, angle1);
		}
    }


	HWR_ProcessSeg(); // Doesn't need arguments because they're defined globally :D
	return;
}


// HWR_CheckBBox
// Checks BSP node/subtree bounding box.
// Returns true
//  if some part of the bbox might be visible.
//
// modified to use local variables
// PrBoom: Super optimization
static boolean HWR_CheckBBox(fixed_t *bspcoord)
{
	int boxpos;
	angle_t angle1, angle2;

	// Find the corners of the box
	// that define the edges from current viewpoint.
	boxpos = (viewx <= bspcoord[BOXLEFT] ? 0 : viewx < bspcoord[BOXRIGHT ] ? 1 : 2) +
    (viewy >= bspcoord[BOXTOP ] ? 0 : viewy > bspcoord[BOXBOTTOM] ? 4 : 8);

	if (boxpos == 5)
		return true;

	const int* check = checkcoord[boxpos];

#ifdef NEWCLIP
	if (rendermode == render_opengl)
	{
		angle1 = R_PointToAngle(bspcoord[check[0]], bspcoord[check[1]]);
		angle2 = R_PointToAngle(bspcoord[check[2]], bspcoord[check[3]]);
		return gld_clipper_SafeCheckRange(angle2, angle1);
	}
#endif

	return true;
}

#ifdef POLYOBJECTS

//
// HWR_AddPolyObjectSegs
//
// haleyjd 02/19/06
// Adds all segs in all polyobjects in the given subsector.
// Modified for SRB2 hardware rendering -Jazz 7/13/09
//
static void HWR_AddPolyObjectSegs(void)
{
	size_t i, j;

	// Precache space for polyobjects
	// A fake line is needed to convert coords into OpenGL format
	seg_t gr_fakeline;
	// These need to be malloc'd to not cause any warnings
	polyvertex_t *pv1 = malloc(sizeof(polyvertex_t));
	polyvertex_t *pv2 = malloc(sizeof(polyvertex_t));

	// Sort through all the polyobjects
	for (i = 0; i < numpolys; ++i)
	{
		// Render the polyobject's lines
		for (j = 0; j < po_ptrs[i]->segCount; ++j)
		{
			// Copy the info of a polyobject's seg, then convert it to OpenGL floating point
			memcpy(&gr_fakeline, po_ptrs[i]->segs[j], sizeof(seg_t));

			// Now convert the line to floating point
			pv1->x = FIXED_TO_FLOAT(gr_fakeline.v1->x);
			pv1->y = FIXED_TO_FLOAT(gr_fakeline.v1->y);
			pv2->x = FIXED_TO_FLOAT(gr_fakeline.v2->x);
			pv2->y = FIXED_TO_FLOAT(gr_fakeline.v2->y);

			// Now give the linedef we're about to add the converted coords
			gr_fakeline.v1 = (vertex_t *)pv1;
			gr_fakeline.v2 = (vertex_t *)pv2;

			HWR_AddLine(&gr_fakeline);
		}
	}

	// Free temporary data no longer needed
	free(pv1);
    free(pv2);
}

#ifdef POLYOBJECTS_PLANES
static void HWR_RenderPolyObjPlane(polyobj_t *polysector, fixed_t fixedheight,
									FBITFIELD blendmode, byte lightlevel, lumpnum_t lumpnum, sector_t *FOFsector,
									byte alpha, extracolormap_t *planecolormap, polyvertex_t *pv, size_t nrPlaneVerts)
{

	// In the arguments for this function: nrPlaneVerts is the number of vertexes of the polygon,
	// polyvertex_t *pv contains the data for the coordinates of all the vertexes stored in this plane

	float           height; // Constant y (vertical) coodinate for all points on the convex flat polygon
	FOutVector      *v3d; // Actual transformed vertexes passed to the renderer
	size_t             i;
	float           flatxref, flatyref; // For flat texture coordinates
	float fflatsize;
	// SRB2CBTODO: Support polyobject spinning texture in OpenGL
	angle_t planeangle = 0;
	FSurfaceInfo    Surf;

	height = FIXED_TO_FLOAT(fixedheight);

	// This plane can't even be rendered if it's not a polygon
	if (nrPlaneVerts < 3)
	{
		CONS_Printf("HWR_RenderPolyObjPlane: A needs at least 3 or more sides!\n");
		return;
	}

	// Determine the current plane angle
	if (FOFsector != NULL)
	{
		if (fixedheight == FOFsector->floorheight) // it's a floor
			planeangle = ANGLE_TO_INT(FOFsector->floorpic_angle);
		else // it's a ceiling
			planeangle = ANGLE_TO_INT(FOFsector->ceilingpic_angle);
	}

	// SRB2CBTODO: Using planeangle never works!
	Surf.TexRotate = (planeangle % 360);

	if (blendmode & PF_NoTexture)
	{
		// Transform
		v3d = planeVerts;
		for (i = 0; i < nrPlaneVerts; i++, v3d++)
		{
			// No texture, make sow and tow 0 so there's no rendering issues
			v3d->sow = 0;
			v3d->tow = 0;
			v3d->x = pv[i].x;
			v3d->y = height;
			v3d->z = pv[i].y;
		}
	}
	else
	{
		// Get the size of the texture this flat is using for tow and sow
        // When loading from a data file, these textures are always saved in power of two
		size_t len;
		len = W_LumpLength(lumpnum);

		switch (len)
		{
			case 4194304: // 2048x2048 lump
				fflatsize = 2048.0f;
				break;
			case 1048576: // 1024x1024 lump
				fflatsize = 1024.0f;
				break;
			case 262144:// 512x512 lump
				fflatsize = 512.0f;
				break;
			case 65536: // 256x256 lump
				fflatsize = 256.0f;
				break;
			case 16384: // 128x128 lump
				fflatsize = 128.0f;
				break;
			case 1024: // 32x32 lump
				fflatsize = 32.0f;
				break;
			default: // 64x64 lump
				fflatsize = 64.0f;
				break;
		}

		// Reference point for the flat texture coordinates for each vertex that make up the polygon
		flatxref = 0.0f;//(float)(((fixed_t)pv[0].x & (~flatflag)) / fflatsize);
		flatyref = 0.0f;//(float)(((fixed_t)pv[0].y & (~flatflag)) / fflatsize);

		// Transform
		v3d = planeVerts;

		for (i = 0; i < nrPlaneVerts; i++, v3d++)
		{
			// ESLOPE: Sort vertexes THEN render poly objects? Just need ones that won't overlap for convex polyobjects
			// Scroll the texture of a floor/ceiling
			float scrollx = 0.0f, scrolly = 0.0f; // SRB2CBTODO: Scroll is needed when a polyobject moves!

			if (FOFsector != NULL)
			{
				if (fixedheight == FOFsector->floorheight) // it's a floor
				{
					scrollx = FIXED_TO_FLOAT(FOFsector->floor_xoffs)/fflatsize;
					scrolly = FIXED_TO_FLOAT(FOFsector->floor_yoffs)/fflatsize;
				}
				else // it's a ceiling
				{
					scrollx = FIXED_TO_FLOAT(FOFsector->ceiling_xoffs)/fflatsize;
					scrolly = FIXED_TO_FLOAT(FOFsector->ceiling_yoffs)/fflatsize;
				}
			}
			else if (gr_frontsector)
			{
				if (fixedheight < viewz) // it's a floor
				{
					scrollx = FIXED_TO_FLOAT(gr_frontsector->floor_xoffs)/fflatsize;
					scrolly = FIXED_TO_FLOAT(gr_frontsector->floor_yoffs)/fflatsize;
				}
				else // it's a ceiling
				{
					scrollx = FIXED_TO_FLOAT(gr_frontsector->ceiling_xoffs)/fflatsize;
					scrolly = FIXED_TO_FLOAT(gr_frontsector->ceiling_yoffs)/fflatsize;
				}
			}


			v3d->sow = (float)((pv[i].x / fflatsize) - flatxref + scrollx);
			v3d->tow = (float)(flatyref - (pv[i].y / fflatsize) + scrolly);

			v3d->x = pv[i].x;
			v3d->y = height;
			v3d->z = pv[i].y;

			polysector = NULL;
		}
	}


	// Determine the current plane angle, not in the above loop to speed this function up
	if (FOFsector != NULL)
	{
		if (fixedheight == FOFsector->floorheight) // it's a floor
			planeangle = ANGLE_TO_INT(FOFsector->floorpic_angle);
		else // it's a ceiling
			planeangle = ANGLE_TO_INT(FOFsector->ceilingpic_angle);
	}

	// SRB2CBTODO: Allow some polyobjects to have special rotation BESIDES normal rotate???
	// its better to just leave polyobjects with no plane align support except for XY
	Surf.TexRotate = (planeangle % 360);

	if (planecolormap && planecolormap->fog)
	{
		if (FOFsector != NULL && FOFsector->extra_colormap && FOFsector->extra_colormap->fog)
			Surf.FlatColor.rgba = HWR_Lighting(LightLevelToLum(lightlevel), planecolormap->rgba, false, false);
		else
			Surf.FlatColor.rgba = HWR_Lighting(LightLevelToLum(lightlevel), planecolormap->rgba, true, false);
	}
	else if (planecolormap)
		Surf.FlatColor.rgba = HWR_Lighting(LightLevelToLum(lightlevel), planecolormap->rgba, true, true);
	else
		Surf.FlatColor.rgba = HWR_Lighting(LightLevelToLum(lightlevel), NORMALFOG, false, false);

	// Recopy the alpha level, it gets reset after HWR_lighting
	Surf.FlatColor.s.alpha = alpha;

	// Now send the final values to the OpenGL system
	// and render it
	if (blendmode & PF_Environment)
		blendmode |= PF_Modulated|PF_Occlude;
	else if (blendmode & PF_Translucent)
		blendmode |= PF_Modulated;
	else
		blendmode |= PF_Masked|PF_Modulated;

#if 0 // SRB2CBTODO: MAKE THIS WORK POLYOBJECTS_PLANES
	if (Surf.TexRotate)
		blendmode |= PF_TexRotate;
#endif


	GL_DrawPolygon(&Surf, planeVerts, nrPlaneVerts, blendmode, 0);

}
#endif


//
// HWR_AddPolyObjectPlanes
//
// Kalaron 11/17/09
// Adds planes for all polyobjects in the given subsector
//
static void HWR_AddPolyObjectPlanes(void)
{
#if 1
#ifdef POLYOBJECTS_PLANES // SRB2CBTODO:!!!!!
	size_t i, j;
	sector_t *polysec;

	/*
	 * This is a somewhat confusing part of the code,
	 * the game normally uses an OpenGL bsp drawing method to get information
	 * to pass to HWR_RenderPlane, but polyobjects AREN'T part of the BSP,
	 * so this causes a bunch of issues when trying to draw flats on them.
	 * Polyobjects themselves are dynamic objects added at the end of BSP calculation,
	 * which means that they must be drawn in specialized functions.
	 */

	// Count polyobjects and render
	for (i = 0; i < numpolys; ++i)
	{
		if (!((po_ptrs[i]->flags & POF_RENDERTOP) || (po_ptrs[i]->flags & POF_RENDERBOTTOM)))
			continue;

		// Polyobject's sector to get the texture data from
		polysec = po_ptrs[i]->lines[0]->backsector;

		// This stores the vertexes of the polyobject's plane
		polyvertex_t *pvertexbuffer;

		// Allocate the plane's vertexes
		// The vertexbuffer cannot be individually alloc'd and freed here,
		// this data is copied to a the polyobject's vertex list and the game must reserve space for it,
		// it can only be removed after the polyobject is done being used meaning the data is freed after a level ends
		pvertexbuffer = Z_Calloc(po_ptrs[i]->numLines * sizeof (*pvertexbuffer), PU_LEVEL, NULL);

		// Data cannot be directly copy data from a polyobject's
		// line data because pvertexbuffer uses polyvertex_t, instead of vertex_t
		// a polyobject's vertex_t, so each space for a polygon vertex must be allocated
		// The Zone system is used so data can be freed after the level is done
		//for (q = 0; q < totalpvcount; q++)
		//	pvertexbuffer[q] = Z_Calloc(sizeof(polyvertex_t), PU_LEVEL, NULL);

		// Number of the vertex in the list array we're adding to
		size_t pvcount = 0;

		for (j = 0; j < po_ptrs[i]->numLines; ++j) // SRB2CBTODO: Vertexes need sorting somehow or is HWR_Renderpolyobjplane the issue?
		{
			pvertexbuffer[pvcount].x = FIXED_TO_FLOAT(po_ptrs[i]->lines[j]->v1->x);
			pvertexbuffer[pvcount].y = FIXED_TO_FLOAT(po_ptrs[i]->lines[j]->v1->y);
			// SRB2CBTODO: Z could also be used here for sloped polyobjects
			pvcount++;
		}

		if (pvcount != po_ptrs[i]->numLines)
			I_Error("%s: Plane vertexes incorrectly counted!\n"
					"Vertexcount = %lu, should be: %lu", __FUNCTION__, (ULONG)pvcount, (ULONG)po_ptrs[i]->numLines);

		if (po_ptrs[i]->flags & POF_RENDERTOP)
		{
			if (polysec->floorheight <= gr_frontsector->ceilingheight
				&& polysec->floorheight >= gr_frontsector->floorheight
				&& (viewz < polysec->floorheight))
			{
				// SRB2CBTODO: For when polyojects support being casted on by light
				//light = R_GetPlaneLight(gr_frontsector, polysec->floorheight, viewz < polysec->floorheight);

				HWR_GetFlat(levelflats[polysec->floorpic].lumpnum, true);
				HWR_RenderPolyObjPlane(po_ptrs[i], polysec->floorheight, PF_Occlude,
									   polysec->lightlevel, levelflats[polysec->floorpic].lumpnum,
									   polysec, 255, NULL, pvertexbuffer, pvcount); // SRB2CBTODO: 255 should equal polyobject's transparency
			}
		}

		if (po_ptrs[i]->flags & POF_RENDERTOP)
		{
			if (polysec && polysec->ceilingheight >= gr_frontsector->floorheight
				&& polysec->ceilingheight <= gr_frontsector->ceilingheight
				&& (viewz > polysec->ceilingheight))
			{
				// SRB2CBTODO: For when polyojects support being casted on by light
				//light = R_GetPlaneLight(gr_frontsector, polysec->ceilingheight, viewz < polysec->ceilingheight);

				HWR_GetFlat(levelflats[polysec->ceilingpic].lumpnum, true);
				HWR_RenderPolyObjPlane(po_ptrs[i], polysec->ceilingheight, PF_Occlude,
									   polysec->lightlevel, levelflats[polysec->ceilingpic].lumpnum,
									   polysec, 255, NULL, pvertexbuffer, pvcount); // SRB2CBTODO: 255 should equal polyobject's transparency
			}
		}
	}

#endif // POLYOBJECTS_PLANES
#else

	size_t i, j, pvcount = 0;

	// Precache space for polyobjects
	// A fake line is needed to convert coords into OpenGL format
	seg_t gr_fakeline;
	// These need to be malloc'd to not cause any warnings
	//polyvertex_t *pv1 = Z_Calloc(po_ptrs[i]->segCount * sizeof (*pvertexbuffer), PU_LEVEL, NULL);
	//polyvertex_t *pv2 = malloc(sizeof(polyvertex_t)*numpolys);

	sector_t *polysec;

	// Sort through all the polyobjects
	for (i = 0; i < numpolys; ++i)
	{
		polysec = po_ptrs[i]->lines[0]->backsector;
		polyvertex_t *pv1 = Z_Calloc(po_ptrs[i]->segCount * sizeof (*pv1), PU_LEVEL, NULL);
		// Render the polyobject's lines
		for (j = 0; j < po_ptrs[i]->segCount; ++j)
		{
			// Copy the info of a polyobject's seg, then convert it to OpenGL floating point
			memcpy(&gr_fakeline, po_ptrs[i]->segs[j], sizeof(seg_t));

			// Now convert the line to floating point
			pv1[pvcount].x = FIXED_TO_FLOAT(gr_fakeline.v1->x);
			pv1[pvcount].y = FIXED_TO_FLOAT(gr_fakeline.v1->y);
			//pv2[pvcount].x = FIXED_TO_FLOAT(gr_fakeline.v2->x);
			//pv2[pvcount].y = FIXED_TO_FLOAT(gr_fakeline.v2->y);

			pvcount++;



			// Now give the linedef we're about to add the converted coords
			//gr_fakeline.v1 = (vertex_t *)pv1;
			//gr_fakeline.v2 = (vertex_t *)pv2;

			//HWR_AddLine(&gr_fakeline);
		}
		HWR_RenderPolyObjPlane(po_ptrs[i], polysec->floorheight, PF_Occlude,
							   polysec->lightlevel, levelflats[polysec->floorpic].lumpnum,
							   polysec, 255, NULL, pv1, po_ptrs[i]->segCount);
	}

	// Free temporary data no longer needed
	//free(pv1);
	//free(pv2);

#endif
}
#endif // POLYOBJECTS

// -----------------+
// HWR_Subsector    : Determine floor/ceiling planes.
//                  : Add sprites of things in sector.
// Notes            : Draw one or more line segments.
// -----------------+
static void HWR_Subsector(size_t num) // SRB2CBTODO: This can be serverly optimized by using the current viewpoint to cut out rendering
{
	int count, floorlightlevel, ceilinglightlevel, light;
	seg_t *line;
	subsector_t *sub;
	static sector_t tempsec; // Deep water hack
	extracolormap_t *floorcolormap;
	extracolormap_t *ceilingcolormap;

	if (num >= numsubsectors)
		I_Error("%s: Subsector count incorrect: ss %u with numss = %u", // SRB2CBTODO: SRB2CBFIX: Why isn't __FUNCTION__ used throughout the code 0_0 ?
			__FUNCTION__, (int)num, (int)numsubsectors);

	// subsectors added at run-time
	if (num >= numsubsectors)
		return;

	sub = &subsectors[num];
	gr_frontsector = sub->sector;
	count = sub->numlines;
	line = &segs[sub->firstline];

	// Deep water/fake ceiling effect.
	// This helps assign things such as light values
	gr_frontsector = R_FakeFlat(gr_frontsector, &tempsec, &floorlightlevel,
								&ceilinglightlevel, false);

	floorcolormap = ceilingcolormap = gr_frontsector->extra_colormap;

	// Check and prep all 3D floors. Set the sector floor/ceiling light levels and colormaps.
	if (gr_frontsector->ffloors)
	{
		if (gr_frontsector->moved)
		{
			gr_frontsector->numlights = sub->sector->numlights = 0;
			R_Prep3DFloors(gr_frontsector);
			sub->sector->lightlist = gr_frontsector->lightlist;
			sub->sector->numlights = gr_frontsector->numlights;
			sub->sector->moved = gr_frontsector->moved = false;
		}

		light = R_GetPlaneLight(gr_frontsector, gr_frontsector->floorheight, false);
		if (gr_frontsector->floorlightsec == -1)
			floorlightlevel = *gr_frontsector->lightlist[light].lightlevel;
		floorcolormap = gr_frontsector->lightlist[light].extra_colormap;
		light = R_GetPlaneLight(gr_frontsector, gr_frontsector->ceilingheight, false);
		if (gr_frontsector->ceilinglightsec == -1)
			ceilinglightlevel = *gr_frontsector->lightlist[light].lightlevel;
		ceilingcolormap = gr_frontsector->lightlist[light].extra_colormap;
	}

	sub->sector->extra_colormap = gr_frontsector->extra_colormap;

	// Render sector planes
	// yeah, easy backface cull! :)
	// ESLOPE: NOTE: Sloped planes need to use P_DistFromPlanef to calculate if you can see it
#ifdef ESLOPE
	v3float_t   cam;
	// SoM: Slopes!
	cam.x = FIXED_TO_FLOAT(viewx);
	cam.y = FIXED_TO_FLOAT(viewy);
	cam.z = FIXED_TO_FLOAT(viewz);


	if (((cv_grtest.value != 9) && (gr_frontsector->f_slope &&  P_DistFromPlanef(&cam, &gr_frontsector->f_slope->of,
													 &gr_frontsector->f_slope->normalf) > 0.0f)) || (((cv_grtest.value == 9) || !gr_frontsector->f_slope) && gr_frontsector->floorheight < viewz))
#else
	if (gr_frontsector->floorheight < viewz)
#endif
	{
		if (gr_frontsector->floorpic == skyflatnum)
		{
			HWR_RenderSkyPlane(&extrasubsectors[num], subsectors[num].sector->floorheight);
		}
		else
		{
			HWR_GetFlat(levelflats[gr_frontsector->floorpic].lumpnum, true);
			HWR_RenderPlane(gr_frontsector, &extrasubsectors[num], gr_frontsector->floorheight, PF_Occlude,
							floorlightlevel, levelflats[gr_frontsector->floorpic].lumpnum, NULL,
							255, floorcolormap);
		}
	}

	// ESLOPE: Use math to tell if a slope plane must be rendered
#ifdef ESLOPE
	if (((cv_grtest.value != 9) && (gr_frontsector->c_slope &&  P_DistFromPlanef(&cam, &gr_frontsector->c_slope->of,
													 &gr_frontsector->c_slope->normalf) > 0.0f)) || (((cv_grtest.value == 9) || !gr_frontsector->c_slope) && gr_frontsector->ceilingheight > viewz))
#else
	if (gr_frontsector->ceilingheight > viewz)
#endif
	{
		if (gr_frontsector->ceilingpic == skyflatnum)
		{
			HWR_RenderSkyPlane(&extrasubsectors[num], subsectors[num].sector->ceilingheight);
		}
		else
		{
			HWR_GetFlat(levelflats[gr_frontsector->ceilingpic].lumpnum, true);
			HWR_RenderPlane(gr_frontsector, &extrasubsectors[num], gr_frontsector->ceilingheight, PF_Occlude,
							ceilinglightlevel, levelflats[gr_frontsector->ceilingpic].lumpnum,
							NULL, 255, ceilingcolormap);
		}
	}


#ifdef R_FAKEFLOORS // Render floors of FOFs // ZTODO: VEEEERY laggy implementation
	if (gr_frontsector->ffloors)
	{
		/// \todo fix light, xoffs, yoffs, extracolormap ?
		ffloor_t * rover;

		for (rover = gr_frontsector->ffloors;
			rover; rover = rover->next)
		{
			if (!(rover->flags & FF_EXISTS) || !(rover->flags & FF_RENDERPLANES))
				continue;

			pslope_t *topslope = NULL;
			if (rover->master->frontsector->c_slope)
				topslope = rover->master->frontsector->c_slope;

			pslope_t *bottomslope = NULL;
			if (rover->master->frontsector->f_slope)
				bottomslope = rover->master->frontsector->f_slope;

			if (*rover->bottomheight <= gr_frontsector->ceilingheight &&
			    *rover->bottomheight >= gr_frontsector->floorheight &&
				((
				  ((viewz < *rover->bottomheight || bottomslope) && !(rover->flags & FF_INVERTPLANES))
				  || ((viewz > *rover->bottomheight || bottomslope) && (rover->flags & FF_BOTHPLANES))
				 )
				|| (rover->flags & FF_FOG)))
			{
				// Fogs always need light
				if (rover->flags & FF_FOG)
					light = R_GetPlaneLight(gr_frontsector, *rover->bottomheight, false);
				else if (bottomslope)
					light = R_GetPlaneLight(gr_frontsector, *rover->bottomheight, (P_DistFromPlanef(&cam, &bottomslope->of, &bottomslope->normalf) > 0.0f));
				else
					light = R_GetPlaneLight(gr_frontsector, *rover->bottomheight, viewz < *rover->bottomheight);

				if (rover->flags & FF_FOG)
				{
					byte alpha = rover->master->frontsector->lightlevel/1.4;

					if (!(rover->master->frontsector->extra_colormap && rover->master->frontsector->extra_colormap->fog))
					{
						if (rover->master->frontsector->lightlevel == 255)
							alpha = 0;
						// TODO: Do something with Light fog blocks! (maybe fade in/out or something?)
					}

					// When the FOF is a fog block, the alpha uses the frontsector's lightlevel instead
					HWR_AddTransparentFloor(NULL, 0, // HWR_AddNoTexFloor
											&extrasubsectors[num],
											*rover->bottomheight,
											rover->master->frontsector->lightlevel,
											alpha,
											rover->master->frontsector, PF_Translucent|PF_NoTexture,
											rover->master->frontsector->extra_colormap);
				}
				else if (rover->flags & FF_TRANSLUCENT)
				{
					// If the alpha level is bellow 255, draw this transparent, but allow for flats with holes
					// Otherwise it's a normal transparent floor
					if (rover->alpha < 243)
					{
						HWR_AddTransparentFloor(NULL, levelflats[*rover->bottompic].lumpnum,
												&extrasubsectors[num],
												*rover->bottomheight,
												*gr_frontsector->lightlist[light].lightlevel,
												rover->alpha, rover->master->frontsector, PF_Translucent,
												gr_frontsector->lightlist[light].extra_colormap);
					}
					else
					{
						HWR_AddPeggedFloor(NULL, levelflats[*rover->bottompic].lumpnum,
										   &extrasubsectors[num],
										   *rover->bottomheight,
										   *gr_frontsector->lightlist[light].lightlevel,
										   rover->alpha, rover->master->frontsector,
										   gr_frontsector->lightlist[light].extra_colormap);
					}

				}
				else
				{
					HWR_GetFlat(levelflats[*rover->bottompic].lumpnum, true);
					HWR_RenderPlane(NULL, &extrasubsectors[num], *rover->bottomheight, PF_Occlude,
									*gr_frontsector->lightlist[light].lightlevel,
									levelflats[*rover->bottompic].lumpnum,
					                rover->master->frontsector, 255,
									gr_frontsector->lightlist[light].extra_colormap);
				}
			}

			// ESLOPE: Just always render the slope if it has one (too much math :P) // ESLOPETODO: Do the culling for backward normals
			if (*rover->topheight >= gr_frontsector->floorheight &&
			    *rover->topheight <= gr_frontsector->ceilingheight &&
				((
				  ((viewz > *rover->topheight || topslope) && !(rover->flags & FF_INVERTPLANES))
				  || ((viewz < *rover->topheight || topslope) && (rover->flags & FF_BOTHPLANES))
				 )
				 || (rover->flags & FF_FOG)))

			{
				// Fogs always need light
				if (rover->flags & FF_FOG)
					light = R_GetPlaneLight(gr_frontsector, *rover->topheight, true);
				else if (topslope)
					light = R_GetPlaneLight(gr_frontsector, *rover->topheight, (P_DistFromPlanef(&cam, &topslope->of, &topslope->normalf) > 0.0f));
				else
					light = R_GetPlaneLight(gr_frontsector, *rover->topheight, viewz < *rover->topheight);

				if (rover->flags & FF_FOG)
				{
					byte alpha = rover->master->frontsector->lightlevel/1.4;

					if (!(rover->master->frontsector->extra_colormap && rover->master->frontsector->extra_colormap->fog))
					{
						if (rover->master->frontsector->lightlevel == 255)
							alpha = 0;
						// TODO: Do something with Light fog blocks! (maybe fade in/out or something?)
					}

					// When the FOF is a fog block, the alpha uses the frontsector's lightlevel instead
					HWR_AddTransparentFloor(NULL, 0, // HWR_AddNoTexFloor
											&extrasubsectors[num],
											*rover->topheight,
											rover->master->frontsector->lightlevel,
											alpha, rover->master->frontsector,
											PF_Translucent|PF_NoTexture, rover->master->frontsector->extra_colormap);
				}
				else if (rover->flags & FF_TRANSLUCENT)
				{
					// If the alpha level is bellow 255, draw this transparent, but allow for flats with holes
					// Otherwise it's a normal transparent floor
					if (rover->alpha < 243)
					{
						HWR_AddTransparentFloor(NULL, levelflats[*rover->toppic].lumpnum,
												&extrasubsectors[num],
												*rover->topheight,
												*gr_frontsector->lightlist[light].lightlevel,
												rover->alpha, rover->master->frontsector, PF_Translucent,
												gr_frontsector->lightlist[light].extra_colormap);
					}
					else
					{
						HWR_AddPeggedFloor(NULL, levelflats[*rover->toppic].lumpnum,
										   &extrasubsectors[num],
										   *rover->topheight,
										   *gr_frontsector->lightlist[light].lightlevel,
										   rover->alpha, rover->master->frontsector,
										   gr_frontsector->lightlist[light].extra_colormap);
					}

				}
				else
				{
					HWR_GetFlat(levelflats[*rover->toppic].lumpnum, true);
					HWR_RenderPlane(NULL, &extrasubsectors[num], *rover->topheight, PF_Occlude,
									*gr_frontsector->lightlist[light].lightlevel, levelflats[*rover->toppic].lumpnum,
									rover->master->frontsector, 255, gr_frontsector->lightlist[light].extra_colormap);
				}
			}
		}
	}
#endif // R_FAKEFLOORS planes

	// The game just draws the floor and ceiling
	// Sprites are drawn, then the walls

	// Draw sprites first, because they are clipped to the solidsegs of
	// subsectors and are more 'in front'

	HWR_AddSprites(sub->sector);

#ifdef POLYOBJECTS // Isolated subsector rendering reference, YAY!
	// Draw all the polyobjects in this subsector
	if (sub->polyList)
	{
		polyobj_t *po = sub->polyList;

		numpolys = 0;

		// Count all the polyobjects, reset the list, and recount them
		while (po)
		{
			++numpolys;
			po = (polyobj_t *)(po->link.next);
		}

		// Sort polyobjects
		R_SortPolyObjects(sub);

		// Draw polyobject lines.
		HWR_AddPolyObjectSegs();

		// Draw polyobject planes
		HWR_AddPolyObjectPlanes();


	}
#endif

	while (count--)
	{
		HWR_AddLine(line);
		line++;
		gr_curline = NULL;
	}
}

fixed_t *hwlightbbox;

//
// Renders all subsectors below a given node, // SRB2CBTODO: Optimize a custom node system!
//  traversing subtree recursively.
// Just call with BSP root.
//
static void HWR_RenderBSPNode(int bspnum)
{
	while (!(bspnum & NF_SUBSECTOR))  // Found a subsector?
	{
		node_t *bsp = &nodes[bspnum];

		// Decide which side the view point is on.
		int side = R_PointOnSide(viewx, viewy, bsp);

		hwlightbbox = bsp->bbox[side];

		// Recursively divide front space.
		HWR_RenderBSPNode(bsp->children[side]);

		// Possibly divide back space.
		if (!HWR_CheckBBox(bsp->bbox[side^1]))
			return;

		hwlightbbox = bsp->bbox[side^1];
		bspnum = bsp->children[side^1];
	}
	HWR_Subsector(bspnum == -1 ? 0 : bspnum & ~NF_SUBSECTOR);
}

// ==========================================================================
//                                                              FROM R_MAIN.C
// ==========================================================================

// SRB2CBTODO: Use the proper aiming angle to determine clipping
void HWR_InitTextureMapping(void) // SRB2CBTODO: USE same thing as software does for this!
{
	angle_t i;
	int x;
	int t;
	fixed_t focallength;
	fixed_t grcenterx;
	fixed_t grcenterxfrac;
	int grviewwidth;

	grviewwidth = vid.width;
	grcenterx = grviewwidth/2;
	grcenterxfrac = grcenterx<<FRACBITS;

	// Use tangent table to generate viewangletox:
	//  viewangletox will give the next greatest x
	//  after the view angle.
	//
	// Calc focallength
	//  so FIELDOFVIEW angles covers SCREENWIDTH.
	focallength = FixedDiv(centerxfrac,
						   FINETANGENT(FINEANGLES/4+/*cv_fov.value*/ FIELDOFVIEW/2));

	for (i = 0; i < FINEANGLES/2; i++)
	{
		if (FINETANGENT(i) > FRACUNIT*2)
			t = -1;
		else if (FINETANGENT(i) < -FRACUNIT*2)
			t = grviewwidth+1;
		else
		{
			t = FixedMul(FINETANGENT(i), focallength);
			t = (grcenterxfrac - t+FRACUNIT-1)>>FRACBITS;

			if (t < -1)
				t = -1;
			else if (t > grviewwidth+1)
				t = grviewwidth+1;
		}
		gr_viewangletox[i] = t;
	}

	// Scan viewangletox[] to generate xtoviewangle[]:
	//  xtoviewangle will give the smallest view angle
	//  that maps to x.
	for (x = 0; x <= grviewwidth; x++)
	{
		i = 0;
		while (gr_viewangletox[i]>x)
			i++;
		gr_xtoviewangle[x] = (i<<ANGLETOFINESHIFT) - ANG90;
	}

	// Take out the fencepost cases from viewangletox.
	for (i = 0; i < FINEANGLES/2; i++)
	{
		if (gr_viewangletox[i] == -1)
			gr_viewangletox[i] = 0;
		else if (gr_viewangletox[i] == viewwidth+1)
			gr_viewangletox[i]  = viewwidth;
	}

	gr_clipangle = gr_xtoviewangle[0];
	gr_doubleclipangle = gr_clipangle*2;
}

// ==========================================================================
// gr_things.c // SRB2CBTODO: Doesn't exist
// ==========================================================================

// sprites are drawn after all wall and planes are rendered, so that
// sprite translucency effects apply on the rendered view (instead of the background sky!!)

static gr_vissprite_t gr_vissprites[MAXVISSPRITES];
static gr_vissprite_t *gr_vissprite_p;

// --------------------------------------------------------------------------
// HWR_ClearSprites
// Called at frame start.
// --------------------------------------------------------------------------
static void HWR_ClearSprites(void)
{
	gr_vissprite_p = gr_vissprites;
}

// --------------------------------------------------------------------------
// HWR_NewVisSprite
// --------------------------------------------------------------------------
static gr_vissprite_t gr_overflowsprite;

static gr_vissprite_t *HWR_NewVisSprite(void)
{
	if (gr_vissprite_p == &gr_vissprites[MAXVISSPRITES])
		return &gr_overflowsprite;

	gr_vissprite_p++;
	return gr_vissprite_p-1;
}


// JTE: Finds a floor through which light does not pass.
// A simple function to check if a sprite shadow should render on a floor
static fixed_t HWR_OpaqueFloorAtPos(fixed_t x, fixed_t y, fixed_t z, fixed_t height)
{
	const sector_t *sec = R_PointInSubsector(x, y)->sector;
	fixed_t floorz = sec->floorheight;

	if (sec->f_slope) // That's right, CHECK FOR SLOPES BABY!!!!
		floorz = P_GetZAt(sec->f_slope, x, y);

	if (sec->ffloors)
	{
		ffloor_t *rover;
		fixed_t delta1, delta2;
		const fixed_t thingtop = z + height;

		for (rover = sec->ffloors; rover; rover = rover->next)
		{
			if (!(rover->flags & FF_EXISTS)
				|| !(rover->flags & FF_RENDERPLANES)
				|| rover->flags & FF_TRANSLUCENT
				|| rover->flags & FF_FOG
				|| rover->flags & FF_INVERTPLANES)
				continue;

			delta1 = z - (*rover->bottomheight + ((*rover->topheight - *rover->bottomheight)/2));
			delta2 = thingtop - (*rover->bottomheight + ((*rover->topheight - *rover->bottomheight)/2));
			if (*rover->topheight > floorz && abs(delta1) < abs(delta2))
				floorz = *rover->topheight; // SRB2CBTODO: FOF slopes
		}
	}

	return floorz;
}



// SRB2CBTODO: Draw shadow before sprite to fix a sorting issue
// SRB2CBTODO: Add to the shadow list here
static void HWR_DrawShadow(gr_vissprite_t *spr, FOutVector *wallVerts, FSurfaceInfo *Surf, GLPatch_t *gpatch)
{
	////////////////////
	// SHADOW SPRITE! //
	////////////////////
	fixed_t floorheight;

	spritedef_t *sprdef;
	spriteframe_t *sprframe;
	size_t lump;

	unsigned rot;
	byte flip;
	angle_t ang;
	mobj_t *thing = spr->mobj;

	if (thing->skin && thing->sprite == SPR_PLAY)
		sprdef = &((skin_t *)thing->skin)->spritedef;
	else
		sprdef = &sprites[thing->sprite];

	if ((byte)(thing->frame&FF_FRAMEMASK) >= sprdef->numframes)
	{
		CONS_Printf(PREFIX_WARN "Mobj of type %i with invalid sprite data (%ld) detected and removed.\n", thing->type, (thing->frame&FF_FRAMEMASK));
		if (thing->player)
			P_SetPlayerMobjState(thing, S_PLAY_STND);
		else
			P_SetMobjState(thing, S_DISS);
		return;
	}

	sprframe = &sprdef->spriteframes[thing->frame & FF_FRAMEMASK];

	if (!sprframe)
		return;

	if (sprframe->rotate)
	{
		// choose a different rotation based on player view
		ang = R_PointToAngle(thing->x, thing->y);
		rot = (ang-thing->angle+(angle_t)(ANG45/2)*9)>>29;
		//Fab: lumpid is the index for spritewidth, spriteoffset... tables
		lump = sprframe->lumpid[rot];
		flip = sprframe->flip[rot];
	}
	else
	{
		// use single rotation for all views
		rot = 0;                        //Fab: for vis->patch below
		lump = sprframe->lumpid[0];     //Fab: see note above
		flip = sprframe->flip[0];
	}


	fixed_t mobjfloor = HWR_OpaqueFloorAtPos(
											 spr->mobj->x, spr->mobj->y,
											 spr->mobj->z, spr->mobj->height);

	// Don't offset the player's shadow - this helps with platforming and seeing where you'll land
	if (spr->mobj->type != MT_PLAYER)
	{
		angle_t shadowdir;

		// Set direction
		if (splitscreen && stplyr != &players[displayplayer])
			shadowdir = localangle2 + (ANGLE_10/10)*cv_cam2_rotate.value;
		else
			shadowdir = localangle + (ANGLE_10/10)*cv_cam_rotate.value;

		// Find floorheight
		floorheight = HWR_OpaqueFloorAtPos(
										   spr->mobj->x + P_ReturnThrustX(spr->mobj, ANGLE_10, spr->mobj->z - mobjfloor),
										   spr->mobj->y + P_ReturnThrustY(spr->mobj, ANGLE_10, spr->mobj->z - mobjfloor),
										   spr->mobj->z, spr->mobj->height);


		floorheight = (spr->mobj->z - floorheight)/FRACUNIT;
	}
	else
		floorheight = (spr->mobj->z - mobjfloor)/FRACUNIT;
	// A shadow isn't visble // SRB2CBTODO: Set camera distance of no-draw!
	if (floorheight >= 1024)
		return;
	else
	{
		// create the sprite billboard
		//
		//  3--2
		//  | /|
		//  |/ |
		//  0--1

		// NOTE: For shadow sprites, the z and y coordinates are flipped

		float spry = 0;//FIXED_TO_FLOAT(spritecachedinfo[lump].topoffset);
		if (spritecachedinfo[lump].topoffset > 0 && (spritecachedinfo[lump].topoffset < spritecachedinfo[lump].height))
			spry += 10+5.0f*thing->scale/100;
		// Horizontal
		wallVerts[0].x = wallVerts[3].x = FIXED_TO_FLOAT(spr->mobj->x);
		wallVerts[2].x = wallVerts[1].x = FIXED_TO_FLOAT(spr->mobj->x) + FIXED_TO_FLOAT(spritecachedinfo[lump].width);
		// Adjust the offset
		wallVerts[0].x = wallVerts[3].x -= FIXED_TO_FLOAT(spritecachedinfo[lump].offset);
		wallVerts[2].x = wallVerts[1].x -= FIXED_TO_FLOAT(spritecachedinfo[lump].offset);
		// Depth - when the shadow goes outward
		// TODO: Needs an spry equivalent
		wallVerts[3].z = wallVerts[2].z = FIXED_TO_FLOAT(spr->mobj->y)+gpatch->height+FIXED_TO_FLOAT(spritecachedinfo[lump].topoffset)-gpatch->height+spry;//spr->tz;
		wallVerts[1].z = wallVerts[0].z = FIXED_TO_FLOAT(spr->mobj->y)+FIXED_TO_FLOAT(spritecachedinfo[lump].topoffset)-gpatch->height+spry;//spr->tz;
		// Vertical
#if 0 // SRB2CBTODO: Cut and cast the sprite over walls and slopes
		if (R_PointInSubsector(FLOAT_TO_FIXED(wallVerts[2].x), FLOAT_TO_FIXED(wallVerts[2].z))->sector->f_slope)
			wallVerts[2].y = P_GetZAtf(R_PointInSubsector(FLOAT_TO_FIXED(wallVerts[2].x), FLOAT_TO_FIXED(wallVerts[2].z))->sector->f_slope, wallVerts[2].x, wallVerts[2].z) + 1;
		else
			wallVerts[2].y = FIXED_TO_FLOAT(R_PointInSubsector(FLOAT_TO_FIXED(wallVerts[2].x), FLOAT_TO_FIXED(wallVerts[2].z))->sector->floorheight) + 1;

		if (R_PointInSubsector(FLOAT_TO_FIXED(wallVerts[3].x), FLOAT_TO_FIXED(wallVerts[3].z))->sector->f_slope)
			wallVerts[3].y = P_GetZAtf(R_PointInSubsector(FLOAT_TO_FIXED(wallVerts[3].x), FLOAT_TO_FIXED(wallVerts[3].z))->sector->f_slope, wallVerts[3].x, wallVerts[3].z) + 1;
		else
			wallVerts[3].y = FIXED_TO_FLOAT(R_PointInSubsector(FLOAT_TO_FIXED(wallVerts[3].x), FLOAT_TO_FIXED(wallVerts[3].z))->sector->floorheight) + 1;

		if (R_PointInSubsector(FLOAT_TO_FIXED(wallVerts[0].x), FLOAT_TO_FIXED(wallVerts[0].z))->sector->f_slope)
			wallVerts[0].y = P_GetZAtf(R_PointInSubsector(FLOAT_TO_FIXED(wallVerts[0].x), FLOAT_TO_FIXED(wallVerts[0].z))->sector->f_slope, wallVerts[0].x, wallVerts[0].z) + 1;
		else
			wallVerts[0].y = FIXED_TO_FLOAT(R_PointInSubsector(FLOAT_TO_FIXED(wallVerts[0].x), FLOAT_TO_FIXED(wallVerts[0].z))->sector->floorheight) + 1;

		if (R_PointInSubsector(FLOAT_TO_FIXED(wallVerts[1].x), FLOAT_TO_FIXED(wallVerts[1].z))->sector->f_slope)
			wallVerts[1].y = P_GetZAtf(R_PointInSubsector(FLOAT_TO_FIXED(wallVerts[1].x), FLOAT_TO_FIXED(wallVerts[1].z))->sector->f_slope, wallVerts[1].x, wallVerts[1].z) + 1;
		else
			wallVerts[1].y = FIXED_TO_FLOAT(R_PointInSubsector(FLOAT_TO_FIXED(wallVerts[1].x), FLOAT_TO_FIXED(wallVerts[1].z))->sector->floorheight) + 1;
#endif

		const float radius = (spr->x2 - spr->x1)/2.0f;
		float scale;

		// When a sprite is high resolution, it means the game should render it 2x as small
		if (spr->mobj->flags & MF_HIRES)
			scale = (float)(100.0f - spr->mobj->scale/2);
		else
			scale = (float)(100.0f - spr->mobj->scale);

		if (spr->mobj->scale != 100)
		{
			wallVerts[0].x = wallVerts[3].x += (radius)*scale/100.0f;
			wallVerts[2].x = wallVerts[1].x -= (radius)*scale/100.0f;

			wallVerts[0].z = wallVerts[1].z += abs(gpatch->topoffset - gpatch->height)*scale/100.0f;
			wallVerts[2].z = wallVerts[3].z += abs(gpatch->topoffset - gpatch->height)*scale/100.0f;
			wallVerts[2].z = wallVerts[3].z -= (gpatch->height)*scale/100.0f;

			wallVerts[3].z = wallVerts[2].z -= 10*scale/100;
			wallVerts[1].z = wallVerts[0].z -= 10*scale/100;
		}
		else if (spr->mobj->flags & MF_HIRES)
		{
			wallVerts[0].x = wallVerts[3].x += (radius)*scale/100.0f;
			wallVerts[2].x = wallVerts[1].x -= (radius)*scale/100.0f;

			wallVerts[0].z = wallVerts[1].z += abs(gpatch->topoffset - gpatch->height)*scale/100.0f;
			wallVerts[2].z = wallVerts[3].z += abs(gpatch->topoffset - gpatch->height)*scale/100.0f;
			wallVerts[2].z = wallVerts[3].z -= (gpatch->height)*scale/100.0f;
		}

		// Push out shadow before sloping so we have the right vertexes
		if (spr->mobj->type != MT_PLAYER) // Don't offset the player's shadow, this helps with platforming and seeing where you'll land
		{
			wallVerts[3].z = wallVerts[2].z += floorheight;
			wallVerts[1].z = wallVerts[0].z += floorheight;
		}


#ifdef ESLOPE
		if (spr->mobj->subsector->sector->f_slope) // SLOPES! ESLOPE Gotta slope these, AFTER scaling so the coords match up correctly
		{
			wallVerts[2].y = P_GetZAtf(spr->mobj->subsector->sector->f_slope, wallVerts[2].x, wallVerts[2].z)+3;
			wallVerts[3].y = P_GetZAtf(spr->mobj->subsector->sector->f_slope, wallVerts[3].x, wallVerts[3].z)+3;

			wallVerts[0].y = P_GetZAtf(spr->mobj->subsector->sector->f_slope, wallVerts[0].x, wallVerts[0].z)+3;
			wallVerts[1].y = P_GetZAtf(spr->mobj->subsector->sector->f_slope, wallVerts[1].x, wallVerts[1].z)+3;
		}
		else
#endif
			wallVerts[2].y = wallVerts[3].y = wallVerts[0].y = wallVerts[1].y = FIXED_TO_FLOAT(spr->mobj->floorz) + 1;


		if (spr->flip)
		{
			wallVerts[0].sow = wallVerts[3].sow = gpatch->max_s;
			wallVerts[2].sow = wallVerts[1].sow = 0;
		}
		else
		{
			wallVerts[0].sow = wallVerts[3].sow = 0;
			wallVerts[2].sow = wallVerts[1].sow = gpatch->max_s;
		}
		wallVerts[3].tow = wallVerts[2].tow = 0;

		wallVerts[0].tow = wallVerts[1].tow = gpatch->max_t;

		// flip the texture coords
#ifdef SPRITEROLL
		if (spr->mobj->rollangle == spr->mobj->destrollangle)
#endif
			if (spr->vflip)
			{
				FLOAT temp = wallVerts[0].tow;
				wallVerts[0].tow = wallVerts[3].tow;
				wallVerts[3].tow = temp;
				temp = wallVerts[1].tow;
				wallVerts[1].tow = wallVerts[2].tow;
				wallVerts[2].tow = temp;
			}

		// The normal color of a shadow
		Surf->FlatColor.s.red = 0x00;
		Surf->FlatColor.s.blue = 0x00;
		Surf->FlatColor.s.green = 0x00;

		if (spr->mobj->frame & FF_TRANSMASK || spr->mobj->flags2 & MF2_TRANSLUCENT)
		{
			// colormap test
			sector_t *sector;
			sector = spr->mobj->subsector->sector;
			int lightnum;
			extracolormap_t *colormap;
			lightnum = sector->lightlevel;
			colormap = sector->extra_colormap;

			if (sector->numlights)
			{
				int light;
				light = R_GetPlaneLight(sector, spr->mobj->z+spr->mobj->height, false);
				lightnum = *sector->lightlist[light].lightlevel;
				colormap = sector->lightlist[light].extra_colormap;
			}

			if (colormap)
			{
				if (colormap->fog)
					Surf->FlatColor.rgba = HWR_Lighting(LightLevelToLum(lightnum), colormap->rgba, true, false); // SRB2CBTODO: Better RGBA and light/1.4 for fog
				else
					Surf->FlatColor.rgba = HWR_Lighting(LightLevelToLum(lightnum), colormap->rgba, false, true);
			}
			else
				Surf->FlatColor.rgba = HWR_Lighting(LightLevelToLum(lightnum), NORMALFOG, false, false);
		}
		else
			Surf->FlatColor.rgba = NORMALFOG; // Shadows must always be black if there is no colormap


		if (spr->mobj->subsector->sector->lightlevel > 100)
			Surf->FlatColor.s.alpha = (spr->mobj->subsector->sector->lightlevel - 100)- floorheight/4;
		else
			Surf->FlatColor.s.alpha = (spr->mobj->subsector->sector->lightlevel)- floorheight/4;

		GL_SetTransform(&atransform);
		// YAY, shadows now have BACKFACE CULLING, so only the top side of the shadow will draw
		GL_DrawPolygon(Surf, wallVerts, 4, PF_Modulated|PF_Decal|PF_Translucent, PF2_CullBack); // NEWSHADOW
		GL_SetTransform(NULL);
	}
}


// -----------------+
// HWR_DrawMD2      : Draw 3D MD2
//                  : (characters, enemies, flowers, scenery, ...)
// -----------------+
void HWR_DrawMD2(gr_vissprite_t *spr)
{
	FSurfaceInfo Surf;
	char filename[64];
	int frame;
	FTransform p;
	byte color[4]; // For colormaps!

	// We need to have spr->mobj and a subsector, otherwise, don't draw
	if (!spr->mobj)
		return;

	if (!spr->mobj->subsector)
		return;

	// colormap test
	// Perform colormapping and shading by proper sector lightlist // SRB2CBTODO: Seperate this into a seperate function for this to use?
	sector_t *sector;
	sector = spr->mobj->subsector->sector;
	int lightnum;
	extracolormap_t *colormap;
	lightnum = sector->lightlevel;
	colormap = sector->extra_colormap;

	int l = 0;

	if (sector->numlights)
	{
		for (l = 1; l < sector->numlights; l++)
		{
			if (sector->lightlist[l].flags & FF_NOSHADE)
				continue;

			if (sector->lightlist[l].height <= (spr->mobj->z + spr->mobj->height))
				continue;

			if (!((spr->mobj->frame & (FF_FULLBRIGHT))
				  && (!(sector->extra_colormap && sector->extra_colormap->fog))))
				lightnum = *sector->lightlist[l].lightlevel;
			else
				lightnum = 255;

			colormap = sector->lightlist[l].extra_colormap;
		}
	}
	else
	{
		if (!((spr->mobj->frame & (FF_FULLBRIGHT))
			  && (!(sector->extra_colormap && sector->extra_colormap->fog))))
			lightnum = sector->lightlevel;
		else
			lightnum = 255;

		colormap = sector->extra_colormap;
	}

	if (spr->mobj->frame & (FF_FULLBRIGHT))
		lightnum = 255;

	if (colormap)
		Surf.FlatColor.rgba = HWR_Lighting(LightLevelToLum(lightnum), colormap->rgba, true, true);
	else
		Surf.FlatColor.rgba = HWR_Lighting(LightLevelToLum(lightnum), NORMALFOG, false, false);

	md2_t *md2;

	// SRB2CBTODO: Let MD2s use this thing->skin thing!
	if (spr->mobj->player) // don't use spr->mobj->skin, that's a void thing, use the real player skin value
	{
		md2 = &md2_playermodels[spr->mobj->player->skin];
		md2->skin = spr->mobj->player->skin;
	}
	else
		md2 = &md2_models[spr->mobj->sprite];

	// Dont forget to enable depth test because we can't do this like
	// before: polygons models are not sorted

	// 1. load model+texture if not already loaded
	// 2. draw model with correct position, rotation,...
	if (!md2->model)
	{
		sprintf(filename, "md2/%s", md2->filename);
		md2->model = MD2_ReadModel(filename);

#if 0
		if (spr->mobj->player)
			CONS_Printf("Loading MD2...%s (%s)\n", filename, skins[spr->mobj->player->skin].name);
		else
			CONS_Printf("Loading MD2...%s (%s)\n", filename, sprnames[spr->mobj->sprite]);
#endif

		if (!md2->model || !MD2_ReadModel(filename))
		{
			if (spr->mobj->player)
				CONS_Printf("Failed Loading MD2...%s (%s)\n", filename, skins[md2->skin].name);
			else
				CONS_Printf("Failed Loading MD2...%s (%s)\n", filename, sprnames[spr->mobj->sprite]);
			md2->notfound = true;
			return;
		}
	}

	// Look at HWR_ProjectSprite for more
	if (md2->scale > 0.0f)
	{
		FBITFIELD blendmode = 0;
		int *buff;
		ULONG durs = spr->mobj->state->tics;
		ULONG tics = spr->mobj->tics;
		md2_frame_t *curr, *next = NULL;

		if (spr->mobj->flags2 & MF2_TRANSLUCENT) // Preset transparency level
		{
			Surf.FlatColor.s.alpha = 0x40;
			blendmode = PF_Translucent;
		}
		else if (spr->mobj->frame & FF_TRANSMASK)
			blendmode = HWR_TranstableToAlpha((spr->mobj->frame & FF_TRANSMASK)>>FF_TRANSSHIFT, &Surf);
		else
		{
			Surf.FlatColor.s.alpha = 0xFF;
			blendmode = PF_Translucent|PF_Occlude;
		}
		// Update the light level and set the colormap before drawing the md2
		GL_DrawPolygon(&Surf, NULL, 4, blendmode|PF_Modulated|PF_MD2, 0);

		MD2_LoadTexture(md2);
#ifdef WADTEX
		MD2_LoadWadTexture(md2);
#endif

		// SRB2CBTODO: For MD2s, this is not yet correct, use skin frame too!
		frame = spr->mobj->frame % md2->model->header.numFrames;
		buff = md2->model->glCommandBuffer;
		curr = &md2->model->frames[frame];
		if (spr->mobj->state->nextstate != S_DISS && spr->mobj->state->nextstate != S_NULL
			// Do not interpoliate if you're currently standing and about to go to the waiting pose
			// SRB2CBTODO: This is only temporary, until a special mode is made that's made to interpoliating between standing
			&& !(spr->mobj->player && (spr->mobj->state->nextstate == S_PLAY_TAP1 || spr->mobj->state->nextstate == S_PLAY_TAP2) && spr->mobj->state == &states[S_PLAY_STND])
			)
		{
			const int nextframe = states[spr->mobj->state->nextstate].frame % md2->model->header.numFrames;
			next = &md2->model->frames[nextframe];
		}

		// SRB2CBTODO: is there mobj angle?
		p.x = FIXED_TO_FLOAT(spr->mobj->x);
		p.y = FIXED_TO_FLOAT(spr->mobj->y)+md2->offset;

#ifdef SPRITEROLL
		// Use roll angle to offset height.
		if (spr->mobj->rollangle)
		{
			short ang = spr->mobj->rollangle / (ANG45/45);
			if (ang > 180) ang -= 360;
			p.z = FIXED_TO_FLOAT(spr->mobj->z);
			p.z += FIXED_TO_FLOAT(spr->mobj->height) / 180 * abs(ang);
		}
		else
		{
			p.z = FIXED_TO_FLOAT(spr->mobj->z);
		}
#else
		p.z = FIXED_TO_FLOAT(spr->mobj->z);

		if (spr->vflip)
			p.z += FIXED_TO_FLOAT(spr->mobj->height);
#endif

#ifdef SPRITEROLL // SRB2CBTODO: What is this? this changes the md2's color?!!
		if (spr->mobj->rollangle)
		{
			p.anglex = spr->mobj->rollangle / (ANG45/45); // Roll
		}
		else
		{
			p.anglex = 0;
		}
#else
		if (spr->vflip)
			p.anglex = 180.0f;
		else
			p.anglex = 0;
#endif

		// Setup the color of the MD2 for extra ambient light diffusion
		color[0] = Surf.FlatColor.s.red;
		color[1] = Surf.FlatColor.s.green;
		color[2] = Surf.FlatColor.s.blue;
		color[3] = Surf.FlatColor.s.alpha;

		p.angley = FIXED_TO_FLOAT((angle_t)AngleFixed(spr->mobj->angle)); // Yaw
#ifdef VPHYSICS
		p.glrollangle = spr->mobj->pitchangle/(ANG45/45);//0.0f;// Pitch VPHYSICS: glrollangle will sync up with the engine's coords(for slopes and stuff)
#else
		p.glrollangle = 0.0f;
#endif

		// SRB2CBTODO: MD2 scaling support
		float finalscale = md2->scale;

		if (spr->mobj->scale != 100)
		{
			float normscale = md2->scale;
			finalscale = (float)(normscale*(((float)spr->mobj->scale)/100.0f));
		}


		GL_DrawMD2(buff, curr, durs, tics, next, &p, finalscale, color);


		// MD2 SHADOW!!!!

		fixed_t floorheight;

		fixed_t mobjfloor = HWR_OpaqueFloorAtPos(
												 spr->mobj->x, spr->mobj->y,
												 spr->mobj->z, spr->mobj->height);

		floorheight = (spr->mobj->z - mobjfloor)/FRACUNIT;

		GL_DrawMD2Shadow(buff, curr, durs, tics, next, &p, finalscale, 0, 0, 0, spr->mobj);

	}
}


//#define SPRITESPLIT // SRB2CBTODO: Not majorly important at all

// Split up a sprite when it has multiple lightlevels or colormaps on it
#ifdef SPRITESPLIT
static void HWR_SplitSprite(gr_vissprite_t *spr, FOutVector *wallVerts, FSurfaceInfo *Surf, FBITFIELD blendmode)
{
	int i, lightnum;
	sector_t *sector;
	extracolormap_t *colormap;

	sector = spr->mobj->subsector->sector;
	lightnum = sector->lightlevel;
	colormap = sector->extra_colormap;

	float realtop, realbot, top, bot;
	float pegt, pegb, pegmul;
	float height;

	realtop = top = wallVerts[2].y;
	realbot = bot = wallVerts[0].y;

	pegt = wallVerts[2].tow;
	pegb = wallVerts[0].tow;
	pegmul = (pegb - pegt) / (top - bot);

	const byte alpha = Surf->FlatColor.s.alpha;

	for (i = 1; i < sector->numlights; i++)
	{
		if (top < realbot)
			return;

		if (!(sector->lightlist[i].caster->flags & FF_CUTSPRITES))
			continue;

		height = FIXED_TO_FLOAT(sector->lightlist[i].height) - gr_viewz;

		// Not touching the block?
		if (height > top)
			continue;

		// Found a split! The bottom of the new polygon is the height of the
		bot = height;

		if (bot < realbot)
			bot = realbot;

		// light levels
		if (sector->lightlist[i-1].caster)
			lightnum = LightLevelToLum(*sector->lightlist[i-1].lightlevel);
		else
			lightnum = LightLevelToLum(sector->lightlevel);

		// colormaps
		if (sector->lightlist[i-1].extra_colormap)
		{
			if (sector->lightlist[i-1].extra_colormap->fog) // SRB2CBTODO: Better fog colormap casts
				Surf->FlatColor.rgba = HWR_Lighting(lightnum, sector->lightlist[i-1].extra_colormap->rgba, true);
			else
				Surf->FlatColor.rgba = HWR_Lighting(lightnum, sector->lightlist[i-1].extra_colormap->rgba, true);
		}
		else
			Surf->FlatColor.rgba = HWR_Lighting(lightnum, NORMALFOG, false);

		// Recopy the alpha level, it gets reset after HWR_Lighting
		Surf->FlatColor.s.alpha = alpha;

		wallVerts[3].tow = wallVerts[2].tow = pegt + ((realtop - top) * pegmul);
		wallVerts[0].tow = wallVerts[1].tow = pegt + ((realtop - bot) * pegmul);

		// set top/bottom coords
		wallVerts[2].y = wallVerts[3].y = top;
		wallVerts[0].y = wallVerts[1].y = bot;

		// Render the sprite or return here
		GL_DrawPolygon(Surf, wallVerts, 4, blendmode, 0);
	}

	// Other stuff
	top = height;
	bot = realbot;

	if (top > realtop)
		top = realtop;

	// Render the rest of the sprite that's not in the FOF
	if (top <= realbot)
		return;

	// light levels
	if (sector->lightlist[i-1].caster)
		lightnum = LightLevelToLum(*sector->lightlist[i-1].lightlevel);
	else
		lightnum = LightLevelToLum(sector->lightlevel);

	// colormaps
	if (sector->lightlist[i-1].extra_colormap)
	{
		if (sector->lightlist[i-1].extra_colormap->fog) // SRB2CBTODO: Better fog colormap casts
			Surf->FlatColor.rgba = HWR_Lighting(lightnum, sector->lightlist[i-1].extra_colormap->rgba, false);
		else
			Surf->FlatColor.rgba = HWR_Lighting(lightnum, sector->lightlist[i-1].extra_colormap->rgba, true);
	}
	else
		Surf->FlatColor.rgba = HWR_Lighting(lightnum, NORMALFOG, false);

	// Recopy the alpha level, it gets reset by doing lighting // SRB2CBTODO: Make everything specify the alpha level
	Surf->FlatColor.s.alpha = alpha;

	wallVerts[3].tow = wallVerts[2].tow = pegt + ((realtop - top) * pegmul);
	wallVerts[0].tow = wallVerts[1].tow = pegt + ((realtop - bot) * pegmul);

	// set top/bottom coords
	wallVerts[2].y = wallVerts[3].y = top;
	wallVerts[0].y = wallVerts[1].y = bot;

	// Render or return again here
	GL_DrawPolygon(Surf, wallVerts, 4, blendmode, 0);
}
#endif

// -----------------+
// HWR_DrawSprite   : Draw flat sprites
//                  : (characters, enemies, flowers, scenery, ...)
// -----------------+
#if 0 // Experimental stuff to make sprites easier to manage
// -----------------+
// HWR_DrawSprite   : Draw flat sprites
//                  : (characters, enemies, flowers, scenery, ...)
// -----------------+
static void HWR_DrawSprite(gr_vissprite_t *spr)
{
	FOutVector wallVerts[4];
	GLPatch_t *gpatch; // sprite patch converted to hardware
	FSurfaceInfo Surf;

	// We need to have spr->mobj and a subsector, otherwise, don't draw
	if (!spr->mobj)
		return;

	if (!spr->mobj->subsector)
		return;

	// Quickly reject sprites with bad x ranges.
	if (spr->x1 > spr->x2)
		return;

	gpatch = W_CachePatchNum(spr->patchlumpnum, PU_CACHE);

	HWR_GetMappedPatch(gpatch, spr->colormap);

	// create the sprite billboard
	//
	//  3--2
	//  | /|
	//  |/ |
	//  0--1

	spritedef_t *sprdef;
	spriteframe_t *sprframe;
	size_t lump;

	unsigned rot;
	byte flip;
	angle_t ang;
	mobj_t *thing = spr->mobj;

	if (thing->skin && thing->sprite == SPR_PLAY)
		sprdef = &((skin_t *)thing->skin)->spritedef;
	else
		sprdef = &sprites[thing->sprite];

	if ((byte)(thing->frame&FF_FRAMEMASK) >= sprdef->numframes)
	{
		CONS_Printf(PREFIX_WARN "Mobj of type %i with invalid sprite data (%ld) detected and removed.\n", thing->type, (thing->frame&FF_FRAMEMASK));
		if (thing->player)
			P_SetPlayerMobjState(thing, S_PLAY_STND);
		else
			P_SetMobjState(thing, S_DISS);
		return;
	}

	sprframe = &sprdef->spriteframes[thing->frame & FF_FRAMEMASK];

	if (!sprframe)
		return;

	if (sprframe->rotate)
	{
		// choose a different rotation based on player view
		ang = R_PointToAngle(thing->x, thing->y);
		rot = (ang-thing->angle+(angle_t)(ANG45/2)*9)>>29;
		//Fab: lumpid is the index for spritewidth, spriteoffset... tables
		lump = sprframe->lumpid[rot];
		flip = sprframe->flip[rot];
	}
	else
	{
		// use single rotation for all views
		rot = 0;                        //Fab: for vis->patch below
		lump = sprframe->lumpid[0];     //Fab: see note above
		flip = sprframe->flip[0];
	}

	float spry = FIXED_TO_FLOAT(spr->mobj->z + spritecachedinfo[lump].topoffset);
	if (spritecachedinfo[lump].topoffset > 0 && (spritecachedinfo[lump].topoffset < spritecachedinfo[lump].height))
		spry += 4.0f*thing->scale/100;

	// Horizontal
	wallVerts[0].x = wallVerts[3].x = FIXED_TO_FLOAT(spr->mobj->x);
	wallVerts[2].x = wallVerts[1].x = FIXED_TO_FLOAT(spr->mobj->x) + FIXED_TO_FLOAT(spritecachedinfo[lump].width);
	// Adjust the offset
	wallVerts[0].x = wallVerts[3].x -= FIXED_TO_FLOAT(spritecachedinfo[lump].offset);
	wallVerts[2].x = wallVerts[1].x -= FIXED_TO_FLOAT(spritecachedinfo[lump].offset);
	// Vertical
	wallVerts[2].y = wallVerts[3].y = spry;
	wallVerts[0].y = wallVerts[1].y = spry - gpatch->height;

	// SRB2CBTODO: Make sprites like walls so they don't need special transforming

	const float radius = (spr->x2 - spr->x1)/2.0f;
	float scale;

	// When a sprite is high resolution, it means the game should render it 2x as small
	if (spr->mobj->flags & MF_HIRES)
		scale = (float)(100.0f - spr->mobj->scale/2);
	else
		scale = (float)(100.0f - spr->mobj->scale);

	if (spr->mobj->scale != 100)
	{
		wallVerts[0].x = wallVerts[3].x += (radius)*scale/100.0f;
		wallVerts[2].x = wallVerts[1].x -= (radius)*scale/100.0f;

		wallVerts[0].y = wallVerts[1].y += abs(gpatch->topoffset - gpatch->height)*scale/100.0f;
		wallVerts[2].y = wallVerts[3].y += abs(gpatch->topoffset - gpatch->height)*scale/100.0f;
		wallVerts[2].y = wallVerts[3].y -= (gpatch->height)*scale/100.0f;
	}
	else if (spr->mobj->flags & MF_HIRES)
	{
		wallVerts[0].x = wallVerts[3].x += (radius)*scale/100.0f;
		wallVerts[2].x = wallVerts[1].x -= (radius)*scale/100.0f;

		wallVerts[0].y = wallVerts[1].y += abs(gpatch->topoffset - gpatch->height)*scale/100.0f;
		wallVerts[2].y = wallVerts[3].y += abs(gpatch->topoffset - gpatch->height)*scale/100.0f;
		wallVerts[2].y = wallVerts[3].y -= (gpatch->height)*scale/100.0f;
	}


	//  3--2
	//  | /|
	//  |/ |
	//  0--1

	// Make a wall polygon (with 2 triangles), using the floor/ceiling heights,
	// and the 2d map coords of start/end vertices
	// SRB2CBTODO: Adjust Z to camera angle
	// Depth - but the sprites themselves don't have this, so just use an object's current position
	wallVerts[3].z = wallVerts[0].z = FIXED_TO_FLOAT(spr->mobj->y);//spr->tz;
	wallVerts[2].z = wallVerts[1].z = FIXED_TO_FLOAT(spr->mobj->y);//spr->tz;


	wallVerts[3].x = wallVerts[0].x += 0;
	wallVerts[2].x = wallVerts[1].x += 0;
	wallVerts[3].z = wallVerts[0].z += 0;
	wallVerts[2].z = wallVerts[1].z += 0;

#if 0
	// up/down
	wallVerts[3].z = wallVerts[2].z = FIXED_TO_FLOAT(spr->mobj->y)+1;//spr->tz;
	wallVerts[0].z = wallVerts[1].z = FIXED_TO_FLOAT(spr->mobj->y)+20;//spr->tz;
#endif

	//  3--2
	//  | /|
	//  |/ |
	//  0--1

#if 0
	// The camera works like this
	///     90
	//
	// 180  cam >   0
	//
	//      270
	float anglex = (float)(viewpitch>>ANGLETOFINESHIFT)*(360.0f/(float)FINEANGLES);
	//if (spr->mobj->player)
	CONS_Printf("X: %f\n", anglex);
	// PAPERMARIO: Z makes the sprite go away from the camera
	if (anglex > 0 && anglex < 180)
	{
		float offset = 20.0f;
		wallVerts[0].z += offset;
		wallVerts[1].z += offset;

		wallVerts[2].z += -offset;
		wallVerts[3].z += -offset;
	}
	if (anglex > 270)
	{
		float offset = 10.0f;
		wallVerts[0].z += -offset;
		wallVerts[1].z += -offset;

		wallVerts[2].z += offset;
		wallVerts[3].z += offset;
	}
#endif

	if (spr->flip)
	{
		wallVerts[0].sow = wallVerts[3].sow = gpatch->max_s;
		wallVerts[2].sow = wallVerts[1].sow = 0;
	}
    else
    {
		wallVerts[0].sow = wallVerts[3].sow = 0;
		wallVerts[2].sow = wallVerts[1].sow = gpatch->max_s;
	}

	wallVerts[3].tow = wallVerts[2].tow = 0;

	wallVerts[0].tow = wallVerts[1].tow = gpatch->max_t;

	// SRB2CBTODO: SPRITEROLL OPENGL

	// flip the texture coords
#ifdef SPRITEROLL
	if (spr->mobj->rollangle == spr->mobj->destrollangle)
#endif
		if (spr->vflip)
		{
			FLOAT temp = wallVerts[0].tow;
			wallVerts[0].tow = wallVerts[3].tow;
			wallVerts[3].tow = temp;
			temp = wallVerts[1].tow;
			wallVerts[1].tow = wallVerts[2].tow;
			wallVerts[2].tow = temp;
		}

	// Set the RGBA before setting the alpha because alpha gets reset with HWR_Lighting
	Surf.FlatColor.rgba = HWR_Lighting(LightLevelToLum(spr->mobj->subsector->sector->lightlevel), NORMALFOG, false);

#ifndef SPRITESPLIT // Non splited code
	// Perform colormapping and shading by proper sector lightlist
	sector_t *sector;
	sector = spr->mobj->subsector->sector;
	int lightnum;
	extracolormap_t *colormap;
	lightnum = sector->lightlevel;
	colormap = sector->extra_colormap;

	int l = 0;

	if (sector->numlights)
	{
		for (l = 1; l < sector->numlights; l++)
		{
			if (sector->lightlist[l].height <= (spr->mobj->z + spr->mobj->height))
				continue;

			// SRB2CBTODO: FF_NOSHADE support in OpenGL?
			if (sector->lightlist[l].flags & FF_NOSHADE)
				continue;

			if (!((spr->mobj->frame & (FF_FULLBRIGHT))
				  && (!(sector->extra_colormap && sector->extra_colormap->fog))))
				lightnum = *sector->lightlist[l].lightlevel;
			else
				lightnum = 255;

			colormap = sector->lightlist[l].extra_colormap;
		}
	}
	else
	{
		if (!((spr->mobj->frame & (FF_FULLBRIGHT))
			  && (!(sector->extra_colormap && sector->extra_colormap->fog))))
			lightnum = sector->lightlevel;
		else
			lightnum = 255;

		colormap = sector->extra_colormap;
	}

	if (spr->mobj->frame & (FF_FULLBRIGHT))
		lightnum = 255;

	if (colormap)
		Surf.FlatColor.rgba = HWR_Lighting(LightLevelToLum(lightnum), colormap->rgba, true);
	else
		Surf.FlatColor.rgba = HWR_Lighting(LightLevelToLum(lightnum), NORMALFOG, false);
#endif


	FBITFIELD blendmode = 0;
	if (spr->mobj->flags2 & MF2_TRANSLUCENT)
	{
		Surf.FlatColor.s.alpha = 0x40;
		blendmode = PF_Translucent|PF_Occlude;
	}
	else if (spr->mobj->frame & FF_TRANSMASK)
	{
		blendmode = HWR_TranstableToAlpha((spr->mobj->frame & FF_TRANSMASK)>>FF_TRANSSHIFT, &Surf);
	}
#ifdef SRB2CBTODO
	else if (spr->mobj->player && spr->mobj->player->chartrans
			 && spr->mobj->player->chartrans > 0)
	{
		Surf.FlatColor.s.alpha = (255 - spr->mobj->player->chartrans);
		blendmode = PF_Translucent;
		blendmode |= PF_Occlude;
	}
#endif
	else
	{
		Surf.FlatColor.s.alpha = 0xFF;
		blendmode = PF_Translucent|PF_Occlude;
	}


	// Draw the character flat, parallel to the floor,
	// for anything related to looking directly down at a sprite
	// SRB2CBTODO: make this work with the camera
#if 0 // Flat characters in OpenGL
	if (spr->mobj->player)
	{
		float floorheight;

		floorheight = FIXED_TO_FLOAT(spr->mobj->z - spr->mobj->floorz);

		// create the sprite billboard
		//
		//  3--2
		//  | /|
		//  |/ |
		//  0--1
		wallVerts[0].x = wallVerts[3].x = spr->x1;
		wallVerts[2].x = wallVerts[1].x = spr->x2;

		// Shadows are five pixels above the floor
		// fixed so that shadows show for more sprites
		wallVerts[0].y = wallVerts[1].y =
		wallVerts[2].y = wallVerts[3].y =
		spr->ty - gpatch->height - (gpatch->topoffset - gpatch->height) + (floorheight)/FRACUNIT;

		// Spread out top away from the camera.
		// SRB2CBTODO: Make it always move out in the same direction! Requires making its own transform thingy
		wallVerts[0].z = wallVerts[1].z = spr->tz - (gpatch->height-gpatch->topoffset);
		wallVerts[2].z = wallVerts[3].z = spr->tz + gpatch->height - (gpatch->height-gpatch->topoffset);

		// transform
		wv = wallVerts;

		for (i = 0; i < 4; i++,wv++)
		{
			HWR_Transform(&wv->x, &wv->y, &wv->z, true);
		}

		if (spr->flip)
		{
			wallVerts[0].sow = wallVerts[3].sow = gpatch->max_s;
			wallVerts[2].sow = wallVerts[1].sow = 0;
		}
		else
		{
			wallVerts[0].sow = wallVerts[3].sow = 0;
			wallVerts[2].sow = wallVerts[1].sow = gpatch->max_s;
		}
		wallVerts[3].tow = wallVerts[2].tow = 0;

		wallVerts[0].tow = wallVerts[1].tow = gpatch->max_t;

	} // flatness
#endif

	// SRB2CBTODO: This could be used for a LOT of things....
	if (cv_speed.value != 16 && cv_speed.value < 360 && cv_grtest.value == 4) // SRB2CBTODO: globalrollangle
	{
		if (spr->mobj->player)
		{
			Surf.PolyRotate = leveltime % 360;//cv_speed.value; // SRB2CBTODO: TextRotate support
			blendmode |= PF_Rotate;
		}
	}

	if (spr->mobj->rollangle && !(spr->mobj->rollangle == spr->mobj->destrollangle)) // SPRITEROLL
	{
		Surf.PolyRotate = spr->mobj->rollangle / (ANG45/45);
		blendmode |= PF_Rotate;
	}
	else
	{
		Surf.PolyRotate = 0;
		if (blendmode & PF_Rotate)
			blendmode &= ~PF_Rotate;
	}

	// SRB2CBTODO: Splitted sprites in OpenGL
#ifdef SPRITESPLIT
	if (spr->mobj->subsector->sector->numlights)
		HWR_SplitSprite(spr, wallVerts, &Surf, blendmode|PF_Modulated);
	else
#endif
#ifndef SORTING
	GL_SetTransform(&atransform);
#endif
	GL_DrawPolygon(&Surf, wallVerts, 4, blendmode|PF_Modulated, 0); // normal sprite drawing
	//HWR_ProjectWall(wallVerts, &Surf, blendmode|PF_Modulated);
#ifndef SORTING
	GL_SetTransform(NULL);
#endif

	//HWR_SpriteLighting(wallVerts); // SRB2CBTODO: !

	// Draw shadow AFTER SPRITE.
	if (cv_shadow.value // Shadows enabled
		// Don't even bother drawing the shadow when any of these conditions are met
		&& !(spr->mobj->flags & MF_SCENERY && spr->mobj->flags & MF_SPAWNCEILING && spr->mobj->flags & MF_NOGRAVITY) // Ceiling scenery has no shadow.
		&& !(spr->mobj->type == MT_THOK) // Thok graphics should never have a shadow

		// These should never have a shadow
		&& !(spr->mobj->type == MT_STEAM)
		&& !(spr->mobj->type == MT_DROWNNUMBERS)
		&& !(spr->mobj->type == MT_NIGHTSPARKLE)
		&& !(spr->mobj->type == MT_HOOP)
		&& !(spr->mobj->type == MT_FLINGCOIN)
		&& !(spr->mobj->type == MT_FLINGRING)
		&& !(spr->mobj->type == MT_RAIN)
		&& !(spr->mobj->type == MT_SNOWFLAKE)
		&& !(spr->mobj->flags & MF_NOSECTOR)
		&& !(spr->mobj->flags & MF_NOBLOCKMAP)
		// Balloons have no shadow to speed up framerate
		&& !(spr->mobj->type == MT_BALLOON)
		&& !(spr->mobj->type == MT_BALLOONR)
#ifdef PARTICLES
		&& !(spr->mobj->type == MT_PARTICLE) // Particles have no shadow to speed up framerate too
		&& !(spr->mobj->type == MT_LIGHTPARTICLE)
		&& !(spr->mobj->type == MT_SPARTICLE)
#endif
#ifdef SRB2CBTODO
		// No shadows for translucent players
		&& !(spr->mobj->player && spr->mobj->player->chartrans && spr->mobj->player->chartrans > 0)
#endif
#ifdef SRB2CBTODO // corona light stuff
		&& !(t_lspr[spr->mobj->sprite]->type // Things with dynamic lights have no shadow.
			 && (!spr->mobj->player || spr->mobj->player->powers[pw_super])) // Except for non-super players.
#endif
		&& (spr->mobj->z >= spr->mobj->floorz)) // you can't be below the ground
	{
		HWR_DrawShadow(spr, wallVerts, &Surf, gpatch);
	}

	switch(spr->mobj->type)
	{
		case MT_LIGHTPARTICLE:
		case MT_NIGHTSPARKLE:
			/// SRB2CBTODO: Add a special way to make a sprite draw a corona
			HWR_DrawCorona(FIXED_TO_FLOAT(spr->mobj->x), FIXED_TO_FLOAT(spr->mobj->y),
						   FIXED_TO_FLOAT(spr->mobj->z)+FIXED_TO_FLOAT(spr->mobj->height>>1),
						   HWR_Lighting(LightLevelToLum(200), NORMALFOG, false), 16.0f); // 0xffffefff
			break;
		default:
			break;
	}

	if (spr->mobj->type == MT_PLAYER) // SRB2CBTODO: Find a real use for these and make then REAL light sources
	{
		if (cv_grtest.value == 3)
			HWR_AddDynLight(FIXED_TO_FLOAT(spr->mobj->x), FIXED_TO_FLOAT(spr->mobj->y),
							FIXED_TO_FLOAT(spr->mobj->z)+FIXED_TO_FLOAT(spr->mobj->height>>1),
							HWR_Lighting(LightLevelToLum(200), NORMALFOG, false), 64); // 0xffffefff
		else if (cv_grtest.value == 4)
			HWR_DrawCorona(FIXED_TO_FLOAT(spr->mobj->x), FIXED_TO_FLOAT(spr->mobj->y),
						   FIXED_TO_FLOAT(spr->mobj->z)+FIXED_TO_FLOAT(spr->mobj->height>>1),
						   HWR_Lighting(LightLevelToLum(200), NORMALFOG, false), 32.0f); // 0xffffefff
		else if (cv_grtest.value == 5)
			HWR_AddDynShadow(FIXED_TO_FLOAT(spr->mobj->x), FIXED_TO_FLOAT(spr->mobj->y),
							 FIXED_TO_FLOAT(spr->mobj->z)+FIXED_TO_FLOAT(spr->mobj->height>>1),
							 HWR_Lighting(LightLevelToLum(5), NORMALFOG, false), 64, spr); // SRB2CBTODO: Allow shadows to be based on real light
	}

}
#else
static void HWR_DrawSprite(gr_vissprite_t *spr)
{
	byte i;
	FOutVector wallVerts[4];
	FOutVector *wv;
	GLPatch_t *gpatch; // sprite patch converted to hardware
	FSurfaceInfo Surf;

	// We need to have spr->mobj and a subsector, otherwise, don't draw
	if (!spr->mobj)
		return;

	if (!spr->mobj->subsector)
		return;

	// Quickly reject sprites with bad x ranges.
	if (spr->x1 > spr->x2)
		return;

	gpatch = W_CachePatchNum(spr->patchlumpnum, PU_CACHE);

	HWR_GetMappedPatch(gpatch, spr->colormap);

	// create the sprite billboard
	//
	//  3--2
	//  | /|
	//  |/ |
	//  0--1
	wallVerts[0].x = wallVerts[3].x = spr->x1;
	wallVerts[2].x = wallVerts[1].x = spr->x2;
	wallVerts[2].y = wallVerts[3].y = spr->ty;
	wallVerts[0].y = wallVerts[1].y = spr->ty - gpatch->height;

	const float radius = (spr->x2 - spr->x1)/2.0f;
	float scale;

	// When a sprite is high resolution, it means the game should render it 2x as small
	if (spr->mobj->flags & MF_HIRES)
		scale = (float)(100.0f - spr->mobj->scale/2);
	else
		scale = (float)(100.0f - spr->mobj->scale);

	if (spr->mobj->scale != 100)
	{
		wallVerts[0].x = wallVerts[3].x += (radius)*scale/100.0f;
		wallVerts[2].x = wallVerts[1].x -= (radius)*scale/100.0f;

		wallVerts[0].y = wallVerts[1].y += abs(gpatch->topoffset - gpatch->height)*scale/100.0f;
		wallVerts[2].y = wallVerts[3].y += abs(gpatch->topoffset - gpatch->height)*scale/100.0f;
		wallVerts[2].y = wallVerts[3].y -= (gpatch->height)*scale/100.0f;
	}
	else if (spr->mobj->flags & MF_HIRES)
	{
		wallVerts[0].x = wallVerts[3].x += (radius)*scale/100.0f;
		wallVerts[2].x = wallVerts[1].x -= (radius)*scale/100.0f;

		wallVerts[0].y = wallVerts[1].y += abs(gpatch->topoffset - gpatch->height)*scale/100.0f;
		wallVerts[2].y = wallVerts[3].y += abs(gpatch->topoffset - gpatch->height)*scale/100.0f;
		wallVerts[2].y = wallVerts[3].y -= (gpatch->height)*scale/100.0f;
	}

	// Make a wall polygon (with 2 triangles), using the floor/ceiling heights,
	// and the 2d map coords of start/end vertices
	// SRB2CBTODO: Adjust Z to camera angle
	wallVerts[3].z = wallVerts[2].z = spr->tz;
	wallVerts[1].z = wallVerts[0].z = spr->tz;

	//  3--2
	//  | /|
	//  |/ |
	//  0--1

#if 0
	// The camera works like this
	///     90
	//
	// 180  cam >   0
	//
	//      270
	float anglex = (float)(viewpitch>>ANGLETOFINESHIFT)*(360.0f/(float)FINEANGLES);
	//if (spr->mobj->player)
		CONS_Printf("X: %f\n", anglex);
	// PAPERMARIO: Z makes the sprite go away from the camera
	if (anglex > 0 && anglex < 180)
	{
		float offset = 20.0f;
		wallVerts[0].z += offset;
		wallVerts[1].z += offset;

		wallVerts[2].z += -offset;
		wallVerts[3].z += -offset;
	}
	if (anglex > 270)
	{
		float offset = 10.0f;
		wallVerts[0].z += -offset;
		wallVerts[1].z += -offset;

		wallVerts[2].z += offset;
		wallVerts[3].z += offset;
	}
#endif

	// transform
	wv = wallVerts;

	// SRB2CBTODO: Allow sprites to be atransformed for better compilancy
	for (i = 0; i < 4; i++, wv++)
	{
		HWR_Transform(&wv->x, &wv->y, &wv->z, true);
	}

	if (spr->flip)
	{
		wallVerts[0].sow = wallVerts[3].sow = gpatch->max_s;
		wallVerts[2].sow = wallVerts[1].sow = 0;
	}
    else
    {
		wallVerts[0].sow = wallVerts[3].sow = 0;
		wallVerts[2].sow = wallVerts[1].sow = gpatch->max_s;
	}

	wallVerts[3].tow = wallVerts[2].tow = 0;

	wallVerts[0].tow = wallVerts[1].tow = gpatch->max_t;

	// SRB2CBTODO: SPRITEROLL OPENGL

	// flip the texture coords
#ifdef SPRITEROLL
	if (spr->mobj->rollangle == spr->mobj->destrollangle)
#endif
	if (spr->vflip)
	{
		FLOAT temp = wallVerts[0].tow;
		wallVerts[0].tow = wallVerts[3].tow;
		wallVerts[3].tow = temp;
		temp = wallVerts[1].tow;
		wallVerts[1].tow = wallVerts[2].tow;
		wallVerts[2].tow = temp;
	}

	// Set the RGBA before setting the alpha because alpha gets reset with HWR_Lighting
	Surf.FlatColor.rgba = HWR_Lighting(LightLevelToLum(spr->mobj->subsector->sector->lightlevel), NORMALFOG, false, false);

#ifndef SPRITESPLIT // Non splited code
	// Perform colormapping and shading by proper sector lightlist
	sector_t *sector;
	sector = spr->mobj->subsector->sector;
	int lightnum;
	extracolormap_t *colormap;
	lightnum = sector->lightlevel;
	colormap = sector->extra_colormap;

	int l = 0;

	if (sector->numlights)
	{
		for (l = 1; l < sector->numlights; l++)
		{
			if (sector->lightlist[l].height <= (spr->mobj->z + spr->mobj->height))
				continue;

			// SRB2CBTODO: FF_NOSHADE support in OpenGL?
			if (sector->lightlist[l].flags & FF_NOSHADE)
				continue;

			if (!((spr->mobj->frame & (FF_FULLBRIGHT))
				  && (!(sector->extra_colormap && sector->extra_colormap->fog))))
				lightnum = *sector->lightlist[l].lightlevel;
			else
				lightnum = 255;

			colormap = sector->lightlist[l].extra_colormap;
		}
	}
	else
	{
		if (!((spr->mobj->frame & (FF_FULLBRIGHT))
			  && (!(sector->extra_colormap && sector->extra_colormap->fog))))
			lightnum = sector->lightlevel;
		else
			lightnum = 255;

		colormap = sector->extra_colormap;
	}

	if (spr->mobj->frame & (FF_FULLBRIGHT))
		lightnum = 255;

	if (colormap)
		Surf.FlatColor.rgba = HWR_Lighting(LightLevelToLum(lightnum), colormap->rgba, true, true);
	else
		Surf.FlatColor.rgba = HWR_Lighting(LightLevelToLum(lightnum), NORMALFOG, false, false);
#endif


	FBITFIELD blendmode = 0;
	if (spr->mobj->flags2 & MF2_TRANSLUCENT)
	{
		Surf.FlatColor.s.alpha = 0x40;
		blendmode = PF_Translucent|PF_Occlude;
	}
	else if (spr->mobj->frame & FF_TRANSMASK)
	{
		blendmode = HWR_TranstableToAlpha((spr->mobj->frame & FF_TRANSMASK)>>FF_TRANSSHIFT, &Surf);
	}
#ifdef SRB2CBTODO
	else if (spr->mobj->player && spr->mobj->player->chartrans
			 && spr->mobj->player->chartrans > 0)
	{
		Surf.FlatColor.s.alpha = (255 - spr->mobj->player->chartrans);
		blendmode = PF_Translucent;
		blendmode |= PF_Occlude;
	}
#endif
	else
	{
		Surf.FlatColor.s.alpha = 0xFF;
		blendmode = PF_Translucent|PF_Occlude;
	}


	// Draw the character flat, parallel to the floor,
	// for anything related to looking directly down at a sprite
	// SRB2CBTODO: make this work with the camera
#if 0 // Flat characters in OpenGL
	if (spr->mobj->player)
	{
		float floorheight;

		floorheight = FIXED_TO_FLOAT(spr->mobj->z - spr->mobj->floorz);

		// create the sprite billboard
		//
		//  3--2
		//  | /|
		//  |/ |
		//  0--1
		wallVerts[0].x = wallVerts[3].x = spr->x1;
		wallVerts[2].x = wallVerts[1].x = spr->x2;

		// Shadows are five pixels above the floor
		// fixed so that shadows show for more sprites
		wallVerts[0].y = wallVerts[1].y =
		wallVerts[2].y = wallVerts[3].y =
		spr->ty - gpatch->height - (gpatch->topoffset - gpatch->height) + (floorheight)/FRACUNIT;

		// Spread out top away from the camera.
		// SRB2CBTODO: Make it always move out in the same direction! Requires making its own transform thingy
		wallVerts[0].z = wallVerts[1].z = spr->tz - (gpatch->height-gpatch->topoffset);
		wallVerts[2].z = wallVerts[3].z = spr->tz + gpatch->height - (gpatch->height-gpatch->topoffset);

		// transform
		wv = wallVerts;

		for (i = 0; i < 4; i++,wv++)
		{
			HWR_Transform(&wv->x, &wv->y, &wv->z, true);
		}

		if (spr->flip)
		{
			wallVerts[0].sow = wallVerts[3].sow = gpatch->max_s;
			wallVerts[2].sow = wallVerts[1].sow = 0;
		}
		else
		{
			wallVerts[0].sow = wallVerts[3].sow = 0;
			wallVerts[2].sow = wallVerts[1].sow = gpatch->max_s;
		}
		wallVerts[3].tow = wallVerts[2].tow = 0;

		wallVerts[0].tow = wallVerts[1].tow = gpatch->max_t;

	} // flatness
#endif

	 // SRB2CBTODO: This could be used for a LOT of things....
	if (cv_speed.value != 16 && cv_speed.value < 360 && cv_grtest.value == 4) // SRB2CBTODO: globalrollangle
	{
		if (spr->mobj->player)
		{
			Surf.PolyRotate = leveltime % 360;//cv_speed.value; // SRB2CBTODO: TextRotate support
			blendmode |= PF_Rotate;
		}
	}

	if (spr->mobj->rollangle && !(spr->mobj->rollangle == spr->mobj->destrollangle)) // SPRITEROLL
	{
		Surf.PolyRotate = spr->mobj->rollangle / (ANG45/45);
		blendmode |= PF_Rotate;
	}
	else
	{
		Surf.PolyRotate = 0;
		if (blendmode & PF_Rotate)
			blendmode &= ~PF_Rotate;
	}

	// SRB2CBTODO: Splitted sprites in OpenGL
#ifdef SPRITESPLIT
	if (spr->mobj->subsector->sector->numlights)
		HWR_SplitSprite(spr, wallVerts, &Surf, blendmode|PF_Modulated);
	else
#endif
		GL_DrawPolygon(&Surf, wallVerts, 4, blendmode|PF_Modulated, 0); // normal sprite drawing

	HWR_SpriteLighting(wallVerts); // SRB2CBTODO: !

	// Draw shadow AFTER SPRITE.
	if (cv_shadow.value // Shadows enabled
		// Don't even bother drawing the shadow when any of these conditions are met
		&& !(spr->mobj->flags & MF_SCENERY && spr->mobj->flags & MF_SPAWNCEILING && spr->mobj->flags & MF_NOGRAVITY) // Ceiling scenery has no shadow.
		&& !(spr->mobj->type == MT_THOK) // Thok graphics should never have a shadow

		// These should never have a shadow
		&& !(spr->mobj->type == MT_STEAM)
		&& !(spr->mobj->type == MT_DROWNNUMBERS)
		&& !(spr->mobj->type == MT_NIGHTSPARKLE)
		&& !(spr->mobj->type == MT_HOOP)
		&& !(spr->mobj->type == MT_FLINGCOIN)
		&& !(spr->mobj->type == MT_FLINGRING)
		&& !(spr->mobj->type == MT_RAIN)
		&& !(spr->mobj->type == MT_SNOWFLAKE)
		&& !(spr->mobj->flags & MF_NOSECTOR)
		&& !(spr->mobj->flags & MF_NOBLOCKMAP)
		// Balloons have no shadow to speed up framerate
		&& !(spr->mobj->type == MT_BALLOON)
		&& !(spr->mobj->type == MT_BALLOONR)
#ifdef PARTICLES
		&& !(spr->mobj->type == MT_PARTICLE) // Particles have no shadow to speed up framerate too
		&& !(spr->mobj->type == MT_LIGHTPARTICLE)
		&& !(spr->mobj->type == MT_SPARTICLE)
#endif
#ifdef SRB2CBTODO
		// No shadows for translucent players
		&& !(spr->mobj->player && spr->mobj->player->chartrans && spr->mobj->player->chartrans > 0)
#endif
#ifdef SRB2CBTODO // corona light stuff
		&& !(t_lspr[spr->mobj->sprite]->type // Things with dynamic lights have no shadow.
		&& (!spr->mobj->player || spr->mobj->player->powers[pw_super])) // Except for non-super players.
#endif
		&& (spr->mobj->z >= spr->mobj->floorz)) // you can't be below the ground
	{
		HWR_DrawShadow(spr, wallVerts, &Surf, gpatch);
	}

	switch(spr->mobj->type)
	{
		case MT_LIGHTPARTICLE:
		case MT_NIGHTSPARKLE:
		/// SRB2CBTODO: Add a special way to make a sprite draw a corona
		HWR_DrawCorona(FIXED_TO_FLOAT(spr->mobj->x), FIXED_TO_FLOAT(spr->mobj->y),
					   FIXED_TO_FLOAT(spr->mobj->z)+FIXED_TO_FLOAT(spr->mobj->height>>1),
					   HWR_Lighting(LightLevelToLum(200), NORMALFOG, false, true), 16.0f); // 0xffffefff
			break;
		default:
			break;
	}

	if (spr->mobj->type == MT_PLAYER) // SRB2CBTODO: Find a real use for these and make then REAL light sources
	{
		if (cv_grtest.value == 3)
			HWR_AddDynLight(FIXED_TO_FLOAT(spr->mobj->x), FIXED_TO_FLOAT(spr->mobj->y),
							FIXED_TO_FLOAT(spr->mobj->z)+FIXED_TO_FLOAT(spr->mobj->height>>1),
							HWR_Lighting(LightLevelToLum(200), NORMALFOG, false, true), 64); // 0xffffefff
		else if (cv_grtest.value == 4)
			HWR_DrawCorona(FIXED_TO_FLOAT(spr->mobj->x), FIXED_TO_FLOAT(spr->mobj->y),
						   FIXED_TO_FLOAT(spr->mobj->z)+FIXED_TO_FLOAT(spr->mobj->height>>1),
						   HWR_Lighting(LightLevelToLum(200), NORMALFOG, false, true), 32.0f); // 0xffffefff
		else if (cv_grtest.value == 5)
			HWR_AddDynShadow(FIXED_TO_FLOAT(spr->mobj->x), FIXED_TO_FLOAT(spr->mobj->y),
							 FIXED_TO_FLOAT(spr->mobj->z)+FIXED_TO_FLOAT(spr->mobj->height>>1),
							 HWR_Lighting(LightLevelToLum(5), NORMALFOG, false, true), 64, spr); // SRB2CBTODO: Allow shadows to be based on real light
	}

}

#endif

// Sprite drawer for precipitation
static void HWR_DrawPrecipitationSprite(gr_vissprite_t *spr)
{
	byte i;
	FBITFIELD blendmode = 0;
	FOutVector wallVerts[4];
	FOutVector *wv;
	GLPatch_t *gpatch; // sprite patch converted to hardware
	FSurfaceInfo Surf;

	if (!spr->mobj)
		return;

	if (!spr->mobj->subsector)
		return;

	// cache sprite graphics
	gpatch = W_CachePatchNum(spr->patchlumpnum, PU_CACHE);

	// cache the patch in the graphics card memory
	HWR_GetMappedPatch(gpatch, spr->colormap);

	// create the sprite billboard
	//
	//  3--2
	//  | /|
	//  |/ |
	//  0--1
	wallVerts[0].x = wallVerts[3].x = spr->x1;
	wallVerts[2].x = wallVerts[1].x = spr->x2;
	wallVerts[2].y = wallVerts[3].y = spr->ty;
	wallVerts[0].y = wallVerts[1].y = spr->ty - gpatch->height;

	// make a wall polygon (with 2 triangles), using the floor/ceiling heights,
	// and the 2d map coords of start/end vertices
	wallVerts[0].z = wallVerts[1].z = wallVerts[2].z = wallVerts[3].z = spr->tz;

	// transform
	wv = wallVerts;

	for (i = 0; i < 4; i++, wv++)
	{
		HWR_Transform(&wv->x, &wv->y, &wv->z, true);
	}

	wallVerts[0].sow = wallVerts[3].sow = 0;
	wallVerts[2].sow = wallVerts[1].sow = gpatch->max_s;

	wallVerts[3].tow = wallVerts[2].tow = 0;
	wallVerts[0].tow = wallVerts[1].tow = gpatch->max_t;


	// Perform colormapping and shading by proper sector lightlist
	sector_t *sector;
	sector = spr->mobj->subsector->sector;
	int lightnum;
	extracolormap_t *colormap;
	lightnum = sector->lightlevel;
	colormap = sector->extra_colormap;

	int l;

	if (sector->numlights)
	{
		for (l = 1; l < sector->numlights; l++)
		{
			if (sector->lightlist[l].height <= (spr->mobj->z + spr->mobj->height))
				continue;

			if (!((spr->mobj->frame & (FF_FULLBRIGHT|FF_TRANSMASK))
				  && (!(spr->mobj->subsector->sector->extra_colormap
						&& spr->mobj->subsector->sector->extra_colormap->fog))))
				lightnum = *sector->lightlist[l].lightlevel;
			else
				lightnum = 255;

			colormap = sector->lightlist[l].extra_colormap;
		}
	}
	else
	{
		if (!((spr->mobj->frame & (FF_FULLBRIGHT|FF_TRANSMASK))
			  && (!(spr->mobj->subsector->sector->extra_colormap
					&& spr->mobj->subsector->sector->extra_colormap->fog))))
			lightnum = sector->lightlevel;
		else
			lightnum = 255;

		colormap = sector->extra_colormap;
	}

	if (spr->mobj->frame & (FF_FULLBRIGHT))
		lightnum = 255;

	if (colormap)
		Surf.FlatColor.rgba = HWR_Lighting(LightLevelToLum(lightnum), colormap->rgba, true, true);
	else
		Surf.FlatColor.rgba = HWR_Lighting(LightLevelToLum(lightnum), NORMALFOG, false, false);


	blendmode = HWR_TranstableToAlpha((spr->mobj->frame & FF_TRANSMASK)>>FF_TRANSSHIFT, &Surf);

	// Make sure to define the alpha level
	Surf.FlatColor.s.alpha = 255;

	GL_DrawPolygon(&Surf, wallVerts, 4, blendmode|PF_Modulated|PF_Occlude, 0);
}

// --------------------------------------------------------------------------
// Sort vissprites by distance
// --------------------------------------------------------------------------
static gr_vissprite_t gr_vsprsortedhead;

static void HWR_SortVisSprites(void)
{
	size_t i, count;
	gr_vissprite_t *ds;
	gr_vissprite_t *best = NULL;
	gr_vissprite_t unsorted;
	float bestdist;

	count = gr_vissprite_p - gr_vissprites;

	unsorted.next = unsorted.prev = &unsorted;

	if (!count)
		return;

	for (ds = gr_vissprites; ds < gr_vissprite_p; ds++)
	{
		ds->next = ds+1;
		ds->prev = ds-1;
	}

	gr_vissprites[0].prev = &unsorted;
	unsorted.next = &gr_vissprites[0];
	(gr_vissprite_p-1)->next = &unsorted;
	unsorted.prev = gr_vissprite_p-1;

	// Pull the vissprites out by scale
	gr_vsprsortedhead.next = gr_vsprsortedhead.prev = &gr_vsprsortedhead;
	for (i = 0; i < count; i++)
	{
		bestdist = ZCLIP_PLANE-1;
		for (ds = unsorted.next; ds != &unsorted; ds = ds->next)
		{
			if (ds->tz > bestdist)
			{
				bestdist = ds->tz;
				best = ds;
			}
		}
		best->next->prev = best->prev;
		best->prev->next = best->next;
		best->next = &gr_vsprsortedhead;
		best->prev = gr_vsprsortedhead.prev;
		gr_vsprsortedhead.prev->next = best;
		gr_vsprsortedhead.prev = best;
	}
}

// A drawnode is something that points to a 3D floor, 3D side, or masked
// middle texture. This is used for sorting with sprites and other transparent textures.

// Transparent walls
size_t numtranswalls = 0;
// Transparent floors
size_t numtransplanes = 0;
// Transparent wall data
transwallinfo_t *transwallinfo = NULL;
// Transparent plane data
transplaneinfo_t *transplaneinfo = NULL;

static void HWR_RenderTransparentWall(wallVert3D *wallVerts, FSurfaceInfo *pSurf, FBITFIELD blendmode, const sector_t *sector, boolean fogwall);




typedef struct gr_drawnode_s
{
	transplaneinfo_t *plane;
	transwallinfo_t *wall;
	gr_vissprite_t *sprite;

	struct gr_drawnode_s *next;
	struct gr_drawnode_s *prev;
} gr_drawnode_t;





















static size_t drawcount = 0;

// Adds a transparent floor to a list to be properly rendered elsewhere
static void HWR_AddTransparentFloor(const sector_t *sector, lumpnum_t lumpnum, extrasubsector_t *xsub,
	fixed_t fixedheight, byte lightlevel, byte alpha, sector_t *FOFSector, FBITFIELD blendmode, extracolormap_t *planecolormap)
{
	// Floor doesn't need to be drawn if it's completely invisible
	if (alpha == 0)
		return;
	// This unfortunately can only be freed when there is no data at all, otherwise there's an array lookup error
	transplaneinfo = realloc(transplaneinfo,
						   sizeof *transplaneinfo*(numtransplanes + 1));

	transplaneinfo[numtransplanes].sector = sector;
	transplaneinfo[numtransplanes].fixedheight = fixedheight;
	transplaneinfo[numtransplanes].lightlevel = lightlevel;
	transplaneinfo[numtransplanes].lumpnum = lumpnum;
	transplaneinfo[numtransplanes].xsub = xsub;
	transplaneinfo[numtransplanes].alpha = alpha;
	transplaneinfo[numtransplanes].FOFSector = FOFSector;
	transplaneinfo[numtransplanes].drawcount = drawcount++;
	transplaneinfo[numtransplanes].planecolormap = planecolormap;
	transplaneinfo[numtransplanes].blendmode = blendmode;
	numtransplanes++;
}

// Adds a transparent wall to a list to be properly rendered elsewhere
// NOTE: Just like, HWR_RenderTransparentFloor, all the needed data for the line
// must be copied because gr_frontsector will change when this line is sorted
static void HWR_AddTransparentWall(wallVert3D *wallVerts, FSurfaceInfo *pSurf, FBITFIELD blendmode, int texnum, const sector_t *sector, boolean fogwall) // NEEDS extra data of the posistion for sorting later
{
	// Wall doesn't need to be drawn if it's completely invisible
	if (pSurf->FlatColor.s.alpha == 0)
		return;
	// This unfortunately can only be freed when there is no data at all, otherwise there's an array lookup error
	transwallinfo = realloc(transwallinfo,
							    sizeof *transwallinfo*(numtranswalls + 1));

	memcpy(transwallinfo[numtranswalls].wallVerts, wallVerts, sizeof (transwallinfo[numtranswalls].wallVerts));
	memcpy(&transwallinfo[numtranswalls].Surf, pSurf, sizeof (FSurfaceInfo));
	transwallinfo[numtranswalls].texnum = texnum;
	transwallinfo[numtranswalls].blendmode = blendmode;
	transwallinfo[numtranswalls].drawcount = drawcount++;

	// Kalaron: Compare top coords incase this wall is sloped
	float walltop = wallVerts[2].y > wallVerts[3].y ? wallVerts[2].y : wallVerts[3].y;
	float wallbottom = wallVerts[0].y < wallVerts[1].y ? wallVerts[0].y : wallVerts[1].y;


	transwallinfo[numtranswalls].walltop = walltop;
	transwallinfo[numtranswalls].wallbottom = wallbottom;

	transwallinfo[numtranswalls].sector = sector;
	transwallinfo[numtranswalls].fogwall = fogwall;
	numtranswalls++;
}

// Specifically draws walls with holes in the texture,
// only opaque holed walls need to use this
//
// Also, these walls don't need to be sorted because
// OpenGL treats them as if the transparency just isn't part of the polygon!
static void HWR_AddPeggedWall(wallVert3D *wallVerts, FSurfaceInfo *pSurf, int texnum)
{
	// This function only draws opaque walls,
	// make sure the alpha is opaque
	pSurf->FlatColor.s.alpha = 255;
	// When somthing has PF_Environment on it, passing PF_Translucent
	// removes some artifacts when the texture is blurred by mipmaps
	FBITFIELD blendmode = PF_Environment|PF_Translucent;
	HWR_GetTexture(texnum, false);
	HWR_RenderTransparentWall(wallVerts, pSurf, blendmode, NULL, false);
}

// Specifically draws floors with holes in the texture,
// only opaque holed floors need to use this
static void HWR_AddPeggedFloor(sector_t *sector, lumpnum_t lumpnum, extrasubsector_t *xsub,
							 fixed_t fixedheight, byte lightlevel, byte alpha, sector_t *FOFSector, extracolormap_t *planecolormap)
{
	// This function only draws opaque floors,
	// make sure the alpha is opaque
	alpha = 255;
	// When somthing has PF_Environment on it, passing PF_Translucent
	// removes some artifacts when the texture is blurred by mipmaps
	FBITFIELD blendmode = PF_Environment|PF_Translucent;
	HWR_GetFlat(lumpnum, false);
	HWR_RenderPlane(sector, xsub, fixedheight, blendmode, lightlevel, lumpnum, FOFSector, alpha, planecolormap);
}

//
// HWR_CreateDrawNodes
// Creates and sorts a list of drawnodes for the scene being rendered.
//
// Bubble sort implementation:
// Everything is dumpped into one giant list of all transparent planes, walls, and sprites in one scene
// They are re-added into the list of drawing by their back-to-front order
// Everything must be drawn IN ORDER, simply having your normal XYZ coordinates at different locations won't cut it for
// transparent stuff
//
// Sprites are already sorted, this makes it a bit easier:
// If nothing is done, the geometry is blended in front of the sprite, otherwise its behind the sprite
// Any sprite closer to the camera then a piece of map geometry simple needs to be drawn AFTER a piece of wall
#ifndef SORTING
static void HWR_CreateDrawNodes(void) // SRB2CBTODO: This is really OpenGL's R_Drawmasked
{
	size_t i = 0, p = 0, prev = 0, loop;

	// SRB2CBTODO: Optimize the sort list
	// SRB2CBTODO: when viewing a floor in front and wall in back, the overlap is wrong
	// SRB2CBTODO: Major thing needing fixing is that sprites are always overlapped with this

	gr_drawnode_t *sortnode = memset(malloc((sizeof(transplaneinfo_t)*numtransplanes) + (sizeof(transwallinfo_t)*numtranswalls)),
									 0, (sizeof(transplaneinfo_t)*numtransplanes) + (sizeof(transwallinfo_t)*numtranswalls));

	size_t *sortindex = memset(malloc(sizeof(size_t) * (numtransplanes + numtranswalls)),
							   0, sizeof(size_t) * (numtransplanes + numtranswalls));

	for (i = 0; i < numtransplanes; i++, p++)
	{
		sortnode[p].plane = &transplaneinfo[i];
		sortindex[p] = p;
	}

	for (i = 0; i < numtranswalls; i++, p++)
	{
		sortnode[p].wall = &transwallinfo[i];
		sortindex[p] = p;
	}

	// p is the number of stuff to sort

	// Add the 3D floors, thicksides, and masked textures...
	// Instead of going through drawsegs, we need to iterate
	// through the lists of masked textures and
	// translucent ffloors being drawn.

	// This is a bubble sort! Wahoo!

	// Stuff is sorted:
	//      sortnode[sortindex[0]]   = farthest away
	//      sortnode[sortindex[p-1]] = closest
	// "i" should be closer. "prev" should be further.
	// The lower drawcount is, the further it is from the screen.

	for (loop = 0; loop < p; loop++) // This extra loop is needed to make sure there is no over/under draw // TODO: SRB2CBTODO: USE THIS FOR EVERYTHING
	{
		for (i = 1; i < p; i++)
		{
			prev = i-1;


			if (sortnode[sortindex[i]].plane)
			{
				// What are we comparing it with?
				if (sortnode[sortindex[prev]].plane)
				{
					// Plane (i) is further away than plane (prev)
					if (ABS(sortnode[sortindex[i]].plane->fixedheight - viewz) > ABS(sortnode[sortindex[prev]].plane->fixedheight - viewz))
					{
						size_t temp;

						temp = sortindex[prev];
						sortindex[prev] = sortindex[i];
						sortindex[i] = temp;
					}
				}
				else if (sortnode[sortindex[prev]].wall)
				{
					// Compare Z coords wih Plane (i) and wall (prev)
					if (ABS(sortnode[sortindex[prev]].wall->wallbottom - gr_viewz) <= ABS(sortnode[sortindex[i]].plane->fixedheight/FRACUNIT - gr_viewz))
					{
						size_t temp;

						temp = sortindex[prev];
						sortindex[prev] = sortindex[i];
						sortindex[i] = temp;
					}
				}
			}
			else if (sortnode[sortindex[i]].wall)
			{
				// What are we comparing it with?
				if (sortnode[sortindex[prev]].plane)
				{
					// Wall (i) is further than plane(prev)
					if (ABS(sortnode[sortindex[i]].wall->wallbottom - gr_viewz) > ABS(sortnode[sortindex[prev]].plane->fixedheight/FRACUNIT - gr_viewz))
					{
						size_t temp;

						temp = sortindex[prev];
						sortindex[prev] = sortindex[i];
						sortindex[i] = temp;
					}
				}
				else if (sortnode[sortindex[prev]].wall)
				{
					// Wall (i) is further than wall (prev)
					if (sortnode[sortindex[i]].wall->drawcount > sortnode[sortindex[prev]].wall->drawcount)
					{
						size_t temp;

						temp = sortindex[prev];
						sortindex[prev] = sortindex[i];
						sortindex[i] = temp;
					}
				}
			}

		} // done with sorting
	} // done with extra loop to prevent redraw // TODO: Use this for everything

	//
	//GL_SetTransform(NULL);
	// Okay! Let's draw it all! Woo! // SRB2CBTODO: Stencil reflection on water
	for (i = 0; i < p; i++)
	{
		GL_SetTransform(&atransform);

		if (sortnode[sortindex[i]].plane)
		{
			if (!(sortnode[sortindex[i]].plane->blendmode & PF_NoTexture))
				HWR_GetFlat(sortnode[sortindex[i]].plane->lumpnum, false);

			HWR_RenderPlane(sortnode[sortindex[i]].plane->sector, sortnode[sortindex[i]].plane->xsub, sortnode[sortindex[i]].plane->fixedheight, sortnode[sortindex[i]].plane->blendmode,
							sortnode[sortindex[i]].plane->lightlevel, sortnode[sortindex[i]].plane->lumpnum, sortnode[sortindex[i]].plane->FOFSector,
							sortnode[sortindex[i]].plane->alpha, sortnode[sortindex[i]].plane->planecolormap);
			continue;
		}
		else if (sortnode[sortindex[i]].wall)
		{
			if (!(sortnode[sortindex[i]].wall->blendmode & PF_NoTexture))
				HWR_GetTexture(sortnode[sortindex[i]].wall->texnum, false);
			HWR_RenderTransparentWall(sortnode[sortindex[i]].wall->wallVerts, &sortnode[sortindex[i]].wall->Surf, sortnode[sortindex[i]].wall->blendmode, sortnode[sortindex[i]].wall->sector, sortnode[sortindex[i]].wall->fogwall);
			continue;
		}
	}

	// Clear out transparent data now that the game is done with this frame
	numtranswalls = 0;
	numtransplanes = 0;
	transwallinfo = NULL;
	transplaneinfo = NULL;

	free(sortnode);
	free(sortindex);
	GL_SetTransform(NULL); // Reset transform
}
#else
static void HWR_CreateDrawNodes(void) // SRB2CBTODO: This is really OpenGL's R_Drawmasked
{
	size_t i = 0, p = 0, prev = 0, loop;

	// SRB2CBTODO: Optimize the sort list
	// SRB2CBTODO: when viewing a floor in front and wall in back, the overlap is wrong
	// SRB2CBTODO: Major thing needing fixing is that sprites are always overlapped with this

	gr_drawnode_t *sortnode = memset(malloc((sizeof(transplaneinfo_t)*numtransplanes) + (sizeof(transwallinfo_t)*numtranswalls)
#ifdef SORTING
											+ (sizeof(gr_vissprite_t)*(gr_vissprite_p - gr_vissprites))
#endif
											),
									 0, (sizeof(transplaneinfo_t)*numtransplanes) + (sizeof(transwallinfo_t)*numtranswalls)
#ifdef SORTING
									 + (sizeof(gr_vissprite_t)*(gr_vissprite_p - gr_vissprites))
#endif
									 );

	size_t *sortindex = memset(malloc(sizeof(size_t) * (numtransplanes + numtranswalls
#ifdef SORTING
														+ (gr_vissprite_p - gr_vissprites)
#endif
														)),
							   0, sizeof(size_t) * (numtransplanes + numtranswalls
#ifdef SORTING
													+ (gr_vissprite_p - gr_vissprites)
#endif
													));

	for (i = 0; i < numtransplanes; i++, p++)
	{
		sortnode[p].plane = &transplaneinfo[i];
		sortindex[p] = p;
	}

	for (i = 0; i < numtranswalls; i++, p++)
	{
		sortnode[p].wall = &transwallinfo[i];
		sortindex[p] = p;
	}

#ifdef SORTING
	if (gr_vissprite_p > gr_vissprites)
	{
		gr_vissprite_t *spr;

		for (spr = gr_vsprsortedhead.next;
			 spr != &gr_vsprsortedhead;
			 spr = spr->next, p++)
		{
			sortnode[p].sprite = spr;
			sortindex[p] = p;
		}
	}
#endif

	// p is the number of stuff to sort

	// Add the 3D floors, thicksides, and masked textures...
	// Instead of going through drawsegs, we need to iterate
	// through the lists of masked textures and
	// translucent ffloors being drawn.

	// This is a bubble sort! Wahoo!

	// Stuff is sorted:
	//      sortnode[sortindex[0]]   = farthest away
	//      sortnode[sortindex[p-1]] = closest
	// "i" should be closer. "prev" should be further.
	// The lower drawcount is, the further it is from the screen.

	for (loop = 0; loop < p; loop++) // This extra loop is needed to make sure there is no over/under draw
	{
		for (i = 1; i < p; i++)
		{
			prev = i-1;


			if (sortnode[sortindex[i]].plane)
			{
				// What are we comparing it with?
				if (sortnode[sortindex[prev]].plane)
				{
					// Plane (i) is further away than plane (prev)
					if (ABS(sortnode[sortindex[i]].plane->fixedheight - viewz) > ABS(sortnode[sortindex[prev]].plane->fixedheight - viewz))
					{
						size_t temp;

						temp = sortindex[prev];
						sortindex[prev] = sortindex[i];
						sortindex[i] = temp;
					}
				}
				else if (sortnode[sortindex[prev]].wall)
				{
					// Plane (i) is further than wall (prev)
					if (sortnode[sortindex[i]].plane->drawcount > sortnode[sortindex[prev]].wall->drawcount)
					{
						size_t temp;

						temp = sortindex[prev];
						sortindex[prev] = sortindex[i];
						sortindex[i] = temp;
					}
				}
				else if (sortnode[sortindex[prev]].sprite) // SRB2CBTODO: Make this work, gotta compare with camera
				{
					//if (ABS(sortnode[sortindex[i]].plane->fixedheight - viewz)
					//	> ABS(sortnode[sortindex[prev]].sprite->mobj->z - viewz))
					{
						I_Error("Swit\n");
						size_t temp;

						temp = sortindex[prev];
						sortindex[prev] = sortindex[i];
						sortindex[i] = temp;
					}
				}
			}
			else if (sortnode[sortindex[i]].wall)
			{
				// What are we comparing it with?
				if (sortnode[sortindex[prev]].plane)
				{
					// Wall (i) is further than plane(prev)
					if (sortnode[sortindex[i]].wall->drawcount > sortnode[sortindex[prev]].plane->drawcount)
					{
						size_t temp;

						temp = sortindex[prev];
						sortindex[prev] = sortindex[i];
						sortindex[i] = temp;
					}
				}
				else if (sortnode[sortindex[prev]].wall)
				{
					// Wall (i) is further than wall (prev)
					if (sortnode[sortindex[i]].wall->drawcount > sortnode[sortindex[prev]].wall->drawcount)
					{
						size_t temp;

						temp = sortindex[prev];
						sortindex[prev] = sortindex[i];
						sortindex[i] = temp;
					}
				}
			}
			else if (sortnode[sortindex[i]].sprite)
			{
				// SRB2CBTODO: Sprite wall sort

			}

		} // done with sorting
	} // done with extra loop to prevent redraw


	// SRB2CBTODO: Sort everything here


	//
	//GL_SetTransform(NULL);
	// Okay! Let's draw it all! Woo! // SRB2CBTODO: Stencil reflection on water
	for (i = 0; i < p; i++)
	{
		GL_SetTransform(&atransform);
		if (sortnode[sortindex[i]].plane)
		{
			if (!(sortnode[sortindex[i]].plane->blendmode & PF_NoTexture))
				HWR_GetFlat(sortnode[sortindex[i]].plane->lumpnum, true);

			HWR_RenderPlane(sortnode[sortindex[i]].plane->sector, sortnode[sortindex[i]].plane->xsub,
							sortnode[sortindex[i]].plane->fixedheight, sortnode[sortindex[i]].plane->blendmode,
							sortnode[sortindex[i]].plane->lightlevel, sortnode[sortindex[i]].plane->lumpnum,
							sortnode[sortindex[i]].plane->FOFSector,
							sortnode[sortindex[i]].plane->alpha, sortnode[sortindex[i]].plane->planecolormap);
			continue;
		}
		else if (sortnode[sortindex[i]].wall)
		{
			//GL_SetTransform(&atransform);
			if (!(sortnode[sortindex[i]].wall->blendmode & PF_NoTexture))
				HWR_GetTexture(sortnode[sortindex[i]].wall->texnum, true);
			HWR_RenderTransparentWall(sortnode[sortindex[i]].wall->wallVerts, &sortnode[sortindex[i]].wall->Surf,
									  sortnode[sortindex[i]].wall->blendmode, sortnode[sortindex[i]].wall->sector, sortnode[sortindex[i]].wall->fogwall);
			continue;
		}
		else if (sortnode[i].sprite)
		{
			//GL_SetTransform(NULL);
			// SRB2CBTODO: Allow this to use atransform
			if (sortnode[i].sprite->precip) // Precipitation sprites are optimized into it's own function
				HWR_DrawPrecipitationSprite(sortnode[i].sprite);
			else if (!cv_grmd2.value
					 || (cv_grmd2.value && md2_models[sortnode[i].sprite->mobj->sprite].notfound == true))
				HWR_DrawSprite(sortnode[i].sprite);
			else
			{
				md2_t *md2;

				md2 = &md2_models[sortnode[i].sprite->mobj->sprite];

				if (!sortnode[i].sprite->precip
					&& md2_models[sortnode[i].sprite->mobj->sprite].notfound == false && md2_models[sortnode[i].sprite->mobj->sprite].scale > 0.0f)
					HWR_DrawMD2(sortnode[i].sprite);
			}
		}
	}

	// Clear out transparent data now that the game is done with this frame
	numtranswalls = 0;
	numtransplanes = 0;
	transwallinfo = NULL;
	transplaneinfo = NULL;

	free(sortnode);
	free(sortindex);
	GL_SetTransform(NULL); // Reset transform
}
#endif

// --------------------------------------------------------------------------
//  Draw all vissprites
// --------------------------------------------------------------------------
#ifndef SORTING
static void HWR_DrawSprites(void)
{
	if (gr_vissprite_p > gr_vissprites)
	{
		gr_vissprite_t *spr;

		// draw all vissprites back to front
		for (spr = gr_vsprsortedhead.next;
		     spr != &gr_vsprsortedhead;
		     spr = spr->next)
		{
			if (spr->precip) // Precipitation sprites are optimized into it's own function
				HWR_DrawPrecipitationSprite(spr);
			else if (spr->mobj->player) // Precipitation sprites are optimized into it's own function
			{
				if (!cv_grmd2.value || (cv_grmd2.value && md2_playermodels[spr->mobj->player->skin].notfound == true))
					HWR_DrawSprite(spr);
			}
			else if (!cv_grmd2.value || (cv_grmd2.value && md2_models[spr->mobj->sprite].notfound == true))
				HWR_DrawSprite(spr);
		}
	}
}

static void HWR_DrawMD2s(void)
{
	if (!cv_grmd2.value)
		return;

	if (gr_vissprite_p > gr_vissprites)
	{
		gr_vissprite_t *spr;

		// draw all MD2 back to front
		for (spr = gr_vsprsortedhead.next;
			 spr != &gr_vsprsortedhead;
			 spr = spr->next)
		{
			if (!spr->mobj)
				continue;

			if (!spr->precip) // Yeah MD2s for weather effects is just asking for trouble
			{
				if (spr->mobj && spr->mobj->player && spr->mobj->player->skin != -1)
				{
					if ((md2_playermodels[spr->mobj->player->skin].notfound == false) && (md2_playermodels[spr->mobj->player->skin].scale > 0.0f))
						HWR_DrawMD2(spr);
				}
				else if (md2_models[spr->mobj->sprite].notfound == false && md2_models[spr->mobj->sprite].scale > 0.0f)
					HWR_DrawMD2(spr);
			}
		}

	}
}
#endif

// --------------------------------------------------------------------------
// HWR_AddSprites
// During BSP traversal, this adds sprites by sector.
// --------------------------------------------------------------------------
static void HWR_AddSprites(sector_t *sec)
{
	mobj_t *thing;
	precipmobj_t *precipthing;
	fixed_t adx, ady, approx_dist;

	if (sec->validcount == validcount)
		return;

	// Check to not overdraw sprites // SRB2CBTODO: Do the same when drawing walls!
	sec->validcount = validcount;

		// Handle all things in sector.
		for (thing = sec->thinglist; thing; thing = thing->snext)
		{
			if (!thing)
				continue;

			if (!(thing->flags2 & MF2_DONTDRAW))
				HWR_ProjectSprite(thing);

			if (cv_objectplace.value
			&& !(thing->flags2 & MF2_DONTDRAW))
				objectsdrawn++;

			if (!thing->snext)
				break;
		}

	if (playeringame[displayplayer] && players[displayplayer].mo) // ZTODO: Make a more efficient non-mobj weather system
	{
		for (precipthing = sec->preciplist; precipthing; precipthing = precipthing->snext)
		{
			if (!precipthing)
				continue;

			if (precipthing->invisible)
				continue;

			adx = abs(players[displayplayer].mo->x - precipthing->x);
			ady = abs(players[displayplayer].mo->y - precipthing->y);

			// From _GG1_ p.428. Approx. eucledian distance fast.
			approx_dist = adx + ady - ((adx < ady ? adx : ady)>>1);

			// Only draw the precipitation oh-so-far from the player.
			if (approx_dist < (2320 << FRACBITS))
				HWR_ProjectPrecipitationSprite(precipthing);
			else if ((splitscreen && rendersplit) && players[secondarydisplayplayer].mo)
			{
				adx = abs(players[secondarydisplayplayer].mo->x - precipthing->x);
				ady = abs(players[secondarydisplayplayer].mo->y - precipthing->y);

				// From _GG1_ p.428. Approx. eucledian distance fast.
				approx_dist = adx + ady - ((adx < ady ? adx : ady)>>1);

				if (approx_dist < (2320 << FRACBITS))
					HWR_ProjectPrecipitationSprite(precipthing);
			}
		}
	}
}

// --------------------------------------------------------------------------
// HWR_ProjectSprite
//  Generates a vissprite for a thing if it might be visible.
//  Lighting, colormapping, and translucency handled in actually drawing the sprite
// --------------------------------------------------------------------------
// SRB2CBTODO: UFRAME needs this to be called more than normal
// scale the spritexyz here
static void HWR_ProjectSprite(mobj_t *thing) // SRB2CBTODO: MD2's projectsprite needs parts of this?
{
	gr_vissprite_t *vis;
	float tr_x, tr_y;
	float gxt, gyt;
	float tx, tz;

	float x1;

	spritedef_t *sprdef;
	spriteframe_t *sprframe;
	size_t lump;

	unsigned rot;
	byte flip;

	angle_t ang;

	if (!thing || (thing->flags2 & MF2_DONTDRAW))
		return;

	float spritex;
	float spritey;
	float spritez;

	spritex = thing->x;
	spritey = thing->y;
	spritez = thing->z;

	// Transform the origin point
	tr_x = FIXED_TO_FLOAT(spritex) - gr_viewx;
	tr_y = FIXED_TO_FLOAT(spritey) - gr_viewy;

	// Rotation around vertical axis
	tz = (tr_x * gr_viewcos) + (tr_y * gr_viewsin); // SRB2CBTODO: Allow looking up/down and move sprites too

	// Thing is behind view plane?
	if (tz < ZCLIP_PLANE)
		return;

	gxt = -(tr_x*gr_viewsin);
	gyt = (tr_y*gr_viewcos);
	tx = -(gyt+gxt);

	// Decide which patch to use for sprite relative to player
	if ((unsigned int)thing->sprite >= numsprites)
#ifdef RANGECHECK
		I_Error("%s: invalid sprite number %i ",
		        __FUNCTION__, thing->sprite);
#else
	{
		CONS_Printf(PREFIX_WARN "Mobj of type %i with invalid sprite data (%d) detected and removed.\n", thing->type, thing->sprite);
		if (thing->player)
			P_SetPlayerMobjState(thing, S_PLAY_STND);
		else
			P_SetMobjState(thing, S_DISS);
		return;
	}
#endif

	// SRB2CBTODO: Let MD2s use this thing->skin thing!
	if (thing->skin && thing->sprite == SPR_PLAY)
		sprdef = &((skin_t *)thing->skin)->spritedef;
	else
		sprdef = &sprites[thing->sprite];

	if ((byte)(thing->frame&FF_FRAMEMASK) >= sprdef->numframes)
#ifdef RANGECHECK
		I_Error("HWR_ProjectSprite: invalid sprite frame %i : %lu for %s",
		         thing->sprite, thing->frame, sprnames[thing->sprite]);
#else
	{
		CONS_Printf(PREFIX_WARN "Mobj of type %i with invalid sprite data (%ld) detected and removed.\n", thing->type, (thing->frame&FF_FRAMEMASK));
		if (thing->player)
			P_SetPlayerMobjState(thing, S_PLAY_STND);
		else
			P_SetMobjState(thing, S_DISS);
		return;
	}
#endif

	sprframe = &sprdef->spriteframes[thing->frame & FF_FRAMEMASK];

	if (!sprframe)
#ifdef PARANOIA
		I_Error("sprframes NULL for sprite %d\n", thing->sprite);
#else
		return;
#endif

	if (sprframe->rotate)
	{
		// choose a different rotation based on player view
		ang = R_PointToAngle(thing->x, thing->y);
		rot = (ang-thing->angle+(angle_t)(ANG45/2)*9)>>29;
		//Fab: lumpid is the index for spritewidth, spriteoffset... tables
		lump = sprframe->lumpid[rot];
		flip = sprframe->flip[rot];
	}
	else
	{
		// use single rotation for all views
		rot = 0;                        //Fab: for vis->patch below
		lump = sprframe->lumpid[0];     //Fab: see note above
		flip = sprframe->flip[0];
	}

	// calculate edges of the shape
	tx -= FIXED_TO_FLOAT(spritecachedinfo[lump].offset);

	// project x
	x1 = tx;

	tx += FIXED_TO_FLOAT(spritecachedinfo[lump].width);

	// SRB2CBTODO: Cull plane support for OpenGL
	fixed_t gz, gzt;
	long heightsec;

	// Calculate sprite clipping coordinates
	if (thing->eflags & MFE_VERTICALFLIP)
	{
		if (thing->scale != 100)
		{
			if (thing->flags & MF_HIRES)
			{
				gzt = thing->z + thing->height + (((spritecachedinfo[lump].height - spritecachedinfo[lump].topoffset)/2)/2)*thing->scale/100;
				gz = gzt - (spritecachedinfo[lump].height/2)*thing->scale/100;
			}
			else
			{
				gzt = thing->z + thing->height + ((spritecachedinfo[lump].height - spritecachedinfo[lump].topoffset)/2)*thing->scale/100;
				gz = gzt - spritecachedinfo[lump].height*thing->scale/100;
			}
		}
		else if (thing->flags & MF_HIRES)
		{
			gzt = thing->z + thing->height + (((spritecachedinfo[lump].height - spritecachedinfo[lump].topoffset)/2)/2)*thing->scale/100;
			gz = gzt - spritecachedinfo[lump].height/2;
		}
		else
		{
			gzt = thing->z + thing->height + spritecachedinfo[lump].height - spritecachedinfo[lump].topoffset;
			gz = gzt - spritecachedinfo[lump].height;
		}
	}
	else
	{
		if (thing->scale != 100)
		{
			if (thing->flags & MF_HIRES)
			{
				gzt = thing->z + (spritecachedinfo[lump].topoffset/2)*thing->scale/100;
				gz = gzt - (spritecachedinfo[lump].height/2)*thing->scale/100;
			}
			else
			{
				gzt = thing->z + spritecachedinfo[lump].topoffset*thing->scale/100;
				gz = gzt - spritecachedinfo[lump].height*thing->scale/100;
			}
		}
		else if (thing->flags & MF_HIRES)
		{
			gzt = thing->z + spritecachedinfo[lump].topoffset/2;
			gz = gzt - spritecachedinfo[lump].height/2;
		}
		else
		{
			gzt = thing->z + spritecachedinfo[lump].topoffset;
			gz = gzt - spritecachedinfo[lump].height;
		}
	}

	if (thing->subsector->sector->cullheight)
	{
		if (thing->subsector->sector->cullheight->flags & ML_NOCLIMB) // Group culling
		{
			// Make sure this is part of the same group
			if (viewsector->cullheight && viewsector->cullheight->frontsector
				== thing->subsector->sector->cullheight->frontsector)
			{
				// OK, we can cull
				if (viewz > thing->subsector->sector->cullheight->frontsector->floorheight
					&& gzt < thing->subsector->sector->cullheight->frontsector->floorheight) // Cull if below plane
					return;

				if (gz > thing->subsector->sector->cullheight->frontsector->floorheight
					&& viewz <= thing->subsector->sector->cullheight->frontsector->floorheight) // Cull if above plane
					return;
			}
		}
		else // Quick culling
		{
			if (viewz > thing->subsector->sector->cullheight->frontsector->floorheight
				&& gzt < thing->subsector->sector->cullheight->frontsector->floorheight) // Cull if below plane
				return;

			if (gz > thing->subsector->sector->cullheight->frontsector->floorheight
				&& viewz <= thing->subsector->sector->cullheight->frontsector->floorheight) // Cull if above plane
				return;
		}
	}

	heightsec = thing->subsector->sector->heightsec;

	if (heightsec != -1) // only clip things which are in special sectors
	{
		long phs = players[displayplayer].mo->subsector->sector->heightsec;
		if (phs != -1 && viewz < sectors[phs].floorheight ?
		    thing->z >= sectors[heightsec].floorheight :
		    gzt < sectors[heightsec].floorheight)
			return;
		if (phs != -1 && viewz > sectors[phs].ceilingheight ?
			gzt < sectors[heightsec].ceilingheight &&
			viewz >= sectors[heightsec].ceilingheight :
			thing->z >= sectors[heightsec].ceilingheight)
			return;
	}

	// store information in a vissprite for the OpenGL renderer
	vis = HWR_NewVisSprite();
	vis->x1 = x1;
	vis->x2 = tx;
	vis->tz = tz;

#ifdef PAPERMARIO
	vis->y1 = (float)spritecachedinfo[lump].topoffset;
	vis->y2 = vis->y1 - ((float)spritecachedinfo[lump].height);

	vis->vt = 0.0f;
	vis->vb = 1;
	if (flip)
	{
		vis->ul = 0.0f;
		vis->ur = 1;
	}
	else
	{
		vis->ul = 1;
		vis->ur = 0.0f;
	}
#endif


	vis->patchlumpnum = sprframe->lumppat[rot];
	vis->flip = flip;

	// New for sprite clips
	vis->gx = thing->x;
	vis->gy = thing->y;
	vis->gz = gz;
	vis->gzt = gzt;
	vis->thingheight = thing->height;
	vis->pz = thing->z;
	vis->pzt = vis->pz + vis->thingheight;

	vis->mobj = thing;

	vis->sector = thing->subsector->sector;

	if (thing->flags & MF_TRANSLATION)
	{
		// New colormap stuff for characters
		if (vis->mobj->player && vis->mobj->sprite == SPR_PLAY) // This thing is a player!
			vis->colormap = (byte *)translationtables[vis->mobj->player->skin] - 256 + ((long)vis->mobj->color<<8);
		else if ((vis->mobj->flags & MF_BOSS) && (vis->mobj->flags2 & MF2_FRET) && (leveltime & 1*NEWTICRATERATIO)) // Bosses "flash"
			vis->colormap = (byte *)bosstranslationtables;
		else
			vis->colormap = (byte *)defaulttranslationtables - 256 + ((long)vis->mobj->color<<8);
	}
	else
		vis->colormap = colormaps;

	// set top/bottom coords
	vis->ty = FIXED_TO_FLOAT(spritez + spritecachedinfo[lump].topoffset) - gr_viewz;

	// A sprite must have it's 4 added to its y value, otherwise it clips into the ground/ceiling
	// Sprites didn't need to be clipped in the original software render's floors because there was no zclipping,
	// so the game has to change the offset just a bit so it isn't as noticeable
	// SRB2CBTODO: Is it possible to have non absolute values? Do a real adjustment system?
	if (spritecachedinfo[lump].topoffset > 0 && (spritecachedinfo[lump].topoffset < spritecachedinfo[lump].height))
		vis->ty += 4.0f*thing->scale/100;

	vis->precip = false;

	if (thing->eflags & MFE_VERTICALFLIP)
		vis->vflip = true;
	else
		vis->vflip = false;
}

// Precipitation projector for hardware mode
// Lighting, colormapping, and translucency handled in actually drawing the sprite
static void HWR_ProjectPrecipitationSprite(precipmobj_t *thing)
{
	gr_vissprite_t *vis;
	float tr_x, tr_y;
	float tx, tz;
	float x1;
	spritedef_t *sprdef;
	spriteframe_t *sprframe;
	lumpnum_t lumpnum;
	unsigned rot = 0;
	byte flip;

	// Transform the origin point
	tr_x = FIXED_TO_FLOAT(thing->x) - gr_viewx;
	tr_y = FIXED_TO_FLOAT(thing->y) - gr_viewy;

	// Rotation around vertical axis
	tz = (tr_x * gr_viewcos) + (tr_y * gr_viewsin);

	// Thing is behind view plane?
	if (tz < ZCLIP_PLANE)
		return;

	tx = (tr_x * gr_viewsin) - (tr_y * gr_viewcos);

	// Decide which patch to use for sprite relative to player
	if ((unsigned int)thing->sprite >= numsprites)
#ifdef RANGECHECK
		I_Error("HWR_ProjectSprite: invalid sprite number %i ",
		        thing->sprite);
#else
		return;
#endif

	sprdef = &sprites[thing->sprite];

	if ((byte)(thing->frame&FF_FRAMEMASK) >= sprdef->numframes)
#ifdef RANGECHECK
		I_Error("HWR_ProjectSprite: invalid sprite frame %i : %i for %s",
		        thing->sprite, thing->frame, sprnames[thing->sprite]);
#else
		return;
#endif

	sprframe = &sprdef->spriteframes[ thing->frame & FF_FRAMEMASK];

	// use single rotation for all views
	lumpnum = sprframe->lumpid[0];
	flip = sprframe->flip[0];

	// calculate edges of the shape
	tx -= FIXED_TO_FLOAT(spritecachedinfo[lumpnum].offset);

	x1 = tx;

	tx += FIXED_TO_FLOAT(spritecachedinfo[lumpnum].width);

	//
	// store information in a vissprite
	//
	vis = HWR_NewVisSprite();
	vis->x1 = x1;
	vis->x2 = tx;
	vis->tz = tz;
	vis->patchlumpnum = sprframe->lumppat[rot];
	vis->flip = flip;
	vis->mobj = (mobj_t *)thing;

	vis->colormap = colormaps;

	// set top/bottom coords
	vis->ty = FIXED_TO_FLOAT(thing->z + spritecachedinfo[lumpnum].topoffset) - gr_viewz;

	vis->precip = true;
}

// ==========================================================================
//
// ==========================================================================
// Draw the sky as a texture
static void HWR_DrawSkyTextureBackground(void)
{
	FOutVector v[4];
	angle_t angle, yangle;
	float f;
	float skyzclip = 4.0f;

	//  3--2
	//  | /|
	//  |/ |
	//  0--1

	HWR_GetTexture(skytexture, false);

	// The sky is the only texture that needs 4.0f texture coordinates instead of 1.0,
	// because it's called just after clearing the screen, so the near clipping plane is set to 3.99
	v[0].x = v[3].x = -skyzclip;
	v[1].x = v[2].x =  skyzclip;
	v[0].y = v[1].y = -skyzclip;
	v[2].y = v[3].y =  skyzclip;

	v[0].z = v[1].z = v[2].z = v[3].z = skyzclip;

	// Sky textures with dimensions over 256 need to have the viewangle/viewpitch offset scaled
	// ANGLE90 equals 360/4
	if (textures[skytexture]->width > 256)
		angle = (angle_t)((float)(viewangle + gr_xtoviewangle[0])
						/((float)textures[skytexture]->width/256.0f))
							%(ANGLE_90);
	else
		angle = (angle_t)(viewangle + gr_xtoviewangle[0])%(ANGLE_90);

	// horizonangle is the scale used to determine the horizon of a sky: 259/800 is the scale all skies use,
	// which is the standard texture height, 128, divided by 200, which is the BASEVIDHEIGHT, plus one degree,
	// this is then converted to an angle
	angle_t horizonangle;
	horizonangle = (0.32375f * (float)textures[skytexture]->height)*(ANG45/45);

	// The "y" angle is the vertical offset of the sky,
	// viewpitch is the absolute angle the world is viewed at
	if (textures[skytexture]->height > 256)
		yangle = (angle_t)(((horizonangle + viewpitch)% (ANGLE_MAX)) / (textures[skytexture]->height/256.0f)) % (ANGLE_MAX/4);
	else
		yangle = (angle_t)((horizonangle + viewpitch) % (ANGLE_MAX/4));

	// Draw the horizontal angle of sky based on viewangle
	f = (float)((textures[skytexture]->width/2.0f)
			* FIXED_TO_FLOAT(finetangent[(2048
				- ((angle_t)angle>>(ANGLETOFINESHIFT + 1))) & FINEMASK]));

	v[0].sow = v[3].sow = 0.22f+(f)/(textures[skytexture]->width/2.0f);
	v[2].sow = v[1].sow = 0.22f+(f+(127.0f))/(textures[skytexture]->width/2.0f);

	// Draw the vertical position of sky based on viewpitch
	f = (float)((textures[skytexture]->height/2.0f)
			* FIXED_TO_FLOAT(finetangent[(2048
				- ((angle_t)(yangle)>>(ANGLETOFINESHIFT + 1))) & FINEMASK]));

	v[3].tow = v[2].tow = 0.22f+(f)/(textures[skytexture]->height/2.0f);
	v[0].tow = v[1].tow = 0.22f+(f+(127.0f))/(textures[skytexture]->height/2.0f);

	FBITFIELD PolyFlags = PF_NoDepthTest|PF_NoAlphaTest;
	FSurfaceInfo Surf;

	/*
	 /// SRB2CBTODO: Day to night return!
	if (timecycle)
	{
		Surf.FlatColor.rgba = UINT2RGBA(0x00800000);
		PolyFlags |= PF_Modulated;
	}*/

	GL_DrawPolygon(&Surf, v, 4, PolyFlags, 0);
}

// Captures the current screen texture and renders it over the current screen
// X Y values are realtive to 0, 0 being the center of the screen
// SRB2CBTODO: Find a use for this cool screen capture
static void HWR_RenderScreenTexture(float screenscale, float x, float y, boolean grayscale, byte alpha) // TODO: Use this as a way for fast DOF?
{
	FOutVector v[4];
	FBITFIELD blendmode = PF_NoDepthTest;

	if (cv_grcompat.value)
		return;

	//  3--2
	//  | /|
	//  |/ |
	//  0--1

	GL_MakeScreenTexture(skyviewscreentex, grayscale);

	float xfix, yfix;
	int texsize = 2048;

	if (screen_width <= 1024)
		texsize = 1024;
	if (screen_width <= 512)
		texsize = 512;

	xfix = 1/((float)(texsize)/((float)((screen_width))));
	yfix = 1/((float)(texsize)/((float)((screen_height))));

	v[0].x = v[3].x = -screenscale+x;
	v[1].x = v[2].x =  screenscale+x;
	v[0].y = v[1].y = -screenscale+y;
	v[2].y = v[3].y =  screenscale+y;

	v[0].z = v[1].z = v[2].z = v[3].z = 6.0f;

	v[0].tow = v[1].tow = 0;
	v[2].tow = v[3].tow = yfix;

	v[0].sow = v[3].sow = 0;
	v[1].sow = v[2].sow = xfix;

	if (alpha < 255)
	{
		FSurfaceInfo Surf;
		Surf.FlatColor.s.red = Surf.FlatColor.s.green = Surf.FlatColor.s.blue = 0xff;
		Surf.FlatColor.s.alpha = alpha;

		blendmode |= PF_Translucent|PF_Modulated;

		GL_DrawPolygon(&Surf, v, 4, blendmode, 0);
	}
	else
		GL_DrawPolygon(NULL, v, 4, blendmode, 0);
}

static void HWR_DOFLine(float screenscale, float x, float y, boolean grayscale, byte alpha) // TODO: Use this as a way for fast DOF?
{
	//FOutVector v[4];
	wallVert3D wallVerts[4];
	FBITFIELD blendmode = PF_NoDepthTest;

	if (cv_grcompat.value)
		return;

	//  3--2
	//  | /|
	//  |/ |
	//  0--1

	GL_MakeScreenTexture(skyviewscreentex, grayscale);

	float xfix, yfix;
	int texsize = 2048;

	if (screen_width <= 1024)
		texsize = 1024;
	if (screen_width <= 512)
		texsize = 512;

	xfix = 1/((float)(texsize)/((float)((screen_width))));
	yfix = 1/((float)(texsize)/((float)((screen_height))));
#if 0
	v[0].x = v[3].x = -screenscale+x;
	v[1].x = v[2].x =  screenscale+x;
	v[0].y = v[1].y = -screenscale+y;
	v[2].y = v[3].y =  screenscale+y;
#endif

	// remember vertices ordering
	//  3--2
	//  | /|
	//  |/ |
	//  0--1
	// make a wall polygon (with 2 triangles), using the floor/ceiling heights,
	// and the 2d map coords of the start/end vertices
	wallVerts[0].x = wallVerts[3].x = -screenscale+x;
	wallVerts[0].z = wallVerts[3].z = -screenscale+y;
	wallVerts[2].x = wallVerts[1].x = screenscale+x;
	wallVerts[2].z = wallVerts[1].z = screenscale+y;
	wallVerts[0].w = wallVerts[1].w = wallVerts[2].w = wallVerts[3].w = 1.0f;

	wallVerts[2].y = wallVerts[3].y = 2000;
	wallVerts[0].y = wallVerts[1].y = 0;

	wallVerts[0].x = wallVerts[3].x = -3504.000000;
	wallVerts[0].z = wallVerts[3].z = -400.000000;
	wallVerts[2].x = wallVerts[1].x = 3504.000000;
	wallVerts[2].z = wallVerts[1].z = 816.000000;

	wallVerts[0].t = wallVerts[1].t = 0;
	wallVerts[2].t = wallVerts[3].t = yfix;

	wallVerts[0].s = wallVerts[3].s = 0;
	wallVerts[1].s = wallVerts[2].s = xfix;

	FSurfaceInfo Surf;
	Surf.FlatColor.s.red = Surf.FlatColor.s.green = Surf.FlatColor.s.blue = 0xff;

	// R_SetupFrame: Sets up the viewangle, viewx, viewy, viewz
	//R_SetupFrame(player);
	GL_SetTransform(&atransform);

	if (alpha < 255)
	{
		Surf.FlatColor.s.alpha = alpha;

		blendmode |= PF_Translucent|PF_Modulated;

		HWR_ProjectWall(wallVerts, &Surf, blendmode);
	}
	else
		HWR_ProjectWall(wallVerts, &Surf, PF_Masked);

	GL_SetTransform(NULL);
}


// -----------------+
// HWR_ClearView : Clear the viewwindow, with maximum z value
// -----------------+
static inline void HWR_ClearView(void)
{
	//  3--2
	//  | /|
	//  |/ |
	//  0--1

	const float clearz = NZCLIP_PLANE*4.0f;

	/// \bug faB - enable depth mask, disable color mask
	GL_GClipRect((int)gr_viewwindowx,
	                 (int)gr_viewwindowy,
	                 (int)(gr_viewwindowx + gr_viewwidth),
	                 (int)(gr_viewwindowy + gr_viewheight),
	                 clearz); // SRB2CBTODO: This effects the precision of the z buffer in the entire engine, support higher values here?
	GL_ClearBuffer(false, true, 0);
}


// -----------------+
// HWR_SetViewSize  : set projection and scaling values
// -----------------+
void HWR_SetViewSize(void)
{
	// setup view size
	gr_viewwidth = (float)vid.width;
	gr_viewheight = (float)vid.height;

	if ((splitscreen && rendersplit))
		gr_viewheight /= 2.0f;

	gr_centerx = gr_viewwidth / 2.0f;
	gr_basecentery = gr_viewheight / 2.0f; // NOTE: this is (gr_centerx * gr_viewheight / gr_viewwidth)

	gr_viewwindowx = (vid.width - gr_viewwidth) / 2.0f;
	gr_windowcenterx = (float)(vid.width / 2.0f);
	if (gr_viewwidth == vid.width)
	{
		gr_baseviewwindowy = 0;
		gr_basewindowcentery = gr_viewheight / 2.0f;               // window top left corner at 0,0
	}
	else
	{
		gr_baseviewwindowy = (vid.height-gr_viewheight) / 2.0f;
		gr_basewindowcentery = (vid.height / 2.0f);
	}

	gr_pspritexscale = gr_viewwidth / BASEVIDWIDTH;
	gr_pspriteyscale = ((vid.height*gr_pspritexscale*BASEVIDWIDTH)/BASEVIDHEIGHT)/vid.width;
}


static void HWR_ExtraEffects(player_t *player)
{
	// Handle bonus count for OpenGL
	if (player->bonuscount)
	{
		FOutVector      v[4];
		FSurfaceInfo Surf;

		// SRB2CB: highest value so it doesn't overlap with anything else
		v[0].x = v[2].y = v[3].x = v[3].y = -5.0f;
		v[0].y = v[1].x = v[1].y = v[2].x = 5.0f;
		v[0].z = v[1].z = v[2].z = v[3].z = 5.0f;

		Surf.FlatColor.s.red = Surf.FlatColor.s.green = Surf.FlatColor.s.blue = 255;
		Surf.FlatColor.s.alpha = (byte)(player->bonuscount*25);

		GL_DrawPolygon(&Surf, v, 4, PF_Modulated|PF_Translucent|PF_NoDepthTest|PF_NoTexture, 0);
	}
}

#if 0
// Fog can be processed at the current OpenGL viewpoint
// EDIT: Fog now calculated per polygon 0_0
static void HWR_ProcessFog(void)
{
	if (cv_grfog.value) // If fog is enabled,
	{
		HWR_FoggingOn(); // First of all, turn it on (done per-frame)

		// Special feature that changes fog color when in a colormap FOF
		// NOTE: Splitscreen automatically supported
		if (viewsector && viewsector->ffloors)
		{
			// Setup sector for FOF checking
			sector_t *sector;

			sector = viewsector;

			ffloor_t *rover;

			for (rover = sector->ffloors; rover; rover = rover->next)
			{
				if (!(rover->flags & FF_EXISTS) || rover->flags & FF_SOLID)
					continue;
				if (!(rover->flags & FF_SWIMMABLE || rover->flags & FF_FOG))
					continue;
				if (*rover->topheight <= viewz
					|| *rover->bottomheight > (viewz))
					continue;

				if (viewz + 0 < *rover->topheight)
				{
					if (!(rover->master->frontsector->extra_colormap)) // See if there's a colormap in this FOF
						continue;

					// No fog if the FOF is supposed to be an invisible fogblock
					if ((rover->flags & FF_FOG) && !(rover->master->frontsector->extra_colormap && rover->master->frontsector->extra_colormap->fog)
						&& (rover->master->frontsector->lightlevel == 255))
						continue;

					ULONG sectorcolormap; // RGBA value of the sector's colormap
					sectorcolormap = rover->master->frontsector->extra_colormap->rgba;

					RGBA_t rgbcolor; // Convert the value from ULONG to RGA_t
					rgbcolor.rgba = sectorcolormap;

					ULONG fogvalue; // convert the color to FOG from RGBA to RGB
					fogvalue = (rgbcolor.s.red*0x10000)+(rgbcolor.s.green*0x100)+rgbcolor.s.blue;

					GL_SetSpecialState(HWD_SET_FOG_COLOR, fogvalue);
					GL_SetSpecialState(HWD_SET_FOG_DENSITY, cv_grfogdensity.value+70);

				}

			}
		}
		// If there's custom fog defined in a level
		else if (mapheaderinfo[gamemap-1].levelfog)
		{
			// Change the color and density
			char *fog = mapheaderinfo[gamemap-1].levelfogcolor;
			GL_SetSpecialState(HWD_SET_FOG_COLOR, atohex(fog));
			GL_SetSpecialState(HWD_SET_FOG_DENSITY, mapheaderinfo[gamemap-1].levelfogdensity);
		}
#ifdef DAYTONIGHT // Day to night fog changing
		// Day to Night fog is overridden by levelfog, then by FOF fog if it's defined in a map header
		else if (mapheaderinfo[gamemap-1].daytonight)
		{
			// Setup the time information
			time_t timer;
			struct tm *timeinfo;

			time(&timer);
			timeinfo = localtime(&timer);

			// Early morning
			if (timeinfo->tm_hour >= 5 && timeinfo->tm_hour <= 8) // 5am to 8am
			{
				GL_SetSpecialState(HWD_SET_FOG_COLOR, 0xAAAAAA); // White fog during the morning
				GL_SetSpecialState(HWD_SET_FOG_DENSITY, cv_grfogdensity.value*2);
			}

			// Day sky
			else if (timeinfo->tm_hour > 8 && timeinfo->tm_hour <= 17) // 9am to 5pm
			{
				// Go to the default preferences here
				GL_SetSpecialState(HWD_SET_FOG_DENSITY, 25);
				GL_SetSpecialState(HWD_SET_FOG_COLOR, 0xAAAAAA);
			}

			// Sunset
			else if (timeinfo->tm_hour == 18 || timeinfo->tm_hour == 19) // 6pm to 7pm Sunset
			{
				GL_SetSpecialState(HWD_SET_FOG_COLOR, 0x902134); // Nice red sunsety fog
				GL_SetSpecialState(HWD_SET_FOG_DENSITY, cv_grfogdensity.value*2);
			}

			else // 8pm to 4am, night time
			{
				GL_SetSpecialState(HWD_SET_FOG_COLOR, 0x000000); // Black fog
				GL_SetSpecialState(HWD_SET_FOG_DENSITY, cv_grfogdensity.value*3); // It's darker at night
			}

		}

#endif
		// If there's no other custom fog defined, or otherwise, just turn off the distance and color
		else
		{
			GL_SetSpecialState(HWD_SET_FOG_COLOR, 0);
			GL_SetSpecialState(HWD_SET_FOG_DENSITY, 0);
		}

	}
	else // If fog is off
	    GL_SetSpecialState(HWD_SET_FOG_MODE, 0); // Turn it totally off
}
#endif

#ifdef IFRAME
static void R_InterpolateView (fixed_t frac, interpview_t *iview, boolean NoInterpolateView)
{
	//	frac = r_TicFrac;
	if (NoInterpolateView)
	{
		NoInterpolateView = false;
		iview->oviewx = iview->nviewx;
		iview->oviewy = iview->nviewy;
		iview->oviewz = iview->nviewz;
		iview->oviewpitch = iview->nviewpitch;
		iview->oviewangle = iview->nviewangle;
	}

	viewx = iview->oviewx + FixedMul (frac, iview->nviewx - iview->oviewx);
	viewy = iview->oviewy + FixedMul (frac, iview->nviewy - iview->oviewy);
	viewz = iview->oviewz + FixedMul (frac, iview->nviewz - iview->oviewz);
	if (!paused && (!netgame))
	{
		viewangle = iview->nviewangle + (localangle & 0xFFFF0000);

		fixed_t delta = -(signed)(localaiming & 0xFFFF0000);

		viewpitch = iview->nviewpitch;
		if (delta > 0)
		{
			// Avoid overflowing viewpitch (can happen when a netgame is stalled)
			if (viewpitch + delta <= viewpitch)
			{
				viewpitch = +ANGLE_1;
			}
			else
			{
				viewpitch = MIN(viewpitch + delta, +ANGLE_1);
			}
		}
		else if (delta < 0)
		{
			// Avoid overflowing viewpitch (can happen when a netgame is stalled)
			if (viewpitch + delta >= viewpitch)
			{
				viewpitch = -ANGLE_1;
			}
			else
			{
				//viewpitch = MAX(viewpitch + delta, -ANGLE_1); // SRB2CBTODO: Support this
			}
		}
	}
	else
	{
		viewpitch = iview->oviewpitch + FixedMul (frac, iview->nviewpitch - iview->oviewpitch);
		viewangle = iview->oviewangle + FixedMul (frac, iview->nviewangle - iview->oviewangle);
	}

	// Due to interpolation this is not necessarily the same as the sector the camera is in.
	viewsector = R_PointInSubsector(viewx, viewy)->sector;
}
#endif


// Translate views of the world to OpenGL floating point
// SRB2CBTODO: Handle interpoliation here
static void HWR_SetupView(boolean viewnumber)
{
	// copy view cam position for local use
	viewx = viewx;
	viewy = viewy;
	viewz = viewz;
	viewangle = viewangle;

#ifdef IFRAME
	interpview_t *iview;

	iview = malloc(sizeof(interpview_t)); // SRB2CBTODO: viewxyz

	int nowtic = I_GetTime();

	if (iview->otic != -1 && nowtic > iview->otic)
	{
		iview->otic = nowtic;
		iview->oviewx = iview->nviewx;
		iview->oviewy = iview->nviewy;
		iview->oviewz = iview->nviewz;
		iview->oviewpitch = iview->nviewpitch;
		iview->oviewangle = iview->nviewangle;
	}

	{
		iview->nviewx = viewx;
		iview->nviewy = viewy;
		iview->nviewz = viewz;
	}

	iview->nviewpitch = viewpitch;
	iview->nviewangle = viewangle;

	boolean reset;

	if (iview->otic == -1)
	{
		reset = true;//R_ResetViewInterpolation(); // IFRAMETODO
		iview->otic = nowtic;
	}
#endif

#ifdef IFRAME
	R_InterpolateView(FRACUNIT, iview, reset);
#endif

	// set window position
	gr_centery = gr_basecentery;
	gr_viewwindowy = gr_baseviewwindowy;
	gr_windowcentery = gr_basewindowcentery;

	if ((rendersplit && splitscreen) && viewnumber == true)
	{
		gr_viewwindowy += (vid.height/2.0f);
		gr_windowcentery += (vid.height/2.0f);
	}

	// check for new console commands.
	NetUpdate();

	gr_viewx = FIXED_TO_FLOAT(viewx);
	gr_viewy = FIXED_TO_FLOAT(viewy);
	gr_viewz = FIXED_TO_FLOAT(viewz);
	gr_viewsin = FIXED_TO_FLOAT(viewsin);
	gr_viewcos = FIXED_TO_FLOAT(viewcos);

	gr_viewludsin = FIXED_TO_FLOAT(FINECOSINE(viewpitch>>ANGLETOFINESHIFT));
	gr_viewludcos = FIXED_TO_FLOAT(-FINESINE(viewpitch>>ANGLETOFINESHIFT));

	// Adjust the FOV values by the user controls
	fixed_t basefov;
	basefov = cv_grfov.value;

	if (basefov > 130*FRACUNIT)
		basefov = 130*FRACUNIT;
	if (basefov < 30*FRACUNIT)
		basefov = 30*FRACUNIT;
	if (!basefov)
		basefov = 90*FRACUNIT;

	gr_fov = FIXED_TO_FLOAT(basefov);


	// This sets the view of the 3D world // SRB2CBTODO: Add roll support in addition to viewangle and aiming support
	// It should replace all other gr_viewxxx when finished

	// Rotate and move around the world by these values
	atransform.anglex = (float)(viewpitch>>ANGLETOFINESHIFT)*(360.0f/(float)FINEANGLES);
	atransform.angley = (float)(viewangle>>ANGLETOFINESHIFT)*(360.0f/(float)FINEANGLES);
	if (cv_speed.value != 16 && cv_speed.value < 360 && cv_grtest.value == 8)
		atransform.glrollangle = cv_speed.value; // SRB2CBTODO: Make sprites support this, they need to use transform!!!
	else
		atransform.glrollangle = 0; //viewroll/(ANG45/45); // #ifdef AWESOME
	atransform.x      = gr_viewx;  // FIXED_TO_FLOAT(viewx)
	atransform.y      = gr_viewy;  // FIXED_TO_FLOAT(viewy)
	atransform.z      = gr_viewz;  // FIXED_TO_FLOAT(viewz)
	atransform.scalex = 1;
	atransform.scaley = ORIGINAL_ASPECT;
	atransform.scalez = 1;
	atransform.fovxangle = FIXED_TO_FLOAT(basefov)+grfovadjust;
	atransform.fovyangle = FIXED_TO_FLOAT(basefov)+grfovadjust;
	atransform.splitscreen = (rendersplit && splitscreen);

	gr_fovlud = (float)(1/tan(((basefov>>FRACBITS) + grfovadjust)*M_PI/360));
}


void HWR_Transform(float *cx, float *cy, float *cz, boolean sprite)
{
	if (sprite)
	{
		float tr_x, tr_y;
		tr_x = *cz;
		tr_y = *cy;

		*cy = (tr_x * gr_viewludcos) + (tr_y * gr_viewludsin);
		*cz = (tr_x * gr_viewludsin) - (tr_y * gr_viewludcos);
		// ---------------------- test ----------------------------------

		// Scale y before frustum so that frustum can be scaled to screen height
		*cy *= ORIGINAL_ASPECT * gr_fovlud;
		*cx *= gr_fovlud;
	}
	else
	{
		float tr_x,tr_y;
		// translation
		tr_x = *cx - gr_viewx;
		tr_y = *cz - gr_viewy;
		//	*cy = *cy;

		// rotation around vertical y axis
		*cx = (tr_x * gr_viewsin) - (tr_y * gr_viewcos);
		tr_x = (tr_x * gr_viewcos) + (tr_y * gr_viewsin);

		//look up/down, do the 2 in one!
		tr_y = *cy - gr_viewz;

		*cy = (tr_x * gr_viewludcos) + (tr_y * gr_viewludsin);
		*cz = (tr_x * gr_viewludsin) - (tr_y * gr_viewludcos);

		//scale y before frustum so that frustum can be scaled to screen height
		*cy *= ORIGINAL_ASPECT * gr_fovlud;
		*cx *= gr_fovlud;
	}
}


// The skybox must be drawn before the rest of the real world is drawn,
// so what must be done is to render map geometry at a different posistion then normal
// This is essentialy the same as HWR_RenderPlayerView but with a different viewpoint
static void HWR_RenderSkyBoxView(mobj_t *skyboxview, mobj_t *skycentermobj, player_t *player, boolean viewnumber)
{
	if (!skyboxview)
		I_Error("HWR_RenderSkyBoxView: No skybox viewpoint was found!\n");

	// Skyboxes are simply map geometry rendered at a different view
	// than the player's view, this function is very similar to
	// HWR_RenderPlayerView, but the viewpoint is modified to be to a skybox view mobj's

	// Setup all the normal frame's stuff first, to make sure that everything gets any data it needs first
	R_SetupFrame(player);

#if 0 // Proper SKYBOXES!
	// Make sure that we have the right viewx and viewy (this is mainly for netgames)
	viewplayer = player;

	if (((cv_chasecam.value && thiscam == &camera) || (rendersplit && cv_chasecam2.value && thiscam == &camera2))
		&& !player->awayviewtics)
	{
		viewx = thiscam->x;
		viewy = thiscam->y;

		if (thiscam->subsector)
			viewsector = thiscam->subsector->sector;
		else
			viewsector = R_PointInSubsector(viewx, viewy)->sector;
	}
	else
	{
		viewx = viewmobj->x;
		viewy = viewmobj->y;

		if (viewmobj->subsector)
			viewsector = viewmobj->subsector->sector;
		else
			viewsector = R_PointInSubsector(viewx, viewy)->sector;
	}
#endif

	// SRB2CBTODO: Support for skyboxes that move slightly relative to the player,
	// a seperate realative skybox center could be setup on the real map so as the player moves a large distance,
	// the skybox view moves slightly to make a more realistic sense of motion
	// center's deaf flag could determine if it goes up/down relitively too
	// Setup the different views here

	if (moveskybox && (skycentermobj != NULL))
	{
		double horizonscale;
		double zscale;
		double zratio;

		double xdiff;
		double ydiff;

		// Skycentermobj(the static view) to viewxyz(current view) = Skyboxmobj(static) to XYZ(translated new coordinates)

		// XYZ ratio is the ratio of the player's currnt view(viewxyz) to the map skybox center

		// ratio = static view / current view
		// staticview = ratio * currentview
		// currentview = staticview / ratio


		// 10 / 20 = 0.5
		// 0.5 * 20 = 10

		// static view / currentview = ratio
		// ratio * static view = currentview

		xdiff = skycentermobj->x - viewx;
		ydiff = skycentermobj->y - viewy;

		horizonscale = skycentermobj->angle/(ANG45/45);
		zscale = skycentermobj->angle/(ANG45/45);

		// The game will treat horizonscales of 0 or undefined with default values
		if (horizonscale == 0)
		    horizonscale = 1;
        if (zscale == 0)
		    zscale = 1;

		zratio = skycentermobj->z/(double)viewz;

		// xyz ratio should use a scale based on the angle of skycenter
		// viewxyz is the dynamic view

        if (!(skycentermobj->flags & MF_AMBUSH))
        {
            viewx = (skyboxmobj->x - (xdiff / horizonscale));
		    viewy = (skyboxmobj->y - (ydiff / horizonscale));
        }
        else
        {
    		viewx = skyboxview->x;
		    viewy = skyboxview->y;
        }

		viewz = (skyboxmobj->z + (skyboxmobj->z / zratio)/zscale);
	}
	else
	{
		viewx = skyboxview->x;
		viewy = skyboxview->y;
		viewz = skyboxview->z;
	}

	// Viewsector is used for culling planes and fog processing
	viewsector = R_PointInSubsector(viewx, viewy)->sector;

	// Clear buffers
	HWR_SetLights(viewnumber);
	HWR_SetShadows(viewnumber);
	HWR_ClearSprites();
	drawcount = 0;

	HWR_SetupView(viewnumber);

	HWR_ClearView();

	HWR_DrawSkyTextureBackground(); // SRB2CBTODO: Skyboxes within skyboxes?

#ifdef NEWCLIP
	if (rendermode == render_opengl)
	{
		angle_t a1 = gld_FrustumAngle();
		gld_clipper_Clear();
		gld_clipper_SafeAddClipRange(viewangle + a1, viewangle - a1);
		gld_FrustrumSetup();
	}
#endif

	GL_SetTransform(&atransform);

	// Render map geometry
	HWR_RenderBSPNode((int)(numnodes-1));

	// Handles most fog effects, turns it both on or off per view
	// other fog effects are handled by HWR_Lighting
	// Edit: Replaced fog calculation to be per polygon 0_0
	//HWR_ProcessFog();

	HWR_ResetLights(); // SRB2CBTODO: Find a good way to make sure lights can be reset correctly, add this to other views
	HWR_ResetShadows();

	// Sort MD2 and sprites
	HWR_SortVisSprites();

#ifndef SORTING
	// Render any available MD2s
	HWR_DrawMD2s();
#endif

	// Draw the sprites like it was done previously without translucency
	GL_SetTransform(NULL);

	// Render sprites
	// SRB2CBTODO: Sort an individual sprite with a draw node for correctness!
#ifndef SORTING
	HWR_DrawSprites();

	if (numtransplanes || numtranswalls) // Sort transparent objects and geometry
#endif
		HWR_CreateDrawNodes();

	// For splitscreen
	GL_GClipRect(0, 0, vid.width, vid.height, NZCLIP_PLANE);
}




// Oh yeah, the game can make MIRRORS now too
// This is a view rendered on a mirror line, R_SetupFrame() has already been done once we begin this function
// So just make sure to set it back to normal after the drawing is done
// Remember: The viewangle of the mirror is looking out, as if some thing in the mirror is looking at you
//
// Note that viewz remains the same here
//
// SRB2CBTODO: Support mirror recursions - a mirror reflecting against a mirror, etc.
// SRB2CBTODO: Make this work with no endless loop(needs draw boolean in other funtions etc.)
// SRB2CBTODO: COPY the view on the linedef, don't overlap and such, this is so wrong except for the linedef's view
#ifdef MIRRORS
void HWR_RenderLineReflection(seg_t *mirrorwall, byte maxrecursions)
{
	angle_t startang = viewangle;
	fixed_t startx = viewx;
	fixed_t starty = viewy;

	vertex_t *v1 = mirrorwall->v1;

	// Reflect the current view behind the mirror.
	if (mirrorwall->linedef->dx == 0)
	{ // vertical mirror
		viewx = v1->x - startx + v1->x;
	}
	else if (mirrorwall->linedef->dy == 0)
	{ // horizontal mirror
		viewy = v1->y - starty + v1->y;
	}
	else
	{ // any mirror--use floats to avoid integer overflow
		vertex_t *v2 = mirrorwall->v2;

		float dx = FIXED_TO_FLOAT(v2->x - v1->x);
		float dy = FIXED_TO_FLOAT(v2->y - v1->y);
		float x1 = FIXED_TO_FLOAT(v1->x);
		float y1 = FIXED_TO_FLOAT(v1->y);
		float x = FIXED_TO_FLOAT(startx);
		float y = FIXED_TO_FLOAT(starty);

		// the above two cases catch len == 0
		float r = ((x - x1)*dx + (y - y1)*dy) / (dx*dx + dy*dy);

		viewx = FLOAT_TO_FIXED((x1 + r * dx)*2 - x);
		viewy = FLOAT_TO_FIXED((y1 + r * dy)*2 - y);
	}

	viewsector = R_PointInSubsector(viewx, viewy)->sector;

	viewangle = 2*R_PointToAngle2(mirrorwall->v1->x, mirrorwall->v1->y,
								  mirrorwall->v2->x, mirrorwall->v2->y) - startang;

	viewsin = FINESINE(viewangle>>ANGLETOFINESHIFT);
	viewcos = FINECOSINE(viewangle>>ANGLETOFINESHIFT);

	// Clear buffers.
	//HWR_SetLights(viewnumber); // SRB2CBTODO: viewnum and console player check for lights and such
	//HWR_SetShadows(viewnumber);
	HWR_ClearSprites();

	HWR_SetupView(0); // SRB2CBTODO: Proper check

	HWR_ClearView();

	HWR_DrawSkyTextureBackground();

	GL_SetTransform(&atransform);

	// Render map geometry
	HWR_RenderBSPNode((int)(numnodes-1));

	// Handles all fog effects, turns it both on or off per view // SRB2CBTODO: Support custom fog for reflections
	// Edit: Replaced fog calculation to be per polygon 0_0
	//HWR_ProcessFog();

	HWR_ResetLights(); // SRB2CBTODO: Find a good way to make sure lights can be reset correctly, add this to other views
	HWR_ResetShadows();

	// Sort MD2 and sprites
	HWR_SortVisSprites();

#ifndef SORTING
	// Render any available MD2s
	HWR_DrawMD2s();
#endif

	GL_SetTransform(NULL);
	// SRB2CBTODO: Sprites need to be drawn the same way
	// all the other stuff (walls, planes, MD2's) are so the code is more flexible
	// Draw sprites without geometry transform (bad)

	// Render sprites
	// SRB2CBTODO: Sort an individual sprite with a draw node for correctness!
#ifndef SORTING
	HWR_DrawSprites();

	if (numtransplanes || numtranswalls) // Sort transparent objects and geometry
#endif
		HWR_CreateDrawNodes();

	// For splitscreen
	//GL_GClipRect(0, 0, vid.width, vid.height, NZCLIP_PLANE); // SRB2CBTODO: Needed?

	viewangle = startang;
	viewx = startx;
	viewy = starty;
}
#endif

boolean firstblur = true;


// ==========================================================================
//
// ==========================================================================
// This is where the main 3D world is drawn, through the player's viewpoint,
// every other viewpoint is called though this function
// SRB2CBTODO: This entire source code file needs a merge into the software renderer's files
void HWR_RenderPlayerView(player_t *player, boolean viewnumber)
{
	if (useskybox == true)
		HWR_RenderSkyBoxView(skyboxmobj, skyboxcentermobj, player, viewnumber);

	// R_SetupFrame: Sets up the viewangle, viewx, viewy, viewz
	R_SetupFrame(player);

	// Clear buffers.
	HWR_SetLights(viewnumber);
	HWR_SetShadows(viewnumber);
	HWR_ClearSprites();
	drawcount = 0;

	HWR_SetupView(viewnumber);

	HWR_ClearView();

	if (useskybox == false)
		HWR_DrawSkyTextureBackground();

#ifdef NEWCLIP
	if (rendermode == render_opengl)
	{
		angle_t a1 = gld_FrustumAngle();
		gld_clipper_Clear();
		gld_clipper_SafeAddClipRange(viewangle + a1, viewangle - a1);
		gld_FrustrumSetup();
	}
#endif

	GL_SetTransform(&atransform);

	// Render map geometry, regular walls, and planes
	HWR_RenderBSPNode((int)(numnodes-1));

	// Kalaron: Now we only have to render the BSP node ONCE! That's the way it should be!

	// Handles all fog effects, turns it both on or off
	// Edit: Replaced fog calculation to be per polygon 0_0
	//HWR_ProcessFog();

	HWR_ResetLights(); // SRB2CBTODO: Find a good way to make sure lights can be reset correctly, add this to other views
	HWR_ResetShadows();

	// Sort MD2 and sprites
	HWR_SortVisSprites();

#ifndef SORTING
	// Render any available MD2s
	HWR_DrawMD2s();
#endif

	GL_SetTransform(NULL);
	// SRB2CBTODO: Sprites need to be drawn the same way
	// all the other stuff (walls, planes, MD2's) are so the code is more flexible
	// Draw sprites without geometry transform (bad)

	// Render sprites
	// SRB2CBTODO: Sort an individual sprite with a draw node for correctness!
#ifndef SORTING
	HWR_DrawSprites();

	if (numtransplanes || numtranswalls) // Sort transparent objects and geometry
#endif
		HWR_CreateDrawNodes();



#ifdef MOTIONBLUR // SRB2CBTODO: THIS ACTUALLY WORKS!!!!! Now use this for per-object motion blur
	// SRB2CBTODO: Combine for only objects that move, possible?
	//  Blur level is the relative amount of reblurred images in the process,
	// 1.0f is no motion blur, 0.1 has many images composed in it
	// 0.0f makes the screen black
	if (((player->speed > 38 && (player->powers[pw_sneakers] > 1))
		 || (abs(player->mo->momz) > 27*FRACUNIT && player->mo->state == &states[S_PLAY_PLG1])
		 || (abs(player->mo->momz) > 36*FRACUNIT && !(player->pflags & PF_MACESPIN)))
		&& !(paused || menuactive)
		&& !(cv_grcompat.value)
		&& (cv_motionblur.value)
		)
	{
		float blurlevel = 1.0f;

		if (player->speed > 70)
			blurlevel = 0.3f;
		else if (player->speed > 60)
			blurlevel = 0.35f;
		else if (player->speed > 50)
			blurlevel = 0.36;
		else if (player->speed > 43)
			blurlevel = 0.37;
		else if (player->speed > 38)
			blurlevel = 0.38;
		else if (abs(player->mo->momz) > 35*FRACUNIT/100)
			blurlevel = ((abs(player->mo->momz)/FRACUNIT)*1.2f)/100.0f;
		else
			blurlevel = 1.0f;

		// Every time the the game stops motion-blurring,
		// it has to clear the last motion blur frame from the buffer
		// Thid lets a new motion blur be calean and not have any artifacts from
		// the last motion blur!
		 if (firstblur)
		{
			glAccum(GL_MULT, 0); // Very important, sets all accumulation values to 0
			glAccum(GL_MULT, 1.0f); // Now we take a direct copy of the current screen at full alpha
			glAccum(GL_ACCUM, 1.0f);
			glAccum(GL_RETURN, 1.0f); // display the screen

			firstblur = false;
		}
		else
		{
			if (blurlevel < 0.0f) // Can't have a negative blur value
				blurlevel = 0.0f;
			if (blurlevel > 1.0f) // Can't have too much blur(dark screen)
				blurlevel = 1.0f;
			glAccum(GL_MULT, 1.0f - blurlevel);

			glAccum(GL_ACCUM, blurlevel);

			glAccum(GL_RETURN, 1.0f);
		}
	}
	else
	{
		firstblur = true; // When we're done blurring, make sure to go into firstblur mode if we motionblur again
	}

#endif


#ifdef OMGSHADE
#endif


	// Check for new console commands.
	NetUpdate();

	// Capture the current image of the screen for postimg processing, or intermission drawing.
	// Always capture a screenshot if one does not exist, the screenshot is stored in memory,
	//if the screen changed, the screentexture also must be updated with
	// the function to stay in sync with the latest image.
	// So the screen only needs to be captured once if the game is paused because the screen won't change,
	// Otherwise the screentexture must be updated every frame to keep in sync
	// GL_MakeScreenTexture can be used anywhere in the code to capture OpenGL's current screen image,
	// but calling it here is sufficient because this function is called every frame
	// It is possible to do other effects that don't need the current screentexture to work,
	// such as motionblur, but GL_MakeScreentexture stores the texture as "screentexture"
	// any other textures need their own texture number if it involves the screen
	// startwipetex and endwipetex are an example of how else this function can be used
	if ((!playerviewscreentex || ((postimgtype && postimgtype != postimg_none)
		&& !(paused || (!netgame && menuactive && !demoplayback))))
			&& (gamestate != GS_INTERMISSION))
	{
		GL_MakeScreenTexture(playerviewscreentex, false);
	}

	if (postimgtype != postimg_none)
		HWR_DoPostProcessor(postimgtype);

	// Handle bonus count and other simple effects
	HWR_ExtraEffects(player);

	// Capture the screentexture again when the player is exiting.
	// This is after screen effects are drawn so the real last screen texture is captureed
	if (player->exiting && player->exiting < 2)
		GL_MakeScreenTexture(playerviewscreentex, false);

	// SRB2CBTODO: Find a use for this HWR_RenderScreenTexture
	if (cv_grtest.value == 11)
		HWR_RenderScreenTexture(1.0f, 0.0f, 0.0f, false, 100);

	if (cv_grtest.value == 12)
		HWR_DOFLine(1.0f, 0.0f, 0.0f, false, 100); // DOF: TODO: Make a blurred image capture of DOF

	// Check for new console commands.
	NetUpdate();

	//------------------------------------------------------------------------
	// Don't render fog on the HUD, menu, console, etc.
	if (cv_grfog.value)
		GL_SetSpecialState(HWD_SET_FOG_MODE, 0);

	// For splitscreen
	GL_GClipRect(0, 0, vid.width, vid.height, NZCLIP_PLANE);
}

// ==========================================================================
//                                                                        FOG
// ==========================================================================

#if 1 // SRB2CBTODO: Save this for later, maybe for timeofday feature? :D
static unsigned int atohex(const char *s)
{
	int iCol;
	const char *sCol;
	char cCol;
	int i;

	// Six characters in a hex string
	if (strlen(s) < 6)
		return 0;

	iCol = 0;
	sCol = s;
	for (i = 0; i < 6; i++, sCol++)
	{
		iCol <<= 4;
		cCol = *sCol;
		if (cCol >= '0' && cCol <= '9')
			iCol |= cCol - '0';
		else
		{
			if (cCol >= 'F')
				cCol -= 'a' - 'A';
			if (cCol >= 'A' && cCol <= 'F')
				iCol = iCol | (cCol - 'A' + 10);
		}
	}
	return iCol;
}
#endif

static void HWR_FoggingOn(void)
{
	GL_SetSpecialState(HWD_SET_FOG_COLOR, 0);
	GL_SetSpecialState(HWD_SET_FOG_DENSITY, 0);
	GL_SetSpecialState(HWD_SET_FOG_MODE, 1);
}

// ==========================================================================
//                                                         3D ENGINE COMMANDS
// ==========================================================================


// **************************************************************************
//                                                            3D ENGINE SETUP
// **************************************************************************

// --------------------------------------------------------------------------
// Add hardware engine commands & consvars
// --------------------------------------------------------------------------
// OpenGL console variables that are saved
void HWR_AddCommands(void)
{
	CV_RegisterVar(&cv_grfov);
}

// Commands for testing OpenGL features
static inline void HWR_AddEngineCommands(void)
{
	CV_RegisterVar(&cv_grtest);
	CV_RegisterVar(&cv_motionblur);
}


// --------------------------------------------------------------------------
// Setup the hardware renderer
// --------------------------------------------------------------------------
void HWR_Startup(void)
{
	static boolean startupdone = false;

	// setup GLPatch_t scaling
	gr_patch_scalex = (float)(1.0f / vid.width);
	gr_patch_scaley = (float)(1.0f / vid.height);

	// do this once
	if (!startupdone)
	{
		CONS_Printf("HWR_Startup()\n");
		// add console cmds & vars
		HWR_AddEngineCommands();
		HWR_InitTextureCache();
	}

	// Setup colorspace for OpenGL
	textureformat = patchformat = GR_RGBA;

	startupdone = true;
}


// --------------------------------------------------------------------------
// Free resources allocated by the hardware renderer
// --------------------------------------------------------------------------
void HWR_Shutdown(void)
{
	CONS_Printf("HWR_Shutdown()\n");
	HWR_FreeExtraSubsectors();
	HWR_FreeTextureCache();
}

// This is almost just the same as ProjectWall, but it's only used for transparent walls or half transparent walls
static void HWR_RenderTransparentWall(wallVert3D *wallVerts, FSurfaceInfo *pSurf, FBITFIELD blendmode, const sector_t *sector, boolean fogwall)
{
	FOutVector  trVerts[4];
	FOutVector  *wv;

	// transform
	wv = trVerts;

	// It starts at 0 for the first wv,
	// render for each the wv's 4 points
	wv->sow = wallVerts->s;
	wv->tow = wallVerts->t;
	wv->x   = wallVerts->x;
	wv->y   = wallVerts->y;
	wv->z   = wallVerts->z;

	wv++;
	wallVerts++;

	wv->sow = wallVerts->s;
	wv->tow = wallVerts->t;
	wv->x   = wallVerts->x;
	wv->y   = wallVerts->y;
	wv->z   = wallVerts->z;

	wv++;
	wallVerts++;

	wv->sow = wallVerts->s;
	wv->tow = wallVerts->t;
	wv->x   = wallVerts->x;
	wv->y   = wallVerts->y;
	wv->z   = wallVerts->z;

	wv++;
	wallVerts++;

	wv->sow = wallVerts->s;
	wv->tow = wallVerts->t;
	wv->x   = wallVerts->x;
	wv->y   = wallVerts->y;
	wv->z   = wallVerts->z;

	// Check for frontsector again, sorting can mess this up
	// (and the fronsector can change to another sector with different lighting)
	// because the game is setup to use frontsector as a global,
	// but the actual frontsector will change if an object isn't
	// rendered the correct order with the fronsector
	// So we pass a constant sector all the way to this function

	// This is only done for sorted transparent walls, opaque walls don't need a lighting recalculation
	if (sector)
	{
		const float wallalpha = pSurf->FlatColor.s.alpha; // Preseve the alpha value

		byte lightnum;
		lightnum = sector->lightlevel;
		extracolormap_t *colormap;
		colormap = sector->extra_colormap;
		pSurf->FlatColor.s.alpha = 0xff;

		if (colormap)
		{
			if (colormap->fog)
				pSurf->FlatColor.rgba = HWR_Lighting(LightLevelToLum(lightnum), colormap->rgba, false, false);
			else
				pSurf->FlatColor.rgba = HWR_Lighting(LightLevelToLum(lightnum), colormap->rgba, true, true);
		}
		else
			pSurf->FlatColor.rgba = HWR_Lighting(LightLevelToLum(lightnum), NORMALFOG, false, false);

		pSurf->FlatColor.s.alpha = wallalpha;

		if (fogwall)
		{
			// In software mode, fog blocks are simply a colormapped area of the map,
			// they can never be visibly fully opaque, they use rover's master frontsector's
			// to modulate the transparency because software renders darker things more opqaue,
			// so basically, OpenGL mode can't render a colormaped area without polygons,
			// so use software's alpha darkness-colormap thingy method :P
			// (sector is rover->master->frontsector with fogblocks)

			if (sector->extra_colormap)
				pSurf->FlatColor.rgba = HWR_Lighting(sector->lightlevel, sector->extra_colormap->rgba, false, false);
			else
				pSurf->FlatColor.rgba = HWR_Lighting(sector->lightlevel, NORMALFOG, false, false);

			pSurf->FlatColor.s.alpha = sector->lightlevel/1.4;

			// Important check, the game does not draw non-colored fog blocks at full brightness
			// these fog blocks are not drawn as part of special visual effects for certain levels
			// full brightness = 255 brightness
			// (0 alpha walls are automatically not rendered when passed to HWR_AddTransparentWall
			if (!(sector->extra_colormap && sector->extra_colormap->fog))
			{
				if (sector->lightlevel == 255)
					pSurf->FlatColor.s.alpha = 0;
			}

		}
	}

	GL_DrawPolygon(pSurf, trVerts, 4, blendmode|PF_Modulated|PF_Occlude, 0);

#ifdef WALLSPLATS
	if (gr_curline && gr_curline->linedef->splats && cv_splats.value)
		HWR_DrawSegsSplats(pSurf);
#endif

	// SRB2CBTODO: Support dynamic light casting based on this wall's alpha and such
	HWR_WallLighting(trVerts);
	HWR_WallShading(trVerts);
}

void HWR_SetPaletteColor(int palcolor)
{
	GL_SetSpecialState(HWD_SET_PALETTECOLOR, palcolor);
}

int HWR_GetTextureUsed(void)
{
	return GL_GetTextureUsed();
}

//
// HWR_DoPostProcessor
//
// Perform a particular image postprocessing function.
//
void HWR_DoPostProcessor(ULONG type)
{
	if (rendermode != render_opengl)
		return;

	if (!postimgtype)
		return;

	if (splitscreen && rendersplit)
		return;

	// This function cannot continue if there is no texture
	if (!playerviewscreentex)
		return;

	if (cv_grcompat.value)
		return;

	// 10 by 10 grid. 2 coordinates (xy)
	float v[SCREENVERTS][SCREENVERTS][2];
	byte x, y;
	byte flipy; // For flipping the screen upside down

	// Increment values for effects that move the screen
	static double disStart = 0;
	static double disStart2 = 0;
	static double disStart3 = 0;

	// Modifies the wave for postimg_water
	static const int WAVELENGTH = 20; // Lower is longer
	static const int AMPLITUDE = 20; // Lower is bigger
	static const int FREQUENCY = 16; // Lower is faster
	// Shakes the screen for postimg_shake
	static const int AMPLITUDE2 = 16; // Lower is bigger
	static const int FREQUENCY2 = 25; // Lower is faster
	// Distorts the screen for postimg_heat
	static const int WAVELENGTH2 = 20;
	static const int AMPLITUDE3 = 20; // Lower is bigger
	static const int FREQUENCY3 = 16; // Lower is faster

	for (x = 0; x < SCREENVERTS; x++)
	{
		// Set the base screen coordinates
		if (type & postimg_flip) // Invert the screen
		{
			for (y = 0, flipy = SCREENVERTS; y < SCREENVERTS; y++, flipy--)
			{
				// Flip the screen.
				v[x][y][0] = (x/((float)(SCREENVERTS-1.0f)/9.0f))-4.5f;
				v[x][y][1] = (flipy/((float)(SCREENVERTS-1.0f)/9.0f))-5.5f;

				if (type & postimg_water)
					v[x][y][0] += (float)sin((disStart+(y*WAVELENGTH))/FREQUENCY)/AMPLITUDE;
				else if (type & postimg_heat)
					v[x][y][1] += (float)sin((disStart3+(y*WAVELENGTH2))/FREQUENCY3)/AMPLITUDE3;
				if (type & postimg_shake)
				{
					if (postimgparam == 1)
						v[x][y][1] += (float)sin((disStart2+(y))/FREQUENCY2)/AMPLITUDE2;
					else
						v[x][y][0] += (float)sin((disStart2+(y))/FREQUENCY2)/AMPLITUDE2;
				}
			}
		}
		else // Normal screen
		{
			for (y = 0; y < SCREENVERTS; y++)
			{
				v[x][y][0] = (x/((float)(SCREENVERTS-1.0f)/9.0f))-4.5f;
				v[x][y][1] = (y/((float)(SCREENVERTS-1.0f)/9.0f))-4.5f;

				if (type & postimg_water)
					v[x][y][0] += (float)sin((disStart+(y*WAVELENGTH))/FREQUENCY)/AMPLITUDE;
				else if (type & postimg_heat)
					v[x][y][1] += (float)sin((disStart3+(y*WAVELENGTH2))/FREQUENCY3)/AMPLITUDE3;
				if (type & postimg_shake)
				{
					if (postimgparam == 1)
						v[x][y][1] += (float)sin((disStart2+(y))/FREQUENCY2)/AMPLITUDE2;
					else
						v[x][y][0] += (float)sin((disStart2+(y))/FREQUENCY2)/AMPLITUDE2;
				}
			}
		}
	}
	// Do not increment values for moving effects when paused
	if (!(paused || (!netgame && menuactive && !demoplayback)))
	{
		disStart += 1;
		disStart2 += 32;
		disStart3 += 1;
	}
	GL_PostImgRedraw(v);
}

void HWR_StartScreenWipe(void)
{
	GL_StartScreenWipe();
}

void HWR_EndScreenWipe(void)
{
	HWRWipeCounter = 1.0f;
	GL_EndScreenWipe();
}

// Prepare the screen for fading to black.
void HWR_PrepFadeToBlack(boolean white)
{
	FOutVector      v[4];
	FSurfaceInfo Surf;

	// SRB2CB: highest value so it doesn't overlap with anything else
	v[0].x = v[2].y = v[3].x = v[3].y = -5.0f;
	v[0].y = v[1].x = v[1].y = v[2].x = 5.0f;
	v[0].z = v[1].z = v[2].z = v[3].z = 5.0f;

	if (white)
		Surf.FlatColor.s.red = Surf.FlatColor.s.green = Surf.FlatColor.s.blue = 255;
	else
		Surf.FlatColor.s.red = Surf.FlatColor.s.green = Surf.FlatColor.s.blue = 0;

	Surf.FlatColor.s.alpha = 255;

	GL_DrawPolygon(&Surf, v, 4, PF_Modulated|PF_Translucent|PF_NoDepthTest|PF_NoTexture, 0);
}

void HWR_DrawIntermissionBG(void)
{
	GL_DrawIntermissionBG();
}

void HWR_DoScreenWipe(void)
{
	HWRWipeCounter -= 0.07f;

	GL_DoScreenWipe(HWRWipeCounter);
}

#endif // HWRENDER
