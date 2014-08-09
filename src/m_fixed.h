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
/// \brief Fixed point arithmetics implementation
///
///	Fixed point, 32bit as 16.16.

#ifndef __M_FIXED__
#define __M_FIXED__

#include "doomtype.h"
#include <math.h>
#ifdef __GNUC__
#include <stdlib.h>
#endif

#ifdef _WIN32_WCE
#include "sdl/SRB2CE/cehelp.h"
#endif

/*!
  \brief bits of the fraction
*/
#define FRACBITS 16
/*!
  \brief units of the fraction
*/
#define FRACUNIT (1<<FRACBITS)
#define FRACMASK (FRACUNIT -1)
/**	\brief	Redefinition of int as fixed_t
	unit used as fixed_t
*/

#if defined (_MSC_VER)
typedef __int32 fixed_t;
#else
typedef int fixed_t;
#endif

/*!
  \brief convert a fixed_t number into floating number
*/
#define FIXED_TO_FLOAT(x) (((float)(x)) / ((float)FRACUNIT))

/*!
 \brief convert a floating number into fixed_t number
 */
#define FLOAT_TO_FIXED(f) ((fixed_t)((f) * (float)FRACUNIT))

/*!
 \brief convert a fixed_t number into double floating number
 */
#define FIXED_TO_DOUBLE(f) ((double)((f) / FRACUNIT))

/*!
 \brief convert a double floating number into fixed_t number
 */
#define DOUBLE_TO_FIXED(f) ((fixed_t)((f) * FRACUNIT))

/*!
 \brief convert a integer into fixed_t number
 */
#define INT_TO_FIXED(x) ((int)((x) * FRACUNIT))

/*!
 \brief convert a fixed_t number into integer
 */
#define FIXED_TO_INT(x) (((int)(x)) / (FRACUNIT))


/**	\brief	The TMulScale16 function

	\param	a	a parameter of type fixed_t
	\param	b	a parameter of type fixed_t
	\param	c	a parameter of type fixed_t
	\param	d	a parameter of type fixed_t
	\param	e	a parameter of type fixed_t
	\param	f	a parameter of type fixed_t

	\return	fixed_t


*/
FUNCMATH FUNCINLINE static ATTRINLINE fixed_t TMulScale16(fixed_t a, fixed_t b, fixed_t c, fixed_t d, fixed_t e, fixed_t f) \
{ \
	return (fixed_t)((((INT64)a * (INT64)b) + ((INT64)c * (INT64)d) \
		+ ((INT64)e * (INT64)f)) >> 16); \
}

/**	\brief	The DMulScale16 function

	\param	a	a parameter of type fixed_t
	\param	b	a parameter of type fixed_t
	\param	c	a parameter of type fixed_t
	\param	d	a parameter of type fixed_t

	\return	fixed_t


*/
FUNCMATH FUNCINLINE static ATTRINLINE fixed_t DMulScale16(fixed_t a, fixed_t b, fixed_t c, fixed_t d) \
{ \
	return (fixed_t)((((INT64)a * (INT64)b) + ((INT64)c * (INT64)d)) >> 16); \
}

/**	\brief	The DMulScale32 function
 
 \param	a	a parameter of type fixed_t
 \param	b	a parameter of type fixed_t
 \param	c	a parameter of type fixed_t
 \param	d	a parameter of type fixed_t
 
 \return	fixed_t
 
 
 */
FUNCMATH FUNCINLINE static ATTRINLINE fixed_t DMulScale32(fixed_t a, fixed_t b, fixed_t c, fixed_t d) \
{ \
	return (fixed_t)((((INT64)a * (INT64)b) + ((INT64)c * (INT64)d)) >> 32); \
}

static inline int DivScale32 (fixed_t a, fixed_t b) { return (fixed_t)(((INT64)a << 32) / b); }

#if defined (__WATCOMC__) && FRACBITS == 16
	#pragma aux FixedMul =  \
		"imul ebx",         \
		"shrd eax,edx,16"   \
		parm    [eax] [ebx] \
		value   [eax]       \
		modify exact [eax edx]

	#pragma aux FixedDiv2 = \
		"cdq",              \
		"shld edx,eax,16",  \
		"sal eax,16",       \
		"idiv ebx"          \
		parm    [eax] [ebx] \
		value   [eax]       \
		modify exact [eax edx]
#elif defined (__GNUC__) && defined (__i386__) && !defined (NOASM)
// DJGPP, i386 linux, cygwin or mingw
FUNCMATH FUNCINLINE static inline fixed_t FixedMul(fixed_t a, fixed_t b) // asm
{
	fixed_t ret;
	asm
	(
		"imull %2;"            // a*b
		"shrdl %3,%%edx,%0;"   // shift logical right FRACBITS bits
		:"=a" (ret)            // eax is always the result and the first operand (%0,%1)
		:"0" (a), "r" (b)      // and %2 is what we use imull on with what in %1
		, "I" (FRACBITS)       // %3 holds FRACBITS (normally 16)
		:"%cc", "%edx"         // edx and condition codes clobbered
	 );
	return ret;
}

FUNCMATH FUNCINLINE static inline fixed_t FixedDiv2(fixed_t a, fixed_t b)
{
	fixed_t ret;
	asm
	(
		"movl  %1,%%edx;"    // these two instructions allow the next two to pair, on the Pentium processor.
		"sarl  $31,%%edx;"   // shift arithmetic right 31 on EDX
		"shldl %3,%1,%%edx;" // DP shift logical left FRACBITS on EDX
		"sall  %3,%0;"       // shift arithmetic left FRACBITS on EAX
		"idivl %2;"          // EDX/b = EAX
		: "=a" (ret)
		: "0" (a), "r" (b)
		, "I" (FRACBITS)
		: "%edx"
	 );
	return ret;
}
#elif defined (__GNUC__) && defined (__arm__) // ARMv4 ASM
FUNCMATH FUNCINLINE static inline fixed_t FixedMul(fixed_t a, fixed_t b) // Use smull
{
	fixed_t ret;
	asm
	(
		"smull %[lo], r1, %[a], %[b];"
		"mov %[lo], %[lo], lsr %3;"
		"orr %[lo], %[lo], r1, lsl %3;"
		: [lo] "=&r" (ret) // rhi, rlo and rm must be distinct registers
		: [a] "r" (a), [b] "r" (b)
		, "i" (FRACBITS)
		: "r1"
	 );
	return ret;
}

FUNCMATH FUNCINLINE static inline fixed_t FixedDiv2(fixed_t a, fixed_t b) // No double or asm div in ARM land
{
	return (((INT64)a)<<FRACBITS)/b;
}
#elif defined (__GNUC__) && defined (__ppc__) // Nintendo Wii: PPC CPU
FUNCMATH FUNCINLINE static inline fixed_t FixedMul(fixed_t a, fixed_t b) // asm
{
	fixed_t ret, hi, lo;
	asm
	(
		"mullw %0, %2, %3;"
		"mulhw %1, %2, %3"
		: "=r" (hi), "=r" (lo)
		: "r" (a), "r" (b)
		, "I" (FRACBITS)
	 );
	ret = (INT64)((hi>>FRACBITS)+lo)<<FRACBITS;
	return ret;
}

FUNCMATH FUNCINLINE static inline fixed_t FixedDiv2(fixed_t a, fixed_t b)
{
	return (((INT64)a)<<FRACBITS)/b;
}
#elif defined (__GNUC__) && defined (__mips__) // Sony PSP: MIPS CPU
FUNCMATH FUNCINLINE static inline fixed_t FixedMul(fixed_t a, fixed_t b) // asm
{
	fixed_t ret;
	asm
	(
		"mult %3, %4;"    // a*b=h<32+l
		: "=r" (ret), "=l" (a), "=h" (b) // TODO: Use shr opcode
		: "0" (a), "r" (b)
		, "I" (FRACBITS)
		//: "+l", "+h"
	 );
	ret = (INT64)((a>>FRACBITS)+b)<<FRACBITS;
	return ret;
}

FUNCMATH FUNCINLINE static inline fixed_t FixedDiv2(fixed_t a, fixed_t b) // No 64b asm div in MIPS land
{
	return (((INT64)a)<<FRACBITS)/b;
}
#elif defined (__GNUC__) && defined (__sh__) && 0 // DC: SH4 CPU
#elif defined (__GNUC__) && defined (__m68k__) && 0 // DEAD: Motorola 6800 CPU
#elif defined (_MSC_VER) && defined(USEASM) && FRACBITS == 16
// Microsoft Visual C++ (no asm inline)
fixed_t __cdecl FixedMul(fixed_t a, fixed_t b);
fixed_t __cdecl FixedDiv2(fixed_t a, fixed_t b);
#else
FUNCMATH fixed_t FixedMul(fixed_t a, fixed_t b);
FUNCMATH fixed_t FixedDiv2(fixed_t a, fixed_t b);
#define __USE_C_FIXED__
#endif

/**	\brief	The FixedDiv function

	\param	a	fixed_t number
	\param	b	fixed_t number

	\return	a/b


*/
FUNCINLINE static ATTRINLINE fixed_t FixedDiv(fixed_t a, fixed_t b)
{
	if ((abs(a) >> (FRACBITS-2)) >= abs(b))
		return (a^b) < 0 ? MININT : MAXINT;

	return FixedDiv2(a, b);
}


/**	\brief	The FixedMod function
	\author CPhipps from PrBoom

	\param	a	fixed_t number
	\param	b	fixed_t number

	\return	 a % b, guaranteeing 0 <= a < b
	\note that the C standard for % does not guarantee this
*/
FUNCINLINE static ATTRINLINE fixed_t FixedMod(fixed_t a, fixed_t b)
{
	if (b & (b-1))
	{
		const fixed_t r = a % b;
		return ((r < 0) ? r+b : r);
	}
	return (a & (b-1));
}

/**	\brief	The FixedInt function
 
 \param	a	fixed_t number
 
 \return	 a/FRACUNIT
 */

FUNCMATH FUNCINLINE static ATTRINLINE fixed_t FixedInt(fixed_t a)
{
	return FixedMul(a, 1);
}

/**	\brief	The FixedSqrt function
 
 \param	x	fixed_t number
 
 \return	sqrt(x)
 
 
 */
FUNCMATH FUNCINLINE static ATTRINLINE fixed_t FixedSqrt(fixed_t x)
{
	const float fx = FIXED_TO_FLOAT(x);
	float fr;
	fr = (float)sqrt(fx);
	return FLOAT_TO_FIXED(fr);
}

#endif //m_fixed.h
