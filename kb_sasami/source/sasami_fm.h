#pragma once

#include "sasami_file.h"
#include <stdint.h>
#include <mutex>

class SasamiFmPlayer {
public:
	SasamiFmPlayer();
	~SasamiFmPlayer();
	bool Open(const SasamiSong& song, uint32_t sampleRate, const wchar_t* rhythmDir, int fmMode = 2);
	void Close();
	// raira=1 かつ OPN/OPNA 再生時: %TEMP%\ogg_kbsasami\ にレジスタ dump（BEEP は無効）
	void SetFmMonDump(int enable, const wchar_t* sourcePath);
	int PlayFmMode() const; // 0=BEEP 1=OPN 2=OPNA
	uint32_t Render(int16_t* interleavedStereo, uint32_t frames);
	uint64_t SeekSample(uint64_t sample);
	uint64_t TotalSamples() const { return m_totalSamples; }
	uint64_t CurSample() const { return m_curSample; }
	uint32_t SampleRate() const { return m_hostRate; }
	const char* TitleSjis() const { return m_title; }

private:
	struct Impl;
	uint32_t RenderUnlocked(int16_t* interleavedStereo, uint32_t frames);
	Impl* m;
	uint32_t m_hostRate;
	uint64_t m_totalSamples;
	uint64_t m_curSample;
	char m_title[65];
	std::mutex m_lock;
};
