#pragma once

#include <windows.h>

// Decoded PCM cache for Ogg/FLAC loop region [loop1, loop1 + loop2).
// When the region is fully cached, playback reads from RAM instead of the decoder.

void OggFlacLoopCache_Free();
void OggFlacLoopCache_Invalidate();
void OggFlacLoopCache_InvalidateUnlessLoopRewind(int saveloop, int loop1, int loop2, int targetFrames);

// ループ有効かつ幅あり（生PCMキャッシュはテンポ/RB/EQより前なので条件はこれだけ）
bool OggFlacLoopCache_ShouldUse(int saveloop, int loop2);

// デコーダ直後・readtempo 前の生PCM（bpf バイト／フレーム）を [dst, dst+len_bytes) にコピー。全部埋まっていれば非0。
int OggFlacLoopCache_TryReadRawBytes(
	int playb_frames_start,
	int loop1,
	int loop2,
	int bpf,
	BYTE* dst,
	int len_bytes);

// frame_start = first sample-frame index for pcm[0]
void OggFlacLoopCache_Commit(
	int frame_start,
	const BYTE* pcm,
	int len_bytes,
	int loop1,
	int loop2,
	int bpf,
	int saveloop);

// ループ区間内で当該フレームがキャッシュに完全格納されているとき 1
int OggFlacLoopCache_FrameFullyFilled(int frame_idx, int loop1, int loop2, int bpf);

// buf_frame_start から nframes 分: ループ内かつ埋まっているフレームだけ dst[i*bpf] にコピー
void OggFlacLoopCache_CopyCachedFramesInRange(
	int buf_frame_start,
	int nframes,
	int loop1,
	int loop2,
	int bpf,
	BYTE* dst);
