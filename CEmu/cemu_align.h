#pragma once
/* AVX2 前提の 32byte 境界。realloc だと 16byte 未満に落ちることがあり MixAdd が遅くなる。 */
#include <malloc.h>
#include <stdint.h>

#ifndef CEMU_ALIGN_BYTES
#define CEMU_ALIGN_BYTES 32
#endif

static inline void* CEmuAlignedAlloc(size_t bytes)
{
	if (!bytes) return NULL;
	return _aligned_malloc(bytes, CEMU_ALIGN_BYTES);
}

static inline void* CEmuAlignedRealloc(void* p, size_t bytes)
{
	if (!bytes) {
		if (p) _aligned_free(p);
		return NULL;
	}
	return _aligned_realloc(p, bytes, CEMU_ALIGN_BYTES);
}

static inline void CEmuAlignedFree(void* p)
{
	if (p) _aligned_free(p);
}

static inline int CEmuPtrAligned32(const void* p)
{
	return (((uintptr_t)p) & (CEMU_ALIGN_BYTES - 1)) == 0;
}
