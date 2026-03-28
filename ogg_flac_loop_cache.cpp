#include "stdafx.h"
#include "ogg_flac_loop_cache.h"
#include <stdlib.h>
#include <string.h>

#ifndef MAX_CACHE_BYTES
#define MAX_CACHE_BYTES (512ULL * 1024 * 1024)
#endif

static BYTE* g_loop_pcm = NULL;
static BYTE* g_loop_fill = NULL;
static size_t g_loop_bytes = 0;
static int g_cached_loop1 = -1;
static int g_cached_loop2 = 0;
static int g_cached_bpf = 0;

static bool range_filled(size_t off, size_t len);

void OggFlacLoopCache_Free()
{
	free(g_loop_pcm);
	g_loop_pcm = NULL;
	free(g_loop_fill);
	g_loop_fill = NULL;
	g_loop_bytes = 0;
	g_cached_loop1 = -1;
	g_cached_loop2 = 0;
	g_cached_bpf = 0;
}

void OggFlacLoopCache_Invalidate()
{
	if (g_loop_fill && g_loop_bytes)
		memset(g_loop_fill, 0, g_loop_bytes);
}

void OggFlacLoopCache_InvalidateUnlessLoopRewind(int saveloop, int loop1, int loop2, int targetFrames)
{
	if (saveloop && loop2 > 0 && targetFrames == loop1)
		return;
	OggFlacLoopCache_Invalidate();
}

bool OggFlacLoopCache_ShouldUse(int saveloop, int loop2)
{
	return saveloop != 0 && loop2 > 0;
}

int OggFlacLoopCache_TryReadRawBytes(
	int playb_frames_start,
	int loop1,
	int loop2,
	int bpf,
	BYTE* dst,
	int len_bytes)
{
	if (!dst || len_bytes <= 0 || bpf <= 0)
		return 0;
	if ((len_bytes % bpf) != 0)
		return 0;
	if (!g_loop_pcm || !g_loop_fill || g_loop_bytes == 0)
		return 0;
	if (g_cached_loop1 != loop1 || g_cached_loop2 != loop2 || g_cached_bpf != bpf)
		return 0;
	const __int64 frames = (__int64)(len_bytes / bpf);
	if (playb_frames_start < (__int64)loop1)
		return 0;
	if (playb_frames_start + frames > (__int64)loop1 + (__int64)loop2)
		return 0;
	const size_t off = (size_t)((__int64)playb_frames_start - (__int64)loop1) * (size_t)bpf;
	if (off + (size_t)len_bytes > g_loop_bytes)
		return 0;
	if (!range_filled(off, (size_t)len_bytes))
		return 0;
	memcpy(dst, g_loop_pcm + off, (size_t)len_bytes);
	return len_bytes;
}

static bool frame_fully_filled_in_loop(int frame_idx, int loop1_, int loop2_, int bpf)
{
	if (loop2_ <= 0 || bpf <= 0)
		return false;
	if (frame_idx < loop1_ || frame_idx >= loop1_ + loop2_)
		return false;
	if (!g_loop_pcm || !g_loop_fill || g_loop_bytes == 0)
		return false;
	if (g_cached_loop1 != loop1_ || g_cached_loop2 != loop2_ || g_cached_bpf != bpf)
		return false;
	const size_t off = (size_t)(frame_idx - loop1_) * (size_t)bpf;
	if (off + (size_t)bpf > g_loop_bytes)
		return false;
	return range_filled(off, (size_t)bpf);
}

int OggFlacLoopCache_FrameFullyFilled(int frame_idx, int loop1, int loop2, int bpf)
{
	return frame_fully_filled_in_loop(frame_idx, loop1, loop2, bpf) ? 1 : 0;
}

void OggFlacLoopCache_CopyCachedFramesInRange(
	int buf_frame_start,
	int nframes,
	int loop1_,
	int loop2_,
	int bpf,
	BYTE* dst)
{
	if (!dst || nframes <= 0 || bpf <= 0)
		return;
	if (!g_loop_pcm || !g_loop_fill)
		return;
	if (g_cached_loop1 != loop1_ || g_cached_loop2 != loop2_ || g_cached_bpf != bpf)
		return;
	const int L0 = loop1_;
	const int L1 = loop1_ + loop2_;
	for (int i = 0; i < nframes; ++i) {
		const int ff = buf_frame_start + i;
		if (ff < L0 || ff >= L1)
			continue;
		if (!frame_fully_filled_in_loop(ff, loop1_, loop2_, bpf))
			continue;
		const size_t cache_off = (size_t)(ff - L0) * (size_t)bpf;
		memcpy(dst + (size_t)i * (size_t)bpf, g_loop_pcm + cache_off, (size_t)bpf);
	}
}

static bool ensure_layout(int loop1, int loop2, int bpf, int saveloop)
{
	if (!saveloop || loop2 <= 0 || bpf <= 0)
		return false;
	const size_t need = (size_t)loop2 * (size_t)bpf;
	if (need == 0 || need > MAX_CACHE_BYTES)
		return false;
	if (g_loop_pcm && g_cached_loop1 == loop1 && g_cached_loop2 == loop2 && g_cached_bpf == bpf)
		return true;
	OggFlacLoopCache_Free();
	g_loop_pcm = (BYTE*)calloc(need, 1);
	g_loop_fill = (BYTE*)calloc(need, 1);
	if (!g_loop_pcm || !g_loop_fill) {
		OggFlacLoopCache_Free();
		return false;
	}
	g_loop_bytes = need;
	g_cached_loop1 = loop1;
	g_cached_loop2 = loop2;
	g_cached_bpf = bpf;
	return true;
}

static bool range_filled(size_t off, size_t len)
{
	if (!g_loop_fill || off + len > g_loop_bytes)
		return false;
	for (size_t i = 0; i < len; ++i) {
		if (!g_loop_fill[off + i])
			return false;
	}
	return true;
}

void OggFlacLoopCache_Commit(
	int frame_start,
	const BYTE* pcm,
	int len_bytes,
	int loop1,
	int loop2,
	int bpf,
	int saveloop)
{
	if (!pcm || len_bytes <= 0 || bpf <= 0)
		return;
	if ((len_bytes % bpf) != 0)
		return;
	if (!ensure_layout(loop1, loop2, bpf, saveloop))
		return;
	const __int64 f0 = frame_start;
	const __int64 f1 = f0 + (__int64)(len_bytes / bpf);
	const __int64 lr0 = loop1;
	const __int64 lr1 = (__int64)loop1 + (__int64)loop2;
	const __int64 a0 = (f0 > lr0) ? f0 : lr0;
	const __int64 a1 = (f1 < lr1) ? f1 : lr1;
	if (a0 >= a1)
		return;
	const int skip_frames = (int)(a0 - f0);
	const int copy_frames = (int)(a1 - a0);
	const BYTE* src = pcm + (size_t)skip_frames * (size_t)bpf;
	const size_t copy_bytes = (size_t)copy_frames * (size_t)bpf;
	const size_t cache_off = (size_t)(a0 - lr0) * (size_t)bpf;
	if (cache_off + copy_bytes > g_loop_bytes)
		return;
	memcpy(g_loop_pcm + cache_off, src, copy_bytes);
	memset(g_loop_fill + cache_off, 1, copy_bytes);
}
