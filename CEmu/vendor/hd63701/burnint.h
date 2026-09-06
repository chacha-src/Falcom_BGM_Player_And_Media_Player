/* Minimal FBNeo burnint stubs for detached HD63701 (CEmu). */
#ifndef CEMU_HD63701_BURNINT_H
#define CEMU_HD63701_BURNINT_H

#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint8_t  UINT8;
typedef int8_t   INT8;
typedef uint16_t UINT16;
typedef int16_t  INT16;
typedef uint32_t UINT32;
typedef int32_t  INT32;
typedef uint64_t UINT64;
typedef int64_t  INT64;

typedef union {
	struct { UINT8 l, h, h2, h3; } b;
	struct { UINT16 l, h; } w;
	UINT32 d;
} PAIR;

#ifndef INLINE
#if defined(_MSC_VER)
#define INLINE __inline static
#else
#define INLINE static inline
#endif
#endif

#ifndef STRUCT_SIZE_HELPER
#define STRUCT_SIZE_HELPER(t, m) (offsetof(t, m) + sizeof(((t*)0)->m))
#endif

/* Quiet FBNeo debug hooks used by m6800.cpp (FBNEO_DEBUG off). */
#ifndef PRINT_ERROR
#define PRINT_ERROR 0
#endif

#ifdef __cplusplus
}
#endif

#endif
