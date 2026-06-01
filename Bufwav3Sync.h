#pragma once
#include "mp3.h"
#include "AudioUpscaler.h"

// bufwav3 / DirectSound リング — Speana と同一の readPos 計算

inline ULONG Bufwav3RingBytes()
{
	return (g_ds_buffer_bytes > 0)
		? g_ds_buffer_bytes
		: (ULONG)(OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM);
}

inline ULONG DsQueuedBytes(ULONG playCur, ULONG writeCur, ULONG ringBytes)
{
	if (ringBytes == 0) return 0;
	return (writeCur + ringBytes - playCur) % ringBytes;
}

inline long DsQueuedSamples(ULONG playCur, ULONG writeCur, int bytesPerFrame)
{
	if (bytesPerFrame <= 0) return 0;
	const ULONG ringBytes = Bufwav3RingBytes();
	return (long)(DsQueuedBytes(playCur, writeCur, ringBytes) / (ULONG)bytesPerFrame);
}

inline long SpeanaAnalysisLatencyBytes(double sampleRate, int bytesPerFrame, int windowBytes, int ringBytes)
{
	int latencySetting = (windowBytes >= 8192) ? -1600 : -800;
	const int rateForLatency = (int)(sampleRate + 0.5);
	if (rateForLatency > 0 && rateForLatency < 44100) {
		latencySetting = (int)((float)latencySetting * (44100.0f / (float)rateForLatency));
	}
	long latencyBytes = (long)(sampleRate * bytesPerFrame * latencySetting / 1000.0);
	const long maxSafeLatency = -(long)(ringBytes * 0.9) + windowBytes;
	if (latencyBytes < maxSafeLatency)
		latencyBytes = maxSafeLatency;
	return latencyBytes;
}

// Speana() と同じ: readPos = PlayCursor - windowBytes + latencyBytes
// extraLatencyMs>0: さらに過去を読む（ピアノロールが音より早いとき用）
inline long SpeanaAnalysisReadPos(ULONG playCursor, int windowBytes, int bytesPerFrame, int ringBytes, double sampleRate, int extraLatencyMs = 0)
{
	long latencyBytes = SpeanaAnalysisLatencyBytes(sampleRate, bytesPerFrame, windowBytes, ringBytes);
	if (extraLatencyMs > 0)
		latencyBytes -= (long)(sampleRate * (double)bytesPerFrame * (double)extraLatencyMs / 1000.0);
	long readPos = (long)playCursor - windowBytes + latencyBytes;
	while (readPos < 0) readPos += ringBytes;
	while (readPos >= ringBytes) readPos -= ringBytes;
	if (bytesPerFrame > 0)
		readPos -= (readPos % bytesPerFrame);
	return readPos;
}

// ピアノロール: DS キュー（未再生）分を引いてから解析（先読みで表示が早くなるのを防ぐ）
inline long PianoAnalysisReadPos(ULONG playCursor, ULONG writeCursor, int windowBytes, int bytesPerFrame, int ringBytes, double sampleRate, int extraLatencyMs = 45)
{
	if (ringBytes <= 0 || bytesPerFrame <= 0) return 0;
	const long queued = (long)DsQueuedBytes(playCursor, writeCursor, (ULONG)ringBytes);
	long heardCur = (long)playCursor - queued;
	while (heardCur < 0) heardCur += ringBytes;
	while (heardCur >= ringBytes) heardCur -= ringBytes;
	return SpeanaAnalysisReadPos((ULONG)heardCur, windowBytes, bytesPerFrame, ringBytes, sampleRate, extraLatencyMs);
}
