/*
 *  hw_clip.h
 *  SRB2CB
 *
 *  PrBoom's OpenGL clipping
 *  
 *
 */

// OpenGL BSP clipping
#include "../doomdef.h"
#include "../tables.h"
#include "../doomtype.h"

boolean gld_clipper_SafeCheckRange(angle_t startAngle, angle_t endAngle);
void gld_clipper_SafeAddClipRange(angle_t startangle, angle_t endangle);
void gld_clipper_Clear(void);
angle_t gld_FrustumAngle(void);
void gld_FrustrumSetup(void);
boolean gld_SphereInFrustum(float x, float y, float z, float radius);

