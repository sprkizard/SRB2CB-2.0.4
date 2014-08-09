// Emacs style mode select   -*- C++ -*-
//-----------------------------------------------------------------------------
//
// Copyright(C) 2004 Stephen McGranahan
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
//
//--------------------------------------------------------------------------
//
// DESCRIPTION:
//      Vectors
//      SoM created 05/18/09
//
//-----------------------------------------------------------------------------

#ifndef M_VECTOR_H__
#define M_VECTOR_H__

#ifdef ESLOPE

#include "m_fixed.h"
#include "tables.h"

#define TWOPI	    M_PI*2.0
#define HALFPI 	    M_PI*0.5
#define QUARTERPI   M_PI*0.25
//#define EPSILON     0.000001f
//#define OMEGA       10000000.0f

typedef struct
{
   fixed_t x, y, z;
} v3fixed_t;

typedef struct
{
   fixed_t x, y;
} v2fixed_t;

typedef struct
{
   float x, y, z;
} v3float_t;

typedef struct
{
	float yaw, pitch, roll;
} angles3d_t;

typedef struct
{
	double x, y, z;
} v3double_t;

typedef struct
{
   float x, y;
} v2float_t;


v3fixed_t *M_MakeVec3(const v3fixed_t *point1, const v3fixed_t *point2, v3fixed_t *a_o);
v3float_t *M_MakeVec3f(const v3float_t *point1, const v3float_t *point2, v3float_t *a_o);
void M_TranslateVec3(v3fixed_t *vec);
void M_TranslateVec3f(v3float_t *vec);
void M_AddVec3(v3fixed_t *dest, const v3fixed_t *v1, const v3fixed_t *v2);
void M_AddVec3f(v3float_t *dest, const v3float_t *v1, const v3float_t *v2);
void M_SubVec3(v3fixed_t *dest, const v3fixed_t *v1, const v3fixed_t *v2);
void M_SubVec3f(v3float_t *dest, const v3float_t *v1, const v3float_t *v2);
fixed_t M_DotVec3(const v3fixed_t *v1, const v3fixed_t *v2);
float M_DotVec3f(const v3float_t *v1, const v3float_t *v2);

#ifdef SESLOPE
v3double_t *M_MakeVec3d(const v3double_t *point1, const v3double_t *point2, v3double_t *a_o);
double M_DotVec3d(const v3double_t *v1, const v3double_t *v2);
void M_TranslateVec3d(v3double_t *vec);
#endif

void M_CrossProduct3(v3fixed_t *dest, const v3fixed_t *v1, const v3fixed_t *v2);
void M_CrossProduct3f(v3float_t *dest, const v3float_t *v1, const v3float_t *v2);
fixed_t FV_Magnitude(const v3fixed_t *a_normal);
float FV_Magnitudef(const v3float_t *a_normal);
fixed_t FV_NormalizeO(const v3fixed_t *a_normal, v3fixed_t *a_o);
float FV_NormalizeOf(const v3float_t *a_normal, v3float_t *a_o);
fixed_t FV_Normalize(v3fixed_t *a_normal);
fixed_t FV_Normalizef(v3float_t *a_normal);
void FV_Normal(const v3fixed_t *a_triangle, v3fixed_t *a_normal);
void FV_Normalf(const v3float_t *a_triangle, v3float_t *a_normal);
v3fixed_t *M_LoadVec(v3fixed_t *vec, fixed_t x, fixed_t y, fixed_t z);
v3fixed_t *M_CopyVec(v3fixed_t *a_o, const v3fixed_t *a_i);
v3float_t *M_LoadVecf(v3float_t *vec, float x, float y, float z);
v3float_t *M_CopyVecf(v3float_t *a_o, const v3float_t *a_i);
v3fixed_t *FV_Midpoint(const v3fixed_t *a_1, const v3fixed_t *a_2, v3fixed_t *a_o);
fixed_t FV_Distance(const v3fixed_t *p1, const v3fixed_t *p2);
v3float_t *FV_Midpointf(const v3float_t *a_1, const v3float_t *a_2, v3float_t *a_o);
angle_t FV_AngleBetweenVectors(const v3fixed_t *Vector1, const v3fixed_t *Vector2);
float FV_AngleBetweenVectorsf(const v3float_t *Vector1, const v3float_t *Vector2);
float FV_Distancef(const v3float_t *p1, const v3float_t *p2);


// Kalaron: something crazy, vector physics
float M_VectorYaw(v3float_t v);
float M_VectorPitch(v3float_t v);
v3float_t *M_VectorAlignTo(float Pitch, float Yaw, float Roll, v3float_t v, byte AngleAxis, float Rate);


void FV_Rotate(v3float_t *rotVec, const v3float_t *axisVec, const angle_t angle);






/// Doomsday ////




typedef float         vectorcomp_t;
typedef vectorcomp_t  vec2_t[2];
typedef const float   const_pvec2_t[2];
typedef vectorcomp_t  vec3_t[3];
typedef const float   const_pvec3_t[3];
typedef vectorcomp_t *pvec2_t;
typedef vectorcomp_t *pvec3_t;
typedef vec2_t       *arvec2_t;
typedef vec3_t       *arvec3_t;

// 2-dimensions:
void            V2_Set(pvec2_t vec, vectorcomp_t x, vectorcomp_t y);
void            V2_SetFixed(pvec2_t vec, fixed_t x, fixed_t y);
float           V2_Length(const pvec2_t vector);
float           V2_Distance(const pvec2_t a, const pvec2_t b);
float           V2_Normalize(pvec2_t vec);
void            V2_Copy(pvec2_t dest, const_pvec2_t src);
void            V2_Scale(pvec2_t vector, float scalar);
void            V2_Rotate(pvec2_t vec, float radians);
void            V2_Sum(pvec2_t dest, const pvec2_t src1, const pvec2_t src2);
void            V2_Subtract(pvec2_t dest, const pvec2_t src1,
                            const pvec2_t src2);
float           V2_DotProduct(const pvec2_t a, const pvec2_t b);
float           V2_ScalarProject(const pvec2_t a, const pvec2_t b);
void            V2_Project(pvec2_t dest, const pvec2_t a, const pvec2_t b);
boolean         V2_IsParallel(const pvec2_t a, const pvec2_t b);
boolean         V2_IsZero(const pvec2_t vec);
float           V2_Intersection(const pvec2_t p1, const pvec2_t delta1,
                                const pvec2_t p2, const pvec2_t delta2,
                                pvec2_t point);
float           V2_Intercept(const pvec2_t a, const pvec2_t b, const pvec2_t c,
                             const pvec2_t d, pvec2_t point);
boolean         V2_Intercept2(const pvec2_t a, const pvec2_t b,
                              const pvec2_t c, const pvec2_t d, pvec2_t point,
                              float *abFrac, float *cdFrac);
void            V2_Lerp(pvec2_t dest, const pvec2_t a, const pvec2_t b,
                        float c);
void            V2_InitBox(arvec2_t box, const pvec2_t point);
void            V2_AddToBox(arvec2_t box, const pvec2_t point);

// 3-dimensions:
void            V3_Set(pvec3_t vec, vectorcomp_t x, vectorcomp_t y,
                       vectorcomp_t z);
void            V3_SetFixed(pvec3_t vec, fixed_t x, fixed_t y, fixed_t z);
float           V3_Length(const pvec3_t vec);
float           V3_Distance(const pvec3_t a, const pvec3_t b);
float           V3_Normalize(pvec3_t vec);
void            V3_Copy(pvec3_t dest, const_pvec3_t src);
void            V3_Scale(pvec3_t vec, float scalar);
void            V3_Sum(pvec3_t dest, const_pvec3_t src1, const_pvec3_t src2);
void            V3_Subtract(pvec3_t dest, const_pvec3_t src1,
                            const_pvec3_t src2);
float           V3_DotProduct(const_pvec3_t a, const_pvec3_t b);
void            V3_CrossProduct(pvec3_t dest, const pvec3_t src1,
                                const pvec3_t src2);
void            V3_PointCrossProduct(pvec3_t dest, const pvec3_t v1,
                                     const pvec3_t v2, const pvec3_t v3);
float           V3_ClosestPointOnPlane(pvec3_t dest,
                                       const pvec3_t planeNormal,
                                       const pvec3_t planePoint,
                                       const pvec3_t arbPoint);
int             V3_MajorAxis(const pvec3_t vec);
boolean         V3_IsZero(const pvec3_t vec);
void            V3_Lerp(pvec3_t dest, const pvec3_t a, const pvec3_t b,
                        float c);






/// Doomsday /////





#endif

// EOF
#endif // #ifdef ESLOPE

