// Emacs style mode select   -*- C++ -*-
//-----------------------------------------------------------------------------
//
// Copyright (C) 1998-2006 by Sonic Team Junior.
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
//
//-----------------------------------------------------------------------------
/// \file
/// \brief OpenGL API for SRB2CB

#if defined (_WIN32) || defined (_WIN64)
//#define WIN32_LEAN_AND_MEAN
#define RPC_NO_WINDOWS_H
#include <windows.h>
#endif

#ifdef __GNUC__
#include <unistd.h>
#endif

#include <stdarg.h>
#include <math.h>
#include "../../d_netcmd.h"
#include "r_opengl.h"
#include "../../doomdef.h"

#ifdef HWRENDER
// for KOS: GL_TEXTURE_ENV, glAlphaFunc, glColorMask, glPolygonOffset, glReadPixels, GL_ALPHA_TEST, GL_POLYGON_OFFSET_FILL

#ifndef GL_TEXTURE_MIN_LOD
#define GL_TEXTURE_MIN_LOD			0x813A
#endif
#ifndef GL_TEXTURE_MAX_LOD
#define GL_TEXTURE_MAX_LOD			0x813B
#endif

struct GLRGBAFloat
{
	GLfloat red;
	GLfloat green;
	GLfloat blue;
	GLfloat alpha;
};
typedef struct GLRGBAFloat GLRGBAFloat;

// ==========================================================================
//                                                                  CONSTANTS
// ==========================================================================

// With OpenGL 1.1+, the first texture should be 1
#define NOTEXTURE_NUM     1     // small white texture
#define FIRST_TEX_AVAIL   (NOTEXTURE_NUM + 1)

#define      N_PI_DEMI               (M_PI/2) //(1.5707963268f)

#define      ASPECT_RATIO            (1.0f)  //(320.0f/200.0f)
// SRB2CBTODO:
// Note that the difference between far/near clipping plane has effects on
// z clipping, NEAR_CLIPPING_PLANE's value being greater than 0 has a greater effect than farclipping's
// Also remember, if the near clipping is set too high, you can't see objects too close to the camera
// So for the games needs:
// The player being at a minimum scale of 25 and the camera being directly on the floor and causing no clipping issues
// is as sufficient as is possible to get in real time 3D OpenGL, this will cause issues with detail on far objects, which
// is the only issue that cannot be directly resolved here
// SRB2CBTODO: It may be possible to use polygonoffset to stop any z-issues on far objects;
// but how much of a performance effect does this have, and how portable is this?
#define      FAR_CLIPPING_PLANE      50000.0f // SRB2CBTODO: Should possibly be dynamic to levelsize on level load?
float NEAR_CLIPPING_PLANE =   NZCLIP_PLANE;

#define      MIPMAP_MASK             0x0100

// **************************************************************************
//                                                                    GLOBALS
// **************************************************************************


static  GLuint      NextTexAvail    = FIRST_TEX_AVAIL;
static  GLuint      tex_downloaded  = 0;
static  GLfloat     fov             = 90.0f;
static  GLuint      pal_col         = 0;
static  FRGBAFloat  const_pal_col;
static  FBITFIELD   CurrentPolyFlags;

static  FTextureInfo*  gr_cachetail = NULL;
static  FTextureInfo*  gr_cachehead = NULL;

RGBA_t  myPaletteData[256];
GLint   screen_width    = 0;               // used by Draw2DLine()
GLint   screen_height   = 0;
GLbyte  screen_depth    = 0;
GLint   textureformatGL = 0;
GLint anisotropy = 0;
static GLint min_filter = GL_LINEAR;
static GLint mag_filter = GL_LINEAR;
static GLint anisotropic_filter = 0;
static FTransform  md2_transform;

const GLubyte *gl_extensions = NULL;


#ifndef MINI_GL_COMPATIBILITY
// SRB2CBTODO: Corona support
static GLdouble    modelMatrix[16];
static GLdouble    projMatrix[16];
static GLint       viewport[4];
#endif


#ifdef USE_PALETTED_TEXTURE
PFNGLCOLORTABLEEXTPROC  glColorTableEXT = NULL;
GLubyte                 palette_tex[256*3];
#endif

// Special texture numbers reserved for screentexture use,
// these values are high so they don't mess with any other currently used texture
GLuint playerviewscreentex = 1000000; // screentexture defaults to 0 to check if the texture has data yet
GLuint skyviewscreentex = 1000001;
// For OpenGL screen fades
static GLuint startwipetex = 1000002;
static GLuint endwipetex = 1000003;

// shortcut for ((float)1/i)
static const GLfloat byte2float[256] = {
	0.000000f, 0.003922f, 0.007843f, 0.011765f, 0.015686f, 0.019608f, 0.023529f, 0.027451f,
	0.031373f, 0.035294f, 0.039216f, 0.043137f, 0.047059f, 0.050980f, 0.054902f, 0.058824f,
	0.062745f, 0.066667f, 0.070588f, 0.074510f, 0.078431f, 0.082353f, 0.086275f, 0.090196f,
	0.094118f, 0.098039f, 0.101961f, 0.105882f, 0.109804f, 0.113725f, 0.117647f, 0.121569f,
	0.125490f, 0.129412f, 0.133333f, 0.137255f, 0.141176f, 0.145098f, 0.149020f, 0.152941f,
	0.156863f, 0.160784f, 0.164706f, 0.168627f, 0.172549f, 0.176471f, 0.180392f, 0.184314f,
	0.188235f, 0.192157f, 0.196078f, 0.200000f, 0.203922f, 0.207843f, 0.211765f, 0.215686f,
	0.219608f, 0.223529f, 0.227451f, 0.231373f, 0.235294f, 0.239216f, 0.243137f, 0.247059f,
	0.250980f, 0.254902f, 0.258824f, 0.262745f, 0.266667f, 0.270588f, 0.274510f, 0.278431f,
	0.282353f, 0.286275f, 0.290196f, 0.294118f, 0.298039f, 0.301961f, 0.305882f, 0.309804f,
	0.313726f, 0.317647f, 0.321569f, 0.325490f, 0.329412f, 0.333333f, 0.337255f, 0.341176f,
	0.345098f, 0.349020f, 0.352941f, 0.356863f, 0.360784f, 0.364706f, 0.368627f, 0.372549f,
	0.376471f, 0.380392f, 0.384314f, 0.388235f, 0.392157f, 0.396078f, 0.400000f, 0.403922f,
	0.407843f, 0.411765f, 0.415686f, 0.419608f, 0.423529f, 0.427451f, 0.431373f, 0.435294f,
	0.439216f, 0.443137f, 0.447059f, 0.450980f, 0.454902f, 0.458824f, 0.462745f, 0.466667f,
	0.470588f, 0.474510f, 0.478431f, 0.482353f, 0.486275f, 0.490196f, 0.494118f, 0.498039f,
	0.501961f, 0.505882f, 0.509804f, 0.513726f, 0.517647f, 0.521569f, 0.525490f, 0.529412f,
	0.533333f, 0.537255f, 0.541177f, 0.545098f, 0.549020f, 0.552941f, 0.556863f, 0.560784f,
	0.564706f, 0.568627f, 0.572549f, 0.576471f, 0.580392f, 0.584314f, 0.588235f, 0.592157f,
	0.596078f, 0.600000f, 0.603922f, 0.607843f, 0.611765f, 0.615686f, 0.619608f, 0.623529f,
	0.627451f, 0.631373f, 0.635294f, 0.639216f, 0.643137f, 0.647059f, 0.650980f, 0.654902f,
	0.658824f, 0.662745f, 0.666667f, 0.670588f, 0.674510f, 0.678431f, 0.682353f, 0.686275f,
	0.690196f, 0.694118f, 0.698039f, 0.701961f, 0.705882f, 0.709804f, 0.713726f, 0.717647f,
	0.721569f, 0.725490f, 0.729412f, 0.733333f, 0.737255f, 0.741177f, 0.745098f, 0.749020f,
	0.752941f, 0.756863f, 0.760784f, 0.764706f, 0.768627f, 0.772549f, 0.776471f, 0.780392f,
	0.784314f, 0.788235f, 0.792157f, 0.796078f, 0.800000f, 0.803922f, 0.807843f, 0.811765f,
	0.815686f, 0.819608f, 0.823529f, 0.827451f, 0.831373f, 0.835294f, 0.839216f, 0.843137f,
	0.847059f, 0.850980f, 0.854902f, 0.858824f, 0.862745f, 0.866667f, 0.870588f, 0.874510f,
	0.878431f, 0.882353f, 0.886275f, 0.890196f, 0.894118f, 0.898039f, 0.901961f, 0.905882f,
	0.909804f, 0.913726f, 0.917647f, 0.921569f, 0.925490f, 0.929412f, 0.933333f, 0.937255f,
	0.941177f, 0.945098f, 0.949020f, 0.952941f, 0.956863f, 0.960784f, 0.964706f, 0.968628f,
	0.972549f, 0.976471f, 0.980392f, 0.984314f, 0.988235f, 0.992157f, 0.996078f, 1.000000f
};

float byteasfloat(byte fbyte)
{
	return (float)(byte2float[fbyte]*2.0f);
}

static I_Error_t I_Error_GL = NULL;

// -----------------+
// SetNoTexture     : Disable texture
// -----------------+
static void SetNoTexture(void)
{
	// Set the texture to OpenGL's small white texture.
	if (tex_downloaded != NOTEXTURE_NUM)
	{
		glBindTexture(GL_TEXTURE_2D, NOTEXTURE_NUM);
		tex_downloaded = NOTEXTURE_NUM;
	}
}


// -----------------+
// SetModelView     : Set the view of the 3D world, including fov, this is only called once
// -----------------+
void SetModelView(GLint w, GLint h)
{
	screen_width = w;
	screen_height = h;

	glViewport(0, 0, w, h);

	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	gluPerspective(fov, ASPECT_RATIO, NEAR_CLIPPING_PLANE, FAR_CLIPPING_PLANE);

	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();

#ifndef MINI_GL_COMPATIBILITY
	glGetIntegerv(GL_VIEWPORT, viewport);
	glGetDoublev(GL_PROJECTION_MATRIX, projMatrix);
#endif
}


// -----------------+
// SetStates        : Set permanent states, this is only done once
// -----------------+
void SetStates(void)
{
	// Bind little white RGBA texture to ID NOTEXTURE_NUM.
	FUINT Data[8*8];
	int i;

	glEnable(GL_TEXTURE_2D);      // Two-dimensional texturing
#ifndef KOS_GL_COMPATIBILITY
	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

	glAlphaFunc(GL_NOTEQUAL, 0.0f);
#endif

	glClearColor(0.0, 0.0, 0.0, 1.0);

	glEnable(GL_BLEND);           // enable color blending


#ifndef KOS_GL_COMPATIBILITY
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
#endif

	glEnable(GL_DEPTH_TEST);    // check the depth buffer
	glClearDepth(1.0f);
	glDepthRange(0.0f, 1.0f);
	glDepthFunc(GL_LEQUAL);

#ifdef GLACCUML
	glClearAccum(0.0, 0.0, 0.0, 0.0);
#endif

#ifdef GLSTENCIL
	glClear(GL_STENCIL_BUFFER_BIT);
#endif

	// This sets CurrentPolyFlags to the actual configuration
	CurrentPolyFlags = 0xffffffff;
	GL_SetBlend(0);

	for (i = 0; i < 64; i++)
		Data[i] = 0xffFFffFF;       // white pixel

	tex_downloaded = (GLuint)-1;
	SetNoTexture();

#ifndef KOS_GL_COMPATIBILITY
	glPolygonOffset(-1.0f, -1.0f);
#endif

	// When no translucency
	glLoadIdentity();
	glScalef(1.0f, 1.0f, -1.0f);
#ifndef MINI_GL_COMPATIBILITY
	glGetDoublev(GL_MODELVIEW_MATRIX, modelMatrix); // added for new coronas' code (without depth buffer)
#endif
}


// -----------------+
// Flush            : Flush the OpenGL texture cache
//                  : Clear list of downloaded mipmaps
// -----------------+
void GL_Flush(void)
{
	while (gr_cachehead)
	{
		glDeleteTextures(1, (GLuint *)&gr_cachehead->downloaded);
		gr_cachehead->downloaded = 0;
		gr_cachehead = gr_cachehead->nextmipmap;
	}
	gr_cachetail = gr_cachehead = NULL;
	NextTexAvail = FIRST_TEX_AVAIL;
	tex_downloaded = 0;
}


// -----------------+
// isExtAvailable   : Look if an OpenGL extension is available
// Returns          : true if extension available
// -----------------+
int isExtAvailable(const char *extension, const GLubyte *start)
{
	GLubyte         *where, *terminator;

	if (!extension || !start) return 0;
	where = (GLubyte *) strchr(extension, ' ');
	if (where || *extension == '\0')
		return 0;

	for (;;)
	{
		where = (GLubyte *) strstr((const char *) start, extension);
		if (!where)
			break;
		terminator = where + strlen(extension);
		if (where == start || *(where - 1) == ' ')
			if (*terminator == ' ' || *terminator == '\0')
				return 1;
		start = terminator;
	}
	return 0;
}


// -----------------+
// GL_Init          : Initialise the OpenGL interface API
// Returns          :
// -----------------+
boolean GL_Init(I_Error_t FatalErrorFunction)
{
	I_Error_GL = FatalErrorFunction;
	CONS_LogPrintf("%s\n", DRIVER_STRING);
	return 1;
}

// -----------------+
// ClearMipMapCache : Flush OpenGL textures from memory
// -----------------+
void GL_ClearMipMapCache(void)
{
	GL_Flush();
}


// -----------------+
// ReadRect         : Read a rectangle region of the truecolor framebuffer
//                  : store pixels as 16bit 565 RGB
// Returns          : 16bit 565 RGB pixel array stored in dst_data
// -----------------+
// This is used for screenshots saved to an image file
void GL_ReadRect (int x, int y, int width, int height,
				  int dst_stride, USHORT * dst_data)
{
	int i;
#ifdef KOS_GL_COMPATIBILITY
	(void)x;
	(void)y;
	(void)width;
	(void)height;
	(void)dst_stride;
	(void)dst_data;
#else
	if (dst_stride == width*3)
	{
		GLubyte*top = (GLvoid*)dst_data, *bottom = top + dst_stride * (height - 1);
		GLubyte *row = malloc(dst_stride);
		if (!row) return;
		glReadPixels(x, y, width, height, GL_RGB, GL_UNSIGNED_BYTE, dst_data);
		for (i = 0; i < height/2; i++)
		{
			memcpy(row, top, dst_stride);
			memcpy(top, bottom, dst_stride);
			memcpy(bottom, row, dst_stride);
			top += dst_stride;
			bottom -= dst_stride;
		}
		free(row);
	}
	else
	{
		int j;
		GLubyte *image = malloc(width*height*3*sizeof (*image));
		if (!image) return;
		glReadPixels(x, y, width, height, GL_RGB, GL_UNSIGNED_BYTE, image);
		for (i = height-1; i >= 0; i--)
		{
			for (j = 0; j < width; j++)
			{
				dst_data[(height-1-i)*width+j] =
				(USHORT)(
						 ((image[(i*width+j)*3]>>3)<<11) |
						 ((image[(i*width+j)*3+1]>>2)<<5) |
						 ((image[(i*width+j)*3+2]>>3)));
			}
		}
		free(image);
	}
#endif
}




/*
 jitter.h

 This file contains jitter point arrays for 2,3,4,8,15,24 and 66 jitters.

 The arrays are named j2, j3, etc. Each element in the array has the form,
 for example, j8[0].x and j8[0].y

 Values are floating point in the range -.5 < x < .5, -.5 < y < .5, and
 have a gaussian distribution around the origin.

 Use these to do model jittering for scene anti-aliasing and view volume
 jittering for depth of field effects. Use in conjunction with the
 accwindow() routine.
 */
#ifdef DOF

/* accFrustum()
 * The first 6 arguments are identical to the glFrustum() call.
 *
 * pixdx and pixdy are anti-alias jitter in pixels.
 * Set both equal to 0.0 for no anti-alias jitter.
 * eyedx and eyedy are depth-of field jitter in pixels.
 * Set both equal to 0.0 for no depth of field effects.
 *
 * focus is distance from eye to plane in focus.
 * focus must be greater than, but not equal to 0.0.
 *
 * Note that accFrustum() calls glTranslatef().  You will
 * probably want to insure that your ModelView matrix has been
 * initialized to identity before calling accFrustum().
 */
#if 0
static void accFrustum(GLfloat left, GLfloat right, GLfloat bottom,
					   GLfloat top, GLfloat nearz, GLfloat farz,
					   GLfloat eyedx, GLfloat eyedy, GLfloat focus)
{
	GLfloat dx, dy;
	GLint viewport[4];

	glGetIntegerv(GL_VIEWPORT, viewport);

	// Yup, it's not a typo, why 0? Don't know :P
	dx = -(0/(GLfloat) viewport[2] + eyedx*nearz/focus);
	dy = -(0/(GLfloat) viewport[3] + eyedy*nearz/focus);

	glFrustum(left + dx, right + dx, bottom + dy, top + dy, nearz, farz);
	glTranslatef(-eyedx, -eyedy, 0.0);
}
#endif

/*
 In order to do off screen calculation, the game can use a more extensible glFrustum call,
 A math conversion needs to be done for a gluPerspective to glFrustum call

 There is no difference in performance between gluPerspective and glFrustum

 The field of view (fov) of a glFrustum() call is:
 fov*0.5 = atan((top-bottom)*0.5 / near)

 Since bottom == -top for the symmetrical projection that gluPerspective() produces, then:
 top = tan(fov*0.5) * near
 bottom = -top

 Note: fov must be in radians for the above formula to work with the C math library.
 If the fov was computed in degrees (as in the call to gluPerspective()), then top is calculated as follows:
 top = tan(fov*M_PI/360.0) * near

 The left and right parameters are simply functions of the top, bottom, and aspect:
 left = aspect * bottom
 right = aspect * top
 */
static void GL_FrustumPerspective(GLfloat fovy, GLfloat aspect, GLfloat zNear, GLfloat zFar)
{
	GLfloat xmin, xmax, ymin, ymax;
	ymax = zNear * tan(fovy * 0.008727f); // (M_PI / 360.0f), 3.14159/360.0
	ymin = -ymax;
	xmin = ymin * aspect;
	xmax = ymax * aspect;
	glFrustum(xmin, xmax, ymin, ymax, zNear, zFar);
	//accFrustum(xmin, xmax, ymin, ymax, zNear, zFar, 1, -1, 1);
	// eyedx, eyedy, focus
}
#endif

// -----------------+
// GClipRect        : Defines the 2D hardware clipping window, called multiple times in game, just about every frame
//
// This mainly affects the menu and things not drawn in the 3D world, things in the 3D world are affected by GL_Transform
// -----------------+
void GL_GClipRect(int minx, int miny, int maxx, int maxy, float nearclip)
{
	glViewport(minx, screen_height-maxy, maxx-minx, maxy-miny);
	NEAR_CLIPPING_PLANE = nearclip;

	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
#ifdef DOF
	GL_FrustumPerspective(fov, ASPECT_RATIO, NEAR_CLIPPING_PLANE, FAR_CLIPPING_PLANE);
#else
	gluPerspective(fov, ASPECT_RATIO, NEAR_CLIPPING_PLANE, FAR_CLIPPING_PLANE); // SRB2CBTODO: Must change to frustum for DepthField
#endif
	glMatrixMode(GL_MODELVIEW);

#ifndef MINI_GL_COMPATIBILITY
	glGetIntegerv(GL_VIEWPORT, viewport);
	glGetDoublev(GL_PROJECTION_MATRIX, projMatrix);
#endif
}


// -----------------+
// ClearBuffer      : Clear the color, alpha, and/or depth buffers
// -----------------+
void GL_ClearBuffer(FBOOLEAN ColorMask,
					FBOOLEAN DepthMask,
					FRGBAFloat * ClearColor)
{
	FUINT ClearMask = 0;

	if (ColorMask)
	{
		if (ClearColor)
			glClearColor(ClearColor->red,
						 ClearColor->green,
						 ClearColor->blue,
						 ClearColor->alpha);
		ClearMask |= GL_COLOR_BUFFER_BIT;
	}
	if (DepthMask)
		ClearMask |= GL_DEPTH_BUFFER_BIT;

	GL_SetBlend(DepthMask ? PF_Occlude | CurrentPolyFlags : CurrentPolyFlags & ~PF_Occlude);

	glClear(ClearMask);
}


// -----------------+
// Draw2DLine: Render a 2D line
// -----------------+
void GL_Draw2DLine(F2DCoord * v1,
				   F2DCoord * v2,
				   RGBA_t Color)
{
	GLRGBAFloat c;

#ifdef MINI_GL_COMPATIBILITY
	GLfloat x1, x2, x3, x4;
	GLfloat y1, y2, y3, y4;
	GLfloat dx, dy;
	GLfloat angle;
#endif

	glDisable(GL_TEXTURE_2D);

	c.red   = byte2float[Color.s.red];
	c.green = byte2float[Color.s.green];
	c.blue  = byte2float[Color.s.blue];
	c.alpha = byte2float[Color.s.alpha];

#ifndef MINI_GL_COMPATIBILITY
	glColor4fv(&c.red);    // is in RGBA float format
	glBegin(GL_LINES);
	glVertex3f(v1->x, -v1->y, 1.0f);
	glVertex3f(v2->x, -v2->y, 1.0f);
	glEnd();
#else
	if (v2->x != v1->x)
		angle = (float)atan((v2->y-v1->y)/(v2->x-v1->x));
	else
		angle = N_PI_DEMI;
	dx = (float)sin(angle) / (float)screen_width;
	dy = (float)cos(angle) / (float)screen_height;

	x1 = v1->x - dx;  y1 = v1->y + dy;
	x2 = v2->x - dx;  y2 = v2->y + dy;
	x3 = v2->x + dx;  y3 = v2->y - dy;
	x4 = v1->x + dx;  y4 = v1->y - dy;

	glColor4f(c.red, c.green, c.blue, c.alpha);
	glBegin(GL_TRIANGLE_FAN);
	glVertex3f(x1, -y1, 1);
	glVertex3f(x2, -y2, 1);
	glVertex3f(x3, -y3, 1);
	glVertex3f(x4, -y4, 1);
	glEnd();
#endif

	glEnable(GL_TEXTURE_2D);
}


// -----------------+
// GL_SetBlend         : Set render mode
// -----------------+
// PF_Masked - we could use an ALPHA_TEST of GL_EQUAL, and alpha ref of 0,
//             is it faster when pixels are discarded ?
void GL_SetBlend(FBITFIELD PolyFlags)
{
	FBITFIELD Xor;
	Xor = CurrentPolyFlags^PolyFlags;
	if (Xor & (PF_Blending|PF_RemoveYWrap|PF_ForceWrapX|PF_ForceWrapY|PF_Occlude|PF_NoTexture|PF_Modulated|PF_NoDepthTest|PF_Decal|PF_Invisible|PF_NoAlphaTest))
	{
		if (Xor&(PF_Blending)) // if blending mode must be changed
		{
			switch (PolyFlags & PF_Blending) {
				case PF_Translucent & PF_Blending:
					glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); // alpha = level of transparency
					break;
				case PF_Masked & PF_Blending:
					glBlendFunc(GL_SRC_ALPHA, GL_ZERO);                // 0 alpha = holes in texture
					break;
				case PF_Additive & PF_Blending:
#ifdef ATI_RAGE_PRO_COMPATIBILITY
					glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); // alpha = level of transparency
#else
					glBlendFunc(GL_SRC_ALPHA, GL_ONE);                 // src * alpha + dest
#endif
					break;
				case PF_Environment & PF_Blending:
					glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
					break;
				case PF_Substractive & PF_Blending:
					// good for shadow
					// not realy but what else ?
					glBlendFunc(GL_ZERO, GL_ONE_MINUS_SRC_COLOR);
					break;
				default : // must be 0, otherwise it's an error
					// No blending
					glBlendFunc(GL_ONE, GL_ZERO);   // the same as no blending
					break;
			}
		}
#ifndef KOS_GL_COMPATIBILITY
		if (Xor & PF_NoAlphaTest)
		{
			if (PolyFlags & PF_NoAlphaTest)
				glDisable(GL_ALPHA_TEST);
			else
				glEnable(GL_ALPHA_TEST);      // discard 0 alpha pixels (holes in texture)
		}

		if (Xor & PF_Decal)
		{
			if (PolyFlags & PF_Decal)
				glEnable(GL_POLYGON_OFFSET_FILL);
			else
				glDisable(GL_POLYGON_OFFSET_FILL);
		}
#endif
		if (Xor & PF_NoDepthTest)
		{
			if (PolyFlags & PF_NoDepthTest)
				glDepthFunc(GL_ALWAYS); //glDisable(GL_DEPTH_TEST);
			else
				glDepthFunc(GL_LEQUAL); //glEnable(GL_DEPTH_TEST);
		}

		if (Xor & PF_RemoveYWrap)
		{
			if (PolyFlags & PF_RemoveYWrap)
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
		}

		if (Xor & PF_ForceWrapX)
		{
			if (PolyFlags & PF_ForceWrapX)
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
		}

		if (Xor & PF_ForceWrapY)
		{
			if (PolyFlags & PF_ForceWrapY)
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
		}

#ifdef KOS_GL_COMPATIBILITY
		if (Xor & PF_Modulated && !(PolyFlags & PF_Modulated))
			glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
#else
		if (Xor & PF_Modulated)
		{
#if defined (unix) || defined (UNIXLIKE)
			if (oglflags & GLF_NOTEXENV)
			{
				if (!(PolyFlags & PF_Modulated))
					glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
			}
			else
#endif
				if (PolyFlags & PF_Modulated)
				{   // Mix the texture's color with Surface->FlatColor
					glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
				}
				else
				{   // Texture's color is unchanged before blending
					glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
				}
		}
#endif

		if (Xor & PF_Occlude) // depth test but (no) depth write
		{
			if (PolyFlags & PF_Occlude)
				glDepthMask(1);
			else
				glDepthMask(0);
		}
		// Used for polygons that clip drawing anything behind it (useful for sky)
		// This is helpful for polygons like sky textured map geometry,
		// which should clip everything behind it but the sky
		if (Xor & PF_Invisible)
		{
			if (PolyFlags & PF_Invisible)
				glBlendFunc(GL_ZERO, GL_ONE);         // Transparent blending
			else if ((PolyFlags & PF_Blending) == PF_Masked)
				glBlendFunc(GL_SRC_ALPHA, GL_ZERO);
		}

		if (PolyFlags & PF_NoTexture)
			SetNoTexture();

	}
	CurrentPolyFlags = PolyFlags;
}


// -----------------+
// SetTexture       : The texture parameter becomes the current texture source
// -----------------+
void GL_SetTexture(FTextureInfo *pTexInfo, boolean anisotropic)
{
	if (!pTexInfo)
	{
		SetNoTexture();
		return;
	}
	else if (pTexInfo->downloaded)
	{
		if (pTexInfo->downloaded != tex_downloaded)
		{
			glBindTexture(GL_TEXTURE_2D, pTexInfo->downloaded);
			tex_downloaded = pTexInfo->downloaded;
		}
	}
	else
	{
		// Download a mipmap
#ifdef KOS_GL_COMPATIBILITY
		static GLushort tex[2048*2048];
#else
		static RGBA_t   tex[2048*2048];
#endif
		const GLvoid   *ptex = tex;
		int             w, h;

		w = pTexInfo->width;
		h = pTexInfo->height;


		if ((pTexInfo->glInfo.format == GR_TEXFMT_P_8) ||
			(pTexInfo->glInfo.format == GR_TEXFMT_AP_88))
		{
			const GLubyte *pImgData = (const GLubyte *)pTexInfo->glInfo.data;
			int i, j;

			for (j = 0; j < h; j++)
			{
				for (i = 0; i < w; i++)
				{
					if ((*pImgData == HWR_PATCHES_CHROMAKEY_COLORINDEX) &&
					    (pTexInfo->flags & TF_CHROMAKEYED))
					{
						tex[w*j+i].s.red   = 0;
						tex[w*j+i].s.green = 0;
						tex[w*j+i].s.blue  = 0;
						tex[w*j+i].s.alpha = 0;
					}
					else
					{
						tex[w*j+i].s.red   = myPaletteData[*pImgData].s.red;
						tex[w*j+i].s.green = myPaletteData[*pImgData].s.green;
						tex[w*j+i].s.blue  = myPaletteData[*pImgData].s.blue;
						tex[w*j+i].s.alpha = myPaletteData[*pImgData].s.alpha;
					}

					pImgData++;

					if (pTexInfo->glInfo.format == GR_TEXFMT_AP_88)
					{
						if (!(pTexInfo->flags & TF_CHROMAKEYED))
							tex[w*j+i].s.alpha = *pImgData;
						pImgData++;
					}

				}
			}
		}
		else if (pTexInfo->glInfo.format == GR_RGBA)
		{
			// The RGBA is the common format for most of the game's textures
			// and is the format MD2 models use
			ptex = pTexInfo->glInfo.data;
		}
		else if (pTexInfo->glInfo.format == GR_TEXFMT_ALPHA_INTENSITY_88)
		{
			const GLubyte *pImgData = (const GLubyte *)pTexInfo->glInfo.data;
			int i, j;

			for (j = 0; j < h; j++)
			{
				for (i = 0; i < w; i++)
				{
					tex[w*j+i].s.red   = *pImgData;
					tex[w*j+i].s.green = *pImgData;
					tex[w*j+i].s.blue  = *pImgData;
					pImgData++;
					tex[w*j+i].s.alpha = *pImgData;
					pImgData++;
				}
			}
		}
		else
			CONS_LogPrintf("OpenGL Error: GL_SetTexture(bad format) %ld\n", pTexInfo->glInfo.format);

		pTexInfo->downloaded = NextTexAvail++;
		tex_downloaded = pTexInfo->downloaded;
		glBindTexture(GL_TEXTURE_2D, pTexInfo->downloaded);


		if (!anisotropic && (min_filter & MIPMAP_MASK)
			&& !(pTexInfo->flags & TF_NOFILTER))
			gluBuild2DMipmaps(GL_TEXTURE_2D, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, ptex);
		else
			glTexImage2D(GL_TEXTURE_2D, 0, 4, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, ptex);


			if (pTexInfo->glInfo.format == GR_TEXFMT_ALPHA_INTENSITY_88)
			{
				if ((min_filter & MIPMAP_MASK)
					&& !(pTexInfo->flags & TF_NOFILTER))
					gluBuild2DMipmaps(GL_TEXTURE_2D, GL_LUMINANCE_ALPHA, w, h, GL_RGBA, GL_UNSIGNED_BYTE, ptex);
				else
					glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE_ALPHA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, ptex);
			}
			else
			{
				if ((min_filter & MIPMAP_MASK))
				{
					gluBuild2DMipmaps(GL_TEXTURE_2D, textureformatGL, w, h, GL_RGBA, GL_UNSIGNED_BYTE, ptex);
					// Texture level of detail setting, not needed because of anisotropy, but just here for example
					// The lower the number, the higer the detail of the texture
					if (!anisotropic || (pTexInfo->flags & TF_NOFILTER))
					{
                        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_LOD, 0);
                        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LOD, 0);
					}
				}
				else
					glTexImage2D(GL_TEXTURE_2D, 0, textureformatGL, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, ptex);
			}


		if (pTexInfo->flags & TF_WRAPX)
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
		else
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);

		if (pTexInfo->flags & TF_WRAPY)
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
		else
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);

		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, mag_filter);
		if (pTexInfo->flags & TF_NOFILTER) // Textures with nofilter flags shouldn't have a blur on them
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		else
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, min_filter);

		// Check if the user has the anisotropic option on
		// Do NOT use anisotropy on things with TF_NOFILTER (could cause really ugly artifacts)
		if ((anisotropy) && (!(pTexInfo->flags & TF_NOFILTER)))
		{
			// The call to GL_SetTexture has requested anisotropic filtering?
			if (anisotropic)
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, anisotropic_filter);
		}

		pTexInfo->nextmipmap = NULL;
		if (gr_cachetail) // Instert end of list
		{
			gr_cachetail->nextmipmap = pTexInfo;
			gr_cachetail = pTexInfo;
		}
		else // Initialization list
			gr_cachetail = gr_cachehead =  pTexInfo;
	}
#ifdef MINI_GL_COMPATIBILITY
	switch (pTexInfo->flags)
	{
		case 0 :
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
			break;
		default:
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
			break;
	}
#endif
}

// -----------------+
// GL_DrawPolygon      : Render a polygon, set the texture, set render mode
// -----------------+
void GL_DrawPolygon(FSurfaceInfo *pSurf, FOutVector *pOutVerts, FUINT iNumPts, FBITFIELD PolyFlags, FBITFIELD PolyFlags2)
{
	FUINT i;
	GLRGBAFloat c = {0,0,0,0};

#ifdef MINI_GL_COMPATIBILITY
	if (PolyFlags & PF_Corona)
		PolyFlags &= ~PF_NoDepthTest;
#else
	if ((PolyFlags & PF_Corona) && (oglflags & GLF_NOZBUFREAD))
		PolyFlags &= ~(PF_NoDepthTest|PF_Corona);
#endif

	GL_SetBlend(PolyFlags);

	// If Modulated, mix the surface colors with the texture
	if ((CurrentPolyFlags & PF_Modulated) && pSurf)
	{
		// hack for non-palettized mode
		if (pal_col)
		{
			c.red   = (const_pal_col.red  +byte2float[pSurf->FlatColor.s.red])  /2.0f;
			c.green = (const_pal_col.green+byte2float[pSurf->FlatColor.s.green])/2.0f;
			c.blue  = (const_pal_col.blue +byte2float[pSurf->FlatColor.s.blue]) /2.0f;
			c.alpha = byte2float[pSurf->FlatColor.s.alpha];
		}
		else
		{
			c.red   = byte2float[pSurf->FlatColor.s.red];
			c.green = byte2float[pSurf->FlatColor.s.green];
			c.blue  = byte2float[pSurf->FlatColor.s.blue];
			c.alpha = byte2float[pSurf->FlatColor.s.alpha];
		}

#ifdef MINI_GL_COMPATIBILITY
		glColor4f(c.red, c.green, c.blue, c.alpha);
		glColor4f(c.red, c.green, c.blue, c.alpha);
#else
		glColor4fv(&c.red);    // is in RGBA float format
#endif
	}

	// MD2 models cannot use the iNumPts functions,
	// their verticies are converted elsewhere
	if (PolyFlags & PF_MD2)
		return;

	// SRB2CBTODO: PolyFlags2 is for any extra rendering stuff,
	// turn this into a special struct of data instead for even more flexibility?

	// If there's a back or front face cull, turn on culling
	if ((PolyFlags2 & PF2_CullFront) || (PolyFlags2 & PF2_CullBack))
	{
		glEnable(GL_CULL_FACE);

		if (PolyFlags2 & PF2_CullFront)
			glCullFace(GL_FRONT);
		else if (PolyFlags2 & PF2_CullBack)
			glCullFace(GL_BACK);
		else if ((PolyFlags2 & PF2_CullFront) || (PolyFlags2 & PF2_CullBack))
			glCullFace(GL_FRONT_AND_BACK);
	}

	// SRB2CBTODO: Now here's something special with OpenGL,
	// the game is doing a transform of just the texture coordinates for this polygon only
	// Now that this is set in place, textures can be transformed dynamically,
	// this opens up many posibilties for graphics now

	// This goes for all texture transformations: Set the matrix mode to GL_TEXTURE
	// then Pop and Push the matrix for this polygon so that we only modify THIS polygon
	// GL_TEXTURE can also be changed to GL_MODELVIEW for translating the actual vertexes
	if ((PolyFlags & PF_TexRotate) || (PolyFlags & PF_TexScale) || (PolyFlags & PF_Rotate))
	{
		if ((PolyFlags & PF_TexRotate) || (PolyFlags & PF_TexScale))
			glMatrixMode(GL_TEXTURE);
		glPushMatrix();
	}

	/// Transformations start here ///

	// SRB2CBTODO: Here is the code that allows for custom transformations,
	// the transforming is called here, before the polygon is actually drawn
	if ((PolyFlags & PF_TexRotate) || (PolyFlags & PF_Rotate) || (PolyFlags & PF_TexScale))
	{
		// SRB2CBTODO: Rotate around center
		// SRB2CBTODO: glScalef(1.0f, 1.0f, 1.0f); etc.
		glTranslatef(1.0f, 1.0f, 0.0f);

		if (PolyFlags & PF_Rotate)
			glRotatef(pSurf->PolyRotate, 0.0f, 0.0f, 1.0f);  // SRB2CBTODO: Options for which axis
		if (PolyFlags & PF_TexRotate)
			glRotatef(pSurf->TexRotate, 0.0f, 0.0f, 1.0f); // SRB2CBTODO: It only syncs to a floor plane SOMETIMES, WHY??!!!
		if (PolyFlags & PF_TexScale)
			glScalef(pSurf->TexScale, pSurf->TexScale, 0.0f); // SRB2CBTODO: Scale for each coord?

		glTranslatef(-1.0f, -1.0f, 0.0f);
	}

	//glPolygonMode( GL_FRONT_AND_BACK, GL_LINE); // ZTODO: Important line mode, make sure to clear after updates

	glBegin(GL_TRIANGLE_FAN);

	for (i = 0; i < iNumPts; i++)
	{
		glTexCoord2f(pOutVerts[i].sow, pOutVerts[i].tow);

		// Test code: -pOutVerts[i].z => pOutVerts[i].z
		glVertex3f(pOutVerts[i].x, pOutVerts[i].y, pOutVerts[i].z);
	}

	glEnd();


	// SRB2CBTODO: This goes for all transformations:
	// After using a modifying call such as translating and rotating,
	// We must Pop and Push the matrix so only the current polygon is affected
	if ((PolyFlags & PF_Rotate) || (PolyFlags & PF_TexRotate))
	{
		glPopMatrix();
		// Set the matrix mode back to normal: GL_MODELVIEW,
		// if the texture has been modified
		if (PolyFlags & PF_TexRotate)
			glMatrixMode(GL_MODELVIEW);
	}

	if (PolyFlags & PF_RemoveYWrap)
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

	if (PolyFlags & PF_ForceWrapX)
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);

	if (PolyFlags & PF_ForceWrapY)
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);

	// Disable face culling if it was used
	if ((PolyFlags2 & PF2_CullFront) || (PolyFlags2 & PF2_CullBack))
		glDisable(GL_CULL_FACE);

	glpolycount++;
}


// ==========================================================================
//
// ==========================================================================
void GL_SetSpecialState(hwdspecialstate_t IdState, int Value)
{
	switch (IdState)
	{
			// KALARON: Why was this here?
			/*case 77:
			 {
			 if (!Value)
			 GL_ClearBuffer(false, true, 0); // Clear the depth buffer
			 break;
			 }*/

		case HWD_SET_PALETTECOLOR:
		{
			pal_col = Value;
			const_pal_col.blue  = byte2float[((Value>>16)&0xff)];
			const_pal_col.green = byte2float[((Value>>8)&0xff)];
			const_pal_col.red   = byte2float[((Value)&0xff)];
			break;
		}

		case HWD_SET_FOG_COLOR:
		{
			GLfloat fogcolor[4];

			fogcolor[0] = byte2float[((Value>>16)&0xff)];
			fogcolor[1] = byte2float[((Value>>8)&0xff)];
			fogcolor[2] = byte2float[((Value)&0xff)];
			fogcolor[3] = 0x0;
			glFogfv(GL_FOG_COLOR, fogcolor);
			break;
		}
		case HWD_SET_FOG_DENSITY:
			glFogf(GL_FOG_DENSITY, Value*1200/(500*1000000.0f));
			break;

		case HWD_SET_FOG_START:
			glFogf(GL_FOG_START, Value*1200/(500*1000000.0f));
			break;

		case HWD_SET_FOG_END:
			glFogf(GL_FOG_END, Value*1200/(500*1000000.0f));
			break;

		case HWD_SET_FOG_MODE:
			if (Value)
			{
				glEnable(GL_FOG);

				// None of these really work :<
				// (can't OpenGL magically paint each polygon?)
				//glFogi(GL_FOG_MODE, GL_EXP);
				//glFogi(GL_FOG_MODE, GL_EXP2);
				//glFogi(GL_FOG_MODE, GL_LINEAR);
				//glFogf(GL_FOG_START, 100000);
				//glFogf(GL_FOG_END, 300000);
				//glFogf(GL_FOG_COORD_SRC, GL_FRAGMENT_DEPTH);
			}
			else
				glDisable(GL_FOG);
			break;

		case HWD_SET_TEXTUREFILTERMODE:
			switch (Value)
		{
#ifdef KOS_GL_COMPATIBILITY
			case HWD_SET_TEXTUREFILTER_POINTSAMPLED:
				min_filter = mag_filter = GL_NEAREST;
				break;
			case HWD_SET_TEXTUREFILTER_MIXED1:
			case HWD_SET_TEXTUREFILTER_MIXED2:
				min_filter = mag_filter = GL_FILTER_NONE;
				break;
#else
			case HWD_SET_TEXTUREFILTER_POINTSAMPLED: // Nearest
				min_filter = mag_filter = GL_NEAREST;
				break;

			case HWD_SET_TEXTUREFILTER_MIXED1: // Nearest mippmapped
				mag_filter = GL_NEAREST;
				min_filter = GL_LINEAR_MIPMAP_LINEAR;
				break;
#endif
		}
			GL_Flush(); // Reload all textures with the new filter
			break;

		case HWD_SET_TEXTUREANISOTROPICMODE:
			anisotropic_filter = min(Value,anisotropy);
			if (anisotropy)
				GL_Flush(); // Reload all textures with the new filter
			break;

		default:
			break;
	}
}




void GL_BuildMD2Lists(int *gl_cmd_buffer, md2_t* md2)
{
    int     val, count, pindex;
    GLfloat s, t;
	md2_frame_t* frame;
	size_t state;
	int* originalpos = gl_cmd_buffer;

	CONS_Printf("Calculating MD2 shadow matrix for: %s\n", md2->filename);

	//glEnable(GL_NORMALIZE);

	for(state = 0; state < md2->model->header.numFrames; state++)
	{
		frame = &md2->model->frames[state % md2->model->header.numFrames];

		// The index for the display list
		md2->displist[state] = glGenLists(1);

		// Compile the display list, store the MD2's polygons in it
		glNewList(md2->displist[state], GL_COMPILE);

		gl_cmd_buffer = originalpos;

		val = *gl_cmd_buffer++;

		while (val != 0)
		{
			if (val < 0)
			{
				glBegin(GL_TRIANGLE_FAN);
				count = -val;
			}
			else
			{
				glBegin(GL_TRIANGLE_STRIP);
				count = val;
			}
			glpolycount++;

			while (count--)
			{
				s = *(float *) gl_cmd_buffer++;
				t = *(float *) gl_cmd_buffer++;
				pindex = *gl_cmd_buffer++;

				glTexCoord2f(s, t);

				glVertex3f (frame->vertices[pindex].vertex[0],
							frame->vertices[pindex].vertex[1],
							frame->vertices[pindex].vertex[2]);
			}

			glEnd();

			val = *gl_cmd_buffer++;
		}

		glEndList();
	} // first list

#if 0
	for(state = 0; state < md2->model->header.numFrames; state++)
	{
		frame = &md2->model->frames[state % md2->model->header.numFrames];

		md2->shadowlist[state] = glGenLists(1);

		glNewList(md2->shadowlist[state], GL_COMPILE);

		//glDisable(GL_TEXTURE_2D);

		//		glBindTexture(GL_TEXTURE_2D, md2->texture);

		gl_cmd_buffer = originalpos;

		val = *gl_cmd_buffer++;

		while (val != 0)
		{
			if (val < 0)
			{
				glBegin (GL_TRIANGLE_FAN);
				count = -val;
			}
			else
			{
				glBegin (GL_TRIANGLE_STRIP);
				count = val;
			}

			while (count--)
			{
				s = *(float *) gl_cmd_buffer++;
				t = *(float *) gl_cmd_buffer++;
				index = *gl_cmd_buffer++;

				glTexCoord2f (s, t);
				glVertex3f (frame->vertices[index].vertex[0],
							0.0f,
							frame->vertices[index].vertex[2]);
			}

			glEnd ();

			val = *gl_cmd_buffer++;
		}
		//glEnable(GL_TEXTURE_2D);

		glEndList();
	}
#endif

	//glDisable(GL_NORMALIZE);
}



// Fancy vector based shadow math!

enum {
	X, Y, Z, W
};
enum {
	A, B, C, D
};

static GLfloat floorVertices[4][3] = {
	{ -20.0, 0.0, 20.0 },
	{ 20.0, 0.0, 20.0 },
	{ 20.0, 0.0, -20.0 },
	{ -20.0, 0.0, -20.0 },
};

static GLfloat lightPosition[4];
float lightAngle, lightHeight;


/* Create a matrix that will project the desired shadow. */
static void shadowMatrix(GLfloat shadowMat[4][4],
						 GLfloat groundplane[4],
						 GLfloat lightpos[4])
{
	GLfloat dot;

	/* Find dot product between light position vector and ground plane normal. */
	dot = groundplane[X] * lightpos[X] +
    groundplane[Y] * lightpos[Y] +
    groundplane[Z] * lightpos[Z] +
    groundplane[W] * lightpos[W];

	shadowMat[0][0] = dot - lightpos[X] * groundplane[X];
	shadowMat[1][0] = 0.f - lightpos[X] * groundplane[Y];
	shadowMat[2][0] = 0.f - lightpos[X] * groundplane[Z];
	shadowMat[3][0] = 0.f - lightpos[X] * groundplane[W];

	shadowMat[X][1] = 0.f - lightpos[Y] * groundplane[X];
	shadowMat[1][1] = dot - lightpos[Y] * groundplane[Y];
	shadowMat[2][1] = 0.f - lightpos[Y] * groundplane[Z];
	shadowMat[3][1] = 0.f - lightpos[Y] * groundplane[W];

	shadowMat[X][2] = 0.f - lightpos[Z] * groundplane[X];
	shadowMat[1][2] = 0.f - lightpos[Z] * groundplane[Y];
	shadowMat[2][2] = dot - lightpos[Z] * groundplane[Z];
	shadowMat[3][2] = 0.f - lightpos[Z] * groundplane[W];

	shadowMat[X][3] = 0.f - lightpos[W] * groundplane[X];
	shadowMat[1][3] = 0.f - lightpos[W] * groundplane[Y];
	shadowMat[2][3] = 0.f - lightpos[W] * groundplane[Z];
	shadowMat[3][3] = dot - lightpos[W] * groundplane[W];

}

/* Find the plane equation given 3 points. */
static void findPlane(GLfloat plane[4],
					  GLfloat v0[3], GLfloat v1[3], GLfloat v2[3])
{
	GLfloat vec0[3], vec1[3];

	/* Need 2 vectors to find cross product. */
	vec0[X] = v1[X] - v0[X];
	vec0[Y] = v1[Y] - v0[Y];
	vec0[Z] = v1[Z] - v0[Z];

	vec1[X] = v2[X] - v0[X];
	vec1[Y] = v2[Y] - v0[Y];
	vec1[Z] = v2[Z] - v0[Z];

	/* find cross product to get A, B, and C of plane equation */
	plane[A] = vec0[Y] * vec1[Z] - vec0[Z] * vec1[Y];
	plane[B] = -(vec0[X] * vec1[Z] - vec0[Z] * vec1[X]);
	plane[C] = vec0[X] * vec1[Y] - vec0[Y] * vec1[X];

	plane[D] = -(plane[A] * v0[X] + plane[B] * v0[Y] + plane[C] * v0[Z]);
}

static GLfloat lightPosition[4];

// SRB2CBTODO: MATRIX BASED SHADOWS!!!!! WHOOOOOO
// TODO: Make them sync to the floor and all that good stuff
// AND when that's done, MAKE THEM CAST OVER GEOMERTRY!!!!
// AND *THEN* make an OpenGL based unified light and shadow Global illumination system!
 // SRB2CBTODO: Create displaylists for MD2 models to speed things up
void GL_DrawMD2Shadow(int *gl_cmd_buffer, md2_frame_t *frame, ULONG duration, ULONG tics, md2_frame_t *nextframe,
					  FTransform *pos, float scale, fixed_t height, fixed_t light, fixed_t offset, mobj_t *mobj)
{
	int     val, count, pindex;
	GLfloat s, t;

	float pol;
	ULONG newtime;
	
	return;

	if (duration == 0)
		duration = 1;

	newtime = (duration - tics) + 1;

	pol = (newtime)/(float)duration;

	if (pol > 1.0f)
		pol = 1.0f;

	if (pol < 0.0f)
		pol = 0.0f;

	height = light = offset = 0; // SRB2CBTODO: Use this

	glColor4f(0.0f,0.0f,0.0f, 0.15f);

	GL_DrawPolygon(NULL, NULL, 0, PF_Masked|PF_Modulated|PF_Occlude, 0); // A polygon is drawn and then transformed by the code below
	glpolycount++;


	glEnable(GL_POLYGON_OFFSET_FILL);
	glPolygonOffset(-2.0, -1.0);
	glLineWidth(3.0);

	glPushMatrix(); // should be the same as glLoadIdentity

	glTranslatef(pos->x, FIXED_TO_FLOAT(mobj->floorz), pos->y);
	// Yaw, Roll, and Pitch (in that order too) // SRB2CBTODO: Align MD2 shadows to a plane!!!
	glRotatef(0, 0.0f, -1.0f, 0.0f); // Roll
	glRotatef(0, -1.0f, 0.0f, 0.0f); // Yaw
	glRotatef(0, 0.0f, 0.0f, -1.0f); // Pitch (glrollangle is pitch angle for MD2's)

	glScalef(scale, 1, scale);
	glColor4f(0.0f,0.0f,0.0f, 0.15f); // SRB2CBTODO: Make this one solid color and make the multiple polygons not show through
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); // alpha = level of transparency
	glDepthMask(GL_FALSE);

	lightAngle = 0;//pos->angley/(ANG45/45);//44;//viewangle/(ANG45/45)/36.0f;

	lightHeight = 100; // The height of the light for shadows, make it high for now, SRB2CBTODO: Cascading shadows on everything!!!

	GLfloat floorPlane[4];
	GLfloat floorShadow[4][4];

	/* Reposition the light source. */
	lightPosition[0] = 12*cos(lightAngle)+10;
	lightPosition[1] = lightHeight;
	lightPosition[2] = 12*sin(lightAngle)-80;
	lightPosition[3] = 1.0;


	/* Setup floor plane for projected shadow calculations. */
	findPlane(floorPlane, floorVertices[1], floorVertices[2], floorVertices[3]);

	shadowMatrix(floorShadow, floorPlane, lightPosition);
	//glRotatef(pos->angley, 0.0f, -1.0f, 0.0f); // Roll
	glMultMatrixf((GLfloat *) floorShadow);

	// Now we're modifying how the shadow looks, not the actual coords of the shadow polygon
	glRotatef(pos->angley, 0.0f, -1.0f, 0.0f); // Roll
	glRotatef(pos->anglex, -1.0f, 0.0f, 0.0f); // Yaw
	glRotatef(pos->glrollangle, 0.0f, 0.0f, -1.0f); // Pitch (glrollangle is pitch angle for MD2's)


	val = *gl_cmd_buffer++;

	while (val != 0)
	{
		if (val < 0)
		{
			glBegin(GL_TRIANGLE_FAN);
			count = -val;
		}
		else
		{
			glBegin(GL_TRIANGLE_STRIP);
			count = val;
		}
		glpolycount++;

		while (count--)
		{
			s = *(float *) gl_cmd_buffer++;
			t = *(float *) gl_cmd_buffer++;
			pindex = *gl_cmd_buffer++;

			glTexCoord2f(s, t);

			if (!nextframe)
			{
				glNormal3f(frame->vertices[pindex].normal[0],
						   frame->vertices[pindex].normal[1],
						   frame->vertices[pindex].normal[2]);

				glVertex3f(frame->vertices[pindex].vertex[0]*0.5f,
						   frame->vertices[pindex].vertex[1]*0.5,
						   frame->vertices[pindex].vertex[2]*0.5f);
			}
			else
			{
				// Interpolate
				float px1 = frame->vertices[pindex].vertex[0]*0.5f;
				float px2 = nextframe->vertices[pindex].vertex[0]*0.5f;
				float py1 = frame->vertices[pindex].vertex[1]*0.5f;
				float py2 = nextframe->vertices[pindex].vertex[1]*0.5f;
				float pz1 = frame->vertices[pindex].vertex[2]*0.5;
				float pz2 = nextframe->vertices[pindex].vertex[2]*0.5f;
				float nx1 = frame->vertices[pindex].normal[0];
				float nx2 = nextframe->vertices[pindex].normal[0];
				float ny1 = frame->vertices[pindex].normal[1];
				float ny2 = nextframe->vertices[pindex].normal[1];
				float nz1 = frame->vertices[pindex].normal[2];
				float nz2 = nextframe->vertices[pindex].normal[2];

				glNormal3f((nx1 + pol * (nx2 - nx1)),
						   (ny1 + pol * (ny2 - ny1)),
						   (nz1 + pol * (nz2 - nz1)));
				glVertex3f((px1 + pol * (px2 - px1)),
						   (py1 + pol * (py2 - py1)),
						   (pz1 + pol * (pz2 - pz1)));
			}
		}

		glEnd();

		val = *gl_cmd_buffer++;
	}

	// The depth is changed back and forth when a model is transparent
	//if (color[3] < 255)
	glDepthMask(GL_TRUE);

	glPopMatrix(); // should be the same as glLoadIdentity


	glDisable(GL_POLYGON_OFFSET_FILL);




}




#define GLLIGHT // SRB2CBTODO: Global illumination of light everywhere!
// SRB2CBTODO: This can be modified for relative color
static float LightDiffuse[] = {1.0f, 1.0f, 1.0f, 1.0f};

// -----------------+
// GL_DrawMD2   : Draw an MD2 model with OpenGL commands
// -----------------+
void GL_DrawMD2(int *gl_cmd_buffer, md2_frame_t *frame, ULONG duration, ULONG tics, md2_frame_t *nextframe, FTransform *pos, float scale, byte *color)
{
	int     val, count, pindex;
	GLfloat s, t;

	float pol;
	ULONG newtime;

	if (color[3] < 1)
		return;

	if (duration == 0)
		duration = 1;

	newtime = (duration - tics) + 1;

	pol = (newtime)/(float)duration;

	if (pol > 1.0f)
		pol = 1.0f;

	if (pol < 0.0f)
		pol = 0.0f;

#ifdef GLLIGHT
	GLfloat ambient[4];
	GLfloat diffuse[4];

	ambient[0] = (color[0]/255.0f);
	ambient[1] = (color[1]/255.0f);
	ambient[2] = (color[2]/255.0f);
	ambient[3] = (color[3]/255.0f);
	diffuse[0] = (color[0]/255.0f);
	diffuse[1] = (color[1]/255.0f);
	diffuse[2] = (color[2]/255.0f);
	diffuse[3] = (color[3]/255.0f);

	if (ambient[0] > 0.75f)
		ambient[0] = 0.75f;
	if (ambient[1] > 0.75f)
		ambient[1] = 0.75f;
	if (ambient[2] > 0.75f)
		ambient[2] = 0.75f;
#endif

#ifdef GLLIGHT
	// Lighting for models, light 0 should also be enabled
	glEnable(GL_LIGHTING);
	glLightModelfv(GL_LIGHT_MODEL_AMBIENT, LightDiffuse);
	glEnable (GL_LIGHT0);
#endif

	GL_DrawPolygon(NULL, NULL, 0, PF_Masked|PF_Modulated|PF_Occlude, 0); // A polygon is drawn and then transformed by the code below
	glpolycount++;

	glEnable(GL_CULL_FACE);

	glCullFace(GL_BACK);

#ifdef GLLIGHT
	glShadeModel(GL_SMOOTH);

	glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT, ambient);
	glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, diffuse);
#endif

	glPushMatrix(); // should be the same as glLoadIdentity
	glTranslatef(pos->x, pos->z, pos->y);
	// Yaw, Roll, and Pitch (in that order too)
	glRotatef(pos->angley, 0.0f, -1.0f, 0.0f); // Roll
	glRotatef(pos->anglex, -1.0f, 0.0f, 0.0f); // Yaw
	glRotatef(pos->glrollangle, 0.0f, 0.0f, -1.0f); // Pitch (glrollangle is pitch angle for MD2's)

	glScalef(scale, scale, scale);

	// Remove depth mask when the model is transparent so it doesn't cut thorugh sprites // SRB2CBTODO: For all stuff too?!
	if (color[3] < 255)
	{
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); // alpha = level of transparency
		glDepthMask(GL_FALSE);
	}

	val = *gl_cmd_buffer++;

	while (val != 0)
	{
		if (val < 0)
		{
			glBegin(GL_TRIANGLE_FAN);
			count = -val;
		}
		else
		{
			glBegin(GL_TRIANGLE_STRIP);
			count = val;
		}
		glpolycount++;

		while (count--)
		{
			s = *(float *) gl_cmd_buffer++;
			t = *(float *) gl_cmd_buffer++;
			pindex = *gl_cmd_buffer++;

			glTexCoord2f(s, t);

			if (!nextframe)
			{
				glNormal3f(frame->vertices[pindex].normal[0],
						   frame->vertices[pindex].normal[1],
						   frame->vertices[pindex].normal[2]);

				glVertex3f(frame->vertices[pindex].vertex[0]*0.5f,
						   frame->vertices[pindex].vertex[1]*0.5,
						   frame->vertices[pindex].vertex[2]*0.5f);
			}
			else
			{
				// Interpolate
				float px1 = frame->vertices[pindex].vertex[0]*0.5f;
				float px2 = nextframe->vertices[pindex].vertex[0]*0.5f;
				float py1 = frame->vertices[pindex].vertex[1]*0.5f;
				float py2 = nextframe->vertices[pindex].vertex[1]*0.5f;
				float pz1 = frame->vertices[pindex].vertex[2]*0.5;
				float pz2 = nextframe->vertices[pindex].vertex[2]*0.5f;
				float nx1 = frame->vertices[pindex].normal[0];
				float nx2 = nextframe->vertices[pindex].normal[0];
				float ny1 = frame->vertices[pindex].normal[1];
				float ny2 = nextframe->vertices[pindex].normal[1];
				float nz1 = frame->vertices[pindex].normal[2];
				float nz2 = nextframe->vertices[pindex].normal[2];

				glNormal3f((nx1 + pol * (nx2 - nx1)),
						   (ny1 + pol * (ny2 - ny1)),
						   (nz1 + pol * (nz2 - nz1)));
				glVertex3f((px1 + pol * (px2 - px1)),
						   (py1 + pol * (py2 - py1)),
						   (pz1 + pol * (pz2 - pz1)));
			}
		}

		glEnd();

		val = *gl_cmd_buffer++;
	}
	glPopMatrix(); // should be the same as glLoadIdentity
	glDisable(GL_CULL_FACE);
#ifdef GLLIGHT
	glDisable(GL_LIGHT0);
	glDisable(GL_LIGHTING);
	glShadeModel(GL_FLAT);
#endif

	// The depth is changed back and forth when a model is transparent
	if (color[3] < 255)
		glDepthMask(GL_TRUE);
}

// -----------------+
// GL_SetTransform   :
// Sets the current view of the 3D world
// -----------------+
void GL_SetTransform(FTransform *stransform)
{
	static boolean special_splitscreen;

	glLoadIdentity();

	if (stransform)
	{
		// Keep a trace of the transformation for MD2
		memcpy(&md2_transform, stransform, sizeof (md2_transform));

		glScalef(stransform->scalex, stransform->scaley, -stransform->scalez);
		// VPHYSICS: Use extensively
		glRotatef(stransform->glrollangle,     0.0f, 0.0f, 1.0f); // Pitch
		glRotatef(stransform->anglex,        1.0f, 0.0f, 0.0f); // Yaw
		glRotatef(stransform->angley+270.0f, 0.0f, 1.0f, 0.0f); // Roll
		glTranslatef(-stransform->x, -stransform->z, -stransform->y);

		glMatrixMode(GL_PROJECTION);
		glLoadIdentity();
		special_splitscreen = (stransform->splitscreen && stransform->fovxangle == 90.0f);
		// SRB2CBTODO: Make this use a relative atan to the real fovangle
		if (special_splitscreen)
#ifdef DOF
			GL_FrustumPerspective(53.13f, 2*ASPECT_RATIO,  // 53.13 = 2*atan(0.5), predefining it is faster
								  NEAR_CLIPPING_PLANE, FAR_CLIPPING_PLANE);
#else
		gluPerspective(53.13f, 2*ASPECT_RATIO,  // 53.13 = 2*atan(0.5), predefining it is faster
					   NEAR_CLIPPING_PLANE, FAR_CLIPPING_PLANE);
#endif
		else
#ifdef DOF
			GL_FrustumPerspective(stransform->fovxangle, ASPECT_RATIO, NEAR_CLIPPING_PLANE, FAR_CLIPPING_PLANE);
#else
		gluPerspective(stransform->fovxangle, ASPECT_RATIO, NEAR_CLIPPING_PLANE, FAR_CLIPPING_PLANE);
#endif

#ifndef MINI_GL_COMPATIBILITY
		glGetDoublev(GL_PROJECTION_MATRIX, projMatrix); // added for new coronas' code (without depth buffer)
#endif

		glMatrixMode(GL_MODELVIEW);
	}
	else
	{
		glScalef(1.0f, 1.0f, -1.0f);

		glMatrixMode(GL_PROJECTION);
		glLoadIdentity();
		if (special_splitscreen)
#ifdef DOF
			GL_FrustumPerspective(53.13f, 2*ASPECT_RATIO,  // 53.13 = 2*atan(0.5)
								  NEAR_CLIPPING_PLANE, FAR_CLIPPING_PLANE);
#else
		gluPerspective(53.13f, 2*ASPECT_RATIO,  // 53.13 = 2*atan(0.5)
					   NEAR_CLIPPING_PLANE, FAR_CLIPPING_PLANE);
#endif
		else
#ifdef DOF
			GL_FrustumPerspective(fov, ASPECT_RATIO, NEAR_CLIPPING_PLANE, FAR_CLIPPING_PLANE);
#else
		gluPerspective(fov, ASPECT_RATIO, NEAR_CLIPPING_PLANE, FAR_CLIPPING_PLANE);
#endif

#ifndef MINI_GL_COMPATIBILITY
		glGetDoublev(GL_PROJECTION_MATRIX, projMatrix); // added for new coronas' code (without depth buffer)
#endif

		glMatrixMode(GL_MODELVIEW);
	}

#ifndef MINI_GL_COMPATIBILITY
	glGetDoublev(GL_MODELVIEW_MATRIX, modelMatrix); // added for new coronas' code (without depth buffer)
#endif
}

int GL_GetTextureUsed(void)
{
	FTextureInfo*   tmp = gr_cachehead;
	int             res = 0;

	while (tmp)
	{
		res += tmp->height*tmp->width*(screen_depth/8);
		tmp = tmp->nextmipmap;
	}
	return res;
}

int GL_GetRenderVersion(void)
{
	return VERSION;
}

//
// Functions for OpenGL cross-fade effects
//

// Create a screen texture to fade from
void GL_StartScreenWipe(void)
{
	if (cv_grcompat.value)
		return;

	int texsize = 2048;

	// Use a power of two texture
	if (screen_width <= 1024)
		texsize = 1024;
	if (screen_width <= 512)
		texsize = 512;

	// Create screen texture
	glBindTexture(GL_TEXTURE_2D, startwipetex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
	glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 0, 0, texsize, texsize, 0);
}

// Create a screen texture to fade to
void GL_EndScreenWipe(void) // SRB2CBTODO: Merge this to a normal function that can just bind the screen texture nums by parameter
{
	if (cv_grcompat.value)
		return;

	int texsize = 2048;

	// Use a power of two texture
	if (screen_width <= 1024)
		texsize = 1024;
	if (screen_width <= 512)
		texsize = 512;

	// Create screen texture
	glBindTexture(GL_TEXTURE_2D, endwipetex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP);
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP);
	glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 0, 0, texsize, texsize, 0);
}

// Perform screen fades
// It should be noted that whenever a fullscreen polygon is called, it should always have a greater z value than the sky (4.0f),
// Otherwise, sky overlap will occur
void GL_DoScreenWipe(float alpha)
{
	if (cv_grcompat.value)
		return;

	int texsize = 2048;
	float xfix, yfix;

	// Use a power of two texture
	if (screen_width <= 1024)
		texsize = 1024;
	if (screen_width <= 512)
		texsize = 512;

	xfix = 1/((float)(texsize)/((float)((screen_width))));
	yfix = 1/((float)(texsize)/((float)((screen_height))));

	const float screenscale = 8.0f;

	// Draw the screen on back to fade to
	glBindTexture(GL_TEXTURE_2D, endwipetex);
	glBegin(GL_QUADS);
	glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

	// Bottom left
	glTexCoord2f(0.0f, 0.0f);
	glVertex3f(-1.0f*screenscale, -1.0f*screenscale, 8.0f);

	// Top left
	glTexCoord2f(0.0f, yfix);
	glVertex3f(-1.0f*screenscale, 1.0f*screenscale, 8.0f);

	// Top right
	glTexCoord2f(xfix, yfix);
	glVertex3f(1.0f*screenscale, 1.0f*screenscale, 8.0f);

	// Bottom right
	glTexCoord2f(xfix, 0.0f);
	glVertex3f(1.0f*screenscale, -1.0f*screenscale, 8.0f);
	glEnd();

	// Draw the screen on top that fades.
	glBindTexture(GL_TEXTURE_2D, startwipetex);
	glBegin(GL_QUADS);
	glColor4f(1.0f, 1.0f, 1.0f, alpha);

	// Bottom left
	glTexCoord2f(0.0f, 0.0f);
	glVertex3f(-1.0f*screenscale, -1.0f*screenscale, 8.0f);

	// Top left
	glTexCoord2f(0.0f, yfix);
	glVertex3f(-1.0f*screenscale, 1.0f*screenscale, 8.0f);

	// Top right
	glTexCoord2f(xfix, yfix);
	glVertex3f(1.0f*screenscale, 1.0f*screenscale, 8.0f);

	// Bottom right
	glTexCoord2f(xfix, 0.0f);
	glVertex3f(1.0f*screenscale, -1.0f*screenscale, 8.0f);

	glEnd();
}

//
// Functions for image post processing
//
void GL_PostImgRedraw(float points[SCREENVERTS][SCREENVERTS][2])
{
	if (cv_grcompat.value)
		return;

	// This function cannot draw if there is no screentexture
	if (!playerviewscreentex)
		return;

	int x, y;
	float float_x, float_y, float_nextx, float_nexty;
	float xfix, yfix;
	int texsize = 2048;

	// Use a power of two texture
	if (screen_width <= 1024)
		texsize = 1024;
	if (screen_width <= 512)
		texsize = 512;

	// X/Y stretch fix for all resolutions
	xfix = (float)(texsize)/((float)((screen_width)/(float)(SCREENVERTS-1)));
	yfix = (float)(texsize)/((float)((screen_height)/(float)(SCREENVERTS-1)));

	// Make sure this function only manipulates the texture of the screen
	glBindTexture(GL_TEXTURE_2D, playerviewscreentex);

	glBegin(GL_QUADS);
	// Draw a square behind the screen texture so nothing transparently shows over it
	glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
	glVertex3f(-16.0f, -16.0f, 5.0f);
	glVertex3f(-16.0f, 16.0f, 5.0f);
	glVertex3f(16.0f, 16.0f, 5.0f);
	glVertex3f(16.0f, -16.0f, 5.0f);

	for (x = 0; x < SCREENVERTS-1; x++)
	{
		for (y = 0;y < SCREENVERTS-1; y++)
		{
			// Used for texture coordinates
			// Annoying magic numbers to scale the square texture to
			// a non-square screen..
			float_x = (float)(x/(xfix));
			float_y = (float)(y/(yfix));
			float_nextx = (float)(x+1)/(xfix);
			float_nexty = (float)(y+1)/(yfix);

			// Attach the squares together.
			glTexCoord2f(float_x, float_y);
			glVertex3f(points[x][y][0], points[x][y][1], 4.4f);

			glTexCoord2f(float_x, float_nexty);
			glVertex3f(points[x][y+1][0], points[x][y+1][1], 4.4f);

			glTexCoord2f(float_nextx, float_nexty);
			glVertex3f(points[x+1][y+1][0], points[x+1][y+1][1], 4.4f);

			glTexCoord2f(float_nextx, float_y);
			glVertex3f(points[x+1][y][0], points[x+1][y][1], 4.4f);
		}
	}
	glEnd();
}

// Change the current OpenGL texture
void GL_BindTexture(ULONG texturenum)
{
	glBindTexture(GL_TEXTURE_2D, texturenum);
}

// Create a texture of screen's current image
void GL_MakeScreenTexture(ULONG texturenum, boolean grayscale)
{
	// SRB2CBTODO: Old graphics cards can't do "glCopyTexImage2D".. why? Is there a better method?
	if (cv_grcompat.value)
		return;

	int texsize = 2048;

	// Use a power of two texture
	if (screen_width <= 1024)
		texsize = 1024;
	if (screen_width <= 512)
		texsize = 512;

	// Create screen texture
	glBindTexture(GL_TEXTURE_2D, texturenum);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);

	// SRB2CBTODO: GL_LUMINANCE instead of GL_RGB makes the screen, this can be used very creatively!
	glCopyTexImage2D(GL_TEXTURE_2D, 0, grayscale ? GL_LUMINANCE : GL_RGB, 0, 0, texsize, texsize, 0);
}

// Draw the last scene under the intermission
void GL_DrawIntermissionBG(void)
{
	float xfix, yfix;
	int texsize = 2048;

	if (screen_width <= 1024)
		texsize = 1024;
	if (screen_width <= 512)
		texsize = 512;

	xfix = 1/((float)(texsize)/((float)((screen_width))));
	yfix = 1/((float)(texsize)/((float)((screen_height))));

	glBindTexture(GL_TEXTURE_2D, playerviewscreentex);
	glBegin(GL_QUADS);
	glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

	// Bottom left
	glTexCoord2f(0.0f, 0.0f);
	glVertex3f(-7.0f, -7.0f, 7.0f);

	// Top left
	glTexCoord2f(0.0f, yfix);
	glVertex3f(-7.0f, 7.0f, 7.0f);

	// Top right
	glTexCoord2f(xfix, yfix);
	glVertex3f(7.0f, 7.0f, 7.0f);

	// Bottom right
	glTexCoord2f(xfix, 0.0f);
	glVertex3f(7.0f, -7.0f, 7.0f);
	glEnd();
}

#endif //HWRENDER
