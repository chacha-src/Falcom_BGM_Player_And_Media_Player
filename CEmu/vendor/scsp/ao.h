/* Minimal Audio Overload shim for the vendored SCSP (YMF292-F) core.
   Only the types, INLINE and logerror that eng_ssf/scsp.c actually touches —
   the rest of the AO player framework is not needed by the chip core. */
#ifndef _CEMU_SCSP_AO_H_
#define _CEMU_SCSP_AO_H_

#include <stdlib.h> /* scsp.c mallocs its state; without this the pointer
                       return truncates to int under /W3. */

#undef LSB_FIRST
#define LSB_FIRST 1

typedef unsigned char ao_bool;
typedef unsigned char uint8;
typedef unsigned char UINT8;
typedef signed char int8;
typedef signed char INT8;
typedef unsigned short uint16;
typedef unsigned short UINT16;
typedef signed short int16;
typedef signed short INT16;
typedef signed int int32;
typedef unsigned int uint32;
typedef signed int INT32;
typedef unsigned int UINT32;
typedef signed __int64 int64;
typedef unsigned __int64 uint64;
typedef signed __int64 INT64;
typedef unsigned __int64 UINT64;

#ifndef INLINE
#define INLINE __inline
#endif

/* Sample fetch byte order. SCSP wave RAM holds big-endian words on real
   hardware, but eng_ssf stores it pre-swapped (mem_writeword_swap), so on a
   little-endian host LE16 is the identity — same as AO's LSB_FIRST branch. */
#define LE16(x) (x)
#define LE32(x) (x)

#ifndef logerror
#define logerror CEmuScspLogError
#endif

#ifdef __cplusplus
extern "C" {
#endif
void CEmuScspLogError(const char* fmt, ...);
#ifdef __cplusplus
}
#endif

#endif
