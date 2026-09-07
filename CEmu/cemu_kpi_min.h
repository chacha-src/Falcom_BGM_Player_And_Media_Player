#pragma once
#include <windows.h>

/* S98 デコーダ内部専用（kpi_decoder.h の KPI_MEDIAINFO と衝突しない名前） */
struct CEMU_S98_MEDIAINFO {
	enum {
		SEEK_FLAGS_SAMPLE = 0x01,
		FORMAT_PCM = 0
	};
	DWORD cb;
	DWORD dwNumber;
	DWORD dwCount;
	DWORD dwFormatType;
	DWORD dwSampleRate;
	INT32 nBitsPerSample;
	DWORD dwChannels;
	DWORD dwSpeakerConfig;
	UINT64 qwLength;
	UINT64 qwLoop;
	UINT64 qwFadeOut;
	DWORD dwLoopCount;
	DWORD dwUnitSample;
	DWORD dwSeekableFlags;
	DWORD dwVideoWidth;
	DWORD dwVideoHeight;
	DWORD dwReserved[6];
};

inline void CEmuS98InitMediaInfo(CEMU_S98_MEDIAINFO* m)
{
	if (!m) return;
	ZeroMemory(m, sizeof(*m));
	m->cb = sizeof(CEMU_S98_MEDIAINFO);
	m->dwSampleRate = 44100;
	m->nBitsPerSample = 16;
	m->dwChannels = 2;
}

inline UINT64 CEmuS98SampleTo100ns(UINT64 samples, DWORD rate)
{
	if (!rate) return 0;
	return samples * 10000000ull / rate;
}

inline UINT64 CEmuS98_100nsToSample(UINT64 t100ns, DWORD rate)
{
	if (!rate) return 0;
	return t100ns * rate / 10000000ull;
}
