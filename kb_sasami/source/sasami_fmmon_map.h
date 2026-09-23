#pragma once
/* File-backed MapView for live/ring dumps.
   Writer: memcpy slot, then MemoryBarrier, then publish gen (seqlock).
   Reader: load gen, memcpy slot. No ReadFile/WriteFile on the hot path. */
#include "sasami_fmmon.h"
#include <windows.h>
#include <string.h>
#include <stddef.h>

inline void SasamiFmMonUnmap(HANDLE* hMap, void** view)
{
	if (view && *view) {
		UnmapViewOfFile(*view);
		*view = NULL;
	}
	if (hMap && *hMap) {
		CloseHandle(*hMap);
		*hMap = NULL;
	}
}

/* bytes=0 maps the current file size (v7 dump is the only stride). */
inline int SasamiFmMonMapBytes(HANDLE hFile, int writable, DWORD bytes,
	HANDLE* hMap, void** view)
{
	if (!hFile || hFile == INVALID_HANDLE_VALUE || !hMap || !view)
		return 0;
	if (*view)
		return 1;
	const DWORD prot = writable ? PAGE_READWRITE : PAGE_READONLY;
	const DWORD acc = writable ? FILE_MAP_WRITE : FILE_MAP_READ;
	HANDLE m = CreateFileMappingW(hFile, NULL, prot, 0, bytes, NULL);
	if (!m)
		return 0;
	void* v = MapViewOfFile(m, acc, 0, 0, bytes);
	if (!v) {
		CloseHandle(m);
		return 0;
	}
	*hMap = m;
	*view = v;
	return 1;
}

inline int SasamiFmMonMapRing(HANDLE hFile, int writable,
	HANDLE* hMap, SasamiFmMonRing** view)
{
	void* v = NULL;
	if (!SasamiFmMonMapBytes(hFile, writable, 0, hMap, &v))
		return 0;
	*view = (SasamiFmMonRing*)v;
	return 1;
}

inline int SasamiFmMonMapLive(HANDLE hFile, int writable,
	HANDLE* hMap, SasamiFmMonDump** view)
{
	void* v = NULL;
	if (!SasamiFmMonMapBytes(hFile, writable, 0, hMap, &v))
		return 0;
	*view = (SasamiFmMonDump*)v;
	return 1;
}

/* Copy dump into slot[gen%RING], then publish gen so the reader never sees a torn slot. */
inline void SasamiFmMonPublishDump(SasamiFmMonRing* ring, uint32_t* gen,
	SasamiFmMonDump* live, const SasamiFmMonDump* d)
{
	if (!d) return;
	if (ring && gen) {
		const uint32_t idx = (*gen) % (uint32_t)SASAMI_FMMON_RING;
		memcpy(&ring->slot[idx], d, sizeof(*d));
		MemoryBarrier();
		const uint32_t g = *gen + 1u;
		*gen = g;
		ring->magic[0] = 'O';
		ring->magic[1] = 'P';
		ring->magic[2] = 'N';
		ring->magic[3] = 'R';
		ring->version = SASAMI_FMMON_RING_VERSION;
		ring->gen = g;
	}
	if (live)
		memcpy(live, d, sizeof(*d));
}

inline uint32_t SasamiFmMonPeekGen(const SasamiFmMonRing* ring)
{
	if (!ring) return 0;
	const uint32_t g = *(volatile const uint32_t*)&ring->gen;
	MemoryBarrier();
	return g;
}

inline int SasamiFmMonCopySlot(const void* ringBase, uint32_t idx, size_t slotSz,
	SasamiFmMonDump* out)
{
	if (!ringBase || !out || slotSz == 0) return 0;
	memset(out, 0, sizeof(*out));
	const uint8_t* p = (const uint8_t*)ringBase
		+ sizeof(SasamiFmMonRingHdr)
		+ (size_t)idx * slotSz;
	const size_t n = (slotSz < sizeof(*out)) ? slotSz : sizeof(*out);
	memcpy(out, p, n);
	return 1;
}
