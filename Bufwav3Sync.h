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

inline int Bufwav3ScaleRefSamples(int refSamples, double sampleRate)
{
	if (refSamples <= 0) return 0;
	const int sr = (int)(sampleRate + 0.5);
	if (sr < 8000) return refSamples;
	int64_t n = ((int64_t)refSamples * (int64_t)sr + 22050) / 44100;
	if (n < 64) n = 64;
	return (int)n;
}

inline int SpeanaAnalysisLatencySettingMs(int windowBytes, int bytesPerFrame, double sampleRate)
{
	const int windowFrames = (bytesPerFrame > 0) ? (windowBytes / bytesPerFrame) : 0;
	const int thresh8192 = Bufwav3ScaleRefSamples(8192, sampleRate);
	return (windowFrames >= thresh8192) ? -1600 : -800;
}

inline long SpeanaAnalysisLatencyBytes(double sampleRate, int bytesPerFrame, int windowBytes, int ringBytes)
{
	int latencySetting = SpeanaAnalysisLatencySettingMs(windowBytes, bytesPerFrame, sampleRate);
	const int rateForLatency = (int)(sampleRate + 0.5);
	if (rateForLatency > 0 && rateForLatency < 44100) {
		latencySetting = (int)((float)latencySetting * (44100.0f / (float)rateForLatency));
	}
	long latencyBytes = (long)(sampleRate * bytesPerFrame * latencySetting / 1000.0);
	const long maxSafeLatency = -(long)(ringBytes * 0.9) + windowBytes;
	if (latencyBytes < maxSafeLatency)
		latencyBytes = maxSafeLatency;
	// 正の latency は同期点が PlayCursor より先＝表示が実音より早くなる
	if (latencyBytes > 0)
		latencyBytes = 0;
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

// ピアノロール: スペアナと同じ readPos（writeCursor / queued 補正は未使用）
inline long PianoAnalysisReadPos(ULONG playCursor, ULONG /*writeCursor*/, int windowBytes, int bytesPerFrame, int ringBytes, double sampleRate, int extraLatencyMs = 0)
{
	return SpeanaAnalysisReadPos(playCursor, windowBytes, bytesPerFrame, ringBytes, sampleRate, extraLatencyMs);
}

// 解析窓がスペアナより長いとき、末尾（再生同期点）を Speana と揃えて開始位置を返す
inline long PianoRollWideReadPos(ULONG playCursor, int pianoWindowBytes, int speanaWindowBytes, int bytesPerFrame, int ringBytes, double sampleRate, int extraLatencyMs = 0)
{
	if (pianoWindowBytes <= speanaWindowBytes)
		return SpeanaAnalysisReadPos(playCursor, pianoWindowBytes, bytesPerFrame, ringBytes, sampleRate, extraLatencyMs);
	const long speanaStart = SpeanaAnalysisReadPos(playCursor, speanaWindowBytes, bytesPerFrame, ringBytes, sampleRate, extraLatencyMs);
	long pianoStart = speanaStart - (pianoWindowBytes - speanaWindowBytes);
	while (pianoStart < 0) pianoStart += ringBytes;
	while (pianoStart >= ringBytes) pianoStart -= ringBytes;
	if (bytesPerFrame > 0)
		pianoStart -= (pianoStart % bytesPerFrame);
	return pianoStart;
}
